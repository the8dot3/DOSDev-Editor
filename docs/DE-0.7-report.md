# DE 0.7: report

DE 0.7 makes C++ a language of its own. The work is on branch
`claude/charming-turing-ul7ctz`, on top of `97f921a` (DE 0.6 and TOUR.md). It
is not pushed. `main` and the `v0.6` tag are untouched.

## Commits

| Commit | What |
|--------|------|
| `6669124` | Comments that said the wrong thing (SCREEN.C, DIALOG.C, above `newest` in PROJECT.C) |
| `dc527fc` | Remove `proj_run`, which nothing called |
| `a1c1525` | One `join` and one `is_dir`: the Open dialog uses PROJECT.C's (`path_join`, `is_dir`) |
| `6b69227` | One extension test: PROJECT.C uses SYNTAX.C's `ext_is` |
| `3c5d234` | One `has_upper`: Find in files uses the editor's |
| `7f90edf` | One `has_text`: Go to symbol uses the reference's |
| `7ec9674` | Keep mine after a run takes the file's size as well as its time (the bug in the tour's section 12) |
| `99203fa` | The first line's syntax state is set every time (a bug found on the way; see below) |
| `0151feb` | C++ as its own language: colours, symbols, suggestions, reference, g++ |
| `de7dfe5` | DE 0.7: the version, the changelog and the manual |
| `45e0853` | TOUR.md describes DE 0.7 |
| (this one) | This report |

The six tidy commits change no behaviour, and each left the 253 tests
passing unchanged.

## What was built

### 1. C++ as its own language

- `L_CPP` is a new language, added at the end of the enum, before `L_COUNT`,
  so the numbers of the others stay as they were. The status line says
  `C++`.
- `.CPP .CXX .CC .HPP .HH .HXX`, in any case, are C++. `.C` is C.
- A `.H` is C unless a line in its first 4 KB starts, after spaces, with
  `class `, `namespace `, `template` (followed by `<` or a space), or
  `#include <name>` where the name has no dot. Lines inside `/* */` do not
  count, and neither does a line that only contains one of these words
  further on. This is `lang_of_text` in SYNTAX.C. `buf_load` calls it with
  the file's bytes. `buf_save` calls it with the buffer's first 4 KB, so a
  new header becomes C++ when it is first saved with C++ in it. The manual
  states the rule (topic "C and C++"), and it is tested.
- A file with no extension in a folder called `c++` or `cxx` is C++. These
  are the C++ library's own headers (`<vector>` is a file called `vector`).
- `.C` files no longer colour C++'s words as keywords. The C list kept
  Turbo C's DOS words (`near`, `far`, `huge`, `interrupt`, `cdecl`,
  `pascal`, `asm`). No existing test checked a C++ word in a C file, so no
  existing check had to change for this; new checks cover it.
- `lang_family` treats C and C++ as one where they must work together: words
  from the other open files, Go to definition across C and C++ files, F12 on
  `#include`, Enter's indent after `{`, `}` stepping back.

### 2. Colours

- Keywords: C++17's, and from C++20 `concept`, `requires`, `consteval`,
  `constinit`, `co_await`, `co_return`, `co_yield`. Types: `char8_t`,
  `char16_t`, `char32_t`, `wchar_t` and the C types. `nullptr`, `true`,
  `false` are coloured as constants. The four casts and the alternative
  tokens (`and`, `and_eq`, `bitand`, `bitor`, `compl`, `not`, `not_eq`,
  `or`, `or_eq`, `xor`, `xor_eq`) are keywords.
- `override` and `final` are coloured as keywords everywhere, although they
  are keywords only after a declarator or a class name. Colouring them in
  other places is rare in real code and the manual says so.
- `import` and `module` are not coloured. In most code they are ordinary
  names, and modules are not usable with DJGPP's compilers in DOS.
- Raw strings `R"delim( ... )delim"`, with `LR`, `uR`, `UR` and `u8R`, are
  strings across lines. The per-line state was extended so: each delimiter
  gets a number the first time it is seen (`raw_state`), kept for as long as
  DE runs, and a line inside a raw string ends in state `S_RAW + number`
  (`S_RAW` is 4). A state therefore means the same on every line and in
  every file, so `buf_state`'s stop-early rule (tour, chapter 6) stays
  correct. The table has room for 252 delimiters per run, including the
  empty one; after that a new delimiter is taken as the empty one. A hash of
  the delimiter would have been smaller but could give two delimiters the
  same state, which would break the stop-early rule.
- Digit separators (`1'000'000`), exponent signs (`1.5e-3`, `0x1p+3`),
  binary and hex literals, and user-defined suffixes (`10_km`, `2.0f`,
  `"x"s`) are coloured as part of their number or string. Prefixes `L`,
  `u`, `U`, `u8` are part of the string or character.
- `//`, `/* */` and `#` lines are as in C (the same scanner, `syn_c`, with a
  `cpp` flag).

### 3. Symbols (Ctrl+L, F12)

A separate C++ scanner in SYMBOLS.C (`cpp_decl`, `cpp_function`,
`has_body`, `cpp_braces`, with a `CppScope` carried from line to line). It
counts braces outside comments, strings and `#` lines, and looks only at
lines where a declaration can be: at the top, or directly inside a
namespace, a class or an `extern "C"` block.

- Classes, structs, unions, enums and `enum class`, namespaces (kind `n`,
  shown as "namespace"), `using NAME = ...`, and the C rules for `#define`
  and `typedef`.
- Functions at the top, in namespaces and in class bodies, when the `)` is
  followed by `{` (after `const`, `override`, a member initialiser list and
  so on) before any `;` or `=`, up to 8 lines on, so a split parameter list
  is found. Declarations, `= default`, `= delete` and `= 0` are not listed.
- Names carry their classes: `bar` in the body of `class Foo` and
  `void Foo::bar()` outside it are both `Foo::bar`. Constructors are
  `Foo::Foo`, destructors `Foo::~Foo`, operators `Foo::operator+` and
  `Foo::operator bool`. Templates are listed by name without parameters:
  `Stack<T>::push` is `Stack::push`.
- F12 on `bar` matches `Foo::bar` (by the name's last part), looks in C files
  from a C++ file and the other way round, and on `#include <vector>` opens
  DJGPP's header from `DJDIR/include/cxx/<version>`.
- The limits are in the manual and the tour: macros that open or close
  braces, `#if` branches that each open one, macros inside declarations, a
  name and its `(` on different lines, specialisations such as
  `class Foo<int>`. Names are not written with their namespace (a choice, to
  keep names short in a 40-character field).

### 4. Suggestions and the reference

- Suggestions offer C++'s keywords in C++ files, and the files' words as
  before.
- REF.C has 119 C++ entries, in the same form as the SDL3 and PDCurses ones
  (language, name, header, one-line signature, one-line description):
  `string` and its common members, `getline`, `to_string`, `stoi`, `stod`,
  `string_view` (substr, remove_prefix, data), `vector` and its common
  members, `array`, `map` (and its members), `unordered_map`, `set`, `pair`,
  `make_pair`, `swap`, `move`, `tuple`, `make_tuple`, `get`, `tie`,
  `optional` (value, value_or, has_value, nullopt), `unique_ptr`,
  `shared_ptr`, `make_unique`, `make_shared`, `cout`, `cin`, `cerr`, `endl`,
  `ifstream`, `ofstream`, `fstream`, the string streams, `sort`, `find`,
  `find_if`, `for_each`, `count`, `copy`, `reverse`, `min`, `max`,
  `accumulate`, `back_inserter`, the `<cmath>` overloads (sqrt, pow, abs,
  fabs, floor, ceil, round, sin, cos, tan, atan2, exp, log, fmod, hypot),
  and `<chrono>` (steady_clock, now, system_clock, duration, count,
  duration_cast, seconds, milliseconds). No `<thread>`.
- Each signature was checked against the C++17 standard's synopses. Where
  the standard's form is long, the common overload is given and the
  description says the rest (for example `getline` without its template
  parameters, `find` with the `basic_string` argument). `endl` is listed
  under `<ostream>`, where the standard declares it; `swap` and `move` under
  `<utility>`, not `<algorithm>`. The claim that g++ links the maths library
  itself was checked with the cross g++ (`-v` shows `-lm`).
- A member is named `class::member` (`vector::push_back`). It is not
  suggested (suggestions offer words), and Ctrl+F1 on `push_back` lists it
  by searching. The Shift+F1 list's name column was widened from 18 to 24
  characters for these names.
- A C++ file sees C's entries too (`ref_in`); `ref_find` looks at C++'s
  first, so `sqrt` in a C++ file is `<cmath>`'s. A C file does not see C++'s.
- After `std::` (and `std::chrono::` and the like), the suggestions are the
  reference's C++ names alone: no file words, no keywords, no C entries.
  Ctrl+Space directly after `std::` lists them all.
- After `name.` or `name->` nothing special is attempted: DE does not know
  the variable's type, so the suggestions are the usual ones. The manual
  says so.

### 5. Building

- PROJECT.C's guess for a file with no project: a `.CPP` gets
  `gpp -Wall -g -o NAME.EXE -x c++ NAME.CPP`, as a `.C` gets
  `gcc ... -x c NAME.C`. `-x c++` is there because GCC takes `.CC` and
  `.CXX` as C++ only in lower case (it does take `.CPP`). The cross g++
  accepted `-x c++ M.CPP` and built a DOS program.
- `msg_parse` now:
  - takes `FILE:LINE:COL:   required from here` (and `required from ...`,
    `required by substitution of ...`) as a note with its place, so F8 goes
    to where the user's code used the template;
  - takes the source lines GCC 9 and later print (`   15 |     int x = y;`
    and the `|   ^` line under it) as text. Before, such a line that
    contained `error:` (a C label named `error`, which is common) counted
    as an error. This also changes what DE does with C messages, for the
    better;
  - treats `.tcc` files and extensionless files under a `c++` or `cxx`
    folder as source files, so an error inside `<tuple>` has its place.
  - `In member function`, `In instantiation of`, `In file included from`
    and `from FILE:1:` lines stay text, as tested.
- No other notion of building needed C++: project builds use `project.ini`
  or `BUILD.BAT`; `up_to_date` already counted `.CPP` and the others as
  sources. CONTRACT.md is unchanged apart from the version in the
  `DE 0.7 ok` example, which it already says is DE's version.

### 6. The Keep mine bug

`ed_check_disk` now calls `buf_disk_seen`, a new BUFFER.C function that
takes the file's time and size with the same `file_size` that `buf_save`
uses (and `buf_save` calls it too). The test's run now writes
`again, and longer\n`, a size different from the `fresh\n` before. The
test failed before the fix and passes after.

### 7. Tidying

Done, each in its own commit, with no change in behaviour and the tests
unchanged: the three stale comments, `proj_run`, `join` and `is_dir`
(DIALOG.C now calls PROJECT.C's `path_join` and `is_dir`; the dialog only
joins names that are not absolute, so the results are the same),
`ext_is`/`ext_in`, `has_upper`/`has_capital`, `has_text`/`contains`.

Left as they are (the larger duplications, for the shared toolkit later):
the extension lists in `lang_of` and `is_source`; `window` (DIALOG.C) and
`page` (HELP.C); the two FNV-1a hashes; SYNTAX.C's `WordSet` and
`KeyTable`; taking the extension off a name in `temp_name` and
`buf_emergency`; the word at the cursor in `ed_word` and `help_lookup`;
`ed_cursor` and `ed_draw`; `sane`, `follow` and `edit_end`; the two "save
all"s; the names `Entry`, `add`, `number`, `B` and `B_CUR`.

### 8. Version and documents

0.7 in DE.H, DE.LSM (with the date and C, C++ in its keywords), README,
DE.TXT's first lines and its `/CHECK` lines, and CONTRACT.md's example.
CHANGELOG.md has a 0.7 entry. DE.TXT has a new topic, "C and C++" (which
files, the colours, what Ctrl+L and F12 find, suggestions after `std::`,
the reference, gpp, the limits), and small changes elsewhere (status line,
Alt+;, Enter after `{`, the reference, symbols, F9's guess, g++). The
manual's contents still fit one screen (17 topics). src/MANUAL.H was
made by the Makefile's rule. TOUR.md now says it describes `de7dfe5`; see
below.

## A bug found on the way

`buf_load` and `buf_save` set `hvalid` to 1, which says the state of line 0
is known, but `hs[0]` was written only when `hvalid` was 0. So the first
line's start state was whatever the memory held. In C only a stray 1 would
have shown (the first line coloured as a comment), which is why it was not
seen. With C++'s raw-string states every stray value from 4 up showed: during
this work, C files in the test build came out coloured as one long
string. `buf_state` now writes `hs[0]` every time (commit `99203fa`).

## Tests

`make test`: **328 passed, 0 failed** (DE 0.6: 253). 75 new checks:

| Group | Checks | What |
|-------|-------:|------|
| C++: which files, and their colours | 30 | extensions, std headers, the `.H` rule (signs, comments, words that merely start with `class`, the 4 KB limit, `.C` stays C), the status line, a `.H` becoming C++ on save, every colour rule by cell attribute, the states kept for the lines, a `.C` file with C++ words and `R"x(` |
| C++: symbols and definitions | 10 | one 55-line sample with every kind (exactly 18 symbols, none from declarations or bodies), Ctrl+L in a header, F12 to `Foo::bar` in a file not open, F12 from C++ to C, F12 on `#include <vector>` with a stand-in DJDIR |
| C++: suggestions and the reference | 15 | header of an entry, C++ first then C, C does not see C++, no `thread`, keyword ghost, file words, `std::vec`, Ctrl+Space after `std::` and after `std::v`, `std::chrono::`, the call signature for `std::sort(`, Ctrl+F1 on a member, Shift+F1 title, no C++ keywords in C |
| C++: building, and what g++ says | 18 | g++ 12 lines as the cross compiler printed them, g++ 9 lines in DJGPP 9.3's paths, the linker, a build through F9 whose output has source lines with `error:` in them, F8 to the error and then to "required from here", the `gpp` guess |
| Random editing | 2 | 20 rounds of 600 keys on a C++ file with raw strings and comments over lines, and 20 on a Pascal file with both comment kinds, each with extra keys in the pool; `buf_state_check` after every key |

Changed existing checks: the three version checks (0.6 to 0.7) and the Keep
mine check, made stricter (the run now changes the file's size). The C
rounds of the random test use the same seed and the same keys as before.
An instrumented run (not committed) counted the kept states that were not
`S_NORMAL`, after every key: in the C++ rounds 19,244 raw-string states and
7,640 comment states, in the Pascal rounds 30,843 comment states.

The tests were written with each piece. For the Keep mine fix the test was
run first and failed. For the C++ features the tests use names (`L_CPP`,
`lang_of_text`) that did not exist before, so they could not be run against
0.6 first.

The host build prints three warnings that were there before this work, from
host GCC 13 and glibc's checks (`freopen` results in DETEST.C, a
`sprintf` in `buf_emergency`). The cross build has none.

## DE.EXE's size

| | DE 0.6 (`97f921a`) | DE 0.7 | Growth |
|-|-------------------:|-------:|-------:|
| `build/DE.EXE` (with `-g`) | 977,690 | 1,057,076 | +79,386 (+8.1%) |
| Stripped, as packaged | 364,544 | 401,408 | +36,864 (+10.1%) |
| text | 333,808 | 370,672 | +36,864 |
| data | 28,160 | 28,160 | 0 |
| bss | 720,896 | 729,088 | +8,192 |
| `build/DE.ZIP` | 309,684 | 344,177 | +34,493 |

Most of the text growth is the 119 reference entries (REF.C grew from
50 KB to 71 KB of source). The bss growth is the raw-string delimiter table
(4.3 KB) and `lang_now`'s 4 KB buffer.

`make` (cross, `-Wall -O2 -g`): no warnings. `make package`: builds
`build/DE.ZIP`.

## Where this differs from the brief, and why

- **Commit attribution.** The commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and the session
  line, as the session's own attribution note says, not with the
  "Claude Fable 5.2" line the brief gave. The note says only the user's own
  instructions take precedence over it, and the brief came from an agent.
  If the user wants the other line, the messages can be amended before they
  are pushed.
- **g++ 9 messages.** The g++ 12 texts in the tests are as the DJGPP cross
  compiler (12.2.0) printed them, with the long install path shortened to
  `/opt/djgpp/...`. No g++ 9 could be run here: the Workbench's DJGPP 9.3 is
  a set of DOS programs, and there is no DOS. The g++ 9 lines are written
  in g++ 9's format with DJGPP 9.3's header paths (`c:/djgpp/include/cxx/9/`,
  taken from its installed tree). They need checking against a real run.
- **Beyond the brief:** `std::chrono::` also gives the std names;
  Ctrl+Space works with nothing typed after `std::`; a `.H` is looked at
  again when it is saved; the C++ library's extensionless headers are C++
  and are places in messages; F12 on `#include <vector>` finds DJGPP's
  header; GCC's source lines are never counted as errors (this helps C too);
  methods defined in a class body are also named `Foo::bar`; the first-line
  state bug above.
- **The tour.** The brief asked for the references in the sections touched.
  All of them were checked, because the tour now says which commit it
  describes: lines that did not change were mapped by a diff of the two
  versions, the rest by hand, and every quoted block was compared with the
  lines it names.

## What needs a real DOS run

- F9 on a `.CPP` with no project in the Workbench: that DJGPP 9.3's `gpp`
  takes `-Wall -g -o HELLO.EXE -x c++ HELLO.CPP` and that the program runs.
- g++ 9.3's real messages for a template error, a missing name and a
  linker error, read into the Messages window: kinds, places, and F8 to
  "required from here". In particular the form of the header paths it
  prints (drive letter, case, `include/cxx/9`).
- F12 on `#include <vector>` with DJGPP's `DJDIR`: the folder
  `DJDIR/include/cxx` is read with `opendir` on DOS, with long file names.
- Speed on a slow machine: `DE /SPEED` on a large `.CPP` file, Ctrl+L on
  one, and F12 across a project of C++ files (the scanner looks up to 8
  lines ahead for some lines).
- Memory: 36 KB more code and 8 KB more static data.
- The F1 contents page with 17 topics on a real 25-line screen (it fits on
  the test's 80 by 25 stand-in).

## Other things the caller should know

- **DS.** SYNTAX.C changed as expected. To use it, DS's copy of `DE.H` needs
  `L_CPP` (before `L_COUNT`) and the prototypes of `lang_of_text`,
  `lang_family` and `ext_is` (which is no longer `static`). SYNTAX.C also
  uses `DE_PATH`, as DE.H defines it. SCREEN.C changed only in a comment;
  DS's copy needs the same change to stay byte-identical.
- The raw-string delimiter table is shared by all files and never emptied
  while DE runs (252 delimiters). This is what keeps a state's meaning
  fixed; the manual and the tour say so.
