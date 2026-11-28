# A tour of DE, the DOSDev Editor

This is a guided reading of DE's source code. It is meant to be read beside
the source, one chapter at a time. It explains what each part of the program
does, how it does it, and why it is done that way.

Everything here was checked against the code at commit `5f6fe6a`, which is
the released DE 0.6. References look like `EDITOR.C:1865`: a file in `src/`
and a line number. If the code changes later, the line numbers drift, but the
function names stay useful: search for the name.

A note on words. When a C or DOS term is used for the first time, it is
explained briefly. The [glossary](#11-glossary) at the end explains each one
again, in two sentences.

## Contents

1. [The map](#1-the-map)
2. [The journey of one typed character](#2-the-journey-of-one-typed-character)
3. [Only DOS.C knows about DOS](#3-only-dosc-knows-about-dos)
4. [The screen: cells, colour roles and themes](#4-the-screen-cells-colour-roles-and-themes)
5. [How a file is stored and edited](#5-how-a-file-is-stored-and-edited)
6. [Syntax colouring: a small state machine](#6-syntax-colouring-a-small-state-machine)
7. [Drawing the editor window](#7-drawing-the-editor-window)
8. [The other parts](#8-the-other-parts)
   - 8.1 [Starting and stopping: MAIN.C](#81-starting-and-stopping-mainc)
   - 8.2 [Menus and dialogs](#82-menus-and-dialogs)
   - 8.3 [Suggestions and the built-in reference](#83-suggestions-and-the-built-in-reference)
   - 8.4 [Symbols and Go to definition](#84-symbols-and-go-to-definition)
   - 8.5 [Projects, building, running and compiler messages](#85-projects-building-running-and-compiler-messages)
   - 8.6 [Running other programs and coming back](#86-running-other-programs-and-coming-back)
   - 8.7 [The help viewer, and how the manual gets inside](#87-the-help-viewer-and-how-the-manual-gets-inside)
   - 8.8 [When memory runs out](#88-when-memory-runs-out)
9. [How the tests work, and how to add one](#9-how-the-tests-work-and-how-to-add-one)
10. [How to change it: exercises](#10-how-to-change-it-exercises)
11. [Glossary](#11-glossary)
12. [Rough edges](#12-rough-edges)

---

## 1. The map

### What each file is for

| File | Lines | Size | What it is responsible for |
|------|------:|-----:|----------------------------|
| `src/DE.H` | 326 | 17 KB | Every type, constant and function that more than one file shares. It is the program's table of contents. |
| `src/MAIN.C` | 193 | 6 KB | Starting and stopping: the command line, `DE /CHECK`, `DE /SPEED`, the options file `DE.CFG`, and the main loop. |
| `src/EDITOR.C` | 1,976 | 58 KB | The editor itself: the open files, what every key does, the clipboard, find and replace, and drawing the editor window. |
| `src/BUFFER.C` | 714 | 19 KB | One file in memory: its array of lines, the two basic changes (insert and delete), undo and redo, loading and saving, and running out of memory. |
| `src/SYNTAX.C` | 717 | 24 KB | Syntax colours: which language a file is in, and a colour role for every byte of a line, with a state carried from one line to the next. |
| `src/SCREEN.C` | 241 | 8 KB | The screen as an array of cells, simple drawing (text, boxes, shadows), the colour roles and the five themes. |
| `src/DOS.C` | 507 | 15 KB | The only file that knows it runs on DOS: the BIOS screen and keyboard, the mouse, the VGA colours, running other programs. |
| `src/DIALOG.C` | 758 | 26 KB | Menus, input boxes, message boxes, the Open dialog, and a general "pick a line from a list" window. |
| `src/SUGGEST.C` | 606 | 18 KB | Suggestions while typing (the dimmed "ghost"), the Ctrl+Space list, each file's word index, and "which call is the cursor in". |
| `src/REF.C` | 426 | 50 KB | The built-in reference: 373 one-line entries for C, SDL3, PDCurses, DOS, assembly, Pascal and BASIC, and two functions to look them up. |
| `src/SYMBOLS.C` | 644 | 21 KB | Finding functions, labels and types from the shape of lines; Go to symbol (Ctrl+L) and Go to definition (F12). |
| `src/PROJECT.C` | 1,013 | 30 KB | Projects (`project.ini`, `BUILD.BAT`), building and running, reading compiler messages, the Messages window, Find in files. |
| `src/HELP.C` | 288 | 10 KB | The manual viewer (F1), the reference list (Shift+F1) and one reference entry (Ctrl+F1). |
| `src/MANUAL.H` | 430 | 21 KB | `DE.TXT` turned into a C array of strings by `make`. It is generated: change `DE.TXT`, not this file. |

The source is about 8,840 lines; 8,409 without `MANUAL.H`.

Around the source:

| File | What it is |
|------|------------|
| `test/DETEST.C` (1,516 lines) | The tests: scripted keys played into the real editor code, on Linux. |
| `test/HOSTPLAT.C` (119 lines) | A stand-in for `DOS.C` that the tests use: keys come from a script, the screen goes into an array. |
| `test/run.sh` (12 lines) | Compiles and runs the tests (`make test`). |
| `test/stubs/` | Stand-ins for DJGPP's headers, so `DOS.C` can at least be type-checked on Linux. |
| `Makefile`, `BUILD.BAT` | The build on Linux (with a DJGPP cross compiler) and in DOS. |
| `DE.TXT` | The manual. It is also built into DE (F1). |
| `CONTRACT.md`, `CHANGELOG.md` | How DE and DOSDev work together; what changed in each version and why. |

### The order to read them in

1. **`DE.H`**. Read all of it once. Every later file uses its names.
2. **`MAIN.C`**. Short. It shows the order in which things start, and the
   main loop (two lines).
3. **`SCREEN.C`**. Short. The screen array and the colours. Everything that
   draws uses it.
4. **`BUFFER.C`**. The heart of the data: how text is held and changed.
5. **`EDITOR.C`**. The longest file. Read it in this order: `ed_key`
   (`EDITOR.C:1556`), then `move` (`EDITOR.C:586`), `type_char`
   (`EDITOR.C:724`), then the drawing code from `draw_line`
   (`EDITOR.C:1620`) to `ed_draw` (`EDITOR.C:1872`). The rest is commands
   built from the same pieces.
6. **`SYNTAX.C`**. Start with `syn_line` (`SYNTAX.C:669`) and `syn_c`
   (`SYNTAX.C:197`).
7. **`DOS.C`**. Now that you know what the rest asks of it.
8. **`DIALOG.C`**, then **`SUGGEST.C`** with **`REF.C`**, **`SYMBOLS.C`**,
   **`PROJECT.C`**, **`HELP.C`**: the features, in any order.
9. **`test/HOSTPLAT.C`** and **`test/DETEST.C`**: how all of it is tested.

### How the parts call each other

```
                  MAIN.C   start, DE.CFG, the main loop
                     |
                  EDITOR.C  keys -> commands -> edits; draws the window
                     |
   +--------+--------+---------+----------+--------+
   |        |        |         |          |        |
DIALOG.C SUGGEST.C SYMBOLS.C PROJECT.C  HELP.C   (the features)
            |  REF.C                    MANUAL.H
   +--------+--------+---------+----------+
                     |
          BUFFER.C   lines, undo, files      SYNTAX.C   colour roles
                     |
                  SCREEN.C  cells, colours, themes
                     |
                  DOS.C     BIOS, VGA, mouse, running programs
```

The lines show the usual direction of calls. A few calls go the other way.
For example, `BUFFER.C` tells `SUGGEST.C` (`words_line`) and `PROJECT.C`
(`msgs_shift`) whenever lines change, and the dialogs call `ed_paint` in
`EDITOR.C` to draw the editor beneath them.

---

## 2. The journey of one typed character

Follow the letter `a` from the keyboard to the screen. It passes through six
files.

```
 key pressed
     |
 DOS.C     plat_key()      BIOS INT 16h -> translate() -> 'a'
     |
 MAIN.C    main()          while (running) ed_step();
     |
 EDITOR.C  ed_step()       ed_key('a') -> type_char()
     |
 BUFFER.C  buf_insert()    raw_insert(): the rest of the line moves right by one
     |
 EDITOR.C  ed_draw()       ed_paint() -> cached_line() -> draw_line()
     |
 SYNTAX.C  syn_line()      a colour role for every byte of the line
     |
 SCREEN.C  attr[] and scr[]   role -> attribute -> cell
     |
 DOS.C     plat_show()     only the changed rows go to the screen
```

### Step 1: the keyboard (DOS.C)

The BIOS (the PC's built-in firmware) keeps typed keys in a small buffer.
`plat_key` waits until a key is there, then asks the BIOS for it with
interrupt 16h, function 10h. An *interrupt* here is a numbered entry point
into the BIOS.

`src/DOS.C:387-400`
```c
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
```

The BIOS answers with a 16-bit number: the key's *scan code* (which physical
key) in the high byte, and its character in the low byte. `translate`
(`DOS.C:311`) turns that into one of DE's own key numbers. For a plain
letter, the character is the answer:

`src/DOS.C:368-370`
```c
    if (ch < 32) return KM_CTRL | ('@' + ch) | shift;     /* Ctrl-A is 1 */
    if (ch == ' ' && (f & 4)) return KM_CTRL | ' ';         /* Ctrl-Space */
    return ch;
```

So `a` becomes the number 97. Chapter 3 explains the key numbers.

### Step 2: the main loop (MAIN.C, EDITOR.C)

`main` ends in a loop that runs until the user leaves DE:

`src/MAIN.C:187-188`
```c
    while (running)
        ed_step();
```

`ed_step` is one turn of the loop:

`src/EDITOR.C:1865-1870`
```c
void ed_step(void)
{
    if (!plat_pending())
        ed_draw();
    ed_key(plat_key());
}
```

Notice the order. DE draws first, and only if no key is already waiting;
then it waits for a key and acts on it. If you type faster than DE can draw
(possible under DOSBox-X on a slow host), every waiting key is handled
before the screen is drawn again. That is why holding a key down never "runs
on" after you let go.

### Step 3: what the key means (EDITOR.C)

`ed_key` (`EDITOR.C:1556`) tries the meanings of a key in order: Tab taking
a suggestion, the mouse, F10, Alt+digit, the shortcut table, Alt+letter for
a menu, the moving keys, and the editing keys. A plain character falls
through to the end:

`src/EDITOR.C:1607-1612`
```c
    default:
        if (k >= 32 && k <= 255)
            type_char(b, k);
        break;
    }
    follow(b);
```

`type_char` prepares the change and makes it:

`src/EDITOR.C:724-732`
```c
static void type_char(Buffer *b, int ch)
{
    char c = (char)ch;
    Line *L;
    edit_begin(b, 1);
    delete_sel(b);
    L = &b->ln[b->cy];
    if (ch == ' ' && b->cx > 0 && L->s[b->cx - 1] != ' ')
        buf_group(b);            /* a word at a time */
```

`edit_begin` decides whether this key starts a new undo step (chapter 5).
`delete_sel` removes any selected text first, because typing replaces a
selection. A space after a word starts a new undo group, so undo takes back
a word at a time. Then:

`src/EDITOR.C:745-751`
```c
    if (overwrite && b->cx < L->len)
        buf_delete(b, b->cy, b->cx, b->cy, b->cx + 1);
    buf_insert(b, b->cy, b->cx, &c, 1);
    edit_end(b);
    if (is_word_char(b->lang, ch))
        sug_want(b);
}
```

In overwrite mode the character under the cursor is deleted first. Then one
byte is inserted at the cursor. `sug_want` notes that a suggestion should be
worked out the next time the screen is drawn (section 8.3).

### Step 4: the buffer (BUFFER.C)

`buf_insert` (`BUFFER.C:301`) records the change for undo and makes it. If
the last change was typing that ended exactly here, the new letter joins it:

`src/BUFFER.C:308-321`
```c
    /* Typing joins the previous insert, so one undo takes back a word. */
    if (b->upos == b->nch && b->nch > 0 && b->saved != b->upos && !memchr(t, '\n', n)) {
        ch = &b->ch[b->nch - 1];
        if (ch->kind == 'i' && ch->group == b->group && ch->l2 == l && ch->c2 == c) {
            raw_insert(b, l, c, t, n, &el, &ec);
            ch->text = xrealloc(ch->text, ch->n + n);
            memcpy(ch->text + ch->n, t, n);
            ch->n += n;
            ch->c2 = ec;
            b->cy = el;
            b->cx = ec;
            return;
        }
    }
```

The real work is in `raw_insert` (`BUFFER.C:161`). For text with no line
break it is a `memmove`: the end of the line moves one byte to the right and
the letter goes into the gap.

`src/BUFFER.C:171-183`
```c
    touched(b, l, nl > 0);
    words_line(b, l, -1);
    restamp(L);
    if (!nl) {
        line_reserve(L, L->len + n);
        memmove(L->s + c + n, L->s + c, L->len - c + 1);
        memcpy(L->s + c, t, n);
        L->len += n;
        *el = l;
        *ec = c + n;
        words_line(b, l, 1);
        return;
    }
```

Four other parts of DE hear about the change here:

- `touched` marks the syntax states after this line as possibly stale
  (chapter 6).
- `words_line(b, l, -1)` takes the line's words out of the word index
  before the change, and `words_line(b, l, 1)` puts them back after
  (section 8.3).
- `restamp` gives the line a new stamp, so a cached drawing of it is known
  to be out of date (chapter 7).
- For changes with line breaks, `msgs_shift` (`BUFFER.C:220`) moves
  compiler error marks with their lines (section 8.5).

Back in `ed_key`, `follow` (`EDITOR.C:188`) scrolls the view if the cursor
left it (chapter 7).

### Step 5: drawing (EDITOR.C, SYNTAX.C, SCREEN.C)

The next `ed_step` finds no key waiting, so it calls `ed_draw`:

`src/EDITOR.C:1872-1879`
```c
void ed_draw(void)
{
    Buffer *b = B;
    int g = gutter(b), row = TY + b->cy - b->top, col = 1 + g + x_of(&b->ln[b->cy], b->cx) - b->left;
    int visible = row >= TY && row < TY + TH() && col >= 1 + g && col < SC - 1;
    ed_paint();
    show(row, col, visible ? (overwrite ? 2 : 1) : 0);
}
```

`ed_paint` (`EDITOR.C:1787`) draws the whole editor into an array in
memory: the menu bar, the frame, each visible line, the scroll bar and the
status line. It first makes sure the syntax state at the start of every
visible line is known (`buf_state(b, b->top + th)`, `EDITOR.C:1816`). Each
line then goes through `cached_line` (`EDITOR.C:1675`). If nothing about the
line changed since it was last drawn, its cells are copied from a cache. The
line we typed in has a new stamp, so it is drawn again by `draw_line`
(`EDITOR.C:1620`).

`draw_line` asks `syn_line` for a colour role for every byte:

`src/SYNTAX.C:669-682`
```c
int syn_line(int lang, const char *s, int n, int state, unsigned char *role)
{
    paint(role, 0, n, A_TEXT);
    switch (lang) {
    case L_C: return syn_c(s, n, state, role);
    case L_ASM: return syn_asm(s, n, role);
    case L_PASCAL: return syn_pascal(s, n, state, role);
    case L_BASIC: return syn_basic(s, n, role);
    case L_BATCH: return syn_batch(s, n, role);
    case L_MAKE: return syn_make(s, n, role);
    case L_INI: return syn_ini(s, n, role);
    default: return S_NORMAL;
    }
}
```

Then it turns each byte into a screen cell. A role (such as `A_KEYWORD`)
becomes an attribute through the table `attr[]`. A tab becomes several
spaces. The selection and a matching bracket override the colour:

`src/EDITOR.C:1641-1654`
```c
        ch = (unsigned char)L->s[i];
        a = attr[roles[i]];
        if (ch == '\t') {
            w = next_x(x, '\t') - x;
            ch = ' ';
        } else if (ch == 0)
            ch = ' ';
        if (sel && (l > sl1 || (l == sl1 && i >= sc1)) && (l < sl2 || (l == sl2 && i < sc2)))
            a = attr[A_SELECT];
        if ((l == bl && i == bc) || (l == ml && i == mc))
            a = attr[A_MATCH];
        for (k = 0; k < w; k++, x++)
            if (x >= b->left && x < b->left + tw)
                rp[x - b->left] = (unsigned short)(ch | a << 8);
```

`rp` points into `scr`, the screen array in `SCREEN.C`. Each cell is a
16-bit number: the character in the low byte, the attribute in the high
byte (`ch | a << 8`). Chapter 4 explains cells.

### Step 6: the screen (DOS.C)

`show` (`SCREEN.C:238`) passes the array to `plat_show`. `plat_show` keeps a
copy of what is on the screen now (`shown`) and writes only the rows that
differ:

`src/DOS.C:225-239`
```c
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
```

After typing one letter, usually two rows differ: the line typed in and the
bottom frame row, where the column number shown changed. The status line
changes too if a suggestion appeared. `ScreenUpdateLine` is a DJGPP library
function (from `<pc.h>`) that copies one row of cells into the PC's text
screen memory. Finally the cursor is moved, and its shape set: a thin line
for insert, a block for overwrite.

That is the whole journey. The rest of this tour looks at each stop in
detail.

---

## 3. Only DOS.C knows about DOS

### The problem

DE runs on DOS, but most of what it does (holding text, colouring it,
deciding what a key means) has nothing to do with DOS. Code that talks to
the BIOS cannot run on Linux, so it cannot be tested there, with Linux's
debugging tools. Talking to the BIOS is also slow and full of details.

### How

`DE.H` declares a small set of platform functions. Everything DOS-specific
sits behind them:

`src/DE.H:41-51`
```c
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
```

There are more below these: `plat_repeat`, `plat_exec`, `plat_user_screen`,
`plat_fullpath`, `plat_palette` and `plat_mtime` (`DE.H:52-65`).

Two files define them:

- `src/DOS.C`, for the real program. It uses the BIOS video interrupt (10h),
  the BIOS keyboard interrupt (16h) and the mouse driver (33h).
- `test/HOSTPLAT.C`, for the tests on Linux. Keys come from an array the
  test fills; `plat_show` copies the cells into an array the test can read.

The rest of DE is plain C99 and uses only the standard C library and a few
POSIX calls (`opendir`, `stat`, `getcwd`, `chdir`) that DJGPP also has.

### Why: the cost of a BIOS call

DJGPP programs run in 32-bit *protected mode*, with access to all the
computer's memory. The BIOS is 16-bit *real mode* code from the original
PC. To call it, the *DPMI* server (CWSDPMI) switches the processor to real
mode and back. That trip costs far more than the call itself, especially
under DOSBox-X. The comment at the top of `DOS.C` says so, and the code
avoids the trip wherever it can:

- **Reading BIOS memory directly.** The BIOS keeps its state in the *BIOS
  data area*, at segment 40h (address 400h). DE reads it with `_farpeekb`
  and `_farpeekw` instead of calling the BIOS:

  `src/DOS.C:33-38`
  ```c
  /* The BIOS data area, at segment 40h. */
  #define BIOS_SHIFT    0x417            /* Shift, Ctrl, Alt ... */
  #define BIOS_KBD_HEAD 0x41A            /* the keyboard buffer: the next key */
  #define BIOS_KBD_TAIL 0x41C            /* ... and where the next one goes */
  #define BIOS_TICKS    0x46C            /* timer ticks, 18.2 a second */
  #define BIOS_MODE     0x449            /* the video mode */
  ```

  "Is a key waiting?" is then just "is the buffer's head different from its
  tail?" (`key_waiting`, `DOS.C:260`). The clock is the tick count times
  54.9254 ms (`plat_ms`, `DOS.C:265`): 1000 / 18.2065 ticks a second.

- **Writing only what changed.** `plat_show` compares each row with what is
  already on the screen (step 6 above).

- **Resting when idle.** When no key or mouse event is waiting, `plat_key`
  calls `__dpmi_yield` (`DOS.C:424`), so DOSBox-X can give the time to the
  host.

### Keys as numbers

DE turns every key into one `int` (`DE.H:16-33`):

```
 bits:  15  14   13   12   11 ........ 0
        --  ALT  CTRL SHIFT  base key
```

- A character is its own code, 32 to 255 (`'a'` is 97).
- Special keys are numbers from `0x100` up: `DK_UP` is `0x100`, `DK_DOWN`
  is `0x101`, and so on through the function keys (`DK_F9` is `0x112`).
- A modifier is a bit added on top: `KM_SHIFT` is `0x1000`, `KM_CTRL`
  `0x2000`, `KM_ALT` `0x4000`.
- `KBASE(k)` is `k & 0x0FFF`: the key without its modifiers.

So Ctrl+Shift+Z is `0x2000 + 0x1000 + 'Z'` = `0x305A`, and `KBASE` of it is
`'Z'`. Because each modifier is a separate bit, `k & KM_CTRL` asks "is Ctrl
held?" and `k | KM_CTRL` adds Ctrl. This is the same idea as flags packed in
an integer anywhere in graphics code.

`translate` (`DOS.C:311`) does the mapping from BIOS scan codes. The BIOS
gives Ctrl+A as character 1, so `'@' + 1` is `'A'` and the key is
`KM_CTRL | 'A'`. Keys with no character (arrows, F-keys, Alt+letters) come
with character 0 and are told apart by scan code.

### The mouse

`plat_key` also polls the mouse driver (`DOS.C:401-423`). In text mode the
driver reports positions in virtual pixels, eight to a character cell each
way, so `row = y / 8` and `col = x / 8`. A change of the left button
returns `DK_MOUSE` with the global `mouse` filled in (`M_PRESS` or
`M_RELEASE`); moving with the button held returns `M_DRAG`.

---

## 4. The screen: cells, colour roles and themes

### The problem

DE draws menus over text, dialogs over menus, shadows under dialogs. Drawing
straight onto the real screen would flicker and would need a BIOS trip per
character. DE also needs several colour themes, and needs to follow
DOSDev's.

### The screen is an array of cells

`SCREEN.C` keeps the whole screen as an array in memory, `scr`, with `SR`
rows and `SC` columns. Cell `(r, c)` is `scr[r * SC + c]`. This is
*row-major* order, the same layout as an image in memory: row 0 first, then
row 1, and so on.

Each cell is 16 bits, exactly as the PC's text-mode video memory stores it:

```
 15        12 11         8 7                 0
 +-----------+------------+-------------------+
 | background| foreground |  character (CP437)|
 +-----------+------------+-------------------+
   attribute byte (a)        character byte (ch)

 cell = ch | a << 8          a = foreground + 16 * background
```

For example, attribute `0x1E` is foreground 14 (yellow) on background 1
(blue): Borland's editor text. The characters are code page 437, the PC's
character set, which has the box-drawing characters DE uses for frames
(`CH_H`, `CH_V` and the corners, `DE.H:103-122`).

On the original PC, attribute bit 7 makes text blink. `take_screen` turns
that off (`blink(0)`, `DOS.C:181`), so bit 7 instead gives the 8 bright
background colours, and all 16 colours can be backgrounds.

Drawing is plain array writing. `put` is the smallest piece:

`src/SCREEN.C:135-139`
```c
void put(int r, int c, int ch, int a)
{
    if (r >= 0 && r < SR && c >= 0 && c < SC)
        scr[r * SC + c] = (unsigned short)((ch & 0xFF) | (a & 0xFF) << 8);
}
```

`text`, `fill`, `box`, `border` and `hot` build on it. `hot` draws a label
such as `"~F~ile"`, where the letter between tildes is the hot key, in a
second colour. `shadow` (`SCREEN.C:220`) keeps the characters under a
window's shadow and only changes their attribute to `0x08`, dark grey on
black.

Nothing reaches the real screen until `show` is called. This is *double
buffering*, as in graphics: draw the whole frame off screen, then present
it. Dialogs rely on it: each one first calls `ed_paint` to draw the whole
editor into `scr`, then draws itself on top, then calls `show`.

### Colour roles

No drawing code uses a colour number directly. It uses a *role*: what the
thing is (`DE.H:73-80`, shortened here):

```c
A_TEXT, A_KEYWORD, A_TYPE, A_COMMENT, ... A_MENU, A_STATUS, ... A_GHOST,
A_MARKERR, A_MARKWARN, A_COUNT
```

There are 35 roles; `A_COUNT` is how many. The table `attr[A_COUNT]` gives
the attribute for each role in the current theme. So `draw_line` writes
`attr[roles[i]]`, and a menu writes `attr[A_MENU]`. Changing the theme only
changes the table.

### Themes: two layers of lookup

A theme in DE is two tables:

```
 role (A_KEYWORD)  --attr[]-->  attribute 0x1F  (colour 15 on colour 1)
                                      |
 colour number (0..15)  --VGA DAC-->  red, green, blue
```

1. **Role to attribute** (`rows[3][A_COUNT]`, `SCREEN.C:21-30`). There are
   three rows: Borland blue, dark palettes and light palettes.
2. **Colour number to RGB**. The VGA card turns each of the 16 colour
   numbers into a real colour through its *DAC* registers. DE can load new
   RGB values into them. This is an *indexed colour palette*, the same idea
   as a colour lookup table (CLUT) in old graphics modes.

So "Midnight" does not repaint the text with different attribute numbers.
The text is still "colour 14 on colour 1", but colour 1 now looks like
`#2e3440` instead of blue. The palettes of DOSDev's themes are in
`palettes[3][16]` (`SCREEN.C:34-41`), as `0xRRGGBB` numbers.

`theme_set` chooses both layers:

`src/SCREEN.C:95-118`
```c
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
```

The themes are numbered 0 Follow DOSDev, 1 Borland blue, 2 Midnight,
3 Paper, 4 Solar (`theme_name`, `SCREEN.C:13`). Borland blue uses the
PC's own colours, so it sets no palette (`pal` stays `NULL`).

### Following DOSDev: DOSDEV_PALETTE

DOSDev sets the environment variable `DOSDEV_PALETTE` in every DOSBox-X it
starts (`CONTRACT.md`). It is 96 hexadecimal digits: 16 colours, times 3
bytes (red, green, blue), times 2 digits per byte. `dosdev_palette`
(`SCREEN.C:61`) reads it. Anything that is not exactly 96 hex digits is not
used, and DE stays in Borland blue. If the palette is the PC's own (`cga`,
`SCREEN.C:44`), DE changes nothing, because nothing would change.

Which role row to use depends on whether the window background (colour 1)
is light or dark:

`src/SCREEN.C:78-82`
```c
static int light(const unsigned char rgb[48])
{
    long r = rgb[3], g = rgb[4], b = rgb[5];
    return 2126 * r * r + 7152 * g * g + 722 * b * b > 5000L * 255 * 255;
}
```

This is a brightness test. The weights 0.2126, 0.7152 and 0.0722 (here
times 10,000) are the Rec. 709 luminance coefficients: green looks
brightest to the eye, blue darkest. Squaring each 8-bit value roughly undoes
the screen's gamma, as the comment says. Dividing both sides by
10,000 × 255², the test is

```
 0.2126 (r/255)^2 + 0.7152 (g/255)^2 + 0.0722 (b/255)^2  >  0.5
```

that is, "is colour 1 more than half as bright as white?" Paper's colour 1
is `#f4f1e8`, so it is light; Midnight's `#2e3440` is dark.

### Putting colours on the VGA (DOS.C)

`plat_palette` (`DOS.C:138`) receives the 48 bytes. `pal_show`
(`DOS.C:122`) writes them into the DAC with INT 10h, function 1010h. The DAC
holds 6 bits per channel (0 to 63), so each 8-bit value is shifted right by
two: `pal_want[3 * i] >> 2` divides by 4.

Two details matter:

- A text colour number is not the DAC register number. It goes through the
  attribute controller first (on a standard VGA, colour 6 uses DAC register
  20, and colours 8 to 15 use registers 56 to 63). `pal_learn`
  (`DOS.C:88`) asks the BIOS which register each colour uses (function
  1007h), and what colour it holds (function 1015h).
- What DE found is kept in `pal_found`. While another program has the
  screen, and after DE ends, those colours are put back (`give_screen`
  calls `pal_show(0)`, `DOS.C:207`). So DOS and your own programs never see
  DE's theme.

### Why this way

- One array plus "write only changed rows" gives flicker-free drawing with
  few BIOS trips.
- Roles keep colour decisions in one table. Adding a theme is adding data,
  not code.
- Changing the DAC instead of the attributes lets one set of role rows serve
  four different palettes, and matches how DOSDev itself describes a theme:
  as 16 RGB colours.

---

## 5. How a file is stored and edited

### The problem

A file must be held in memory so that typing, deleting, undo, drawing and
colouring are all quick, and so that the file is written back exactly as it
was read (line endings, a missing last line break, a DOS end-of-file mark).

### The data structure: an array of lines

DE holds a file as an array of lines. Each line is its own growable string.
This is not a *gap buffer* (one big array with a movable hole at the
cursor) and not a *rope* (a tree of pieces). It is simpler than both.

`src/DE.H:125-132`
```c
typedef struct { char *s; int len, cap; unsigned stamp; } Line;   /* stamp: new for each change */
typedef struct {
    char kind;                 /* 'i' inserted, 'd' deleted */
    int l1, c1, l2, c2;        /* the range: inserted, or deleted from */
    char *text; int n;         /* what was inserted or deleted */
    int group;                 /* undone and redone together */
    int cy, cx;                /* the cursor before */
} Change;
```

A `Line` has a pointer to its bytes (`s`), how many bytes it holds (`len`),
how many it has room for (`cap`), and a stamp. The bytes never include the
line break, and are always followed by a 0 byte, so `s` is also a normal C
string. A `Buffer` (`DE.H:134-146`) holds the array `ln` of `n` lines, with
room for `cap`:

```
 Buffer b
 +--------------+
 | ln  ---------+--> [0] { s --> "int main(void)\0"   len 14, cap 16 }
 | n   = 4      |    [1] { s --> "{\0"                len  1, cap 16 }
 | cap = 64     |    [2] { s --> "    return 0;\0"    len 13, cap 16 }
 | cy, cx       |    [3] { s --> "}\0"                len  1, cap 16 }
 | ...          |    [4] ... [63]  room, not yet used
 +--------------+
```

There is always at least one line. An empty file is one empty line.

Both arrays grow by doubling. `line_reserve` (`BUFFER.C:59`) starts a line
at 16 bytes and doubles until the text plus its 0 fits; `lines_reserve`
(`BUFFER.C:90`) starts the line array at 64 entries. Doubling means a line
that grows to `m` bytes is moved at most about `log2(m)` times, and the
total copying is at most about `2m` bytes: the cost per added byte is
constant on average.

**What things cost.** Inserting a byte in a line of length `m` moves up to
`m` bytes. Inserting or deleting a whole line moves the `Line` structs after
it. A `Line` is 16 bytes on DJGPP (a 4-byte pointer and three 4-byte
numbers), so an Enter near the top of a 10,000-line file moves about
160 KB. `DE /SPEED` measures what these cost on a given computer.

**Why lines.** Everything else in DE works line by line: drawing (one screen
row per line), colouring (a state at the start of each line), the row cache
(a stamp per line), compiler messages (a line number). With an array of
lines, line `l` is simply `b->ln[l]`, with no searching. The price is the
`memmove` of `Line` structs when lines come and go, which is cheap for the
file sizes DE is for (it refuses files over 16 MB, `DE_MAX_FILE`,
`DE.H:14`).

### Positions, the cursor and the selection

A position in the text is a pair (line, byte): `cy` and `cx` for the
cursor. Both start at 0. `cx` counts bytes, not screen columns, because a
tab is one byte but several columns wide.

The screen column of byte `c` is worked out by `x_of` (`EDITOR.C:80`),
using `next_x`:

`src/EDITOR.C:75-78`
```c
static int next_x(int x, int ch)
{
    return ch == '\t' ? (x / opt_tab + 1) * opt_tab : x + 1;
}
```

With integer division, `(x / T + 1) * T` is the next multiple of `T` that is
greater than `x`. With `T = 8`: from column 0 a tab goes to 8, from 5 to 8,
from 8 to 16. `c_of` (`EDITOR.C:88`) goes the other way: the byte at a
screen column.

`want` remembers the screen column the cursor wants to be in. Moving up and
down keeps `want`, so passing through a short line does not lose your
column.

The selection is a flag and an *anchor*: `sel`, `sy`, `sx`. The other end of
the selection is always the cursor. `sel_range` (`EDITOR.C:115`) orders the
two ends so the start comes first. Shift plus a moving key sets the anchor
where the cursor was, then moves the cursor (`move`, `EDITOR.C:602-607`).

### Two changes make all the others

Every change to the text is one of two functions:

- `buf_insert(b, l, c, text, n)`: insert `n` bytes at (l, c). The text may
  hold `'\n'`, which splits lines. The cursor goes to the end of what was
  inserted.
- `buf_delete(b, l1, c1, l2, c2)`: delete from (l1, c1) up to (l2, c2). The
  cursor goes to the start.

Typing, Enter, Backspace, paste, indenting, commenting, moving lines,
replace: all of them, in `EDITOR.C`, are built from these two. That is what
makes undo simple: there are only two kinds of change to undo.

Each of the two records a `Change` and calls a "raw" function that does the
work: `raw_insert` (`BUFFER.C:161`) and `raw_delete` (`BUFFER.C:223`).
`raw_insert` with line breaks keeps the tail of the line (the part after the
cursor), opens a gap in the line array, fills the new lines, and puts the
tail on the end of the last one. `raw_delete` over several lines joins the
start of the first line to the end of the last, frees the lines between,
and closes the gap.

### Undo and redo

The buffer keeps every change since the file was opened in the array `ch`,
with `nch` entries. `upos` is how many of them are in effect now:

```
 ch:  [0] [1] [2] [3] [4]
                   ^       ^
                 upos=3   nch=5     undo moves upos left; redo moves it right
```

- **Undo** (`buf_undo`, `BUFFER.C:358`) steps back: an insert is undone by
  deleting its range, a delete by inserting its text again. It keeps going
  while the changes have the same `group` number, so a group is undone as
  one step. Then the cursor goes back to where it was before the change
  (`cy`, `cx` in the `Change`).
- **Redo** (`buf_redo`, `BUFFER.C:378`) plays them forward again.
- **A new change after an undo** throws away the changes that could have
  been redone (`push`, `BUFFER.C:279-280`).
- **Modified or not.** `saved` is the value `upos` had when the file was
  last saved. The file has unsaved changes exactly when
  `upos != saved` (`buf_modified`, `BUFFER.C:401`). If the saved state is
  thrown away by a new change, `saved` becomes -1, which `upos` can never
  equal again.

Groups come from `edit_begin` (`EDITOR.C:165`). Each editing key has a kind:
0 for most edits, 1 for typing, 2 for Backspace, 3 for Delete. A key of the
same kind as the last one, at the place where the last one left the cursor,
joins its group. Anything else starts a new group (`buf_group`). So
Backspace pressed five times is one undo step; Backspace, then a move, then
Backspace is two. Inside `buf_insert`, typing also joins the previous
`Change` record itself (step 4 of chapter 2), so a typed word is one record,
not one per letter.

### A worked example

Start with an empty file: one line, `""`. Type `a`, `b`, press Enter, then
Backspace.

1. **`a`**. `edit_begin(b, 1)` starts a new group, say group 1. `buf_insert`
   records `ch[0] = { 'i', (0,0)-(0,1), "a", group 1 }`. Line 0 is `"a"`.
2. **`b`**. Same kind, and the cursor is where `a` left it, so no new group.
   `buf_insert` sees that `ch[0]` is an insert in this group that ends at
   (0,1), and extends it: `ch[0] = { 'i', (0,0)-(0,2), "ab" }`.
3. **Enter**. `newline` (`EDITOR.C:753`) uses kind 0, so a new group, 2.
   The text has a line break, so it is a new record:
   `ch[1] = { 'i', (0,2)-(1,0), "\n" }`. `raw_insert` keeps the tail of
   line 0 (empty here), opens a slot after line 0 in the line array, and
   makes the new line 1 `""` plus the tail.
4. **Backspace** at (1,0). A new group, 3. `backspace` (`EDITOR.C:836`)
   calls `buf_delete(b, 0, 2, 1, 0)`. `buf_text` copies what is about to go
   (`"\n"`), and `ch[2] = { 'd', (0,2)-(1,0), "\n" }`. `raw_delete` joins
   line 0 and line 1 and removes line 1.

```
 ch[0]  i  (0,0)-(0,2)  "ab"   group 1
 ch[1]  i  (0,2)-(1,0)  "\n"   group 2
 ch[2]  d  (0,2)-(1,0)  "\n"   group 3        upos = nch = 3
```

Now Ctrl+Z three times. The first undoes `ch[2]`: insert `"\n"` at (0,2), so
there are two lines again. The second undoes `ch[1]`: delete (0,2)-(1,0),
back to `"ab"`. The third undoes `ch[0]`: delete (0,0)-(0,2), back to `""`.
`upos` is 0. Ctrl+Y three times plays them forward.

The test `test_random` (`test/DETEST.C:1003`) checks exactly this
property: after 600 random keys, undoing everything gives back the empty
file, and redoing everything gives back the same text.

### Loading and saving

`buf_load` (`BUFFER.C:511`) reads the whole file with one `fread`, then cuts
it into lines. On the way it notes three things, so the file can be written
back the same way:

- **Line endings.** DOS ends lines with CR LF (`"\r\n"`), Linux with LF
  alone. DE counts both and keeps CRLF if at least half of the line breaks
  are CRLF (`crlfs * 2 >= lfs`, `BUFFER.C:557`). A file with no line breaks
  counts as CRLF.
- **The last line break.** `final_nl` says whether the last line ends with
  one.
- **Ctrl+Z.** Old DOS programs end text files with byte 1Ah. `ctrlz` keeps
  it, and it is not shown as text.

`buf_save` (`BUFFER.C:640`) writes to `NAME.$$$` first (`temp_name`,
`BUFFER.C:594`), then removes the old file and renames the new one. DOS
cannot rename over an existing file, hence the remove. If the write fails,
the old file is untouched. If the rename fails and the old file is gone,
the text is still safe in the `.$$$` file, and `buf_save` returns -2 so the
editor can say so.

**Changed on disk.** When it loads or saves, the buffer remembers the
file's time and size (`disk_time`, `disk_size`). `buf_changed_on_disk`
(`BUFFER.C:422`) compares both, because DOS file times move in 2-second
steps, so two saves close together can have the same time. Saving over a
file that another program changed asks first (`save_file`,
`EDITOR.C:484-495`), and coming back from a build or a run checks every
open file (`ed_check_disk`, `EDITOR.C:434`).

---

## 6. Syntax colouring: a small state machine

### The problem

To colour a line you must know whether it starts inside a comment that
began on an earlier line. Working that out by reading the file from the top
for every line drawn would be slow on a long file.

### One line at a time, with a state

`syn_line` (`SYNTAX.C:669`) is the contract for every language:

```
 syn_line(language, line text, state at the start)
     -> a role for every byte of the line
     -> the state at the end of the line
```

The state is a small number (`SYNTAX.C:11`):

```c
enum { S_NORMAL, S_CCOMMENT, S_PBRACE, S_PPAREN };
```

C needs one extra state: inside `/* ... */`. Pascal needs two: inside
`{ ... }` and inside `(* ... *)`. For every other language, DE's colouring
has no comment that can cross a line break, so those lines always end in
`S_NORMAL`. For C:

```
                  "/*" with no "*/" after it on the line
     +----------+ --------------------------------------> +------------+
     | S_NORMAL |                                         | S_CCOMMENT |
     +----------+ <-------------------------------------- +------------+
                         "*/" found on a later line
     (a whole line with no "*/" stays in S_CCOMMENT)
```

You can see both arrows in `syn_c`. Starting inside a comment, it looks for
the end:

`src/SYNTAX.C:200-209`
```c
    if (state == S_CCOMMENT) {
        for (j = 0; j + 1 < n && !(s[j] == '*' && s[j + 1] == '/'); j++)
            ;
        if (j + 1 >= n) {
            paint(role, 0, n, A_COMMENT);
            return S_CCOMMENT;
        }
        paint(role, 0, j + 2, A_COMMENT);
        i = j + 2;
    }
```

The rest of `syn_c` is a scanner: it walks the line from left to right,
and at each position decides what starts there (a comment, a `#`
directive, a string, a number, a word, a bracket), paints that stretch with
`paint`, and jumps past it. Words get their colour from `word_role`.

### The states as a sequence

Call the state at the start of line `i` *s(i)*. The first line starts in
`S_NORMAL`, and each line's end state is the next line's start state:

```
 s(0) = S_NORMAL
 s(i+1) = f(line i, s(i))          where f is syn_line
```

This is a recurrence, like a sequence defined by a step function. DE keeps
the values it has worked out in the array `hs` of the buffer: `hs[i]` is
*s(i)*, known to be right for every `i < hvalid`. `buf_state(b, upto)`
(`BUFFER.C:452`) extends the known part up to the line asked for:

`src/BUFFER.C:469-484`
```c
    for (i = b->hvalid; i <= upto;) {
        const Line *L = &b->ln[i - 1];
        unsigned char v;
        if (rcap < L->len + 1) {
            rcap = L->len + 64;
            role = xrealloc(role, rcap);
        }
        v = (unsigned char)syn_line(b->lang, L->s, L->len, b->hs[i - 1], role);
        if (i >= b->hdirty && i < b->hknown && v == b->hs[i]) {
            i = b->hknown;              /* as it was: so are the rest */
            b->hknown = b->hdirty = 0;
            jumped = 1;
            continue;
        }
        b->hs[i++] = v;
    }
```

For drawing, `ed_paint` asks for the states up to the bottom of the
screen, so only the lines above and on the screen are worked out. Opening a long file at
its top costs nothing for the lines below.

### Stopping early after a change

When a line changes, `touched` (`BUFFER.C:106`) moves `hvalid` back to just
after it: the states after the changed line may now be wrong. But usually
they are not. Typing a letter in the middle of a function changes no
comment. So DE keeps the old states (up to `hknown`) and, while working them
out again, compares each new state with the old one. `hdirty` marks the end
of the changed lines.

The reason this is safe is a small induction. Suppose the lines changed are
all before line `d`, and for some `k >= d` the new state equals the old one,
*s'(k) = s(k)*. Every line from `k` on is the same as before, and *f* is the
same function, so *s'(k+1) = f(line k, s'(k)) = f(line k, s(k)) = s(k+1)*,
and so on for every line after. All the remaining old states are right, and
the loop jumps to `hknown`.

An example. A C file has 100 lines and all 100 states are known. You type
`/*` at the end of line 10. `touched` sets `hvalid` to 11, keeps the old
states up to 100 (`hknown`), and sets `hdirty` to 11. Now:

- *s'(11)* is `S_CCOMMENT`; the old *s(11)* was `S_NORMAL`. Different:
  store it.
- Lines 11, 12 ... are now inside the comment. Each new state is
  `S_CCOMMENT` and each old one `S_NORMAL`. Different: store them.
- Line 20 is `x = 1; /* note */`. Starting inside the new comment, the
  first `*/` on it ends that comment, so the new state after line 20 is
  `S_NORMAL`. The old *s(21)* was `S_NORMAL` too. Equal: everything after is
  as it was. The loop jumps to 100.

The work is proportional to how far the change really reaches, not to the
size of the file.

When lines are inserted or deleted, the old states belong to other lines
now (line 50 used to be line 49). `touched` is told so (`moved`) and keeps no
old states at all.

The three numbers `hvalid`, `hknown` and `hdirty` are the subtlest
bookkeeping in DE. The test `buf_state_check` (`BUFFER.C:494`) works every
kept state out again from the top and compares; the random test calls it
after every key.

### Finding keywords quickly: hash tables

Every word on a line has to be looked up: is `while` a keyword, is `int` a
type? Each language's word lists are written as one string with spaces
around each word (`C_KEYWORDS`, `SYNTAX.C:187`). The first time a language
is coloured, `word_role` (`SYNTAX.C:640`) puts all its words into one *hash
table*.

The hash function is FNV-1a:

`src/SYNTAX.C:85-94`
```c
static unsigned word_hash(const char *s, int n, int nocase)
{
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)(nocase ? up(s[i]) : s[i]);
        h *= 16777619u;
    }
    return h;
}
```

It mixes each byte into a 32-bit number with an exclusive-or and a multiply.
The multiply overflows on purpose: for `unsigned` numbers C defines that
arithmetic wraps around modulo 2³², so this is arithmetic in the ring of
integers modulo 2³². (For signed `int`, overflow is undefined behaviour,
which is why hash code uses `unsigned`.)

The table has a power-of-two size, at least twice the number of words. The
slot is `h & mask`, where `mask` is the size minus 1: for a power of two,
`h & (2^k - 1)` equals `h mod 2^k`, and is quicker. If a slot is taken, DE
tries the next one (`h++`): this is *linear probing*. The table is never
more than half full, and Knuth's analysis of linear probing gives about
(1 + 1/(1 - α)) / 2 = 1.5 slots looked at, on average, for a word that is
there, at load factor α = 1/2.

For languages where case does not matter (assembly, Pascal, BASIC, batch),
the hash and the comparison use upper case (`nocase`).

### Which language a file is

`lang_of` (`SYNTAX.C:39`) decides from the name: `Makefile` or `.MAK` is
Make, `.C .H .CPP ...` is C, and so on. The extension lists are strings like
`" C H CPP "`, and `ext_is` looks for `" EXT "` in them, with the spaces, so
`C` does not match inside `CPP`.

### Why this way

- One state number per line is all the memory the cache needs (one byte a
  line, in `hs`).
- Only the start-of-line states are cached. The roles of the bytes of a
  line are worked out again each time the line is drawn, which is cheap,
  and the row cache (chapter 7) avoids even that for unchanged lines.
- Recognising languages by simple scanning, not by parsing, means a file
  with errors in it still colours sensibly.

The limits of this approach are in [Rough edges](#12-rough-edges).

---

## 7. Drawing the editor window

### The layout

On an 80 by 25 screen:

```
 row  0   menu bar                                          menu_bar()
 row  1   +--[#]------------ NAME.C ---------------- 1/2 -+  frame, title
 row  2   | 1 int main(void)                             ^|  TY = 2: first text row
  ...     | gutter | text ...                            #|  TH() = SR - 4 = 21 rows
 row 22   |                                              v|  last text row
 row 23   +- 12:5 ----------------------------------------+  frame, line:column
 row 24   status line                                       status_line()
          col 0, then gutter, then text from tx = 1 + g; scroll bar at col SC - 1
```

`TY` is 2 (`EDITOR.C:22`) and `TH()` is `SR - 4` (`EDITOR.C:25`): the rows
left after the menu bar, the two frame rows and the status line.

### The gutter

With line numbers on, the gutter is wide enough for the largest line number,
at least 3 digits, plus a space (`gutter`, `EDITOR.C:27`):

```
 d = number of decimal digits of n  (the file's line count)
 g = max(d, 3) + 1
```

A 50-line file has `g = 4`; a 1,234-line file has `g = 5`. The text starts
at column `tx = 1 + g`, and is `tw = SC - 2 - g` columns wide (one column
for the left frame, one for the scroll bar). With 80 columns and `g = 4`,
`tw = 74`.

### Scrolling arithmetic

`top` is the first file line shown; `left` is the first screen column of the
text shown. A file position (line `cy`, screen column `x`) lands on the
screen at

```
 row = TY + cy - top
 col = tx + x - left        (tx = 1 + g)
```

`follow` (`EDITOR.C:188`) keeps the cursor in view:

`src/EDITOR.C:196-201`
```c
    if (b->cy < b->top) b->top = b->cy;
    if (b->cy >= b->top + th) b->top = b->cy - th + 1;
    if (b->top < 0) b->top = 0;
    x = x_of(&b->ln[b->cy], b->cx);
    if (x < b->left) b->left = x < 8 ? 0 : x - 8;
    if (x >= b->left + tw) b->left = x - tw + 8;
```

Vertically it keeps `top <= cy <= top + th - 1`, scrolling as little as it
must. With `th = 21`, moving the cursor to line 30 from the top of the file
gives `top = 30 - 21 + 1 = 10`, and the cursor is on row `2 + 30 - 10 = 22`,
the last text row.

Sideways it scrolls 8 columns more than it must, so typing at the right
edge does not scroll on every key. Pressing End on a 200-character line
with `left = 0`: `x = 200 >= 0 + 74`, so `left = 200 - 74 + 8 = 134`, and
the cursor is at screen column `5 + 200 - 134 = 71`. Home then gives
`x = 0 < 134`, so `left = 0`.

PgDn moves both the cursor and `top` by `th - 1` lines (`EDITOR.C:660-661`),
so one line of the old page stays on screen as a reference. `center`
(`EDITOR.C:204`) puts a line in the middle, for jumps.

### The scroll bar

The scroll bar (`scrollbar`, `EDITOR.C:1713`) has an arrow at each end and
`th - 2` track cells between. The thumb's position on the track is

```
 pos = floor( (th - 3) * cy / (n - 1) )        0 <= pos <= th - 3
```

a straight proportion: the first line gives 0, the last line gives `th - 3`.
A click on the track does the inverse (`mouse_key`, `EDITOR.C:1508`):

```
 cy = floor( (r - TY - 1) * (n - 1) / (th - 3) )
```

where `r - TY - 1` is the track cell clicked.

### The row cache

Most keys change one line or none. `cached_line` (`EDITOR.C:1675`) avoids
drawing a line again when nothing it depends on changed. It describes a line
drawing by a key:

`src/EDITOR.C:1668-1673`
```c
#define SLOTS 256
#define COLS 132
typedef struct { const Buffer *b; unsigned stamp; int ok, hs, lang, left, tx, tw, tab, theme, s1, s2, nl, m1, m2; } LineKey;
static LineKey slot_key[SLOTS];
static unsigned short slot_cells[SLOTS][COLS];
static int no_cache;
```

The key holds the line's stamp, its start state, the language, the view,
the tab width, the theme, and where the selection and bracket marks fall on
it. If the key in slot `stamp % 256` is the same (compared with `memcmp`),
the cells saved there are copied into `scr`. Otherwise the line is drawn
and saved in that slot. Because the key includes the stamp and not the
screen row, a line that scrolled to another row is still found.

This is *memoisation*: a function's answer is kept, indexed by its inputs.
The stamp makes the key cheap: instead of comparing a line's text,
`restamp` (`BUFFER.C:74`) gives the line a new number from a counter at
every change, so "same stamp" means "same text". The `memset(&k, 0, ...)`
at `EDITOR.C:1681` matters for this. A struct can have unused padding
bytes between its parts, and `memcmp` compares those too; zeroing the whole
key first makes the padding the same every time.

Two kinds of line are never cached: the cursor's line while a suggestion
ghost shows on it, and every line on a screen so wide that the text
area reaches past column 132, that is when `SC - 1 > 132`
(`EDITOR.C:1700`). The test `ed_paint_check` (`EDITOR.C:1839`) draws the
screen with the cache and again without it, and checks that both are the
same.

### The status line

`status_line` (`EDITOR.C:1726`) shows the first of these that applies:

1. a message from `ed_message` (until the next key);
2. the suggestion Tab would take, with its signature from the reference;
3. a compiler error, warning or note on the cursor's line;
4. the signature of the function whose brackets the cursor is in;
5. the main keys, and on the right the language, CRLF or LF, and INS or
   OVR.

It records where each key hint is (`hints[]`), so a mouse click on the
status line can run that command.

---

## 8. The other parts

### 8.1 Starting and stopping: MAIN.C

`main` (`MAIN.C:143`) does things in this order:

1. `/?`, `/H` or `--help` prints the usage and stops. `/CHECK FILE` runs
   `de_check` and stops (`MAIN.C:155-161`).
2. `plat_init` takes the screen (it needs at least 80 by 25).
3. `scr_init` makes the screen array, `theme_set(0)` sets Follow DOSDev.
4. `find_config` puts `DE.CFG` next to `DE.EXE` (from `argv[0]`), and
   `config_load` reads it. Options are `key=value` lines.
5. `ed_init`, then each file named on the command line is opened. `+LINE`
   before a file opens it at that line; `+LINE` as the last argument moves
   to that line in the last file opened.
6. The main loop, then `theme_done` and `plat_done` to give the screen back.

`DE /CHECK FILE` (`MAIN.C:27`) does steps 2 and 3, puts everything back,
and writes `DE 0.6 ok` into FILE. It exists so that DOSDev's build can prove
the `DE.EXE` it made starts, without a keyboard (`CONTRACT.md`).

`DE /SPEED FILE` runs `ed_speed` (`EDITOR.C:1916`): it times drawing,
showing, moving and typing, each for about a second by the BIOS clock.

`#ifndef DE_NO_MAIN` (`MAIN.C:10`, `MAIN.C:95`) leaves `main` out when the
tests are compiled, because the test program has its own `main`.

### 8.2 Menus and dialogs

**The problem.** A dialog must take keys and draw itself over the editor
until the user answers.

**How.** Each menu and dialog in `DIALOG.C` runs its own loop until it has an
answer. The pattern is always the same:

```
 for (;;) {
     ed_paint();          draw the editor into scr
     ... draw the dialog on top ...
     show(...);           present it
     k = plat_key();      wait
     ... react; return when answered ...
 }
```

This is called a *modal* loop: while it runs, the main loop is paused inside
the call to it. It keeps the code simple. The caller writes
`if (input_box(...))` and gets the answer back as a return value, with no
callbacks or saved state.

**Menus** are tables. Each item has a label, a key to show, and a command
number:

`src/DIALOG.C:18-22`
```c
static const Item m_file[] = {
    { "~N~ew", "Ctrl-N", C_NEW }, { "~O~pen...", "Ctrl-O", C_OPEN }, { "~S~ave", "F2", C_SAVE },
    { "Save ~a~s...", "", C_SAVEAS }, { "Save a~l~l", "", C_SAVEALL }, SEP,
    { "~C~lose", "Ctrl-W", C_CLOSE }, SEP, { "E~x~it", "Alt-X", C_EXIT }, END
};
```

`SEP` is a separator line and `END` ends the table. The letter between
tildes is the hot key. `build` (`DIALOG.C:69`) copies a menu's items and
fills in the ones that change: Options items whose labels are empty get a
label that shows the setting, such as `[x] Line numbers`, and the Window
menu gets one item per open file. `menu_run` (`DIALOG.C:144`) returns the
command number chosen, or `C_NONE`.

**Commands.** Every action has a number in the `C_` list (`DE.H:293-302`).
`ed_command` (`EDITOR.C:1328`) is one `switch` that carries them out. There
are three ways to reach a command: a shortcut key (the table `shortcuts[]`,
`EDITOR.C:1454`), a menu item, or a click on the status line. All three end
in `ed_command`, so a command behaves the same however it was chosen.
`C_FILE1` must stay last in the list: `C_FILE1 + i` means "go to file i".

**The dialogs.**

- `input_box` (`DIALOG.C:260`): one line of text. The text given is
  selected at first ("fresh"), so typing replaces it.
- `msg_box` (`DIALOG.C:361`): a message, wrapped by `wrap`, and buttons.
  Returns the button number, or -1 for Esc.
- `open_box` (`DIALOG.C:506`): a name field and a folder list, with Tab
  between them.
- `pick_run` (`DIALOG.C:667`): a list to pick from, used for the Messages
  window, Go to symbol and Go to definition.
- `busy_box` (`DIALOG.C:750`): "this takes a while", shown and not waited
  on.

`pick_run` is general because its caller passes in functions:

`src/DE.H:314-320`
```c
typedef struct {
    const char *title, *keys;
    int (*count)(const char *filter);
    void (*item)(int i, char *tag, char *text, int size, int *tagrole);
    const char *(*info)(int i);        /* the status line for the line picked; may be NULL */
    int filter;                        /* does typing filter it? */
} Pick;
```

`count`, `item` and `info` are *function pointers*: variables that hold a
function. `pick_run` calls `p->count(filter)` to know how many lines there
are and `p->item(i, ...)` to get line `i`, without knowing whether they are
messages or symbols. `msgs_list` (`PROJECT.C:828`) and `sym_list`
(`SYMBOLS.C:366`) each fill in a `Pick` with their own functions.

### 8.3 Suggestions and the built-in reference

**The problem.** While you type a word, DE should offer the likeliest
ending, quickly, without reading every open file on every key.

**The word index.** Each buffer has a `Words` index (`SUGGEST.C:82-89`):
every word in the file that starts with a letter or `_` and is at least two
characters long, and how many times it appears. It is a hash table (FNV-1a
again, with linear probing; it doubles when half full, `SUGGEST.C:163-170`)
plus 27 linked lists, one for each first letter `a` to `z` and one for the
rest, so "all words starting with `c`" is a walk down one list.

The index is kept up to date a line at a time. `BUFFER.C` calls
`words_line(b, l, -1)` before it changes line `l` and `words_line(b, l, +1)`
after. Subtracting the old line's words and adding the new line's words
keeps every count exact, whatever the change. `words_check`
(`SUGGEST.C:216`) builds a fresh index and compares, for the tests.

**Candidates and scores.** `sug_collect` (`SUGGEST.C:321`) gathers every
word that starts like the one being typed (ignoring case) and gives it a
score:

```
 score(w) = 4 x (times w is in this file, not counting the word being typed)
          + 2 x (times w is in the other open files of the same language)
          + 3 for each reference entry named w
          + 1 for each keyword list w is in
```

Sorting (`cand_cmp`, `SUGGEST.C:288`) adds 1000 to a word that starts with
exactly what was typed, case included, then orders by score (highest
first), then by length (shortest first), then alphabetically. For example,
typing `pri` in a C file with no `pri...` words of your own: the reference
has `printf` and `printw` (PDCurses), each scoring 3 + 1000, and the keyword
list has `private` (a C++ word, 1 + 1000). `printf` and `printw` tie, and
are the same length, so the alphabet decides: `printf`. The ghost shows
`ntf`.

In languages where case does not matter, typing in capitals gives capitals:
`WRITEL` suggests `WRITELN` (`SUGGEST.C:366-392`).

**When it is worked out.** Typing calls `sug_want`, which only notes the
buffer. The work happens in `sug_refresh` when the screen is next drawn
(`ed_paint` calls it first, `EDITOR.C:1797`). So keys typed faster than the
screen is drawn do not each look for a suggestion. If Tab arrives before the
screen was drawn, `sug_take` works it out on the spot (`SUGGEST.C:455-456`).
No suggestion is offered inside a comment or a string (`in_text`,
`SUGGEST.C:403`, which colours the cursor's line to find out).

**The call the cursor is in.** `sug_call` (`SUGGEST.C:476`) walks back from
the cursor, counting brackets and skipping quoted text, to the `(` that is
not yet closed. The word before it is looked up in the reference, and the
status line shows its signature.

**The reference.** `REF.C` is one array, `refs[]`, of 373 entries:
233 for C (with 53 for SDL3 and 51 for PDCurses among them), 77 for
assembly, 39 for Pascal and 24 for BASIC. Each entry is a language, a name,
where it comes from, how it is written, and what it does (`Ref`,
`DE.H:176-182`). `ref_find` (`REF.C:412`) looks a name up by reading the
array through; with a few hundred entries that is quick enough, and it keeps
`REF.C` pure data. `help_lookup` (Ctrl+F1, `HELP.C:267`) shows the entry for
the word at the cursor, or the list searched for that word.

### 8.4 Symbols and Go to definition

**The problem.** Jump to a function or label by name, without a compiler.

**How.** `sym_scan` (`SYMBOLS.C:248`) reads a file line by line. It colours
each line first (`syn_line`, with the state carried along as in chapter 6),
so it can skip comments and strings. Then it applies the shape rules of the
language, which the comment at the top of `SYMBOLS.C` lists. For C,
`c_line` (`SYMBOLS.C:70`) takes:

- `#define NAME`;
- `struct`, `union` or `enum NAME` that is not a declaration ending in `;`;
- the name in `typedef ... NAME;`, or the `} NAME;` that closes a typedef;
- a name followed by `(` at the start of a line that does not end in `;`
  or `,`, and is not `if`, `while`, `for`, `switch`, `return` or `sizeof`.

Each symbol gets a kind letter: `f` function, `d` define, `t` type, `l`
label, `v` data, `s` section.

**Ctrl+L** (`sym_list`, `SYMBOLS.C:366`) shows the symbols in a `Pick`
list. Typing filters it.

**F12** (`sym_definition`, `SYMBOLS.C:588`):

1. On an `#include` line (or `%include`, or Pascal's `{$I ...}`), open that
   file. `find_include` (`SYMBOLS.C:502`) looks in the file's folder, the
   project's folder, each `LIBS/NAME/INCLUDE`, the folders on `INCLUDE`,
   and `DJDIR/include`, in that order.
2. Otherwise, look for definitions of the word at the cursor in this file,
   in the other open files of the same language, then in the project's files
   on disk (`defs_walk`, `SYMBOLS.C:448`: up to 3 folders deep, 1,000
   files, files under 1 MB).
3. One found: go there. Several: a list. None: show the reference entry, if
   there is one.

Every jump first calls `ed_mark` (`EDITOR.C:242`), which keeps up to 32
places, so Alt+Left (`ed_back`) can come back.

### 8.5 Projects, building, running and compiler messages

**Finding the project.** `proj_find` (`PROJECT.C:149`) looks for
`project.ini` (DOSDev's project file) from the file's folder upwards. The
build command is chosen in this order:

1. a command set in Project > Commands, until DE ends (`over[]`);
2. `build =` from `project.ini`, if `build_on = dos`;
3. `BUILD.BAT` in the project's folder;
4. nothing: F9 then asks, offering a guess (`guess`, `PROJECT.C:190`), such
   as `gcc -Wall -g -o HELLO.EXE -x c HELLO.C`.

**Building.** `proj_build` (`PROJECT.C:374`):

```
 save every changed file (ed_save_all)      -- stop if any can't be saved
 Ctrl+F9 and up to date?  -> just run       (up_to_date: output newer than every source)
 busy_box "Building"
 chdir to the project, run the build with its output into DEBUILD.TMP
 chdir back; ed_check_disk (the build may have rewritten an open file)
 msgs_set: read DEBUILD.TMP into the messages (and BUILD.LOG from Free Pascal)
 errors, or the output not made?  -> the Messages window
 otherwise "Built ...", then run or debug if asked
```

`up_to_date` (`PROJECT.C:287`) compares the time of the program named by
`output =` with the newest source file (or `.BAT` or `.INI` file) in the
project (`newest`, up to 3 folders deep).

The build's output goes to a file: `plat_exec(p.build, log)`. In `DOS.C`,
with a log, `plat_exec` points the standard output and standard error (file
descriptors 1 and 2) at the log file with `dup2`, runs the command with
`system`, and points them back (`DOS.C:446-468`). The screen stays DE's, and
`busy_box` stays up.

**Reading compiler messages.** Every compiler says "error at line 12" in its
own way. `msg_parse` (`PROJECT.C:531`) knows three shapes:

`src/PROJECT.C:523-530`
```c
/* One line of a compiler's output: is it an error, a warning ..., and where?
 * Returns 1 with the file, line and column (0: none) when it says where.
 *   GCC, NASM:            MAIN.C:12:5: error: expected ';' before '}' token
 *   Open Watcom:          MAIN.C(12): Error! E1009: Expecting ';' but found '}'
 *   Free Pascal:          MAIN.PAS(12,5) Error: Identifier not found "X"
 *   Turbo Pascal:         MAIN.PAS(12): Error 3: Unknown identifier.
 *   JWasm, MASM:          MAIN.ASM(7) : Error A2008: syntax error
 *   Turbo C, Borland C++: Error MAIN.C 12: Undefined symbol 'x' in function main */
```

- the kind first (`Error FILE LINE:`), Turbo C's way;
- `FILE(LINE)` or `FILE(LINE,COL)`, then the kind;
- `FILE:LINE:` or `FILE:LINE:COL:`, then the kind and a colon.

The file must look like a source file (`is_source`, `PROJECT.C:83`), so that
a line such as `make: *** ...` is not taken for a place. A drive letter's
colon (`C:\`) is skipped first. A line that says where nothing is, but has
`error:` or `undefined reference` in it, still counts as an error. Every
message that says where is shown the same way, whichever compiler said it:
`FILE(LINE): text` (`PROJECT.C:755`).

**The messages.** They are kept in a fixed array, `msgs[2000]`
(`PROJECT.C:621`). Each `Msg` (`DE.H:213-219`) points at the open buffer
it belongs to, if the file is open. The line number moves with the text:
when lines are inserted or deleted, `BUFFER.C` calls `msgs_shift`
(`PROJECT.C:696`). `msgs_marks` gives `ed_paint` the worst message on each
visible line, for the error and warning colours in the gutter.

F8 and Shift+F8 (`msgs_next`, `PROJECT.C:790`) go round the messages that
say where, with modular arithmetic:

```
 i = (start + dir * k + 2 * nmsgs) % nmsgs        k = 1, 2, ... nmsgs
```

Adding `2 * nmsgs` keeps the number positive before `%`, because in C the
remainder of a negative number is negative.

**Find in files** (`find_in_files`, `PROJECT.C:986`) walks the project's
folders (4 deep, leaving `LIBS` out), reads each text file, and adds each
match as a message of kind `M_FOUND`. For a file that is open, it searches
the buffer instead, so unsaved changes are found too. Lower case finds any
case; a capital makes case count.

### 8.6 Running other programs and coming back

**The problem.** A program you run needs the screen as DOS left it, in DOS's
colours. When it ends, DE must take the screen back, perhaps in another
size, and notice any files the program changed.

**How.** `plat_exec` without a log file gives the program the screen:

`src/DOS.C:469-485`
```c
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
```

- `give_screen` (`DOS.C:202`) hides the mouse, turns blinking back on, puts
  back the colours DE found, sets the normal cursor, and puts back the DOS
  screen: as DE found it at the start, or as the last program left it (both
  saved by `keep_user_screen`).
- `system` runs the command. DE stays in memory while it runs.
- `keep_user_screen` saves what the program left, so Alt+F5
  (`plat_user_screen`) can show it again later.
- If the program changed the video mode (a graphics program, for example),
  DE sets its own mode back.
- "Press a key" waits, so you can read the program's output. `wait_key`
  first throws away keys you typed for the program.
- `take_screen` (`DOS.C:172`) takes the screen again. It fills `shown` with
  `0xFF`, which no real cell matches, so the next `plat_show` writes every
  row.

In `PROJECT.C`, `run_cmd` (`PROJECT.C:319`) changes to the project's folder,
calls `plat_exec`, changes back, remakes the screen array if the size
changed (`scr_init`), says how the program ended, and calls `ed_check_disk`.

`ed_check_disk` (`EDITOR.C:434`) looks at every open file. A file with no
unsaved changes that changed on disk is simply read again. A file with
unsaved changes asks: Reload or Keep mine.

### 8.7 The help viewer, and how the manual gets inside

**The problem.** The manual (`DE.TXT`) must be readable inside DE, with no
extra file to install, and must never be a different version from the
program.

**How.** The `Makefile` turns `DE.TXT` into C source with `awk`:

`Makefile:29-31`
```make
$(OUT)/MANUAL.H: DE.TXT
> mkdir -p $(OUT)
> awk 'BEGIN { print "/* Made from DE.TXT by make: change DE.TXT, not this. */"; print "static const char *const MANUAL[] = {" } { gsub(/\r/, ""); gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\"," } END { print "    0"; print "};" }' DE.TXT > $@
```

Each line of the manual becomes one C string, with carriage returns removed
and every `\` and `"` escaped. The array ends with `0`, a null pointer, so
code can find its end. (`.RECIPEPREFIX = >` at `Makefile:10` lets recipe
lines start with `>` instead of a tab.) The file is made in the output
folder and copied over `src/MANUAL.H` only if the content differs
(`Makefile:21`). `src/MANUAL.H` stays in the repository because `BUILD.BAT`,
the build in DOS, does not make it: it only compiles `src\*.C`.

`HELP.C` includes it (`HELP.C:8`). `index_manual` finds the topics: a topic
is a line followed by a line of three or more dashes.

`src/HELP.C:23-32`
```c
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
```

`DE.TXT` has 16 topics. `help_manual` (`HELP.C:59`) shows the contents (the
introduction's first paragraph, then the topics), then one topic at a time,
with Left and Right for the topic before and after.

**Why.** The manual is compiled into the program, so `DE.EXE` alone is
complete. Writing the manual as plain text with underlined headings keeps it
readable as a file too, and the same convention lets Ctrl+L list a text
file's headings (`sym_scan`, `SYMBOLS.C:288-290`).

### 8.8 When memory runs out

**The problem.** If memory runs out in the middle of an edit, the program
cannot go on safely, but your unsaved work must not be lost.

**How.** Almost every allocation goes through one function:

`src/BUFFER.C:31-41`
```c
void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
#ifdef DE_TEST
    if (de_fail_alloc && --de_fail_alloc == 0)
        q = NULL;
#endif
    if (!q)
        out_of_memory();
    return q;
}
```

Because `xrealloc` never returns `NULL`, the rest of DE does not check every
allocation. When `realloc` fails, `out_of_memory` (`BUFFER.C:13`) runs:

1. It frees `spare`, 32 KB kept back for this moment (allocated in
   `buf_new`), so that opening and writing files has memory to work with.
2. `plat_done` gives the screen back to DOS, so the messages can be read.
3. `ed_emergency_save` (`EDITOR.C:336`) writes each file with unsaved
   changes with `buf_emergency` (`BUFFER.C:687`): as `NAME.SAV` next to the
   original, or `UNTITLnn.SAV` for a file with no name. A name already there
   is never written over: `.SA0` to `.SA9` are tried next.
4. It prints where each file went, then `DE: out of memory`, and ends with
   exit code 3.

A `static int busy` flag makes sure the emergency save runs only once, even
if something inside it ran out of memory too.

Two allocations use plain `malloc`, and cope with failure themselves: `spare`
itself, and the `shown` copy of the screen in `take_screen`, without which
`plat_show` simply writes every row.

**Prevention.** `buf_load` refuses a file over 16 MB (`DE_MAX_FILE`) before
reading it, and leaves everything as it was.

**The test.** `test_big_and_memory` (`DETEST.C:1349`) runs the editor in a
child process (`fork`), sets `de_fail_alloc = 1` so the next `xrealloc`
fails, and then checks the exit code and every `.SAV` file.

---

## 9. How the tests work, and how to add one

### What `make test` does

`test/run.sh` does three things:

1. It type-checks `DOS.C` with `-fsyntax-only` against the stand-in headers
   in `test/stubs/`. This proves `DOS.C` still matches `DE.H`, but does not
   run it.
2. It compiles every other source file, plus `test/HOSTPLAT.C` and
   `test/DETEST.C`, with `-DDE_NO_MAIN -DDE_TEST`, and with the address and
   undefined-behaviour *sanitizers* (`-fsanitize=address,undefined`). These
   make the program stop with a report at the first out-of-bounds access,
   use of freed memory, or undefined arithmetic.
3. It runs the result in a temporary folder, and removes the folder.

The program prints each group's name, any failures, and a count at the end
(253 checks at `5f6fe6a`, all passing).

### How a test drives the editor

`HOSTPLAT.C` replaces the platform. Its `plat_key` reads from an array of
keys, `t_keys`, that the test fills; a mouse event is `DK_MOUSE` followed by
its event, row and column. Its `plat_show` copies the screen into
`t_screen`.

`test/HOSTPLAT.C:97-119`
```c
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
```

When the script runs out while a menu or dialog is still waiting, the screen
at that moment is saved in `t_snap` (so the test can look at the dialog),
and Esc is returned until every dialog has closed.

`DETEST.C` then plays the keys through the real main loop:

`test/DETEST.C:45-52`
```c
static void run(void)
{
    t_snapped = 0;
    while (t_kpos < t_nkeys && running)
        ed_step();
    ed_draw();
    t_nkeys = t_kpos = t_extra = 0;
}
```

Because the tests call `ed_step`, exactly what `main` calls, they test the
same code path a real key takes.

### The helpers

| Helper | What it does |
|--------|--------------|
| `fresh()` | Closes everything, sets the options to their defaults, opens one empty file. |
| `key(k)`, `keys(n, ...)` | Adds keys to the script. |
| `type("text\n")` | Adds a string as keys; `\n` becomes Enter. |
| `mouse_ev(ev, row, col)` | Adds a mouse event. |
| `run()` | Plays the script. |
| `text_is("...")` | Is the current file's text exactly this? (lines joined with `\n`) |
| `row_text(r)`, `attr_at(r, c)`, `col_of(r, s)` | The characters of screen row `r`; the attribute at a cell; where a string is on a row. |
| `snap_row(r)`, `snap_has(s)` | The same, on the screen a dialog left when the script ran out. |
| `path(name)`, `write_file`, `file_is` | Files in the test's working folder. |
| `CHECK(what, cond)` | Counts a pass, or prints `FAIL what (line N)`. |

On the 80 by 25 test screen, row 1 is the title, row 2 the first text
line, and row 24 the status line. With line numbers on and a short file,
the text starts at column 5.

### The invariant checks

Some functions exist only for the tests. Each checks something DE keeps
against the text itself, most by working it out again from scratch:

- `buf_check` (`BUFFER.C:432`): every line is sound, the cursor is inside
  the text.
- `words_check` (`SUGGEST.C:216`): the word index matches the text.
- `buf_state_check` (`BUFFER.C:494`): every kept syntax state is right.
- `ed_paint_check` (`EDITOR.C:1839`): the screen drawn with the row cache is
  the same as without it.

`test_random` (`DETEST.C:1003`) plays 40 rounds of 600 random keys from a
pool. After each key it runs `buf_check`, `words_check` and
`buf_state_check` (and `ed_paint_check` after every third key); at the end
of a round it undoes everything and redoes everything. A fixed seed
(`srand(1234)`) makes it repeatable.

### Adding a test

1. Write a function in `DETEST.C`:

   ```c
   static void test_my_feature(void)
   {
       puts("My feature");
       fresh();
       type("hello");
       keys(1, KM_CTRL | 'A');
       run();
       CHECK("Ctrl-A selects everything", files[cur]->sel && files[cur]->cx == 5);
   }
   ```

2. Call it from `main` (`DETEST.C:1479`), before `test_random()`.
3. Run `make test`. Write the check first and see it fail, then change the
   code and see it pass: then you know the test really tests the change.

For a feature that uses a file of a given language, give it the right
extension: `ed_open(path("X.PAS"), 0)` opens a new Pascal file, because the
language comes from the name.

---

## 10. How to change it: exercises

Each exercise names the function to change and the test to add. They go from
easy to harder. Run `make test` after each.

### Exercise 1: add an entry to the reference

`hypot(x, y)`, the length of the vector (x, y), is not in the reference.

- **Change:** `refs[]` in `REF.C`. Add a line after the `fmod` entry
  (`REF.C:107`), in the same form as the others:
  `{ L_C, "hypot", "<math.h>", "double hypot(double x, double y);", "..." },`
- **Test:** in `test_libraries`, add
  `CHECK("the reference has hypot", ref_find(L_C, "hypot", 5) != NULL);`
- **Notice:** nothing else changes. Suggestions, Ctrl+F1 and Shift+F1 find it,
  because they all read `refs[]`.

### Exercise 2: a new keyword for a language

C11's `_Noreturn` is not coloured as a keyword.

- **Change:** add ` _Noreturn` to the string `C_KEYWORDS` (`SYNTAX.C:187`).
  Keep a space on each side of every word in the string; `keys_add` splits
  on spaces.
- **Test:** in `test_brackets_and_colours`, write a file and check the
  colour, as the existing checks do:

  ```c
  write_file("KW.C", "_Noreturn void stop(void);\n", -1);
  ed_open(path("KW.C"), 0);
  run();
  CHECK("_Noreturn is a keyword", attr_at(2, col_of(2, "_Noreturn")) == attr[A_KEYWORD]);
  ```

- **Notice:** suggestions offer the new keyword too, because `lang_lists`
  (`SYNTAX.C:686`) hands the same strings to `SUGGEST.C`.

### Exercise 3: a new colour role

Assembly labels are coloured like directives (`A_PREPROC`). Give them their
own role.

- **Change, in three places:**
  1. `DE.H:73-80`: add `A_LABEL` just before `A_COUNT`.
  2. `SCREEN.C:21-30`: add an attribute at the end of each of the three rows
     of `rows`, for example `0x12` (green on blue). Update the column
     comments above them.
  3. `syn_asm` (`SYNTAX.C:315`): paint labels with `A_LABEL`.
- **Test:** the existing check "asm: a label" (`DETEST.C:395`) now fails,
  which is right: change it to `A_LABEL`.
- **Notice:** if you forget step 2, the program still compiles. C fills
  missing initialisers of an array with 0, so labels would be attribute
  `0x00`: black on black, invisible. Tables that must match an `enum` are
  worth checking by eye.

### Exercise 4: a new menu item

Add Edit > Uppercase, which makes the selected text upper case.

- **Change:**
  1. `DE.H:293-302`: add `C_UPPER` before `C_FILE1` (which must stay last).
  2. `m_edit` (`DIALOG.C:23`): add `{ "Upperca~s~e", "", C_UPPER },`. The
     letter `S` is not yet a hot key in that menu.
  3. `EDITOR.C`: write the function, above `ed_command`, and add
     `case C_UPPER: upper_case(b); break;` to `ed_command`
     (`EDITOR.C:1328`).

  ```c
  static void upper_case(Buffer *b)
  {
      int l1, c1, l2, c2, n, i;
      char *t;
      if (!has_sel(b))
          return;
      sel_range(b, &l1, &c1, &l2, &c2);
      t = buf_text(b, l1, c1, l2, c2, &n);
      for (i = 0; i < n; i++)
          t[i] = (char)toupper((unsigned char)t[i]);
      edit_begin(b, 0);
      buf_delete(b, l1, c1, l2, c2);
      buf_insert(b, l1, c1, t, n);
      free(t);
      b->sel = 0;
      edit_end(b);
  }
  ```

- **Test:**

  ```c
  fresh();
  type("abc def");
  keys(3, KM_CTRL | 'A', KM_ALT | 'E', 's');
  run();
  CHECK("Edit > Uppercase", text_is("ABC DEF"));
  keys(1, KM_CTRL | 'Z');
  run();
  CHECK("...one Ctrl-Z takes it back", text_is("abc def"));
  ```

- **Notice:** `edit_begin(b, 0)` starts one undo group, so the delete and the
  insert are undone together. This is how every command in `EDITOR.C` makes
  "one step" out of several changes.

### Exercise 5: a new key

Make Ctrl+J join the next line onto this one, with one space between.

- **Check first** that the key is free: Ctrl+J is not in `shortcuts[]`
  (`EDITOR.C:1454`), and `translate` (`DOS.C:368`) does deliver it as
  `KM_CTRL | 'J'` (the BIOS gives character 10 with the J key's scan code,
  not Enter's).
- **Change:** write `join_lines` in `EDITOR.C`, and add
  `case KM_CTRL | 'J': join_lines(b); break;` to the `switch` in `ed_key`
  (`EDITOR.C:1597`).

  ```c
  static void join_lines(Buffer *b)
  {
      if (b->cy >= b->n - 1)
          return;
      edit_begin(b, 0);
      b->sel = 0;
      buf_delete(b, b->cy, b->ln[b->cy].len, b->cy + 1, indent_of(&b->ln[b->cy + 1]));
      buf_insert(b, b->cy, b->cx, " ", 1);
      edit_end(b);
  }
  ```

  `buf_delete` leaves the cursor at the start of what it deleted, the old
  end of the line, which is where the space goes.
- **Test:**

  ```c
  fresh();
  type("one\n    two");
  keys(2, KM_CTRL | DK_HOME, KM_CTRL | 'J');
  run();
  CHECK("Ctrl-J joins the next line", text_is("one two"));
  ```

- **Also:** add it to the list of keys in `DE.TXT` (Editing, and Keys at a
  glance), and run `make` so `MANUAL.H` follows.

### Exercise 6: teach the colouring a new comment style

FreeBASIC has block comments, `/' ... '/`, which can cross lines. BASIC in DE
has no state yet (`syn_basic` takes none).

- **Change, in `SYNTAX.C`:**
  1. Add a state `S_BCOMMENT` to the `enum` at `SYNTAX.C:11`.
  2. Give `syn_basic` (`SYNTAX.C:402`) a `state` parameter, as
     `syn_pascal` has, and pass it in `syn_line` (`SYNTAX.C:676`).
  3. At the start of `syn_basic`: if `state == S_BCOMMENT`, look for `'/`.
     If it is not on the line, paint the whole line `A_COMMENT` and return
     `S_BCOMMENT`. If it is, paint up to and including it, and carry on
     from there.
  4. In the loop, check for `/'` *before* the existing check for `'`
     (`SYNTAX.C:407`), because `'` alone already starts a comment to the
     end of the line. On `/'`, look for `'/` the same way.
  `syn_pascal` (`SYNTAX.C:337`) is a good model for both steps.
- **Test:** in `test_brackets_and_colours`:

  ```c
  write_file("T.BAS", "/' a\nlong comment '/ PRINT 1\n", -1);
  ed_open(path("T.BAS"), 0);
  run();
  CHECK("basic: a /' comment '/ over two lines", attr_at(r + 1, col_of(r + 1, "long")) == attr[A_COMMENT]);
  CHECK("...and code after it", attr_at(r + 1, col_of(r + 1, "PRINT")) == attr[A_KEYWORD]);
  ```

- **Notice:** you change only `SYNTAX.C`. The per-line state cache
  (chapter 6), suggestions ("no suggestions in a comment") and symbols
  ("skip comment lines") all follow, because they all go through
  `syn_line`. The state is stored in one `unsigned char` per line, so there
  is room for 256 states.
- **Harder still:** the random test only edits a C file (`RAND.C`). Make it
  also run some rounds on a `.BAS` file, so `buf_state_check` covers the new
  state.

---

## 11. Glossary

**Array.** A row of values of the same type, one after another in memory.
`a[i]` is the value `i` places from the start, and the first is `a[0]`.

**Pointer.** A variable that holds the address of something in memory.
`*p` is the thing it points at, and `p + 1` is the next one along.

**String (C).** An array of `char` ending with a 0 byte, which marks its end.
Functions such as `strlen` and `strcmp` rely on that 0.

**struct.** A type made of named parts, such as `Line` with `s`, `len`,
`cap` and `stamp`. `L->len` reaches a part through a pointer `L`.

**typedef.** A new name for a type. `typedef struct {...} Line;` lets the
code write `Line` instead of the whole struct.

**enum.** A list of named integer constants, numbered 0, 1, 2 ... unless
given values. DE uses them for roles (`A_`), keys (`DK_`), commands (`C_`)
and states (`S_`).

**static.** Outside a function, `static` makes a name private to its file.
Inside a function, it makes a variable keep its value between calls, as
`busy` in `out_of_memory` does.

**Header file.** A `.H` file of declarations that `#include` copies into a
source file. `DE.H` is how the files of DE agree on names and types.

**Macro, `#define`, `#ifdef`.** The preprocessor replaces a macro's name
with its text before compiling. `#ifdef NAME` keeps a block of code only
when `NAME` is defined, as `DE_TEST` and `DE_NO_MAIN` are for the tests.

**malloc, realloc, free.** The C library's functions for memory taken while
the program runs. `realloc` makes a block bigger or smaller, possibly
moving it; `free` gives it back; either may fail and return `NULL`.

**memmove, memcpy, memcmp.** Copy or compare a number of bytes.
`memmove` is safe when the two areas overlap, which is why DE uses it to
shift a line's text.

**Function pointer.** A variable that holds a function, so code can call
"whatever function was given". `Pick` in `DE.H` uses them.

**Undefined behaviour.** Something the C standard gives no meaning to, such
as reading past an array or overflowing a signed `int`. The program may seem
to work, or fail anywhere; the sanitizers catch many cases.

**unsigned arithmetic.** For `unsigned` integers, C defines overflow to wrap
around modulo 2ⁿ. Hash functions rely on this.

**Bit operations.** `a | b` sets the bits of either, `a & b` keeps the bits
in both, `a << 8` shifts left by 8 bits (multiplies by 256). DE uses them
for key modifiers and screen cells.

**Hash table.** A table where an item's place is computed from the item
itself, by a hash function. Finding an item then takes about one look, not a
search through everything.

**Recurrence.** A sequence where each value is computed from the one before.
The syntax states are one: the state after a line comes from the line and
the state before it.

**DOS.** The disk operating system of the IBM PC: one program at a time, a
text screen, and 640 KB of directly usable memory. DE runs on it, in
DOSBox-X under DOSDev or on a real PC.

**BIOS.** The firmware built into a PC, with routines for the screen,
keyboard and disks. Programs call it through interrupts.

**Interrupt (INT).** In DOS programming, a numbered entry point into the
BIOS or DOS, such as INT 10h (video), 16h (keyboard) and 33h (mouse). The
register `AH` or `AX` chooses the function.

**Real mode and protected mode.** Real mode is the 8086's original 16-bit
mode, where the BIOS runs, limited to 1 MB. Protected mode is the 386's
32-bit mode, which DJGPP programs use for all the memory they need.

**DPMI, CWSDPMI.** DPMI is the standard way for a DOS program to run in
protected mode and still call real-mode code. CWSDPMI is the free DPMI
server DJGPP programs use.

**DJGPP.** A port of GCC and its tools to DOS, producing 32-bit protected
mode programs. DE is compiled with it.

**BIOS data area.** Memory at segment 40h (address 400h) where the BIOS
keeps its state: the keyboard buffer, the shift keys, the clock tick count.
DE reads it directly to avoid slow BIOS calls.

**Segment.** In real mode, an address is a segment times 16 plus an offset.
Segment 40h, offset 17h is address 417h.

**Text mode.** A video mode where the screen is a grid of characters, not
pixels. Each cell has a character and an attribute.

**Attribute.** The byte that gives a text cell its colours: foreground plus
16 times background, each a number from 0 to 15.

**Code page 437.** The character set of the original PC. It includes the
line and box characters DE draws frames with.

**Scan code.** The number of the physical key pressed, as the keyboard
reports it. Keys with no character, such as F9, are told apart by it.

**VGA DAC.** The part of a VGA card that turns colour numbers into the red,
green and blue sent to the monitor. Its registers can be reloaded, which is
how DE changes what the 16 colours look like.

**CRLF and LF.** The bytes that end a line: carriage return and line feed
(13, 10) on DOS, line feed alone on Linux. DE keeps whichever a file uses.

**Exit code.** The number a program returns when it ends; 0 usually means
success. DE ends with 3 when memory ran out.

**Standard output and standard error.** The two output streams a program
writes text to, file descriptors 1 and 2. `dup2` can point them at a file,
which is how DE captures a build's messages.

**DOSBox-X.** A program that emulates a PC running DOS. DOSDev runs DE and
your programs in it.

**Sanitizer.** A compiler option that adds checks to a program, such as
AddressSanitizer for bad memory use. DE's tests run with two of them.

---

## 12. Rough edges

These are places where the code is harder to follow than it needs to be,
where logic is repeated, or where there are limits a reader should know
about. They were found while writing this tour, and each was checked in the
code.

### A behaviour that looks wrong

- **"Keep mine" after a run remembers the time but not the size.** In
  `ed_check_disk`, answering Keep mine updates only `disk_time`
  (`EDITOR.C:449-451`). But `buf_changed_on_disk` compares the size as well
  (`BUFFER.C:428`). So if the other program also changed the file's size,
  DE asks again the next time it comes back from a run, although the comment
  says "not asked again for this change". The test "it is not asked again for
  the same change" (`DETEST.C:1310`) passes only because the run writes
  `again\n`, the same size as the `fresh\n` that was there before. Setting
  `disk_size` there as well (with the same `file_size` that `buf_save`
  uses) would make the two agree.

### Comments that are out of date

- `SCREEN.C:32-33` says the palettes "must match program/src/theme.rs;
  programs/de-test/run.sh checks". Those are paths in DOSDev's repository,
  and `CHANGELOG.md` (0.6) says that check is no longer here.
- `DIALOG.C:1` says the file holds "the help screen". The help viewer is in
  `HELP.C`.
- The comment above `newest` (`PROJECT.C:260`) describes `up_to_date`, the
  function after it.

### Code that is never used

- `proj_run` (`PROJECT.C:361`, declared at `DE.H:229`) is never called.
  Ctrl+F9 goes through `proj_build(1)`.

### The same thing written twice

None of these is wrong, but a change to one must be made to the other by
hand:

- `join` exists twice, with different behaviour: `DIALOG.C:431` (always joins,
  cuts to `DE_PATH`) and `PROJECT.C:27` (passes an absolute `name` through
  unchanged).
- `is_dir`: `DIALOG.C:445` and `PROJECT.C:60`.
- Extension lists: `ext_is` (`SYNTAX.C:17`) and `ext_in` (`PROJECT.C:66`) do
  the same job, and the source extensions in `lang_of` (`SYNTAX.C:47-60`)
  are repeated in `is_source` (`PROJECT.C:90`). A new language's extension
  must be added in both, or its compiler messages will not be recognised.
- "Does it contain a capital?": `has_upper` (`EDITOR.C:1102`) and
  `has_capital` (`PROJECT.C:978`).
- Case-insensitive "contains": `has_text` (`HELP.C:180`) and `contains`
  (`SYMBOLS.C:320`) are the same function.
- The list window: `window` (`DIALOG.C:651`) and `page` (`HELP.C:43`) are
  almost the same.
- The FNV-1a hash: `word_hash` (`SYNTAX.C:85`) and `hash` (`SUGGEST.C:64`).
- `SYNTAX.C` has two hash tables of words: `WordSet` with `in_list`
  (`SYNTAX.C:79-157`), used only to recognise `rem`, and `KeyTable` with
  `word_role` (`SYNTAX.C:573-667`), used for everything else.
- Taking the extension off a name: `temp_name` (`BUFFER.C:594`) and
  `buf_emergency` (`BUFFER.C:687`).
- The word at the cursor: `ed_word` (`EDITOR.C:301`) and the start of
  `help_lookup` (`HELP.C:267`).
- The cursor's place on the screen: `ed_cursor` (`EDITOR.C:1854`) and the
  first lines of `ed_draw` (`EDITOR.C:1872`).
- Keeping the cursor inside the text: `sane` (`EDITOR.C:150`), the start of
  `follow` (`EDITOR.C:191-194`) and `edit_end` (`EDITOR.C:175`).
- Two "save all"s: File > Save all (`C_SAVEALL`, `EDITOR.C:1342`) asks for a
  name for untitled files and stops at the first failure, while
  `ed_save_all` (`EDITOR.C:509`, used before a build) skips untitled files
  and goes on. `C_SAVEALL` also does not make the file being saved the
  current one, so its Save dialog can appear over a different file.

### Names that make searching harder

- Two different types are called `Entry`: the word index entry
  (`SUGGEST.C:82`) and the Open dialog's folder entry (`DIALOG.C:413`). Two
  `static` functions are called `add` (`SUGGEST.C:250`, `SYMBOLS.C:49`) and
  two `number` (`SYNTAX.C:168`, `PROJECT.C:491`). This is legal, because
  `static` keeps each private to its file, but a search finds both.
- The current buffer has two macro names: `B` (`EDITOR.C:23`) and `B_CUR`
  (`DE.H:259`). `TH()` (`EDITOR.C:25`) is a function, though its capitals
  make it look like a macro.

### Code that is clever where it could be plain

- **Home and End in a menu** (`DIALOG.C:192-199`): Home sets `sel` to the
  last item and then steps forward one (wrapping to the first); End sets it
  to the first and steps back one. It works, but it is not obvious.
- **The syntax state bookkeeping** (`touched` and `buf_state`,
  `BUFFER.C:106-116` and `BUFFER.C:452-491`) uses three numbers whose
  meanings interact. Chapter 6 explains them; the code's comments are
  short. The part that reuses old states after an edit is guarded only by
  the random test, through `buf_state_check`, and that test edits only a C
  file, so the two Pascal comment states are not exercised by it.
- **The manual's contents page** starts the introduction at `MANUAL[3]`
  (`HELP.C:71`): it assumes `DE.TXT` starts with a title, a line of `=`, and
  a blank line.

### Limits a reader should know

- **Fixed sizes.** 16 open files (`MAXFILES`), 2,000 messages (`MAXMSGS`),
  4,000 symbols (`MAXSYMS`), 64 definitions (`MAXDEFS`), 40 manual topics
  (`MAXTOPICS`), 32 places for Alt+Left (`MARKS`), 8 Project > Commands
  folders (`OVERRIDES`). Words longer than 39 bytes are not suggested
  (`WORD_MAX`), and keywords longer than 31 are not recognised. Find in files
  reads files up to 1 MB, 4,000 files, 4 folders deep; Go to definition
  reads up to 1,000 files, 3 folders deep. Build and run commands hold 159
  characters.
- **Wide screens.** The row cache holds 132 columns (`COLS`,
  `EDITOR.C:1669`). On a screen more than 133 columns wide every line is
  drawn every time, which is correct but slower. The saved DOS screen holds
  at most 132 by 60 cells (`MAXCELLS`, `DOS.C:26`); a bigger one is not
  saved, and DE clears the screen instead of putting it back
  (`put_user_screen`, `DOS.C:162`).
- **Memory that only grows.** The undo history keeps every change since the
  file was opened; there is no limit. The word index never removes a word: a
  word whose count falls to 0 stays as an unused entry (`SUGGEST.C:85`).
  Typing a word letter by letter adds each prefix of two letters or more
  (`pr`, `pri`, `prin` ...) as an entry along the way. Both are reset only
  when the file is read again (and the word index also when Save as changes
  the file's language).
- **Syntax colouring is by scanning, not parsing.** Only block comments
  carry a state from line to line. A C string or `//` comment continued
  with a backslash at the end of the line is not followed onto the next line.
  A Pascal `{$ ... }` directive that crosses a line break is not either. C's
  keyword list also contains C++ words (`class`, `new`, `delete`), so they
  are coloured in C files too.
- **Messages and inserted lines.** `msgs_shift` moves messages on lines
  *after* the changed line (`PROJECT.C:702`). A message on the very line
  where Enter is pressed stays on that line, even if its text moved down.
- **Running out of memory inside a multi-line insert.** `raw_insert` opens
  the gap in the line array (`BUFFER.C:188-189`) before it fills the new
  lines. If memory runs out while it fills them, the gap's slots still hold
  copies of the `Line` structs that were moved, and the end of the split
  line (kept in `tail`) has not been put back. The emergency `.SAV` file is
  then written from that state, and may show a few lines twice or miss the
  end of one line. `write_lines` already copes with lines whose text is
  `NULL` (`BUFFER.C:621`), so this does not crash.
- **Comparing pointers that may be `NULL`.** Several places find the last
  `/` or `\` in a path and pick the later one with a test such as
  `if (sep2 > sep)` (`BUFFER.C:600`, `BUFFER.C:698`, `SYNTAX.C:23`, `PROJECT.C:41`,
  `SYMBOLS.C:513`), where either may be `NULL`. Standard C only defines `<`
  and `>` between pointers into the same array, so this is technically
  undefined behaviour. It works with GCC on a flat address space, which is
  all DE runs on.
- **The clock.** `plat_ms` comes from the BIOS tick count, which goes back to
  0 at midnight. A `DE /SPEED` run across midnight would time wrongly.
- **What the tests do not cover.** `DOS.C` is only type-checked, so key
  translation, the mouse, the palette and running programs are tested only
  by using DE. The tests write their options file to a fixed path,
  `/tmp/DE-test.CFG` (`DETEST.C:1485`), and `run.sh` uses `set -e`, so a
  failing run leaves its temporary folder behind.
- **`MANUAL.H` must follow `DE.TXT`.** Building with `BUILD.BAT` uses the
  `src/MANUAL.H` that is in the folder. If `DE.TXT` was edited and `make` was
  not run, the manual inside that `DE.EXE` is the old one.
