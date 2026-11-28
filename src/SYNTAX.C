/* DE - syntax colours. syn_line() gives each byte of a line a role (A_TEXT,
 * A_KEYWORD ...), given the state the line starts in (inside a comment
 * that began on an earlier line), and returns the state it ends in.
 *
 * This file is shared, byte for byte, with DS. What it needs from DE.H,
 * the header of the program that includes it:
 *   - the languages: enum L_TEXT, L_C, L_ASM, L_PASCAL, L_BASIC, L_BATCH,
 *     L_MAKE, L_INI, L_CPP, L_COUNT, in that order (lang_name follows it);
 *   - the roles A_TEXT, A_KEYWORD, A_TYPE, A_COMMENT, A_STRING, A_NUMBER,
 *     A_PREPROC and A_SYMBOL;
 *   - void *xrealloc(void *p, size_t n), which does not return NULL, and
 *     const char *base_name(const char *path), the name after the last
 *     / or \;
 *   - the prototypes of what this file defines for the program: lang_name,
 *     lang_of, lang_of_text, lang_family, ext_is, syn_line, lang_comment,
 *     lang_lists, lang_nocase and is_word_char. */
#include <ctype.h>
#include <string.h>
#include "DE.H"

const char *const lang_name[L_COUNT] = { "Text", "C", "Asm", "Pascal", "BASIC", "Batch", "Make", "INI", "C++" };

/* States a line can end in. S_RAW and above: inside a C++ raw string, and
 * which delimiter ends it (see raw_state). A state is one byte per line in
 * the buffer, so there are at most 256. */
enum { S_NORMAL, S_CCOMMENT, S_PBRACE, S_PPAREN, S_RAW };

static int up(int c) { return toupper((unsigned char)c); }
static int is_start(int c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_word(int c) { return isalnum((unsigned char)c) || c == '_'; }

/* Is the path's extension in the list (" C H CPP ", upper case)? Any case
 * matches. PROJECT.C uses this too. */
int ext_is(const char *path, const char *list)
{
    const char *dot = strrchr(path, '.'), *p = list;
    const char *sep = strrchr(path, '/'), *sep2 = strrchr(path, '\\');
    char e[8];
    int i;
    if (sep2 > sep)
        sep = sep2;
    if (!dot || (sep && dot < sep) || !dot[1] || strlen(dot + 1) > 6)
        return 0;
    for (i = 0; dot[1 + i]; i++)
        e[i] = (char)up(dot[1 + i]);
    e[i] = 0;
    /* list: " C H CPP " */
    while ((p = strstr(p, e)) != NULL) {
        if (p[-1] == ' ' && p[strlen(e)] == ' ')
            return 1;
        p++;
    }
    return 0;
}

/* The C++ library's own headers have no extension (<vector> is a file
 * called vector), in a folder called c++ (GCC) or cxx (DJGPP). */
#define STD_PATH 260               /* longer paths are cut: the folder is near the start */
static int std_header(const char *path)
{
    char t[STD_PATH];
    int i;
    if (strchr(base_name(path), '.'))
        return 0;
    for (i = 0; path[i] && i < STD_PATH - 1; i++)
        t[i] = (char)tolower((unsigned char)(path[i] == '\\' ? '/' : path[i]));
    t[i] = 0;
    return strstr(t, "/c++/") || strstr(t, "/cxx/");
}

int lang_of(const char *path)
{
    const char *name = base_name(path);
    char u[16];
    int i;
    for (i = 0; name[i] && i < 15; i++)
        u[i] = (char)up(name[i]);
    u[i] = 0;
    if (!strcmp(u, "MAKEFILE") || !strcmp(u, "GNUMAKEFILE") || ext_is(path, " MAK MK "))
        return L_MAKE;
    if (ext_is(path, " C H "))
        return L_C;
    if (ext_is(path, " CPP CXX CC HPP HH HXX ") || std_header(path))
        return L_CPP;
    if (ext_is(path, " ASM INC S NAS MAC "))
        return L_ASM;
    if (ext_is(path, " PAS PP LPR DPR "))
        return L_PASCAL;
    if (ext_is(path, " BAS BI "))
        return L_BASIC;
    if (ext_is(path, " BAT CMD "))
        return L_BATCH;
    if (ext_is(path, " INI CFG CONF LSM "))
        return L_INI;
    return L_TEXT;
}

/* Does a line of text start with w? */
static int starts(const char *s, long n, const char *w)
{
    long k = (long)strlen(w);
    return n >= k && !memcmp(s, w, k);
}

/* A clear sign of C++ on a line that starts at t[0] (spaces skipped):
 * class, namespace, template, or #include <name> with no dot in the name
 * (<vector>, not <stdio.h>). */
static int cpp_line(const char *t, long n)
{
    long k;
    long i;
    if (starts(t, n, "class ") || starts(t, n, "namespace "))
        return 1;
    if (starts(t, n, "template")) {        /* template <, not a C name template */
        for (k = 8; k < n && (t[k] == ' ' || t[k] == '\t'); k++)
            ;
        return k < n && t[k] == '<';
    }
    if (!starts(t, n, "#include"))
        return 0;
    for (i = 8; i < n && (t[i] == ' ' || t[i] == '\t'); i++)
        ;                                   /* #include<vector> too */
    if (i >= n || t[i] != '<')
        return 0;
    for (k = ++i; k < n && t[k] != '>'; k++)
        if (t[k] == '.' || t[k] == ' ')
            return 0;
    return k < n && k > i;
}

/* A .H file is C, unless the first 4 KB of its text show C++ (cpp_line, on
 * a line that is not inside a comment): DE.TXT, "C and C++". Every other
 * name decides by itself, as lang_of says. */
int lang_of_text(const char *path, const char *t, long n)
{
    long i, e, k;
    int comment = 0;
    if (lang_of(path) != L_C || !ext_is(path, " H "))
        return lang_of(path);
    if (n > 4096)
        n = 4096;
    for (i = 0; i < n; i = e + 1) {
        for (e = i; e < n && t[e] != '\n'; e++)
            ;
        for (k = i; k < e && (t[k] == ' ' || t[k] == '\t'); k++)
            ;
        if (!comment && cpp_line(t + k, e - k))
            return L_CPP;
        /* does the line leave a comment open? */
        for (; k < e; k++) {
            if (comment) {
                if (t[k] == '*' && k + 1 < e && t[k + 1] == '/')
                    comment = 0, k++;
            } else if (t[k] == '/' && k + 1 < e && t[k + 1] == '/')
                break;
            else if (t[k] == '/' && k + 1 < e && t[k + 1] == '*')
                comment = 1, k++;
            else if (t[k] == '"' || t[k] == '\'') {
                char q = t[k];
                for (k++; k < e && t[k] != q; k++)
                    if (t[k] == '\\')
                        k++;
            }
        }
    }
    return L_C;
}

/* C and C++ files see each other's names: a C++ program has C headers. */
int lang_family(int lang)
{
    return lang == L_CPP ? L_C : lang;
}

const char *lang_comment(int lang)
{
    switch (lang) {
    case L_C: case L_CPP: case L_PASCAL: return "//";
    case L_ASM: case L_INI: return ";";
    case L_BASIC: return "'";
    case L_BATCH: return "REM";
    case L_MAKE: return "#";
    default: return NULL;
    }
}

/* Is the word s[0..n) in a list of words (" A B C ")? Each list is made
 * into a hash table the first time it's asked about, so a line's words are
 * looked up at once rather than by reading the list through. */
typedef struct {
    const char *list;
    int nocase, mask;
    unsigned short *slot;      /* where a word starts in the list, + 1; 0: empty */
} WordSet;

static unsigned word_hash(const char *s, int n, int nocase)
{
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)(nocase ? up(s[i]) : s[i]);
        h *= 16777619u;
    }
    return h;
}

static int same_word(const char *a, const char *b, int n, int nocase)
{
    int i;
    for (i = 0; i < n; i++)
        if (nocase ? up(a[i]) != up(b[i]) : a[i] != b[i])
            return 0;
    return a[n] == ' ' || a[n] == 0;
}

static WordSet *word_set(const char *list, int nocase)
{
    static WordSet sets[32];
    static int nsets;
    WordSet *w;
    const char *p;
    int i, words = 0, size = 16;
    for (i = 0; i < nsets; i++)
        if (sets[i].list == list && sets[i].nocase == nocase)
            return &sets[i];
    if (nsets == (int)(sizeof sets / sizeof sets[0]))
        return NULL;
    for (p = list; *p; p++)
        if (*p != ' ' && (p == list || p[-1] == ' '))
            words++;
    while (size < words * 2)
        size *= 2;
    w = &sets[nsets++];
    w->list = list;
    w->nocase = nocase;
    w->mask = size - 1;
    w->slot = xrealloc(NULL, size * sizeof *w->slot);
    memset(w->slot, 0, size * sizeof *w->slot);
    for (p = list; *p;) {
        const char *e;
        unsigned h;
        while (*p == ' ')
            p++;
        for (e = p; *e && *e != ' '; e++)
            ;
        if (e > p) {
            for (h = word_hash(p, (int)(e - p), nocase); w->slot[h & w->mask]; h++)
                ;
            w->slot[h & w->mask] = (unsigned short)(p - list + 1);
        }
        p = e;
    }
    return w;
}

static int in_list(const char *list, const char *s, int n, int nocase)
{
    WordSet *w;
    unsigned h;
    if (n > 31 || n < 1)
        return 0;
    if (!(w = word_set(list, nocase)))
        return 0;
    for (h = word_hash(s, n, nocase); w->slot[h & w->mask]; h++)
        if (same_word(list + w->slot[h & w->mask] - 1, s, n, nocase))
            return 1;
    return 0;
}

static int word_role(int lang, const char *s, int n);

static void paint(unsigned char *role, int from, int to, int a)
{
    while (from < to)
        role[from++] = (unsigned char)a;
}

/* A number: digits, letters (hex, suffixes) and dots. */
static int number(const char *s, int n, int i)
{
    while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_'))
        i++;
    return i;
}

/* A quoted string from s[i]; backslash escapes when esc. */
static int quoted(const char *s, int n, int i, int esc)
{
    char q = s[i++];
    while (i < n && s[i] != q) {
        if (esc && s[i] == '\\' && i + 1 < n)
            i++;
        i++;
    }
    return i < n ? i + 1 : n;
}

/* A number: digits, letters (hex, suffixes) and dots, as number() takes
 * them. In C++ also a digit separator (1'000'000) and the sign of an
 * exponent (1e-9, 0x1p+3). A user-defined suffix (10_km, 2.5s) is letters,
 * so it is part of the number already. */
static int c_number(const char *s, int n, int i, int cpp)
{
    int hex = i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'), start = i;
    if (!cpp)
        return number(s, n, i);
    while (i < n) {
        int c = (unsigned char)s[i];
        if (isalnum(c) || c == '.' || c == '_')
            i++;
        else if (c == '\'' && i > start && i + 1 < n && isalnum((unsigned char)s[i - 1]) && isalnum((unsigned char)s[i + 1]))
            i++;
        else if ((c == '+' || c == '-') && i > start
                 && (hex ? up(s[i - 1]) == 'P' : up(s[i - 1]) == 'E' && (isdigit((unsigned char)s[start]) || s[start] == '.')))
            i++;
        else
            break;
    }
    return i;
}

/* After a C++ string or number: a user-defined suffix ("text"s, 5_km). */
static int suffix(const char *s, int n, int i)
{
    if (i < n && is_start(s[i]))
        while (i < n && is_word(s[i]))
            i++;
    return i;
}

/* C++ raw strings, R"delim( ... )delim": they can cross lines, and only
 * )delim" ends one, so a line that ends inside one must say which delimiter
 * it waits for. Each delimiter gets a number the first time it's seen, the
 * same one for as long as DE runs, and the state is S_RAW + that number.
 * So a state means the same on every line and in every file, and buf_state
 * can still stop early when a state comes out as it was. 0 is the empty
 * delimiter, R"( ... )". The last number, RAW_WILD, is kept for when the
 * table is full: a string with a new delimiter then ends at the first )
 * followed by up to 16 delimiter characters and a " (raw_end). That may
 * be too early, but never later than the right place, and it is the same
 * rule on every line, so stopping early stays right. */
#define RAW_LONGEST 16             /* the longest delimiter the standard allows */
#define RAW_ROOM (256 - S_RAW)
#define RAW_WILD (RAW_ROOM - 1)
static char raw_delim[RAW_WILD][RAW_LONGEST + 1];
static int nraw = 1;

static int raw_state(const char *d, int n)
{
    int i;
    for (i = 0; i < nraw; i++)
        if ((int)strlen(raw_delim[i]) == n && !memcmp(raw_delim[i], d, n))
            return S_RAW + i;
    if (nraw == RAW_WILD)
        return S_RAW + RAW_WILD;    /* no room left: any )delim" ends it */
    memcpy(raw_delim[nraw], d, n);
    raw_delim[nraw][n] = 0;
    return S_RAW + nraw++;
}

static int raw_char(int c)          /* may it be in a delimiter? */
{
    return c >= 32 && c != ' ' && c != '(' && c != ')' && c != '\\' && c != '"';
}

/* Where the raw string of this state ends, looking from s[i]: just past
 * its )delim", or -1 if not on this line. */
static int raw_end(const char *s, int n, int i, int state)
{
    const char *d;
    int dn, k;
    if (state - S_RAW == RAW_WILD) {
        for (; i < n; i++)
            if (s[i] == ')') {
                for (k = i + 1; k < n && k - i - 1 < RAW_LONGEST && raw_char((unsigned char)s[k]); k++)
                    ;
                if (k < n && s[k] == '"')
                    return k + 1;
            }
        return -1;
    }
    d = raw_delim[state - S_RAW];
    dn = (int)strlen(d);
    for (; i + dn + 1 < n; i++)
        if (s[i] == ')' && !memcmp(s + i + 1, d, dn) && s[i + dn + 1] == '"')
            return i + dn + 2;
    return -1;
}

/* s[q] is the " of R"delim(: the ( after the delimiter, or -1 if this is
 * not a raw string after all. */
static int raw_open(const char *s, int n, int q)
{
    int k;
    for (k = q + 1; k < n && k - q - 1 <= RAW_LONGEST; k++) {
        int c = (unsigned char)s[k];
        if (c == '(')
            return k;
        if (c == ' ' || c == ')' || c == '\\' || c < 32)
            return -1;
    }
    return -1;
}

/* Is the word s[0..n) one of these (" R LR ")? */
static int one_of(const char *s, int n, const char *list)
{
    const char *p;
    for (p = list; (p = strstr(p, " ")) != NULL && p[1]; p++)
        if (!strncmp(p + 1, s, n) && p[1 + n] == ' ')
            return 1;
    return 0;
}

static const char *C_KEYWORDS =
    " auto break case const continue default do else enum extern for goto if inline"
    " register restrict return sizeof static struct switch typedef union volatile while"
    " asm _asm __asm near far huge interrupt cdecl pascal ";
static const char *C_TYPES =
    " char short int long float double void signed unsigned bool _Bool size_t FILE"
    " int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t BYTE WORD DWORD ";
static const char *C_CONSTS = " NULL true false TRUE FALSE EOF ";

/* C++17's keywords, the C++20 ones in common use, the alternative tokens
 * (and, not ...), and override and final, which are keywords only in some
 * places but are coloured everywhere. import and module are not coloured:
 * they are ordinary names in most code. The DOS words (near, far ...) are
 * Turbo C++'s. */
static const char *CPP_KEYWORDS =
    " alignas alignof asm auto break case catch class const consteval constexpr constinit"
    " const_cast continue co_await co_return co_yield concept decltype default delete do"
    " dynamic_cast else enum explicit export extern final for friend goto if inline mutable"
    " namespace new noexcept operator override private protected public register"
    " reinterpret_cast requires return sizeof static static_assert static_cast struct switch"
    " template this thread_local throw try typedef typeid typename union using virtual"
    " volatile while and and_eq bitand bitor compl not not_eq or or_eq xor xor_eq"
    " _asm __asm near far huge interrupt cdecl pascal ";
static const char *CPP_TYPES =
    " bool char char8_t char16_t char32_t wchar_t short int long float double void signed"
    " unsigned size_t ptrdiff_t nullptr_t FILE int8_t int16_t int32_t int64_t uint8_t"
    " uint16_t uint32_t uint64_t BYTE WORD DWORD ";
static const char *CPP_CONSTS = " nullptr true false NULL TRUE FALSE EOF ";

/* C, and C++ (cpp): the same scanner, with C++'s words, raw strings, digit
 * separators and suffixes. */
static int syn_c(const char *s, int n, int state, unsigned char *role, int cpp)
{
    int i = 0, j, first = 1, lang = cpp ? L_CPP : L_C;
    if (state == S_CCOMMENT) {
        for (j = 0; j + 1 < n && !(s[j] == '*' && s[j + 1] == '/'); j++)
            ;
        if (j + 1 >= n) {
            paint(role, 0, n, A_COMMENT);
            return S_CCOMMENT;
        }
        paint(role, 0, j + 2, A_COMMENT);
        i = j + 2;
    } else if (state >= S_RAW) {
        if ((j = raw_end(s, n, 0, state)) < 0) {
            paint(role, 0, n, A_STRING);
            return state;
        }
        j = suffix(s, n, j);
        paint(role, 0, j, A_STRING);
        i = j;
        first = 0;
    }
    while (i < n) {
        int c = (unsigned char)s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            paint(role, i, n, A_COMMENT);
            return S_NORMAL;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            for (j = i + 2; j + 1 < n && !(s[j] == '*' && s[j + 1] == '/'); j++)
                ;
            if (j + 1 >= n) {
                paint(role, i, n, A_COMMENT);
                return S_CCOMMENT;
            }
            paint(role, i, j + 2, A_COMMENT);
            i = j + 2;
            continue;
        }
        if (c == '#' && first) {
            /* #include <file.h>: the directive, then its file */
            j = i + 1;
            while (j < n && (s[j] == ' ' || s[j] == '\t'))
                j++;
            while (j < n && is_word(s[j]))
                j++;
            paint(role, i, j, A_PREPROC);
            i = j;
            while (i < n && (s[i] == ' ' || s[i] == '\t'))
                role[i++] = A_TEXT;
            if (i < n && s[i] == '<') {
                for (j = i; j < n && s[j] != '>'; j++)
                    ;
                j = j < n ? j + 1 : n;
                paint(role, i, j, A_STRING);
                i = j;
            }
            first = 0;
            continue;
        }
        if (c != ' ' && c != '\t')
            first = 0;
        if (c == '"' || c == '\'') {
            j = quoted(s, n, i, 1);
            if (cpp)
                j = suffix(s, n, j);
            paint(role, i, j, A_STRING);
            i = j;
        } else if (isdigit(c) || (c == '.' && i + 1 < n && isdigit((unsigned char)s[i + 1]))) {
            j = c_number(s, n, i, cpp);
            paint(role, i, j, A_NUMBER);
            i = j;
        } else if (is_start(c)) {
            int k;
            for (j = i; j < n && is_word(s[j]); j++)
                ;
            if (cpp && j < n && s[j] == '"' && one_of(s + i, j - i, " R LR uR UR u8R ")
                && (k = raw_open(s, n, j)) >= 0) {
                int st = raw_state(s + j + 1, k - j - 1), e = raw_end(s, n, k + 1, st);
                if (e < 0) {
                    paint(role, i, n, A_STRING);
                    return st;
                }
                e = suffix(s, n, e);
                paint(role, i, e, A_STRING);
                i = e;
            } else if (cpp && j < n && (s[j] == '"' || s[j] == '\'') && one_of(s + i, j - i, " L u U u8 ")) {
                k = suffix(s, n, quoted(s, n, j, 1));       /* L"wide", u8"text" */
                paint(role, i, k, A_STRING);
                i = k;
            } else {
                paint(role, i, j, word_role(lang, s + i, j - i));
                i = j;
            }
        } else {
            role[i++] = (unsigned char)(strchr("(){}[];,", c) && c ? A_SYMBOL : A_TEXT);
        }
    }
    return S_NORMAL;
}

static const char *ASM_DIRECTIVES =
    " section segment global extern bits org times equ db dw dd dq dt resb resw resd resq"
    " incbin align cpu use16 use32 proc endp macro endm struc endstruc ends end assume"
    " model stack code data dup offset ptr byte word dword qword far near short seg public"
    " include .model .code .data .stack .186 .286 .386 .486 .586 .8086 ";
static const char *ASM_REGISTERS =
    " al ah ax eax bl bh bx ebx cl ch cx ecx dl dh dx edx si esi di edi bp ebp sp esp"
    " cs ds es fs gs ss ip eip cr0 cr2 cr3 st st0 st1 st2 st3 ";
static const char *ASM_OPS =
    " mov add adc sub sbb inc dec mul imul div idiv cmp test and or xor not neg shl shr"
    " sal sar rol ror rcl rcr jmp je jne jz jnz jc jnc jg jge jl jle ja jae jb jbe jo jno"
    " js jns jp jnp jcxz jecxz loop loope loopne loopz loopnz call ret retf retn iret int"
    " into push pop pusha popa pushf popf pushad popad lea lds les lfs lgs nop hlt cli sti"
    " cld std clc stc cmc xchg in out ins outs movs movsb movsw movsd stos stosb stosw"
    " stosd lods lodsb lodsw lodsd scas scasb scasw scasd cmps cmpsb cmpsw cmpsd rep repe"
    " repz repne repnz cbw cwd cwde cdq xlat xlatb aaa aad aam aas daa das sahf lahf bt bts"
    " btr btc bsf bsr sete setne setz setnz movzx movsx enter leave bound wait lock fld"
    " fst fstp fild fist fistp fadd fsub fmul fdiv fxch finit fwait ";

static int syn_asm(const char *s, int n, unsigned char *role)
{
    int i = 0, j;
    while (i < n) {
        int c = (unsigned char)s[i];
        if (c == ';') {
            paint(role, i, n, A_COMMENT);
            break;
        }
        if (c == '"' || c == '\'' || c == '`') {
            j = quoted(s, n, i, c == '`');
            paint(role, i, j, A_STRING);
            i = j;
        } else if (isdigit(c)) {
            j = number(s, n, i);
            paint(role, i, j, A_NUMBER);
            i = j;
        } else if (c == '%' && i + 1 < n && is_start(s[i + 1])) {
            for (j = i + 1; j < n && is_word(s[j]); j++)
                ;
            paint(role, i, j, A_PREPROC);
            i = j;
        } else if (is_start(c) || c == '.' || c == '@' || c == '$' || c == '?') {
            for (j = i + 1; j < n && (is_word(s[j]) || s[j] == '.' || s[j] == '@' || s[j] == '$' || s[j] == '?'); j++)
                ;
            if (j < n && s[j] == ':')
                paint(role, i, j + 1, A_PREPROC);           /* a label */
            else
                paint(role, i, j, word_role(L_ASM, s + i, j - i));
            i = j < n && s[j] == ':' ? j + 1 : j;
        } else {
            role[i++] = (unsigned char)(strchr("[](),+-*:", c) && c ? A_SYMBOL : A_TEXT);
        }
    }
    return S_NORMAL;
}

static const char *PAS_KEYWORDS =
    " and array asm begin case const constructor destructor div do downto else end except"
    " exit exports file finally for function goto if implementation in inherited inline"
    " interface label library mod object of on or packed procedure program property raise"
    " record repeat set shl shr then to try type unit until uses var while with xor"
    " absolute far near forward external interrupt private public virtual not ";
static const char *PAS_TYPES =
    " integer word byte shortint longint longword cardinal int64 qword real single double"
    " extended comp char boolean string text pointer ansistring shortstring ";
static const char *PAS_CONSTS = " true false nil maxint ";

static int syn_pascal(const char *s, int n, int state, unsigned char *role)
{
    int i = 0, j;
    for (;;) {
        if (state == S_PBRACE || state == S_PPAREN) {
            for (j = i; j < n; j++)
                if (state == S_PBRACE ? s[j] == '}' : (s[j] == '*' && j + 1 < n && s[j + 1] == ')'))
                    break;
            if (j >= n) {
                paint(role, i, n, A_COMMENT);
                return state;
            }
            j += state == S_PBRACE ? 1 : 2;
            paint(role, i, j, A_COMMENT);
            i = j;
            state = S_NORMAL;
        }
        if (i >= n)
            return S_NORMAL;
        {
            int c = (unsigned char)s[i];
            if (c == '{' && i + 1 < n && s[i + 1] == '$') {
                for (j = i; j < n && s[j] != '}'; j++)
                    ;
                j = j < n ? j + 1 : n;
                paint(role, i, j, A_PREPROC);
                i = j;
            } else if (c == '{') {
                state = S_PBRACE;
            } else if (c == '(' && i + 1 < n && s[i + 1] == '*') {
                paint(role, i, i + 2, A_COMMENT);
                i += 2;
                state = S_PPAREN;
            } else if (c == '/' && i + 1 < n && s[i + 1] == '/') {
                paint(role, i, n, A_COMMENT);
                return S_NORMAL;
            } else if (c == '\'') {
                j = quoted(s, n, i, 0);
                paint(role, i, j, A_STRING);
                i = j;
            } else if (c == '#' || c == '$' || isdigit(c)) {
                j = number(s, n, i + 1);
                paint(role, i, j, A_NUMBER);
                i = j;
            } else if (is_start(c)) {
                for (j = i; j < n && is_word(s[j]); j++)
                    ;
                paint(role, i, j, word_role(L_PASCAL, s + i, j - i));
                i = j;
            } else {
                role[i++] = (unsigned char)(strchr("()[];,:", c) && c ? A_SYMBOL : A_TEXT);
            }
        }
    }
}

static const char *BAS_KEYWORDS =
    " print input if then else elseif end for to step next while wend do loop until goto"
    " gosub return dim as sub function call declare select case let cls color locate"
    " screen open close and or not mod xor shared const type data read restore exit"
    " randomize sleep beep line circle pset paint get put poke peek def seg on error"
    " resume system chain common redim erase swap width view window key option base"
    " is static byval byref public private namespace using ";
static const char *BAS_TYPES = " integer long single double string byte any ";

static int syn_basic(const char *s, int n, unsigned char *role)
{
    int i = 0, j;
    while (i < n) {
        int c = (unsigned char)s[i];
        if (c == '\'') {
            paint(role, i, n, A_COMMENT);
            break;
        }
        if (c == '"') {
            j = quoted(s, n, i, 0);
            paint(role, i, j, A_STRING);
            i = j;
        } else if (isdigit(c) || (c == '&' && i + 1 < n && up(s[i + 1]) == 'H')) {
            j = number(s, n, i + 1);
            paint(role, i, j, A_NUMBER);
            i = j;
        } else if (is_start(c)) {
            for (j = i; j < n && (is_word(s[j]) || s[j] == '$' || s[j] == '%' || s[j] == '!' || s[j] == '#'); j++)
                ;
            if (j - i == 3 && in_list(" rem ", s + i, 3, 1)) {
                paint(role, i, n, A_COMMENT);
                break;
            }
            paint(role, i, j, word_role(L_BASIC, s + i, j - i));
            i = j;
        } else {
            role[i++] = (unsigned char)(strchr("(),;:", c) && c ? A_SYMBOL : A_TEXT);
        }
    }
    return S_NORMAL;
}

static const char *BAT_KEYWORDS =
    " echo set if else goto call for in do not exist errorlevel shift pause cls cd chdir"
    " md mkdir rd rmdir copy xcopy del erase ren rename move type path prompt choice exit"
    " off on defined lh loadhigh ";

static int syn_batch(const char *s, int n, unsigned char *role)
{
    int i = 0, j;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '@'))
        role[i] = (unsigned char)(s[i] == '@' ? A_SYMBOL : A_TEXT), i++;
    if (i + 1 < n && s[i] == ':' && s[i + 1] == ':') {
        paint(role, 0, n, A_COMMENT);
        return S_NORMAL;
    }
    if (i < n && s[i] == ':') {
        paint(role, i, n, A_PREPROC);
        return S_NORMAL;
    }
    if (i + 3 <= n && in_list(" rem ", s + i, 3, 1) && (i + 3 == n || s[i + 3] == ' ')) {
        paint(role, i, n, A_COMMENT);
        return S_NORMAL;
    }
    while (i < n) {
        int c = (unsigned char)s[i];
        if (c == '%') {
            if (i + 1 < n && (isdigit((unsigned char)s[i + 1]) || s[i + 1] == '%' || s[i + 1] == '~'))
                j = i + 2;
            else {
                for (j = i + 1; j < n && s[j] != '%' && s[j] != ' '; j++)
                    ;
                j = j < n && s[j] == '%' ? j + 1 : j;
            }
            paint(role, i, j, A_TYPE);
            i = j;
        } else if (c == '"') {
            j = quoted(s, n, i, 0);
            paint(role, i, j, A_STRING);
            i = j;
        } else if (is_start(c)) {
            for (j = i; j < n && is_word(s[j]); j++)
                ;
            paint(role, i, j, word_role(L_BATCH, s + i, j - i));
            i = j;
        } else {
            role[i++] = (unsigned char)(strchr("<>|=", c) && c ? A_SYMBOL : A_TEXT);
        }
    }
    return S_NORMAL;
}

static const char *MAKE_DIRECTIVES =
    " include -include sinclude ifeq ifneq ifdef ifndef else endif define endef export unexport override vpath"
    " .PHONY .RECIPEPREFIX ";

static int syn_make(const char *s, int n, unsigned char *role)
{
    int i = 0, j;
    /* target: ... at the start of a line */
    if (n && s[0] != '\t' && s[0] != '>' && s[0] != '#') {
        for (j = 0; j < n && s[j] != ':' && s[j] != '=' && s[j] != '#'; j++)
            ;
        if (j < n && s[j] == ':' && (j + 1 >= n || s[j + 1] != '=')) {
            int k = 0;
            while (k < j && (s[k] == ' ' || s[k] == '\t'))
                k++;
            if (k < j) {
                paint(role, 0, j, A_KEYWORD);
                role[j] = A_SYMBOL;
                i = j + 1;
            }
        }
    }
    while (i < n) {
        int c = (unsigned char)s[i];
        if (c == '#') {
            paint(role, i, n, A_COMMENT);
            break;
        }
        if (c == '$' && i + 1 < n) {
            if (s[i + 1] == '(' || s[i + 1] == '{') {
                char close = s[i + 1] == '(' ? ')' : '}';
                for (j = i + 2; j < n && s[j] != close; j++)
                    ;
                j = j < n ? j + 1 : n;
            } else
                j = i + 2;
            paint(role, i, j, A_TYPE);
            i = j;
        } else if (c == '"' || c == '\'') {
            j = quoted(s, n, i, 0);
            paint(role, i, j, A_STRING);
            i = j;
        } else if (is_start(c) || c == '-' || c == '.') {
            for (j = i + 1; j < n && (is_word(s[j]) || s[j] == '-' || s[j] == '.'); j++)
                ;
            paint(role, i, j, word_role(L_MAKE, s + i, j - i));
            i = j;
        } else {
            role[i++] = (unsigned char)(strchr("=:+?", c) && c ? A_SYMBOL : A_TEXT);
        }
    }
    return S_NORMAL;
}

static int syn_ini(const char *s, int n, unsigned char *role)
{
    int i = 0, j;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        role[i++] = A_TEXT;
    if (i < n && (s[i] == ';' || s[i] == '#')) {
        paint(role, i, n, A_COMMENT);
        return S_NORMAL;
    }
    if (i < n && s[i] == '[') {
        paint(role, i, n, A_KEYWORD);
        return S_NORMAL;
    }
    for (j = i; j < n && s[j] != '=' && s[j] != ':'; j++)
        ;
    if (j < n) {
        paint(role, i, j, A_TYPE);
        role[j] = A_SYMBOL;
        i = j + 1;
    }
    while (i < n) {
        if (isdigit((unsigned char)s[i]) && (i == 0 || !is_word(s[i - 1]))) {
            j = number(s, n, i);
            paint(role, i, j, A_NUMBER);
            i = j;
        } else
            role[i++] = A_TEXT;
    }
    return S_NORMAL;
}

/* A word's colour in a language: each language's lists made into one hash
 * table the first time, so a word is looked up once, not list by list.
 * A word in two lists takes the first list's colour. */
typedef struct { const char *w; unsigned char n, role; } KeyWord;
typedef struct { int ready, nocase, mask, n; unsigned short *slot; KeyWord *e; } KeyTable;

static void keys_add(KeyTable *k, const char *list, int role, int count_only)
{
    const char *p = list, *e;
    for (;;) {
        unsigned h;
        int found = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            return;
        for (e = p; *e && *e != ' '; e++)
            ;
        if (count_only)
            k->n++;
        else if (e - p <= 31) {
            for (h = word_hash(p, (int)(e - p), k->nocase); k->slot[h & k->mask]; h++) {
                const KeyWord *x = &k->e[k->slot[h & k->mask] - 1];
                if (x->n == e - p && same_word(x->w, p, (int)(e - p), k->nocase)) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                k->e[k->n].w = p;
                k->e[k->n].n = (unsigned char)(e - p);
                k->e[k->n].role = (unsigned char)role;
                k->slot[h & k->mask] = (unsigned short)++k->n;
            }
        }
        p = e;
    }
}

static void keys_lists(int lang, KeyTable *k, int count_only)
{
    switch (lang) {
    case L_C:
        keys_add(k, C_KEYWORDS, A_KEYWORD, count_only);
        keys_add(k, C_TYPES, A_TYPE, count_only);
        keys_add(k, C_CONSTS, A_NUMBER, count_only);
        break;
    case L_CPP:
        keys_add(k, CPP_KEYWORDS, A_KEYWORD, count_only);
        keys_add(k, CPP_TYPES, A_TYPE, count_only);
        keys_add(k, CPP_CONSTS, A_NUMBER, count_only);
        break;
    case L_ASM:
        keys_add(k, ASM_OPS, A_KEYWORD, count_only);
        keys_add(k, ASM_REGISTERS, A_TYPE, count_only);
        keys_add(k, ASM_DIRECTIVES, A_PREPROC, count_only);
        break;
    case L_PASCAL:
        keys_add(k, PAS_KEYWORDS, A_KEYWORD, count_only);
        keys_add(k, PAS_TYPES, A_TYPE, count_only);
        keys_add(k, PAS_CONSTS, A_NUMBER, count_only);
        break;
    case L_BASIC:
        keys_add(k, BAS_KEYWORDS, A_KEYWORD, count_only);
        keys_add(k, BAS_TYPES, A_TYPE, count_only);
        break;
    case L_BATCH:
        keys_add(k, BAT_KEYWORDS, A_KEYWORD, count_only);
        break;
    case L_MAKE:
        keys_add(k, MAKE_DIRECTIVES, A_PREPROC, count_only);
        break;
    }
}

static int word_role(int lang, const char *s, int n)
{
    static KeyTable tables[L_COUNT];
    KeyTable *k = &tables[lang];
    unsigned h;
    if (!k->ready) {
        int size = 16;
        k->ready = 1;
        k->nocase = lang_nocase(lang);
        keys_lists(lang, k, 1);
        while (size < k->n * 2)
            size *= 2;
        k->mask = size - 1;
        k->slot = xrealloc(NULL, size * sizeof *k->slot);
        memset(k->slot, 0, size * sizeof *k->slot);
        k->e = xrealloc(NULL, (k->n + 1) * sizeof *k->e);
        k->n = 0;
        keys_lists(lang, k, 0);
    }
    if (n < 1 || n > 31)
        return A_TEXT;
    for (h = word_hash(s, n, k->nocase); k->slot[h & k->mask]; h++) {
        const KeyWord *x = &k->e[k->slot[h & k->mask] - 1];
        if (x->n == n && same_word(x->w, s, n, k->nocase))
            return x->role;
    }
    return A_TEXT;
}

int syn_line(int lang, const char *s, int n, int state, unsigned char *role)
{
    paint(role, 0, n, A_TEXT);
    switch (lang) {
    case L_C: return syn_c(s, n, state, role, 0);
    case L_CPP: return syn_c(s, n, state, role, 1);
    case L_ASM: return syn_asm(s, n, role);
    case L_PASCAL: return syn_pascal(s, n, state, role);
    case L_BASIC: return syn_basic(s, n, role);
    case L_BATCH: return syn_batch(s, n, role);
    case L_MAKE: return syn_make(s, n, role);
    case L_INI: return syn_ini(s, n, role);
    default: return S_NORMAL;
    }
}

/* ---- for suggestions -------------------------------------------------------- */

int lang_lists(int lang, const char *out[4])
{
    switch (lang) {
    case L_C: out[0] = C_KEYWORDS; out[1] = C_TYPES; out[2] = C_CONSTS; return 3;
    case L_CPP: out[0] = CPP_KEYWORDS; out[1] = CPP_TYPES; out[2] = CPP_CONSTS; return 3;
    case L_ASM: out[0] = ASM_OPS; out[1] = ASM_DIRECTIVES; return 2;
    case L_PASCAL: out[0] = PAS_KEYWORDS; out[1] = PAS_TYPES; out[2] = PAS_CONSTS; return 3;
    case L_BASIC: out[0] = BAS_KEYWORDS; out[1] = BAS_TYPES; return 2;
    case L_BATCH: out[0] = BAT_KEYWORDS; return 1;
    default: return 0;
    }
}

int lang_nocase(int lang)
{
    return lang == L_ASM || lang == L_PASCAL || lang == L_BASIC || lang == L_BATCH || lang == L_INI;
}

/* Part of a word: letters, digits and _, and BASIC's $ (INKEY$). A table,
 * as it's asked about every character of a line. */
int is_word_char(int lang, int c)
{
    static unsigned char tab[256];
    static int ready;
    if (!ready) {
        int i;
        for (i = 0; i < 256; i++)
            tab[i] = (unsigned char)(is_word(i) ? 3 : 0);
        tab['$'] = 2;
        ready = 1;
    }
    return tab[c & 255] & (lang == L_BASIC ? 3 : 1);
}
