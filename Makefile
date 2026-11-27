# DE, the DOSDev Editor, built from Linux with a DJGPP cross compiler.
#   make          build/DE.EXE
#   make test     the host tests (DE's own code, on Linux)
#   make package  build/DE.ZIP: DE as a FreeDOS-style package, ready to
#                 add with Modules > Add software
#   make clean    removes the output folder
# The compiler is found on PATH; name another with CC=. The output folder
# is OUT (build by default): make OUT=/tmp/de-out.
# GCC takes .C for C++, so -x c says the sources are C.
.RECIPEPREFIX = >
CC     = i586-pc-msdosdjgpp-gcc
STRIP  = i586-pc-msdosdjgpp-strip
CFLAGS = -Wall -O2 -g
OUT    = build
SRC    = $(addprefix src/,MAIN.C EDITOR.C SUGGEST.C PROJECT.C SYMBOLS.C HELP.C REF.C DIALOG.C BUFFER.C SYNTAX.C SCREEN.C DOS.C)

all: $(OUT)/DE.EXE

$(OUT)/DE.EXE: $(SRC) src/DE.H $(OUT)/MANUAL.H
> mkdir -p $(OUT)
> cmp -s $(OUT)/MANUAL.H src/MANUAL.H || cp $(OUT)/MANUAL.H src/MANUAL.H
> $(CC) $(CFLAGS) -o $@ -x c $(SRC)

# The manual inside DE (F1) is DE.TXT, as C strings. src/MANUAL.H is kept
# in the repository, so BUILD.BAT, in DOS, has it too. It is made in OUT
# and copied over src/MANUAL.H only when its content differs, so building
# from a source folder that can't be written (or whose files have any
# times at all) never touches it.
$(OUT)/MANUAL.H: DE.TXT
> mkdir -p $(OUT)
> awk 'BEGIN { print "/* Made from DE.TXT by make: change DE.TXT, not this. */"; print "static const char *const MANUAL[] = {" } { gsub(/\r/, ""); gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); print "    \"" $$0 "\"," } END { print "    0"; print "};" }' DE.TXT > $@

test:
> sh test/run.sh

# The package, laid out as FreeDOS packages are: APPINFO\DE.LSM describes
# it, BIN\DE.EXE goes on E:\BIN (the Workbench's PATH), and the manual and
# the source go to Docs and sources. The packaged DE.EXE is stripped of its
# debugging information; the build's own keeps it, for GDB (F6).
package: $(OUT)/DE.ZIP

$(OUT)/DE.ZIP: $(OUT)/DE.EXE DE.LSM DE.TXT
> rm -rf $(OUT)/PKG $@
> mkdir -p $(OUT)/PKG/APPINFO $(OUT)/PKG/BIN $(OUT)/PKG/DOC/DE $(OUT)/PKG/SOURCE/DE/SRC
> cp DE.LSM $(OUT)/PKG/APPINFO/
> cp $(OUT)/DE.EXE $(OUT)/PKG/BIN/
> $(STRIP) $(OUT)/PKG/BIN/DE.EXE 2>/dev/null || true
> cp DE.TXT $(OUT)/PKG/DOC/DE/
> cp $(SRC) src/DE.H src/MANUAL.H $(OUT)/PKG/SOURCE/DE/SRC/
> cp Makefile BUILD.BAT DE.LSM DE.TXT project.ini $(OUT)/PKG/SOURCE/DE/
> cd $(OUT)/PKG && { 7zz a -tzip -bso0 -bsp0 ../DE.ZIP APPINFO BIN DOC SOURCE >/dev/null 2>&1 || zip -qr ../DE.ZIP APPINFO BIN DOC SOURCE 2>/dev/null || python3 -m zipfile -c ../DE.ZIP APPINFO BIN DOC SOURCE; }
> rm -rf $(OUT)/PKG
> @echo "$@ is ready: add it with Modules > Add software."

clean:
> rm -rf $(OUT)
# src/MANUAL.H stays: BUILD.BAT needs it, and the build makes it again when DE.TXT changes.

.PHONY: all test package clean
