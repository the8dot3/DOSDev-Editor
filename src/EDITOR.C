/* DE - the editor: the open files, what each key does, and drawing them. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DE.H"

Buffer *files[MAXFILES];
int nfiles, cur, running = 1, overwrite;
int opt_numbers = 1, opt_spaces = 1, opt_indent = 4, opt_tab = 8, opt_auto = 1, opt_fastkeys = 1;

static char msg[160];
static int msg_err;
static char *clip;
static int clip_n, clip_lines;
static char find_text[128], repl_text[128];
static int last_kind, prev_kind, last_l, last_c;   /* undo steps for typing */
static int cut_chain, prev_chain;                  /* Ctrl+K again adds to the cut */
static int dragging;

#define TY 2                    /* the first row of text */
#define B (files[cur])

static int TH(void) { return SR - 4; }

static int gutter(const Buffer *b)
{
    int d = 1, n = b->n;
    if (!opt_numbers)
        return 0;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return (d < 3 ? 3 : d) + 1;
}

/* A line number, right-aligned in w columns, and a space: the gutter. (By
 * hand, as sprintf is slow for something drawn 23 times a key.) */
static void line_number(char *t, int w, int n)
{
    int i = w;
    t[w] = ' ';
    t[w + 1] = 0;
    do {
        t[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n && i > 0);
    while (i > 0)
        t[--i] = ' ';
}

const char *base_name(const char *path)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\'), *c = strrchr(path, ':');
    const char *p = path;
    if (a && a + 1 > p) p = a + 1;
    if (b && b + 1 > p) p = b + 1;
    if (c && c + 1 > p) p = c + 1;
    return p;
}

void ed_message(int error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    msg_err = error;
}

/* ---- columns: bytes and screen positions (tabs take several) ---------------- */

static int next_x(int x, int ch)
{
    return ch == '\t' ? (x / opt_tab + 1) * opt_tab : x + 1;
}

static int x_of(const Line *L, int c)
{
    int i, x = 0;
    for (i = 0; i < c && i < L->len; i++)
        x = next_x(x, L->s[i]);
    return x;
}

static int c_of(const Line *L, int want)
{
    int i, x = 0;
    for (i = 0; i < L->len; i++) {
        int nx = next_x(x, L->s[i]);
        if (nx > want)
            break;
        x = nx;
    }
    return i;
}

static int indent_of(const Line *L)
{
    int i = 0;
    while (i < L->len && (L->s[i] == ' ' || L->s[i] == '\t'))
        i++;
    return i;
}

/* ---- selection ----------------------------------------------------------- */

static int has_sel(const Buffer *b)
{
    return b->sel && (b->sy != b->cy || b->sx != b->cx);
}

static void sel_range(const Buffer *b, int *l1, int *c1, int *l2, int *c2)
{
    if (b->sy < b->cy || (b->sy == b->cy && b->sx < b->cx)) {
        *l1 = b->sy; *c1 = b->sx; *l2 = b->cy; *c2 = b->cx;
    } else {
        *l1 = b->cy; *c1 = b->cx; *l2 = b->sy; *c2 = b->sx;
    }
}

/* The lines a command works on: the selected ones, or the cursor's. */
static void sel_lines(const Buffer *b, int *l1, int *l2)
{
    int c1, c2;
    if (has_sel(b)) {
        sel_range(b, l1, &c1, l2, &c2);
        if (c2 == 0 && *l2 > *l1)
            (*l2)--;
    } else
        *l1 = *l2 = b->cy;
}

static int delete_sel(Buffer *b)
{
    int l1, c1, l2, c2;
    if (!has_sel(b)) {
        b->sel = 0;
        return 0;
    }
    sel_range(b, &l1, &c1, &l2, &c2);
    buf_delete(b, l1, c1, l2, c2);
    b->sel = 0;
    return 1;
}

/* The cursor and the selection's anchor, kept within the text. */
static void sane(Buffer *b)
{
    if (b->cy >= b->n) b->cy = b->n - 1;
    if (b->cy < 0) b->cy = 0;
    if (b->cx > b->ln[b->cy].len) b->cx = b->ln[b->cy].len;
    if (b->cx < 0) b->cx = 0;
    if (b->sy >= b->n) b->sy = b->n - 1;
    if (b->sy < 0) b->sy = 0;
    if (b->sx > b->ln[b->sy].len) b->sx = b->ln[b->sy].len;
    if (b->sx < 0) b->sx = 0;
}

/* ---- undo steps ---------------------------------------------------------- */
/* kind: 0 any edit (a step of its own), 1 typing, 2 backspace, 3 delete.
 * The same kind again, where the last one left off, joins its step. */
static void edit_begin(Buffer *b, int kind)
{
    sane(b);
    if (!has_sel(b))
        b->sel = 0;            /* an empty selection's anchor goes stale */
    if (!(kind && kind == prev_kind && b->cy == last_l && b->cx == last_c))
        buf_group(b);
    last_kind = kind;
}

static void edit_end(Buffer *b)
{
    if (b->cy >= b->n)
        b->cy = b->n - 1;
    if (b->cx > b->ln[b->cy].len)
        b->cx = b->ln[b->cy].len;
    last_l = b->cy;
    last_c = b->cx;
    b->want = x_of(&b->ln[b->cy], b->cx);
}

/* ---- keeping the cursor in view ------------------------------------------ */

static void follow(Buffer *b)
{
    int th = TH(), tw = SC - 2 - gutter(b), x;
    if (b->cy >= b->n) b->cy = b->n - 1;
    if (b->cy < 0) b->cy = 0;
    if (b->cx > b->ln[b->cy].len) b->cx = b->ln[b->cy].len;
    if (b->cx < 0) b->cx = 0;
    if (b->top > b->n - 1) b->top = b->n - 1;
    if (b->cy < b->top) b->top = b->cy;
    if (b->cy >= b->top + th) b->top = b->cy - th + 1;
    if (b->top < 0) b->top = 0;
    x = x_of(&b->ln[b->cy], b->cx);
    if (x < b->left) b->left = x < 8 ? 0 : x - 8;
    if (x >= b->left + tw) b->left = x - tw + 8;
}

static void center(Buffer *b)
{
    b->top = b->cy - TH() / 2;
    if (b->top > b->n - TH()) b->top = b->n - TH();
    if (b->top < 0) b->top = 0;
}

void ed_goto_line(int line)
{
    Buffer *b = B;
    b->cy = line - 1;
    if (b->cy >= b->n) b->cy = b->n - 1;
    if (b->cy < 0) b->cy = 0;
    b->cx = indent_of(&b->ln[b->cy]);
    b->want = x_of(&b->ln[b->cy], b->cx);
    b->sel = 0;
    center(b);
    follow(b);
}

/* Line and column from 1; column 0: where the line's text starts. */
void ed_goto(int line, int col)
{
    Buffer *b = B;
    ed_goto_line(line);
    if (col > 0) {                 /* compilers count bytes: a tab is one */
        b->cx = col - 1 < b->ln[b->cy].len ? col - 1 : b->ln[b->cy].len;
        b->want = x_of(&b->ln[b->cy], b->cx);
        follow(b);
    }
}

/* ---- places to go back to (Alt+Left) -------------------------------------------- */

#define MARKS 32
static struct { Buffer *b; int cy, cx; } marks[MARKS];
static int nmarks;

void ed_mark(void)
{
    Buffer *b = B;
    if (nmarks && marks[nmarks - 1].b == b && marks[nmarks - 1].cy == b->cy)
        return;                 /* there already */
    if (nmarks == MARKS) {
        memmove(&marks[0], &marks[1], (MARKS - 1) * sizeof marks[0]);
        nmarks--;
    }
    marks[nmarks].b = b;
    marks[nmarks].cy = b->cy;
    marks[nmarks].cx = b->cx;
    nmarks++;
}

void ed_back(void)
{
    int i;
    while (nmarks > 0) {
        nmarks--;
        for (i = 0; i < nfiles; i++)
            if (files[i] == marks[nmarks].b) {
                Buffer *b = files[i];
                cur = i;
                b->sel = 0;
                b->cy = marks[nmarks].cy < b->n ? marks[nmarks].cy : b->n - 1;
                b->cx = marks[nmarks].cx;
                if (b->cx > b->ln[b->cy].len)
                    b->cx = b->ln[b->cy].len;
                b->want = x_of(&b->ln[b->cy], b->cx);
                center(b);
                follow(b);
                return;
            }
    }
    ed_message(0, "Nowhere to go back to.");
}

void ed_forget(Buffer *b)
{
    int i, k = 0;
    for (i = 0; i < nmarks; i++)
        if (marks[i].b != b)
            marks[k++] = marks[i];
    nmarks = k;
}

/* Open a file at a line and column, remembering where the cursor was. */
int ed_jump(const char *path, int line, int col)
{
    if (nfiles)
        ed_mark();
    if (ed_open(path, 0))
        return -1;
    ed_goto(line, col);
    return 0;
}

/* The word at (or just before) the cursor. */
const char *ed_word(Buffer *b, char *out, int size)
{
    const Line *L = &b->ln[b->cy];
    int s = b->cx, e = b->cx;
    while (s > 0 && is_word_char(b->lang, (unsigned char)L->s[s - 1]))
        s--;
    while (e < L->len && is_word_char(b->lang, (unsigned char)L->s[e]))
        e++;
    if (e == s || e - s >= size)
        return NULL;
    memcpy(out, L->s + s, e - s);
    out[e - s] = 0;
    return out;
}

/* ---- files ------------------------------------------------------------------ */

void ed_init(void)
{
    nfiles = 0;
    cur = 0;
    running = 1;
}

void ed_new(void)
{
    if (nfiles == MAXFILES) {
        ed_message(1, "%d files are open; close one first (Ctrl-W).", MAXFILES);
        return;
    }
    files[nfiles] = buf_new();
    cur = nfiles++;
}

int ed_same_path(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a == '\\' ? '/' : tolower((unsigned char)*a);
        int y = *b == '\\' ? '/' : tolower((unsigned char)*b);
        if (x != y)
            return 0;
    }
    return *a == *b;
}

int ed_open(const char *path, int line)
{
    Buffer *b;
    char want[DE_PATH], have[DE_PATH];
    int i, r;
    plat_fullpath(path, want);
    for (i = 0; i < nfiles; i++) {
        if (!files[i]->path[0])
            continue;
        plat_fullpath(files[i]->path, have);
        if (ed_same_path(have, want)) {      /* open already, by whatever name */
            cur = i;
            if (line > 0)
                ed_goto_line(line);
            return 0;
        }
    }
    if (nfiles == MAXFILES) {
        ed_message(1, "%d files are open; close one first (Ctrl-W).", MAXFILES);
        return -1;
    }
    b = buf_new();
    r = buf_load(b, path);
    if (r < 0) {
        ed_message(1, "Can't read %s", path);
        buf_free(b);
        return -1;
    }
    /* An empty, untouched untitled file makes way for it. */
    if (nfiles == 1 && !files[0]->path[0] && !buf_modified(files[0]) && files[0]->n == 1 && files[0]->ln[0].len == 0) {
        buf_free(files[0]);
        nfiles = 0;
    }
    files[nfiles] = b;
    cur = nfiles++;
    if (opt_suggest)
        words_build(b);          /* now, while a wait is expected, not at the first word typed */
    msgs_attach(b);
    if (r == 1)
        ed_message(0, "%s is a new file; F2 saves it.", base_name(path));
    if (line > 0)
        ed_goto_line(line);
    return 0;
}

static int save_file(int i, int as)
{
    Buffer *b = files[i];
    char p[DE_PATH];
    int r;
    strcpy(p, b->path);
    if (as || !b->path[0]) {
        FILE *f;
        if (!input_box(as ? "Save as" : "Save", "File name", p, sizeof p) || !p[0])
            return 0;
        if (!ed_same_path(p, b->path) && (f = fopen(p, "rb")) != NULL) {
            static const char *const yn[] = { "~Y~es", "~N~o" };
            fclose(f);
            if (msg_box("Save as", "That file exists. Replace it?", yn, 2, 0) != 0)
                return 0;
        }
    }
    r = buf_save(b, p);
    if (r == 0) {
        ed_message(0, "Saved %s (%d line%s).", base_name(p), b->n, b->n == 1 ? "" : "s");
        return 1;
    }
    if (r == -2)
        ed_message(1, "Couldn't rename it: your text is in the .$$$ file next to %s.", base_name(p));
    else
        ed_message(1, "Couldn't save %s.", p);
    return 0;
}

/* Every changed file that has a name; returns how many couldn't be. */
int ed_save_all(void)
{
    int i, bad = 0, was = cur;
    for (i = 0; i < nfiles; i++)
        if (buf_modified(files[i]) && files[i]->path[0]) {
            cur = i;
            if (!save_file(i, 0))
                bad++;
        }
    cur = was;
    return bad;
}

/* Changes not saved: ask. Returns 0 to stop what was being done. */
static int ask_save(int i)
{
    static const char *const ync[] = { "~Y~es", "~N~o", "Cancel" };
    char q[DE_PATH + 40];
    int a;
    if (!buf_modified(files[i]))
        return 1;
    cur = i;
    sprintf(q, "Save the changes to %s?", files[i]->path[0] ? base_name(files[i]->path) : "this new file");
    a = msg_box("Save", q, ync, 3, 0);
    if (a == 0)
        return save_file(i, 0);
    return a == 1;
}

static void close_file(int i)
{
    if (!ask_save(i))
        return;
    buf_free(files[i]);
    memmove(&files[i], &files[i + 1], (nfiles - i - 1) * sizeof files[0]);
    nfiles--;
    if (cur >= nfiles)
        cur = nfiles - 1;
    if (nfiles == 0)
        ed_new();
}

/* ---- moving ------------------------------------------------------------------ */

static int char_class(int c)
{
    if (c == ' ' || c == '\t')
        return 0;
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 128 ? 1 : 2;
}

static int word_left_of(const Line *L, int c)
{
    int k;
    while (c > 0 && char_class(L->s[c - 1]) == 0)
        c--;
    if (c > 0) {
        k = char_class(L->s[c - 1]);
        while (c > 0 && char_class(L->s[c - 1]) == k)
            c--;
    }
    return c;
}

static int word_right_of(const Line *L, int c)
{
    int k;
    if (c < L->len) {
        k = char_class(L->s[c]);
        while (c < L->len && char_class(L->s[c]) == k)
            c++;
    }
    while (c < L->len && char_class(L->s[c]) == 0)
        c++;
    return c;
}

static int move(Buffer *b, int k)
{
    int base = KBASE(k), ctrl = k & KM_CTRL, shift = k & KM_SHIFT, th = TH();
    Line *L;
    if (base < DK_UP || base > DK_PGDN)
        return 0;
    if (ctrl && !shift && (base == DK_UP || base == DK_DOWN)) {
        /* Ctrl+Up/Down scroll, leaving the cursor where it is if it can. */
        b->top += base == DK_UP ? -1 : 1;
        if (b->top > b->n - 1) b->top = b->n - 1;
        if (b->top < 0) b->top = 0;
        if (b->cy < b->top) b->cy = b->top;
        if (b->cy >= b->top + th) b->cy = b->top + th - 1;
        b->cx = c_of(&b->ln[b->cy], b->want);
        return 1;
    }
    if (shift) {
        if (!b->sel) {
            b->sel = 1;
            b->sy = b->cy;
            b->sx = b->cx;
        }
    } else if (b->sel) {
        int l1, c1, l2, c2;
        if (has_sel(b) && !ctrl && (base == DK_LEFT || base == DK_RIGHT)) {
            sel_range(b, &l1, &c1, &l2, &c2);
            b->cy = base == DK_LEFT ? l1 : l2;
            b->cx = base == DK_LEFT ? c1 : c2;
            b->sel = 0;
            b->want = x_of(&b->ln[b->cy], b->cx);
            return 1;
        }
        b->sel = 0;
    }
    L = &b->ln[b->cy];
    switch (base) {
    case DK_LEFT:
        if (ctrl && b->cx > 0)
            b->cx = word_left_of(L, b->cx);
        else if (b->cx > 0)
            b->cx--;
        else if (b->cy > 0) {
            b->cy--;
            b->cx = b->ln[b->cy].len;
        }
        b->want = x_of(&b->ln[b->cy], b->cx);
        break;
    case DK_RIGHT:
        if (ctrl && b->cx < L->len)
            b->cx = word_right_of(L, b->cx);
        else if (b->cx < L->len)
            b->cx++;
        else if (b->cy < b->n - 1) {
            b->cy++;
            b->cx = 0;
        }
        b->want = x_of(&b->ln[b->cy], b->cx);
        break;
    case DK_UP:
    case DK_DOWN:
        if (base == DK_UP && b->cy > 0)
            b->cy--;
        else if (base == DK_DOWN && b->cy < b->n - 1)
            b->cy++;
        b->cx = c_of(&b->ln[b->cy], b->want);
        break;
    case DK_PGUP:
    case DK_PGDN:
        if (ctrl) {
            b->cy = base == DK_PGUP ? 0 : b->n - 1;
            b->cx = base == DK_PGUP ? 0 : b->ln[b->cy].len;
            b->want = x_of(&b->ln[b->cy], b->cx);
            break;
        }
        b->cy += base == DK_PGUP ? -(th - 1) : th - 1;
        b->top += base == DK_PGUP ? -(th - 1) : th - 1;
        if (b->cy < 0) b->cy = 0;
        if (b->cy > b->n - 1) b->cy = b->n - 1;
        if (b->top > b->n - th) b->top = b->n - th;
        if (b->top < 0) b->top = 0;
        b->cx = c_of(&b->ln[b->cy], b->want);
        break;
    case DK_HOME:
        if (ctrl) {
            b->cy = 0;
            b->cx = 0;
        } else {
            int ind = indent_of(L);
            b->cx = b->cx == ind ? 0 : ind;     /* the text first, then column 1 */
        }
        b->want = x_of(&b->ln[b->cy], b->cx);
        break;
    case DK_END:
        if (ctrl)
            b->cy = b->n - 1;
        b->cx = b->ln[b->cy].len;
        b->want = x_of(&b->ln[b->cy], b->cx);
        break;
    }
    return 1;
}

/* ---- editing -------------------------------------------------------------- */

static int indent_unit(char *out)
{
    if (!opt_spaces) {
        out[0] = '\t';
        return 1;
    }
    memset(out, ' ', opt_indent);
    return opt_indent;
}

static int ends_block(const Buffer *b, const Line *L, int upto)
{
    int j = upto - 1;
    while (j >= 0 && (L->s[j] == ' ' || L->s[j] == '\t'))
        j--;
    if (j < 0)
        return 0;
    if (L->s[j] == '{' && (b->lang == L_C || b->lang == L_MAKE))
        return 1;
    if (b->lang == L_PASCAL && j >= 4) {
        const char *w = L->s + j - 4;
        if (tolower((unsigned char)w[0]) == 'b' && tolower((unsigned char)w[1]) == 'e' && tolower((unsigned char)w[2]) == 'g'
            && tolower((unsigned char)w[3]) == 'i' && tolower((unsigned char)w[4]) == 'n' && (j == 4 || !isalnum((unsigned char)w[-1])))
            return 1;
    }
    return 0;
}

static void type_char(Buffer *b, int ch)
{
    char c = (char)ch;
    Line *L;
    edit_begin(b, 1);
    delete_sel(b);
    L = &b->ln[b->cy];
    if (ch == ' ' && b->cx > 0 && L->s[b->cx - 1] != ' ')
        buf_group(b);            /* a word at a time */
    /* A closing brace on a line of its own steps back one level. */
    if (ch == '}' && opt_auto && b->lang == L_C && b->cx > 0 && indent_of(L) >= b->cx) {
        int rm = 0;
        if (L->s[b->cx - 1] == '\t')
            rm = 1;
        else
            while (rm < opt_indent && b->cx - rm > 0 && L->s[b->cx - rm - 1] == ' ')
                rm++;
        if (rm)
            buf_delete(b, b->cy, b->cx - rm, b->cy, b->cx);
        L = &b->ln[b->cy];
    }
    if (overwrite && b->cx < L->len)
        buf_delete(b, b->cy, b->cx, b->cy, b->cx + 1);
    buf_insert(b, b->cy, b->cx, &c, 1);
    edit_end(b);
    if (is_word_char(b->lang, ch))
        sug_want(b);
}

static void newline(Buffer *b)
{
    char ind[300];
    int n = 0, i;
    Line *L;
    edit_begin(b, 0);
    delete_sel(b);
    L = &b->ln[b->cy];
    ind[n++] = '\n';
    if (opt_auto) {
        for (i = 0; i < L->len && i < b->cx && (L->s[i] == ' ' || L->s[i] == '\t') && n < 200; i++)
            ind[n++] = L->s[i];
        if (ends_block(b, L, b->cx))
            n += indent_unit(ind + n);
    }
    /* Spaces before the cursor on an empty line were only indentation. */
    if (opt_auto && b->cx > 0 && indent_of(L) >= L->len && b->cx == L->len)
        buf_delete(b, b->cy, 0, b->cy, b->cx);
    buf_insert(b, b->cy, b->cx, ind, n);
    edit_end(b);
}

static void indent_lines(Buffer *b, int back)
{
    int l1, l2, l, had = has_sel(b), delta = 0, keepx = b->cx;
    char unit[16];
    int un = indent_unit(unit);
    sel_lines(b, &l1, &l2);
    edit_begin(b, 0);
    for (l = l1; l <= l2; l++) {
        Line *L = &b->ln[l];
        if (!back) {
            if (L->len == 0)
                continue;
            buf_insert(b, l, 0, unit, un);
            delta = un;
        } else {
            int rm = 0;
            if (L->len && L->s[0] == '\t')
                rm = 1;
            else
                while (rm < opt_indent && rm < L->len && L->s[rm] == ' ')
                    rm++;
            if (rm)
                buf_delete(b, l, 0, l, rm);
            delta = -rm;
        }
    }
    if (had) {
        b->sel = 1;
        b->sy = l1;
        b->sx = 0;
        b->cy = l2;
        b->cx = b->ln[l2].len;
    } else {
        b->cy = l1;
        b->cx = keepx + delta;
        if (b->cx < 0) b->cx = 0;
    }
    edit_end(b);
}

static void tab(Buffer *b)
{
    char sp[16];
    int n;
    if (has_sel(b) && b->sy != b->cy) {
        indent_lines(b, 0);
        return;
    }
    edit_begin(b, 0);
    delete_sel(b);
    if (opt_spaces) {
        n = opt_indent - x_of(&b->ln[b->cy], b->cx) % opt_indent;
        memset(sp, ' ', n);
    } else {
        sp[0] = '\t';
        n = 1;
    }
    buf_insert(b, b->cy, b->cx, sp, n);
    edit_end(b);
}

static void backspace(Buffer *b, int word)
{
    Line *L = &b->ln[b->cy];
    edit_begin(b, word ? 0 : 2);
    if (delete_sel(b)) {
        edit_end(b);
        return;
    }
    if (b->cx == 0) {
        if (b->cy > 0)
            buf_delete(b, b->cy - 1, b->ln[b->cy - 1].len, b->cy, 0);
    } else if (word) {
        buf_delete(b, b->cy, word_left_of(L, b->cx), b->cy, b->cx);
    } else if (opt_spaces && indent_of(L) >= b->cx && L->s[b->cx - 1] == ' ') {
        /* In the indentation: back to the previous indent stop. */
        int x = x_of(L, b->cx), target = (x - 1) / opt_indent * opt_indent, rm = 0;
        while (rm < x - target && b->cx - rm > 0 && L->s[b->cx - rm - 1] == ' ')
            rm++;
        buf_delete(b, b->cy, b->cx - rm, b->cy, b->cx);
    } else
        buf_delete(b, b->cy, b->cx - 1, b->cy, b->cx);
    edit_end(b);
    if (!word)
        sug_want(b);
}

static void delete_key(Buffer *b, int word)
{
    Line *L = &b->ln[b->cy];
    edit_begin(b, word ? 0 : 3);
    if (delete_sel(b)) {
        edit_end(b);
        return;
    }
    if (b->cx >= L->len) {
        if (b->cy < b->n - 1)
            buf_delete(b, b->cy, L->len, b->cy + 1, 0);
    } else if (word)
        buf_delete(b, b->cy, b->cx, b->cy, word_right_of(L, b->cx));
    else
        buf_delete(b, b->cy, b->cx, b->cy, b->cx + 1);
    edit_end(b);
}

/* A suggestion taken: the word before the cursor becomes w. */
void ed_put_word(int start, const char *w)
{
    Buffer *b = B;
    edit_begin(b, 0);
    b->sel = 0;
    buf_delete(b, b->cy, start, b->cy, b->cx);
    buf_insert(b, b->cy, start, w, (int)strlen(w));
    edit_end(b);
    follow(b);
}

/* ---- the clipboard and whole lines ------------------------------------------ */

static void set_clip(char *t, int n, int lines)
{
    free(clip);
    clip = t;
    clip_n = n;
    clip_lines = lines;
}

/* Lines l1..l2 as text, each with its line break. */
static char *lines_text(const Buffer *b, int l1, int l2, int *n)
{
    int m;
    char *t = buf_text(b, l1, 0, l2, b->ln[l2].len, &m);
    t = xrealloc(t, m + 2);
    t[m++] = '\n';
    t[m] = 0;
    *n = m;
    return t;
}

static void copy(Buffer *b)
{
    int l1, c1, l2, c2, n;
    char *t;
    if (has_sel(b)) {
        sel_range(b, &l1, &c1, &l2, &c2);
        t = buf_text(b, l1, c1, l2, c2, &n);
        set_clip(t, n, 0);
        ed_message(0, "Copied %d character%s.", n, n == 1 ? "" : "s");
    } else {
        t = lines_text(b, b->cy, b->cy, &n);
        set_clip(t, n, 1);
        ed_message(0, "Copied the line.");
    }
}

static void delete_lines(Buffer *b, int l1, int l2)
{
    if (l2 < b->n - 1)
        buf_delete(b, l1, 0, l2 + 1, 0);
    else if (l1 > 0)
        buf_delete(b, l1 - 1, b->ln[l1 - 1].len, l2, b->ln[l2].len);
    else
        buf_delete(b, l1, 0, l2, b->ln[l2].len);
    if (b->cy > b->n - 1)
        b->cy = b->n - 1;
    b->cx = indent_of(&b->ln[b->cy]);
}

static void cut_line(Buffer *b)
{
    int l1, l2, n;
    char *t;
    sel_lines(b, &l1, &l2);
    t = lines_text(b, l1, l2, &n);
    if (prev_chain && clip && clip_lines) {
        clip = xrealloc(clip, clip_n + n + 1);
        memcpy(clip + clip_n, t, n + 1);
        clip_n += n;
        free(t);
    } else
        set_clip(t, n, 1);
    edit_begin(b, 0);
    b->sel = 0;
    delete_lines(b, l1, l2);
    b->cy = l1 < b->n ? l1 : b->n - 1;
    b->cx = indent_of(&b->ln[b->cy]);
    edit_end(b);
    cut_chain = 1;
}

static void paste(Buffer *b)
{
    int keep = b->cx;
    if (!clip) {
        ed_message(1, "Nothing to paste: copy something first (Ctrl-C).");
        return;
    }
    edit_begin(b, 0);
    delete_sel(b);
    if (clip_lines) {
        /* Whole lines go in above the cursor's line. */
        buf_insert(b, b->cy, 0, clip, clip_n);
        b->cx = keep;
    } else
        buf_insert(b, b->cy, b->cx, clip, clip_n);
    edit_end(b);
}

static void duplicate(Buffer *b)
{
    int l1, l2, n, keep = b->cx;
    char *t;
    sel_lines(b, &l1, &l2);
    t = lines_text(b, l1, l2, &n);
    edit_begin(b, 0);
    if (l2 < b->n - 1)
        buf_insert(b, l2 + 1, 0, t, n);
    else {
        memmove(t + 1, t, n - 1);
        t[0] = '\n';
        buf_insert(b, l2, b->ln[l2].len, t, n);
    }
    free(t);
    b->sel = 0;
    b->cy = l2 + 1 + (b->cy - l1);
    b->cx = keep;
    edit_end(b);
}

static void move_lines(Buffer *b, int dir)
{
    int l1, l2, from, to, n1, n2;
    char *block, *other, *t;
    sel_lines(b, &l1, &l2);
    if ((dir < 0 && l1 == 0) || (dir > 0 && l2 >= b->n - 1))
        return;
    from = dir < 0 ? l1 - 1 : l1;
    to = dir < 0 ? l2 : l2 + 1;
    block = buf_text(b, l1, 0, l2, b->ln[l2].len, &n1);
    other = buf_text(b, dir < 0 ? l1 - 1 : l2 + 1, 0, dir < 0 ? l1 - 1 : l2 + 1, b->ln[dir < 0 ? l1 - 1 : l2 + 1].len, &n2);
    t = xrealloc(NULL, n1 + n2 + 2);
    memcpy(t, dir < 0 ? block : other, dir < 0 ? n1 : n2);
    t[dir < 0 ? n1 : n2] = '\n';
    memcpy(t + (dir < 0 ? n1 : n2) + 1, dir < 0 ? other : block, dir < 0 ? n2 : n1);
    {
        int sel = b->sel, sy = b->sy, sx = b->sx, cy = b->cy, cx = b->cx;
        edit_begin(b, 0);
        buf_delete(b, from, 0, to, b->ln[to].len);
        buf_insert(b, from, 0, t, n1 + n2 + 1);
        b->sel = sel;
        b->sy = sy + dir;
        b->sx = sx;
        b->cy = cy + dir;
        b->cx = cx;
        edit_end(b);
    }
    free(block);
    free(other);
    free(t);
}

static int starts_with(const char *s, int n, const char *p, int nocase)
{
    int i;
    for (i = 0; p[i]; i++) {
        if (i >= n)
            return 0;
        if (nocase ? tolower((unsigned char)s[i]) != tolower((unsigned char)p[i]) : s[i] != p[i])
            return 0;
    }
    return 1;
}

static void toggle_comment(Buffer *b)
{
    const char *p = lang_comment(b->lang);
    int l1, l2, l, plen, all = 1, any = 0, minind = 1 << 30, cx = b->cx, sel = has_sel(b);
    int nocase = b->lang == L_BATCH;
    if (!p) {
        ed_message(1, "DE doesn't know how comments look in this kind of file.");
        return;
    }
    plen = (int)strlen(p);
    sel_lines(b, &l1, &l2);
    for (l = l1; l <= l2; l++) {
        Line *L = &b->ln[l];
        int ind = indent_of(L);
        if (ind == L->len)
            continue;
        any = 1;
        if (!starts_with(L->s + ind, L->len - ind, p, nocase))
            all = 0;
        if (ind < minind)
            minind = ind;
    }
    if (!any)
        return;
    edit_begin(b, 0);
    for (l = l1; l <= l2; l++) {
        Line *L = &b->ln[l];
        int ind = indent_of(L);
        if (ind == L->len)
            continue;
        if (all) {
            int n = plen + (ind + plen < L->len && L->s[ind + plen] == ' ');
            buf_delete(b, l, ind, l, ind + n);
        } else {
            char t[8];
            sprintf(t, "%s ", p);
            buf_insert(b, l, minind, t, plen + 1);
        }
    }
    b->cy = l2;
    if (sel) {
        b->sel = 1;
        b->sy = l1;
        b->sx = 0;
        b->cx = b->ln[l2].len;
    } else {
        b->cy = l1;
        b->cx = cx;
    }
    edit_end(b);
}

/* ---- find and replace ------------------------------------------------------- */

static int has_upper(const char *s)
{
    for (; *s; s++)
        if (isupper((unsigned char)*s))
            return 1;
    return 0;
}

static int match_at(const Line *L, int i, const char *nd, int nn, int cs)
{
    int k;
    if (i + nn > L->len)
        return 0;
    for (k = 0; k < nn; k++) {
        int a = (unsigned char)L->s[i + k], c = (unsigned char)nd[k];
        if (cs ? a != c : tolower(a) != tolower(c))
            return 0;
    }
    return 1;
}

/* Find nd from (l, c): forward, or backward from before it. Lower-case text
 * matches either case; any capital makes the case matter. */
static int find(const Buffer *b, const char *nd, int forward, int wrap, int l, int c, int *fl, int *fc, int *wrapped)
{
    int nn = (int)strlen(nd), cs = has_upper(nd), k, i;
    *wrapped = 0;
    if (!nn)
        return 0;
    for (k = 0; k <= b->n; k++) {
        int line = forward ? l + k : l - k;
        const Line *L;
        if (line >= b->n || line < 0) {
            if (!wrap)
                return 0;
            line = forward ? line - b->n : line + b->n;
            *wrapped = 1;
        }
        L = &b->ln[line];
        if (forward) {
            for (i = k == 0 ? c : 0; i + nn <= L->len; i++)
                if (match_at(L, i, nd, nn, cs)) {
                    *fl = line;
                    *fc = i;
                    return 1;
                }
        } else {
            for (i = (k == 0 ? c : L->len) - 1; i >= 0; i--)
                if (match_at(L, i, nd, nn, cs)) {
                    *fl = line;
                    *fc = i;
                    return 1;
                }
        }
    }
    return 0;
}

static void select_found(Buffer *b, int l, int c, int n)
{
    b->sel = 1;
    b->sy = l;
    b->sx = c;
    b->cy = l;
    b->cx = c + n;
    b->want = x_of(&b->ln[l], b->cx);
    if (b->cy < b->top || b->cy >= b->top + TH())
        center(b);
}

static void find_again(Buffer *b, int forward)
{
    int l1, c1, l2, c2, fl, fc, wrapped;
    if (!find_text[0]) {
        ed_command(C_FIND);
        return;
    }
    if (has_sel(b))
        sel_range(b, &l1, &c1, &l2, &c2);
    else {
        l1 = l2 = b->cy;
        c1 = c2 = b->cx;
    }
    if (find(b, find_text, forward, 1, forward ? l2 : l1, forward ? c2 : c1, &fl, &fc, &wrapped)) {
        select_found(b, fl, fc, (int)strlen(find_text));
        if (wrapped)
            ed_message(0, forward ? "Found it after going back to the top." : "Found it after going round from the end.");
    } else
        ed_message(1, "Not found: %s", find_text);
}

/* What's selected on one line, as a starting point for Find. */
static void selection_word(Buffer *b, char *out, int size)
{
    int l1, c1, l2, c2, n;
    if (has_sel(b)) {
        sel_range(b, &l1, &c1, &l2, &c2);
        if (l1 == l2 && c2 - c1 < size) {
            n = c2 - c1;
            memcpy(out, b->ln[l1].s + c1, n);
            out[n] = 0;
        }
    }
}

static void find_cmd(Buffer *b)
{
    char t[128];
    strcpy(t, find_text);
    selection_word(b, t, sizeof t);
    if (!input_box("Find", "Text to find (lower case finds any case)", t, sizeof t) || !t[0])
        return;
    strcpy(find_text, t);
    if (has_sel(b)) {               /* find from where the selection starts */
        int l1, c1, l2, c2;
        sel_range(b, &l1, &c1, &l2, &c2);
        b->sel = 0;
        b->cy = l1;
        b->cx = c1;
    }
    find_again(b, 1);
}

static void replace_cmd(Buffer *b)
{
    static const char *const ask[] = { "~Y~es", "~N~o", "~A~ll", "Stop" };
    char t[128];
    int l, c, fl, fc, wrapped, nn, rn, all = 0, count = 0;
    strcpy(t, find_text);
    selection_word(b, t, sizeof t);
    if (!input_box("Replace", "Text to find", t, sizeof t) || !t[0])
        return;
    strcpy(find_text, t);
    if (!input_box("Replace", "Replace it with", repl_text, sizeof repl_text))
        return;
    nn = (int)strlen(find_text);
    rn = (int)strlen(repl_text);
    if (has_sel(b)) {
        int l2, c2;
        sel_range(b, &l, &c, &l2, &c2);
    } else {
        l = b->cy;
        c = b->cx;
    }
    edit_begin(b, 0);
    while (find(b, find_text, 1, 0, l, c, &fl, &fc, &wrapped)) {
        if (!all) {
            int a;
            select_found(b, fl, fc, nn);
            follow(b);
            a = msg_box("Replace", "Replace this one?", ask, 4, b->cy - b->top < TH() / 2 ? 2 : 1);
            if (a == 3 || a < 0)
                break;
            if (a == 1) {
                l = fl;
                c = fc + nn;
                continue;
            }
            if (a == 2)
                all = 1;
        }
        buf_delete(b, fl, fc, fl, fc + nn);
        buf_insert(b, fl, fc, repl_text, rn);
        count++;
        l = fl;
        c = fc + rn;
    }
    b->sel = 0;
    edit_end(b);
    ed_message(0, "%d replaced. Ctrl-Z takes %s back.", count, count == 1 ? "it" : "them all");
}

/* The bracket that pairs with the one at (l, c), up to `most` lines away. */
static int bracket_match(const Buffer *b, int l, int c, int most, int *ml, int *mc)
{
    static const char pairs[] = "()[]{}";
    const char *p;
    int depth = 0, dir, lines = 0;
    char open, close;
    if (c >= b->ln[l].len || !b->ln[l].s[c] || !(p = strchr(pairs, b->ln[l].s[c])))
        return 0;
    open = p[0];
    if ((p - pairs) % 2 == 0) {
        close = p[1];
        dir = 1;
    } else {
        close = p[-1];
        dir = -1;
    }
    for (;;) {
        const Line *L = &b->ln[l];
        for (; c >= 0 && c < L->len; c += dir) {
            if (L->s[c] == open)
                depth++;
            else if (L->s[c] == close && --depth == 0) {
                *ml = l;
                *mc = c;
                return 1;
            }
        }
        if (++lines > most)
            return 0;
        l += dir;
        if (l < 0 || l >= b->n)
            return 0;
        c = dir > 0 ? 0 : b->ln[l].len - 1;
    }
}

static int cursor_bracket(const Buffer *b, int most, int *bl, int *bc, int *ml, int *mc)
{
    if (bracket_match(b, b->cy, b->cx, most, ml, mc)) {
        *bl = b->cy;
        *bc = b->cx;
        return 1;
    }
    if (b->cx > 0 && bracket_match(b, b->cy, b->cx - 1, most, ml, mc)) {
        *bl = b->cy;
        *bc = b->cx - 1;
        return 1;
    }
    return 0;
}

/* ---- commands ---------------------------------------------------------------- */

void ed_command(int cmd)
{
    Buffer *b = B;
    int i;
    char t[DE_PATH];
    switch (cmd) {
    case C_NONE: return;
    case C_NEW: ed_new(); break;
    case C_OPEN:
        if (open_box(t))
            ed_open(t, 0);
        break;
    case C_SAVE: save_file(cur, 0); break;
    case C_SAVEAS: save_file(cur, 1); break;
    case C_SAVEALL:
        for (i = 0; i < nfiles; i++)
            if (buf_modified(files[i]) && !save_file(i, 0))
                break;
        break;
    case C_CLOSE: close_file(cur); break;
    case C_EXIT:
        for (i = 0; i < nfiles; i++)
            if (!ask_save(i))
                return;
        running = 0;
        return;
    case C_UNDO:
        if (!buf_undo(b))
            ed_message(1, "Nothing to undo.");
        break;
    case C_REDO:
        if (!buf_redo(b))
            ed_message(1, "Nothing to redo.");
        break;
    case C_CUT:
        if (has_sel(b)) {
            copy(b);
            edit_begin(b, 0);
            delete_sel(b);
            edit_end(b);
        } else
            cut_line(b);
        break;
    case C_COPY: copy(b); break;
    case C_PASTE: paste(b); break;
    case C_SELALL:
        b->sel = 1;
        b->sy = 0;
        b->sx = 0;
        b->cy = b->n - 1;
        b->cx = b->ln[b->cy].len;
        break;
    case C_CUTLINE: cut_line(b); break;
    case C_DUPLINE: duplicate(b); break;
    case C_LINEUP: move_lines(b, -1); break;
    case C_LINEDOWN: move_lines(b, 1); break;
    case C_COMMENT: toggle_comment(b); break;
    case C_INDENT: indent_lines(b, 0); break;
    case C_UNINDENT: indent_lines(b, 1); break;
    case C_FIND: find_cmd(b); break;
    case C_FINDNEXT: find_again(b, 1); break;
    case C_FINDPREV: find_again(b, 0); break;
    case C_REPLACE: replace_cmd(b); break;
    case C_GOTO:
        sprintf(t, "%d", b->cy + 1);
        if (input_box("Go to line", "Line number", t, 12) && atoi(t) > 0)
            ed_goto_line(atoi(t));
        break;
    case C_MATCH: {
        int bl, bc, ml, mc;
        if (cursor_bracket(b, 2000, &bl, &bc, &ml, &mc)) {
            b->sel = 0;
            b->cy = ml;
            b->cx = mc;
            b->want = x_of(&b->ln[ml], mc);
        } else
            ed_message(1, "The cursor isn't at a bracket that has a pair.");
        break;
    }
    case C_NEXT: cur = (cur + 1) % nfiles; break;
    case C_PREV: cur = (cur + nfiles - 1) % nfiles; break;
    case C_NUMBERS: opt_numbers = !opt_numbers; break;
    case C_SPACES: opt_spaces = !opt_spaces; break;
    case C_INDENTSIZE: opt_indent = opt_indent == 2 ? 4 : opt_indent == 4 ? 8 : 2; break;
    case C_TABSIZE: opt_tab = opt_tab == 8 ? 4 : 8; break;
    case C_AUTO: opt_auto = !opt_auto; break;
    case C_THEME: theme_set((theme + 1) % NTHEMES); break;
    case C_SAVEOPTS:
        if (config_save() == 0)
            ed_message(0, "Options saved in %s.", cfg_path);
        else
            ed_message(1, "Couldn't save the options in %s.", cfg_path);
        break;
    case C_HELP: help_manual(); break;
    case C_REFERENCE: help_reference(""); break;
    case C_LOOKUP: help_lookup(b); break;
    case C_COMPLETE: sug_popup(b); break;
    case C_SUGGEST: opt_suggest = !opt_suggest; break;
    case C_FASTKEYS: opt_fastkeys = !opt_fastkeys; plat_repeat(opt_fastkeys); break;
    case C_BUILD: proj_build(0); break;
    case C_RUN: proj_build(1); break;
    case C_DEBUG: proj_debug(); break;
    case C_USERSCREEN: plat_user_screen(); break;
    case C_MESSAGES: msgs_list(); break;
    case C_NEXTMSG: msgs_next(1); break;
    case C_PREVMSG: msgs_next(-1); break;
    case C_COMMANDS: proj_commands(); break;
    case C_FINDFILES: find_in_files(); break;
    case C_SYMBOLS: sym_list(); break;
    case C_DEFINITION: sym_definition(); break;
    case C_BACK: ed_back(); break;
    case C_ABOUT: {
        static const char *const ok[] = { "OK" };
        msg_box("About DE", DE_NAME " " DE_VERSION "\n\nA colour text editor for DOS programmers, written in C and built with DOSDev's DJGPP.\n\nF1 lists every key.", ok, 1, 0);
        break;
    }
    default:
        if (cmd >= C_FILE1 && cmd < C_FILE1 + nfiles)
            cur = cmd - C_FILE1;
        break;
    }
    follow(B);
}

/* ---- keys ---------------------------------------------------------------------- */

static const struct { int key, cmd; } shortcuts[] = {
    { KM_CTRL | 'N', C_NEW }, { KM_CTRL | 'O', C_OPEN }, { DK_F2, C_SAVE }, { KM_CTRL | 'S', C_SAVE },
    { KM_CTRL | 'W', C_CLOSE }, { KM_ALT | 'X', C_EXIT }, { KM_CTRL | 'Q', C_EXIT },
    { KM_CTRL | 'Z', C_UNDO }, { KM_CTRL | KM_SHIFT | 'Z', C_REDO }, { KM_CTRL | 'Y', C_REDO },
    { KM_CTRL | 'X', C_CUT }, { KM_SHIFT | DK_DEL, C_CUT }, { KM_CTRL | 'C', C_COPY }, { KM_CTRL | DK_INS, C_COPY },
    { KM_CTRL | 'V', C_PASTE }, { KM_SHIFT | DK_INS, C_PASTE }, { KM_CTRL | 'A', C_SELALL },
    { KM_CTRL | 'K', C_CUTLINE }, { KM_CTRL | 'D', C_DUPLINE }, { KM_ALT | DK_UP, C_LINEUP },
    { KM_ALT | DK_DOWN, C_LINEDOWN }, { KM_ALT | ';', C_COMMENT }, { KM_CTRL | 'F', C_FIND },
    { DK_F3, C_FINDNEXT }, { KM_SHIFT | DK_F3, C_FINDPREV }, { KM_CTRL | 'R', C_REPLACE },
    { KM_CTRL | 'G', C_GOTO }, { KM_CTRL | 'B', C_MATCH }, { DK_F6, C_NEXT }, { KM_CTRL | DK_TAB, C_NEXT },
    { KM_SHIFT | DK_F6, C_PREV }, { DK_F1, C_HELP }, { KM_SHIFT | DK_F1, C_REFERENCE },
    { KM_CTRL | DK_F1, C_LOOKUP }, { KM_CTRL | ' ', C_COMPLETE }, { KM_ALT | '/', C_COMPLETE },
    { DK_F9, C_BUILD }, { KM_CTRL | DK_F9, C_RUN }, { KM_ALT | DK_F5, C_USERSCREEN }, { DK_F7, C_MESSAGES },
    { DK_F8, C_NEXTMSG }, { KM_SHIFT | DK_F8, C_PREVMSG }, { KM_CTRL | KM_SHIFT | 'F', C_FINDFILES },
    { KM_CTRL | 'L', C_SYMBOLS }, { DK_F12, C_DEFINITION }, { KM_ALT | DK_LEFT, C_BACK }, { 0, 0 }
};

static int shortcut(int k)
{
    int i;
    for (i = 0; shortcuts[i].key; i++)
        if (shortcuts[i].key == k)
            return shortcuts[i].cmd;
    return C_NONE;
}

/* The status line's keys, where they are, for the mouse. */
static struct { int from, to, cmd; } hints[8];
static int nhints;

static void mouse_key(Buffer *b)
{
    int r = mouse.row, c = mouse.col, th = TH(), tx = 1 + gutter(b), i;
    if (mouse.event == M_PRESS) {
        dragging = 0;
        if (r == 0) {
            int m = menu_at(c);
            if (m >= 0)
                ed_command(menu_run(m));
            return;
        }
        if (r == SR - 1) {
            for (i = 0; i < nhints; i++)
                if (c >= hints[i].from && c < hints[i].to)
                    ed_command(hints[i].cmd == C_NONE ? menu_run(0) : hints[i].cmd);
            return;
        }
        if (c == SC - 1 && r >= TY && r < TY + th) {
            if (r == TY)
                move(b, DK_UP);
            else if (r == TY + th - 1)
                move(b, DK_DOWN);
            else {
                b->sel = 0;
                b->cy = (r - TY - 1) * (b->n - 1) / (th - 3 > 0 ? th - 3 : 1);
                if (b->cy > b->n - 1) b->cy = b->n - 1;
                b->cx = 0;
                center(b);
            }
            follow(b);
            return;
        }
        if (r >= TY && r < TY + th && c >= 1 && c < SC - 1) {
            int l = b->top + r - TY, x = b->left + (c >= tx ? c - tx : 0);
            if (l >= b->n)
                l = b->n - 1;
            if (plat_shift()) {
                if (!b->sel) {
                    b->sel = 1;
                    b->sy = b->cy;
                    b->sx = b->cx;
                }
            } else {
                b->sel = 1;
                b->sy = l;
                b->sx = c_of(&b->ln[l], x);
            }
            b->cy = l;
            b->cx = c_of(&b->ln[l], x);
            b->want = x;
            dragging = 1;
        }
    } else if (mouse.event == M_DRAG && dragging) {
        int rr = r, l, x;
        if (rr < TY) {
            if (b->top > 0) b->top--;
            rr = TY;
        }
        if (rr >= TY + th) {
            if (b->top + th < b->n) b->top++;
            rr = TY + th - 1;
        }
        l = b->top + rr - TY;
        if (l >= b->n) l = b->n - 1;
        x = b->left + (c >= tx ? c - tx : 0);
        b->cy = l;
        b->cx = c_of(&b->ln[l], x);
        b->want = x;
    } else if (mouse.event == M_RELEASE)
        dragging = 0;
}

void ed_key(int k)
{
    Buffer *b = B;
    int base = KBASE(k), mods = k & KM_ALL, cmd;
    sane(b);
    prev_kind = last_kind;
    last_kind = 0;
    prev_chain = cut_chain;
    cut_chain = 0;
    if (k != DK_MOUSE)
        msg[0] = 0;
    if (k == DK_TAB && sug_take(b))
        return;
    sug_clear();
    if (k == DK_MOUSE) {
        mouse_key(b);
        return;
    }
    if (k == DK_F10) {
        ed_command(menu_run(0));
        return;
    }
    if (mods == KM_ALT && base >= '1' && base <= '9') {
        ed_command(C_FILE1 + base - '1');
        return;
    }
    cmd = shortcut(k);
    if (!cmd && mods == (KM_CTRL | KM_SHIFT) && base >= 'A' && base <= 'Z')
        cmd = shortcut(k & ~KM_SHIFT);       /* Ctrl+Shift+S is Ctrl+S */
    if (cmd) {
        ed_command(cmd);
        return;
    }
    if (mods == KM_ALT && menu_for_key(k) >= 0) {
        ed_command(menu_run(menu_for_key(k)));
        return;
    }
    if (!(mods & KM_ALT) && move(b, k)) {
        follow(b);
        return;
    }
    switch (k) {
    case DK_ENTER: newline(b); break;
    case DK_TAB: tab(b); break;
    case KM_SHIFT | DK_TAB: indent_lines(b, 1); break;
    case DK_BACKSPACE: backspace(b, 0); break;
    case KM_CTRL | DK_BACKSPACE: backspace(b, 1); break;
    case DK_DEL: delete_key(b, 0); break;
    case KM_CTRL | DK_DEL: delete_key(b, 1); break;
    case DK_INS: overwrite = !overwrite; break;
    case DK_ESC: b->sel = 0; break;
    default:
        if (k >= 32 && k <= 255)
            type_char(b, k);
        break;
    }
    follow(b);
}

/* ---- drawing ------------------------------------------------------------------- */

static unsigned char *roles;
static int rolecap;

static void draw_line(Buffer *b, int l, int row, int tx, int tw, int sel, int sl1, int sc1, int sl2, int sc2,
                      int bl, int bc, int ml, int mc)
{
    const Line *L = &b->ln[l];
    const char *g = l == b->cy ? sug_ghost(b, NULL) : NULL;
    unsigned short *rp = scr + row * SC + tx;   /* the cell of column x is rp[x - b->left] */
    int i, k, x = 0;
    if (rolecap < L->len + 1) {
        rolecap = L->len + 256;
        roles = xrealloc(roles, rolecap);
    }
    syn_line(b->lang, L->s, L->len, b->hs[l], roles);
    fill(row, tx, 1, tw, ' ', attr[A_TEXT]);
    for (i = 0; i <= L->len && x < b->left + tw; i++) {
        int ch, w = 1, a;
        if (g && i == b->cx)
            for (k = 0; g[k]; k++, x++)          /* the suggestion, dimmed */
                if (x >= b->left && x < b->left + tw)
                    rp[x - b->left] = (unsigned short)((unsigned char)g[k] | attr[A_GHOST] << 8);
        if (i == L->len)
            break;
        ch = (unsigned char)L->s[i];
        a = attr[roles[i]];
        if (ch == '\t') {
            w = next_x(x, '\t') - x;
            ch = ' ';
        } else if (ch == 0)
            ch = ' ';
        if (sel && (l > sl1 || (l == sl1 && i >= sc1)) && (l < sl2 || (l == sl2 && i < sc2)))
            a = attr[A_SELECT];
        if ((l == bl && i == bc) || (l == ml && i == mc))
            a = attr[A_MATCH];
        for (k = 0; k < w; k++, x++)
            if (x >= b->left && x < b->left + tw)
                rp[x - b->left] = (unsigned short)(ch | a << 8);
    }
    /* The line break of a selected line shows as one selected cell. */
    if (sel && l >= sl1 && l < sl2 && x >= b->left && x < b->left + tw)
        put(row, tx + x - b->left, ' ', attr[A_SELECT]);
}

/* Lines as drawn before, kept with what each was drawn from: the line's
 * stamp (new for each change to it), the syntax state it starts in, the
 * view, and the selection and bracket marks on it. A line drawn from the
 * same is copied, not drawn again, wherever on the screen it now is: moving
 * the cursor or scrolling draws only the lines that weren't showing, or
 * whose marks moved, and typing only the line typed in. A slot is chosen
 * by stamp; a line in another's slot just pushes it out. */
#define SLOTS 256
#define COLS 132
typedef struct { const Buffer *b; unsigned stamp; int ok, hs, lang, left, tx, tw, tab, theme, s1, s2, nl, m1, m2; } LineKey;
static LineKey slot_key[SLOTS];
static unsigned short slot_cells[SLOTS][COLS];
static int no_cache;

static void cached_line(Buffer *b, int l, int row, int tx, int tw, int sel, int sl1, int sc1, int sl2, int sc2,
                        int bl, int bc, int ml, int mc)
{
    LineKey k;
    const Line *L = &b->ln[l];
    int i = (int)(L->stamp % SLOTS);
    memset(&k, 0, sizeof k);
    k.b = b;
    k.stamp = L->stamp;
    k.ok = 1;
    k.hs = b->hs[l];
    k.lang = b->lang;
    k.left = b->left;
    k.tx = tx;
    k.tw = tw;
    k.tab = opt_tab;
    k.theme = theme;
    k.s1 = k.s2 = -1;
    if (sel && l >= sl1 && l <= sl2) {
        k.s1 = l == sl1 ? sc1 : 0;
        k.s2 = l == sl2 ? sc2 : L->len + 1;
        k.nl = l < sl2;
    }
    k.m1 = l == bl ? bc : -1;
    k.m2 = l == ml ? mc : -1;
    if (no_cache || tx + tw > COLS || (l == b->cy && sug_ghost(b, NULL))) {
        draw_line(b, l, row, tx, tw, sel, sl1, sc1, sl2, sc2, bl, bc, ml, mc);
        return;                          /* (the ghost isn't kept) */
    }
    if (!memcmp(&k, &slot_key[i], sizeof k)) {
        memcpy(scr + row * SC + tx, slot_cells[i] + tx, tw * sizeof *scr);
        return;
    }
    draw_line(b, l, row, tx, tw, sel, sl1, sc1, sl2, sc2, bl, bc, ml, mc);
    memcpy(slot_cells[i] + tx, scr + row * SC + tx, tw * sizeof *scr);
    memcpy(&slot_key[i], &k, sizeof k);
}

static void scrollbar(Buffer *b, int th)
{
    int i, pos, c = SC - 1;
    put(TY, c, CH_UP, attr[A_SCROLL]);
    put(TY + th - 1, c, CH_DOWN, attr[A_SCROLL]);
    for (i = TY + 1; i < TY + th - 1; i++)
        put(i, c, CH_TRACK, attr[A_SCROLL]);
    if (th > 3) {
        pos = b->n > 1 ? (th - 3) * b->cy / (b->n - 1) : 0;
        put(TY + 1 + pos, c, CH_THUMB, attr[A_SCROLL]);
    }
}

static void status_line(void)
{
    static const struct { const char *label; int cmd; } keys[] = {
        { " ~F1~ Help ", C_HELP }, { " ~F2~ Save ", C_SAVE }, { " ~Ctrl-O~ Open ", C_OPEN },
        { " ~Ctrl-F~ Find ", C_FIND }, { " ~F10~ Menu ", C_NONE }, { " ~Alt-X~ Exit ", C_EXIT },
    };
    int c = 0, i, r = SR - 1;
    Buffer *b = B;
    char right[64];
    fill(r, 0, 1, SC, ' ', attr[msg[0] && msg_err ? A_ERROR : A_STATUS]);
    nhints = 0;
    if (msg[0]) {
        text(r, 1, msg, attr[msg_err ? A_ERROR : A_STATUS], SC - 2);
        return;
    }
    {
        const Ref *ref = NULL;
        const char *g = sug_ghost(b, &ref);
        char t[160];
        if (g) {
            int s, x;
            word_prefix(b, &s);
            x = hot(r, 0, " ~Tab~ ", attr[A_STATUS], attr[A_STATUSKEY]);
            snprintf(t, sizeof t, "%.*s%s", b->cx - s, b->ln[b->cy].s + s, g);
            x += text(r, x, t, attr[A_STATUS], SC - x);
            if (ref) {
                snprintf(t, sizeof t, "   %s", ref->sig);
                text(r, x, t, attr[A_STATUS], SC - x - 1);
            }
            return;
        }
        {
            const Msg *m = msgs_at(b, b->cy);
            if (m && m->kind != M_FOUND) {
                const char *w = m->kind == M_ERROR ? " error " : m->kind == M_WARNING ? " warning " : " note ";
                int x = text(r, 0, w, attr[m->kind == M_ERROR ? A_MARKERR : A_MARKWARN], -1);
                text(r, x + 1, m->text, attr[A_STATUS], SC - x - 2);
                return;
            }
        }
        if ((ref = sug_call(b)) != NULL) {
            snprintf(t, sizeof t, " %s   %s", ref->sig, ref->where);
            text(r, 0, t, attr[A_STATUS], SC - 1);
            return;
        }
    }
    sprintf(right, "%s %s %s ", lang_name[b->lang], b->crlf ? "CRLF" : "LF", overwrite ? "OVR" : "INS");
    for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        int w = hotlen(keys[i].label);
        if (c + w > SC - (int)strlen(right) - 1)
            break;
        hot(r, c, keys[i].label, attr[A_STATUS], attr[A_STATUSKEY]);
        hints[nhints].from = c;
        hints[nhints].to = c + w;
        hints[nhints].cmd = keys[i].cmd;
        nhints++;
        c += w;
    }
    text(r, SC - (int)strlen(right), right, attr[A_STATUS], -1);
}

void ed_paint(void)
{
    Buffer *b = B;
    int th = TH(), g = gutter(b), tx = 1 + g, tw = SC - 2 - g, r, sel = has_sel(b);
    int sl1 = 0, sc1 = 0, sl2 = 0, sc2 = 0, bl = -1, bc = -1, ml = -1, mc = -1;
    char t[DE_PATH + 16];
    const char *name = b->path[0] ? base_name(b->path) : "UNTITLED";

    unsigned char kinds[132];

    sug_refresh();
    menu_bar();
    border(1, 0, SR - 2, SC, attr[A_FRAME], 1);   /* everything inside is drawn below */
    put(1, 2, '[', attr[A_FRAME]);
    put(1, 3, CH_THUMB, attr[A_FRAME]);
    put(1, 4, ']', attr[A_FRAME]);
    sprintf(t, " %s%s ", buf_modified(b) ? "*" : "", name);
    text(1, (SC - (int)strlen(t)) / 2, t, attr[A_TITLE], SC - 10);
    if (nfiles > 1) {
        sprintf(t, " %d/%d ", cur + 1, nfiles);
        text(1, SC - 3 - (int)strlen(t), t, attr[A_FRAME], -1);
    }
    sprintf(t, " %d:%d ", b->cy + 1, x_of(&b->ln[b->cy], b->cx) + 1);
    text(SR - 2, 2, t, attr[A_FRAME], -1);

    if (sel)
        sel_range(b, &sl1, &sc1, &sl2, &sc2);
    else
        cursor_bracket(b, 500, &bl, &bc, &ml, &mc);   /* drawn on every key: not too far */
    buf_state(b, b->top + th);
    msgs_marks(b, b->top, th < 132 ? th : 132, kinds);
    for (r = 0; r < th; r++) {
        int l = b->top + r, row = TY + r;
        if (g) {
            if (l < b->n) {
                int a = r >= 132 || kinds[r] == M_TEXT ? (l == b->cy ? A_GUTTERCUR : A_GUTTER)
                      : kinds[r] == M_ERROR ? A_MARKERR : A_MARKWARN;
                line_number(t, g - 1, l + 1);
                text(row, 1, t, attr[a], g);
            } else
                fill(row, 1, 1, g, ' ', attr[A_GUTTER]);
        }
        if (l < b->n)
            cached_line(b, l, row, tx, tw, sel, sl1, sc1, sl2, sc2, bl, bc, ml, mc);
        else
            fill(row, tx, 1, tw, ' ', attr[A_TEXT]);
    }
    scrollbar(b, th);
    status_line();
}

/* For the tests: the same screen, drawn with no cache. */
int ed_paint_check(void)
{
    static unsigned short a[132 * 60];
    int n = SR * SC, same;
    if (n > (int)(sizeof a / sizeof a[0]))
        return 1;
    ed_paint();
    memcpy(a, scr, n * sizeof *scr);
    no_cache = 1;
    ed_paint();
    no_cache = 0;
    same = !memcmp(a, scr, n * sizeof *scr);
    return same;
}

int ed_cursor(int *row, int *col)
{
    Buffer *b = B;
    int g = gutter(b);
    *row = TY + b->cy - b->top;
    *col = 1 + g + x_of(&b->ln[b->cy], b->cx) - b->left;
    return *row >= TY && *row < TY + TH() && *col >= 1 + g && *col < SC - 1;
}

/* The main loop, once round. The screen is drawn only when no key is
 * waiting: keys that come faster than it can be drawn are all done first. */
void ed_step(void)
{
    if (!plat_pending())
        ed_draw();
    ed_key(plat_key());
}

void ed_draw(void)
{
    Buffer *b = B;
    int g = gutter(b), row = TY + b->cy - b->top, col = 1 + g + x_of(&b->ln[b->cy], b->cx) - b->left;
    int visible = row >= TY && row < TY + TH() && col >= 1 + g && col < SC - 1;
    ed_paint();
    show(row, col, visible ? (overwrite ? 2 : 1) : 0);
}

/* ---- DE /SPEED FILE --------------------------------------------------------------
 * How long DE takes, on this computer, over each kind of key: drawing the
 * screen, putting it on the screen, moving the cursor and typing. Each is
 * done again and again for about a second, timed by the BIOS clock. The
 * file is changed only in memory, and never saved. */

static long tick(void)
{
    long t = plat_ms();
    while (plat_ms() == t)
        ;                       /* start as the clock moves */
    return plat_ms();
}

#define SPEED_FOR 1000          /* ms each measure runs for */
#define SPEED_MOST 20000        /* ... or this many times, on a fast computer */

static double per(long t0, int n)
{
    return n ? (double)(plat_ms() - t0) / n : 0;
}

static int speed_type(int n)
{
    static const char line[] = "value = buffer_count + line_length;";
    int i;
    for (i = 0; line[i] && n < SPEED_MOST; i++, n++) {
        ed_key((unsigned char)line[i]);
        ed_draw();
    }
    ed_key(DK_ENTER);
    ed_draw();
    return n + 1;
}

int ed_speed(const char *out[], double ms[], int max)
{
    Buffer *b = B;
    static unsigned short alt[132 * 60];
    long t0;
    int n, k = 0, suggest = opt_suggest, rows = SR * SC;

    ed_goto_line(b->n / 2 + 1);
    words_build(b);
    ed_draw();

    if (k < max) {
        t0 = tick();
        for (n = 0; plat_ms() - t0 < SPEED_FOR && n < SPEED_MOST; n++)
            ed_paint();
        out[k] = "Drawing the screen (DE's own work)";
        ms[k++] = per(t0, n);
    }
    if (k < max && rows <= (int)(sizeof alt / sizeof alt[0])) {
        int i;
        /* every row different each time: the left edge changes */
        ed_paint();
        memcpy(alt, scr, rows * sizeof *scr);
        for (i = 0; i < SR; i++)
            alt[i * SC] ^= 0x0001;
        t0 = tick();
        for (n = 0; plat_ms() - t0 < SPEED_FOR && n < SPEED_MOST; n++)
            plat_show(n & 1 ? alt : scr, 0, 0, 0);
        out[k] = "Showing a whole screen (DOS, DOSBox-X)";
        ms[k++] = per(t0, n);
    }
    if (k < max) {
        t0 = tick();
        for (n = 0; plat_ms() - t0 < SPEED_FOR && n < SPEED_MOST; n++) {
            ed_key(n & 1 ? DK_UP : DK_DOWN);
            ed_draw();
        }
        out[k] = "Moving the cursor, per key";
        ms[k++] = per(t0, n);
    }
    ed_key(DK_END);
    ed_key(DK_ENTER);
    if (k < max) {
        opt_suggest = 1;
        t0 = tick();
        for (n = 0; plat_ms() - t0 < SPEED_FOR && n < SPEED_MOST;)
            n = speed_type(n);
        out[k] = "Typing, with suggestions, per key";
        ms[k++] = per(t0, n);
    }
    if (k < max) {
        opt_suggest = 0;
        t0 = tick();
        for (n = 0; plat_ms() - t0 < SPEED_FOR && n < SPEED_MOST;)
            n = speed_type(n);
        out[k] = "Typing, without suggestions, per key";
        ms[k++] = per(t0, n);
    }
    opt_suggest = suggest;
    return k;
}
