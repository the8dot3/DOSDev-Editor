# DE 0.7 review, round 1: fix all of these (test first)

CRITICAL
1. Stack buffer overflow in add_scoped (src/SYMBOLS.C ~197-199): `n += snprintf(t + n, sizeof t - n, ...)` adds what snprintf would have written; once n > 80, `sizeof t - n` wraps and the next class name is written past t[80]. Reproduced under the sanitizer with three nested classes whose first two names are about 39 characters each and a method inside. Reached by Ctrl+L, F12 and F12's project scan. Fix: stop appending when the buffer is full (never let n pass sizeof t - 1). Add the reproduction as a test, and look for the same pattern (n += snprintf with sizeof - n) everywhere else in src/ and fix each.

IMPORTANT
2. Multi-line constructor initialiser lists produce false function symbols (src/SYMBOLS.C ~345, cpp_decl / has_body):
     Widget::Widget(int x)
         : x_(x),
           y_(2),
           name_("w")
     {
   lists x_, y_ and name_ as functions; the `: Base(o)` / `, z_(o.z_)` style lists Base and z_; inside a class body a continuation line `: x_(x), y_(2)` becomes Widget::x_. Fix: keep an "in initialiser list" flag in the scanner's state (set when a function header's `)` is followed by `:` or a line starts with `:` after a header; cleared at `{` or `;`) and do not take declarations while it is set; skip lines starting with `,`. Add both layouts to the sample test.
3. F8 steps into library headers. With the real g++ 9.3 output (in this repository now as test/gpp93-real.txt: use it as test data) F8 visits 14 places, four of them notes in c:/djgpp/include/cxx/9/bits/stl_vector.h, each opening that header as an editable buffer. Fix: F8 and Shift+F8 skip notes whose file is not in the project's folder (or below it); every note stays in the F7 list, where Enter still goes to it; "required from here" in the user's file stays reachable. Test with the real output: the places F8 visits, in order.
4. The manual on F8 and notes (DE.TXT ~307-310) and on the raw-string delimiter limit (~324-326) must be true: see items 3 and 6.

MINOR (fix these too)
5. The .H rule (src/SYNTAX.C ~98) takes `template` followed by a space: a C header with `template` as an identifier at the start of a line becomes C++. After `template` skip spaces and require `<`. Also detect `#include<vector>` with no space.
6. When the raw-string delimiter table is full (src/SYNTAX.C ~326), a new delimiter is treated as the empty one, so its string ends only at the next `)"`, usually later, colouring code as string and losing symbols. Reserve the last slot as a wildcard that ends at the first `)` followed by up to 16 delimiter characters and `"` (stop-early must stay correct); test with more delimiters than the table holds; make DE.TXT say exactly what happens.
7. `#if`/`#else` branches that each open a brace lose every symbol after them (src/SYMBOLS.C ~439 cpp_braces): save the depth and scope count at #if/#ifdef/#ifndef and restore them at #elif/#else. Test.
8. Out-of-class definitions keep the namespaces they are written with (`outer::inner::Widget::decl`) while in-class ones are `Widget::decl`; DE.TXT ~322 says names carry no namespace. List both as Class::method (the last class part and the name) and keep the manual true.
9. src/SYNTAX.C ~291: `.5e-3` colours as `.5e` then `-3`: the exponent sign after a number that starts with a dot.
10. Member calls get free-function hints: in `s.find(`, `m.count(`, `p.get(` the status-line signature and Ctrl+F1 show std::find, std::count, std::get. When the word follows `.` or `->`, do not offer the non-member C++ entries.
11. A regression test for the first-line state fix (commit 99203fa): set hs[0] to a wrong state with hvalid 1, call buf_state, check the first line's colours. And the CHANGELOG's "not always set" should say what was true: it was never set.
12. SYNTAX.C is shared, byte-identical, with DS: reduce what it needs from the program that includes it. Give std_header's buffer its own constant so DE_PATH is not needed, and say in a comment at the top of SYNTAX.C exactly what the host header must provide (the L_ enum with L_CPP, the A_ roles, xrealloc, and so on).
After the fixes: update TOUR.md where these change what it says (symbols, F8, raw strings) and re-check the file:line references in the sections you touch; CHANGELOG 0.7 entry; DE.TXT and src/MANUAL.H.
