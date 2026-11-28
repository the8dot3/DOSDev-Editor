/* DE - a file being edited: its lines, undo and redo, loading and saving. */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DE.H"

/* Memory has run out and the text being edited is not safe: it is written
 * out as NAME.SAV files first. A little memory is kept back for that. */
static void *spare;

static void out_of_memory(void)
{
    static int busy;
    free(spare);
    spare = NULL;
    plat_done();
    if (!busy) {
        busy = 1;
        ed_emergency_save();
    }
    fputs("DE: out of memory\n", stderr);
    exit(3);
}

#ifdef DE_TEST
int de_fail_alloc;             /* the tests: fail the Nth xrealloc from now (1: the next) */
#endif

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
#ifdef DE_TEST
    if (de_fail_alloc && --de_fail_alloc == 0)
        q = NULL;
#endif
    if (!q)
        out_of_memory();
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xrealloc(NULL, n), s, n);
}

int eq_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* ---- lines --------------------------------------------------------------- */

static void line_reserve(Line *L, int need)
{
    if (need + 1 > L->cap) {
        int c = L->cap ? L->cap : 16;
        while (c < need + 1)
            c *= 2;
        L->s = xrealloc(L->s, c);
        L->cap = c;
    }
}

static unsigned stamps;

/* A line's text changed: a new stamp, so a drawing of it from before is
 * known to be out of date. */
static void restamp(Line *L)
{
    L->stamp = ++stamps;
}

static void line_set(Line *L, const char *s, int n)
{
    L->s = NULL;
    L->cap = 0;
    line_reserve(L, n);
    memcpy(L->s, s, n);
    L->s[n] = 0;
    L->len = n;
    restamp(L);
}

static void lines_reserve(Buffer *b, int need)
{
    if (need > b->cap) {
        int c = b->cap ? b->cap : 64;
        while (c < need)
            c *= 2;
        b->ln = xrealloc(b->ln, c * sizeof(Line));
        b->cap = c;
    }
}

/* A line changed: the syntax state of every line after it may have too.
 * The states already worked out are kept (to hknown): when working them
 * out again, once past the changed lines, a state that comes out as it was
 * means all the rest are as they were, and the work stops there. That can't
 * be when lines came or went (moved), as the states would be other lines'. */
static void touched(Buffer *b, int l, int moved)
{
    if (b->hknown < b->hvalid)
        b->hknown = b->hvalid;
    if (b->hvalid > l + 1)
        b->hvalid = l + 1;
    if (moved)
        b->hknown = b->hvalid;
    if (b->hdirty < l + 1)
        b->hdirty = l + 1;
}

Buffer *buf_new(void)
{
    Buffer *b = xrealloc(NULL, sizeof *b);
    if (!spare)
        spare = malloc(32 * 1024);
    memset(b, 0, sizeof *b);
    lines_reserve(b, 1);
    line_set(&b->ln[0], "", 0);
    b->n = 1;
    b->crlf = 1;
    b->final_nl = 1;
    b->isnew = 1;
    b->hvalid = 1;
    return b;
}

static void free_lines(Buffer *b)
{
    int i;
    for (i = 0; i < b->n; i++)
        free(b->ln[i].s);
    b->n = 0;
}

void buf_free(Buffer *b)
{
    int i;
    if (!b)
        return;
    free_lines(b);
    free(b->ln);
    for (i = 0; i < b->nch; i++)
        free(b->ch[i].text);
    free(b->ch);
    free(b->hs);
    words_free(b);
    msgs_forget(b);             /* messages and places to go back to: not in it now */
    ed_forget(b);
    free(b);
}

/* ---- the two changes everything else is made of ---------------------------- */

static void raw_insert(Buffer *b, int l, int c, const char *t, int n, int *el, int *ec)
{
    Line *L = &b->ln[l];
    const char *p, *q, *end = t + n;
    char *tail;
    int i, nl = 0, taillen, k;

    for (i = 0; i < n; i++)
        if (t[i] == '\n')
            nl++;
    touched(b, l, nl > 0);
    words_line(b, l, -1);
    restamp(L);
    if (!nl) {
        line_reserve(L, L->len + n);
        memmove(L->s + c + n, L->s + c, L->len - c + 1);
        memcpy(L->s + c, t, n);
        L->len += n;
        *el = l;
        *ec = c + n;
        words_line(b, l, 1);
        return;
    }
    taillen = L->len - c;
    tail = xrealloc(NULL, taillen + 1);
    memcpy(tail, L->s + c, taillen + 1);
    lines_reserve(b, b->n + nl);
    memmove(&b->ln[l + 1 + nl], &b->ln[l + 1], (b->n - l - 1) * sizeof(Line));
    b->n += nl;
    L = &b->ln[l];
    p = t;
    q = memchr(p, '\n', end - p);
    line_reserve(L, c + (int)(q - p));
    memcpy(L->s + c, p, q - p);
    L->len = c + (int)(q - p);
    L->s[L->len] = 0;
    p = q + 1;
    for (k = l + 1;; k++) {
        Line *M = &b->ln[k];
        q = memchr(p, '\n', end - p);
        if (!q) {
            int m = (int)(end - p);
            M->s = NULL;
            M->cap = 0;
            line_reserve(M, m + taillen);
            memcpy(M->s, p, m);
            memcpy(M->s + m, tail, taillen + 1);
            M->len = m + taillen;
            restamp(M);
            *el = k;
            *ec = m;
            break;
        }
        line_set(M, p, (int)(q - p));
        p = q + 1;
    }
    free(tail);
    for (k = l; k <= *el; k++)
        words_line(b, k, 1);
    msgs_shift(b, l, nl);       /* messages below move down with their lines */
}

static void raw_delete(Buffer *b, int l1, int c1, int l2, int c2)
{
    Line *A = &b->ln[l1], *Z;
    int i, rest;

    touched(b, l1, l2 > l1);
    for (i = l1; i <= l2; i++)
        words_line(b, i, -1);
    restamp(A);
    if (l1 == l2) {
        memmove(A->s + c1, A->s + c2, A->len - c2 + 1);
        A->len -= c2 - c1;
        words_line(b, l1, 1);
        return;
    }
    Z = &b->ln[l2];
    rest = Z->len - c2;
    line_reserve(A, c1 + rest);
    memcpy(A->s + c1, Z->s + c2, rest + 1);
    A->len = c1 + rest;
    for (i = l1 + 1; i <= l2; i++)
        free(b->ln[i].s);
    memmove(&b->ln[l1 + 1], &b->ln[l2 + 1], (b->n - l2 - 1) * sizeof(Line));
    b->n -= l2 - l1;
    words_line(b, l1, 1);
    msgs_shift(b, l1, l1 - l2);
}

char *buf_text(const Buffer *b, int l1, int c1, int l2, int c2, int *n)
{
    int i, len = 0, at = 0;
    char *t;

    for (i = l1; i <= l2; i++) {
        int from = i == l1 ? c1 : 0, to = i == l2 ? c2 : b->ln[i].len;
        len += to - from + (i < l2);
    }
    t = xrealloc(NULL, len + 1);
    for (i = l1; i <= l2; i++) {
        int from = i == l1 ? c1 : 0, to = i == l2 ? c2 : b->ln[i].len;
        memcpy(t + at, b->ln[i].s + from, to - from);
        at += to - from;
        if (i < l2)
            t[at++] = '\n';
    }
    t[at] = 0;
    if (n)
        *n = len;
    return t;
}

/* ---- undo and redo ------------------------------------------------------- */

static Change *push(Buffer *b)
{
    Change *c;
    while (b->nch > b->upos)
        free(b->ch[--b->nch].text);
    if (b->saved > b->upos)
        b->saved = -1;          /* the saved state can't come back */
    if (b->nch == b->capch) {
        b->capch = b->capch ? b->capch * 2 : 64;
        b->ch = xrealloc(b->ch, b->capch * sizeof(Change));
    }
    c = &b->ch[b->nch++];
    b->upos = b->nch;
    c->group = b->group;
    c->cy = b->cy;
    c->cx = b->cx;
    c->text = NULL;
    return c;
}

void buf_group(Buffer *b)
{
    b->group++;
}

void buf_insert(Buffer *b, int l, int c, const char *t, int n)
{
    Change *ch;
    int el, ec;

    if (n <= 0)
        return;
    /* Typing joins the previous insert, so one undo takes back a word. */
    if (b->upos == b->nch && b->nch > 0 && b->saved != b->upos && !memchr(t, '\n', n)) {
        ch = &b->ch[b->nch - 1];
        if (ch->kind == 'i' && ch->group == b->group && ch->l2 == l && ch->c2 == c) {
            raw_insert(b, l, c, t, n, &el, &ec);
            ch->text = xrealloc(ch->text, ch->n + n);
            memcpy(ch->text + ch->n, t, n);
            ch->n += n;
            ch->c2 = ec;
            b->cy = el;
            b->cx = ec;
            return;
        }
    }
    ch = push(b);
    raw_insert(b, l, c, t, n, &el, &ec);
    ch->kind = 'i';
    ch->l1 = l;
    ch->c1 = c;
    ch->l2 = el;
    ch->c2 = ec;
    ch->text = xrealloc(NULL, n);
    memcpy(ch->text, t, n);
    ch->n = n;
    b->cy = el;
    b->cx = ec;
}

void buf_delete(Buffer *b, int l1, int c1, int l2, int c2)
{
    Change *ch;
    int n;
    char *t;

    if (l1 == l2 && c1 == c2)
        return;
    t = buf_text(b, l1, c1, l2, c2, &n);
    ch = push(b);
    ch->kind = 'd';
    ch->l1 = l1;
    ch->c1 = c1;
    ch->l2 = l2;
    ch->c2 = c2;
    ch->text = t;
    ch->n = n;
    raw_delete(b, l1, c1, l2, c2);
    b->cy = l1;
    b->cx = c1;
}

int buf_undo(Buffer *b)
{
    int g, el, ec;
    if (b->upos == 0)
        return 0;
    g = b->ch[b->upos - 1].group;
    while (b->upos > 0 && b->ch[b->upos - 1].group == g) {
        Change *c = &b->ch[--b->upos];
        if (c->kind == 'i')
            raw_delete(b, c->l1, c->c1, c->l2, c->c2);
        else
            raw_insert(b, c->l1, c->c1, c->text, c->n, &el, &ec);
        b->cy = c->cy;
        b->cx = c->cx;
    }
    b->group++;
    b->sel = 0;
    return 1;
}

int buf_redo(Buffer *b)
{
    int g, el, ec;
    if (b->upos >= b->nch)
        return 0;
    g = b->ch[b->upos].group;
    while (b->upos < b->nch && b->ch[b->upos].group == g) {
        Change *c = &b->ch[b->upos++];
        if (c->kind == 'i') {
            raw_insert(b, c->l1, c->c1, c->text, c->n, &el, &ec);
            b->cy = el;
            b->cx = ec;
        } else {
            raw_delete(b, c->l1, c->c1, c->l2, c->c2);
            b->cy = c->l1;
            b->cx = c->c1;
        }
    }
    b->group++;
    b->sel = 0;
    return 1;
}

int buf_modified(const Buffer *b)
{
    return b->upos != b->saved;
}

/* A file's size, or -1. */
static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n = -1;
    if (f) {
        if (!fseek(f, 0, SEEK_END))
            n = ftell(f);
        fclose(f);
    }
    return n;
}

/* Has the file been written since this buffer read or wrote it? A file
 * that has gone, or that can't be told about, is not "changed": there is
 * nothing to lose by saving over it. */
int buf_changed_on_disk(const Buffer *b)
{
    long t;
    if (!b->path[0] || plat_mtime(b->path, &t))
        return 0;
    /* DOS file times are in 2-second steps: the size is compared too */
    return t != b->disk_time || file_size(b->path) != b->disk_size;
}

/* The file on disk as it is now is the one this buffer knows: its time and
 * size, as buf_save takes them. "Keep mine" uses this, so DE does not ask
 * again about the same change. */
void buf_disk_seen(Buffer *b)
{
    long t;
    b->disk_time = plat_mtime(b->path, &t) ? 0 : t;
    b->disk_size = file_size(b->path);
}

/* For the tests: is everything as it should be? */
int buf_check(const Buffer *b)
{
    int i;
    if (b->n < 1 || b->n > b->cap)
        return 0;
    for (i = 0; i < b->n; i++) {
        const Line *L = &b->ln[i];
        if (!L->s || L->len < 0 || L->len >= L->cap || L->s[L->len] != 0 || memchr(L->s, '\n', L->len))
            return 0;
    }
    if (b->cy < 0 || b->cy >= b->n || b->cx < 0 || b->cx > b->ln[b->cy].len)
        return 0;
    if (b->upos < 0 || b->upos > b->nch)
        return 0;
    return 1;
}

/* ---- syntax states ---------------------------------------------------------- */

/* Make the syntax state at the start of every line up to `upto` known. */
void buf_state(Buffer *b, int upto)
{
    static unsigned char *role;
    static int rcap;
    int i, jumped = 0;
    if (upto >= b->n)
        upto = b->n - 1;
    if (b->hcap < b->n + 1) {
        b->hcap = b->n + 64;
        b->hs = xrealloc(b->hs, b->hcap);
    }
    if (b->hknown > b->n)
        b->hknown = b->n;
    /* The first line starts in the normal state. (Set every time: buf_load
     * and buf_save set hvalid to 1 without it, and a stray byte here is
     * read as a state.) */
    b->hs[0] = 0;
    if (b->hvalid < 1)
        b->hvalid = 1;
    for (i = b->hvalid; i <= upto;) {
        const Line *L = &b->ln[i - 1];
        unsigned char v;
        if (rcap < L->len + 1) {
            rcap = L->len + 64;
            role = xrealloc(role, rcap);
        }
        v = (unsigned char)syn_line(b->lang, L->s, L->len, b->hs[i - 1], role);
        if (i >= b->hdirty && i < b->hknown && v == b->hs[i]) {
            i = b->hknown;              /* as it was: so are the rest */
            b->hknown = b->hdirty = 0;
            jumped = 1;
            continue;
        }
        b->hs[i++] = v;
    }
    if (i > b->hvalid)
        b->hvalid = i;
    /* Past the change, and the states came out different all the way: the
     * ones kept beyond belong to text as it was before, so they go. */
    if ((!jumped && b->hdirty && i > b->hdirty) || b->hvalid >= b->hknown)
        b->hknown = b->hdirty = 0;
}

/* For the tests: every state that's said to be right, worked out again. */
int buf_state_check(Buffer *b)
{
    static unsigned char role[4096];
    int i, st = 0;
    for (i = 1; i < b->hvalid && i < b->n; i++) {
        const Line *L = &b->ln[i - 1];
        if (L->len >= (int)sizeof role)
            return 1;
        st = syn_line(b->lang, L->s, L->len, st, role);
        if (b->hs[i] != (unsigned char)st)
            return 0;
    }
    return 1;
}

/* ---- files ---------------------------------------------------------------- */

int buf_load(Buffer *b, const char *path)
{
    FILE *f;
    long size, when = 0;
    char *data;
    int i, nlines, start, crlfs = 0, lfs = 0;

    if (path != b->path) {      /* (a buffer read again passes its own name) */
        strncpy(b->path, path, DE_PATH - 1);
        b->path[DE_PATH - 1] = 0;
    }
    b->lang = lang_of(path);
    if (plat_mtime(path, &when))
        when = 0;               /* taken before reading: a change while reading shows up later */
    f = fopen(path, "rb");
    if (!f) {
        if (errno != ENOENT)
            return -1;
        b->disk_time = 0;
        b->disk_size = -1;
        return 1;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return -1;
    }
    if (size > DE_MAX_FILE) {   /* too big to hold: the buffer is as it was */
        fclose(f);
        return -2;
    }
    data = xrealloc(NULL, size + 1);
    if (size > 0 && fread(data, 1, size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return -1;
    }
    fclose(f);
    b->ctrlz = size > 0 && data[size - 1] == 0x1A;
    if (b->ctrlz)
        size--;
    for (i = 0; i < size; i++)
        if (data[i] == '\n') {
            lfs++;
            if (i > 0 && data[i - 1] == '\r')
                crlfs++;
        }
    b->crlf = lfs == 0 || crlfs * 2 >= lfs;
    b->final_nl = size == 0 || data[size - 1] == '\n';
    nlines = lfs + 1;
    if (b->final_nl && size > 0)
        nlines--;
    words_free(b);
    free_lines(b);
    lines_reserve(b, nlines);
    b->n = 0;
    start = 0;
    for (i = 0; i <= size && b->n < nlines; i++) {
        if (i == size || data[i] == '\n') {
            int end = i;
            if (i < size && end > start && data[end - 1] == '\r')
                end--;
            line_set(&b->ln[b->n++], data + start, end - start);
            start = i + 1;
        }
    }
    if (b->n == 0)
        line_set(&b->ln[b->n++], "", 0);
    free(data);
    b->isnew = 0;
    b->hvalid = 1;
    b->hknown = b->hdirty = 0;
    b->cy = b->cx = b->top = b->left = b->want = 0;
    b->sel = 0;
    for (i = 0; i < b->nch; i++)        /* a buffer read again starts its history again */
        free(b->ch[i].text);
    b->nch = b->upos = b->saved = 0;
    b->disk_time = when;
    b->disk_size = size + b->ctrlz;      /* as it was on disk */
    return 0;
}

/* The file to write first: the same name with .$$$, so a failed save never
 * leaves half a file in place of the old one. */
static void temp_name(const char *path, char *out)
{
    const char *dot = strrchr(path, '.');
    const char *sep = strrchr(path, '/');
    const char *sep2 = strrchr(path, '\\');
    size_t n;
    if (sep2 > sep)
        sep = sep2;
    n = dot && (!sep || dot > sep) ? (size_t)(dot - path) : strlen(path);
    if (n > DE_PATH - 5)
        n = DE_PATH - 5;
    memcpy(out, path, n);
    strcpy(out + n, ".$$$");
}

/* Writes the lines to a file: 1 if all of it went. Lines that were left
 * without text (memory ran out) write as empty. */
static int write_lines(const Buffer *b, const char *name)
{
    FILE *f = fopen(name, "wb");
    int i, ok = 1;
    const char *eol = b->crlf ? "\r\n" : "\n";

    if (!f)
        return 0;
    if (!(b->n == 1 && b->ln[0].len == 0)) {
        for (i = 0; i < b->n && ok; i++) {
            int len = b->ln[i].s ? b->ln[i].len : 0;
            if (fwrite(b->ln[i].s, 1, len, f) != (size_t)len)
                ok = 0;
            if (i < b->n - 1 || b->final_nl)
                if (fputs(eol, f) < 0)
                    ok = 0;
        }
    }
    if (b->ctrlz)
        fputc(0x1A, f);
    if (fflush(f) || ferror(f))
        ok = 0;
    if (fclose(f))
        ok = 0;
    if (!ok)
        remove(name);
    return ok;
}

int buf_save(Buffer *b, const char *path)
{
    char tmp[DE_PATH];
    FILE *f;

    temp_name(path, tmp);
    if (!write_lines(b, tmp))
        return -1;
    remove(path);                 /* DOS won't rename over a file */
    if (rename(tmp, path)) {
        f = fopen(path, "rb");
        if (f) {                  /* the old file is still there */
            fclose(f);
            remove(tmp);
            return -1;
        }
        return -2;                /* it's safe in the .$$$ file */
    }
    if (path != b->path) {
        strncpy(b->path, path, DE_PATH - 1);
        b->path[DE_PATH - 1] = 0;
        b->lang = lang_of(path);
        b->hvalid = 1;
        b->hknown = b->hdirty = 0;
        if (b->words)
            words_build(b);      /* made again if the language changed */
    }
    buf_disk_seen(b);
    b->saved = b->upos;
    b->isnew = 0;
    return 0;
}

static int exists(const char *name)
{
    FILE *f = fopen(name, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* Out of memory: the text goes to NAME.SAV (the file's name with .SAV), or
 * UNTITLnn.SAV for a file with no name, nn being its place in the list
 * (1 is the first). A name already there (another file's, or one left by an
 * earlier crash) is never written over: .SA0, .SA1 ... .SA9 are tried next. */
int buf_emergency(const Buffer *b, int index, char *name)
{
    const char *dot = strrchr(b->path, '.');
    const char *sep = strrchr(b->path, '/');
    const char *sep2 = strrchr(b->path, '\\');
    size_t n;
    int k;
    if (!b->path[0]) {
        sprintf(name, "UNTITL%02d", (index + 1) % 100);
        n = strlen(name);
    } else {
        if (sep2 > sep)
            sep = sep2;
        n = dot && (!sep || dot > sep) ? (size_t)(dot - b->path) : strlen(b->path);
        if (n > DE_PATH - 5)
            n = DE_PATH - 5;
        memcpy(name, b->path, n);
    }
    for (k = -1; k <= 9; k++) {
        if (k < 0)
            strcpy(name + n, ".SAV");
        else
            sprintf(name + n, ".SA%d", k);
        if (!exists(name))
            return write_lines(b, name) ? 0 : -1;
    }
    return -1;                  /* eleven files by this name already */
}
