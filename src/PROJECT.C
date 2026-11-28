/* DE - building and running from inside DE, and the Messages window: what
 * the last build said, or what Find in files found.
 *
 * A project is DOSDev's: the folder with project.ini, looked for from the
 * file's folder up. F9 builds with the project's build command when it
 * builds in DOS (build_on = dos), or else with the project's BUILD.BAT,
 * which every DOSDev template has for building in the Workbench. With no
 * project, the file's own folder is used. Commands set in Project >
 * Commands come before all of these, until DE ends. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "DE.H"

/* ---- paths -------------------------------------------------------------------- */

static int absolute(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

/* dir and name as one path, DE_PATH long. A name that is already absolute
 * is the path as it is. The Open dialog uses this too. */
void path_join(const char *dir, const char *name, char *out)
{
    size_t n;
    if (absolute(name) || !dir[0]) {
        snprintf(out, DE_PATH, "%s", name);
        return;
    }
    n = strlen(dir);
    snprintf(out, DE_PATH, "%s%s%s", dir, n && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : "/", name);
}

static void dir_of(const char *path, char *out)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    if (b > a)
        a = b;
    if (!a) {
        snprintf(out, DE_PATH, "%s", isalpha((unsigned char)path[0]) && path[1] == ':' ? "" : ".");
        if (!out[0])
            snprintf(out, DE_PATH, "%.2s", path);
        return;
    }
    if (a == path || (a == path + 2 && path[1] == ':'))
        a++;                            /* the root: keep its slash */
    snprintf(out, DE_PATH, "%.*s", (int)(a - path), path);
}

static long mtime_of(const char *path)
{
    struct stat st;
    return stat(path, &st) ? -1 : (long)st.st_mtime;
}

int is_dir(const char *path)
{
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

static int ext_in(const char *name, const char *list)
{
    const char *dot = strrchr(name, '.'), *p;
    char e[8];
    int i;
    if (!dot || strchr(dot, '/') || strchr(dot, '\\') || strlen(dot + 1) > 6 || !dot[1])
        return 0;
    for (i = 0; dot[1 + i]; i++)
        e[i] = (char)toupper((unsigned char)dot[1 + i]);
    e[i] = 0;
    for (p = list; (p = strstr(p, e)) != NULL; p++)
        if (p[-1] == ' ' && p[strlen(e)] == ' ')
            return 1;
    return 0;
}

/* A source file's name, as compilers say them. */
static int is_source(const char *name)
{
    const char *base = base_name(name);
    if (!*name || strchr(name, ' '))
        return 0;
    if (eq_nocase(base, "MAKEFILE") || eq_nocase(base, "GNUMAKEFILE"))
        return 1;
    return ext_in(name, " C H CPP CXX CC HPP HH ASM INC S NAS MAC PAS PP LPR DPR BAS BI MAK MK ");
}

/* ---- the project --------------------------------------------------------------- */

#define OVERRIDES 8
static struct { char dir[DE_PATH], build[160], run[160]; int has_build, has_run; } over[OVERRIDES];
static int nover;

static void ini_read(const char *path, Project *p, char *build_on, int bsize, char *build, char *run)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *k = line, *v, *e;
        if (!eq || line[0] == '#' || line[0] == ';' || line[0] == '[')
            continue;
        *eq = 0;
        v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        for (e = eq; e > k && (e[-1] == ' ' || e[-1] == '\t'); e--) ;
        *e = 0;
        while (*v == ' ' || *v == '\t') v++;
        for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'); e--) ;
        *e = 0;
        if (!strcmp(k, "name")) snprintf(p->name, sizeof p->name, "%s", v);
        else if (!strcmp(k, "build_on")) snprintf(build_on, bsize, "%s", v);
        else if (!strcmp(k, "build")) snprintf(build, 160, "%s", v);
        else if (!strcmp(k, "run")) snprintf(run, 160, "%s", v);
        else if (!strcmp(k, "output")) snprintf(p->output, sizeof p->output, "%s", v);
        else if (!strcmp(k, "debug")) snprintf(p->debug, sizeof p->debug, "%s", v);
    }
    fclose(f);
}

static int find_ini(const char *start, char *dir, char *ini)
{
    char d[DE_PATH], up[DE_PATH];
    snprintf(d, sizeof d, "%s", start);
    for (;;) {
        path_join(d, "project.ini", ini);
        if (mtime_of(ini) >= 0) {
            snprintf(dir, DE_PATH, "%s", d);
            return 1;
        }
        path_join(d, "PROJECT.INI", ini);
        if (mtime_of(ini) >= 0) {
            snprintf(dir, DE_PATH, "%s", d);
            return 1;
        }
        dir_of(d, up);
        if (!strcmp(up, d) || !up[0] || !strcmp(up, "."))
            return 0;
        snprintf(d, sizeof d, "%s", up);
    }
}

void proj_find(Buffer *b, Project *p)
{
    char start[DE_PATH], ini[DE_PATH], t[DE_PATH], build_on[16] = "", build[160] = "", run[160] = "";
    int i;
    memset(p, 0, sizeof *p);
    if (b && b->path[0]) {
        plat_fullpath(b->path, t);
        dir_of(t, start);
    } else if (!getcwd(start, sizeof start))
        snprintf(start, sizeof start, ".");
    snprintf(p->dir, sizeof p->dir, "%s", start);
    if (find_ini(start, p->dir, ini)) {
        ini_read(ini, p, build_on, sizeof build_on, build, run);
        if (eq_nocase(build_on, "dos") && build[0]) {
            snprintf(p->build, sizeof p->build, "%s", build);
            p->build_from = "project.ini";
        }
        if (run[0]) {
            snprintf(p->run, sizeof p->run, "%s", run);
            p->run_from = "project.ini";
        }
    }
    path_join(p->dir, "BUILD.BAT", t);
    if (!p->build[0] && mtime_of(t) >= 0) {
        snprintf(p->build, sizeof p->build, "BUILD.BAT");
        p->build_from = "BUILD.BAT";
    }
    for (i = 0; i < nover; i++)
        if (ed_same_path(over[i].dir, p->dir)) {
            if (over[i].has_build) {
                snprintf(p->build, sizeof p->build, "%s", over[i].build);
                p->build_from = "Commands";
            }
            if (over[i].has_run) {
                snprintf(p->run, sizeof p->run, "%s", over[i].run);
                p->run_from = "Commands";
            }
        }
}

/* What a file on its own might be built and run with, in the Workbench. */
static void guess(Buffer *b, char *build, char *run)
{
    char name[16], *dot;
    const char *base = b->path[0] ? base_name(b->path) : "";
    build[0] = run[0] = 0;
    snprintf(name, sizeof name, "%s", base);
    if ((dot = strrchr(name, '.')) != NULL)
        *dot = 0;
    if (!base[0])
        return;
    switch (b->lang) {
    case L_C:
        snprintf(build, 160, "gcc -Wall -g -o %s.EXE -x c %s", name, base);
        snprintf(run, 160, "%s.EXE", name);
        break;
    case L_ASM:
        snprintf(build, 160, "nasm -f bin -o %s.COM %s", name, base);
        snprintf(run, 160, "%s.COM", name);
        break;
    case L_PASCAL:
        snprintf(build, 160, "fpc -g %s", base);
        snprintf(run, 160, "%s.EXE", name);
        break;
    case L_BATCH:
        snprintf(run, 160, "%s", base);
        break;
    }
}

void proj_commands(void)
{
    Project p;
    char build[160], run[160], label[80], gb[160], gr[160];
    int i;
    proj_find(B_CUR, &p);
    guess(B_CUR, gb, gr);
    snprintf(build, sizeof build, "%s", p.build[0] ? p.build : gb);
    snprintf(label, sizeof label, "Build with%s%s", p.build_from ? " (now from " : "", p.build_from ? p.build_from : "");
    if (p.build_from)
        strcat(label, ")");
    if (!input_box("Project commands", label, build, sizeof build))
        return;
    snprintf(run, sizeof run, "%s", p.run[0] ? p.run : gr);
    snprintf(label, sizeof label, "Run with%s%s", p.run_from ? " (now from " : "", p.run_from ? p.run_from : "");
    if (p.run_from)
        strcat(label, ")");
    if (!input_box("Project commands", label, run, sizeof run))
        return;
    for (i = 0; i < nover && !ed_same_path(over[i].dir, p.dir); i++)
        ;
    if (i == nover) {
        if (nover == OVERRIDES)
            memmove(&over[0], &over[1], (OVERRIDES - 1) * sizeof over[0]);
        else
            nover++;
        i = nover - 1;
        memset(&over[i], 0, sizeof over[i]);
        snprintf(over[i].dir, sizeof over[i].dir, "%s", p.dir);
    }
    if (strcmp(build, p.build)) {
        snprintf(over[i].build, sizeof over[i].build, "%s", build);
        over[i].has_build = 1;
    }
    if (strcmp(run, p.run)) {
        snprintf(over[i].run, sizeof over[i].run, "%s", run);
        over[i].has_run = 1;
    }
    ed_message(0, "F9 builds with: %s", build[0] ? build : "(nothing)");
}

/* The time of the newest source (or .BAT or .INI) file in a folder and the
 * folders under it, 3 deep; best if none is newer. */
static long newest(const char *dir, int depth, long best)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[DE_PATH];
    if (!d)
        return best;
    while ((e = readdir(d)) != NULL) {
        long t;
        if (e->d_name[0] == '.')
            continue;
        path_join(dir, e->d_name, p);
        if (is_dir(p)) {
            if (depth < 3)
                best = newest(p, depth + 1, best);
            continue;
        }
        if (!is_source(e->d_name) && !ext_in(e->d_name, " BAT INI "))
            continue;
        if ((t = mtime_of(p)) > best)
            best = t;
    }
    closedir(d);
    return best;
}

/* Is the output newer than every source in the project? */
static int up_to_date(const Project *p)
{
    char out[DE_PATH];
    long t;
    if (!p->output[0])
        return 0;
    path_join(p->dir, p->output, out);
    if ((t = mtime_of(out)) < 0)
        return 0;
    return newest(p->dir, 0, -1) <= t;
}

/* The file to catch a build's output in: in TMP (T:\ in DOSDev). */
static void log_path(const Project *p, char *out)
{
    const char *tmp = getenv("TMP");
    if (!tmp || !*tmp)
        tmp = getenv("TEMP");
    path_join(tmp && *tmp ? tmp : p->dir, "DEBUILD.TMP", out);
}

static void first_word(const char *cmd, char *out, int size)
{
    int n = 0;
    while (*cmd == ' ')
        cmd++;
    while (cmd[n] && cmd[n] != ' ' && n < size - 1)
        n++;
    snprintf(out, size, "%.*s", n, cmd);
}

/* Runs cmd in the project's folder: its run command, or its debug one. */
static void run_cmd(const Project *p, const char *cmd)
{
    char old[DE_PATH], what[64];
    int st;
    if (!getcwd(old, sizeof old))
        old[0] = 0;
    if (chdir(p->dir)) {
        ed_message(1, "Can't go to %s.", p->dir);
        return;
    }
    st = plat_exec(cmd, NULL);
    if (old[0] && chdir(old)) {
        /* the folder went away: carry on */
    }
    if (plat_rows() != SR || plat_cols() != SC)
        scr_init();             /* it left the screen another size */
    first_word(cmd, what, sizeof what);
    if (st < 0)
        ed_message(1, "Couldn't start %s.", what);
    else if (st > 0)
        ed_message(0, "%s ended, exit code %d. Alt-F5 shows its screen.", what, st);
    else
        ed_message(0, "%s ended. Alt-F5 shows its screen.", what);
    ed_check_disk();            /* it may have written a file that is open */
}

static void run_it(const Project *p) { run_cmd(p, p->run); }

/* What comes after a build: 1 runs the program, 2 debugs it. */
static void after(const Project *p, int then) { run_cmd(p, then == 2 ? p->debug : p->run); }

void proj_debug(void)
{
    Project p;
    proj_find(B_CUR, &p);
    if (!p.debug[0]) {
        ed_message(1, "Nothing to debug with: add a debug = line to project.ini.");
        return;
    }
    proj_build(2);
}

void proj_build(int then_run)
{
    Project p;
    char log[DE_PATH], old[DE_PATH], other[DE_PATH], t[DE_PATH + 40];
    long start;
    int st, errors, warnings, made;
    proj_find(B_CUR, &p);
    if (!p.build[0]) {
        if (then_run == 2 || (then_run && p.run[0])) {
            after(&p, then_run);    /* nothing to build: just run it */
            return;
        }
        proj_commands();
        proj_find(B_CUR, &p);
        if (!p.build[0])
            return;
    }
    if (ed_save_all()) {
        ed_message(1, "Not every file could be saved, so nothing was built.");
        return;
    }
    if ((then_run == 2 || (then_run && p.run[0])) && up_to_date(&p)) {
        after(&p, then_run);
        return;
    }
    snprintf(t, sizeof t, "%.70s", p.build);
    busy_box("Building", t, p.name[0] ? p.name : base_name(p.dir));
    log_path(&p, log);
    if (!getcwd(old, sizeof old))
        old[0] = 0;
    if (chdir(p.dir)) {
        ed_message(1, "Can't go to %s.", p.dir);
        return;
    }
    start = (long)time(NULL);
    st = plat_exec(p.build, log);
    if (old[0] && chdir(old)) {
        /* carry on */
    }
    ed_check_disk();            /* a build may rewrite a file that is open */
    msgs_set("Build", p.dir, log);
    remove(log);
    /* Free Pascal writes its errors to BUILD.LOG itself (-Fe) */
    path_join(p.dir, "BUILD.LOG", other);
    if (mtime_of(other) >= start - 2)
        msgs_set(NULL, p.dir, other);
    errors = msgs_count(M_ERROR);
    warnings = msgs_count(M_WARNING);
    if (p.output[0]) {
        path_join(p.dir, p.output, t);
        made = mtime_of(t) >= start - 2;
    } else
        made = st == 0;
    if (st < 0) {
        ed_message(1, "Couldn't start: %s", p.build);
        return;
    }
    if (errors || !made) {
        if (!errors) {
            snprintf(t, sizeof t, "%s wasn't made.", p.output[0] ? p.output : "Nothing");
            if (!p.output[0])
                snprintf(t, sizeof t, "The build ended with exit code %d.", st);
            msgs_add(t, NULL, 0, 0, M_ERROR);
            errors = 1;
        }
        ed_message(1, "The build failed: %d error%s, %d warning%s.", errors, errors == 1 ? "" : "s",
                   warnings, warnings == 1 ? "" : "s");
        msgs_list();
        return;
    }
    if (warnings)
        ed_message(0, "Built%s%s, with %d warning%s: F7 lists them, F8 goes to each.", p.output[0] ? " " : "",
                   p.output, warnings, warnings == 1 ? "" : "s");
    else
        ed_message(0, "Built%s%s: no errors, no warnings.", p.output[0] ? " " : "", p.output);
    if (then_run == 2)
        after(&p, then_run);
    else if (then_run) {
        if (!p.run[0]) {
            proj_commands();
            proj_find(B_CUR, &p);
        }
        if (p.run[0])
            run_it(&p);
    }
}

/* ---- what compilers say ----------------------------------------------------------- */

static int word_is(const char *s, const char *w)
{
    size_t n = strlen(w);
    size_t i;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)s[i]) != w[i])
            return 0;
    return !isalpha((unsigned char)s[n]);
}

/* The kind a word starts: error, warning ... and how long the word is. */
static int kind_at(const char *s, int *len)
{
    static const struct { const char *w; int kind; } words[] = {
        { "fatal error", M_ERROR }, { "error", M_ERROR }, { "fatal", M_ERROR }, { "warning", M_WARNING },
        { "note", M_NOTE }, { "hint", M_NOTE },
    };
    int i;
    for (i = 0; i < (int)(sizeof words / sizeof words[0]); i++)
        if (word_is(s, words[i].w)) {
            *len = (int)strlen(words[i].w);
            if (s[*len] == '!')
                (*len)++;               /* Watcom: Error! */
            return words[i].kind;
        }
    return -1;
}

static int number(const char **p)
{
    int n = 0;
    if (!isdigit((unsigned char)**p))
        return -1;
    while (isdigit((unsigned char)**p))
        n = n * 10 + *(*p)++ - '0';
    return n;
}

/* After the kind word: "E1009: text", "3: text", ": text" ... the text. */
static const char *text_after(const char *s)
{
    const char *c = strchr(s, ':');
    if (!c)
        return s;
    c++;
    while (*c == ' ')
        c++;
    return c;
}

static int file_at(const char *s, const char *end, char *file, int fsize)
{
    int n = (int)(end - s);
    if (n <= 0 || n >= fsize)
        return 0;
    memcpy(file, s, n);
    file[n] = 0;
    return is_source(file);
}

/* One line of a compiler's output: is it an error, a warning ..., and where?
 * Returns 1 with the file, line and column (0: none) when it says where.
 *   GCC, NASM:            MAIN.C:12:5: error: expected ';' before '}' token
 *   Open Watcom:          MAIN.C(12): Error! E1009: Expecting ';' but found '}'
 *   Free Pascal:          MAIN.PAS(12,5) Error: Identifier not found "X"
 *   Turbo Pascal:         MAIN.PAS(12): Error 3: Unknown identifier.
 *   JWasm, MASM:          MAIN.ASM(7) : Error A2008: syntax error
 *   Turbo C, Borland C++: Error MAIN.C 12: Undefined symbol 'x' in function main */
int msg_parse(const char *line, int *kind, char *file, int fsize, int *lno, int *col, const char **text)
{
    const char *s = line, *p, *e;
    int len, k;
    *kind = M_TEXT;
    *lno = *col = 0;
    *text = line;
    file[0] = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    /* Turbo C: the kind first */
    if ((k = kind_at(s, &len)) >= 0 && s[len] == ' ') {
        p = s + len + 1;
        e = strchr(p, ' ');
        if (e && file_at(p, e, file, fsize)) {
            const char *q = e + 1;
            int n = number(&q);
            if (n > 0 && *q == ':') {
                *kind = k;
                *lno = n;
                *text = q[1] == ' ' ? q + 2 : q + 1;
                return 1;
            }
        }
    }
    /* the file first: past a drive's colon, to ( or : */
    p = s;
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
        p += 2;
    while (*p && *p != '(' && *p != ':' && *p != ' ')
        p++;
    if (*p == '(' && file_at(s, p, file, fsize)) {
        const char *q = p + 1;
        int n = number(&q), c = 0;
        if (*q == ',') {
            q++;
            c = number(&q);
        }
        if (n > 0 && *q == ')') {
            q++;
            while (*q == ' ' || *q == ':')
                q++;
            if ((k = kind_at(q, &len)) >= 0) {
                *kind = k;
                *lno = n;
                *col = c > 0 ? c : 0;
                *text = text_after(q + len);
                if (!**text)
                    *text = q;
                return 1;
            }
        }
    }
    if (*p == ':' && file_at(s, p, file, fsize)) {
        const char *q = p + 1;
        int n = number(&q), c = 0;
        if (n > 0 && *q == ':') {
            q++;
            if (isdigit((unsigned char)*q)) {
                const char *r = q;
                c = number(&r);
                if (*r == ':')
                    q = r + 1;
                else
                    c = 0;
            }
            while (*q == ' ')
                q++;
            if ((k = kind_at(q, &len)) >= 0 && q[len] == ':') {
                *kind = k;
                *lno = n;
                *col = c;
                *text = q[len + 1] == ' ' ? q + len + 2 : q + len + 1;
                return 1;
            }
        }
    }
    /* No place, but it says what happened. */
    file[0] = 0;
    if (strstr(s, "undefined reference") || strstr(s, "error:") || strstr(s, "Error:") || strstr(s, "Error!")
        || strstr(s, "fatal error") || (!strncmp(s, "make", 4) && strstr(s, "***")))
        *kind = M_ERROR;
    else if (strstr(s, "warning:") || strstr(s, "Warning:") || strstr(s, "Warning!"))
        *kind = M_WARNING;
    return 0;
}

/* ---- the messages ---------------------------------------------------------------- */

#define MAXMSGS 2000
static Msg msgs[MAXMSGS];
static int nmsgs, curmsg = -1, dropped;
static char title[100];

void msgs_clear(const char *t)
{
    int i;
    for (i = 0; i < nmsgs; i++) {
        free(msgs[i].text);
        free(msgs[i].path);
    }
    nmsgs = dropped = 0;
    curmsg = -1;
    snprintf(title, sizeof title, "%s", t ? t : "Messages");
}

void msgs_add(const char *text, const char *path, int line, int col, int kind)
{
    Msg *m;
    char *t;
    int i;
    if (nmsgs == MAXMSGS) {
        dropped++;
        return;
    }
    m = &msgs[nmsgs++];
    t = xstrdup(text);
    for (i = 0; t[i]; i++)
        if (t[i] == '\t' || (unsigned char)t[i] < 32)
            t[i] = ' ';
    m->text = t;
    m->path = path ? xstrdup(path) : NULL;
    m->line = line;
    m->col = col;
    m->kind = kind;
    m->b = NULL;
}

int msgs_count(int kind)
{
    int i, n = 0;
    for (i = 0; i < nmsgs; i++)
        if (msgs[i].kind == kind)
            n++;
    return n;
}

/* Point the messages at the files they're in that are open. */
static void attach_all(void)
{
    int i;
    for (i = 0; i < nfiles; i++)
        msgs_attach(files[i]);
}

void msgs_attach(Buffer *b)
{
    char full[DE_PATH];
    int i;
    if (!b->path[0] || !nmsgs)
        return;
    plat_fullpath(b->path, full);
    for (i = 0; i < nmsgs; i++)
        if (!msgs[i].b && msgs[i].path && ed_same_path(msgs[i].path, full))
            msgs[i].b = b;
}

void msgs_forget(Buffer *b)
{
    int i;
    for (i = 0; i < nmsgs; i++)
        if (msgs[i].b == b)
            msgs[i].b = NULL;
}

void msgs_shift(Buffer *b, int line, int delta)
{
    int i;
    if (!delta)
        return;
    for (i = 0; i < nmsgs; i++)
        if (msgs[i].b == b && msgs[i].line > line) {
            msgs[i].line += delta;
            if (msgs[i].line < line)
                msgs[i].line = line;     /* its line was deleted: the line it joined */
        }
}

static int worse(int a, int b)
{
    static const int rank[] = { 0, 4, 3, 2, 1 };   /* text, error, warning, note, found */
    return rank[b] > rank[a] ? b : a;
}

const Msg *msgs_at(const Buffer *b, int line)
{
    const Msg *best = NULL;
    int i;
    for (i = 0; i < nmsgs; i++)
        if (msgs[i].b == b && msgs[i].line == line && msgs[i].path
            && (!best || worse(best->kind, msgs[i].kind) != best->kind))
            best = &msgs[i];
    return best;
}

void msgs_marks(const Buffer *b, int top, int rows, unsigned char *kinds)
{
    int i;
    memset(kinds, M_TEXT, rows);
    for (i = 0; i < nmsgs; i++) {
        const Msg *m = &msgs[i];
        if (m->b == b && m->line >= top && m->line < top + rows && (m->kind == M_ERROR || m->kind == M_WARNING))
            kinds[m->line - top] = (unsigned char)worse(kinds[m->line - top], m->kind);
    }
}

/* Read a log into the messages. A title starts them again; NULL adds. */
void msgs_set(const char *t, const char *dir, const char *log)
{
    FILE *f = fopen(log, "r");
    char line[512], file[DE_PATH], full[DE_PATH];
    if (t)
        msgs_clear(t);
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        const char *text;
        int kind, lno, col;
        line[strcspn(line, "\r\n")] = 0;
        if (!line[strspn(line, " \t")])
            continue;
        if (msg_parse(line, &kind, file, sizeof file, &lno, &col, &text)) {
            char shown[600];
            /* said the same way, whichever compiler said it: FILE(LINE): what */
            snprintf(shown, sizeof shown, "%s(%d): %s", base_name(file), lno, text);
            path_join(dir, file, full);
            plat_fullpath(full, file);
            msgs_add(shown, file, lno - 1, col, kind);
        } else
            msgs_add(line, NULL, 0, 0, kind);
    }
    fclose(f);
    attach_all();
}

static void go_to(int i)
{
    Msg *m = &msgs[i];
    int k;
    curmsg = i;
    if (m->b)
        for (k = 0; k < nfiles; k++)
            if (files[k] == m->b) {
                ed_mark();
                cur = k;
                ed_goto(m->line + 1, m->col);
                return;
            }
    if (mtime_of(m->path) < 0) {
        ed_message(1, "%s isn't there now.", m->path);
        return;
    }
    if (ed_jump(m->path, m->line + 1, m->col) == 0) {
        msgs_attach(B_CUR);
        if (m->b)
            ed_goto(m->line + 1, m->col);   /* where it is now */
    }
}

void msgs_next(int dir)
{
    int i, k;
    if (!nmsgs) {
        ed_message(1, "No messages: F9 builds, Ctrl-Shift-F finds in files.");
        return;
    }
    for (k = 1; k <= nmsgs; k++) {
        i = ((curmsg < 0 ? (dir > 0 ? -1 : 0) : curmsg) + dir * k + nmsgs * 2) % nmsgs;
        if (msgs[i].path) {
            go_to(i);
            if (msgs[i].kind == M_FOUND)
                ed_message(0, "%d of %d: %s", i + 1, nmsgs, msgs[i].text);
            return;
        }
    }
    ed_message(1, "No message says where: F7 lists them.");
}

static int list_count(const char *f)
{
    (void)f;
    return nmsgs;
}

static void list_item(int i, char *tag, char *text, int size, int *role)
{
    const Msg *m = &msgs[i];
    snprintf(tag, 10, "%s", m->kind == M_ERROR ? "error" : m->kind == M_WARNING ? "warning" : m->kind == M_NOTE ? "note" : "");
    *role = m->kind == M_ERROR ? A_MARKERR : m->kind == M_WARNING ? A_MARKWARN : A_HELP;
    snprintf(text, size, "%s", m->text);
}

static const char *list_info(int i)
{
    return msgs[i].text;
}

void msgs_list(void)
{
    char t[200];
    int sel = curmsg, i, e = msgs_count(M_ERROR), w = msgs_count(M_WARNING), found = msgs_count(M_FOUND);
    Pick p;
    if (!nmsgs) {
        ed_message(1, "No messages: F9 builds, Ctrl-Shift-F finds in files.");
        return;
    }
    if (found)
        snprintf(t, sizeof t, "%s", title);
    else
        snprintf(t, sizeof t, "%s: %d error%s, %d warning%s%s", title, e, e == 1 ? "" : "s", w, w == 1 ? "" : "s",
                 dropped ? " (the rest left out)" : "");
    if (sel < 0)
        for (i = 0; i < nmsgs && sel < 0; i++)
            if (msgs[i].path && msgs[i].kind != M_NOTE)
                sel = i;
    p.title = t;
    p.keys = " Enter Go to it   Esc Close   Later: F8 the next, Shift-F8 the one before ";
    p.count = list_count;
    p.item = list_item;
    p.info = list_info;
    p.filter = 0;
    i = pick_run(&p, sel < 0 ? 0 : sel);
    if (i >= 0) {
        if (msgs[i].path)
            go_to(i);
        else
            ed_message(1, "That line doesn't say where.");
    }
}

/* ---- Find in files ------------------------------------------------------------------ */

static const char *needle;
static int needle_n, needle_cs, nfound, scanned;

static int find_in(const char *s, int n)
{
    int i, k;
    for (i = 0; i + needle_n <= n; i++) {
        for (k = 0; k < needle_n; k++) {
            int a = s[i + k], b = needle[k];
            if (!needle_cs) {
                a = tolower((unsigned char)a);
                b = tolower((unsigned char)b);
            }
            if (a != b)
                break;
        }
        if (k == needle_n)
            return i;
    }
    return -1;
}

static void found(const char *full, const char *shown, int line, const char *s, int n, int at)
{
    char t[200];
    while (n > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        n--;
    }
    snprintf(t, sizeof t, "%s(%d): %.*s", shown, line + 1, n > 150 ? 150 : n, s);
    msgs_add(t, full, line, at + 1, M_FOUND);
    nfound++;
}

static void search_file(const char *path, const char *shown)
{
    char full[DE_PATH];
    int i, l;
    FILE *f;
    long size;
    char *data;
    plat_fullpath(path, full);
    scanned++;
    for (i = 0; i < nfiles; i++) {
        char t[DE_PATH];
        if (!files[i]->path[0])
            continue;
        plat_fullpath(files[i]->path, t);
        if (ed_same_path(t, full)) {             /* open: what's in DE, saved or not */
            Buffer *b = files[i];
            for (l = 0; l < b->n && nfound < MAXMSGS; l++) {
                int at = find_in(b->ln[l].s, b->ln[l].len);
                if (at >= 0)
                    found(full, shown, l, b->ln[l].s, b->ln[l].len, at);
            }
            return;
        }
    }
    if (!(f = fopen(path, "rb")))
        return;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || size > 1024L * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return;
    }
    data = xrealloc(NULL, size + 1);
    if (fread(data, 1, size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return;
    }
    fclose(f);
    data[size] = 0;
    if (memchr(data, 0, size < 1024 ? size : 1024)) {
        free(data);                              /* a program, not text */
        return;
    }
    {
        char *s = data, *end = data + size;
        for (l = 0; s < end && nfound < MAXMSGS; l++) {
            char *nl = memchr(s, '\n', end - s);
            int n = (int)((nl ? nl : end) - s), at;
            if (n > 0 && s[n - 1] == '\r')
                n--;
            if ((at = find_in(s, n)) >= 0)
                found(full, shown, l, s, n, at);
            s = nl ? nl + 1 : end;
        }
    }
    free(data);
}

static void walk(const char *dir, const char *shown, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[DE_PATH], s[DE_PATH];
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && nfound < MAXMSGS && scanned < 4000) {
        if (e->d_name[0] == '.')
            continue;
        path_join(dir, e->d_name, p);
        if (shown[0])
            snprintf(s, sizeof s, "%s/%s", shown, e->d_name);
        else
            snprintf(s, sizeof s, "%s", e->d_name);
        if (is_dir(p)) {
            if (depth < 4 && !(depth == 0 && eq_nocase(e->d_name, "LIBS")))
                walk(p, s, depth + 1);      /* not the libraries a project keeps in LIBS */
        } else if (!ext_in(e->d_name, " EXE COM O OBJ LIB A ZIP 7Z GZ TGZ IMG IMA BIN DLL OVL PNG BMP GIF PCX JPG WAV MID $$$ "))
            search_file(p, s);
    }
    closedir(d);
}

static int has_capital(const char *s)
{
    for (; *s; s++)
        if (isupper((unsigned char)*s))
            return 1;
    return 0;
}

void find_in_files(void)
{
    static char what[80];
    Project p;
    char t[160], w[40];
    Buffer *b = B_CUR;
    if (b->sel == 0 && ed_word(b, w, sizeof w))
        snprintf(what, sizeof what, "%s", w);
    if (!input_box("Find in files", "Text to find (capitals: exactly)", what, sizeof what) || !what[0])
        return;
    proj_find(b, &p);
    needle = what;
    needle_n = (int)strlen(what);
    needle_cs = has_capital(what);
    nfound = scanned = 0;
    busy_box("Find in files", what, p.dir);
    snprintf(t, sizeof t, "Find in files: \"%.40s\"", what);
    msgs_clear(t);
    walk(p.dir, "", 0);
    attach_all();
    snprintf(title, sizeof title, "\"%.40s\": %d found in %d file%s%s", what, nfound, scanned, scanned == 1 ? "" : "s",
             nfound >= MAXMSGS ? " (stopped there)" : "");
    if (!nfound) {
        ed_message(1, "\"%s\" isn't in any file in %s.", what, base_name(p.dir));
        return;
    }
    msgs_list();
}
