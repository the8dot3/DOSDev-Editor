# DE, the DOSDev Editor: built from Linux with DOSDev's DJGPP cross compiler.
# DOSDev runs this for Projects > Build (F2); in DOSDev's Terminal, type make.
#   make        DE.EXE, and DE.ZIP: DE as a FreeDOS-style package, ready to
#               add with Modules > Add software
#   make clean  removes both
# GCC takes .C for C++, so -x c says the sources are C.
.RECIPEPREFIX = >
CC     = i586-pc-msdosdjgpp-gcc
STRIP  = i586-pc-msdosdjgpp-strip
CFLAGS = -Wall -O2 -g
SRC    = MAIN.C EDITOR.C SUGGEST.C PROJECT.C SYMBOLS.C HELP.C REF.C DIALOG.C BUFFER.C SYNTAX.C SCREEN.C DOS.C

all: DE.EXE DE.ZIP

DE.EXE: $(SRC) DE.H MANUAL.H
> $(CC) $(CFLAGS) -o DE.EXE -x c $(SRC)

# The manual inside DE (F1) is DE.TXT, as C strings. MANUAL.H comes with
# the project, so BUILD.BAT, in DOS, has it too.
MANUAL.H: DE.TXT
> awk 'BEGIN { print "/* Made from DE.TXT by make: change DE.TXT, not this. */"; print "static const char *const MANUAL[] = {" } { gsub(/\r/, ""); gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\"," } END { print "    0"; print "};" }' DE.TXT > MANUAL.H

# The package, laid out as FreeDOS packages are: APPINFO\DE.LSM describes
# it, BIN\DE.EXE goes on E:\BIN (the Workbench's PATH), and the manual and
# the source go to Docs and sources. The packaged DE.EXE is stripped of its
# debugging information; the project's own keeps it, for GDB (F6).
DE.ZIP: DE.EXE DE.LSM DE.TXT
> rm -rf PKG DE.ZIP
> mkdir -p PKG/APPINFO PKG/BIN PKG/DOC/DE PKG/SOURCE/DE
> cp DE.LSM PKG/APPINFO/
> cp DE.EXE PKG/BIN/
> $(STRIP) PKG/BIN/DE.EXE 2>/dev/null || true
> cp DE.TXT PKG/DOC/DE/
> cp $(SRC) DE.H MANUAL.H Makefile BUILD.BAT DE.LSM DE.TXT project.ini PKG/SOURCE/DE/
> cd PKG && { 7zz a -tzip -bso0 -bsp0 ../DE.ZIP APPINFO BIN DOC SOURCE >/dev/null 2>&1 || zip -qr ../DE.ZIP APPINFO BIN DOC SOURCE 2>/dev/null || python3 -m zipfile -c ../DE.ZIP APPINFO BIN DOC SOURCE; }
> rm -rf PKG
> @echo "DE.ZIP is ready: add it with Modules > Add software."

clean:
> rm -rf DE.EXE DE.ZIP PKG
# MANUAL.H stays: BUILD.BAT needs it, and DE.TXT makes it again when it changes.

.PHONY: all clean
