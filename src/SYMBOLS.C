/* DE - symbols: the functions, labels, types and defines a file has, for
 * Go to symbol (Ctrl+L) and Go to definition (F12). They're found line by
 * line, by the shape of each language's definitions, not by compiling:
 *   C         a name and ( at the start of a line, not ending in ;
 *             #define NAME, struct/union/enum NAME, typedef ... NAME;
 *   Assembly  NAME: at the start of a line, NAME proc/macro/equ/db...,
 *             %define and %macro NAME
 *   Pascal    procedure, function, constructor, destructor NAME,
 *             NAME = record/object/class
 *   BASIC     SUB and FUNCTION NAME, DEF FNNAME, NAME: labels
 *   Batch     :LABEL     Makefile  target:     INI  [section]
 *   Text      a line underlined with - or =, and # headings */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "DE.H"

static int is_id(int c) { return isalnum((unsigned char)c) || c == '_'; }

/* The word at s[i..): its end. */
static int word_end(const char *s, int n, int i)
{
    while (i < n && is_id(s[i]))
        i++;
    return i;
}

static int skip_sp(const char *s, int n, int i)
{
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    return i;
}

static int word_eq(const char *s, int n, int i, int e, const char *w, int nocase)
{
    int k = (int)strlen(w), j;
    if (e - i != k || e > n)
        return 0;
    for (j = 0; j < k; j++)
        if (nocase ? tolower((unsigned char)s[i + j]) != w[j] : s[i + j] != w[j])
            return 0;
    return 1;
}

static int add(Sym *out, int n, int max, const char *s, int from, int to, int line, char kind)
{
    if (n >= max || to <= from)
        return n;
    if (to - from >= (int)sizeof out[0].name)
        to = from + (int)sizeof out[0].name - 1;
    memcpy(out[n].name, s + from, to - from);
    out[n].name[to - from] = 0;
    out[n].line = line;
    out[n].kind = kind;
    return n + 1;
}

/* The last character that isn't a comment or a space. */
static int last_code(const char *s, int n, const unsigned char *role)
{
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || role[n - 1] == A_COMMENT))
        n--;
    return n > 0 ? s[n - 1] : 0;
}

static int c_line(const char *s, int n, const unsigned char *role, int l, Sym *out, int k, int max)
{
    int i, e, j;
    if (!n || role[0] == A_COMMENT || role[0] == A_STRING)
        return k;
    if (s[0] == '#') {
        i = skip_sp(s, n, 1);
        e = word_end(s, n, i);
        if (word_eq(s, n, i, e, "define", 0)) {
            i = skip_sp(s, n, e);
            return add(out, k, max, s, i, word_end(s, n, i), l, 'd');
        }
        return k;
    }
    if (s[0] == '}') {                       /* } Name;  closes a typedef */
        i = skip_sp(s, n, 1);
        e = word_end(s, n, i);
        if (e > i && skip_sp(s, n, e) < n && s[skip_sp(s, n, e)] == ';')
            return add(out, k, max, s, i, e, l, 't');
        return k;
    }
    if (!isalpha((unsigned char)s[0]) && s[0] != '_')
        return k;
    e = word_end(s, n, 0);
    if (word_eq(s, n, 0, e, "typedef", 0)) {
        int last = -1, le = -1;
        const char *fp;
        if (last_code(s, n, role) != ';')
            return k;                        /* the name comes at the } */
        if ((fp = strstr(s, "(*")) != NULL && fp < s + n) {
            i = (int)(fp - s) + 2;
            return add(out, k, max, s, i, word_end(s, n, i), l, 't');
        }
        for (i = e; i < n && s[i] != ';'; i++)
            if (is_id(s[i]) && !is_id(s[i - 1]) && !isdigit((unsigned char)s[i])) {
                last = i;
                le = word_end(s, n, i);
                i = le - 1;
            }
        return last >= 0 ? add(out, k, max, s, last, le, l, 't') : k;
    }
    if (word_eq(s, n, 0, e, "struct", 0) || word_eq(s, n, 0, e, "union", 0) || word_eq(s, n, 0, e, "enum", 0)) {
        i = skip_sp(s, n, e);
        j = word_end(s, n, i);
        if (j > i && last_code(s, n, role) != ';') {
            int after = skip_sp(s, n, j);
            if (after >= n || s[after] == '{' || role[after] == A_COMMENT)
                return add(out, k, max, s, i, j, l, 't');
        }
        return k;
    }
    /* a function: the name before the first (, and no ; at the end */
    for (i = 0; i < n && s[i] != '('; i++)
        if (s[i] == '=' || s[i] == ';' || role[i] == A_COMMENT || role[i] == A_STRING)
            return k;
    if (i == n || last_code(s, n, role) == ';' || last_code(s, n, role) == ',')
        return k;
    j = i;
    while (j > 0 && (s[j - 1] == ' ' || s[j - 1] == '\t'))
        j--;
    e = j;
    while (j > 0 && is_id(s[j - 1]))
        j--;
    if (e == j || isdigit((unsigned char)s[j]))
        return k;
    if (word_eq(s, n, j, e, "if", 0) || word_eq(s, n, j, e, "while", 0) || word_eq(s, n, j, e, "for", 0)
        || word_eq(s, n, j, e, "switch", 0) || word_eq(s, n, j, e, "return", 0) || word_eq(s, n, j, e, "sizeof", 0))
        return k;
    return add(out, k, max, s, j, e, l, 'f');
}

static int asm_line(const char *s, int n, const unsigned char *role, int l, Sym *out, int k, int max)
{
    int i, e, j;
    if (!n || role[0] == A_COMMENT)
        return k;
    if (s[0] == '%') {
        e = word_end(s, n, 1);
        if (word_eq(s, n, 1, e, "define", 1) || word_eq(s, n, 1, e, "macro", 1) || word_eq(s, n, 1, e, "imacro", 1)
            || word_eq(s, n, 1, e, "idefine", 1)) {
            i = skip_sp(s, n, e);
            return add(out, k, max, s, i, word_end(s, n, i), l, 'd');
        }
        return k;
    }
    if (!isalpha((unsigned char)s[0]) && s[0] != '_' && s[0] != '@' && s[0] != '?')
        return k;                            /* indented, or .local */
    e = 0;
    while (e < n && (is_id(s[e]) || s[e] == '@' || s[e] == '?' || s[e] == '$'))
        e++;
    if (e < n && s[e] == ':')
        return add(out, k, max, s, 0, e, l, 'l');
    i = skip_sp(s, n, e);
    j = word_end(s, n, i);
    if (word_eq(s, n, i, j, "proc", 1))
        return add(out, k, max, s, 0, e, l, 'f');
    if (word_eq(s, n, i, j, "macro", 1) || word_eq(s, n, i, j, "equ", 1))
        return add(out, k, max, s, 0, e, l, 'd');
    if (word_eq(s, n, i, j, "struc", 1) || word_eq(s, n, i, j, "struct", 1))
        return add(out, k, max, s, 0, e, l, 't');
    if (word_eq(s, n, i, j, "db", 1) || word_eq(s, n, i, j, "dw", 1) || word_eq(s, n, i, j, "dd", 1)
        || word_eq(s, n, i, j, "dq", 1) || word_eq(s, n, i, j, "dt", 1) || word_eq(s, n, i, j, "resb", 1)
        || word_eq(s, n, i, j, "resw", 1) || word_eq(s, n, i, j, "resd", 1) || word_eq(s, n, i, j, "times", 1))
        return add(out, k, max, s, 0, e, l, 'v');
    return k;
}

static int pascal_line(const char *s, int n, const unsigned char *role, int l, Sym *out, int k, int max)
{
    int i = skip_sp(s, n, 0), e, j;
    if (i >= n || role[i] == A_COMMENT)
        return k;
    e = word_end(s, n, i);
    if (word_eq(s, n, i, e, "procedure", 1) || word_eq(s, n, i, e, "function", 1)
        || word_eq(s, n, i, e, "constructor", 1) || word_eq(s, n, i, e, "destructor", 1)
        || word_eq(s, n, i, e, "program", 1) || word_eq(s, n, i, e, "unit", 1)) {
        char kind = word_eq(s, n, i, e, "program", 1) || word_eq(s, n, i, e, "unit", 1) ? 's' : 'f';
        i = skip_sp(s, n, e);
        j = i;
        while (j < n && (is_id(s[j]) || (s[j] == '.' && j + 1 < n && is_id(s[j + 1]))))
            j++;
        return add(out, k, max, s, i, j, l, kind);
    }
    /* Name = record / object / class */
    if (e > i) {
        int q = skip_sp(s, n, e), r, re;
        if (q < n && s[q] == '=') {
            r = skip_sp(s, n, q + 1);
            if (r < n && s[r] == '^')
                return k;
            re = word_end(s, n, r);
            if (word_eq(s, n, r, re, "record", 1) || word_eq(s, n, r, re, "object", 1)
                || word_eq(s, n, r, re, "class", 1) || (word_eq(s, n, r, re, "packed", 1)))
                return add(out, k, max, s, i, e, l, 't');
        }
    }
    return k;
}

static int basic_line(const char *s, int n, const unsigned char *role, int l, Sym *out, int k, int max)
{
    int i = skip_sp(s, n, 0), e, j;
    if (i >= n || role[i] == A_COMMENT)
        return k;
    e = word_end(s, n, i);
    if (word_eq(s, n, i, e, "sub", 1) || word_eq(s, n, i, e, "function", 1)) {
        i = skip_sp(s, n, e);
        j = word_end(s, n, i);
        if (j < n && s[j] == '$')
            j++;
        return add(out, k, max, s, i, j, l, 'f');
    }
    if (word_eq(s, n, i, e, "def", 1)) {
        i = skip_sp(s, n, e);
        j = word_end(s, n, i);
        if (j - i > 2 && tolower((unsigned char)s[i]) == 'f' && tolower((unsigned char)s[i + 1]) == 'n') {
            if (j < n && s[j] == '$')
                j++;
            return add(out, k, max, s, i, j, l, 'f');
        }
        return k;
    }
    if (i == 0 && isalpha((unsigned char)s[0]) && e < n && s[e] == ':')
        return add(out, k, max, s, 0, e, l, 'l');
    return k;
}

static int all_of(const char *s, int n, int c)
{
    int i;
    if (n < 3)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] != c)
            return 0;
    return 1;
}

int sym_scan(int lang, const Line *ln, int n, Sym *out, int max)
{
    static unsigned char *role;
    static int cap;
    int l, k = 0, state = 0;
    for (l = 0; l < n && k < max; l++) {
        const char *s = ln[l].s;
        int len = ln[l].len, i, e;
        if (cap < len + 1) {
            cap = len + 256;
            role = xrealloc(role, cap);
        }
        state = syn_line(lang, s, len, state, role);
        switch (lang) {
        case L_C: k = c_line(s, len, role, l, out, k, max); break;
        case L_ASM: k = asm_line(s, len, role, l, out, k, max); break;
        case L_PASCAL: k = pascal_line(s, len, role, l, out, k, max); break;
        case L_BASIC: k = basic_line(s, len, role, l, out, k, max); break;
        case L_BATCH:
            if (len > 1 && s[0] == ':' && s[1] != ':') {
                for (e = 1; e < len && s[e] != ' ' && s[e] != '\t'; e++)
                    ;
                k = add(out, k, max, s, 1, e, l, 'l');
            }
            break;
        case L_MAKE:
            if (len && (isalnum((unsigned char)s[0]) || s[0] == '_' || s[0] == '$')) {
                for (i = 0; i < len && s[i] != ':' && s[i] != '=' && s[i] != ' ' && s[i] != '#'; i++)
                    ;
                e = i;
                i = skip_sp(s, len, i);
                if (i < len && s[i] == ':' && (i + 1 >= len || s[i + 1] != '='))
                    k = add(out, k, max, s, 0, e, l, 'l');
            }
            break;
        case L_INI:
            if (len > 2 && s[0] == '[' && (e = (int)(strchr(s, ']') ? strchr(s, ']') - s : 0)) > 1)
                k = add(out, k, max, s, 1, e, l, 's');
            break;
        default:
            if (l > 0 && (all_of(s, len, '-') || all_of(s, len, '=')) && ln[l - 1].len > 0
                && !all_of(ln[l - 1].s, ln[l - 1].len, '-') && !all_of(ln[l - 1].s, ln[l - 1].len, '='))
                k = add(out, k, max, ln[l - 1].s, 0, ln[l - 1].len, l - 1, 's');
            else if (len > 1 && s[0] == '#') {
                for (i = 0; i < len && s[i] == '#'; i++)
                    ;
                k = add(out, k, max, s, skip_sp(s, len, i), len, l, 's');
            }
            break;
        }
    }
    return k;
}

static const char *kind_name(char kind)
{
    switch (kind) {
    case 'f': return "function";
    case 'd': return "define";
    case 't': return "type";
    case 'l': return "label";
    case 'v': return "data";
    default: return "section";
    }
}

/* ---- Go to symbol --------------------------------------------------------------- */

#define MAXSYMS 4000
static Sym syms[MAXSYMS];
static int nsyms, shown[MAXSYMS], nshown;

static int sym_count(const char *f)
{
    int i;
    nshown = 0;
    for (i = 0; i < nsyms; i++)
        if (has_text(syms[i].name, f))
            shown[nshown++] = i;
    return nshown;
}

static void sym_item(int i, char *tag, char *text, int size, int *role)
{
    const Sym *s = &syms[shown[i]];
    snprintf(tag, 10, "%s", kind_name(s->kind));
    *role = A_HELPHEAD;
    snprintf(text, size, "%-44s line %d", s->name, s->line + 1);
}

static const char *sym_info(int i)
{
    const Buffer *b = B_CUR;
    static char t[160];
    int l = syms[shown[i]].line;
    snprintf(t, sizeof t, "%d: %.150s", l + 1, l < b->n ? b->ln[l].s : "");
    return t;
}

static int col_of_name(const Buffer *b, int line, const char *name)
{
    const char *p = line < b->n ? strstr(b->ln[line].s, name) : NULL;
    return p ? (int)(p - b->ln[line].s) + 1 : 0;
}

void sym_list(void)
{
    Buffer *b = B_CUR;
    char t[DE_PATH + 40];
    Pick p;
    int i, sel = 0;
    nsyms = sym_scan(b->lang, b->ln, b->n, syms, MAXSYMS);
    if (!nsyms) {
        ed_message(1, "No functions, labels or headings were found in %s.", b->path[0] ? base_name(b->path) : "this file");
        return;
    }
    sym_count("");
    for (i = 0; i < nsyms; i++)
        if (syms[i].line <= b->cy)
            sel = i;                        /* the one the cursor is in */
    snprintf(t, sizeof t, "Symbols in %s", b->path[0] ? base_name(b->path) : "UNTITLED");
    p.title = t;
    p.keys = " Type to find   Enter Go to it   Esc Close ";
    p.count = sym_count;
    p.item = sym_item;
    p.info = sym_info;
    p.filter = 1;
    i = pick_run(&p, sel);
    if (i >= 0) {
        const Sym *s = &syms[shown[i]];
        ed_mark();
        ed_goto(s->line + 1, col_of_name(b, s->line, s->name));
    }
}

/* ---- Go to definition ------------------------------------------------------------- */

typedef struct { char path[DE_PATH]; int line, col; char kind; char text[80]; } Def;
#define MAXDEFS 64
static Def defs[MAXDEFS];
static int ndefs;

static int same_name(const char *a, const char *b, int nocase)
{
    return nocase ? eq_nocase(a, b) : !strcmp(a, b);
}

static void defs_from(Buffer *f, const char *path, const char *name, int nocase)
{
    static Sym s[MAXSYMS];
    int n = sym_scan(f->lang, f->ln, f->n, s, MAXSYMS), i, k;
    for (i = 0; i < n && ndefs < MAXDEFS; i++)
        if (same_name(s[i].name, name, nocase)) {
            const Line *L = &f->ln[s[i].line];
            for (k = 0; k < ndefs; k++)
                if (defs[k].line == s[i].line && ed_same_path(defs[k].path, path))
                    break;
            if (k < ndefs)
                continue;
            snprintf(defs[ndefs].path, DE_PATH, "%s", path);
            defs[ndefs].line = s[i].line;
            defs[ndefs].col = col_of_name(f, s[i].line, s[i].name);
            defs[ndefs].kind = s[i].kind;
            k = 0;
            while (k < L->len && (L->s[k] == ' ' || L->s[k] == '\t'))
                k++;
            snprintf(defs[ndefs].text, sizeof defs[ndefs].text, "%.79s", L->s + k);
            ndefs++;
        }
}

static int is_open(const char *full)
{
    int i;
    char t[DE_PATH];
    for (i = 0; i < nfiles; i++) {
        if (!files[i]->path[0])
            continue;
        plat_fullpath(files[i]->path, t);
        if (ed_same_path(t, full))
            return 1;
    }
    return 0;
}

static int nscanned;

static void defs_walk(const char *dir, int lang, const char *name, int nocase, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[DE_PATH], full[DE_PATH];
    struct stat st;
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && ndefs < MAXDEFS && nscanned < 1000) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 3)
                defs_walk(p, lang, name, nocase, depth + 1);
            continue;
        }
        if (lang_of(e->d_name) != lang || st.st_size > 1024L * 1024)
            continue;
        plat_fullpath(p, full);
        if (is_open(full))
            continue;                       /* looked at already, as it is in DE */
        nscanned++;
        {
            Buffer *f = buf_new();
            if (buf_load(f, p) == 0)
                defs_from(f, full, name, nocase);
            buf_free(f);
        }
    }
    closedir(d);
}

static int def_count(const char *f)
{
    (void)f;
    return ndefs;
}

static void def_item(int i, char *tag, char *text, int size, int *role)
{
    snprintf(tag, 10, "%s", kind_name(defs[i].kind));
    *role = A_HELPHEAD;
    snprintf(text, size, "%s(%d): %s", base_name(defs[i].path), defs[i].line + 1, defs[i].text);
}

static const char *def_info(int i)
{
    return defs[i].path;
}

/* #include "FILE": where is FILE? */
static int find_include(const Buffer *b, const char *name, char *out)
{
    char dirs[16][DE_PATH], t[DE_PATH], list[512];
    const char *env;
    int n = 0, i;
    Project p;
    DIR *d;
    struct dirent *e;
    plat_fullpath(b->path[0] ? b->path : ".", t);
    if (b->path[0]) {
        char *a = strrchr(t, '/'), *c = strrchr(t, '\\');
        if (c > a)
            a = c;
        if (a)
            *a = 0;
    }
    snprintf(dirs[n++], DE_PATH, "%s", t);
    proj_find((Buffer *)b, &p);
    snprintf(dirs[n++], DE_PATH, "%s", p.dir);
    /* the project's libraries: LIBS/<NAME>/INCLUDE, as DOSDev lays them out */
    snprintf(t, sizeof t, "%.200s/LIBS", p.dir);
    if ((d = opendir(t)) != NULL) {
        while ((e = readdir(d)) != NULL && n < 10)
            if (e->d_name[0] != '.')
                snprintf(dirs[n++], DE_PATH, "%.200s/%.20s/INCLUDE", t, e->d_name);
        closedir(d);
    }
    if ((env = getenv("INCLUDE")) != NULL) {
        char *s, *x;
        snprintf(list, sizeof list, "%s", env);
        for (s = list; *s && n < 15; s = x) {
            x = strchr(s, ';');
            if (x)
                *x++ = 0;
            else
                x = s + strlen(s);
            if (*s)
                snprintf(dirs[n++], DE_PATH, "%.200s", s);
        }
    }
    if ((env = getenv("DJDIR")) != NULL && n < 16)
        snprintf(dirs[n++], DE_PATH, "%s/include", env);
    for (i = 0; i < n; i++) {
        FILE *f;
        snprintf(out, DE_PATH, "%.200s/%.58s", dirs[i], name);
        if ((f = fopen(out, "r")) != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

/* The file named on an include line, if the cursor is on one. */
static int include_name(const Buffer *b, char *name, int size)
{
    const Line *L = &b->ln[b->cy];
    const char *s = L->s, *q, *e;
    while (*s == ' ' || *s == '\t')
        s++;
    if (b->lang == L_C && *s == '#') {
        s++;
        while (*s == ' ')
            s++;
        if (strncmp(s, "include", 7))
            return 0;
        s += 7;
    } else if (b->lang == L_ASM && (!strncmp(s, "%include", 8) || !strncmp(s, "include", 7) || !strncmp(s, "INCLUDE", 7)))
        s += *s == '%' ? 8 : 7;
    else if (b->lang == L_PASCAL && (!strncmp(s, "{$I ", 4) || !strncmp(s, "{$i ", 4)))
        s += 4;
    else
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    q = s;
    if (*q == '"' || *q == '<' || *q == '\'')
        q++;
    for (e = q; *e && *e != '"' && *e != '>' && *e != '\'' && *e != '}' && *e != ' '; e++)
        ;
    if (e == q || e - q >= size)
        return 0;
    snprintf(name, size, "%.*s", (int)(e - q), q);
    return 1;
}

void sym_definition(void)
{
    Buffer *b = B_CUR;
    char w[40], inc[80], path[DE_PATH], full[DE_PATH];
    int nocase = lang_nocase(b->lang), i;
    Project p;
    if (include_name(b, inc, sizeof inc)) {
        if (find_include(b, inc, path))
            ed_jump(path, 1, 0);
        else
            ed_message(1, "Can't find %s, here, in the project or on INCLUDE.", inc);
        return;
    }
    if (!ed_word(b, w, sizeof w)) {
        ed_message(1, "Put the cursor on a name, and F12 goes to where it's defined.");
        return;
    }
    ndefs = nscanned = 0;
    busy_box("Go to definition", w, "Looking in the open files and the project...");
    /* this file, the other open files, then the project's files on disk */
    plat_fullpath(b->path[0] ? b->path : "UNTITLED", full);
    defs_from(b, full, w, nocase);
    for (i = 0; i < nfiles; i++)
        if (files[i] != b && files[i]->lang == b->lang && files[i]->path[0]) {
            plat_fullpath(files[i]->path, path);
            defs_from(files[i], path, w, nocase);
        }
    proj_find(b, &p);
    defs_walk(p.dir, b->lang, w, nocase, 0);
    if (ndefs == 0) {
        const Ref *r = ref_find(b->lang, w, (int)strlen(w));
        if (r)
            help_entry(r);
        else
            ed_message(1, "No definition of %s in the open files or in %s.", w, base_name(p.dir));
        return;
    }
    if (ndefs == 1 && defs[0].line == b->cy && ed_same_path(defs[0].path, full)) {
        ed_message(0, "This is where %s is defined.", w);
        return;
    }
    i = 0;
    if (ndefs > 1) {
        Pick k;
        char t[80];
        snprintf(t, sizeof t, "%s is defined in %d places", w, ndefs);
        k.title = t;
        k.keys = " Enter Go to it   Esc Close ";
        k.count = def_count;
        k.item = def_item;
        k.info = def_info;
        k.filter = 0;
        if ((i = pick_run(&k, 0)) < 0)
            return;
    }
    ed_jump(defs[i].path, defs[i].line + 1, defs[i].col);
}
