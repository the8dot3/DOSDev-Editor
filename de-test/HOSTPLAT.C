/* DE tests: a platform for Linux that plays keys from a script and keeps
 * the last screen shown, in place of DOS.C. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "DE.H"

Mouse mouse;
int t_keys[400000], t_nkeys, t_kpos, t_rows = 25, t_cols = 80, t_shift;
unsigned short t_screen[132 * 60], t_snap[132 * 60];
int t_crow, t_ccol, t_cursor, t_extra, t_snapped;
int t_batch, t_shows, t_repeat = -1;   /* keys come faster than drawing; screens shown; key repeat */
long t_ms;
char t_last_exec[512];
int t_execs, t_user_screens;

/* Commands run in the Linux shell here: the tests give shell commands. */
int plat_exec(const char *cmd, const char *log)
{
    char c[1400];
    int st;
    snprintf(t_last_exec, sizeof t_last_exec, "%s", cmd);
    t_execs++;
    snprintf(c, sizeof c, "( %s ) > '%s' 2>&1 < /dev/null", cmd, log ? log : "/dev/null");
    st = system(c);
    return st == -1 ? -1 : WEXITSTATUS(st);
}

void plat_user_screen(void) { t_user_screens++; }

/* The colours DE asked for: every call counted, the last set kept. */
int t_pal_calls, t_pal_set, t_pal_restores;
unsigned char t_pal[48];
void plat_palette(const unsigned char rgb[48])
{
    t_pal_calls++;
    if (rgb) {
        memcpy(t_pal, rgb, 48);
        t_pal_set = 1;
    } else {
        t_pal_set = 0;
        t_pal_restores++;
    }
}

void plat_fullpath(const char *in, char *out)
{
    char t[PATH_MAX], d[PATH_MAX];
    if (realpath(in, t))
        snprintf(out, DE_PATH, "%.250s", t);
    else if (in[0] == '/')
        snprintf(out, DE_PATH, "%s", in);
    else {
        if (!getcwd(d, sizeof d))
            d[0] = 0;
        snprintf(out, DE_PATH, "%.200s/%.50s", d, in);
    }
}

int plat_init(void) { return 0; }
void plat_done(void) {}
int plat_rows(void) { return t_rows; }
int plat_cols(void) { return t_cols; }
int plat_shift(void) { return t_shift; }
void plat_repeat(int fast) { t_repeat = fast; }

/* A clock that moves on 50 ms each time it's read, so timings end soon. */
long plat_ms(void) { return t_ms += 50; }

/* With t_batch, the script's keys are all already waiting, as when keys
 * come faster than the screen is drawn. */
int plat_pending(void) { return t_batch && t_kpos < t_nkeys; }

void plat_show(const unsigned short *cells, int row, int col, int cursor)
{
    memcpy(t_screen, cells, t_rows * t_cols * sizeof *cells);
    t_shows++;
    t_crow = row;
    t_ccol = col;
    t_cursor = cursor;
}

/* A mouse event is DK_MOUSE followed by its event, row and column. */
int plat_key(void)
{
    if (t_kpos < t_nkeys) {
        int k = t_keys[t_kpos++];
        if (k == DK_MOUSE) {
            mouse.event = t_keys[t_kpos++];
            mouse.row = t_keys[t_kpos++];
            mouse.col = t_keys[t_kpos++];
        }
        return k;
    }
    if (!t_extra) {
        /* What was on the screen when the script ran out (a menu or dialog
         * still open), for the tests to look at. */
        memcpy(t_snap, t_screen, sizeof t_snap);
        t_snapped = 1;
    }
    if (++t_extra > 2000) {
        fprintf(stderr, "test: a dialog kept asking for keys after the script ended\n");
        abort();
    }
    return DK_ESC;
}
