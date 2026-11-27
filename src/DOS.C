/* DE - the DOS side, for DJGPP: the text screen, the keyboard and the mouse,
 * through the PC BIOS (interrupts 10h and 16h) and the mouse driver (33h).
 * Everything else in DE is plain C.
 *
 * Each BIOS or mouse call from protected mode is a trip to real mode and
 * back, which costs far more than the call itself, under DOSBox-X most of
 * all. So what can be read from the BIOS's own memory is read there (is a
 * key waiting, the Shift keys, the clock), and the screen is written only
 * where it changed. */
#include <dpmi.h>
#include <fcntl.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/exceptn.h>
#include <sys/stat.h>
#include <unistd.h>
#include "DE.H"

Mouse mouse;

#define MAXCELLS (132 * 60)
static unsigned short user[MAXCELLS];  /* the DOS screen: as DE found it, or as the last program left it */
static int user_rows, user_cols, user_ok;
static unsigned short *shown;          /* what's on the screen now */
static int rows, cols, srow, scol, shape = -1, has_mouse, buttons, mrow = -1, mcol = -1;
static int crow = -1, ccol = -1, repeat_set, mode;

/* The BIOS data area, at segment 40h. */
#define BIOS_SHIFT    0x417            /* Shift, Ctrl, Alt ... */
#define BIOS_KBD_HEAD 0x41A            /* the keyboard buffer: the next key */
#define BIOS_KBD_TAIL 0x41C            /* ... and where the next one goes */
#define BIOS_TICKS    0x46C            /* timer ticks, 18.2 a second */
#define BIOS_MODE     0x449            /* the video mode */

static void bios(int vec, __dpmi_regs *r)
{
    r->x.ss = r->x.sp = 0;             /* the DPMI host gives the stack */
    __dpmi_int(vec, r);
}

static int mouse_call(int ax, int *bx, int *cx, int *dx)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = (unsigned short)ax;
    if (bx) r.x.bx = (unsigned short)*bx;
    if (cx) r.x.cx = (unsigned short)*cx;
    if (dx) r.x.dx = (unsigned short)*dx;
    bios(0x33, &r);
    if (bx) *bx = r.x.bx;
    if (cx) *cx = r.x.cx;
    if (dx) *dx = r.x.dx;
    return r.x.ax;
}

static void cursor_shape(int cx)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x01;
    r.x.cx = (unsigned short)cx;
    bios(0x10, &r);
}

static void blink(int on)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x1003;                   /* attribute bit 7: blink, or bright background */
    r.h.bl = (unsigned char)on;
    bios(0x10, &r);
}

/* ---- colours ------------------------------------------------------------------- */
/* The VGA's colour registers (its DAC) behind the 16 text colours: DE's
 * theme while DE has the screen, and what DE found while a program runs
 * and after DE ends. */
static unsigned char pal_dac[16];      /* the DAC register each text colour shows */
static unsigned char pal_found[48];    /* their colours when DE first changed them (6-bit) */
static unsigned char pal_want[48];     /* DE's colours (8-bit), while it has some */
static int pal_known, pal_wanted, pal_shown;

static void pal_learn(void)
{
    __dpmi_regs r;
    int i;
    for (i = 0; i < 16; i++) {
        memset(&r, 0, sizeof r);
        r.x.ax = 0x1007;               /* which DAC register a text colour uses */
        r.h.bl = (unsigned char)i;
        bios(0x10, &r);
        pal_dac[i] = r.h.bh;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x1015;               /* what colour it holds */
        r.x.bx = pal_dac[i];
        bios(0x10, &r);
        pal_found[3 * i] = r.h.dh;
        pal_found[3 * i + 1] = r.h.ch;
        pal_found[3 * i + 2] = r.h.cl;
    }
    pal_known = 1;
}

static void dac_set(int reg, int red, int green, int blue)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x1010;
    r.x.bx = (unsigned short)reg;
    r.h.dh = (unsigned char)red;
    r.h.ch = (unsigned char)green;
    r.h.cl = (unsigned char)blue;
    bios(0x10, &r);
}

/* DE's colours on the screen (on), or the ones it found (off). */
static void pal_show(int on)
{
    int i;
    if (on && pal_wanted) {
        if (!pal_known)
            pal_learn();
        for (i = 0; i < 16; i++)
            dac_set(pal_dac[i], pal_want[3 * i] >> 2, pal_want[3 * i + 1] >> 2, pal_want[3 * i + 2] >> 2);
        pal_shown = 1;
    } else if (pal_shown) {
        for (i = 0; i < 16; i++)
            dac_set(pal_dac[i], pal_found[3 * i], pal_found[3 * i + 1], pal_found[3 * i + 2]);
        pal_shown = 0;
    }
}

void plat_palette(const unsigned char rgb[48])
{
    if (rgb) {
        memcpy(pal_want, rgb, 48);
        pal_wanted = 1;
        pal_show(1);
    } else {
        pal_wanted = 0;
        pal_show(0);
    }
}

/* Keep the DOS screen as it is now, to put back later. */
static void keep_user_screen(void)
{
    int text_mode = _farpeekb(_dos_ds, BIOS_MODE);
    user_rows = ScreenRows();
    user_cols = ScreenCols();
    user_ok = (text_mode <= 3 || text_mode == 7) && user_rows * user_cols <= MAXCELLS;
    if (user_ok)
        ScreenRetrieve(user);
    ScreenGetCursor(&srow, &scol);
}

static void put_user_screen(void)
{
    if (user_ok && user_rows == ScreenRows() && user_cols == ScreenCols())
        ScreenUpdate(user);
    else
        ScreenClear();
    ScreenSetCursor(srow, scol);
}

/* DE's screen, from nothing: after DE starts, and after a program ran. */
static void take_screen(void)
{
    rows = ScreenRows();
    cols = ScreenCols();
    free(shown);
    shown = malloc(rows * cols * 2);   /* what DE never draws: the first show writes it all */
    if (shown)
        memset(shown, 0xFF, rows * cols * 2);
    shape = crow = ccol = -1;
    blink(0);
    pal_show(1);                       /* a program's mode change put the VGA's own colours back */
    has_mouse = (mouse_call(0, NULL, NULL, NULL) & 0xFFFF) == 0xFFFF;
    if (has_mouse)
        mouse_call(1, NULL, NULL, NULL);
    buttons = 0;
}

int plat_init(void)
{
    if (ScreenRows() < 25 || ScreenCols() < 80)
        return 1;
    mode = _farpeekb(_dos_ds, BIOS_MODE);
    keep_user_screen();
    __djgpp_set_ctrl_c(0);             /* Ctrl-C is Copy, not Stop */
    signal(SIGINT, SIG_IGN);
    take_screen();
    return 0;
}

/* Give the screen back to DOS, as it was. */
static void give_screen(void)
{
    if (has_mouse)
        mouse_call(2, NULL, NULL, NULL);
    blink(1);
    pal_show(0);
    cursor_shape(0x0607);
    put_user_screen();
}

void plat_done(void)
{
    give_screen();
    plat_repeat(0);
    free(shown);
    shown = NULL;
}

int plat_rows(void) { return rows; }
int plat_cols(void) { return cols; }

/* Only the rows that changed are written; the mouse pointer is taken off
 * the screen while they are, as the mouse driver asks. */
void plat_show(const unsigned short *cells, int row, int col, int cursor)
{
    int r, hidden = 0, n = cols * 2;
    for (r = 0; r < rows; r++) {
        const unsigned short *line = cells + r * cols;
        if (shown && !memcmp(shown + r * cols, line, n))
            continue;
        if (has_mouse && !hidden) {
            mouse_call(2, NULL, NULL, NULL);
            hidden = 1;
        }
        ScreenUpdateLine(line, r);
        if (shown)
            memcpy(shown + r * cols, line, n);
    }
    if (hidden)
        mouse_call(1, NULL, NULL, NULL);
    if (cursor != shape) {
        cursor_shape(cursor == 0 ? 0x2000 : cursor == 2 ? 0x0007 : 0x0607);
        shape = cursor;
    }
    if (cursor && (row != crow || col != ccol)) {
        ScreenSetCursor(row, col);
        crow = row;
        ccol = col;
    }
}

/* What INT 16h AH=12h would say about Shift and Ctrl, read straight from
 * the BIOS's memory. */
static int shift_flags(void)
{
    return _farpeekb(_dos_ds, BIOS_SHIFT);
}

static int key_waiting(void)
{
    return _farpeekw(_dos_ds, BIOS_KBD_HEAD) != _farpeekw(_dos_ds, BIOS_KBD_TAIL);
}

long plat_ms(void)
{
    return (long)(_farpeekl(_dos_ds, BIOS_TICKS) * 54.9254);
}

/* Fast: a key held down repeats after a quarter of a second, 30 times a
 * second. Off: half a second, as DOSBox-X starts. (INT 16h AX=0305h.) */
void plat_repeat(int fast)
{
    __dpmi_regs r;
    if (!fast && !repeat_set)
        return;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0305;
    r.h.bh = fast ? 0 : 1;             /* delay: 0 = 250 ms, 1 = 500 ms */
    r.h.bl = 0;                        /* rate: 0 = 30 a second */
    bios(0x16, &r);
    repeat_set = fast;
}

int plat_shift(void)
{
    return (shift_flags() & 3) != 0;
}

/* The Alt key's letters, by scan code. */
static int alt_letter(int scan)
{
    static const char row1[] = "QWERTYUIOP", row2[] = "ASDFGHJKL", row3[] = "ZXCVBNM", digits[] = "1234567890";
    if (scan >= 0x10 && scan <= 0x19) return row1[scan - 0x10];
    if (scan >= 0x1E && scan <= 0x26) return row2[scan - 0x1E];
    if (scan >= 0x2C && scan <= 0x32) return row3[scan - 0x2C];
    if (scan >= 0x78 && scan <= 0x81) return digits[scan - 0x78];
    switch (scan) {
    case 0x27: return ';';
    case 0x28: return '\'';
    case 0x33: return ',';
    case 0x34: return '.';
    case 0x35: return '/';
    case 0x82: return '-';
    case 0x83: return '=';
    }
    return 0;
}

/* A BIOS keystroke (scan code << 8 | character) as one of DE's keys. */
static int translate(int ax)
{
    int scan = (ax >> 8) & 0xFF, ch = ax & 0xFF, f = shift_flags();
    int shift = (f & 3) ? KM_SHIFT : 0;
    if (ch == 0 || (ch == 0xE0 && scan != 0)) {
        int a;
        switch (scan) {
        case 0x48: return DK_UP | shift;
        case 0x50: return DK_DOWN | shift;
        case 0x4B: return DK_LEFT | shift;
        case 0x4D: return DK_RIGHT | shift;
        case 0x47: return DK_HOME | shift;
        case 0x4F: return DK_END | shift;
        case 0x49: return DK_PGUP | shift;
        case 0x51: return DK_PGDN | shift;
        case 0x52: return DK_INS | shift;
        case 0x53: return DK_DEL | shift;
        case 0x8D: return KM_CTRL | DK_UP | shift;
        case 0x91: return KM_CTRL | DK_DOWN | shift;
        case 0x73: return KM_CTRL | DK_LEFT | shift;
        case 0x74: return KM_CTRL | DK_RIGHT | shift;
        case 0x77: return KM_CTRL | DK_HOME | shift;
        case 0x75: return KM_CTRL | DK_END | shift;
        case 0x84: return KM_CTRL | DK_PGUP | shift;
        case 0x76: return KM_CTRL | DK_PGDN | shift;
        case 0x92: return KM_CTRL | DK_INS;
        case 0x93: return KM_CTRL | DK_DEL;
        case 0x98: return KM_ALT | DK_UP;
        case 0xA0: return KM_ALT | DK_DOWN;
        case 0x9B: return KM_ALT | DK_LEFT;
        case 0x9D: return KM_ALT | DK_RIGHT;
        case 0x0F: return KM_SHIFT | DK_TAB;
        case 0x94: return KM_CTRL | DK_TAB;
        case 0x0E: return KM_ALT | DK_BACKSPACE;
        case 0x1C: case 0xA6: return KM_ALT | DK_ENTER;
        case 0x85: return DK_F11;
        case 0x86: return DK_F12;
        case 0x87: return KM_SHIFT | DK_F11;
        case 0x88: return KM_SHIFT | DK_F12;
        case 0x89: return KM_CTRL | DK_F11;
        case 0x8A: return KM_CTRL | DK_F12;
        case 0x8B: return KM_ALT | DK_F11;
        case 0x8C: return KM_ALT | DK_F12;
        }
        if (scan >= 0x3B && scan <= 0x44) return DK_F1 + scan - 0x3B;
        if (scan >= 0x54 && scan <= 0x5D) return KM_SHIFT | (DK_F1 + scan - 0x54);
        if (scan >= 0x5E && scan <= 0x67) return KM_CTRL | (DK_F1 + scan - 0x5E);
        if (scan >= 0x68 && scan <= 0x71) return KM_ALT | (DK_F1 + scan - 0x68);
        a = alt_letter(scan);
        return a ? KM_ALT | a : DK_NONE;
    }
    if (ch == 8 && scan == 0x0E) return DK_BACKSPACE;
    if (ch == 0x7F && scan == 0x0E) return KM_CTRL | DK_BACKSPACE;
    if (ch == 9 && scan == 0x0F) return DK_TAB;
    if (ch == 13 && (scan == 0x1C || scan == 0xE0)) return DK_ENTER;
    if (ch == 10 && (scan == 0x1C || scan == 0xE0)) return KM_CTRL | DK_ENTER;
    if (ch == 27 && scan == 0x01) return DK_ESC;
    if (ch < 32) return KM_CTRL | ('@' + ch) | shift;     /* Ctrl-A is 1 */
    if (ch == ' ' && (f & 4)) return KM_CTRL | ' ';         /* Ctrl-Space */
    return ch;
}

/* Is a key waiting that DE will act on? The main loop draws only when
 * none is, so keys that come faster than the screen can be drawn are all
 * taken first, and holding a key down never runs on after it's let go. */
int plat_pending(void)
{
    unsigned head = _farpeekw(_dos_ds, BIOS_KBD_HEAD), k;
    if (head == _farpeekw(_dos_ds, BIOS_KBD_TAIL))
        return 0;
    k = _farpeekw(_dos_ds, 0x400 + head);
    if ((k & 0xFF) == 0xF0 && (k >> 8))
        k &= 0xFF00;                   /* as INT 16h AH=10h gives it */
    return translate(k) != DK_NONE;
}

int plat_key(void)
{
    for (;;) {
        __dpmi_regs r;
        if (key_waiting()) {
            int k;
            memset(&r, 0, sizeof r);
            r.h.ah = 0x10;             /* read it, extended keys too */
            bios(0x16, &r);
            k = translate(r.x.ax);
            if (k != DK_NONE)
                return k;
            continue;
        }
        if (has_mouse) {
            int b = 0, x = 0, y = 0, row, col;
            mouse_call(3, &b, &x, &y);
            row = y / 8;
            col = x / 8;
            if ((b & 1) != (buttons & 1)) {
                buttons = b;
                mrow = row;
                mcol = col;
                mouse.row = row;
                mouse.col = col;
                mouse.event = (b & 1) ? M_PRESS : M_RELEASE;
                return DK_MOUSE;
            }
            if ((b & 1) && (row != mrow || col != mcol)) {
                mrow = row;
                mcol = col;
                mouse.row = row;
                mouse.col = col;
                mouse.event = M_DRAG;
                return DK_MOUSE;
            }
        }
        __dpmi_yield();                /* let DOSBox-X rest while nothing happens */
    }
}

/* ---- running programs ----------------------------------------------------------- */

static void wait_key(void)
{
    __dpmi_regs r;
    while (key_waiting()) {            /* what was typed for the program isn't for this */
        memset(&r, 0, sizeof r);
        r.h.ah = 0x10;
        bios(0x16, &r);
    }
    memset(&r, 0, sizeof r);
    r.h.ah = 0x10;
    bios(0x16, &r);
}

int plat_exec(const char *cmd, const char *log)
{
    int st, fast = repeat_set;
    if (log) {
        /* what it says, both ways it says it, into the log; the screen stays DE's */
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR), o1, o2;
        if (fd < 0)
            return -1;
        fflush(stdout);
        fflush(stderr);
        o1 = dup(1);
        o2 = dup(2);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        st = system(cmd);
        fflush(stdout);
        fflush(stderr);
        dup2(o1, 1);
        dup2(o2, 2);
        close(o1);
        close(o2);
        if (shown)
            memset(shown, 0xFF, rows * cols * 2);   /* it may have written on the screen itself */
        return st;
    }
    /* the program gets the screen, as DOS had it */
    give_screen();
    st = system(cmd);
    keep_user_screen();                /* for Alt+F5 */
    if (_farpeekb(_dos_ds, BIOS_MODE) != mode) {
        __dpmi_regs r;                 /* it changed the video mode: back to DE's */
        memset(&r, 0, sizeof r);
        r.h.al = (unsigned char)mode;
        bios(0x10, &r);
    }
    fputs("\r\nPress a key to go back to DE.", stdout);
    fflush(stdout);
    wait_key();
    take_screen();
    if (fast)
        plat_repeat(1);                /* the program may have changed it */
    return st;
}

void plat_user_screen(void)
{
    give_screen();
    wait_key();
    take_screen();
}

int plat_mtime(const char *path, long *out)
{
    struct stat st;
    if (stat(path, &st))
        return -1;
    *out = (long)st.st_mtime;
    return 0;
}

void plat_fullpath(const char *in, char *out)
{
    _fixpath(in, out);
}
