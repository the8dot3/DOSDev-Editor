/* DE - suggestions. While a word is typed, the likeliest ending shows after
 * the cursor, dimmed (the "ghost"); Tab takes it. Ctrl+Space (or Alt+/)
 * lists every candidate, with what the reference says about it. Inside a
 * call's brackets the status line shows how the function is written.
 *
 * Candidates come from the words in the open files (this one counts most),
 * the language's keywords, and the reference (REF.C). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DE.H"

int opt_suggest = 1;

static char ghost[40];
static const Ref *ghost_ref;
static const Buffer *gb;
static Buffer *wanted;          /* typed into: the ghost is worked out when it's drawn */
static int gl = -1, gc;

void sug_clear(void)
{
    gl = -1;
    ghost_ref = NULL;
    wanted = NULL;
}

/* After typing, the ghost is wanted; it's worked out when the screen is
 * drawn, so keys typed faster than that don't each look for one. */
void sug_want(Buffer *b)
{
    sug_clear();
    wanted = b;
}

void sug_refresh(void)
{
    if (wanted)
        sug_update(wanted);
}

int word_prefix(const Buffer *b, int *start)
{
    const Line *L = &b->ln[b->cy];
    int s = b->cx;
    while (s > 0 && is_word_char(b->lang, (unsigned char)L->s[s - 1]))
        s--;
    *start = s;
    return b->cx - s;
}

static int starts_like(const char *w, int wn, const char *p, int pn)
{
    int i;
    if (wn <= pn)
        return 0;
    for (i = 0; i < pn; i++)
        if (tolower((unsigned char)w[i]) != tolower((unsigned char)p[i]))
            return 0;
    return 1;
}

static unsigned hash(const char *s, int n)
{
    unsigned h = 2166136261u;
    while (n--) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* ---- the word index ------------------------------------------------------------
 * Every word in a file (two letters or more, starting with a letter or _),
 * and how many times it's there. It's kept up to date a line at a time, as
 * the file changes, so a suggestion costs a look at the words that begin
 * like the one being typed, not a read of every file. */

#define WORD_MAX 40             /* longer words aren't suggested */

typedef struct { int off, count, next; unsigned char len; } Entry;
struct Words {
    int lang;
    Entry *e; int n, cap;       /* every word ever seen; a count of 0 is unused */
    int *slot, mask;            /* hash table: entry + 1, 0 empty */
    char *text; int used, room; /* the words' letters */
    int head[27];               /* entries by first letter, any case (a-z, _): + 1 */
};

static int letter_of(int c)
{
    c = tolower((unsigned char)c);
    return c >= 'a' && c <= 'z' ? c - 'a' : 26;
}

static Words *words_new(int lang)
{
    Words *w = xrealloc(NULL, sizeof *w);
    memset(w, 0, sizeof *w);
    w->lang = lang;
    w->mask = 255;
    w->slot = xrealloc(NULL, (w->mask + 1) * sizeof *w->slot);
    memset(w->slot, 0, (w->mask + 1) * sizeof *w->slot);
    return w;
}

static void words_delete(Words *w)
{
    if (!w)
        return;
    free(w->e);
    free(w->slot);
    free(w->text);
    free(w);
}

static int *find_slot(Words *w, const char *s, int n)
{
    unsigned h = hash(s, n);
    for (;; h++) {
        int *slot = &w->slot[h & w->mask];
        const Entry *x;
        if (!*slot)
            return slot;
        x = &w->e[*slot - 1];
        if (x->len == n && !memcmp(w->text + x->off, s, n))
            return slot;
    }
}

static void count_word(Words *w, const char *s, int n, int delta)
{
    int *slot = find_slot(w, s, n);
    Entry *x;
    if (*slot) {
        x = &w->e[*slot - 1];
        x->count += delta;
        if (x->count < 0)
            x->count = 0;       /* can't happen; words_check would say */
        return;
    }
    if (delta <= 0)
        return;
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 256;
        w->e = xrealloc(w->e, w->cap * sizeof *w->e);
    }
    if (w->used + n > w->room) {
        while (w->used + n > w->room)
            w->room = w->room ? w->room * 2 : 4096;
        w->text = xrealloc(w->text, w->room);
    }
    x = &w->e[w->n];
    x->off = w->used;
    x->len = (unsigned char)n;
    x->count = delta;
    memcpy(w->text + w->used, s, n);
    w->used += n;
    x->next = w->head[letter_of(*s)];
    w->head[letter_of(*s)] = w->n + 1;
    *slot = ++w->n;
    if (w->n * 2 > w->mask) {   /* half full: twice the size */
        int i;
        w->mask = w->mask * 2 + 1;
        w->slot = xrealloc(w->slot, (w->mask + 1) * sizeof *w->slot);
        memset(w->slot, 0, (w->mask + 1) * sizeof *w->slot);
        for (i = 0; i < w->n; i++)
            *find_slot(w, w->text + w->e[i].off, w->e[i].len) = i + 1;
    }
}

static void count_line(Words *w, const Line *L, int delta)
{
    int i;
    for (i = 0; i < L->len; i++) {
        int s = i, ch = (unsigned char)L->s[i];
        if (!is_word_char(w->lang, ch))
            continue;
        while (i < L->len && is_word_char(w->lang, (unsigned char)L->s[i]))
            i++;
        if ((isalpha(ch) || ch == '_') && i - s >= 2 && i - s < WORD_MAX)
            count_word(w, L->s + s, i - s, delta);
    }
}

void words_free(Buffer *b)
{
    words_delete(b->words);
    b->words = NULL;
}

void words_build(Buffer *b)
{
    int l;
    if (b->words && b->words->lang == b->lang)
        return;
    words_free(b);
    b->words = words_new(b->lang);
    for (l = 0; l < b->n; l++)
        count_line(b->words, &b->ln[l], 1);
}

void words_line(Buffer *b, int l, int delta)
{
    if (!b->words)
        return;                 /* none yet: made when first wanted */
    if (b->words->lang != b->lang) {
        words_free(b);          /* renamed to another language: made again */
        return;
    }
    count_line(b->words, &b->ln[l], delta);
}

/* For the tests: an index made now from the text says the same. */
int words_check(Buffer *b)
{
    Words *w = b->words, *fresh;
    int i, ok = 1;
    if (!w)
        return 1;
    if (w->lang != b->lang)
        return 0;
    fresh = words_new(b->lang);
    for (i = 0; i < b->n; i++)
        count_line(fresh, &b->ln[i], 1);
    for (i = 0; ok && i < w->n; i++) {
        const Entry *x = &w->e[i];
        int *slot = find_slot(fresh, w->text + x->off, x->len);
        if (x->count != (*slot ? fresh->e[*slot - 1].count : 0))
            ok = 0;
    }
    for (i = 0; ok && i < fresh->n; i++) {
        const Entry *x = &fresh->e[i];
        int *slot = find_slot(w, fresh->text + x->off, x->len);
        if (!*slot || w->e[*slot - 1].count != x->count)
            ok = 0;
    }
    words_delete(fresh);
    return ok;
}

/* ---- candidates -------------------------------------------------------------- */

#define POOL 1024               /* candidates gathered, before the best are kept */

static Cand pool[POOL];
static int npool, pslot[POOL * 2];

static void add(const char *w, int wn, int score, const Ref *ref)
{
    unsigned h;
    if (wn >= WORD_MAX)
        return;
    for (h = hash(w, wn);; h++) {
        int *slot = &pslot[h & (POOL * 2 - 1)];
        Cand *c;
        if (*slot) {
            c = &pool[*slot - 1];
            if ((int)strlen(c->word) == wn && !memcmp(c->word, w, wn)) {
                c->score += score;
                if (ref && !c->ref)
                    c->ref = ref;
                return;
            }
            continue;
        }
        if (npool == POOL)
            return;
        c = &pool[npool];
        memcpy(c->word, w, wn);
        c->word[wn] = 0;
        c->score = score;
        c->ref = ref;
        *slot = ++npool;
        return;
    }
}

static const char *sort_prefix;
static int sort_plen;

static int word_cmp(const void *a, const void *b)
{
    return strcmp(((const Cand *)a)->word, ((const Cand *)b)->word);
}

static int cand_cmp(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    int sx = x->score + (strncmp(x->word, sort_prefix, sort_plen) ? 0 : 1000);
    int sy = y->score + (strncmp(y->word, sort_prefix, sort_plen) ? 0 : 1000);
    if (sx != sy)
        return sy - sx;
    if (strlen(x->word) != strlen(y->word))
        return (int)strlen(x->word) - (int)strlen(y->word);
    return strcmp(x->word, y->word);
}

/* The words in a file that begin like p, each scored by how often it's
 * used. `self` is the word being typed, which doesn't count itself. */
static void from_file(Buffer *f, const char *p, int plen, const char *self, int selfn, int score)
{
    const Words *w;
    int i;
    words_build(f);
    w = f->words;
    for (i = w->head[letter_of(*p)]; i; i = w->e[i - 1].next) {
        const Entry *x = &w->e[i - 1];
        const char *s = w->text + x->off;
        int n = x->count;
        if (n > 0 && starts_like(s, x->len, p, plen)) {
            if (self && x->len == selfn && !memcmp(s, self, selfn))
                n--;
            if (n > 0)
                add(s, x->len, n * score, NULL);
        }
    }
}

int sug_collect(Buffer *b, const char *p, int plen, Cand *v, int max)
{
    const char *lists[4], *self;
    int i, k, nl, s, e, rl = ref_lang(b->lang);
    char pre[WORD_MAX];
    const Line *L = &b->ln[b->cy];
    if (plen < 1 || plen >= (int)sizeof pre)
        return 0;
    memcpy(pre, p, plen);
    pre[plen] = 0;
    npool = 0;
    memset(pslot, 0, sizeof pslot);
    /* the word at the cursor: the one being typed */
    word_prefix(b, &s);
    for (e = s; e < L->len && is_word_char(b->lang, (unsigned char)L->s[e]); e++)
        ;
    self = e > s && (isalpha((unsigned char)L->s[s]) || L->s[s] == '_') ? L->s + s : NULL;
    from_file(b, pre, plen, self, e - s, 4);
    for (k = 0; k < nfiles; k++)
        if (files[k] != b && files[k]->lang == b->lang)
            from_file(files[k], pre, plen, NULL, 0, 2);
    if (rl >= 0)
        for (i = 0; i < nrefs; i++) {
            const Ref *r = &refs[i];
            int rn = (int)strlen(r->name), ok = 1;
            for (k = 0; k < rn; k++)
                if (!is_word_char(b->lang, (unsigned char)r->name[k]))
                    ok = 0;
            if (ok && r->lang == rl && starts_like(r->name, rn, pre, plen))
                add(r->name, rn, 3, r);
        }
    nl = lang_lists(b->lang, lists);
    for (k = 0; k < nl; k++) {
        const char *t = lists[k];
        while (*t) {
            const char *z;
            while (*t == ' ')
                t++;
            for (z = t; *z && *z != ' '; z++)
                ;
            if (z > t && starts_like(t, (int)(z - t), pre, plen))
                add(t, (int)(z - t), 1, NULL);
            t = z;
        }
    }
    /* Words that are any case follow the case being typed: PRI gives PRINT. */
    if (lang_nocase(b->lang)) {
        int upper = 1, any = 0;
        for (i = 0; i < plen; i++)
            if (isalpha((unsigned char)pre[i])) {
                any = 1;
                if (!isupper((unsigned char)pre[i]))
                    upper = 0;
            }
        if (any && upper && plen > 1) {
            for (k = 0; k < npool; k++)
                for (i = 0; pool[k].word[i]; i++)
                    pool[k].word[i] = (char)toupper((unsigned char)pool[k].word[i]);
            /* upper-casing can make two the same: keep one */
            qsort(pool, npool, sizeof *pool, word_cmp);
            for (k = 0, i = 1; i < npool; i++)
                if (!strcmp(pool[k].word, pool[i].word)) {
                    pool[k].score += pool[i].score;
                    /* the reference's first entry for it, as add() keeps */
                    if (!pool[k].ref || (pool[i].ref && pool[i].ref < pool[k].ref))
                        pool[k].ref = pool[i].ref;
                } else
                    pool[++k] = pool[i];
            if (npool)
                npool = k + 1;
        }
    }
    sort_prefix = pre;
    sort_plen = plen;
    qsort(pool, npool, sizeof *pool, cand_cmp);
    if (npool < max)
        max = npool;
    memcpy(v, pool, max * sizeof *v);
    return max;
}

/* Comments and strings get no suggestions. */
static int in_text(Buffer *b, int c)
{
    static unsigned char *role;
    static int cap;
    const Line *L = &b->ln[b->cy];
    if (cap < L->len + 1) {
        cap = L->len + 256;
        role = xrealloc(role, cap);
    }
    buf_state(b, b->cy);
    syn_line(b->lang, L->s, L->len, b->hs[b->cy], role);
    return c < L->len && (role[c] == A_COMMENT || role[c] == A_STRING);
}

void sug_update(Buffer *b)
{
    static Cand v[64];
    const Line *L = &b->ln[b->cy];
    int s, plen = word_prefix(b, &s), n;
    sug_clear();
    if (!opt_suggest || plen < 2 || b->sel)
        return;
    if (!isalpha((unsigned char)L->s[s]) && L->s[s] != '_')
        return;
    if (b->cx < L->len && is_word_char(b->lang, (unsigned char)L->s[b->cx]))
        return;                              /* in the middle of a word */
    if (in_text(b, s))
        return;
    n = sug_collect(b, L->s + s, plen, v, 64);
    if (!n)
        return;
    snprintf(ghost, sizeof ghost, "%s", v[0].word + plen);
    ghost_ref = v[0].ref;
    gb = b;
    gl = b->cy;
    gc = b->cx;
}

const char *sug_ghost(const Buffer *b, const Ref **ref)
{
    if (gl < 0 || b != gb || b->cy != gl || b->cx != gc || !ghost[0])
        return NULL;
    if (ref)
        *ref = ghost_ref;
    return ghost;
}

int sug_take(Buffer *b)
{
    int s, plen;
    char w[80];
    const Line *L;
    if (wanted == b)
        sug_update(b);          /* typed faster than drawn: the ghost it would show */
    if (!sug_ghost(b, NULL))
        return 0;
    L = &b->ln[b->cy];
    plen = word_prefix(b, &s);
    /* the whole word as suggested, in the case it was suggested in */
    {
        static Cand v[64];
        int n = sug_collect(b, L->s + s, plen, v, 64);
        if (n)
            snprintf(w, sizeof w, "%s", v[0].word);
        else
            snprintf(w, sizeof w, "%.*s%s", plen, L->s + s, ghost);
    }
    sug_clear();
    ed_put_word(s, w);
    return 1;
}

/* The call the cursor is inside: printf("x = %d", | shows printf. */
const Ref *sug_call(const Buffer *b)
{
    const Line *L = &b->ln[b->cy];
    int i, depth = 0, quote = 0, e;
    if (ref_lang(b->lang) < 0)
        return NULL;
    for (i = b->cx - 1; i >= 0; i--) {
        char c = L->s[i];
        if (quote) {
            if (c == quote && (i == 0 || L->s[i - 1] != '\\'))
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'')
            quote = c;
        else if (c == ')')
            depth++;
        else if (c == '(') {
            if (depth == 0)
                break;
            depth--;
        }
    }
    if (i < 0)
        return NULL;
    while (i > 0 && L->s[i - 1] == ' ')
        i--;
    e = i;
    while (i > 0 && is_word_char(b->lang, (unsigned char)L->s[i - 1]))
        i--;
    return e > i ? ref_find(b->lang, L->s + i, e - i) : NULL;
}

/* ---- the list (Ctrl+Space) ---------------------------------------------- */

void sug_popup(Buffer *b)
{
    static Cand v[200];
    int sel = 0, top = 0;
    for (;;) {
        const Line *L = &b->ln[b->cy];
        int s, plen = word_prefix(b, &s), n, i, w = 24, h, r, c, row, col, k, lh;
        char t[120];
        if (plen == 0) {
            ed_message(1, "Type the start of a word, then Ctrl-Space lists the ways to finish it.");
            return;
        }
        n = sug_collect(b, L->s + s, plen, v, 200);
        if (n == 0) {
            ed_message(1, "No words begin with %.*s.", plen, L->s + s);
            return;
        }
        if (sel >= n) sel = n - 1;
        lh = n < 10 ? n : 10;
        if (sel < top) top = sel;
        if (sel >= top + lh) top = sel - lh + 1;
        for (i = 0; i < n; i++) {
            int iw = (int)strlen(v[i].word) + (v[i].ref ? 8 : 4);
            if (iw > w)
                w = iw;
        }
        if (w > 44)
            w = 44;
        h = lh + 2;
        ed_paint();
        ed_cursor(&row, &col);
        c = col - plen - 1;
        if (c + w > SC)
            c = SC - w;
        if (c < 0)
            c = 0;
        r = row + 1 + h <= SR - 1 ? row + 1 : row - h;
        box(r, c, h, w, attr[A_LIST], 0);
        for (i = 0; i < lh; i++) {
            const Cand *x = &v[top + i];
            int on = top + i == sel;
            fill(r + 1 + i, c + 1, 1, w - 2, ' ', attr[on ? A_LISTSEL : A_LIST]);
            text(r + 1 + i, c + 2, x->word, attr[on ? A_LISTSEL : A_LIST], w - 8);
            if (x->ref)
                text(r + 1 + i, c + w - 6, x->ref->lang == L_ASM && strchr(x->ref->where, 'h') ? "int" : "ref", attr[on ? A_LISTSEL : A_LIST], 3);
        }
        if (n > lh) {
            snprintf(t, sizeof t, " %d/%d ", sel + 1, n);
            text(r + h - 1, c + w - 1 - (int)strlen(t), t, attr[A_LIST], -1);
        }
        shadow(r, c, h, w);
        /* the selected word's entry, on the status line */
        fill(SR - 1, 0, 1, SC, ' ', attr[A_STATUS]);
        if (v[sel].ref)
            snprintf(t, sizeof t, " %s   %s", v[sel].ref->sig, v[sel].ref->where);
        else
            snprintf(t, sizeof t, " Enter or Tab: %s    Esc: none", v[sel].word);
        text(SR - 1, 0, t, attr[A_STATUS], SC);
        show(row, col, 1);
        k = plat_key();
        switch (k) {
        case DK_UP: if (sel > 0) sel--; break;
        case DK_DOWN: if (sel < n - 1) sel++; break;
        case DK_PGUP: sel -= lh; if (sel < 0) sel = 0; break;
        case DK_PGDN: sel += lh; if (sel > n - 1) sel = n - 1; break;
        case DK_HOME: sel = 0; break;
        case DK_END: sel = n - 1; break;
        case DK_ENTER: case DK_TAB:
            ed_put_word(s, v[sel].word);
            return;
        case DK_ESC:
            return;
        case KM_CTRL | DK_F1:
            if (v[sel].ref)
                help_entry(v[sel].ref);
            break;
        case DK_MOUSE:
            if (mouse.event == M_PRESS) {
                int at = mouse.row - r - 1;
                if (at >= 0 && at < lh && mouse.col > c && mouse.col < c + w - 1) {
                    ed_put_word(s, v[top + at].word);
                    return;
                }
                return;
            }
            break;
        default:
            /* typing goes on: the list follows it; anything else closes it */
            ed_key(k);
            if (!(k == DK_BACKSPACE || (k < 256 && is_word_char(b->lang, k))))
                return;
            sel = top = 0;
            break;
        }
    }
}
