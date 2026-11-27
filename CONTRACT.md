# How DE and DOSDev work together

DE runs by itself. DOSDev starts it, and DE follows what DOSDev tells it in
these ways. DOSDev's side of each is in DOSDev's documentation.

## Command line

    DE [FILE ...]
    DE +LINE FILE
    DE FILE +LINE

DOSDev opens a file at a line with `DE +LINE FILE`, for F4 and for the
errors a build lists. `DE /SPEED FILE` times the editor and is not used by
DOSDev.

    DE /CHECK FILE

`DE /CHECK FILE` is for a build to prove that the DE.EXE it made starts. It
needs no keyboard. DE sets the screen up as at a normal start, puts it back,
writes `DE 0.6 ok` and a line feed (not a carriage return) in FILE, and
ends with exit code 0. If it cannot start, or cannot write FILE, it says why
on the error output and ends with exit code 1. The number is DE's version.

## Environment

DOSDev sets these in every DOSBox-X start:

- `DOSDEV_PALETTE`: 96 hex digits, the red, green and blue bytes of the 16
  text colours. DE 0.5 and later read it when Options > Colours is Follow DOSDev. DE
  sets the colours through the VGA BIOS, and puts back the ones it found
  when it ends and while a program it runs has the screen. A value that is
  missing, or is not 96 hex digits, is not used.
- `DOSDEV_THEME`: the name of DOSDev's theme. DE does not read it.

DE also reads `INCLUDE` and `DJDIR` to find headers for F12 on an
`#include` line.

## project.ini

DE finds the project as the folder with `project.ini`, looking from the
file's folder upwards. It reads only these keys, from any section:

| Key        | Use in DE                                                        |
|------------|------------------------------------------------------------------|
| `name`     | The project's name                                               |
| `build_on` | `dos`: F9 uses `build`. Anything else: F9 uses `BUILD.BAT`       |
| `build`    | The build command, when `build_on = dos`                         |
| `run`      | The command Ctrl-F9 runs                                         |
| `output`   | The program the build makes, checked after a build               |
| `debug`    | The command Project > Debug runs                                 |

Lines that start with `#`, `;` or `[` are skipped. Commands set in Project >
Commands come before `project.ini`, until DE ends.

## BUILD.BAT

A project folder with a `BUILD.BAT` is built by it, in the Workbench, when
`project.ini` does not name a DOS build. DE shows the compiler's messages and
F7 and F8 go to each. DE's own `BUILD.BAT` makes `BUILD\DE.EXE`.

## LIBS/NAME/INCLUDE

A project keeps its libraries in `LIBS\NAME`, with their headers in
`LIBS\NAME\INCLUDE`. F12 on an `#include` looks in the file's folder, the
project's folder, each of these `INCLUDE` folders, the folders on `INCLUDE`,
and `DJDIR\include`, in that order. Find in files leaves `LIBS` out.

## DE.CFG

DE keeps its options in `DE.CFG`, in the folder `DE.EXE` is in. It is a text
file of `key=value` lines: `numbers`, `spaces`, `indent`, `tab`,
`autoindent`, `colours`, `suggest` and `fastkeys`. DOSDev does not write it.

## Build interface

DOSDev builds DE with `make -C <source> CC=i586-pc-msdosdjgpp-gcc
OUT=<folder>`, and takes `<folder>/DE.EXE`. `OUT` and `CC` are the Makefile
variables that name the output folder and the compiler.
