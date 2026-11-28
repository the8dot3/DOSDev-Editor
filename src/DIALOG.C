/* DE - menus, dialogs and the file browser. Each runs its own little loop
 * until it has an answer, drawing the editor beneath it. (The help viewer
 * is in HELP.C.) */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "DE.H"

/* ---- menus ----------------------------------------------------------------- */

typedef struct { const char *label; const char *keys; int cmd; } Item;
#define SEP { NULL, NULL, 0 }
#define END { NULL, NULL, -1 }

static const Item m_file[] = {
    { "~N~ew", "Ctrl-N", C_NEW }, { "~O~pen...", "Ctrl-O", C_OPEN }, { "~S~ave", "F2", C_SAVE },
    { "Save ~a~s...", "", C_SAVEAS }, { "Save a~l~l", "", C_SAVEALL }, SEP,
    { "~C~lose", "Ctrl-W", C_CLOSE }, SEP, { "E~x~it", "Alt-X", C_EXIT }, END
};
static const Item m_edit[] = {
    { "~U~ndo", "Ctrl-Z", C_UNDO }, { "~R~edo", "Ctrl-Y", C_REDO }, SEP,
    { "Cu~t~", "Ctrl-X", C_CUT }, { "~C~opy", "Ctrl-C", C_COPY }, { "~P~aste", "Ctrl-V", C_PASTE },
    { "Select ~a~ll", "Ctrl-A", C_SELALL }, SEP,
    { "Cut ~l~ine", "Ctrl-K", C_CUTLINE }, { "~D~uplicate line", "Ctrl-D", C_DUPLINE },
    { "~M~ove line up", "Alt-Up", C_LINEUP }, { "Move line d~o~wn", "Alt-Down", C_LINEDOWN },
    { "Comm~e~nt/uncomment", "Alt-;", C_COMMENT }, { "~I~ndent", "Tab", C_INDENT },
    { "U~n~indent", "Shift-Tab", C_UNINDENT }, SEP, { "Suggest ~w~ords", "Ctrl-Space", C_COMPLETE }, END
};
static const Item m_search[] = {
    { "~F~ind...", "Ctrl-F", C_FIND }, { "Find ~n~ext", "F3", C_FINDNEXT },
    { "Find ~p~revious", "Shift-F3", C_FINDPREV }, { "~R~eplace...", "Ctrl-R", C_REPLACE },
    { "Find ~i~n files...", "Ctrl-Shift-F", C_FINDFILES }, SEP,
    { "~G~o to line...", "Ctrl-G", C_GOTO }, { "~M~atching bracket", "Ctrl-B", C_MATCH },
    { "Go to ~s~ymbol...", "Ctrl-L", C_SYMBOLS }, { "Go to ~d~efinition", "F12", C_DEFINITION },
    { "~B~ack", "Alt-Left", C_BACK }, END
};
static const Item m_project[] = {
    { "~B~uild", "F9", C_BUILD }, { "~R~un", "Ctrl-F9", C_RUN }, { "~D~ebug", "", C_DEBUG }, { "~U~ser screen", "Alt-F5", C_USERSCREEN }, SEP,
    { "~M~essages", "F7", C_MESSAGES }, { "~N~ext message", "F8", C_NEXTMSG },
    { "~P~revious message", "Shift-F8", C_PREVMSG }, SEP, { "~C~ommands...", "", C_COMMANDS }, END
};
static const Item m_window[] = {
    { "~N~ext", "F6", C_NEXT }, { "~P~revious", "Shift-F6", C_PREV }, { "~C~lose", "Ctrl-W", C_CLOSE }, END
};
static const Item m_options[] = {
    { "", "", C_NUMBERS }, { "", "", C_SPACES }, { "", "", C_INDENTSIZE }, { "", "", C_TABSIZE },
    { "", "", C_AUTO }, { "", "", C_SUGGEST }, { "", "", C_THEME }, { "", "", C_FASTKEYS }, SEP,
    { "Sa~v~e options", "", C_SAVEOPTS }, END
};
static const Item m_help[] = {
    { "~M~anual", "F1", C_HELP }, { "~R~eference", "Shift-F1", C_REFERENCE },
    { "~L~ook up the word", "Ctrl-F1", C_LOOKUP }, SEP, { "~A~bout DE", "", C_ABOUT }, END
};
static const struct { const char *name; const Item *items; } menus[] = {
    { "~F~ile", m_file }, { "~E~dit", m_edit }, { "~S~earch", m_search }, { "~P~roject", m_project },
    { "~W~indow", m_window }, { "~O~ptions", m_options }, { "~H~elp", m_help },
};
#define NMENUS 7
#define MAXITEMS 32

static int bar_x[NMENUS], bar_w[NMENUS];
static char dyn[MAXITEMS][48];

/* The items of a menu as they are now: options say their setting, and the
 * Window menu lists the open files. */
static int build(int m, Item *out)
{
    int n = 0, i;
    const Item *it = menus[m].items;
    for (; it->cmd != -1; it++) {
        out[n] = *it;
        if (it->label && !it->label[0]) {
            switch (it->cmd) {
            case C_NUMBERS: sprintf(dyn[n], "[%c] Line ~n~umbers", opt_numbers ? 'x' : ' '); break;
            case C_SPACES: sprintf(dyn[n], "[%c] Tab key inserts ~s~paces", opt_spaces ? 'x' : ' '); break;
            case C_INDENTSIZE: sprintf(dyn[n], "~I~ndent size: %d", opt_indent); break;
            case C_TABSIZE: sprintf(dyn[n], "~T~ab width: %d", opt_tab); break;
            case C_AUTO: sprintf(dyn[n], "[%c] ~A~uto-indent", opt_auto ? 'x' : ' '); break;
            case C_SUGGEST: sprintf(dyn[n], "[%c] Su~g~gestions as you type", opt_suggest ? 'x' : ' '); break;
            case C_THEME: sprintf(dyn[n], "~C~olours: %s", theme_name[theme]); break;
            case C_FASTKEYS: sprintf(dyn[n], "[%c] Fast ~k~ey repeat", opt_fastkeys ? 'x' : ' '); break;
            }
            out[n].label = dyn[n];
        }
        n++;
    }
    if (menus[m].items == m_window) {
        out[n++] = (Item)SEP;
        for (i = 0; i < nfiles && n < MAXITEMS - 1; i++) {
            const Buffer *b = files[i];
            if (i < 9)
                sprintf(dyn[n], "~%d~ %s%.30s", i + 1, buf_modified(b) ? "*" : "", b->path[0] ? base_name(b->path) : "UNTITLED");
            else
                sprintf(dyn[n], "  %s%.30s", buf_modified(b) ? "*" : "", b->path[0] ? base_name(b->path) : "UNTITLED");
            out[n].label = dyn[n];
            out[n].keys = "";
            out[n].cmd = C_FILE1 + i;
            n++;
        }
    }
    return n;
}

void menu_bar(void)
{
    int i, c = 1;
    char t[32];
    fill(0, 0, 1, SC, ' ', attr[A_MENU]);
    for (i = 0; i < NMENUS; i++) {
        bar_x[i] = c;
        bar_w[i] = hotlen(menus[i].name) + 2;
        put(0, c, ' ', attr[A_MENU]);
        hot(0, c + 1, menus[i].name, attr[A_MENU], attr[A_MENUHOT]);
        c += bar_w[i];
    }
    sprintf(t, "DE %s ", DE_VERSION);
    text(0, SC - (int)strlen(t), t, attr[A_MENU], -1);
}

int menu_at(int col)
{
    int i;
    menu_bar();
    for (i = 0; i < NMENUS; i++)
        if (col >= bar_x[i] && col < bar_x[i] + bar_w[i])
            return i;
    return -1;
}

int menu_for_key(int k)
{
    int i, base = KBASE(k);
    if (base >= 'a' && base <= 'z')
        base -= 32;
    for (i = 0; i < NMENUS; i++)
        if (hotkey(menus[i].name) == base)
            return i;
    return -1;
}

int menu_run(int m)
{
    int sel = 0, k, i;
    Item items[MAXITEMS];
    for (;;) {
        int n = build(m, items), w = 20, r = 1, c, h;
        for (i = 0; i < n; i++)
            if (items[i].label) {
                int iw = hotlen(items[i].label) + (int)strlen(items[i].keys) + 7;
                if (iw > w)
                    w = iw;
            }
        if (sel >= n)
            sel = 0;
        while (!items[sel].label)
            sel = (sel + 1) % n;
        h = n + 2;
        ed_paint();
        c = bar_x[m];
        if (c + w + 2 > SC)
            c = SC - w - 2;
        fill(0, bar_x[m], 1, bar_w[m], ' ', attr[A_MENUSEL]);
        hot(0, bar_x[m] + 1, menus[m].name, attr[A_MENUSEL], attr[A_MENUSELHOT]);
        box(r, c, h, w, attr[A_MENU], 0);
        for (i = 0; i < n; i++) {
            int row = r + 1 + i, on = i == sel;
            if (!items[i].label) {
                put(row, c, CH_LT, attr[A_MENU]);
                fill(row, c + 1, 1, w - 2, CH_H, attr[A_MENU]);
                put(row, c + w - 1, CH_RT, attr[A_MENU]);
                continue;
            }
            fill(row, c + 1, 1, w - 2, ' ', attr[on ? A_MENUSEL : A_MENU]);
            hot(row, c + 2, items[i].label, attr[on ? A_MENUSEL : A_MENU], attr[on ? A_MENUSELHOT : A_MENUHOT]);
            text(row, c + w - 2 - (int)strlen(items[i].keys), items[i].keys, attr[on ? A_MENUSEL : A_MENU], -1);
        }
        shadow(r, c, h, w);
        show(0, 0, 0);
        k = plat_key();
        if (k == DK_ESC || k == DK_F10)
            return C_NONE;
        if (k == DK_ENTER)
            return items[sel].cmd;
        if (k == DK_LEFT || k == DK_RIGHT) {
            m = (m + (k == DK_LEFT ? NMENUS - 1 : 1)) % NMENUS;
            sel = 0;
            continue;
        }
        if (k == DK_UP || k == DK_DOWN || k == DK_HOME || k == DK_END) {
            int d = k == DK_UP || k == DK_END ? -1 : 1;
            if (k == DK_HOME) sel = n - 1;
            if (k == DK_END) sel = 0;
            do
                sel = (sel + d + n) % n;
            while (!items[sel].label);
            continue;
        }
        if (k == DK_MOUSE) {
            int row = mouse.row - r - 1;
            if (mouse.row == 0) {
                int mm = menu_at(mouse.col);
                if (mouse.event == M_PRESS && mm == m)
                    return C_NONE;
                if (mm >= 0 && mm != m) {
                    m = mm;
                    sel = 0;
                }
                continue;
            }
            if (mouse.col > c && mouse.col < c + w - 1 && row >= 0 && row < n && items[row].label) {
                sel = row;
                if (mouse.event == M_RELEASE)
                    return items[sel].cmd;      /* press, then let go on it */
                continue;
            }
            if (mouse.event == M_PRESS)
                return C_NONE;
            continue;
        }
        if ((k & KM_ALT) && menu_for_key(k) >= 0) {
            m = menu_for_key(k);
            sel = 0;
            continue;
        }
        if (k >= 32 && k < 127) {
            int ch = toupper(k);
            for (i = 0; i < n; i++)
                if (items[i].label && hotkey(items[i].label) == ch)
                    return items[i].cmd;
        }
    }
}

/* ---- dialog parts ---------------------------------------------------------------- */

void frame(int r, int c, int h, int w, const char *title)
{
    char t[80];
    box(r, c, h, w, attr[A_DIALOGFRAME], 1);
    fill(r + 1, c + 1, h - 2, w - 2, ' ', attr[A_DIALOG]);
    sprintf(t, " %.60s ", title);
    text(r, c + (w - (int)strlen(t)) / 2, t, attr[A_DIALOGFRAME], -1);
    shadow(r, c, h, w);
}

/* A button; returns its width. */
static int button(int r, int c, const char *label, int focus)
{
    char t[40];
    sprintf(t, " %.30s ", label);
    hot(r, c, t, attr[focus ? A_BUTTONSEL : A_BUTTON], attr[focus ? A_BUTTONSEL : A_BUTTON]);
    return hotlen(t);
}

/* ---- a line of text to type ------------------------------------------------------ */

int input_box(const char *title, const char *label, char *buf, int size)
{
    int w = SC - 16 > 64 ? 64 : SC - 16, h = 8, r = (SR - h) / 2, c = (SC - w) / 2;
    int len = (int)strlen(buf), pos = len, fresh = 1, off = 0, fw = w - 6, k, i;
    int okx = c + w / 2 - 11, cax = c + w / 2 + 1;
    for (;;) {
        if (pos < off) off = pos;
        if (pos >= off + fw) off = pos - fw + 1;
        ed_paint();
        frame(r, c, h, w, title);
        text(r + 2, c + 3, label, attr[A_DIALOG], w - 6);
        fill(r + 3, c + 3, 1, fw, ' ', attr[A_INPUT]);
        for (i = 0; i < fw && off + i < len; i++)
            put(r + 3, c + 3 + i, (unsigned char)buf[off + i], attr[fresh && len ? A_SELECT : A_INPUT]);
        button(r + 5, okx, "    OK    ", 1);
        button(r + 5, cax, "  Cancel  ", 0);
        show(r + 3, c + 3 + pos - off, 1);
        k = plat_key();
        if (k == DK_ENTER)
            return 1;
        if (k == DK_ESC)
            return 0;
        if (k == DK_MOUSE) {
            if (mouse.event == M_PRESS && mouse.row == r + 5) {
                if (mouse.col >= okx && mouse.col < okx + 12) return 1;
                if (mouse.col >= cax && mouse.col < cax + 12) return 0;
            }
            if (mouse.event == M_PRESS && mouse.row == r + 3 && mouse.col >= c + 3 && mouse.col < c + 3 + fw) {
                pos = off + mouse.col - c - 3;
                if (pos > len) pos = len;
                fresh = 0;
            }
            continue;
        }
        switch (k) {
        case DK_LEFT: if (pos > 0) pos--; break;
        case DK_RIGHT: if (pos < len) pos++; break;
        case DK_HOME: pos = 0; break;
        case DK_END: pos = len; break;
        case DK_BACKSPACE:
            if (fresh) {
                len = pos = 0;
                buf[0] = 0;
            } else if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
            }
            break;
        case DK_DEL:
            if (fresh) {
                len = pos = 0;
                buf[0] = 0;
            } else if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
            }
            break;
        default:
            if (k >= 32 && k <= 255) {
                if (fresh) {
                    len = pos = 0;
                    buf[0] = 0;
                }
                if (len < size - 1) {
                    memmove(buf + pos + 1, buf + pos, len - pos + 1);
                    buf[pos++] = (char)k;
                    len++;
                }
            }
            break;
        }
        fresh = 0;
    }
}

/* ---- a message and buttons ---------------------------------------------------------- */

/* Words wrapped into lines of at most w characters; '\n' starts a line. */
int wrap(const char *s, int w, char lines[][72], int max)
{
    int n = 0;
    while (*s && n < max) {
        int len = 0, last = -1;
        while (s[len] && s[len] != '\n' && len < w) {
            if (s[len] == ' ')
                last = len;
            len++;
        }
        if (s[len] && s[len] != '\n' && last > 0)
            len = last;
        memcpy(lines[n], s, len);
        lines[n][len] = 0;
        n++;
        s += len;
        if (*s == ' ' || *s == '\n')
            s++;
    }
    return n;
}

int msg_box(const char *title, const char *msgtext, const char *const *buttons, int nb, int place)
{
    char lines[12][72];
    int n = wrap(msgtext, 52, lines, 12), w = 34, h, r, c, i, sel = 0, k, bw = 0, bx[8];
    for (i = 0; i < n; i++)
        if ((int)strlen(lines[i]) + 6 > w)
            w = (int)strlen(lines[i]) + 6;
    for (i = 0; i < nb; i++)
        bw += hotlen(buttons[i]) + 4;
    if (bw + 4 > w)
        w = bw + 4;
    h = n + 6;
    r = place == 1 ? 3 : place == 2 ? SR - h - 2 : (SR - h) / 2;
    c = (SC - w) / 2;
    for (;;) {
        int x = c + (w - bw) / 2 + 1;
        ed_paint();
        frame(r, c, h, w, title);
        for (i = 0; i < n; i++)
            text(r + 2 + i, c + 3, lines[i], attr[A_DIALOG], w - 6);
        for (i = 0; i < nb && i < 8; i++) {
            char t[40];
            sprintf(t, " %s ", buttons[i]);
            bx[i] = x;
            x += button(r + h - 3, x, t, i == sel) + 2;
        }
        show(0, 0, 0);
        k = plat_key();
        if (k == DK_ENTER)
            return sel;
        if (k == DK_ESC)
            return -1;
        if (k == DK_LEFT || k == (KM_SHIFT | DK_TAB))
            sel = (sel + nb - 1) % nb;
        else if (k == DK_RIGHT || k == DK_TAB)
            sel = (sel + 1) % nb;
        else if (k == DK_MOUSE) {
            if (mouse.event == M_PRESS && mouse.row == r + h - 3)
                for (i = 0; i < nb; i++)
                    if (mouse.col >= bx[i] && mouse.col < bx[i] + hotlen(buttons[i]) + 4)
                        return i;
        } else {
            int ch = toupper(KBASE(k));
            for (i = 0; i < nb; i++)
                if (hotkey(buttons[i]) && hotkey(buttons[i]) == ch)
                    return i;
        }
    }
}

/* ---- opening a file ----------------------------------------------------------------- */

typedef struct { char name[64]; int dir; } Entry;

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    const char *p = x->name, *q = y->name;
    if (x->dir != y->dir)
        return y->dir - x->dir;
    if (!strcmp(p, ".."))
        return -1;
    if (!strcmp(q, ".."))
        return 1;
    for (; *p && tolower((unsigned char)*p) == tolower((unsigned char)*q); p++, q++)
        ;
    return tolower((unsigned char)*p) - tolower((unsigned char)*q);
}

/* dir and name as one path, cut off where DE_PATH ends. */
static void join(const char *dir, const char *name, char *out)
{
    size_t n = strlen(dir), m = strlen(name);
    if (n > DE_PATH - 1)
        n = DE_PATH - 1;
    memcpy(out, dir, n);
    if (n < DE_PATH - 1 && (!n || (dir[n - 1] != '/' && dir[n - 1] != '\\')))
        out[n++] = '/';
    if (m > DE_PATH - 1 - n)
        m = DE_PATH - 1 - n;
    memcpy(out + n, name, m);
    out[n + m] = 0;
}

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int list_dir(const char *dir, Entry **out)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    Entry *v = NULL;
    int n = 0, cap = 0;
    char full[DE_PATH];
    size_t dl = strlen(dir);
    int root = (dl == 1 && dir[0] == '/') || (dl == 3 && dir[1] == ':');
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || (root && !strcmp(e->d_name, "..")) || strlen(e->d_name) >= 64)
                continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                v = xrealloc(v, cap * sizeof *v);
            }
            strcpy(v[n].name, e->d_name);
            join(dir, e->d_name, full);
            v[n].dir = !strcmp(e->d_name, "..") || is_dir(full);
            n++;
        }
        closedir(d);
    }
    if (!root && (!n || strcmp(v[0].name, ".."))) {
        int found = 0, i;
        for (i = 0; i < n; i++)
            if (!strcmp(v[i].name, ".."))
                found = 1;
        if (!found) {
            v = xrealloc(v, (n + 1) * sizeof *v);
            strcpy(v[n].name, "..");
            v[n].dir = 1;
            n++;
        }
    }
    if (n)
        qsort(v, n, sizeof *v, entry_cmp);
    *out = v;
    return n;
}

static void parent(char *dir)
{
    char *s = strrchr(dir, '/');
    if (!s)
        return;
    if (s == dir)
        dir[1] = 0;                     /* / */
    else if (s == dir + 2 && dir[1] == ':')
        dir[3] = 0;                     /* d:/ */
    else
        *s = 0;
}

int open_box(char *path)
{
    char dir[DE_PATH], name[DE_PATH] = "", t[DE_PATH + 8];
    Entry *e = NULL;
    int n, sel = 0, top = 0, focus = 0, k, i, pos = 0;
    int w = SC - 12 > 66 ? 66 : SC - 12, h = SR - 4, r = 2, c = (SC - w) / 2, lh = h - 8;
    if (!getcwd(dir, sizeof dir))
        strcpy(dir, ".");
    for (i = 0; dir[i]; i++)
        if (dir[i] == '\\')
            dir[i] = '/';
    n = list_dir(dir, &e);
    for (;;) {
        int bx = c + w / 2 - 11;
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        if (sel < top) top = sel;
        if (sel >= top + lh) top = sel - lh + 1;
        ed_paint();
        frame(r, c, h, w, "Open a file");
        hot(r + 2, c + 3, "~N~ame", attr[A_DIALOG], attr[A_DIALOGHOT]);
        fill(r + 2, c + 9, 1, w - 12, ' ', attr[A_INPUT]);
        text(r + 2, c + 9, name, attr[A_INPUT], w - 12);
        snprintf(t, sizeof t, "In %s", dir);
        text(r + 4, c + 3, t, attr[A_DIALOG], w - 6);
        for (i = 0; i < lh; i++) {
            int at = top + i, on = at == sel;
            fill(r + 5 + i, c + 3, 1, w - 6, ' ', attr[on && focus ? A_LISTSEL : A_LIST]);
            if (at < n) {
                snprintf(t, sizeof t, " %s%s", e[at].name, e[at].dir ? "\\" : "");
                text(r + 5 + i, c + 3, t, attr[on && focus ? A_LISTSEL : on ? A_LISTSEL : A_LIST], w - 6);
            }
        }
        button(r + h - 2, bx, "   Open   ", 1);
        button(r + h - 2, bx + 12, "  Cancel  ", 0);
        if (focus == 0)
            show(r + 2, c + 9 + pos, 1);
        else
            show(0, 0, 0);
        k = plat_key();
        if (k == DK_ESC) {
            free(e);
            return 0;
        }
        if (k == DK_TAB || k == (KM_SHIFT | DK_TAB)) {
            focus = !focus;
            continue;
        }
        if (k == DK_MOUSE) {
            int row = mouse.row - r - 5;
            if (mouse.event != M_PRESS)
                continue;
            if (mouse.row == r + h - 2 && mouse.col >= bx + 12 && mouse.col < bx + 24) {
                free(e);
                return 0;
            }
            if (mouse.row == r + h - 2 && mouse.col >= bx && mouse.col < bx + 12)
                k = DK_ENTER;
            else if (row >= 0 && row < lh && top + row < n && mouse.col > c && mouse.col < c + w - 1) {
                if (focus && sel == top + row)
                    k = DK_ENTER;        /* a second click opens it */
                else {
                    sel = top + row;
                    focus = 1;
                    continue;
                }
            } else if (mouse.row == r + 2) {
                focus = 0;
                continue;
            } else
                continue;
        }
        if (k == DK_ENTER) {
            if (focus == 0 && name[0]) {
                int abs = name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':');
                if (abs)
                    strcpy(t, name);
                else
                    join(dir, name, t);
                if (is_dir(t)) {
                    strcpy(dir, t);
                    name[0] = 0;
                    pos = 0;
                    free(e);
                    n = list_dir(dir, &e);
                    sel = top = 0;
                    continue;
                }
                strcpy(path, t);
                free(e);
                return 1;
            }
            if (n == 0)
                continue;
            if (e[sel].dir) {
                if (!strcmp(e[sel].name, ".."))
                    parent(dir);
                else {
                    join(dir, e[sel].name, t);
                    strcpy(dir, t);
                }
                free(e);
                n = list_dir(dir, &e);
                sel = top = 0;
                continue;
            }
            join(dir, e[sel].name, path);
            free(e);
            return 1;
        }
        if (focus == 1) {
            if (k == DK_UP) sel--;
            else if (k == DK_DOWN) sel++;
            else if (k == DK_PGUP) sel -= lh;
            else if (k == DK_PGDN) sel += lh;
            else if (k == DK_HOME) sel = 0;
            else if (k == DK_END) sel = n - 1;
            else if (k >= 32 && k < 256) {
                focus = 0;              /* typing goes to the name */
                name[0] = 0;
                pos = 0;
            }
            if (focus == 1)
                continue;
        }
        if (k == DK_DOWN || k == DK_UP) {
            focus = 1;
            continue;
        }
        if (k == DK_BACKSPACE && pos > 0) {
            memmove(name + pos - 1, name + pos, strlen(name + pos) + 1);
            pos--;
        } else if (k == DK_LEFT && pos > 0)
            pos--;
        else if (k == DK_RIGHT && name[pos])
            pos++;
        else if (k >= 32 && k < 256 && (int)strlen(name) < w - 13) {
            memmove(name + pos + 1, name + pos, strlen(name + pos) + 1);
            name[pos++] = (char)k;
        }
    }
}

/* ---- a list to pick from ----------------------------------------------------------- */

static void window(const char *title, const char *keys, int *r, int *c, int *w, int *h)
{
    char t[100];
    *w = SC - 4 > 76 ? 76 : SC - 4;
    *h = SR - 2;
    *r = 1;
    *c = (SC - *w) / 2;
    ed_paint();
    box(*r, *c, *h, *w, attr[A_HELPHEAD], 1);
    fill(*r + 1, *c + 1, *h - 2, *w - 2, ' ', attr[A_HELP]);
    snprintf(t, sizeof t, " %.70s ", title);
    text(*r, *c + (*w - (int)strlen(t)) / 2, t, attr[A_HELPHEAD], *w - 4);
    hot(*r + *h - 1, *c + 2, keys, attr[A_HELPHEAD], attr[A_HELPHEAD]);
    shadow(*r, *c, *h, *w);
}

int pick_run(const Pick *p, int sel)
{
    char f[32] = "", tag[16], t[200];
    int top = 0, n = p->count(f), k, i, tags = 0;
    for (;;) {
        int r, c, w, h, lh, row, tw;
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        window(p->title, p->keys, &r, &c, &w, &h);
        row = r + 1;
        if (p->filter) {
            hot(r + 2, c + 3, "~Find~ ", attr[A_HELP], attr[A_HELPHEAD]);
            fill(r + 2, c + 8, 1, 30, ' ', attr[A_INPUT]);
            text(r + 2, c + 8, f, attr[A_INPUT], 30);
            snprintf(t, sizeof t, "%d of them", n);
            text(r + 2, c + 41, t, attr[A_HELP], -1);
            row = r + 4;
        }
        lh = r + h - 1 - row;
        if (sel < top) top = sel;
        if (sel >= top + lh) top = sel - lh + 1;
        if (top < 0) top = 0;
        /* a column for the tags, if any line has one */
        for (i = 0, tags = 0; i < lh && top + i < n; i++) {
            int role;
            p->item(top + i, tag, t, sizeof t, &role);
            if (tag[0] && (int)strlen(tag) + 2 > tags)
                tags = (int)strlen(tag) + 2;
        }
        tw = w - 4 - (tags ? tags + 1 : 0);
        for (i = 0; i < lh && top + i < n; i++) {
            int role, on = top + i == sel, x = c + 2;
            p->item(top + i, tag, t, sizeof t, &role);
            fill(row + i, c + 1, 1, w - 2, ' ', attr[on ? A_LISTSEL : A_HELP]);
            if (tags) {
                if (tag[0]) {
                    fill(row + i, x, 1, tags, ' ', attr[role]);
                    text(row + i, x + 1, tag, attr[role], tags - 1);
                }
                x += tags + 1;
            }
            text(row + i, x, t, attr[on ? A_LISTSEL : A_HELP], tw);
        }
        if (!n)
            text(row, c + 3, p->filter && f[0] ? "Nothing matches." : "There's nothing here.", attr[A_HELP], -1);
        fill(SR - 1, 0, 1, SC, ' ', attr[A_STATUS]);
        if (n && p->info) {
            const char *in = p->info(sel);
            text(SR - 1, 1, in ? in : "", attr[A_STATUS], SC - 2);
        }
        show(r + 2, c + 8 + (int)strlen(f), p->filter);
        k = plat_key();
        if (k == DK_ESC)
            return -1;
        if (k == DK_ENTER)
            return n ? sel : -1;
        if (k == DK_UP) sel--;
        else if (k == DK_DOWN) sel++;
        else if (k == DK_PGUP) sel -= lh;
        else if (k == DK_PGDN) sel += lh;
        else if (k == (KM_CTRL | DK_HOME) || (!p->filter && k == DK_HOME)) sel = 0;
        else if (k == (KM_CTRL | DK_END) || (!p->filter && k == DK_END)) sel = n - 1;
        else if (p->filter && k == DK_BACKSPACE && f[0]) {
            f[strlen(f) - 1] = 0;
            n = p->count(f);
            sel = top = 0;
        } else if (p->filter && k >= 32 && k < 127 && strlen(f) < sizeof f - 1) {
            size_t l = strlen(f);
            f[l] = (char)k;
            f[l + 1] = 0;
            n = p->count(f);
            sel = top = 0;
        } else if (k == DK_MOUSE && mouse.event == M_PRESS) {
            int at = mouse.row - row;
            if (at >= 0 && at < lh && top + at < n && mouse.col > c && mouse.col < c + w - 1)
                return top + at;
            if (mouse.row < r || mouse.row >= r + h || mouse.col < c || mouse.col >= c + w)
                return -1;
        }
    }
}

/* Something is happening that takes a while: say so, and go on. */
void busy_box(const char *title, const char *line1, const char *line2)
{
    int w = SC - 16 > 64 ? 64 : SC - 16, h = 6, r = (SR - h) / 2, c = (SC - w) / 2;
    ed_paint();
    frame(r, c, h, w, title);
    text(r + 2, c + 3, line1, attr[A_DIALOG], w - 6);
    text(r + 3, c + 3, line2 ? line2 : "", attr[A_DIALOG], w - 6);
    show(0, 0, 0);
}
