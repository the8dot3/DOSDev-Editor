#!/bin/sh
# DE's tests on Linux: the editor's own code, driven by scripted keys, under
# the address and undefined-behaviour sanitizers. DOS.C is only type-checked,
# against stand-ins for DJGPP's headers (stubs/).
#   programs/de-test/run.sh
set -e
T="$(cd "$(dirname "$0")" && pwd)"; D="$T/../de"; W="$(mktemp -d)"
gcc -x c -std=gnu99 -Wall -Wextra -fsyntax-only -I"$T/stubs" -I"$D" "$D/DOS.C"
gcc -x c -std=gnu99 -g -O1 -Wall -Wextra -fsanitize=address,undefined -DDE_NO_MAIN -I"$D" -o "$W/detest" \
    "$D/BUFFER.C" "$D/SYNTAX.C" "$D/SCREEN.C" "$D/EDITOR.C" "$D/SUGGEST.C" "$D/HELP.C" "$D/REF.C" "$D/DIALOG.C" "$D/PROJECT.C" "$D/SYMBOLS.C" "$D/MAIN.C" "$T/HOSTPLAT.C" "$T/DETEST.C"
"$W/detest" "$W/files"
# DE's copies of DOSDev's palettes must match DOSDev's own (theme.rs).
for p in MIDNIGHT PAPER SOLAR; do
  a="$(awk -v p="$p" '$0 ~ "const " p ":" { on = 1; next } on && /\];/ { exit } on' "$T/../../program/src/theme.rs" | grep -o '0x[0-9a-f]\{6\}' | tr '\n' ' ')"
  b="$(awk -v p="$p" 'index($0, "/* " p " */") { on = 1 } on { print } on && /}/ { exit }' "$D/SCREEN.C" | grep -o '0x[0-9a-f]\{6\}' | tr '\n' ' ')"
  [ -n "$a" ] && [ "$a" = "$b" ] || { echo "FAIL  DE's $p palette differs from DOSDev's:"; echo "  DOSDev: $a"; echo "  DE:     $b"; exit 1; }
done
echo "  ok    DE's palettes are DOSDev's"
rm -rf "$W"
