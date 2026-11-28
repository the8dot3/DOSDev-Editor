/* DE - the DOSDev Editor: a colour text editor for DOS programmers.
 * Shared declarations. DE is plain C; only DOS.C knows it runs on DOS
 * (DJGPP), so the rest builds and is tested anywhere.
 */
#ifndef DE_H
#define DE_H

#include <stddef.h>

#define DE_NAME    "DOSDev Editor"
#define DE_VERSION "0.7"
#define MAXFILES   16
#define DE_PATH    260
#define DE_MAX_FILE (16L * 1024 * 1024)   /* the largest file DE opens: see DE.TXT */

/* ---- keys -------------------------------------------------------------- */
/* A key is a character (32-255), one of the codes below, or either with
 * KM_SHIFT, KM_CTRL or KM_ALT added. Ctrl+A is KM_CTRL | 'A'; Alt+F is
 * KM_ALT | 'F'. */
#define DK_BACKSPACE 8
#define DK_TAB       9
#define DK_ENTER     13
#define DK_ESC       27
enum {
    DK_UP = 0x100, DK_DOWN, DK_LEFT, DK_RIGHT, DK_HOME, DK_END, DK_PGUP, DK_PGDN,
    DK_INS, DK_DEL, DK_F1, DK_F2, DK_F3, DK_F4, DK_F5, DK_F6, DK_F7, DK_F8, DK_F9,
    DK_F10, DK_F11, DK_F12, DK_MOUSE, DK_NONE
};
#define KM_SHIFT 0x1000
#define KM_CTRL  0x2000
#define KM_ALT   0x4000
#define KM_ALL   (KM_SHIFT | KM_CTRL | KM_ALT)
#define KBASE(k) ((k) & 0x0FFF)

/* ---- the mouse (filled in by the platform before it returns DK_MOUSE) --- */
enum { M_PRESS = 1, M_RELEASE, M_DRAG };
typedef struct { int row, col, event; } Mouse;
extern Mouse mouse;

/* ---- the platform: DOS.C on DOS ----------------------------------------- */
int  plat_init(void);          /* 0: ready */
void plat_done(void);          /* puts the DOS screen back */
int  plat_rows(void);
int  plat_cols(void);
/* Show a screen of cells (character | attribute << 8), and the cursor:
 * 0 hidden, 1 a line (insert), 2 a block (overwrite). */
void plat_show(const unsigned short *cells, int row, int col, int cursor);
int  plat_key(void);           /* waits for a key or a mouse event */
int  plat_pending(void);       /* is a key waiting already? */
int  plat_shift(void);         /* is Shift held down now? */
long plat_ms(void);            /* a clock, in milliseconds (DOS: 55 ms steps) */
void plat_repeat(int fast);    /* how fast a key held down repeats */
/* Run a DOS command. With a log, what it says goes there and the screen is
 * left alone; without, it has the screen, and a key brings DE back. Returns
 * its exit status, or -1 if it couldn't be started. The screen may come back
 * another size: see plat_rows(). */
int  plat_exec(const char *cmd, const char *log);
void plat_user_screen(void);   /* the screen the last program left, until a key */
void plat_fullpath(const char *in, char *out);  /* an absolute path, DE_PATH long */
/* Give the 16 text colours these R, G, B values (8-bit, colour 0 first);
 * NULL puts back the colours DE found. */
void plat_palette(const unsigned char rgb[48]);
/* When a file was last changed, as seconds (any fixed origin): 0 if it could
 * be read, -1 if not (it is not there). */
int  plat_mtime(const char *path, long *out);

/* ---- memory ------------------------------------------------------------- */
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
int   eq_nocase(const char *a, const char *b);

/* ---- the screen and colours: SCREEN.C ----------------------------------- */
enum {
    A_TEXT, A_KEYWORD, A_TYPE, A_COMMENT, A_STRING, A_NUMBER, A_PREPROC,
    A_SYMBOL, A_SELECT, A_MATCH, A_GUTTER, A_GUTTERCUR, A_FRAME, A_TITLE,
    A_SCROLL, A_MENU, A_MENUHOT, A_MENUSEL, A_MENUSELHOT, A_STATUS,
    A_STATUSKEY, A_DIALOG, A_DIALOGFRAME, A_DIALOGHOT, A_INPUT, A_BUTTON,
    A_BUTTONSEL, A_LIST, A_LISTSEL, A_HELP, A_HELPHEAD, A_ERROR, A_GHOST,
    A_MARKERR, A_MARKWARN, A_COUNT
};
#define NTHEMES 5                  /* Follow DOSDev, then DOSDev's four */
extern unsigned char attr[A_COUNT];
extern int theme;
extern const char *const theme_name[NTHEMES];
extern const char *const theme_id[NTHEMES];   /* as DE.CFG saves them */
void theme_set(int t);
void theme_done(void);             /* before DE ends: the colours DE found */

extern int SR, SC;                 /* screen rows and columns */
extern unsigned short *scr;        /* what's drawn, shown by show() */
void scr_init(void);
void put(int r, int c, int ch, int a);
int  text(int r, int c, const char *s, int a, int w);   /* w < 0: no limit */
int  hot(int r, int c, const char *s, int a, int ha);   /* ~X~ is the hot key */
int  hotlen(const char *s);
int  hotkey(const char *s);
void fill(int r, int c, int h, int w, int ch, int a);
void box(int r, int c, int h, int w, int a, int dbl);
void border(int r, int c, int h, int w, int a, int dbl);   /* just the edges */
void shadow(int r, int c, int h, int w);
void show(int row, int col, int cursor);

/* Box-drawing characters (code page 437). */
#define CH_H   0xC4
#define CH_V   0xB3
#define CH_TL  0xDA
#define CH_TR  0xBF
#define CH_BL  0xC0
#define CH_BR  0xD9
#define CH_HH  0xCD
#define CH_VV  0xBA
#define CH_TLL 0xC9
#define CH_TRR 0xBB
#define CH_BLL 0xC8
#define CH_BRR 0xBC
#define CH_LT  0xC3
#define CH_RT  0xB4
#define CH_TRACK 0xB1
#define CH_THUMB 0xFE
#define CH_UP  0x18
#define CH_DOWN 0x19
#define CH_DOT 0xFA

/* ---- a file being edited: BUFFER.C -------------------------------------- */
typedef struct { char *s; int len, cap; unsigned stamp; } Line;   /* stamp: new for each change */
typedef struct {
    char kind;                 /* 'i' inserted, 'd' deleted */
    int l1, c1, l2, c2;        /* the range: inserted, or deleted from */
    char *text; int n;         /* what was inserted or deleted */
    int group;                 /* undone and redone together */
    int cy, cx;                /* the cursor before */
} Change;
typedef struct Words Words;   /* a file's words and how often each is used: SUGGEST.C */
typedef struct {
    Line *ln; int n, cap;
    char path[DE_PATH];        /* empty: untitled */
    int crlf, final_nl, ctrlz, lang, isnew;
    long disk_size;            /* ... and its size */
    long disk_time;            /* the file's time on disk when loaded or saved; 0: not there then */
    int cy, cx, want, top, left;   /* cursor (line, byte), view */
    int sel, sy, sx;               /* selection: on, and its anchor */
    Change *ch; int nch, capch, upos, saved, group;
    unsigned char *hs; int hcap, hvalid;   /* syntax state per line: right up to hvalid */
    int hknown, hdirty;            /* ... and may still be up to hknown, past the lines changed (to hdirty) */
    Words *words;                  /* for suggestions; made when first wanted */
} Buffer;

Buffer *buf_new(void);
void  buf_free(Buffer *b);
int   buf_load(Buffer *b, const char *path);   /* 0 read, 1 new file, -1 error, -2 too large (DE_MAX_FILE) */
int   buf_save(Buffer *b, const char *path);   /* 0 saved, -1 not, -2 left in .$$$ */
int   buf_modified(const Buffer *b);
int   buf_changed_on_disk(const Buffer *b);    /* has the file changed since it was loaded or saved? */
void  buf_disk_seen(Buffer *b);                /* ... take the file as it is now as known */
int   buf_emergency(const Buffer *b, int index, char *name);   /* write it as NAME.SAV (name: DE_PATH long); 0 written */
void  buf_insert(Buffer *b, int l, int c, const char *t, int n);  /* cursor to its end */
void  buf_delete(Buffer *b, int l1, int c1, int l2, int c2);      /* cursor to its start */
char *buf_text(const Buffer *b, int l1, int c1, int l2, int c2, int *n);
void  buf_group(Buffer *b);
int   buf_undo(Buffer *b);
int   buf_redo(Buffer *b);
int   buf_check(const Buffer *b);
void  buf_state(Buffer *b, int upto);           /* syntax states up to a line */
int   buf_state_check(Buffer *b);               /* for the tests: are they what they'd be made again? */

/* ---- syntax colours: SYNTAX.C ------------------------------------------- */
enum { L_TEXT, L_C, L_ASM, L_PASCAL, L_BASIC, L_BATCH, L_MAKE, L_INI, L_CPP, L_COUNT };
extern const char *const lang_name[L_COUNT];
int  lang_of(const char *path);
int  lang_of_text(const char *path, const char *text, long n);   /* ... and a .H's first 4 KB: C or C++ */
int  lang_family(int lang);                      /* C for C++: languages whose files share names */
int  ext_is(const char *path, const char *list);   /* list: " C H CPP " */
int  syn_line(int lang, const char *s, int n, int state, unsigned char *role);
const char *lang_comment(int lang);
int  lang_lists(int lang, const char *out[4]);   /* its keyword lists, for suggestions */
int  lang_nocase(int lang);                      /* are its words any case? */
int  is_word_char(int lang, int c);

/* ---- the reference: REF.C ------------------------------------------------- */
typedef struct {
    unsigned char lang;        /* L_C, L_CPP, L_ASM, L_PASCAL or L_BASIC */
    const char *name;          /* what's looked up and suggested */
    const char *where;         /* its header, unit or interrupt */
    const char *sig;           /* how it's written */
    const char *desc;          /* what it does */
} Ref;
extern const Ref refs[];
extern const int nrefs;
int  ref_lang(int lang);                         /* the reference a file uses, or -1 */
int  ref_in(int lang, const Ref *r);             /* ... is this entry in it? (C++ has C's too) */
const Ref *ref_find(int lang, const char *name, int n);

/* ---- suggestions: SUGGEST.C ------------------------------------------------ */
typedef struct { char word[40]; int score; const Ref *ref; } Cand;
extern int opt_suggest;
int  sug_collect(Buffer *b, const char *p, int plen, Cand *v, int max);
void sug_update(Buffer *b);                      /* the ghost for what's typed */
void sug_want(Buffer *b);                        /* after typing: the ghost, when next drawn */
void sug_refresh(void);                          /* ... which is now */
void sug_clear(void);
const char *sug_ghost(const Buffer *b, const Ref **ref);   /* the ghost at the cursor */
int  sug_take(Buffer *b);                        /* Tab: take the ghost */
void sug_popup(Buffer *b);                       /* Ctrl+Space: the list */
const Ref *sug_call(const Buffer *b);            /* the call the cursor is in */
int  after_member(const Buffer *b, int start);   /* C++: the word at start follows . or -> */
int  word_prefix(const Buffer *b, int *start);   /* the word ending at the cursor */
/* The word index: BUFFER.C takes a line's words out (-1) before changing
 * it and puts them back (+1) after, so suggestions never read a whole file. */
void words_line(Buffer *b, int l, int delta);
void words_build(Buffer *b);
void words_free(Buffer *b);
int  words_check(Buffer *b);                     /* for the tests: the index matches the text */

/* ---- building, running and messages: PROJECT.C ----------------------------- */
/* A project is the folder with project.ini (DOSDev's) above the file, or
 * the file's own folder. Messages are what the last build said, or what
 * Find in files found: each line, and where it points, if it does. */
enum { M_TEXT, M_ERROR, M_WARNING, M_NOTE, M_FOUND };
typedef struct {
    char *text;                /* as said */
    char *path;                /* the file it points at, absolute; NULL: nowhere */
    int line, col;             /* 0-based line (moved as lines come and go); col 1-based, 0: none */
    int kind;
    Buffer *b;                 /* the open file it's in, if it is */
} Msg;
typedef struct {
    char dir[DE_PATH];         /* the project's folder */
    char name[64];
    char build[160], run[160], output[64];
    char debug[160];           /* project.ini's debug =: Project > Debug */
    const char *build_from, *run_from;  /* where each came from, said in Commands */
} Project;
void proj_find(Buffer *b, Project *p);
void path_join(const char *dir, const char *name, char *out);   /* dir/name; an absolute name as it is */
int  is_dir(const char *path);
void proj_build(int then_run);         /* F9; Ctrl+F9 builds if needed, then runs (2: debugs) */
void proj_debug(void);                 /* Project > Debug: builds if needed, then debugs */
void proj_commands(void);              /* Project > Commands */
int  msg_parse(const char *line, int *kind, char *file, int fsize, int *lno, int *col, const char **text);
void msgs_set(const char *title, const char *dir, const char *log);   /* a build log; NULL title: add to them */
void msgs_add(const char *text, const char *path, int line, int col, int kind);
void msgs_clear(const char *title);
int  msgs_count(int kind);
void msgs_list(void);                  /* the Messages window */
void msgs_next(int dir);               /* F8 and Shift+F8 */
void msgs_attach(Buffer *b);           /* a file was opened */
void msgs_forget(Buffer *b);           /* ... or closed */
void msgs_shift(Buffer *b, int line, int delta);   /* lines after `line` moved */
const Msg *msgs_at(const Buffer *b, int line);     /* the worst message on a line */
void msgs_marks(const Buffer *b, int top, int rows, unsigned char *kinds);
void find_in_files(void);              /* Ctrl+Shift+F */

/* ---- symbols and definitions: SYMBOLS.C ------------------------------------- */
typedef struct { char name[40]; int line; char kind; } Sym;  /* kind: f d t l s v n (namespace) */
int  sym_scan(int lang, const Line *ln, int n, Sym *out, int max);
void sym_list(void);                   /* Ctrl+L */
void sym_definition(void);             /* F12 */

/* ---- the manual and the reference browser: HELP.C ------------------------- */
void help_manual(void);                          /* F1 */
void help_reference(const char *filter);         /* Shift+F1 */
void help_lookup(Buffer *b);                     /* Ctrl+F1 */
void help_entry(const Ref *r);                   /* one entry */
int  has_text(const char *s, const char *f);      /* f in s, any case */

/* ---- the editor: EDITOR.C ------------------------------------------------ */
#define B_CUR (files[cur])     /* the file being edited */
extern Buffer *files[MAXFILES];
extern int nfiles, cur, running, overwrite;
extern int opt_numbers, opt_spaces, opt_indent, opt_tab, opt_auto, opt_fastkeys;
extern char cfg_path[DE_PATH];
void ed_init(void);
int  ed_open(const char *path, int line);
void ed_new(void);
void ed_check_disk(void);      /* after a run: offer to reload each file that changed on disk */
void ed_emergency_save(void);  /* out of memory: every unsaved file is written as NAME.SAV */
void ed_paint(void);           /* draw everything into scr */
int  ed_paint_check(void);     /* for the tests: does it draw the same without its row cache? */
void ed_draw(void);            /* ... and show it */
void ed_key(int k);
void ed_step(void);            /* the main loop, once: draw (unless keys wait), then a key */
int  ed_speed(const char *out[], double ms[], int max);   /* DE /SPEED: timings */
void ed_command(int cmd);
void ed_message(int error, const char *fmt, ...);
void ed_goto_line(int line);
void ed_put_word(int start, const char *w);    /* replace the word before the cursor */
void ed_goto(int line, int col);               /* 1-based; col 0: where the line's text starts */
int  ed_jump(const char *path, int line, int col);   /* open it there, and remember where we were */
void ed_mark(void);                            /* remember where the cursor is, for Back */
void ed_back(void);                            /* Alt+Left */
void ed_forget(Buffer *b);                     /* a file closing: forget places in it */
int  ed_save_all(void);                        /* save every changed file that has a name */
int  ed_same_path(const char *a, const char *b);
int  has_upper(const char *s);                  /* a capital in it? */
const char *ed_word(Buffer *b, char *out, int size);   /* the word at the cursor, or NULL */
int  ed_cursor(int *row, int *col);            /* where the cursor is on the screen */
const char *base_name(const char *path);
int  de_check(const char *file);               /* DE /CHECK FILE: start, stop, write "DE n ok" there */
int  config_load(void);
int  config_save(void);

enum {
    C_NONE, C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_SAVEALL, C_CLOSE, C_EXIT,
    C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_SELALL, C_CUTLINE, C_DUPLINE,
    C_LINEUP, C_LINEDOWN, C_COMMENT, C_INDENT, C_UNINDENT,
    C_FIND, C_FINDNEXT, C_FINDPREV, C_REPLACE, C_GOTO, C_MATCH,
    C_NEXT, C_PREV, C_NUMBERS, C_SPACES, C_INDENTSIZE, C_TABSIZE, C_AUTO,
    C_THEME, C_SAVEOPTS, C_HELP, C_ABOUT, C_SUGGEST, C_COMPLETE, C_REFERENCE,
    C_LOOKUP, C_FASTKEYS, C_BUILD, C_RUN, C_USERSCREEN, C_MESSAGES, C_NEXTMSG,
    C_PREVMSG, C_COMMANDS, C_FINDFILES, C_SYMBOLS, C_DEFINITION, C_BACK, C_DEBUG, C_FILE1
};

/* ---- menus and dialogs: DIALOG.C ----------------------------------------- */
void menu_bar(void);                      /* draws row 0 */
int  menu_run(int which);                 /* returns a command, or C_NONE */
int  menu_at(int col);                    /* which menu is at a column of row 0 */
int  menu_for_key(int k);                 /* Alt+letter: which menu, or -1 */
int  input_box(const char *title, const char *label, char *buf, int size);
int  msg_box(const char *title, const char *text, const char *const *buttons, int nb, int place);
int  open_box(char *path);
/* A list to pick from: `count` makes it for what's typed (filter) and says
 * how long it is; `item` gives line i, and the colour role of its tag. */
typedef struct {
    const char *title, *keys;
    int (*count)(const char *filter);
    void (*item)(int i, char *tag, char *text, int size, int *tagrole);
    const char *(*info)(int i);        /* the status line for the line picked; may be NULL */
    int filter;                        /* does typing filter it? */
} Pick;
int  pick_run(const Pick *p, int sel);    /* the line picked, or -1 */
void busy_box(const char *title, const char *line1, const char *line2);   /* shown, not waited on */
void frame(int r, int c, int h, int w, const char *title);
int  wrap(const char *s, int w, char lines[][72], int max);

#endif
