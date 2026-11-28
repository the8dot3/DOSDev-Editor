/* DE - the manual (F1), the reference list (Shift+F1) and its entries
 * (Ctrl+F1). The manual is DE.TXT, built in as MANUAL.H by make: a line
 * underlined with dashes starts a topic. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "DE.H"
#include "MANUAL.H"

#define MAXTOPICS 40

static int topics[MAXTOPICS], ntopics, nlines;

static int is_rule(const char *s, char ch)
{
    int n = 0;
    for (; *s; s++, n++)
        if (*s != ch)
            return 0;
    return n >= 3;
}

static void index_manual(void)
{
    int i;
    if (nlines)
        return;
    for (i = 0; MANUAL[i]; i++)
        if (MANUAL[i + 1] && is_rule(MANUAL[i + 1], '-') && ntopics < MAXTOPICS)
            topics[ntopics++] = i;
    nlines = i;
}

static int topic_end(int t)
{
    int e = t + 1 < ntopics ? topics[t + 1] : nlines;
    while (e > topics[t] + 2 && !MANUAL[e - 1][0])
        e--;                                    /* no blank lines at the end */
    return e;
}

/* The window the manual and the reference share. */
static void page(const char *title, const char *keys, int *r, int *c, int *w, int *h)
{
    char t[80];
    *w = SC - 4 > 76 ? 76 : SC - 4;
    *h = SR - 2;
    *r = 1;
    *c = (SC - *w) / 2;
    ed_paint();
    box(*r, *c, *h, *w, attr[A_HELPHEAD], 1);
    fill(*r + 1, *c + 1, *h - 2, *w - 2, ' ', attr[A_HELP]);
    snprintf(t, sizeof t, " %s ", title);
    text(*r, *c + (*w - (int)strlen(t)) / 2, t, attr[A_HELPHEAD], *w - 4);
    hot(*r + *h - 1, *c + 2, keys, attr[A_HELPHEAD], attr[A_HELPHEAD]);
    shadow(*r, *c, *h, *w);
}

void help_manual(void)
{
    int topic = -1, sel = 0, top = 0, k, i;
    index_manual();
    for (;;) {
        int r, c, w, h, lh;
        if (topic < 0) {
            /* the contents: the introduction, then the topics */
            int intro_end = ntopics ? topics[0] : nlines, row;
            page("DE manual", " Enter Read   Esc Close ", &r, &c, &w, &h);
            row = r + 2;
            /* the introduction's first paragraph */
            for (i = 3; i < intro_end && MANUAL[i][0] && row < r + h - 3 - ntopics; i++)
                text(row++, c + 2, MANUAL[i], attr[A_HELP], w - 4);
            row++;
            lh = r + h - 1 - row;
            if (sel < top) top = sel;
            if (sel >= top + lh) top = sel - lh + 1;
            for (i = 0; i < lh && top + i < ntopics; i++) {
                char t[80];
                int on = top + i == sel;
                snprintf(t, sizeof t, " %2d  %s ", top + i + 1, MANUAL[topics[top + i]]);
                fill(row + i, c + 2, 1, w - 4, ' ', attr[A_HELP]);
                text(row + i, c + 4, t, attr[on ? A_LISTSEL : A_HELPHEAD], w - 8);
            }
            show(0, 0, 0);
            k = plat_key();
            if (k == DK_ESC || k == DK_F1)
                return;
            if (k == DK_UP && sel > 0) sel--;
            else if (k == DK_DOWN && sel < ntopics - 1) sel++;
            else if (k == DK_HOME) sel = 0;
            else if (k == DK_END) sel = ntopics - 1;
            else if (k == DK_ENTER || k == DK_RIGHT) {
                topic = sel;
                top = 0;
            } else if (k == DK_MOUSE && mouse.event == M_PRESS) {
                int at = mouse.row - row;
                if (at >= 0 && at < lh && top + at < ntopics) {
                    sel = top + at;
                    topic = sel;
                    top = 0;
                } else if (mouse.row < r || mouse.row >= r + h)
                    return;
            }
            if (topic >= 0)
                top = 0;
            continue;
        }
        /* a topic */
        {
            int from = topics[topic] + 2, to = topic_end(topic), n = to - from;
            char title[80];
            snprintf(title, sizeof title, "%s  (%d of %d)", MANUAL[topics[topic]], topic + 1, ntopics);
            page(title, " Esc Contents   \x1b \x1a Topics   PgUp PgDn ", &r, &c, &w, &h);
            lh = h - 3;
            if (top > n - lh) top = n - lh;
            if (top < 0) top = 0;
            while (from + top < to && top == 0 && !MANUAL[from][0])
                from++, n--;
            for (i = 0; i < lh && top + i < n; i++)
                text(r + 1 + i, c + 2, MANUAL[from + top + i], attr[A_HELP], w - 4);
            show(0, 0, 0);
            k = plat_key();
            switch (k) {
            case DK_UP: top--; break;
            case DK_DOWN: top++; break;
            case DK_PGUP: top -= lh - 1; break;
            case DK_PGDN: case ' ': top += lh - 1; break;
            case DK_HOME: top = 0; break;
            case DK_END: top = n; break;
            case DK_LEFT: if (topic > 0) { topic--; top = 0; } break;
            case DK_RIGHT: if (topic < ntopics - 1) { topic++; top = 0; } break;
            case DK_ESC: case DK_BACKSPACE: case DK_F1:
                sel = topic;
                topic = -1;
                top = 0;
                break;
            case DK_MOUSE:
                if (mouse.event == M_PRESS && (mouse.row < r || mouse.row >= r + h)) {
                    sel = topic;
                    topic = -1;
                    top = 0;
                }
                break;
            }
        }
    }
}

/* ---- the reference ------------------------------------------------------------ */

static const char *lang_title(int l)
{
    return l == L_C ? "C" : l == L_CPP ? "C++" : l == L_ASM ? "Assembly" : l == L_PASCAL ? "Pascal" : l == L_BASIC ? "BASIC" : "";
}

void help_entry(const Ref *r0)
{
    char lines[24][72], t[96];
    int r, c, w, h, row, n, i, k;
    for (;;) {
        page(r0->name, " Esc Back ", &r, &c, &w, &h);
        row = r + 2;
        snprintf(t, sizeof t, "%s, %s", lang_title(r0->lang), r0->where);
        text(row, c + 3, t, attr[A_HELPHEAD], w - 6);
        row += 2;
        n = wrap(r0->sig, w - 8, lines, 6);
        for (i = 0; i < n; i++)
            text(row++, c + 5, lines[i], attr[A_HELPHEAD], w - 8);
        row++;
        n = wrap(r0->desc, w - 6, lines, 16);
        for (i = 0; i < n && row < r + h - 2; i++)
            text(row++, c + 3, lines[i], attr[A_HELP], w - 6);
        show(0, 0, 0);
        k = plat_key();
        if (k == DK_ESC || k == DK_ENTER || k == DK_BACKSPACE || (k == DK_MOUSE && mouse.event == M_PRESS))
            return;
    }
}

/* Is f in s, in any case? An empty f is in everything. Ctrl+L uses this too. */
int has_text(const char *s, const char *f)
{
    int n = (int)strlen(f), i, k;
    for (i = 0; s[i]; i++) {
        for (k = 0; k < n && s[i + k]; k++)
            if (tolower((unsigned char)s[i + k]) != tolower((unsigned char)f[k]))
                break;
        if (k == n)
            return 1;
    }
    return n == 0;
}

void help_reference(const char *filter)
{
    static short list[1024];
    char f[32], t[100];
    int sel = 0, top = 0, rl = ref_lang(files[cur]->lang), all = rl < 0, k, i, n;
    snprintf(f, sizeof f, "%s", filter ? filter : "");
    for (;;) {
        int r, c, w, h, lh, row;
        n = 0;
        for (i = 0; i < nrefs && n < 1024; i++)
            if ((all || ref_in(files[cur]->lang, &refs[i])) && (has_text(refs[i].name, f) || has_text(refs[i].where, f)))
                list[n++] = (short)i;
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        snprintf(t, sizeof t, "Reference: %s", all ? "every language" : lang_title(rl));
        page(t, " Type to search   Enter Read   Tab Languages   Esc Close ", &r, &c, &w, &h);
        hot(r + 2, c + 3, "~Find~ ", attr[A_HELP], attr[A_HELPHEAD]);
        fill(r + 2, c + 8, 1, 30, ' ', attr[A_INPUT]);
        text(r + 2, c + 8, f, attr[A_INPUT], 30);
        snprintf(t, sizeof t, "%d found", n);
        text(r + 2, c + 41, t, attr[A_HELP], -1);
        row = r + 4;
        lh = h - 9;
        if (sel < top) top = sel;
        if (sel >= top + lh) top = sel - lh + 1;
        for (i = 0; i < lh && top + i < n; i++) {
            const Ref *x = &refs[list[top + i]];
            int on = top + i == sel;
            snprintf(t, sizeof t, " %-24.24s %s", x->name, x->where);
            fill(row + i, c + 2, 1, w - 4, ' ', attr[on ? A_LISTSEL : A_HELP]);
            text(row + i, c + 2, t, attr[on ? A_LISTSEL : A_HELP], w - 4);
        }
        if (n) {
            char lines[4][72];
            int m = wrap(refs[list[sel]].sig, w - 6, lines, 2), j;
            for (j = 0; j < m; j++)
                text(r + h - 4 + j, c + 3, lines[j], attr[A_HELPHEAD], w - 6);
        } else
            text(r + h - 4, c + 3, "Nothing matches.", attr[A_HELP], -1);
        show(r + 2, c + 8 + (int)strlen(f), 1);
        k = plat_key();
        if (k == DK_ESC)
            return;
        if (k == DK_ENTER && n)
            help_entry(&refs[list[sel]]);
        else if (k == DK_TAB && rl >= 0) {
            all = !all;
            sel = top = 0;
        } else if (k == DK_UP) sel--;
        else if (k == DK_DOWN) sel++;
        else if (k == DK_PGUP) sel -= lh;
        else if (k == DK_PGDN) sel += lh;
        else if (k == DK_BACKSPACE && f[0]) {
            f[strlen(f) - 1] = 0;
            sel = top = 0;
        } else if (k >= 32 && k < 127 && strlen(f) < sizeof f - 1) {
            size_t l = strlen(f);
            f[l] = (char)k;
            f[l + 1] = 0;
            sel = top = 0;
        } else if (k == DK_MOUSE && mouse.event == M_PRESS) {
            int at = mouse.row - row;
            if (at >= 0 && at < lh && top + at < n) {
                sel = top + at;
                help_entry(&refs[list[sel]]);
            } else if (mouse.row < r || mouse.row >= r + h)
                return;
        }
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
    }
}

/* Ctrl+F1: the word at the cursor, in the reference. */
void help_lookup(Buffer *b)
{
    const Line *L = &b->ln[b->cy];
    int s = b->cx, e = b->cx;
    char w[40];
    const Ref *r;
    while (s > 0 && is_word_char(b->lang, (unsigned char)L->s[s - 1]))
        s--;
    while (e < L->len && is_word_char(b->lang, (unsigned char)L->s[e]))
        e++;
    if (e == s || e - s >= (int)sizeof w) {
        help_reference("");
        return;
    }
    memcpy(w, L->s + s, e - s);
    w[e - s] = 0;
    r = ref_find(b->lang, w, e - s);
    if (r)
        help_entry(r);
    else
        help_reference(w);
}
