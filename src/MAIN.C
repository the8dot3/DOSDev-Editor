/* DE - starting and stopping, the command line, and the options file. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DE.H"

char cfg_path[DE_PATH];

#ifndef DE_NO_MAIN
/* DE.CFG lives next to DE.EXE. */
static void find_config(const char *argv0)
{
    const char *name = base_name(argv0);
    size_t n = (size_t)(name - argv0);
    if (n > DE_PATH - 8)
        n = 0;
    memcpy(cfg_path, argv0, n);
    strcpy(cfg_path + n, "DE.CFG");
}

#endif

int config_load(void)
{
    FILE *f = fopen(cfg_path, "r");
    char line[128];
    int old_theme = -1, named = -1, i;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        int v;
        if (!eq)
            continue;
        *eq = 0;
        eq[strcspn(eq + 1, "\r\n") + 1] = 0;
        v = atoi(eq + 1);
        if (!strcmp(line, "numbers")) opt_numbers = v != 0;
        else if (!strcmp(line, "spaces")) opt_spaces = v != 0;
        else if (!strcmp(line, "indent") && (v == 2 || v == 4 || v == 8)) opt_indent = v;
        else if (!strcmp(line, "tab") && (v == 4 || v == 8)) opt_tab = v;
        else if (!strcmp(line, "autoindent")) opt_auto = v != 0;
        else if (!strcmp(line, "colours")) {
            for (i = 0; i < NTHEMES; i++)
                if (!strcmp(eq + 1, theme_id[i]))
                    named = i;
        }
        else if (!strcmp(line, "theme")) old_theme = v;   /* DE 0.4 and before */
        else if (!strcmp(line, "suggest")) opt_suggest = v != 0;
        else if (!strcmp(line, "fastkeys")) opt_fastkeys = v != 0;
    }
    fclose(f);
    /* DE 0.4 numbered its themes: Borland blue (which now follows DOSDev,
     * as its Borland blue does), Amber, Green phosphor, Paper white. */
    if (named >= 0)
        theme_set(named);
    else if (old_theme >= 0)
        theme_set(old_theme == 3 ? 3 : old_theme == 0 ? 0 : 1);
    return 0;
}

int config_save(void)
{
    FILE *f = fopen(cfg_path, "w");
    if (!f)
        return -1;
    fprintf(f, "numbers=%d\nspaces=%d\nindent=%d\ntab=%d\nautoindent=%d\ncolours=%s\nsuggest=%d\nfastkeys=%d\n",
            opt_numbers, opt_spaces, opt_indent, opt_tab, opt_auto, theme_id[theme], opt_suggest, opt_fastkeys);
    return fclose(f) ? -1 : 0;
}

#ifndef DE_NO_MAIN
static void usage(void)
{
    puts("DE " DE_VERSION ", the DOSDev Editor: a colour text editor for DOS programmers.\n"
         "\n"
         "  DE [FILE ...]        Open files; a name that doesn't exist yet starts a new file\n"
         "  DE +LINE FILE        Open FILE at a line (FILE +LINE works too)\n"
         "  DE /SPEED FILE       Time DE on FILE: how long each kind of key takes here\n"
         "\n"
         "Inside DE, F1 lists every key and F10 opens the menus.");
}

/* /NAME or -NAME, in any case. */
static int is_opt(const char *a, const char *name)
{
    if (a[0] != '/' && a[0] != '-')
        return 0;
    for (a++; *a && *name; a++, name++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*name))
            return 0;
    return !*a && !*name;
}

/* DE /SPEED FILE: the timings, printed once the screen is back. */
static int speed(const char *file)
{
    const char *what[8];
    double ms[8];
    int i, n, lines;
    if (ed_open(file, 0) || files[cur]->isnew) {
        theme_done();
        plat_done();
        fprintf(stderr, "DE: can't read %s\n", file);
        return 1;
    }
    lines = files[cur]->n;
    n = ed_speed(what, ms, 8);
    theme_done();
    plat_done();
    printf("DE %s speed test, on %s (%d lines)\n\n", DE_VERSION, base_name(file), lines);
    for (i = 0; i < n; i++)
        printf("  %-40s %7.2f ms\n", what[i], ms[i]);
    puts("\nEach is an average over about a second (the BIOS clock moves in 55 ms steps).\n"
         "A key held down repeats every 33 ms: anything under that keeps up with it.");
    return 0;
}

int main(int argc, char **argv)
{
    int i, line = 0, opened = 0;
    for (i = 1; i < argc; i++)
        if (is_opt(argv[i], "?") || is_opt(argv[i], "h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        }
    if (argc > 1 && is_opt(argv[1], "speed") && argc != 3) {
        usage();
        return 1;
    }
    if (plat_init()) {
        fputs("DE needs a text screen of at least 80 columns and 25 rows.\n", stderr);
        return 1;
    }
    scr_init();
    theme_set(0);
    find_config(argc > 0 ? argv[0] : "DE.EXE");
    config_load();
    plat_repeat(opt_fastkeys);
    ed_init();
    if (argc == 3 && is_opt(argv[1], "speed"))
        return speed(argv[2]);
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '+') {
            line = atoi(argv[i] + 1);
            if (opened && i == argc - 1)
                ed_goto_line(line);          /* FILE +LINE */
            continue;
        }
        if (ed_open(argv[i], line) == 0)
            opened = 1;
        line = 0;
    }
    if (nfiles == 0)
        ed_new();
    while (running)
        ed_step();
    theme_done();
    plat_done();
    return 0;
}
#endif
