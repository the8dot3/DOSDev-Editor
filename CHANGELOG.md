# Changelog

## 0.7

- C++ is a language of its own. `.CPP`, `.CXX`, `.CC`, `.HPP`, `.HH` and
  `.HXX` files are C++, in any case. A `.H` file is C unless its first
  4 KB have a line that starts with `class`, `namespace`, `template` or
  `#include <name>` with no dot in the name; it is looked at again when it
  is saved. The status line says `C++`.
- `.C` files no longer colour C++'s words (`class`, `new`, `template` and
  the rest) as keywords.
- C++ colours C++17's keywords, the C++20 ones in common use, the
  alternative words (`and`, `not` ...), `nullptr`, the casts, and
  `override` and `final`. `import` and `module` stay names. A raw string,
  `R"x( ... )x"`, is coloured over as many lines as it takes. DE keeps
  251 delimiters; after that a new one ends at the first `)` followed by
  up to 16 delimiter characters and `"`. Digit
  separators, exponents, prefixes such as `u8` and suffixes such as `"x"s`
  and `10_km` are part of their number or string.
- Ctrl-L and F12 find C++'s classes, structs, enums, namespaces, `using`
  names, and functions in namespaces and class bodies. A method is listed
  as `Foo::bar`, whether it is written in the class or outside it;
  constructors, destructors and operators too. Only the innermost class
  is written, and no namespace. Constructors' member initialiser lists
  are not taken for functions, members initialised in braces (`a_{1}`)
  included. `#if`/`#else` branches that each open a brace no longer lose
  the symbols after them, `#if 0` blocks are passed over, and a `#` line
  inside a comment is not taken as a directive. F12 on `bar` finds
  `Foo::bar`, looks in C files from C++ and the other way round, and opens
  DJGPP's `<vector>` and the like from `DJDIR\INCLUDE\CXX`.
- The reference has 119 entries for the everyday C++ library, each with
  its header. A C++ file sees C's entries too. After `std::`, suggestions
  are the reference's C++ names alone. After `.` or `->`, the reference's
  free functions are not suggested or shown (`s.find(` is not
  `std::find`). There is no `<thread>`.
- F9 on a `.CPP` with no project offers `gpp -Wall -g -o NAME.EXE -x c++
  NAME.CPP`.
- g++'s `required from here` lines are notes, with their place, so F8
  goes to where your code used a template. F8 and Shift-F8 skip notes
  about files outside the project, such as g++'s notes in the C++
  library's headers; F7 still lists them. With no `project.ini`, the
  project is only the file's own folder, not the folders in it. The source lines GCC 9 and
  later show under a message are text, even when they say `error:`. An
  error in a C++ library header (`vector`, `bits/vector.tcc`) has its
  place.
- Keep mine, after a run that changed a file's size, is not asked again
  for the same change. DE took the file's new time but not its new size.
- The syntax state of a file's first line was never set: it was whatever
  the memory held, and a stray value could colour the first line as a
  comment.
- The random-edit test also edits a C++ file and a Pascal file.

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
