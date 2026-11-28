/* DE tests: scripted keys against the real editor code, on Linux.
 *   detest [WORKDIR]          the scenarios and the random-edit test
 *   detest --shot FILE OUT    open FILE, write the screen to OUT (for pictures)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#endif
#include "DE.H"

extern int t_keys[], t_nkeys, t_kpos, t_shift, t_rows, t_cols, t_crow, t_ccol, t_cursor, t_extra;
extern unsigned short t_screen[], t_snap[];
extern int t_snapped, t_batch, t_shows, t_repeat, t_execs, t_user_screens;
extern char t_last_exec[];
extern int t_pal_calls, t_pal_set, t_pal_restores;
extern unsigned char t_pal[48];
extern int de_fail_alloc;

static int passes, fails;
static char dir[512], srcdir[512];   /* srcdir: test/, for the files the tests read */

#define CHECK(what, cond) do { if (cond) passes++; else { fails++; printf("  FAIL  %s (line %d)\n", what, __LINE__); } } while (0)

static void key(int k) { t_keys[t_nkeys++] = k; }
static void keys(int n, ...);
static void type(const char *s)
{
    for (; *s; s++)
        key(*s == '\n' ? DK_ENTER : (unsigned char)*s);
}
static void mouse_ev(int ev, int row, int col)
{
    key(DK_MOUSE);
    key(ev);
    key(row);
    key(col);
}
static void run(void)
{
    t_snapped = 0;
    while (t_kpos < t_nkeys && running)
        ed_step();
    ed_draw();
    t_nkeys = t_kpos = t_extra = 0;
}

/* A row of what was showing when the keys ran out inside a menu or dialog. */
static char *snap_row(int r)
{
    static char s[200];
    int i;
    for (i = 0; i < t_cols; i++)
        s[i] = (char)(t_snap[r * t_cols + i] & 0xFF);
    s[t_cols] = 0;
    return s;
}

static int snap_has(const char *s)
{
    int r;
    for (r = 0; r < t_rows; r++)
        if (strstr(snap_row(r), s))
            return 1;
    return 0;
}

static char *text_of(Buffer *b)
{
    return buf_text(b, 0, 0, b->n - 1, b->ln[b->n - 1].len, NULL);
}

static int text_is(const char *want)
{
    char *t = text_of(files[cur]);
    int ok = !strcmp(t, want);
    if (!ok)
        printf("        text is: [%s]\n        wanted:  [%s]\n", t, want);
    free(t);
    return ok;
}

static char *row_text(int r)
{
    static char s[200];
    int i;
    for (i = 0; i < t_cols; i++)
        s[i] = (char)(t_screen[r * t_cols + i] & 0xFF);
    s[t_cols] = 0;
    return s;
}

static int attr_at(int r, int c) { return t_screen[r * t_cols + c] >> 8; }

/* Where a string is on a row of the screen, or -1. */
static int col_of(int r, const char *s)
{
    char *p = strstr(row_text(r), s);
    return p ? (int)(p - row_text(r)) : -1;
}

static char *path(const char *name)
{
    static char p[4][600];
    static int i;
    i = (i + 1) % 4;
    snprintf(p[i], sizeof p[i], "%s/%s", dir, name);
    return p[i];
}

static void write_file(const char *name, const char *content, int n)
{
    FILE *f = fopen(path(name), "wb");
    if (n < 0)
        n = (int)strlen(content);
    fwrite(content, 1, n, f);
    fclose(f);
}

static int file_is(const char *name, const char *want, int n)
{
    FILE *f = fopen(path(name), "rb");
    char buf[8192];
    int got;
    if (!f)
        return 0;
    if (n < 0)
        n = (int)strlen(want);
    got = (int)fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (got != n || memcmp(buf, want, n)) {
        printf("        file has %d bytes: [%.*s]\n", got, got, buf);
        return 0;
    }
    return 1;
}

static void fresh(void)
{
    int i;
    msgs_clear(NULL);
    for (i = 0; i < nfiles; i++)
        buf_free(files[i]);
    nfiles = 0;
    ed_init();
    theme_set(0);
    opt_numbers = 1;
    opt_spaces = 1;
    opt_indent = 4;
    opt_tab = 8;
    opt_auto = 1;
    opt_suggest = 1;
    overwrite = 0;
    t_shift = 0;
    ed_new();
    run();
}

static void keys(int n, ...)
{
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        key(va_arg(ap, int));
    va_end(ap);
}

static void test_typing_and_files(void)
{
    puts("Typing, saving and loading");
    fresh();
    ed_open(path("NEW.C"), 0);
    type("int main(void)\n{\nreturn 0;\n}");
    run();
    CHECK("Enter keeps indentation and indents after {, } steps back", text_is("int main(void)\n{\n    return 0;\n}"));
    CHECK("the title shows it changed", strstr(row_text(1), "*NEW.C") != NULL);
    key(DK_F2);
    run();
    CHECK("F2 saves with DOS line ends", file_is("NEW.C", "int main(void)\r\n{\r\n    return 0;\r\n}\r\n", -1));
    CHECK("...and says so", strstr(row_text(24), "Saved NEW.C") != NULL);
    CHECK("...and the title doesn't say changed", strstr(row_text(1), "*NEW.C") == NULL);

    write_file("UNIX.TXT", "one\ntwo\n", 8);
    ed_open(path("UNIX.TXT"), 2);
    CHECK("opening at a line", files[cur]->cy == 1);
    keys(1, DK_END);
    type("!");
    run();
    CHECK("the status line says LF", strstr(row_text(24), "LF") != NULL);
    keys(1, KM_CTRL | 'S');
    run();
    CHECK("a file with Linux line ends keeps them", file_is("UNIX.TXT", "one\ntwo!\n", -1));

    write_file("NOEOL.TXT", "a\r\nb", 4);
    ed_open(path("NOEOL.TXT"), 0);
    keys(1, KM_CTRL | 'S');
    run();
    CHECK("no line break at the end stays so", file_is("NOEOL.TXT", "a\r\nb", 4));
    write_file("CTRLZ.TXT", "x\r\n\x1a", 4);
    ed_open(path("CTRLZ.TXT"), 0);
    CHECK("a DOS end-of-file mark isn't shown", files[cur]->n == 1 && files[cur]->ln[0].len == 1);
    keys(1, KM_CTRL | 'S');
    run();
    CHECK("...and is kept", file_is("CTRLZ.TXT", "x\r\n\x1a", 4));
    CHECK("each file has a window of its own", nfiles == 4);
}

static void test_undo(void)
{
    puts("Undo and redo");
    fresh();
    type("hello world");
    run();
    keys(1, KM_CTRL | 'Z');
    run();
    CHECK("undo takes back the last word", text_is("hello"));
    keys(1, KM_CTRL | 'Z');
    run();
    CHECK("...then the one before", text_is(""));
    keys(2, KM_CTRL | 'Y', KM_CTRL | 'Y');
    run();
    CHECK("redo puts them back", text_is("hello world"));
    keys(3, DK_BACKSPACE, DK_BACKSPACE, DK_BACKSPACE);
    run();
    keys(1, KM_CTRL | 'Z');
    run();
    CHECK("backspaces in a row undo together", text_is("hello world"));
    keys(1, KM_CTRL | KM_SHIFT | 'Z');
    run();
    CHECK("Ctrl-Shift-Z redoes too", text_is("hello wo"));
}

static void test_selection_and_clipboard(void)
{
    puts("Selecting, the clipboard and lines");
    fresh();
    type("alpha\nbeta\ngamma");
    keys(2, KM_CTRL | DK_HOME, KM_SHIFT | DK_RIGHT);
    keys(2, KM_SHIFT | DK_RIGHT, KM_SHIFT | DK_RIGHT);
    run();
    CHECK("Shift-Right selects", files[cur]->sel && files[cur]->cx == 3);
    CHECK("...shown in the selection colour", attr_at(2, 5) == attr[A_SELECT]);
    keys(1, DK_DEL);
    run();
    CHECK("Del removes the selection", text_is("ha\nbeta\ngamma"));
    type("XY");
    run();
    CHECK("typing goes where it was", text_is("XYha\nbeta\ngamma"));
    keys(2, DK_DOWN, KM_CTRL | 'C');
    keys(2, DK_DOWN, KM_CTRL | 'V');
    run();
    CHECK("Ctrl-C with nothing selected copies the line; Ctrl-V puts it above", text_is("XYha\nbeta\nbeta\ngamma"));
    keys(2, KM_CTRL | DK_HOME, KM_CTRL | 'K');
    keys(1, KM_CTRL | 'K');
    run();
    CHECK("Ctrl-K twice cuts two lines", text_is("beta\ngamma"));
    keys(2, KM_CTRL | DK_END, KM_CTRL | 'V');
    run();
    CHECK("...and pastes them both", text_is("beta\nXYha\nbeta\ngamma"));
    keys(2, KM_CTRL | DK_HOME, KM_CTRL | 'D');
    run();
    CHECK("Ctrl-D duplicates the line", text_is("beta\nbeta\nXYha\nbeta\ngamma"));
    keys(2, KM_CTRL | DK_END, KM_ALT | DK_UP);
    run();
    CHECK("Alt-Up moves the line up", text_is("beta\nbeta\nXYha\ngamma\nbeta") && files[cur]->cy == 3);
    keys(1, KM_ALT | DK_DOWN);
    run();
    CHECK("Alt-Down moves it back", text_is("beta\nbeta\nXYha\nbeta\ngamma"));
    keys(1, KM_CTRL | 'A');
    keys(1, KM_CTRL | 'X');
    run();
    CHECK("Ctrl-A, Ctrl-X cuts everything", text_is(""));
    keys(1, KM_CTRL | 'Z');
    run();
    CHECK("...and Ctrl-Z brings it back", text_is("beta\nbeta\nXYha\nbeta\ngamma"));
}

static void test_indent_and_comments(void)
{
    puts("Indenting and comments");
    fresh();
    ed_open(path("IND.C"), 0);
    type("a();\nb();\nc();");
    keys(3, KM_CTRL | DK_HOME, KM_SHIFT | DK_DOWN, KM_SHIFT | DK_DOWN);
    keys(1, DK_TAB);
    run();
    CHECK("Tab indents the selected lines", text_is("    a();\n    b();\nc();"));
    keys(1, KM_SHIFT | DK_TAB);
    run();
    CHECK("Shift-Tab takes it back", text_is("a();\nb();\nc();"));
    keys(1, KM_ALT | ';');
    run();
    CHECK("Alt-; comments the lines", text_is("// a();\n// b();\nc();"));
    keys(1, KM_ALT | ';');
    run();
    CHECK("...and uncomments them", text_is("a();\nb();\nc();"));
    keys(3, DK_ESC, KM_CTRL | DK_END, DK_END);
    keys(2, DK_HOME, DK_TAB);
    run();
    CHECK("Tab inserts spaces to the next stop", text_is("a();\nb();\n    c();"));
    keys(1, DK_BACKSPACE);
    run();
    CHECK("Backspace in the indentation goes back one stop", text_is("a();\nb();\nc();"));
    fresh();
    ed_open(path("X.ASM"), 0);
    type("mov ax, 1");
    keys(1, KM_ALT | ';');
    run();
    CHECK("assembly comments with ;", text_is("; mov ax, 1"));
}

static void test_find_and_replace(void)
{
    puts("Finding and replacing");
    fresh();
    type("Foo foo\nbar FOO\nfoo");
    keys(2, KM_CTRL | DK_HOME, KM_CTRL | 'F');
    type("foo\n");
    run();
    CHECK("Ctrl-F finds lower case in any case", files[cur]->sel && files[cur]->cy == 0 && files[cur]->sx == 0);
    keys(1, DK_F3);
    run();
    CHECK("F3 finds the next", files[cur]->cy == 0 && files[cur]->sx == 4);
    keys(2, DK_F3, DK_F3);
    run();
    CHECK("...and the next two", files[cur]->cy == 2 && files[cur]->sx == 0);
    keys(1, DK_F3);
    run();
    CHECK("...then goes round to the top, and says so", files[cur]->cy == 0 && strstr(row_text(24), "top") != NULL);
    keys(1, KM_SHIFT | DK_F3);
    run();
    CHECK("Shift-F3 goes back", files[cur]->cy == 2);
    keys(2, KM_CTRL | DK_HOME, KM_CTRL | 'F');
    type("FOO\n");
    run();
    CHECK("a capital makes case count", files[cur]->cy == 1 && files[cur]->sx == 4);
    keys(3, DK_ESC, KM_CTRL | DK_HOME, KM_CTRL | 'R');
    type("foo\nbaz\nA");
    run();
    CHECK("Replace All", text_is("baz baz\nbar baz\nbaz"));
    CHECK("...says how many", strstr(row_text(24), "4 replaced") != NULL);
    keys(1, KM_CTRL | 'Z');
    run();
    CHECK("one Ctrl-Z takes them all back", text_is("Foo foo\nbar FOO\nfoo"));
    keys(2, KM_CTRL | DK_HOME, KM_CTRL | 'R');
    type("foo\nqq\nnys");
    run();
    CHECK("Replace one at a time: No, Yes, Stop", text_is("Foo qq\nbar FOO\nfoo"));
    keys(2, KM_CTRL | 'F', DK_BACKSPACE);
    type("zzz\n");
    run();
    CHECK("not found says so", strstr(row_text(24), "Not found") != NULL);
    keys(1, KM_CTRL | 'G');
    type("3\n");
    run();
    CHECK("Ctrl-G goes to a line", files[cur]->cy == 2);
}

static void test_brackets_and_colours(void)
{
    int r = 2;
    puts("Brackets and colours");
    fresh();
    write_file("COL.C", "#include <stdio.h>\n/* a\n   comment */\nint x = 42; // note\nchar *s = \"hi\";\nif (x) { return; }\n", -1);
    ed_open(path("COL.C"), 0);
    run();
    CHECK("#include is a directive", attr_at(r, col_of(r, "#include")) == attr[A_PREPROC]);
    CHECK("<stdio.h> is a string", attr_at(r, col_of(r, "<stdio.h>")) == attr[A_STRING]);
    CHECK("a comment over two lines", attr_at(r + 2, col_of(r + 2, "comment")) == attr[A_COMMENT]);
    CHECK("int is a type", attr_at(r + 3, col_of(r + 3, "int")) == attr[A_TYPE]);
    CHECK("42 is a number", attr_at(r + 3, col_of(r + 3, "42")) == attr[A_NUMBER]);
    CHECK("// starts a comment", attr_at(r + 3, col_of(r + 3, "note")) == attr[A_COMMENT]);
    CHECK("\"hi\" is a string", attr_at(r + 4, col_of(r + 4, "\"hi\"")) == attr[A_STRING]);
    CHECK("return is a keyword", attr_at(r + 5, col_of(r + 5, "return")) == attr[A_KEYWORD]);
    CHECK("line numbers", strncmp(row_text(r) + 1, "  1 ", 4) == 0);
    keys(2, KM_CTRL | DK_END, DK_HOME);
    keys(3, DK_RIGHT, DK_RIGHT, DK_RIGHT);
    run();
    CHECK("the bracket at the cursor and its pair light up", attr_at(r + 5, col_of(r + 5, "(x)")) == attr[A_MATCH]
          && attr_at(r + 5, col_of(r + 5, "(x)") + 2) == attr[A_MATCH]);
    keys(4, DK_RIGHT, DK_RIGHT, DK_RIGHT, DK_RIGHT);
    keys(1, KM_CTRL | 'B');
    run();
    CHECK("Ctrl-B jumps to the matching bracket", files[cur]->ln[files[cur]->cy].s[files[cur]->cx] == '}');
    write_file("T.ASM", "start:  mov ax, 0x10 ; go\n        int 21h\n", -1);
    ed_open(path("T.ASM"), 0);
    run();
    CHECK("asm: a label", attr_at(r, col_of(r, "start:")) == attr[A_PREPROC]);
    CHECK("asm: an instruction", attr_at(r, col_of(r, "mov")) == attr[A_KEYWORD]);
    CHECK("asm: a register", attr_at(r, col_of(r, "ax")) == attr[A_TYPE]);
    CHECK("asm: a comment", attr_at(r, col_of(r, "; go")) == attr[A_COMMENT]);
    CHECK("asm: 21h is a number", attr_at(r + 1, col_of(r + 1, "21h")) == attr[A_NUMBER]);
    write_file("T.PAS", "program Hi; { a\ncomment }\nbegin WriteLn('x') end.\n", -1);
    ed_open(path("T.PAS"), 0);
    run();
    CHECK("pascal: keywords in any case", attr_at(r, col_of(r, "program")) == attr[A_KEYWORD]);
    CHECK("pascal: a { comment } over two lines", attr_at(r + 1, col_of(r + 1, "comment")) == attr[A_COMMENT]);
    CHECK("pascal: a string", attr_at(r + 2, col_of(r + 2, "'x'")) == attr[A_STRING]);
    write_file("MAKEFILE", "# build\nall: x.o\n\t$(CC) -o x\n", -1);
    ed_open(path("MAKEFILE"), 0);
    run();
    CHECK("make: a target", attr_at(r + 1, col_of(r + 1, "all")) == attr[A_KEYWORD]);
    CHECK("make: a variable", attr_at(r + 2, col_of(r + 2, "$(CC)")) == attr[A_TYPE]);
}

static void test_menus_dialogs_files(void)
{
    puts("Menus, dialogs and several files");
    fresh();
    keys(1, KM_ALT | 'O');
    keys(1, DK_ENTER);
    run();
    CHECK("Alt-O, Enter: the first option (line numbers) turns off", opt_numbers == 0);
    keys(2, DK_F10, DK_RIGHT);
    keys(4, DK_RIGHT, DK_RIGHT, DK_RIGHT, DK_RIGHT);
    keys(6, DK_DOWN, DK_DOWN, DK_DOWN, DK_DOWN, DK_DOWN, DK_DOWN);
    keys(1, DK_ENTER);
    run();
    CHECK("F10, arrows, Enter: the colours change", theme == 1);
    keys(2, KM_ALT | 'O', 'c');
    keys(2, KM_ALT | 'O', 'c');
    keys(2, KM_ALT | 'O', 'c');
    run();
    CHECK("a menu's letter picks its item, through the five themes", theme == 4);
    keys(2, KM_ALT | 'O', 'c');
    run();
    CHECK("...and round to Follow DOSDev", theme == 0);
    keys(1, KM_ALT | 'O');
    key('n');
    run();
    write_file("ONE.TXT", "one\n", -1);
    write_file("TWO.TXT", "two\n", -1);
    {
        char cwd[600];
        if (getcwd(cwd, sizeof cwd) && chdir(dir) == 0) {
            keys(1, KM_CTRL | 'O');
            type("ONE.TXT\n");
            keys(1, KM_CTRL | 'O');
            type("TWO.TXT\n");
            run();
            CHECK("Ctrl-O opens a file by name", nfiles == 2 && strstr(files[0]->path, "ONE.TXT") && strstr(files[1]->path, "TWO.TXT"));
            CHECK("...and an untouched new file makes way", nfiles == 2);
            keys(1, DK_F6);
            run();
            CHECK("F6 goes to the next file", cur == 0);
            keys(1, KM_ALT | '2');
            run();
            CHECK("Alt-2 to the second", cur == 1);
            keys(2, KM_ALT | 'W', DK_END);
            run();
            CHECK("the Window menu lists them", t_snapped && strstr(snap_row(6), "1 ONE.TXT") && strstr(snap_row(7), "2 TWO.TXT"));
            keys(1, KM_CTRL | 'O');
            keys(3, DK_TAB, DK_HOME, DK_DOWN);
            run();
            type("x");
            keys(1, KM_CTRL | 'W');
            key('n');
            run();
            CHECK("closing a changed file asks; No drops the change", nfiles == 1);
            if (chdir(cwd)) {}
        }
    }
    fresh();
    type("hi");
    keys(1, DK_F2);
    type(path("SAVED.TXT"));
    key(DK_ENTER);
    run();
    CHECK("F2 on a new file asks for its name", file_is("SAVED.TXT", "hi\r\n", -1));
    type("!");
    keys(1, KM_ALT | 'X');
    key('y');
    run();
    CHECK("Alt-X with changes asks; Yes saves, then exits", !running && file_is("SAVED.TXT", "hi!\r\n", -1));
    running = 1;
    fresh();
    keys(1, DK_F1);
    run();
    CHECK("F1 opens the manual's contents", t_snapped && strstr(snap_row(1), "DE manual") != NULL);
    keys(2, DK_F1, DK_ENTER);
    run();
    CHECK("Enter opens the first topic", t_snapped && strstr(snap_row(1), "Getting started  (1 of") != NULL);
    keys(3, DK_F1, DK_ENTER, DK_RIGHT);
    run();
    CHECK("Right goes to the next topic", t_snapped && strstr(snap_row(1), "The screen  (2 of") != NULL);
}

static void test_mouse_and_view(void)
{
    char line[300];
    int i;
    puts("The mouse, tabs and long lines");
    fresh();
    type("first line\nsecond line\nthird");
    run();
    mouse_ev(M_PRESS, 3, 1 + 4 + 2);
    mouse_ev(M_RELEASE, 3, 1 + 4 + 2);
    run();
    CHECK("a click puts the cursor there", files[cur]->cy == 1 && files[cur]->cx == 2);
    mouse_ev(M_PRESS, 2, 5);
    mouse_ev(M_DRAG, 3, 9);
    mouse_ev(M_RELEASE, 3, 9);
    run();
    CHECK("dragging selects", files[cur]->sel && files[cur]->sy == 0 && files[cur]->sx == 0 && files[cur]->cy == 1 && files[cur]->cx == 4);
    fresh();
    type("\tx");
    run();
    CHECK("Tab inserts spaces by default", text_is("    x"));
    keys(1, KM_ALT | 'O');
    key('s');
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("\tx");
    run();
    CHECK("...or a tab, when chosen", text_is("\tx"));
    CHECK("a tab shows as spaces to the next 8", (t_screen[2 * 80 + 5 + 8] & 0xFF) == 'x');
    opt_spaces = 1;
    fresh();
    for (i = 0; i < 200; i++)
        line[i] = (char)('a' + i % 26);
    line[200] = 0;
    type(line);
    run();
    CHECK("a long line scrolls sideways to the cursor", files[cur]->left > 100 && t_ccol < 79);
    keys(1, DK_HOME);
    run();
    CHECK("...and back", files[cur]->left == 0);
    keys(1, DK_INS);
    type("XY");
    run();
    CHECK("Ins: overwrite", files[cur]->ln[0].len == 200 && files[cur]->ln[0].s[0] == 'X' && t_cursor == 2);
    keys(1, DK_INS);
    run();
}

static void test_suggestions(void)
{
    puts("Suggestions and the reference");
    fresh();
    ed_open(path("SUG.C"), 0);
    type("int counter_value = 0;\ncou");
    run();
    CHECK("a word from the file is suggested, dimmed", strstr(row_text(3), "counter_value") && attr_at(3, col_of(3, "nter_value")) == attr[A_GHOST]);
    CHECK("the status line says what Tab types", strstr(row_text(24), "Tab counter_value") != NULL);
    keys(1, DK_TAB);
    run();
    CHECK("Tab takes it", text_is("int counter_value = 0;\ncounter_value"));
    keys(1, KM_CTRL | 'Z');
    run();
    CHECK("one Ctrl-Z takes it back", text_is("int counter_value = 0;\ncou"));
    keys(2, DK_END, DK_ENTER);
    type("pri");
    run();
    CHECK("a library function is suggested", strstr(row_text(4), "printf") != NULL);
    CHECK("...with how it's written", strstr(row_text(24), "int printf(const char *format") != NULL);
    keys(1, DK_TAB);
    type("(\"%d\", ");
    run();
    CHECK("inside its brackets, the status line shows it", strstr(row_text(24), "int printf(const char *format, ...);") != NULL);
    keys(2, DK_END, DK_ENTER);
    type("// cou");
    run();
    CHECK("no suggestions in a comment", sug_ghost(files[cur], NULL) == NULL);
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("str");
    keys(1, KM_CTRL | ' ');
    run();
    CHECK("Ctrl-Space lists the candidates", t_snapped && strstr(snap_row(4), "strcat") && strstr(snap_row(5), "strchr"));
    keys(3, KM_CTRL | ' ', DK_DOWN, DK_ENTER);
    run();
    CHECK("...Down, Enter takes the second", text_is("strchr"));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("strl");
    keys(2, KM_CTRL | ' ', 'e');
    keys(1, DK_ENTER);
    run();
    CHECK("...typing narrows the list", text_is("strlen"));
    keys(1, KM_CTRL | DK_F1);
    run();
    CHECK("Ctrl-F1 shows the word's entry", t_snapped && strstr(snap_row(1), " strlen ") && col_of(5, "size_t strlen") < 0 && strstr(snap_row(5), "size_t strlen(const char *s);"));
    keys(1, KM_ALT | 'O');
    key('g');
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("int counter;\ncou");
    run();
    CHECK("Options: suggestions as you type, off", !opt_suggest && sug_ghost(files[cur], NULL) == NULL);
    opt_suggest = 1;
    fresh();
    ed_open(path("SUG.PAS"), 0);
    type("WRITEL");
    run();
    CHECK("Pascal: capitals give capitals", strstr(row_text(2), "WRITELN") != NULL);
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("writel");
    keys(1, DK_TAB);
    run();
    CHECK("...and lower case gives the usual spelling", text_is("WriteLn"));
    fresh();
    ed_open(path("SUG.ASM"), 0);
    type("mo");
    keys(1, DK_TAB);
    run();
    CHECK("assembly: mo gives mov", text_is("mov"));
    keys(1, KM_SHIFT | DK_F1);
    type("21h");
    run();
    CHECK("Shift-F1: the reference, searched", t_snapped && strstr(snap_row(5), "INT 21h/") != NULL);
    keys(2, DK_END, ' ');
    type("21h");
    keys(1, KM_CTRL | DK_F1);
    run();
    CHECK("Ctrl-F1 on a word with no entry searches for it", t_snapped && strstr(snap_row(3), "21h") && strstr(snap_row(5), "INT 21h/"));
}


/* ---- the IDE: building, messages, finding, symbols, definitions ------------------ */

static int parsed(const char *line, int kind, const char *file, int lno, int col, const char *text)
{
    char f[260];
    const char *t;
    int k, l, c, at = msg_parse(line, &k, f, sizeof f, &l, &c, &t);
    if (!file)
        return !at && k == kind;
    if (at && k == kind && !strcmp(f, file) && l == lno && c == col && !strcmp(t, text))
        return 1;
    printf("        [%s] gave %d kind %d [%s] %d:%d [%s]\n", line, at, k, at ? f : "", l, c, t);
    return 0;
}

static void test_parse(void)
{
    puts("What compilers say");
    CHECK("GCC: file, line, column, error", parsed("MAIN.C:12:5: error: expected ';' before '}' token", M_ERROR, "MAIN.C", 12, 5, "expected ';' before '}' token"));
    CHECK("GCC: a warning with no column", parsed("src/UTIL.C:7: warning: unused variable 'x'", M_WARNING, "src/UTIL.C", 7, 0, "unused variable 'x'"));
    CHECK("GCC: a fatal error", parsed("MAIN.C:1:10: fatal error: NOPE.H: No such file or directory", M_ERROR, "MAIN.C", 1, 10, "NOPE.H: No such file or directory"));
    CHECK("GCC: a note, with a drive", parsed("c:/djgpp/include/stdio.h:40:5: note: expected 'int'", M_NOTE, "c:/djgpp/include/stdio.h", 40, 5, "expected 'int'"));
    CHECK("NASM", parsed("MAIN.ASM:7: error: parser: instruction expected", M_ERROR, "MAIN.ASM", 7, 0, "parser: instruction expected"));
    CHECK("Open Watcom", parsed("MAIN.C(12): Error! E1009: Expecting ';' but found '}'", M_ERROR, "MAIN.C", 12, 0, "Expecting ';' but found '}'"));
    CHECK("Open Watcom: a warning", parsed("MAIN.C(5): Warning! W202: Symbol 'x' has been defined, but not referenced", M_WARNING, "MAIN.C", 5, 0, "Symbol 'x' has been defined, but not referenced"));
    CHECK("Free Pascal", parsed("MAIN.PAS(12,5) Error: Identifier not found \"X\"", M_ERROR, "MAIN.PAS", 12, 5, "Identifier not found \"X\""));
    CHECK("Free Pascal: a hint is a note", parsed("MAIN.PAS(3,1) Hint: Unit \"Crt\" not used", M_NOTE, "MAIN.PAS", 3, 1, "Unit \"Crt\" not used"));
    CHECK("Turbo Pascal", parsed("MAIN.PAS(12): Error 3: Unknown identifier.", M_ERROR, "MAIN.PAS", 12, 0, "Unknown identifier."));
    CHECK("JWasm", parsed("MAIN.ASM(7) : Error A2008: syntax error : mov", M_ERROR, "MAIN.ASM", 7, 0, "syntax error : mov"));
    CHECK("Turbo C", parsed("Error MAIN.C 12: Undefined symbol 'x' in function main", M_ERROR, "MAIN.C", 12, 0, "Undefined symbol 'x' in function main"));
    CHECK("Turbo C: a warning, with a path", parsed("Warning D:\\HELLO\\MAIN.C 5: 'y' is assigned a value that is never used", M_WARNING, "D:\\HELLO\\MAIN.C", 5, 0, "'y' is assigned a value that is never used"));
    CHECK("the linker: an error that says nowhere", parsed("MAIN.O:MAIN.C:(.text+0x1c): undefined reference to `foo'", M_ERROR, NULL, 0, 0, NULL));
    CHECK("make stopping is an error", parsed("make: *** [Makefile:8: MAIN.EXE] Error 1", M_ERROR, NULL, 0, 0, NULL));
    CHECK("GCC's 'In function' is just text", parsed("MAIN.C: In function 'main':", M_TEXT, NULL, 0, 0, NULL));
    CHECK("...and so is a summary", parsed("Errors: 0  Warnings: 0", M_TEXT, NULL, 0, 0, NULL));
}

static void test_build(void)
{
    Project pr;
    Buffer *b;
    struct stat st;
    puts("Building, running and messages");
    mkdir(path("PROJ"), 0777);
    mkdir(path("PROJ/SUB"), 0777);
    write_file("PROJ/project.ini", "# A DOSDev project\n[project]\nname = Demo\nbuild_on = dos\n"
               "build = printf 'MAIN.C:3:10: error: expected semicolon\\nMAIN.C: In function x:\\nMAIN.C:4: warning: unused x\\nSUB/UTIL.C(4): Error! E1009: bad\\n'\n"
               "output = MAIN.EXE\nrun = echo ran > RAN.TXT\n", -1);
    write_file("PROJ/MAIN.C", "int main(void)\n{\n    int x\n    return util_fn();\n}\n", -1);
    write_file("PROJ/SUB/UTIL.C", "/* util */\n#include \"UTIL.H\"\n\nint util_fn(void)\n{\n    return 1;\n}\n", -1);
    write_file("PROJ/SUB/UTIL.H", "int util_fn(void);\n", -1);
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    b = files[cur];
    proj_find(b, &pr);
    CHECK("the project is the folder with project.ini", strstr(pr.dir, "/PROJ") && !strcmp(pr.name, "Demo") && !strcmp(pr.output, "MAIN.EXE"));
    CHECK("...it builds in DOS, so its build command is used", !strncmp(pr.build, "printf", 6) && !strcmp(pr.build_from, "project.ini"));
    keys(1, DK_F9);
    run();
    CHECK("F9 builds, and a failed build opens the messages", t_snapped && strstr(snap_row(1), "Build: 2 errors, 1 warning"));
    CHECK("...each line, errors marked", strstr(snap_row(2), "error") && strstr(snap_row(2), "MAIN.C(3): expected semicolon") && strstr(snap_row(5), "UTIL.C(4): bad"));
    CHECK("...counted", msgs_count(M_ERROR) == 2 && msgs_count(M_WARNING) == 1 && msgs_count(M_TEXT) == 1);
    CHECK("the line with the error is marked in the margin", attr_at(4, 1) == attr[A_MARKERR] && attr_at(5, 1) == attr[A_MARKWARN] && attr_at(3, 1) == attr[A_GUTTER]);
    keys(1, DK_F8);
    run();
    CHECK("F8 goes to the first error, at its column", b->cy == 2 && b->cx == 9);
    CHECK("...and the status line says what's wrong", strstr(row_text(24), "error") && strstr(row_text(24), "expected semicolon"));
    keys(1, DK_F8);
    run();
    CHECK("F8 again: the warning", b->cy == 3);
    keys(1, DK_F8);
    run();
    CHECK("...and the error in a file that wasn't open: it's opened", files[cur] != b && strstr(files[cur]->path, "UTIL.C") && files[cur]->cy == 3);
    keys(1, KM_SHIFT | DK_F8);
    run();
    CHECK("Shift-F8 goes back", files[cur] == b && b->cy == 3);
    keys(1, KM_ALT | DK_LEFT);
    run();
    CHECK("Alt-Left goes back to where the cursor was", files[cur] != b && files[cur]->cy == 3);
    keys(1, KM_ALT | DK_LEFT);
    run();
    CHECK("...and again", files[cur] == b && b->cy == 3);
    keys(3, KM_CTRL | DK_HOME, DK_ENTER, DK_ENTER);
    run();
    CHECK("messages move with their lines as lines come and go", msgs_at(b, 4) && msgs_at(b, 4)->kind == M_ERROR && !msgs_at(b, 2));
    keys(2, KM_CTRL | 'Z', KM_CTRL | 'Z');
    run();
    CHECK("...and back with undo", msgs_at(b, 2) && msgs_at(b, 2)->kind == M_ERROR);
    keys(1, DK_F7);
    run();
    CHECK("F7 lists the messages again", t_snapped && strstr(snap_row(1), "Build: 2 errors"));
    keys(3, DK_F7, DK_END, DK_ENTER);
    run();
    CHECK("...and Enter on one goes to it", strstr(files[cur]->path, "UTIL.C") && files[cur]->cy == 3);

    write_file("PROJ/project.ini", "[project]\nbuild_on = dos\nbuild = printf 'MAIN.C:3: warning: unused x\\n'; touch MAIN.EXE\n"
               "output = MAIN.EXE\nrun = echo ran > RAN.TXT\n", -1);
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    keys(1, DK_F9);
    run();
    CHECK("a build that works: said on the status line", t_execs > 0 && strstr(row_text(24), "Built MAIN.EXE, with 1 warning"));
    CHECK("...its warnings marked all the same", attr_at(4, 1) == attr[A_MARKWARN]);
    t_last_exec[0] = 0;
    keys(1, KM_CTRL | DK_F9);
    run();
    CHECK("Ctrl-F9 with nothing changed: no build, it just runs", !strcmp(t_last_exec, "echo ran > RAN.TXT") && stat(path("PROJ/RAN.TXT"), &st) == 0);
    CHECK("...and says it ended", strstr(row_text(24), "echo ended"));
    keys(1, KM_ALT | DK_F5);
    run();
    CHECK("Alt-F5 shows the program's screen", t_user_screens == 1);
    keys(3, KM_ALT | 'P', 'c', 'x');
    keys(2, DK_ENTER, DK_ENTER);
    run();
    proj_find(files[cur], &pr);
    CHECK("Project > Commands sets the build command, for now", !strcmp(pr.build, "x") && !strcmp(pr.build_from, "Commands"));

    mkdir(path("BAT"), 0777);
    write_file("BAT/BUILD.BAT", "@ECHO OFF\r\ngcc -o HI.EXE HI.C\r\n", -1);
    write_file("BAT/HI.C", "int main(void) { return 0; }\n", -1);
    fresh();
    ed_open(path("BAT/HI.C"), 0);
    proj_find(files[cur], &pr);
    CHECK("no project.ini: BUILD.BAT builds", !strcmp(pr.build, "BUILD.BAT") && !strcmp(pr.build_from, "BUILD.BAT") && !pr.run[0]);
    mkdir(path("LOOSE"), 0777);
    write_file("LOOSE/HELLO.C", "int main(void) { return 0; }\n", -1);
    fresh();
    ed_open(path("LOOSE/HELLO.C"), 0);
    keys(1, DK_F9);
    run();
    CHECK("nothing to build with: F9 asks, with a guess", t_snapped && snap_has("gcc -Wall -g -o HELLO.EXE -x c HELLO.C"));
}

static void test_find_files(void)
{
    puts("Find in files");
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    keys(2, KM_CTRL | DK_END, DK_ENTER);
    type("/* util_fn, not saved */");
    keys(1, KM_CTRL | KM_SHIFT | 'F');
    type("util_fn");
    keys(1, DK_ENTER);
    run();
    CHECK("Ctrl-Shift-F finds it in every file of the project", t_snapped && strstr(snap_row(1), "\"util_fn\": 4 found"));
    CHECK("...in what's open, saved or not", strstr(snap_row(2), "MAIN.C(4)") && strstr(snap_row(3), "MAIN.C(6): /* util_fn, not saved */"));
    CHECK("...and in the files on disk", strstr(snap_row(4), "SUB/UTIL.C(4): int util_fn(void)") || strstr(snap_row(5), "SUB/UTIL.C(4): int util_fn(void)"));
    keys(1, DK_F8);
    run();
    CHECK("F8 goes to each", files[cur]->cy == 3 && files[cur]->cx == 11);
    keys(1, KM_CTRL | KM_SHIFT | 'F');
    type("nothing_like_this");
    keys(1, DK_ENTER);
    run();
    CHECK("nothing found: said so", strstr(row_text(24), "isn't in any file"));
}

static int has_sym(const Sym *s, int n, const char *name, int line, char kind)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(s[i].name, name) && s[i].line == line && s[i].kind == kind)
            return 1;
    return 0;
}

static int scan(int lang, const char *text, Sym *s)
{
    Buffer *b = buf_new();
    int n;
    b->lang = lang;
    buf_insert(b, 0, 0, text, (int)strlen(text));
    n = sym_scan(lang, b->ln, b->n, s, 64);
    buf_free(b);
    return n;
}

static void test_symbols(void)
{
    static Sym s[64];
    int n;
    puts("Symbols and definitions");
    n = scan(L_C, "#include <stdio.h>\n#define MAX 10\nstruct point {\n    int x;\n};\ntypedef struct point Point;\n"
             "typedef int (*cmp_fn)(const void *, const void *);\nstatic int add(int a, int b)\n{\n    if (a) return a;\n}\n"
             "int proto(void);\n/* fake(void) */\nint\nmain(void)\n{\n    return 0;\n}\ntypedef struct {\n    int y;\n} Thing;\n", s);
    CHECK("C: defines, structs, typedefs, functions", n == 7 && has_sym(s, n, "MAX", 1, 'd') && has_sym(s, n, "point", 2, 't')
          && has_sym(s, n, "Point", 5, 't') && has_sym(s, n, "cmp_fn", 6, 't') && has_sym(s, n, "add", 7, 'f')
          && has_sym(s, n, "main", 14, 'f') && has_sym(s, n, "Thing", 20, 't'));
    n = scan(L_ASM, "org 100h\nstart:\n    mov ax, 1\n.loop:\n    jmp start\nmsg db 'Hi$'\n%define WIDTH 80\nPRINT proc\n", s);
    CHECK("assembly: labels, data, defines, procs (not .local)", n == 4 && has_sym(s, n, "start", 1, 'l') && has_sym(s, n, "msg", 5, 'v')
          && has_sym(s, n, "WIDTH", 6, 'd') && has_sym(s, n, "PRINT", 7, 'f'));
    n = scan(L_PASCAL, "program Demo;\ntype\n  TPoint = record\n    X: Integer;\n  end;\nprocedure Show(P: TPoint);\nbegin\nend;\n"
             "FUNCTION TObj.Area: Real;\n", s);
    CHECK("Pascal: program, types, procedures, methods", n == 4 && has_sym(s, n, "Demo", 0, 's') && has_sym(s, n, "TPoint", 2, 't')
          && has_sym(s, n, "Show", 5, 'f') && has_sym(s, n, "TObj.Area", 8, 'f'));
    n = scan(L_BASIC, "DECLARE SUB Hello ()\nSUB Hello\nEND SUB\nFUNCTION Name$ (x)\nEND FUNCTION\nTop:\nDEF FNsq (x) = x * x\n", s);
    CHECK("BASIC: SUB, FUNCTION, DEF FN, labels", n == 4 && has_sym(s, n, "Hello", 1, 'f') && has_sym(s, n, "Name$", 3, 'f')
          && has_sym(s, n, "Top", 5, 'l') && has_sym(s, n, "FNsq", 6, 'f'));
    n = scan(L_BATCH, "@ECHO OFF\n:: a comment\n:START\nGOTO END\n:END\n", s);
    CHECK("batch: labels", n == 2 && has_sym(s, n, "START", 2, 'l') && has_sym(s, n, "END", 4, 'l'));
    n = scan(L_MAKE, "CC = gcc\nall: DE.EXE\n\nDE.EXE: MAIN.C\n> $(CC) -o $@ $^\n.PHONY: all\nX := 1\n", s);
    CHECK("Makefile: targets", n == 2 && has_sym(s, n, "all", 1, 'l') && has_sym(s, n, "DE.EXE", 3, 'l'));
    n = scan(L_INI, "[project]\nname = x\n[more]\n", s);
    CHECK("INI: sections", n == 2 && has_sym(s, n, "project", 0, 's') && has_sym(s, n, "more", 2, 's'));
    n = scan(L_TEXT, "DE manual\n=========\n\nSpeed\n-----\ntext\n# Markdown heading\n", s);
    CHECK("text: underlined headings, and # headings", n == 3 && has_sym(s, n, "DE manual", 0, 's') && has_sym(s, n, "Speed", 3, 's')
          && has_sym(s, n, "Markdown heading", 6, 's'));

    fresh();
    ed_open(path("PROJ/SUB/UTIL.C"), 0);
    keys(1, KM_CTRL | 'L');
    run();
    CHECK("Ctrl-L lists the file's symbols", t_snapped && strstr(snap_row(1), "Symbols in UTIL.C") && strstr(snap_row(5), "util_fn"));
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    write_file("PROJ/MAIN.C", "int main(void)\n{\n    int x;\n    return util_fn();\n}\nstatic void helper(void)\n{\n}\n", -1);
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    keys(1, KM_CTRL | 'L');
    type("hel");
    keys(1, DK_ENTER);
    run();
    CHECK("...typing finds one, Enter goes to it, at its name", files[cur]->cy == 5 && files[cur]->cx == 12);
    ed_goto(4, 13);
    keys(1, DK_F12);
    run();
    CHECK("F12 goes to the definition, in a file that isn't open", strstr(files[cur]->path, "UTIL.C") && files[cur]->cy == 3 && files[cur]->cx == 4);
    keys(1, DK_F12);
    run();
    CHECK("...on the definition itself, it says so", strstr(row_text(24), "This is where util_fn is defined"));
    ed_goto(2, 12);
    keys(1, DK_F12);
    run();
    CHECK("F12 on #include \"UTIL.H\" opens it", strstr(files[cur]->path, "UTIL.H") != NULL);
    keys(1, KM_ALT | DK_LEFT);
    run();
    CHECK("Alt-Left comes back", strstr(files[cur]->path, "UTIL.C") && files[cur]->cy == 1);
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    keys(2, KM_CTRL | DK_END, DK_ENTER);
    type("strlen");
    keys(1, DK_F12);
    run();
    CHECK("F12 on a library function shows its reference entry", t_snapped && strstr(snap_row(1), " strlen "));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("nowhere_at_all");
    keys(1, DK_F12);
    run();
    CHECK("...and on a name defined nowhere, says so", strstr(row_text(24), "No definition of nowhere_at_all"));
}

/* A project made from DOSDev's SDL3 or PDCurses types: the reference knows
 * the library, F12 finds its headers in LIBS, Find in files leaves LIBS out,
 * and Project > Debug runs the project's debug command. */
static void test_libraries(void)
{
    const Ref *r;
    struct stat st;
    puts("Libraries, Debug and the manual");
    r = ref_find(L_C, "SDL_RenderGeometry", 18);
    CHECK("the reference has SDL3", r && !strcmp(r->where, "<SDL3/SDL.h>"));
    r = ref_find(L_C, "mvprintw", 8);
    CHECK("...and curses", r && !strcmp(r->where, "<curses.h>"));
    mkdir(path("SDLP"), 0777);
    mkdir(path("SDLP/LIBS"), 0777);
    mkdir(path("SDLP/LIBS/SDL3"), 0777);
    mkdir(path("SDLP/LIBS/SDL3/INCLUDE"), 0777);
    mkdir(path("SDLP/LIBS/SDL3/INCLUDE/SDL3"), 0777);
    write_file("SDLP/LIBS/SDL3/INCLUDE/SDL3/SDL.h", "/* SDL3 */\nbool SDL_Init(SDL_InitFlags flags);\n", -1);
    write_file("SDLP/project.ini", "[project]\nname = Cube\nbuild_on = linux\nbuild = make\noutput = MAIN.EXE\n"
               "run = MAIN.EXE\ndebug = echo debugging > DBG.TXT\n", -1);
    write_file("SDLP/MAIN.C", "#include <SDL3/SDL.h>\n\nint main(int argc, char *argv[])\n{\n    SDL_Init(0);\n    return 0;\n}\n", -1);
    fresh();
    ed_open(path("SDLP/MAIN.C"), 0);
    keys(2, KM_CTRL | DK_END, DK_ENTER);
    type("SDL_Cre");
    run();
    CHECK("typing SDL_Cre suggests from the reference", strstr(row_text(24), "Tab SDL_Create") != NULL);
    fresh();
    ed_open(path("SDLP/MAIN.C"), 0);
    ed_goto(1, 12);
    keys(1, DK_F12);
    run();
    CHECK("F12 on #include <SDL3/SDL.h> opens it from the project's LIBS", strstr(files[cur]->path, "LIBS/SDL3/INCLUDE/SDL3/SDL.h") != NULL);
    fresh();
    ed_open(path("SDLP/MAIN.C"), 0);
    keys(1, KM_CTRL | KM_SHIFT | 'F');
    type("SDL_Init");
    keys(1, DK_ENTER);
    run();
    CHECK("Find in files leaves the libraries in LIBS out", msgs_count(M_FOUND) == 1);
    t_last_exec[0] = 0;
    keys(2, KM_ALT | 'P', 'd');
    run();
    CHECK("Project > Debug runs the project's debug command", !strcmp(t_last_exec, "echo debugging > DBG.TXT") && stat(path("SDLP/DBG.TXT"), &st) == 0);
    fresh();
    ed_open(path("PROJ/MAIN.C"), 0);
    t_last_exec[0] = 0;
    keys(2, KM_ALT | 'P', 'd');
    run();
    CHECK("...and with none, says where to set it", !t_last_exec[0] && strstr(row_text(24), "debug =") != NULL);
    fresh();
    keys(1, DK_F1);
    run();
    CHECK("the manual's contents fit one screen", t_snapped && snap_has("Getting started") && snap_has("How DE works"));
    CHECK("...and it says it's 0.7", t_snapped && snap_has("Version 0.7"));
}

/* Keys that come faster than the screen can be drawn, the word index, the
 * key repeat option and DE /SPEED. */
static void test_speed(void)
{
    const char *what[8];
    double ms[8];
    int n, i, ok;
    puts("Speed: keys faster than drawing, the word index, DE /SPEED");
    fresh();
    ed_open(path("FAST.C"), 0);
    t_batch = 1;
    t_shows = 0;
    type("int counter_value = 0;\ncou");
    keys(1, DK_TAB);
    type(" = 1;");
    run();
    t_batch = 0;
    CHECK("keys waiting are all taken before the screen is drawn: drawn once", t_shows == 1);
    CHECK("...and do what they would one at a time, Tab taking the suggestion", text_is("int counter_value = 0;\ncounter_value = 1;"));
    CHECK("the word index matches the text", files[cur]->words && words_check(files[cur]));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("alpha beta alpha\ngamma\n");
    keys(3, KM_CTRL | DK_HOME, DK_END, DK_BACKSPACE);
    keys(2, KM_CTRL | 'Z', KM_CTRL | 'Y');
    run();
    CHECK("...after deleting, undo and redo too", words_check(files[cur]));
    buf_save(files[cur], path("FAST.BAS"));
    CHECK("...and after Save As changes the language", files[cur]->lang == L_BASIC && words_check(files[cur]));
    fresh();
    t_repeat = -1;
    keys(1, KM_ALT | 'O');
    key('k');
    run();
    CHECK("Options: fast key repeat, off", !opt_fastkeys && t_repeat == 0);
    keys(1, KM_ALT | 'O');
    run();
    CHECK("...the menu says so", t_snapped && strstr(snap_row(9), "[ ] Fast key repeat"));
    keys(1, KM_ALT | 'O');
    key('k');
    run();
    CHECK("...and on again", opt_fastkeys && t_repeat == 1);
    fresh();
    ed_open(path("SPEED.C"), 0);
    for (i = 0; i < 300; i++)
        type("    total = count_lines(buffer) + i;\n");
    run();
    n = ed_speed(what, ms, 8);
    for (ok = n == 5, i = 0; i < n; i++)
        if (!(ms[i] > 0) || !what[i][0])
            ok = 0;
    CHECK("DE /SPEED: five timings", ok);
    for (ok = 1, n = 0; n < 4; n++) {
        if (n == 2) {
            fresh();
            ed_open(path("SHORT.C"), 0);
            type("int x;\n");
            run();
        }
        opt_numbers = n & 1;
        for (i = 0; i < SR * SC; i++)
            scr[i] = 0xABCD;
        ed_goto(1, 0);
        ed_paint();
        for (i = 0; i < SR * SC; i++)
            if (scr[i] == 0xABCD)
                ok = 0;
    }
    opt_numbers = 1;
    CHECK("drawing covers every cell of the screen, with line numbers or without, long file or short", ok);
    CHECK("...and the file is still sound", buf_check(files[cur]) && words_check(files[cur]) && opt_suggest == 1);
}

/* ---- C++ ------------------------------------------------------------------------ */

static int at_role(int r, const char *s, int off, int role)
{
    int c = col_of(r, s);
    if (c < 0) {
        printf("        [%s] is not on row %d: [%s]\n", s, r, row_text(r));
        return 0;
    }
    return attr_at(r, c + off) == attr[role];
}

static void test_cpp_language(void)
{
    static char big[5000];
    int i;
    puts("C++: which files, and their colours");
    CHECK("C++ by its extension, in any case", lang_of("A.CPP") == L_CPP && lang_of("a.cxx") == L_CPP && lang_of("B.Cc") == L_CPP
          && lang_of("C.HPP") == L_CPP && lang_of("d.hh") == L_CPP && lang_of("E.HXX") == L_CPP);
    CHECK("...C by its", lang_of("F.C") == L_C && lang_of("g.h") == L_C && lang_of("README") == L_TEXT);
    CHECK("...and the C++ library's headers, which have none", lang_of("c:/djgpp/include/cxx/9/vector") == L_CPP
          && lang_of("/usr/include/c++/12/map") == L_CPP && lang_of("C:\\DJGPP\\INCLUDE\\CXX\\9\\TUPLE") == L_CPP);
    CHECK(".H: C", lang_of_text("X.H", "#include <stdio.h>\nint f(void);\n", 32) == L_C);
    CHECK(".H: class makes it C++", lang_of_text("X.H", "#pragma once\n  class Foo {\n};\n", 28) == L_CPP);
    CHECK("...so do namespace and template", lang_of_text("X.H", "namespace a {\n", 14) == L_CPP
          && lang_of_text("X.H", "template<class T>\n", 18) == L_CPP && lang_of_text("X.H", "template <class T>\n", 19) == L_CPP);
    CHECK("...and #include <vector>, a name with no dot", lang_of_text("X.H", "#include <vector>\n", 18) == L_CPP);
    CHECK("...not template as a name at the start of a line, in C", lang_of_text("X.H", "template = 3;\ntemplate x;\n", 25) == L_C);
    CHECK("...#include<vector> with no space is C++ too", lang_of_text("X.H", "#include<vector>\n", 17) == L_CPP);
    CHECK("...not #include <sys/types.h>", lang_of_text("X.H", "#include <sys/types.h>\n", 23) == L_C);
    CHECK("...not class in a comment, or a word that starts with it", lang_of_text("X.H", "/* the\nclass Foo */\n// class X\nint classes;\n", 43) == L_C);
    for (i = 0; i < 4096; i += 8)
        memcpy(big + i, "int aa;\n", 8);
    memcpy(big + 4096, "class Foo {\n", 12);
    CHECK("...only the first 4 KB are looked at", lang_of_text("X.H", big, 4108) == L_C);
    CHECK("a .C file is C whatever it says", lang_of_text("X.C", "class Foo {\n", 12) == L_C);

    fresh();
    write_file("SIGN.H", "#include <string>\nstd::string name();\n", -1);
    ed_open(path("SIGN.H"), 0);
    run();
    CHECK("a .H with C++ in it says C++ on the status line", files[cur]->lang == L_CPP && strstr(row_text(24), " C++ LF"));
    write_file("PLAIN.H", "int plain(void);\n", -1);
    ed_open(path("PLAIN.H"), 0);
    run();
    CHECK("...one without says C", files[cur]->lang == L_C && strstr(row_text(24), " C LF"));
    remove(path("NEWH.H"));
    ed_open(path("NEWH.H"), 0);
    type("namespace geo {\n");
    run();
    CHECK("a new .H is C while it is typed", files[cur]->lang == L_C && attr_at(2, col_of(2, "namespace")) == attr[A_TEXT]);
    keys(1, DK_F2);
    run();
    CHECK("...and C++ once saved with C++ in it", files[cur]->lang == L_CPP && attr_at(2, col_of(2, "namespace")) == attr[A_KEYWORD]);

    fresh();
    write_file("COL.CPP",
        "#include <vector>\n"
        "class Point final : public Base { };\n"
        "auto s = R\"xy(one )\" still\n"
        "two )x\" more\n"
        ")xy\"s; int k = 1'000'000;\n"
        "auto t = R\"(a)\" + u8\"b\" + \"c\"s + L'd';\n"
        "double d = 1.5e-3 + 0x1Fu + 0b1010 + 10_km;\n"
        "p = nullptr; q = static_cast<int>(x) and not y;\n"
        "concept char8_t co_await import module override\n"
        "/* a\n"
        "   b */ int z; // done\n", -1);
    ed_open(path("COL.CPP"), 0);
    run();
    keys(1, DK_HOME);           /* (the line Saved NEWH.H goes) */
    run();
    CHECK("C++: the status line says so", strstr(row_text(24), " C++ LF"));
    CHECK("C++: #include <vector> as in C", at_role(2, "#include", 0, A_PREPROC) && at_role(2, "<vector>", 0, A_STRING));
    CHECK("C++: class, final and public are keywords", at_role(3, "class", 0, A_KEYWORD) && at_role(3, "final", 0, A_KEYWORD)
          && at_role(3, "public", 0, A_KEYWORD) && at_role(3, "Point", 0, A_TEXT));
    CHECK("C++: a raw string", at_role(4, "R\"xy", 0, A_STRING) && at_role(4, "still", 0, A_STRING));
    CHECK("...goes on over lines, past a )\" that does not end it", at_role(5, "two", 0, A_STRING) && at_role(5, "more", 0, A_STRING));
    CHECK("...to its own )delim\", with a suffix", at_role(6, ")xy\"s", 4, A_STRING) && at_role(6, "int", 0, A_TYPE));
    CHECK("C++: digit separators are part of the number", at_role(6, "1'000'000", 1, A_NUMBER) && at_role(6, "1'000'000", 8, A_NUMBER)
          && at_role(6, "1'000'000;", 9, A_SYMBOL));
    CHECK("C++: R\"( ... )\" on one line, u8\"..\", \"..\"s and L'.'", at_role(7, "R\"(a)\"", 5, A_STRING) && at_role(7, "R\"(a)\" +", 7, A_TEXT)
          && at_role(7, "u8\"b\"", 0, A_STRING) && at_role(7, "\"c\"s", 3, A_STRING) && at_role(7, "L'd'", 0, A_STRING));
    CHECK("C++: numbers with exponents, hex, binary and suffixes", at_role(8, "1.5e-3", 4, A_NUMBER) && at_role(8, "1.5e-3", 5, A_NUMBER)
          && at_role(8, "0x1Fu", 4, A_NUMBER) && at_role(8, "0b1010", 5, A_NUMBER) && at_role(8, "10_km", 4, A_NUMBER));
    CHECK("C++: nullptr, a cast, and and not", at_role(9, "nullptr", 0, A_NUMBER) && at_role(9, "static_cast", 0, A_KEYWORD)
          && at_role(9, "and", 0, A_KEYWORD) && at_role(9, "not", 0, A_KEYWORD));
    CHECK("C++20: concept, char8_t, co_await; import and module are names", at_role(10, "concept", 0, A_KEYWORD)
          && at_role(10, "char8_t", 0, A_TYPE) && at_role(10, "co_await", 0, A_KEYWORD) && at_role(10, "import", 0, A_TEXT)
          && at_role(10, "module", 0, A_TEXT) && at_role(10, "override", 0, A_KEYWORD));
    CHECK("C++: comments as in C", at_role(12, "b */", 0, A_COMMENT) && at_role(12, "int", 0, A_TYPE) && at_role(12, "// done", 0, A_COMMENT));
    CHECK("...and the states kept for the lines are right", buf_state_check(files[cur]));
    {
        unsigned char role[16];
        syn_line(L_CPP, "x = .5e-3;", 10, 0, role);
        CHECK("C++: .5e-3 is one number", role[4] == A_NUMBER && role[7] == A_NUMBER && role[8] == A_NUMBER);
    }
    files[cur]->hs[0] = 1;                  /* a stray state for line 0, with hvalid 1 */
    files[cur]->hvalid = 1;
    run();
    CHECK("the first line's state is set again, whatever was there", files[cur]->hs[0] == 0 && at_role(2, "#include", 0, A_PREPROC));
    write_file("CW.C", "class Foo { public: new delete this; };\nint x = template + nullptr;\nchar *s = R\"x(;\nint y;\n", -1);
    ed_open(path("CW.C"), 0);
    run();
    CHECK("C: C++'s words are not keywords in a .C file", at_role(2, "class", 0, A_TEXT) && at_role(2, "public", 0, A_TEXT)
          && at_role(2, "new", 0, A_TEXT) && at_role(2, "this", 0, A_TEXT) && at_role(3, "template", 0, A_TEXT)
          && at_role(3, "nullptr", 0, A_TEXT) && at_role(3, "int", 0, A_TYPE));
    CHECK("...nor is there a raw string", at_role(5, "int", 0, A_TYPE));
}

static void test_cpp_symbols(void)
{
    static Sym s[64];
    int n;
    puts("C++: symbols and definitions");
    n = scan(L_CPP,
        "#include <vector>\n"                                     /* 0 */
        "namespace geo {\n"
        "\n"
        "class Point : public Shape {\n"
        "public:\n"
        "    Point(double x, double y) : x_(x), y_(y) {}\n"           /* 5 */
        "    ~Point() {}\n"
        "    double len() const;\n"
        "    double x() const { return x_; }\n"
        "    Point operator+(const Point &o) const\n"
        "    {\n"                                                 /* 10 */
        "        return Point(x_ + o.x_, y_ + o.y_);\n"
        "    }\n"
        "    explicit operator bool() const { return x_ != 0; }\n"
        "private:\n"
        "    double x_, y_;\n"                                    /* 15 */
        "};\n"
        "\n"
        "double Point::len() const\n"
        "{\n"
        "    if (x_) return 1;\n"                                 /* 20 */
        "    return 0;\n"
        "}\n"
        "\n"
        "template <class T>\n"
        "T biggest(const std::vector<T> &v)\n"                    /* 25 */
        "{\n"
        "    return v[0];\n"
        "}\n"
        "\n"
        "template <class T> class Stack {\n"                      /* 30 */
        "    void push(T x) { items.push_back(x); }\n"
        "};\n"
        "\n"
        "template <class T>\n"
        "void Stack<T>::pop()\n"                                  /* 35 */
        "{\n"
        "}\n"
        "\n"
        "enum class Colour { Red, Green };\n"
        "struct Pair;\n"                                          /* 40 */
        "using Names = std::vector<std::string>;\n"
        "}  // namespace geo\n"
        "\n"
        "int main(int argc,\n"
        "         char **argv)\n"                                 /* 45 */
        "{\n"
        "    geo::Point p(1, 2);\n"
        "    return 0;\n"
        "}\n"
        "bool operator==(const geo::Point &a, const geo::Point &b) { return true; }\n"   /* 50 */
        "typedef struct {\n"
        "    int y;\n"
        "} Thing;\n"
        "#define TWICE(x) ((x) * 2)\n"
        "Widget::Widget(int x)\n"                                /* 55 */
        "    : x_(x),\n"
        "      y_(2),\n"
        "      name_(\"w\")\n"
        "{\n"
        "}\n"                                                    /* 60 */
        "Derived::Derived(const Derived &o)\n"
        "    : Base(o)\n"
        "    , z_(o.z_)\n"
        "{\n"
        "}\n"                                                    /* 65 */
        "class Gadget {\n"
        "    Gadget(int x)\n"
        "        : x_(x), y_(2)\n"
        "    {}\n"
        "};\n"                                                   /* 70 */
        "void outer::inner::Widget::decl() {}\n", s);
    CHECK("C++: a namespace, a class and its constructor and destructor", has_sym(s, n, "geo", 1, 'n') && has_sym(s, n, "Point", 3, 't')
          && has_sym(s, n, "Point::Point", 5, 'f') && has_sym(s, n, "Point::~Point", 6, 'f'));
    CHECK("...methods defined in the class body, operators among them", has_sym(s, n, "Point::x", 8, 'f')
          && has_sym(s, n, "Point::operator+", 9, 'f') && has_sym(s, n, "Point::operator bool", 13, 'f'));
    CHECK("...and one defined outside it, as Foo::bar", has_sym(s, n, "Point::len", 18, 'f'));
    CHECK("...templates, by their names alone", has_sym(s, n, "biggest", 25, 'f') && has_sym(s, n, "Stack", 30, 't')
          && has_sym(s, n, "Stack::push", 31, 'f') && has_sym(s, n, "Stack::pop", 35, 'f'));
    CHECK("...enum class, using, a function over two lines, a free operator, typedef and #define",
          has_sym(s, n, "Colour", 39, 't') && has_sym(s, n, "Names", 41, 't') && has_sym(s, n, "main", 44, 'f')
          && has_sym(s, n, "operator==", 50, 'f') && has_sym(s, n, "Thing", 53, 't') && has_sym(s, n, "TWICE", 54, 'd'));
    CHECK("constructors whose initialiser lists go over lines: no symbols from the list",
          has_sym(s, n, "Widget::Widget", 55, 'f') && has_sym(s, n, "Derived::Derived", 61, 'f')
          && has_sym(s, n, "Gadget", 66, 't') && has_sym(s, n, "Gadget::Gadget", 67, 'f'));
    CHECK("a definition written with its namespaces is Class::method", has_sym(s, n, "Widget::decl", 71, 'f'));
    CHECK("...and nothing else: not declarations, not what is inside a function or a list", n == 23);
    if (n != 23) {
        int i;
        for (i = 0; i < n; i++)
            printf("        %c %d %s\n", s[i].kind, s[i].line, s[i].name);
    }

    n = scan(L_CPP,
        "class AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA {\n"
        "class BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB {\n"
        "class Inner {\n"
        "    void method() {}\n"
        "};\n};\n};\n", s);
    CHECK("classes inside classes with long names: the innermost class and the name, within bounds",
          has_sym(s, n, "Inner::method", 3, 'f'));
    n = scan(L_CPP,
        "#ifdef BIG\n"
        "long f(long a) {\n"
        "#else\n"
        "int f(int a) {\n"
        "#endif\n"
        "    return a;\n"
        "}\n"
        "void g() {}\n", s);
    CHECK("#if and #else branches that each open a brace: the symbols after them are still found",
          has_sym(s, n, "f", 1, 'f') && has_sym(s, n, "f", 3, 'f') && has_sym(s, n, "g", 7, 'f') && n == 3);
    mkdir(path("CPPP"), 0777);
    write_file("CPPP/project.ini", "[project]\nname = Geometry\n", -1);
    write_file("CPPP/GEO.HPP", "#pragma once\nnamespace geo {\nclass Point {\npublic:\n    double area() const { return 0; }\n"
               "    double len() const;\n};\n}\n", -1);
    write_file("CPPP/GEO.CPP", "#include \"GEO.HPP\"\n\nnamespace geo {\ndouble Point::len() const\n{\n    return 1;\n}\n}\n", -1);
    write_file("CPPP/UTIL.C", "int util_c(void)\n{\n    return 2;\n}\n", -1);
    write_file("CPPP/MAIN.CPP", "#include \"GEO.HPP\"\nextern \"C\" int util_c(void);\n\nint main()\n{\n"
               "    geo::Point p;\n    return p.len() + util_c();\n}\n", -1);
    fresh();
    ed_open(path("CPPP/GEO.HPP"), 0);
    keys(1, KM_CTRL | 'L');
    run();
    CHECK("Ctrl-L in a C++ header lists its classes and methods", t_snapped && snap_has("namespace") && snap_has("Point::area")
          && snap_has("Point  "));
    fresh();
    ed_open(path("CPPP/MAIN.CPP"), 0);
    ed_goto(7, 14);
    keys(1, DK_F12);
    run();
    CHECK("F12 on a method goes to Foo::bar, in a file that isn't open", strstr(files[cur]->path, "GEO.CPP") && files[cur]->cy == 3
          && files[cur]->cx == 7);
    fresh();
    ed_open(path("CPPP/MAIN.CPP"), 0);
    ed_goto(7, 23);
    keys(1, DK_F12);
    run();
    CHECK("...and from C++ to a function in a C file", strstr(files[cur]->path, "UTIL.C") && files[cur]->cy == 0);
    mkdir(path("DJ"), 0777);
    mkdir(path("DJ/include"), 0777);
    mkdir(path("DJ/include/cxx"), 0777);
    mkdir(path("DJ/include/cxx/9"), 0777);
    write_file("DJ/include/cxx/9/vector", "// <vector>\nnamespace std {\ntemplate<class T> class vector {\n};\n}\n", -1);
    setenv("DJDIR", path("DJ"), 1);
    fresh();
    ed_open(path("CPPP/VEC.CPP"), 0);
    type("#include <vector>");
    keys(1, DK_F12);
    run();
    unsetenv("DJDIR");
    CHECK("F12 on #include <vector> opens DJGPP's, as C++", strstr(files[cur]->path, "include/cxx/9/vector") && files[cur]->lang == L_CPP);
}

static void test_cpp_suggestions(void)
{
    const Ref *r;
    int i, ok;
    puts("C++: suggestions and the reference");
    r = ref_find(L_CPP, "make_unique", 11);
    CHECK("the reference has the C++ library, with each entry's header", r && !strcmp(r->where, "<memory>") && r->lang == L_CPP);
    r = ref_find(L_CPP, "sqrt", 4);
    CHECK("...C++'s own entry first: <cmath>'s sqrt, not <math.h>'s", r && !strcmp(r->where, "<cmath>"));
    CHECK("...and C's library too, in a C++ file", ref_find(L_CPP, "printf", 6) && ref_find(L_CPP, "printf", 6)->lang == L_C);
    CHECK("...but not C++'s in a C file", ref_find(L_C, "vector", 6) == NULL && ref_find(L_C, "sqrt", 4)->lang == L_C);
    CHECK("...and no threads, which DOS has not", ref_find(L_CPP, "thread", 6) == NULL);
    fresh();
    ed_open(path("SUG.CPP"), 0);
    type("nullp");
    run();
    CHECK("a C++ keyword is suggested in a C++ file", strstr(row_text(2), "nullptr") && attr_at(2, col_of(2, "tr")) == attr[A_GHOST]);
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("int vecky_word;\nvec");
    run();
    CHECK("...a word from the file, as in C", strstr(row_text(24), "Tab vecky_word"));
    keys(2, DK_END, DK_ENTER);
    type("std::vec");
    run();
    CHECK("after std::, the reference's std names: vector", strstr(row_text(24), "Tab vector") && strstr(row_text(24), "class vector"));
    keys(2, DK_END, DK_ENTER);
    type("std::");
    keys(1, KM_CTRL | ' ');
    run();
    CHECK("...Ctrl-Space right after std:: lists them all", t_snapped && strstr(snap_row(7), " abs ") && strstr(snap_row(8), " cin "));
    keys(1, 'v');
    keys(1, KM_CTRL | ' ');
    run();
    for (i = 6, ok = 1; i < 24; i++)
        if (strstr(snap_row(i), "vecky_word") || strstr(snap_row(i), "volatile"))
            ok = 0;
    CHECK("...std::v and Ctrl-Space: vector, not the file's words or keywords", t_snapped && strstr(snap_row(7), " vector ") && ok);
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("auto t0 = std::chrono::stea");
    run();
    CHECK("...after std::chrono:: too", strstr(row_text(24), "Tab steady_clock"));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("std::sort(v.begin(), ");
    run();
    CHECK("inside std::sort's brackets, the status line shows it", strstr(row_text(24), "void sort(RandomIt first, RandomIt last);"));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("s.find(");
    run();
    CHECK("after s. a member call gets no free function's signature", !strstr(row_text(24), "InputIt"));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("p->get(");
    run();
    CHECK("...nor after p->", !strstr(row_text(24), "tuple_element_t"));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("m.count");
    keys(1, KM_CTRL | DK_F1);
    run();
    CHECK("...and Ctrl-F1 on m.count searches, rather than showing std::count", t_snapped && snap_has("Reference: C++"));
    keys(2, KM_CTRL | 'A', DK_DEL);
    type("v.push_back");
    keys(1, KM_CTRL | DK_F1);
    run();
    CHECK("Ctrl-F1 on a member searches the reference: vector::push_back", t_snapped && snap_has("vector::push_back") && snap_has("<vector>"));
    keys(1, KM_SHIFT | DK_F1);
    run();
    CHECK("Shift-F1 in a C++ file: the C++ reference", t_snapped && snap_has("Reference: C++"));
    fresh();
    ed_open(path("SUG.C"), 0);
    type("nullp");
    run();
    CHECK("C++'s keywords are not suggested in a C file", sug_ghost(files[cur], NULL) == NULL);
}

static void test_cpp_build(void)
{
    puts("C++: building, and what g++ says");
    /* g++ 12.2 (the DJGPP cross compiler), as it printed these */
    CHECK("g++ 12: In member function is text", parsed("HELLO.CPP: In member function 'void Foo::bar()':", M_TEXT, NULL, 0, 0, NULL));
    CHECK("g++ 12: an error with a column", parsed("HELLO.CPP:15:13: error: 'y' was not declared in this scope", M_ERROR, "HELLO.CPP", 15, 13,
          "'y' was not declared in this scope"));
    CHECK("g++ 12: the line it shows, and the ^ under it, are text", parsed("   15 |     int x = y;", M_TEXT, NULL, 0, 0, NULL)
          && parsed("      |             ^", M_TEXT, NULL, 0, 0, NULL));
    CHECK("...even when the line says error:", parsed("   12 |     error: return -1;", M_TEXT, NULL, 0, 0, NULL));
    CHECK("g++ 12: a warning", parsed("HELLO.CPP:26:11: warning: comparison of integer expressions of different signedness: 'unsigned int' and 'int' [-Wsign-compare]",
          M_WARNING, "HELLO.CPP", 26, 11, "comparison of integer expressions of different signedness: 'unsigned int' and 'int' [-Wsign-compare]"));
    CHECK("g++ 12: In instantiation of is text", parsed("HELLO.CPP: In instantiation of 'T maxof(T, T) [with T = int]':", M_TEXT, NULL, 0, 0, NULL));
    CHECK("g++ 12: required from here is a note, at its place", parsed("HELLO.CPP:23:10:   required from here", M_NOTE, "HELLO.CPP", 23, 10, "required from here"));
    CHECK("g++ 12: In file included from is text", parsed("In file included from TUP.CPP:1:", M_TEXT, NULL, 0, 0, NULL)
          && parsed("                 from TUP.CPP:2:", M_TEXT, NULL, 0, 0, NULL));
    CHECK("g++ 12: an error in a library header with no extension",
          parsed("/opt/djgpp/lib/gcc/i586-pc-msdosdjgpp/12.2.0/include/c++/tuple:1357:25: error: static assertion failed: tuple index must be in range",
                 M_ERROR, "/opt/djgpp/lib/gcc/i586-pc-msdosdjgpp/12.2.0/include/c++/tuple", 1357, 25, "static assertion failed: tuple index must be in range"));
    CHECK("g++ 12: required by substitution of, in a .h", parsed("/opt/djgpp/lib/gcc/i586-pc-msdosdjgpp/12.2.0/include/c++/bits/utility.h:84:11:   "
          "required by substitution of 'template<long unsigned int __i, class _Tp> using __tuple_element_t = typename std::tuple_element::type'", M_NOTE,
          "/opt/djgpp/lib/gcc/i586-pc-msdosdjgpp/12.2.0/include/c++/bits/utility.h", 84, 11,
          "required by substitution of 'template<long unsigned int __i, class _Tp> using __tuple_element_t = typename std::tuple_element::type'"));
    CHECK("g++ 12: a note", parsed("TUP.CPP:8:33: note: declared here", M_NOTE, "TUP.CPP", 8, 33, "declared here"));
    CHECK("g++ 12: the linker", parsed("/tmp/ccAnPCIo.o:LNK.CPP:(.text+0x7): undefined reference to `foo()'", M_ERROR, NULL, 0, 0, NULL)
          && parsed("collect2: error: ld returned 1 exit status", M_ERROR, NULL, 0, 0, NULL));
    /* g++ 9.3, DJGPP's own, as it printed these inside DOS (test/gpp93-real.txt) */
    CHECK("g++ 9 (real): required from here", parsed("BAD.CPP:8:20:   required from here", M_NOTE, "BAD.CPP", 8, 20, "required from here"));
    CHECK("g++ 9 (real): a note in DJGPP's <vector>", parsed("c:/djgpp/include/cxx/9/bits/stl_vector.h:1184:7: note:   conversion of argument 1 would be ill-formed:",
          M_NOTE, "c:/djgpp/include/cxx/9/bits/stl_vector.h", 1184, 7, "  conversion of argument 1 would be ill-formed:"));
    CHECK("g++ 9 (real): a note after a tab", parsed("BAD.CPP:4:28: note: \t'virtual int Shape::area() const'", M_NOTE, "BAD.CPP", 4, 28,
          "\t'virtual int Shape::area() const'"));
    CHECK("g++ 9 (real): the source lines and their marks are text", parsed("   12 | }", M_TEXT, NULL, 0, 0, NULL)
          && parsed("      |                 |", M_TEXT, NULL, 0, 0, NULL) && parsed("      | ~            ", M_TEXT, NULL, 0, 0, NULL));
    CHECK("g++ 9 (real): In file included from is text", parsed("In file included from c:/djgpp/include/cxx/9/vector:67,", M_TEXT, NULL, 0, 0, NULL));
    /* (constructed, in g++ 9's shapes) */
    CHECK("g++ 9: an error in <vector>, which is a file called vector", parsed("c:/djgpp/include/cxx/9/vector:12:3: error: x", M_ERROR,
          "c:/djgpp/include/cxx/9/vector", 12, 3, "x"));
    CHECK("g++ 9: an error in its template code (.tcc)", parsed("c:/djgpp/include/cxx/9/bits/vector.tcc:1:1: error: y", M_ERROR,
          "c:/djgpp/include/cxx/9/bits/vector.tcc", 1, 1, "y"));

    mkdir(path("GPPP"), 0777);
    write_file("GPPP/project.ini", "[project]\nbuild_on = dos\nbuild = cat GPP.OUT\n", -1);
    write_file("GPPP/GPP.OUT", "HELLO.CPP: In function 'int main()':\n"
               "HELLO.CPP:3:5: error: 'y' was not declared in this scope\n    3 |     y = 1;\n      |     ^\n    4 |     error: ;\n"
               "HELLO.CPP: In instantiation of 'T f(T) [with T = int]':\nHELLO.CPP:5:6:   required from here\n", -1);
    write_file("GPPP/HELLO.CPP", "int main()\n{\n    y = 1;\n    error: ;\n    return f(1);\n}\n", -1);
    fresh();
    ed_open(path("GPPP/HELLO.CPP"), 0);
    keys(1, DK_F9);
    run();
    CHECK("a g++ build: its error counted, not the lines it shows", t_snapped && snap_has("Build: 1 error") && msgs_count(M_ERROR) == 1
          && msgs_count(M_NOTE) == 1);
    keys(2, DK_ESC, DK_F8);
    keys(1, DK_F8);
    run();
    CHECK("...F8 goes to the error, then to where the template was used", files[cur]->cy == 4 && files[cur]->cx == 5);
    if (srcdir[0]) {
        static const int want[][2] = { { 7, 23 }, { 7, 17 }, { 7, 17 }, { 9, 11 }, { 4, 8 }, { 4, 28 }, { 10, 5 }, { 11, 13 }, { 8, 20 }, { 3, 47 } };
        char real[4096], t[700];
        FILE *f;
        size_t got;
        int k, ok = 1;
        snprintf(t, sizeof t, "%s/gpp93-real.txt", srcdir);
        f = fopen(t, "rb");
        got = f ? fread(real, 1, sizeof real - 1, f) : 0;
        if (f)
            fclose(f);
        real[got] = 0;
        mkdir(path("GPP9"), 0777);
        write_file("GPP9/project.ini", "[project]\nbuild_on = dos\nbuild = cat GPP.OUT\n", -1);
        write_file("GPP9/GPP.OUT", real, (int)got);
        write_file("GPP9/BAD.CPP", "#include <vector>\n\ntemplate <typename T> T twice(T x) { return x.nothere(); }\n"
                   "struct Shape { virtual int area() const = 0; };\nint main() {\n    std::vector<int> v;\n"
                   "    v.push_back(\"text\");\n    int y = twice(3);\n    Shape s;\n    undeclared_name = 4;\n    return 0\n}\n", -1);
        fresh();
        ed_open(path("GPP9/BAD.CPP"), 0);
        keys(1, DK_F9);
        run();
        CHECK("real g++ 9.3 output: 7 errors and 7 notes, the four in DJGPP's header among them",
              got > 0 && msgs_count(M_ERROR) == 7 && msgs_count(M_NOTE) == 7);
        for (k = 0; k < 11; k++) {
            int w = k % 10;
            keys(1, DK_F8);
            run();
            if (!strstr(files[cur]->path, "BAD.CPP") || files[cur]->cy != want[w][0] - 1 || files[cur]->cx != want[w][1] - 1) {
                printf("        F8 %d went to %s %d:%d\n", k + 1, base_name(files[cur]->path), files[cur]->cy + 1, files[cur]->cx + 1);
                ok = 0;
            }
        }
        CHECK("...F8 goes to the 10 places in BAD.CPP in order, then round; not into the library's header", ok && nfiles == 1);
        keys(1, KM_SHIFT | DK_F8);
        run();
        CHECK("...Shift-F8 goes back the same way", files[cur]->cy == 2 && files[cur]->cx == 46);
        keys(2, DK_F7, DK_HOME);          /* line 7 of the list: the first note in stl_vector.h */
        keys(6, DK_DOWN, DK_DOWN, DK_DOWN, DK_DOWN, DK_DOWN, DK_DOWN);
        keys(1, DK_ENTER);
        run();
        CHECK("...F7 still lists the header's notes, and Enter still goes to one", strstr(row_text(24), "stl_vector.h"));
    }
    mkdir(path("LOOSE"), 0777);
    write_file("LOOSE/HELLO.CPP", "int main() { return 0; }\n", -1);
    fresh();
    ed_open(path("LOOSE/HELLO.CPP"), 0);
    keys(1, DK_F9);
    run();
    CHECK("a .CPP on its own: F9 offers gpp", t_snapped && snap_has("gpp -Wall -g -o HELLO.EXE -x c++ HELLO.CPP"));
}

/* Random editing, then undo all of it and redo all of it. A C file, then
 * a C++ one whose raw strings and comments cross lines, then a Pascal one
 * with both of its comments: each language's states, kept and worked out
 * again after every key, are checked against the text (buf_state_check). */
static int random_rounds(const char *name, const char *start, const int *more, int nmore, int rounds)
{
    static const int pool[] = {
        'a', 'b', ' ', '{', '}', '(', ')', ';', 'x', DK_ENTER, DK_TAB, KM_SHIFT | DK_TAB, DK_BACKSPACE, DK_DEL,
        DK_LEFT, DK_RIGHT, DK_UP, DK_DOWN, DK_HOME, DK_END, DK_PGUP, DK_PGDN, KM_SHIFT | DK_LEFT,
        KM_SHIFT | DK_RIGHT, KM_SHIFT | DK_UP, KM_SHIFT | DK_DOWN, KM_SHIFT | DK_END, KM_CTRL | DK_LEFT,
        KM_CTRL | DK_RIGHT, KM_CTRL | 'K', KM_CTRL | 'D', KM_ALT | DK_UP, KM_ALT | DK_DOWN, KM_ALT | ';',
        KM_CTRL | 'C', KM_CTRL | 'X', KM_CTRL | 'V', KM_CTRL | DK_BACKSPACE, KM_CTRL | DK_DEL,
        KM_CTRL | 'Z', KM_CTRL | 'Y', KM_CTRL | DK_HOME, KM_CTRL | DK_END, DK_INS, DK_ESC, KM_CTRL | 'B',
        'p', 'r', 'i', 'n', 't', 'f', 'z', 's', KM_CTRL | ' ', KM_CTRL | DK_F1, DK_F1, KM_SHIFT | DK_F1, DK_ESC, DK_ESC,
        '/', '*', '/', '*', '"', '\'', DK_ENTER,
    };
    int npool = (int)(sizeof pool / sizeof pool[0]);
    int round, i, bad = 0, stop;
    for (round = 0; round < rounds && !bad; round++) {
        char *before, *after, *now;
        fresh();
        ed_open(path(name), 0);
        type(start);
        run();
        before = text_of(files[cur]);
        for (i = 0; i < 600; i++) {
            int k = rand() % (npool + nmore);
            ed_key(k < npool ? pool[k] : more[k - npool]);
            if (i % 50 == 0)
                ed_draw();
            if (!buf_check(files[cur])) {
                bad = 1;
                printf("  FAIL  %s: the buffer broke in round %d, step %d\n", name, round, i);
                break;
            }
            if (!words_check(files[cur])) {
                bad = 1;
                printf("  FAIL  %s: the word index differs from the text in round %d, step %d\n", name, round, i);
                break;
            }
            if (i % 3 == 0 && !ed_paint_check()) {
                bad = 1;
                printf("  FAIL  %s: the screen drawn with its row cache differs, round %d, step %d\n", name, round, i);
                break;
            }
            if (!buf_state_check(files[cur])) {
                bad = 1;
                printf("  FAIL  %s: a kept syntax state is wrong, round %d, step %d\n", name, round, i);
                break;
            }
        }
        if (bad)
            break;
        ed_draw();
        overwrite = 0;
        after = text_of(files[cur]);
        stop = files[cur]->upos;          /* the random keys may have undone some */
        while (buf_undo(files[cur]))
            ;
        now = text_of(files[cur]);
        if (strcmp(now, "")) {
            /* everything since the file opened, typing included, is undone */
            bad = 1;
            printf("  FAIL  %s: undoing everything in round %d left: [%s]\n", name, round, now);
        }
        free(now);
        while (files[cur]->upos < stop && buf_redo(files[cur]))
            ;
        now = text_of(files[cur]);
        if (strcmp(now, after)) {
            bad = 1;
            printf("  FAIL  %s: redoing everything in round %d gave a different text\n", name, round);
        }
        if (!files[cur]->words || !words_check(files[cur])) {
            bad = 1;
            printf("  FAIL  %s: after undo and redo, the word index differs from the text (round %d)\n", name, round);
        }
        free(now);
        free(before);
        free(after);
    }
    return !bad;
}

static void test_random(void)
{
    /* R"x( ... )x" and the like: keys that open and close raw strings */
    static const int cpp_keys[] = { 'R', '"', '(', ')', 'x', 'R', '"', '(', ')', '"', ')', 'x', '"', '\'', '1' };
    static const int pas_keys[] = { '{', '}', '(', '*', ')', '*', ')', '{', '}', '$' };
    puts("Random editing (undo all of it, redo all of it)");
    srand(1234);
    CHECK("40 rounds of 600 random keys: the buffer, its word index, syntax states and screen stay sound; undo and redo are exact",
          random_rounds("RAND.C", "int f(void)\n{\n    return (1 + 2);\n}\n", NULL, 0, 40));
    CHECK("...in C++, with raw strings over lines, 20 rounds",
          random_rounds("RAND.CPP", "auto s = R\"x(one\ntwo )\" three\n)x\";\nint f() { return 1'0; } /* a\nb */\nauto t = R\"(\n)\";\n",
                        cpp_keys, (int)(sizeof cpp_keys / sizeof cpp_keys[0]), 20));
    CHECK("...in Pascal, with { } and (* *) comments over lines, 20 rounds",
          random_rounds("RAND.PAS", "program P; { a\nb } (* c\nd *)\nbegin WriteLn('x') end.\n",
                        pas_keys, (int)(sizeof pas_keys / sizeof pas_keys[0]), 20));
}

/* More raw-string delimiters than the table holds (run last: the table
 * lasts as long as the program). A new one is then a wildcard state that
 * ends at the first ) with up to 16 delimiter characters and a ". */
static void test_raw_table_full(void)
{
    unsigned char role[64];
    char line[32];
    int i, st = 0, first_full = -1, ok = 1;
    puts("C++: more raw-string delimiters than the table holds");
    for (i = 0; i < 300; i++) {
        int n = snprintf(line, sizeof line, "R\"d%03d(", i), e;
        st = syn_line(L_CPP, line, n, 0, role);
        n = snprintf(line, sizeof line, ")d%03d\" int x;", i);
        e = syn_line(L_CPP, line, n, st, role);
        if (e != 0 || role[n - 6] != A_TYPE)
            ok = 0;                         /* each one still ends at its own )delim" */
        if (st == 255 && first_full < 0)
            first_full = i;
    }
    CHECK("every raw string ends at its own delimiter, past the table's size too", ok);
    CHECK("...the table fills (a wildcard state after that)", first_full > 0 && st == 255);
    st = syn_line(L_CPP, "R\"new1(", 7, 0, role);
    i = syn_line(L_CPP, "a )\" b", 6, st, role);
    CHECK("...a wildcard string ends at the first ) with up to 16 delimiter characters and a \", )\" too", i == 0);
    i = syn_line(L_CPP, "a ) b )q(\" c", 12, st, role);
    CHECK("...nor at ) followed by a character no delimiter has", i == st);
}

/* Open a file, play a script, and write the screen (or the menu or dialog
 * open when the script ran out) as hex cells, for pictures. */
static void shot(const char *file, const char *out, const char *script)
{
    FILE *f;
    int i;
    const unsigned short *cells;
    fresh();
    if (file[0])
        ed_open(file, 0);
    for (; script && *script; script++) {
        switch (*script) {
        case 'D': key(DK_DOWN); break;
        case 'U': key(DK_UP); break;
        case 'R': key(DK_RIGHT); break;
        case 'S': key(KM_SHIFT | DK_DOWN); break;
        case 'M': key(DK_F10); break;
        case 'E': key(KM_ALT | 'E'); break;
        case 'H': key(DK_F1); break;
        case 'F': key(KM_CTRL | 'F'); break;
        case 'T': key(KM_ALT | 'O'); key('c'); break;
        case 'O': key(KM_CTRL | 'O'); break;
        case 'N': key(KM_ALT | 'O'); key('n'); break;
        case 'G': key(KM_CTRL | ' '); break;
        case 'Z': key(KM_CTRL | DK_END); break;
        case 'L': key(KM_CTRL | DK_F1); break;
        case 'K': key(KM_SHIFT | DK_F1); break;
        case 'B': key(DK_F9); break;
        case 'Y': key(KM_CTRL | 'L'); break;
        case 'W': key(DK_F7); break;
        case 'J': key(DK_F8); break;
        case 'X': key(KM_CTRL | KM_SHIFT | 'F'); break;
        case '\n': key(DK_ENTER); break;
        default: key((unsigned char)*script); break;
        }
    }
    t_snapped = 0;
    while (t_kpos < t_nkeys && running) {
        ed_draw();
        ed_key(plat_key());
    }
    ed_draw();
    cells = t_snapped ? t_snap : t_screen;
    f = fopen(out, "w");
    for (i = 0; i < t_rows * t_cols; i++)
        fprintf(f, "%02x%02x%c", cells[i] & 0xFF, cells[i] >> 8, (i + 1) % t_cols ? ' ' : '\n');
    fprintf(f, "cursor %d %d %d\n", t_crow, t_ccol, t_snapped ? 0 : t_cursor);
    fclose(f);
}

/* DOSDev's palettes, as DOSDEV_PALETTE gives them. */
#define PAL_MIDNIGHT "1013182e34408fb5737f9fc09a35408e6f8fa0704a98a3b64c566a5e81aca3be8c88c0d0d08770b48eadf3d58feceff4"
#define PAL_PAPER    "1a1a1af4f1e87fb069a9c3dcc0392b7a3e9dc98a2bb3b0a65f5c55b8c2cc2e7d321f6f8bb03a2e6a3d9a2020201f3b70"

static void test_theme(void)
{
    FILE *f;
    char line[200];
    int saved = 0;
    puts("Colours that follow DOSDev");
    CHECK("DE 0.7", !strcmp(DE_VERSION, "0.7"));
    CHECK("five themes", NTHEMES == 5 && !strcmp(theme_name[0], "Follow DOSDev") && !strcmp(theme_name[1], "Borland blue")
          && !strcmp(theme_name[2], "Midnight") && !strcmp(theme_name[3], "Paper") && !strcmp(theme_name[4], "Solar"));

    theme_set(1);
    unsetenv("DOSDEV_PALETTE");
    t_pal_calls = t_pal_restores = 0;
    theme_set(0);
    CHECK("outside DOSDev: no palette call", t_pal_calls == 0);
    CHECK("...and DE's own Borland blue", attr[A_TEXT] == 0x1E && attr[A_DIALOGHOT] == 0x7E);

    setenv("DOSDEV_PALETTE", PAL_MIDNIGHT, 1);
    theme_set(0);
    CHECK("in DOSDev: its 16 colours are loaded", t_pal_calls == 1 && t_pal_set && t_pal[3] == 0x2e && t_pal[4] == 0x34
          && t_pal[5] == 0x40 && t_pal[45] == 0xec && t_pal[47] == 0xf4);
    CHECK("...with the roles for a dark palette", attr[A_TEXT] == 0x1E && attr[A_DIALOGHOT] == 0x74);
    theme_done();
    CHECK("...and put back when DE ends", t_pal_set == 0 && t_pal_restores == 1);

    setenv("DOSDEV_PALETTE", PAL_PAPER, 1);
    theme_set(0);
    CHECK("a light palette gets the roles for light paper", t_pal_set && attr[A_COMMENT] == 0x18 && attr[A_ERROR] == 0x41);

    theme_set(1);
    CHECK("Borland blue puts the colours DE found back", t_pal_set == 0);
    t_pal_calls = 0;
    setenv("DOSDEV_PALETTE", "12345", 1);
    theme_set(0);
    setenv("DOSDEV_PALETTE", PAL_MIDNIGHT "0", 1);
    theme_set(0);
    strcpy(line, PAL_MIDNIGHT);
    line[0] = line[1] = 'z';
    setenv("DOSDEV_PALETTE", line, 1);
    theme_set(0);
    CHECK("a malformed DOSDEV_PALETTE: no palette call", t_pal_calls == 0);
    CHECK("...and DE's own Borland blue", attr[A_TEXT] == 0x1E && attr[A_DIALOGHOT] == 0x7E);

    unsetenv("DOSDEV_PALETTE");
    theme_set(2);
    CHECK("Midnight on its own, outside DOSDev", t_pal_set && t_pal[3] == 0x2e && attr[A_DIALOGHOT] == 0x74);
    theme_set(4);
    CHECK("Solar", t_pal_set && t_pal[3] == 0x00 && t_pal[4] == 0x2b && t_pal[5] == 0x36);
    theme_set(1);

    f = fopen(cfg_path, "w");
    fputs("numbers=1\ntheme=2\n", f);
    fclose(f);
    config_load();
    CHECK("DE 0.4's Green phosphor opens as Borland blue", theme == 1);
    f = fopen(cfg_path, "w");
    fputs("theme=3\n", f);
    fclose(f);
    config_load();
    CHECK("...and its Paper white as Paper", theme == 3);
    f = fopen(cfg_path, "w");
    fputs("theme=0\n", f);
    fclose(f);
    config_load();
    CHECK("...and its Borland blue as Follow DOSDev", theme == 0);
    theme_set(4);
    config_save();
    f = fopen(cfg_path, "r");
    while (f && fgets(line, sizeof line, f))
        if (!strcmp(line, "colours=solar\n"))
            saved = 1;
    if (f)
        fclose(f);
    CHECK("the theme is saved by name", saved);
    theme_set(1);
    config_load();
    CHECK("...and read back", theme == 4);

    fresh();
    theme_set(0);
    keys(1, KM_ALT | 'O');
    run();
    CHECK("the Options menu says Follow DOSDev", t_snapped && snap_has("Colours: Follow DOSDev"));
    theme_set(0);
    remove(cfg_path);
}

/* ---- work that must not be lost ------------------------------------------ */

static void set_mtime(const char *name, long t)
{
    struct utimbuf u;
    u.actime = u.modtime = (time_t)t;
    utime(path(name), &u);
}

static long mtime_now(const char *name)
{
    struct stat st;
    return stat(path(name), &st) ? -1 : (long)st.st_mtime;
}

/* Keys to open a file and run to the end of them: what's on the screen
 * is what a dialog left when they ran out. */
static void test_disk_changes(void)
{
    Buffer *b;
    Project pr;
    puts("A file that changed on disk");
    write_file("CH.TXT", "one\n", -1);
    set_mtime("CH.TXT", 1000000000L);
    fresh();
    ed_open(path("CH.TXT"), 0);
    b = files[cur];
    CHECK("a file read is not changed on disk", !buf_changed_on_disk(b) && b->disk_time == 1000000000L);
    keys(2, 'X', DK_F2);
    run();
    CHECK("an unchanged file: saving never asks", !t_snapped && file_is("CH.TXT", "Xone\n", -1) && strstr(row_text(24), "Saved CH.TXT"));
    CHECK("...and saving takes the new time as its own", !buf_changed_on_disk(b));

    write_file("CH.TXT", "XYone\nmore\n", -1);
    set_mtime("CH.TXT", mtime_now("CH.TXT") - 0);
    set_mtime("CH.TXT", b->disk_time);
    CHECK("a change in size alone, with the same time, is a change", buf_changed_on_disk(b));
    write_file("CH.TXT", "two\n", -1);
    set_mtime("CH.TXT", 1000000500L);
    CHECK("a file written by another program is changed on disk", buf_changed_on_disk(b));
    keys(2, 'Y', DK_F2);
    run();
    CHECK("saving asks", t_snapped && snap_has("changed on disk") && snap_has("Reload") && snap_has("Keep mine"));
    CHECK("...and writes nothing until it is answered", file_is("CH.TXT", "two\n", -1));
    keys(3, 'Z', DK_F2, 'K');
    run();
    CHECK("Keep mine writes the buffer", file_is("CH.TXT", "XYZone\n", -1) && !buf_modified(b));
    write_file("CH.TXT", "three\n", -1);
    set_mtime("CH.TXT", 1000000900L);
    keys(3, 'Q', DK_F2, DK_ESC);
    run();
    CHECK("Esc leaves the file as it is, and the buffer too", file_is("CH.TXT", "three\n", -1) && buf_modified(b));
    keys(2, DK_F2, 'R');
    run();
    CHECK("Reload replaces the buffer with the file", text_is("three") && !buf_modified(b) && !buf_changed_on_disk(b));
    CHECK("...and says so", strstr(row_text(24), "Reloaded CH.TXT"));
    keys(3, KM_CTRL | 'Z', KM_CTRL | 'Z', DK_F2);
    run();
    CHECK("...undo does not reach back past the reload", text_is("three") && !t_snapped);

    /* Returning from a run. The run writes the file, then gives it a time. */
    mkdir(path("RUNP"), 0777);
    write_file("RUNP/RUNF.TXT", "old\n", -1);
    set_mtime("RUNP/RUNF.TXT", 1000000000L);
    write_file("RUNP/project.ini", "[project]\nbuild_on = dos\nrun = printf 'fresh\\n' > RUNF.TXT; touch -d @1000000100 RUNF.TXT\n", -1);
    fresh();
    ed_open(path("RUNP/RUNF.TXT"), 0);
    b = files[cur];
    proj_find(b, &pr);
    keys(1, KM_CTRL | DK_F9);
    run();
    CHECK("returning from a run: an unchanged buffer is reloaded", !t_snapped && text_is("fresh") && !buf_modified(b));
    CHECK("...with a line to say so", strstr(row_text(24), "RUNF.TXT changed on disk"));
    keys(1, 'm');
    run();
    write_file("RUNP/project.ini", "[project]\nbuild_on = dos\nrun = printf 'again\\n' > RUNF.TXT; touch -d @1000000200 RUNF.TXT\n", -1);
    keys(1, KM_CTRL | DK_F9);
    run();
    CHECK("...a buffer with changes asks", t_snapped && snap_has("RUNF.TXT") && snap_has("Reload") && snap_has("Keep mine"));
    CHECK("...Esc keeps the buffer as it is", text_is("mfresh") && buf_modified(b));
    /* a size other than the one DE knows, so Keep mine must take both */
    write_file("RUNP/project.ini", "[project]\nbuild_on = dos\nrun = printf 'again, and longer\\n' > RUNF.TXT; touch -d @1000000250 RUNF.TXT\n", -1);
    keys(2, KM_CTRL | DK_F9, 'K');
    run();
    CHECK("...so does Keep mine", !t_snapped && text_is("mfresh") && buf_modified(b));
    keys(1, KM_CTRL | DK_F9);
    run();
    CHECK("...and it is not asked again for the same change, though the size changed too", !t_snapped && text_is("mfresh"));
    write_file("RUNP/project.ini", "[project]\nbuild_on = dos\nrun = printf 'last\\n' > RUNF.TXT; touch -d @1000000300 RUNF.TXT\n", -1);
    keys(2, KM_CTRL | DK_F9, 'R');
    run();
    CHECK("...Reload takes the file", text_is("last") && !buf_modified(b));
    keys(1, KM_CTRL | DK_F9);
    run();
    CHECK("a run that changes nothing asks nothing", !t_snapped && text_is("last") && !strstr(row_text(24), "changed on disk"));
    CHECK("a file that is not there is not changed on disk", mtime_now("RUNP/NOPE.TXT") < 0);
}

static void test_make_tabs(void)
{
    puts("Tab in a Makefile");
    opt_spaces = 1;
    fresh();
    ed_open(path("Makefile"), 0);
    keys(1, DK_TAB);
    run();
    CHECK("Makefile: Tab inserts a tab character, as Make needs", text_is("\t"));
    fresh();
    ed_open(path("PROG.MAK"), 0);
    keys(2, DK_TAB, 'x');
    run();
    CHECK("*.MAK too", text_is("\tx"));
    fresh();
    ed_open(path("MAIN.C"), 0);
    keys(1, DK_TAB);
    run();
    CHECK("MAIN.C: Tab follows the option (spaces)", text_is("    "));
    fresh();
    opt_spaces = 0;
    ed_open(path("MAIN.C"), 0);
    keys(1, DK_TAB);
    run();
    CHECK("...and (tabs)", text_is("\t"));
    opt_spaces = 1;
}

static void test_big_and_memory(void)
{
    FILE *f;
    int r, n;
    pid_t pid;
    int st;
    char *t;
    puts("Memory and big files");
    f = fopen(path("BIG.TXT"), "wb");
    fseek(f, DE_MAX_FILE, SEEK_SET);
    fputc('x', f);
    fclose(f);
    fresh();
    ed_open(path("CH.TXT"), 0);
    keys(1, 'k');
    run();
    n = nfiles;
    r = ed_open(path("BIG.TXT"), 0);
    CHECK("a file over the limit is refused", r == -1 && nfiles == n);
    ed_draw();
    CHECK("...with a message that says why", strstr(row_text(24), "larger than 16 MB"));
    CHECK("...and what was open is as it was", buf_modified(files[cur]) && strstr(files[cur]->path, "CH.TXT"));
    f = fopen(path("BIG.TXT"), "wb");
    fseek(f, DE_MAX_FILE - 1, SEEK_SET);
    fputc('\n', f);
    fclose(f);
    CHECK("a file at the limit is opened", ed_open(path("BIG.TXT"), 0) == 0);
    remove(path("BIG.TXT"));

    write_file("KEEP.TXT", "kept\n", -1);
    remove(path("WORK.SAV"));
    remove(path("KEEP.SAV"));
    remove(path("WORK.SA0"));
    remove(path("WORK.SA1"));
    write_file("WORK.SAV", "from an earlier crash\n", -1);
    remove(path("UNTITL04.SAV"));
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        int i;
#if defined(__SANITIZE_ADDRESS__)
        __lsan_disable();
#endif
        if (chdir(dir))              /* a file with no name is saved where DE is */
            _exit(78);
        fresh();
        ed_open(path("KEEP.TXT"), 0);
        ed_open(path("WORK.TXT"), 0);
        freopen(path("OOM.ERR"), "w", stderr);
        type("hello world");
        run();
        ed_open(path("WORK.H"), 0);      /* the same name, another extension */
        type("header text");
        run();
        ed_new();
        type("no name yet");
        run();
        de_fail_alloc = 1;
        for (i = 0; i < 100; i++)
            key('a');
        run();
        _exit(77);              /* DE should have ended itself */
    }
    waitpid(pid, &st, 0);
    CHECK("memory ran out: DE ends with exit code 3", WIFEXITED(st) && WEXITSTATUS(st) == 3);
    t = NULL;
    f = fopen(path("WORK.SA0"), "rb");
    if (f) {
        char buf[300];
        size_t k = fread(buf, 1, sizeof buf - 1, f);
        buf[k] = 0;
        t = strdup(buf);
        fclose(f);
    }
    CHECK("...having written each unsaved file as NAME.SAV (here NAME.SA0: a NAME.SAV was there)", t && !strncmp(t, "hello world", 11));
    free(t);
    CHECK("...a file already there is not written over", file_is("WORK.SAV", "from an earlier crash\n", -1));
    t = NULL;
    f = fopen(path("WORK.SA1"), "rb");
    if (f) {
        char buf[300];
        size_t k = fread(buf, 1, sizeof buf - 1, f);
        buf[k] = 0;
        t = strdup(buf);
        fclose(f);
    }
    CHECK("...nor one written a moment before: WORK.H goes to WORK.SA1", t && !strncmp(t, "header text", 11));
    free(t);
    CHECK("...the unsaved file with no name too", mtime_now("UNTITL04.SAV") >= 0);
    CHECK("...and only those", mtime_now("KEEP.SAV") < 0);
    t = NULL;
    f = fopen(path("OOM.ERR"), "rb");
    if (f) {
        char buf[600];
        size_t k = fread(buf, 1, sizeof buf - 1, f);
        buf[k] = 0;
        t = strdup(buf);
        fclose(f);
    }
    CHECK("...and said which, and why it ended", t && strstr(t, "saved in") && strstr(t, "WORK.SA0") && strstr(t, "WORK.SA1") && strstr(t, "out of memory"));
    free(t);
    CHECK("...the real file untouched", file_is("KEEP.TXT", "kept\n", -1) && mtime_now("WORK.TXT") < 0);
}

static void test_check_switch(void)
{
    FILE *f;
    char line[80];
    int r, quiet;
    puts("DE /CHECK");
    remove(path("CHECK.OUT"));
    CHECK("/CHECK FILE returns 0", de_check(path("CHECK.OUT")) == 0);
    line[0] = 0;
    f = fopen(path("CHECK.OUT"), "rb");
    if (f) {
        size_t k = fread(line, 1, sizeof line - 1, f);
        line[k] = 0;
        fclose(f);
    }
    CHECK("...and writes the line", !strcmp(line, "DE 0.7 ok\n"));
    fflush(stderr);
    quiet = dup(2);
    freopen("/dev/null", "w", stderr);
    r = de_check(path("NODIR/X.OUT"));
    fflush(stderr);
    dup2(quiet, 2);
    close(quiet);
    CHECK("...into a folder that is not there: 1, with no file", r == 1);
}

int main(int argc, char **argv)
{
    t_rows = 25;
    t_cols = 80;
    scr_init();
    theme_set(0);
    strcpy(cfg_path, "/tmp/DE-test.CFG");
    if (argc >= 4 && !strcmp(argv[1], "--shot")) {
        snprintf(dir, sizeof dir, "/tmp");
        shot(argv[2], argv[3], argc > 4 ? argv[4] : NULL);
        return 0;
    }
    snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "/tmp/detest");
    snprintf(srcdir, sizeof srcdir, "%s", argc > 2 ? argv[2] : "");
    mkdir(dir, 0755);
    test_typing_and_files();
    test_undo();
    test_selection_and_clipboard();
    test_indent_and_comments();
    test_find_and_replace();
    test_brackets_and_colours();
    test_menus_dialogs_files();
    test_theme();
    test_mouse_and_view();
    test_suggestions();
    test_speed();
    test_parse();
    test_build();
    test_find_files();
    test_symbols();
    test_libraries();
    test_cpp_language();
    test_cpp_symbols();
    test_cpp_suggestions();
    test_cpp_build();
    test_disk_changes();
    test_make_tabs();
    test_big_and_memory();
    test_check_switch();
    test_random();
    test_raw_table_full();
    printf("\n%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
