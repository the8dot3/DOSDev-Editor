# Changelog

## 0.6

- DE no longer saves over a file another program changed. Saving asks when
  the file changed on disk since DE read or saved it: Reload, or Keep mine.
- When DE comes back from running a program or a build, it looks at every
  open file. A file with no unsaved changes that changed on disk is read
  again; one with unsaved changes asks.
- Tab in a Makefile (`Makefile`, `*.MAK`, `*.MK`) types a tab character,
  whatever Options > Tab key inserts says.
- A file changed on disk is also noticed by its size, as DOS file times
  have 2-second steps.
- DE opens files up to 16 MB. A larger file is refused with a message.
- If memory runs out, DE writes each file with unsaved changes as
  `NAME.SAV` before it ends, and says so. A file with no name is written as
  `UNTITLnn.SAV`. It never writes over a file already there: it uses
  `.SA0`, `.SA1` and so on.
- `DE /CHECK FILE` starts DE and stops it with no keyboard, writes
  `DE 0.6 ok` in FILE and ends with exit code 0. A build can use it to see
  that DE.EXE starts.
- `BUILD.BAT` now makes `BUILD\DE.EXE`, where the Linux build puts it.
- The build has no warnings with GCC 12.2 and `-Wall`.
- `make OUT=FOLDER` does not write into the source folder when
  `src/MANUAL.H` already has the content `DE.TXT` gives.
- DE has its own repository. The sources are in `src/`, the tests in `test/`.
- `make` builds into the folder named by `OUT` (`build` by default), and has
  `test` and `package` targets.
- The test that compared DE's palettes with DOSDev's `theme.rs` is gone from
  the tests here. DOSDev owns that check.

## 0.5

- DE follows DOSDev's theme. Every DOSBox-X start in DOSDev sets
  `DOSDEV_THEME` and `DOSDEV_PALETTE`; DE reads the palette.
- Options > Colours: Follow DOSDev (the default), Borland blue, Midnight,
  Paper or Solar. Following DOSDev sets the 16 text colours through the VGA
  BIOS. A missing or malformed palette leaves the colours alone, and DE
  stays in its own Borland blue.
- DE puts the colours it found back while a program it runs has the screen,
  and when it ends.
- Dark palettes use red dialog hot keys. A light one (Paper) has its own
  colour roles.
- `DE.CFG` saves the theme by name (`colours=`). DE 0.4's numbers are still
  read.

## 0.4

- DE knows the SDL3 and PDCurses functions: suggestions, and Ctrl-F1. The
  reference has 53 SDL3 entries and 51 for PDCurses.
- F12 on `#include <SDL3/SDL.h>` opens the header from the project's
  `LIBS/<NAME>/INCLUDE` folders. Find in files leaves `LIBS` out.
- Project > Debug runs the project's debugger (`debug =` in `project.ini`),
  building first if a source is newer than the program.
- The manual has a Libraries topic.
- DOSDev opens DE for F4 and for errors, at the line, when `BIN\DE.EXE` is
  on E:.

## 0.1 to 0.3

DOSDev's changelog does not describe these versions. DE 0.3 came with DOSDev
kit 0.14.1, which is where this history starts.
