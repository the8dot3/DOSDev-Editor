/* DE - symbols: the functions, labels, types and defines a file has, for
 * Go to symbol (Ctrl+L) and Go to definition (F12). They're found line by
 * line, by the shape of each language's definitions, not by compiling:
 *   C         a name and ( at the start of a line, not ending in ;
 *             #define NAME, struct/union/enum NAME, typedef ... NAME;
 *   C++       as C, and class/struct/union/enum/namespace NAME, using NAME =,
 *             and functions in namespaces and class bodies (cpp_scan)
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

/* ---- C++ ------------------------------------------------------------------------
 * C++ puts definitions inside namespaces and class bodies, indented, so a
 * line's place in the braces matters more than where it starts. cpp_scan
 * counts the braces (outside comments, strings and # lines) and looks only
 * at lines where a declaration can be: at the top, or directly inside a
 * namespace, a class or an extern "C" block, never inside a function body.
 * There it takes:
 *   class, struct, union NAME and enum (class) NAME, when { or : or the end
 *   of the line follows (not a declaration ending in ;); namespace NAME;
 *   using NAME = ...; and the C rules for #define and typedef;
 *   a function: a name and its ( whose ) is followed by { (or by : and the
 *   member initialisers, or const, override ... and then {) before any ; or
 *   =, on this line or the next few. Its name has the class it is in in
 *   front: bar in the body of class Foo, and void ns::Foo::bar() outside
 *   it, are both Foo::bar (only the innermost class, no namespace: see
 *   add_scoped). Constructors (Foo::Foo), destructors (Foo::~Foo) and
 *   operators (Foo::operator+) are functions too. A template's parameter
 *   list is left out (template <class T> T max(T, T) is max; Stack<T>::push
 *   is Stack::push).
 * A constructor's member initialisers (: x_(x), y_(2), over any number of
 * lines) are passed over until the body's { (after_params, CppScope.init),
 * and each #else or #elif branch is counted from the braces at its #if.
 * It is a guess from the shape of lines, as the C rules are. */

#define SCOPES 16
#define IFS 8
typedef struct {
    int depth, nsc;             /* braces open; the scopes open */
    struct { char name[40]; int depth, cls; } sc[SCOPES];
    char pending[40]; int pend, pcls;   /* a class or namespace whose { is still to come */
    int in_define;              /* the line before was a # line ending in \ */
    int paren;                  /* a definition's parameters still open, over lines */
    int init;                   /* in a constructor's initialiser list, until { or ; */
    struct { int depth, nsc; } ifs[IFS]; int nifs;   /* at each #if: #else starts there again */
    int if0;                    /* in #if 0: 1 + the #ifs opened inside it; 0 not */
} CppScope;

static int word_at(const char *s, int n, int i, const char *w)
{
    return word_eq(s, n, i, word_end(s, n, i), w, 0);
}

/* Past a balanced <...> that starts at s[i] ('<'), or -1 if not closed. */
static int angle_end(const char *s, int n, int i)
{
    int d = 0;
    for (; i < n; i++)
        if (s[i] == '<')
            d++;
        else if (s[i] == '>' && --d == 0)
            return i + 1;
        else if (s[i] == ';' || s[i] == '{')
            return -1;
    return -1;
}

/* A name as it is listed: the class it is in and the name, Foo::bar, both
 * for bar in the body of class Foo and for outer::Foo::bar written outside
 * it. Only the innermost class is written, and no namespace, so names stay
 * short (Sym's name holds 39 characters; snprintf cuts what is longer). */
static int add_scoped(const CppScope *c, Sym *out, int k, int max, const char *name, int line, char kind)
{
    char t[sizeof out[0].name];
    const char *p = name, *q, *last = NULL;
    int i;
    while ((q = strstr(p, "::")) != NULL) {     /* a::b::Foo::bar: from Foo */
        last = p;
        p = q + 2;
    }
    if (last)
        snprintf(t, sizeof t, "%s", last);
    else {
        for (i = c->nsc - 1; i >= 0 && !(c->sc[i].cls && c->sc[i].name[0]); i--)
            ;
        if (i >= 0)
            snprintf(t, sizeof t, "%s::%s", c->sc[i].name, name);
        else
            snprintf(t, sizeof t, "%s", name);
    }
    return add(out, k, max, t, 0, (int)strlen(t), line, kind);
}

/* After the ( at line l, column i: is this a definition? The ) that closes
 * it, then the first {, ; or = outside brackets, looked for over the next
 * few lines: { means a body. Comments and strings are passed over. */
static int has_body(const Line *ln, int n, int l, int i)
{
    int depth = 0, comment = 0, last = l + 8 < n ? l + 8 : n - 1, closed = 0;
    for (; l <= last; l++, i = 0) {
        const char *s = ln[l].s;
        int len = ln[l].len;
        for (; i < len; i++) {
            char ch = s[i];
            if (comment) {
                if (ch == '*' && i + 1 < len && s[i + 1] == '/')
                    comment = 0, i++;
                continue;
            }
            if (ch == '/' && i + 1 < len && s[i + 1] == '/')
                break;
            if (ch == '/' && i + 1 < len && s[i + 1] == '*') {
                comment = 1;
                i++;
            } else if (ch == '"' || ch == '\'') {
                for (i++; i < len && s[i] != ch; i++)
                    if (s[i] == '\\')
                        i++;
            } else if (ch == '(' || ch == '[')
                depth++;
            else if (ch == ')' || ch == ']') {
                if (--depth == 0)
                    closed = 1;
            } else if (closed && depth == 0 && (ch == '{' || ch == ';' || ch == '='))
                return ch == '{';
            else if (!closed && depth == 0 && (ch == ';' || ch == '{'))
                return 0;
        }
    }
    return 0;
}

/* A definition's parameters, from a ( at s[0], or the rest of them on a
 * later line (c->paren still open): where they close, a single : starts
 * the member initialisers, x_(x), y_(2), which are not definitions. They
 * end at the body's { (or a ;), which cpp_braces sees. */
static void after_params(CppScope *c, const char *s, int n, const unsigned char *role)
{
    int i;
    for (i = 0; i < n; i++) {
        if (role[i] != A_SYMBOL && role[i] != A_TEXT)
            continue;
        if (s[i] == '(')
            c->paren++;
        else if (s[i] == ')' && c->paren > 0 && --c->paren == 0)
            break;
    }
    for (i++; i < n && !c->paren; i++)
        if (role[i] == A_TEXT && s[i] == ':' && (i + 1 >= n || s[i + 1] != ':') && (i == 0 || s[i - 1] != ':')) {
            c->init = 1;                    /* until the body's {: cpp_braces */
            return;
        }
}

static int is_not_a_name(const char *s, int n)
{
    static const char *const w[] = { "if", "while", "for", "switch", "return", "sizeof", "catch", "decltype",
        "alignof", "alignas", "noexcept", "static_assert", "typeid", "throw", "new", "delete", "case", "do",
        "else", "requires", "co_return", "co_await", "co_yield", "defined", "__attribute__" };
    int i;
    for (i = 0; i < (int)(sizeof w / sizeof w[0]); i++)
        if ((int)strlen(w[i]) == n && !memcmp(w[i], s, n))
            return 1;
    return 0;
}

/* The function on a declaration line from s[i], if it is one. */
static int cpp_function(CppScope *c, const Line *ln, int nl, int l, int i, const unsigned char *role,
                        Sym *out, int k, int max)
{
    const char *s = ln[l].s;
    int n = ln[l].len, p, angle = 0, op = -1, end = -1, paren = -1, j, m;
    char name[80];
    for (p = i; p < n; p++) {
        char ch = s[p];
        if (role[p] == A_COMMENT || role[p] == A_STRING)
            return k;
        if (is_id(ch) && (p == i || !is_id(s[p - 1])) && word_at(s, n, p, "operator")) {
            int q = skip_sp(s, n, p + 8);
            op = p;
            if (q + 1 < n && s[q] == '(' && s[q + 1] == ')')
                end = q + 2;                 /* operator() */
            else if (q < n && is_id(s[q])) {
                end = q;                     /* operator bool, operator new[] */
                while (end < n && s[end] != '(')
                    end++;
                while (end > q && s[end - 1] == ' ')
                    end--;
            } else
                for (end = q; end < n && s[end] != '(' && s[end] != ' '; end++)
                    ;
            paren = end;
            while (paren < n && s[paren] != '(')
                paren++;
            break;
        }
        if (ch == '<' && p > i && is_id(s[p - 1]))
            angle++;
        else if (ch == '>' && angle > 0)
            angle--;
        else if (angle == 0 && ch == '(') {
            paren = end = p;
            break;
        } else if (angle == 0 && (ch == '=' || ch == ';' || ch == '{' || ch == '}'))
            return k;
    }
    if (paren < 0 || paren >= n || !has_body(ln, nl, l, paren))
        return k;
    /* back from the name's end, over Foo<T>::~Foo */
    j = op >= 0 ? op : end;
    if (op < 0) {
        while (j > i && s[j - 1] == ' ')
            j--;
        end = j;
        if (j > i && s[j - 1] == '>')          /* f<int>(...): a specialisation */
            for (m = 0; j > i; j--) {
                if (s[j - 1] == '>') m++;
                else if (s[j - 1] == '<' && --m == 0) { j--; break; }
            }
        while (j > i && is_id(s[j - 1]))
            j--;
        if (j > i && s[j - 1] == '~')
            j--;
    }
    while (j - 2 >= i && s[j - 1] == ':' && s[j - 2] == ':') {
        j -= 2;
        if (j > i && s[j - 1] == '>')
            for (m = 0; j > i; j--) {
                if (s[j - 1] == '>') m++;
                else if (s[j - 1] == '<' && --m == 0) { j--; break; }
            }
        while (j > i && is_id(s[j - 1]))
            j--;
    }
    /* the name, without template arguments or spaces */
    for (m = 0, p = j; p < end && m < (int)sizeof name - 1; p++) {
        if (s[p] == '<' && (op < 0 || p < op)) {      /* operator< is a name */
            int e = angle_end(s, end, p);
            if (e < 0)
                break;
            p = e - 1;
            continue;
        }
        if (s[p] != ' ' && s[p] != '\t')
            name[m++] = s[p];
        else if (m && is_id(name[m - 1]) && p + 1 < end && is_id(s[p + 1]))
            name[m++] = ' ';                           /* operator bool */
    }
    name[m] = 0;
    if (!m || isdigit((unsigned char)name[0]) || name[0] == ':' || is_not_a_name(name, m))
        return k;
    after_params(c, s + paren, n - paren, role + paren);
    return add_scoped(c, out, k, max, name, l, 'f');
}

/* One line where a declaration can be. */
static int cpp_decl(CppScope *c, const Line *ln, int nl, int l, const unsigned char *role, Sym *out, int k, int max)
{
    const char *s = ln[l].s;
    int n = ln[l].len, i = skip_sp(s, n, 0), e, j, a;
    char t[40];
    if (i >= n || role[i] == A_COMMENT || role[i] == A_STRING)
        return k;
    if (s[i] == '#' || s[i] == '}')
        return c_line(s + i, n - i, role + i, l, out, k, max);   /* #define, } Name; */
    if (word_at(s, n, i, "template")) {
        i = skip_sp(s, n, word_end(s, n, i));
        if (i < n && s[i] == '<' && (i = angle_end(s, n, i)) < 0)
            return k;                        /* the parameters go on: the next line says what */
        i = skip_sp(s, n, i);
        if (i >= n || role[i] == A_COMMENT)
            return k;
    }
    for (;;) {                               /* words that come before what it is */
        e = word_end(s, n, i);
        if (word_at(s, n, i, "extern") && skip_sp(s, n, e) < n && s[skip_sp(s, n, e)] == '"') {
            for (j = skip_sp(s, n, e) + 1; j < n && s[j] != '"'; j++)
                ;
            j = skip_sp(s, n, j + 1);
            if (j >= n || s[j] == '{' || role[j] == A_COMMENT) {
                c->pend = 1;                 /* extern "C" {: what's inside is at the top */
                c->pending[0] = 0;
                c->pcls = 0;
                return k;
            }
            i = j;
        } else if (word_at(s, n, i, "export") || word_at(s, n, i, "inline") || word_at(s, n, i, "static")
                   || word_at(s, n, i, "extern") || word_at(s, n, i, "constexpr") || word_at(s, n, i, "consteval")
                   || word_at(s, n, i, "virtual") || word_at(s, n, i, "explicit") || word_at(s, n, i, "friend"))
            i = skip_sp(s, n, e);
        else
            break;
    }
    if (i >= n)
        return k;
    e = word_end(s, n, i);
    if (word_at(s, n, i, "public") || word_at(s, n, i, "private") || word_at(s, n, i, "protected"))
        return k;
    if (word_at(s, n, i, "typedef"))
        return c_line(s + i, n - i, role + i, l, out, k, max);
    if (word_at(s, n, i, "using")) {
        int q = skip_sp(s, n, e), qe = word_end(s, n, q);
        if (qe > q && !word_at(s, n, q, "namespace") && skip_sp(s, n, qe) < n && s[skip_sp(s, n, qe)] == '=') {
            snprintf(t, sizeof t, "%.*s", qe - q, s + q);
            return add_scoped(c, out, k, max, t, l, 't');
        }
        return k;
    }
    if (word_at(s, n, i, "namespace")) {
        j = skip_sp(s, n, e);
        for (a = j; a < n && (is_id(s[a]) || s[a] == ':'); a++)
            ;                                /* namespace a::b (C++17) */
        if (skip_sp(s, n, a) < n && s[skip_sp(s, n, a)] == '=')
            return k;                        /* an alias */
        snprintf(c->pending, sizeof c->pending, "%.*s", a - j, s + j);
        c->pend = 1;
        c->pcls = 0;
        return a > j ? add(out, k, max, s, j, a, l, 'n') : k;
    }
    {
        int cls = word_at(s, n, i, "class") || word_at(s, n, i, "struct") || word_at(s, n, i, "union");
        if (cls || word_at(s, n, i, "enum")) {
            j = skip_sp(s, n, e);
            if (!cls && (word_at(s, n, j, "class") || word_at(s, n, j, "struct")))
                j = skip_sp(s, n, word_end(s, n, j));
            e = word_end(s, n, j);
            a = skip_sp(s, n, e);
            if (word_at(s, n, a, "final"))
                a = skip_sp(s, n, word_end(s, n, a));
            if (a >= n || s[a] == '{' || role[a] == A_COMMENT || (s[a] == ':' && (a + 1 >= n || s[a + 1] != ':'))) {
                if (last_code(s, n, role) == ';' && !memchr(s + a, '{', n - a))
                    return k;                /* class Foo; or enum E : int; */
                if (cls) {
                    snprintf(c->pending, sizeof c->pending, "%.*s", e - j, s + j);
                    c->pend = 1;
                    c->pcls = 1;
                }
                if (e > j) {
                    snprintf(t, sizeof t, "%.*s", e - j, s + j);
                    k = add_scoped(c, out, k, max, t, l, 't');
                }
                return k;
            }
            /* struct Point make(...): a function that returns one */
        }
    }
    return cpp_function(c, ln, nl, l, i, role, out, k, max);
}

/* #if 0 ... #endif (or #else) is passed over whole: what is switched off
 * may be half a function, and its braces would put the count out. Returns
 * 1 if the line is in such a block, or starts or ends one. */
static int cpp_if0(CppScope *c, const char *s, int n, const unsigned char *role)
{
    int i = skip_sp(s, n, 0), w, e;
    if (i >= n || s[i] != '#' || role[i] != A_PREPROC)
        return c->if0 > 0;
    w = skip_sp(s, n, i + 1);
    e = word_end(s, n, w);
    if (!c->if0) {
        int z = skip_sp(s, n, e);
        if (word_at(s, n, w, "if") && z < n && s[z] == '0' && (z + 1 >= n || !is_id(s[z + 1]))) {
            c->if0 = 1;
            return 1;
        }
        return 0;
    }
    if (word_at(s, n, w, "if") || word_at(s, n, w, "ifdef") || word_at(s, n, w, "ifndef"))
        c->if0++;
    else if (word_at(s, n, w, "endif"))
        c->if0--;
    else if (c->if0 == 1 && (word_at(s, n, w, "else") || word_at(s, n, w, "elif"))) {
        c->if0 = 0;                          /* what follows is switched on */
        if (c->nifs < IFS) {                 /* ... and its #endif closes an #if, for cpp_braces */
            c->ifs[c->nifs].depth = c->depth;
            c->ifs[c->nifs].nsc = c->nsc;
        }
        c->nifs++;
    }
    return 1;
}

/* The braces on a line, and the scopes they open and close. */
static void cpp_braces(CppScope *c, const char *s, int n, const unsigned char *role)
{
    int i = skip_sp(s, n, 0), define = c->in_define, hash = i < n && s[i] == '#' && role[i] == A_PREPROC;
    /* (hash: a # coloured as a directive; one inside a comment is not) */
    c->in_define = (define || hash) && n > 0 && s[n - 1] == '\\';
    if (!define && hash) {
        /* #if and #else branches that each open a brace: each branch starts
         * from the braces there were at the #if */
        int w = skip_sp(s, n, i + 1);
        if (word_at(s, n, w, "if") || word_at(s, n, w, "ifdef") || word_at(s, n, w, "ifndef")) {
            if (c->nifs < IFS) {
                c->ifs[c->nifs].depth = c->depth;
                c->ifs[c->nifs].nsc = c->nsc;
            }
            c->nifs++;
        } else if ((word_at(s, n, w, "else") || word_at(s, n, w, "elif")) && c->nifs > 0 && c->nifs <= IFS) {
            c->depth = c->ifs[c->nifs - 1].depth;
            c->nsc = c->ifs[c->nifs - 1].nsc;
        } else if (word_at(s, n, w, "endif") && c->nifs > 0)
            c->nifs--;
    }
    if (define || hash)
        return;                              /* a # line's braces are a macro's */
    for (; i < n; i++) {
        if (role[i] != A_SYMBOL)
            continue;
        if (s[i] == '{') {
            int p = i;
            while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t'))
                p--;
            if (c->init && p > 0 && (is_id(s[p - 1]) || s[p - 1] == '>')) {
                c->depth++;                 /* a_{1} or b_<int>{}: a member's initialiser, not the body */
                continue;
            }
            c->depth++;
            if (c->pend && c->nsc < SCOPES) {
                snprintf(c->sc[c->nsc].name, sizeof c->sc[0].name, "%s", c->pending);
                c->sc[c->nsc].depth = c->depth;
                c->sc[c->nsc++].cls = c->pcls;
            }
            c->pend = 0;
            c->init = 0;
        } else if (s[i] == '}') {
            if (c->nsc && c->sc[c->nsc - 1].depth == c->depth)
                c->nsc--;
            if (c->depth > 0)
                c->depth--;
        } else if (s[i] == ';')
            c->pend = c->init = 0;
    }
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
    CppScope cs;
    memset(&cs, 0, sizeof cs);
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
        case L_CPP:
            if (cpp_if0(&cs, s, len, role))
                break;
            i = cs.nsc ? cs.sc[cs.nsc - 1].depth : 0;     /* where declarations are */
            e = skip_sp(s, len, 0);
            if (cs.paren)
                after_params(&cs, s, len, role);   /* a definition's parameters go on */
            else if (cs.depth == i && e < len && (s[e] == ',' || (s[e] == ':' && (e + 1 >= len || s[e + 1] != ':'))))
                cs.init = 1;                       /* : x_(x), and , y_(2): initialisers, not definitions */
            else if (!cs.in_define && !cs.init && (cs.depth == i || (cs.depth == i + 1 && e < len && s[e] == '}')))
                k = cpp_decl(&cs, ln, n, l, role, out, k, max);   /* (} Name; closes a typedef) */
            cpp_braces(&cs, s, len, role);
            break;
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
    case 'n': return "namespace";
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

/* The last part of a C++ name: bar in Foo::bar. */
static const char *last_part(const char *name)
{
    const char *p = name, *q;
    while ((q = strstr(p, "::")) != NULL)
        p = q + 2;
    return p;
}

static int col_of_name(const Buffer *b, int line, const char *name)
{
    const char *p = line < b->n ? strstr(b->ln[line].s, name) : NULL;
    if (!p && line < b->n)
        p = strstr(b->ln[line].s, last_part(name));    /* Foo::bar, defined in Foo's body */
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
        if (same_name(s[i].name, name, nocase) || same_name(last_part(s[i].name), name, nocase)) {
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
        if (lang_family(lang_of(e->d_name)) != lang_family(lang) || st.st_size > 1024L * 1024)
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
    /* C++'s own headers: DJGPP keeps them in include/cxx/<GCC's version> */
    if (env && b->lang == L_CPP) {
        snprintf(t, sizeof t, "%.200s/include/cxx", env);
        if ((d = opendir(t)) != NULL) {
            while ((e = readdir(d)) != NULL && n < 16)
                if (e->d_name[0] != '.')
                    snprintf(dirs[n++], DE_PATH, "%.200s/%.20s", t, e->d_name);
            closedir(d);
        }
    }
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
    if (lang_family(b->lang) == L_C && *s == '#') {
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
        if (files[i] != b && lang_family(files[i]->lang) == lang_family(b->lang) && files[i]->path[0]) {
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
