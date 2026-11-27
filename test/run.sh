#!/bin/sh
# DE's tests on Linux: the editor's own code, driven by scripted keys, under
# the address and undefined-behaviour sanitizers. DOS.C is only type-checked,
# against stand-ins for DJGPP's headers (stubs/).
#   sh test/run.sh      (or: make test)
set -e
T="$(cd "$(dirname "$0")" && pwd)"; D="$T/../src"; W="$(mktemp -d)"
gcc -x c -std=gnu99 -Wall -Wextra -fsyntax-only -I"$T/stubs" -I"$D" "$D/DOS.C"
gcc -x c -std=gnu99 -g -O1 -Wall -Wextra -fsanitize=address,undefined -DDE_NO_MAIN -I"$D" -o "$W/detest" \
    "$D/BUFFER.C" "$D/SYNTAX.C" "$D/SCREEN.C" "$D/EDITOR.C" "$D/SUGGEST.C" "$D/HELP.C" "$D/REF.C" "$D/DIALOG.C" "$D/PROJECT.C" "$D/SYMBOLS.C" "$D/MAIN.C" "$T/HOSTPLAT.C" "$T/DETEST.C"
"$W/detest" "$W/files"
rm -rf "$W"
