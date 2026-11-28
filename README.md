# DOSDev-Editor

DE is a colour text editor and IDE for DOS programmers, in the style of the
Borland IDEs. It builds and runs your programs, colours C, assembly, Pascal
and BASIC, suggests words as you type, and knows the SDL3 and PDCurses
functions. It is the editor of DOSDev, a DOS development environment for Linux,
and it also works on its own in any DOS system with a 386 or newer and a DPMI
server (CWSDPMI).

The manual is `DE.TXT`. It is also inside DE: press F1.

## Building

DE is written in C99 for DJGPP.

On Linux, with a DJGPP cross compiler (`i586-pc-msdosdjgpp-gcc`) on `PATH`:

    make            builds build/DE.EXE
    make test       runs the host tests
    make package    builds build/DE.ZIP
    make clean      removes the output folder

`make CC=...` names another compiler. `make OUT=...` names another output
folder (`build` by default). `make package` needs a zip tool: 7zz, zip or
python3.

In DOS, with DJGPP, run `BUILD.BAT`. It builds `DE.EXE` from `src\`.

`src\MANUAL.H` is made from `DE.TXT` by `make`. It stays in the repository
because `BUILD.BAT` needs it.

## Tests

`make test` compiles DE's own code for Linux, under the address and
undefined-behaviour sanitizers, and drives it with scripted keys. `DOS.C`,
which talks to the DOS machine, is only type-checked, against stand-ins for
DJGPP's headers (`test/stubs`). The tests need only gcc and a shell, and use
no network.

## Layout

    src/        the source, and MANUAL.H
    test/       the host tests
    DE.TXT      the manual
    DE.LSM      the package description
    BUILD.BAT   the build in DOS
    project.ini the DOSDev project
    CONTRACT.md how DE and DOSDev work together

## Licence

GPL version 2 or later. See `LICENSE`.
