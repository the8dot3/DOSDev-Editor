/* DE - the screen: a buffer of cells drawn into, then shown at once, and
 * the colour themes. A cell is a character and its colour (an attribute:
 * foreground + background * 16), as in the PC's text-mode video memory. */
#include <stdlib.h>
#include <string.h>
#include "DE.H"

int SR, SC;
unsigned short *scr;
unsigned char attr[A_COUNT];
int theme;

const char *const theme_name[NTHEMES] = { "Follow DOSDev", "Borland blue", "Midnight", "Paper", "Solar" };
const char *const theme_id[NTHEMES] = { "follow", "borland", "midnight", "paper", "solar" };

/* What each role is drawn in: one row per kind of palette, in the order of
 * the A_ roles in DE.H. Borland blue is the PC's own colours; a dark
 * palette (Midnight, Solar) takes the same roles but red dialog hot keys,
 * which stay readable on its grays; Paper's windows are the page. */
enum { ROW_BORLAND, ROW_DARK, ROW_LIGHT };
static const unsigned char rows[3][A_COUNT] = {
    /* text  kw    type  comm  str   num   pre   sym   sel   match gut   gutc  frame title scrl */
    { 0x1E, 0x1F, 0x1B, 0x17, 0x1A, 0x1D, 0x1C, 0x1E, 0x30, 0x2F, 0x13, 0x1F, 0x1F, 0x1F, 0x31,
    /* menu  mhot  msel  mshot stat  skey  dlg   dfrm  dhot  input btn   btnsl list  lstsl help  hhead err   ghost merr  mwarn */
      0x70, 0x74, 0x20, 0x24, 0x70, 0x74, 0x70, 0x7F, 0x7E, 0x1F, 0x20, 0x2F, 0x30, 0x2F, 0x30, 0x3F, 0x4F, 0x19, 0x4F, 0x6E },
    { 0x1E, 0x1F, 0x1B, 0x17, 0x1A, 0x1D, 0x1C, 0x1E, 0x30, 0x2F, 0x13, 0x1F, 0x1F, 0x1F, 0x31,
      0x70, 0x74, 0x20, 0x24, 0x70, 0x74, 0x70, 0x7F, 0x74, 0x1F, 0x20, 0x2F, 0x30, 0x2F, 0x30, 0x3F, 0x4F, 0x19, 0x4F, 0x60 },
    { 0x1E, 0x1F, 0x1B, 0x18, 0x1A, 0x1D, 0x1C, 0x1E, 0x30, 0x2F, 0x18, 0x1F, 0x1F, 0x1F, 0x38,
      0x70, 0x74, 0x20, 0x24, 0x70, 0x74, 0x70, 0x7F, 0x74, 0x1F, 0x20, 0x2F, 0x30, 0x2F, 0x30, 0x3F, 0x41, 0x17, 0x41, 0x6E },
};

/* The palettes of DOSDev's themes, as 0xRRGGBB, colour 0 first. They must
 * match DOSDev's own; DOSDev's tests check that, not DE's. */
static const unsigned long palettes[3][16] = {
    { /* MIDNIGHT */ 0x101318, 0x2e3440, 0x8fb573, 0x7f9fc0, 0x9a3540, 0x8e6f8f, 0xa0704a, 0x98a3b6,
      0x4c566a, 0x5e81ac, 0xa3be8c, 0x88c0d0, 0xd08770, 0xb48ead, 0xf3d58f, 0xeceff4 },
    { /* PAPER */ 0x1a1a1a, 0xf4f1e8, 0x7fb069, 0xa9c3dc, 0xc0392b, 0x7a3e9d, 0xc98a2b, 0xb3b0a6,
      0x5f5c55, 0xb8c2cc, 0x2e7d32, 0x1f6f8b, 0xb03a2e, 0x6a3d9a, 0x202020, 0x1f3b70 },
    { /* SOLAR */ 0x00141a, 0x002b36, 0x859900, 0x7f969c, 0xa8201d, 0xd33682, 0xb58900, 0x8c9c9c,
      0x4a5d63, 0x268bd2, 0x9cb300, 0x2aa198, 0xcb4b16, 0x6c71c4, 0xe3b341, 0xfdf6e3 },
};

/* The PC's own colours: following a DOSDev in Borland blue changes nothing. */
static const unsigned char cga[48] = {
    0, 0, 0, 0, 0, 170, 0, 170, 0, 0, 170, 170, 170, 0, 0, 170, 0, 170, 170, 85, 0, 170, 170, 170,
    85, 85, 85, 85, 85, 255, 85, 255, 85, 85, 255, 255, 255, 85, 85, 255, 85, 255, 255, 255, 85, 255, 255, 255,
};

static int pal_on;                     /* DE has set colours of its own */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* DOSDEV_PALETTE, set by DOSDev in DOS: 96 hex digits, R G B of each of
 * the 16 colours. Anything else isn't used. */
static int dosdev_palette(unsigned char rgb[48])
{
    const char *s = getenv("DOSDEV_PALETTE");
    int i;
    if (!s || strlen(s) != 96)
        return 0;
    for (i = 0; i < 48; i++) {
        int h = hexval(s[2 * i]), l = hexval(s[2 * i + 1]);
        if (h < 0 || l < 0)
            return 0;
        rgb[i] = (unsigned char)(h * 16 + l);
    }
    return 1;
}

/* Is the window background (colour 1) light? Brightness squared stands
 * in for the screen's gamma well enough for this. */
static int light(const unsigned char rgb[48])
{
    long r = rgb[3], g = rgb[4], b = rgb[5];
    return 2126 * r * r + 7152 * g * g + 722 * b * b > 5000L * 255 * 255;
}

static void use_palette(const unsigned char *rgb)
{
    if (rgb) {
        plat_palette(rgb);
        pal_on = 1;
    } else if (pal_on) {
        plat_palette(NULL);
        pal_on = 0;
    }
}

void theme_set(int t)
{
    unsigned char rgb[48];
    const unsigned char *pal = NULL;
    int i, row = ROW_BORLAND;
    if (t < 0 || t >= NTHEMES)
        t = 0;
    theme = t;
    if (t == 0) {
        if (dosdev_palette(rgb) && memcmp(rgb, cga, 48))
            pal = rgb;
    } else if (t >= 2) {
        for (i = 0; i < 16; i++) {
            rgb[3 * i] = (unsigned char)(palettes[t - 2][i] >> 16);
            rgb[3 * i + 1] = (unsigned char)(palettes[t - 2][i] >> 8);
            rgb[3 * i + 2] = (unsigned char)palettes[t - 2][i];
        }
        pal = rgb;
    }
    if (pal)
        row = light(pal) ? ROW_LIGHT : ROW_DARK;
    memcpy(attr, rows[row], A_COUNT);
    use_palette(pal);
}

/* DE ends: the colours go back to what DE found. */
void theme_done(void)
{
    use_palette(NULL);
}

void scr_init(void)
{
    SR = plat_rows();
    SC = plat_cols();
    free(scr);
    scr = xrealloc(NULL, SR * SC * sizeof *scr);
    fill(0, 0, SR, SC, ' ', 0x07);
}

void put(int r, int c, int ch, int a)
{
    if (r >= 0 && r < SR && c >= 0 && c < SC)
        scr[r * SC + c] = (unsigned short)((ch & 0xFF) | (a & 0xFF) << 8);
}

int text(int r, int c, const char *s, int a, int w)
{
    int i;
    for (i = 0; s[i] && (w < 0 || i < w); i++)
        put(r, c + i, (unsigned char)s[i], a);
    return i;
}

int hot(int r, int c, const char *s, int a, int ha)
{
    int n = 0, on = 0;
    for (; *s; s++) {
        if (*s == '~') {
            on = !on;
            continue;
        }
        put(r, c + n++, (unsigned char)*s, on ? ha : a);
    }
    return n;
}

int hotlen(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s != '~')
            n++;
    return n;
}

int hotkey(const char *s)
{
    const char *p = strchr(s, '~');
    if (!p || !p[1])
        return 0;
    return p[1] >= 'a' && p[1] <= 'z' ? p[1] - 32 : p[1];
}

void fill(int r, int c, int h, int w, int ch, int a)
{
    unsigned short cell = (unsigned short)((ch & 0xFF) | (a & 0xFF) << 8);
    int i, j;
    if (r < 0) { h += r; r = 0; }
    if (c < 0) { w += c; c = 0; }
    if (r + h > SR) h = SR - r;
    if (c + w > SC) w = SC - c;
    for (i = 0; i < h; i++) {
        unsigned short *p = scr + (r + i) * SC + c;
        for (j = 0; j < w; j++)
            p[j] = cell;
    }
}

void box(int r, int c, int h, int w, int a, int dbl)
{
    fill(r, c, h, w, ' ', a);
    border(r, c, h, w, a, dbl);
}

/* A box's edges only, for when what's inside is drawn anyway. */
void border(int r, int c, int h, int w, int a, int dbl)
{
    int i;
    int hz = dbl ? CH_HH : CH_H, vt = dbl ? CH_VV : CH_V;
    for (i = 1; i < w - 1; i++) {
        put(r, c + i, hz, a);
        put(r + h - 1, c + i, hz, a);
    }
    for (i = 1; i < h - 1; i++) {
        put(r + i, c, vt, a);
        put(r + i, c + w - 1, vt, a);
    }
    put(r, c, dbl ? CH_TLL : CH_TL, a);
    put(r, c + w - 1, dbl ? CH_TRR : CH_TR, a);
    put(r + h - 1, c, dbl ? CH_BLL : CH_BL, a);
    put(r + h - 1, c + w - 1, dbl ? CH_BRR : CH_BR, a);
}

/* The shadow under a window: dim what's beneath, to the right and below. */
void shadow(int r, int c, int h, int w)
{
    int i;
    for (i = 1; i <= h; i++) {
        int k;
        for (k = 0; k < 2; k++) {
            int rr = r + i, cc = c + w + k;
            if (rr < SR && cc < SC && i < h + 1 && rr < r + h + 1)
                scr[rr * SC + cc] = (scr[rr * SC + cc] & 0xFF) | 0x0800;
        }
    }
    for (i = 2; i < w + 2; i++) {
        int rr = r + h, cc = c + i;
        if (rr < SR && cc < SC)
            scr[rr * SC + cc] = (scr[rr * SC + cc] & 0xFF) | 0x0800;
    }
}

void show(int row, int col, int cursor)
{
    plat_show(scr, row, col, cursor);
}
