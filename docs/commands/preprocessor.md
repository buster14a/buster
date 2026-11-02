# preprocessor

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_preprocessor.c` is the preprocessor: one translation
  unit's token stream from its files' refined tokens (C23's `#embed` among its
  directives), and `./build/compiler -E` (as `clang -E`, with `-dM` and `--embed-dir=`).
  `src/buster/include` holds the compiler's own freestanding headers, which
  `--buster-headers` puts where clang's resource directory goes (the default),
  with clang's after them for the headers they don't supply (never for a name
  they have, so their `#include_next` reaches the C library's), and
  `-nobuiltininc` leaves out the compiler's headers, whichever they are.
  `docs/c-preprocessor.md` explains the design, the `clang -E`
  oracle and the 9700X measurements. `c_preprocessor_test.c` holds its tests
  (`main.c` includes it; `./build/compiler test-preprocessor` runs them),
  `c_preprocessor_fuzz.c` its differential fuzzing (#109; `main.c` includes
  it after the tests) and `c_preprocessor_benchmark.c` provides
  `benchmark-preprocessor`. Its `FileCache` holds what is prepared from each
  file (by identity) and every path looked up, for every translation unit that
  preprocesses with it (`PPOptions.files`; a unit without one makes its own).
  `c_predefines.c` gives the predefined macros the target and the options decide,
  as clang does (`-march`, the `-m` feature flags, `-march=native` from CPUID,
  `-O`, `-f...`, `-g`), from tables generated from clang 23; `c_predefines_test.c`
  checks them against clang under `test-preprocessor --clang` and regenerates the
  tables (`test-preprocessor --generate-targets`). Of the `-f` options that
  change clang's keywords, lexing or `-E` output (#281), `-E` models GNU's
  keywords (`-f[no-]gnu-keywords`, `-f[no-]asm`), `$` in identifiers, and char's
  signedness and `wchar_t`'s width in `#if` (from `c_predefines.c`'s options),
  and refuses the others where their last option turns them on
  (`pp_take_family`, `pp_settle_families`).
- `src/buster/compiler/c_preprocessor_history.c`, included by `c_preprocessor.c`,
  records macro expansion history in cold records and compressed output runs;
  `pp_diagnose_output` renders a diagnostic for an output-token index.
  With `PPOptions.defer_history` a unit records none while it runs: its
  diagnostics, and `pp_history_ensure` for a later reader, record it by
  preprocessing the unit again (`docs/c-preprocessor.md`, "Deferred history").
  `-E`, `parse` and the checker's driver defer it (but with `--scalar` and
  `-flazy-system-headers`). A reader of a unit whose history may be deferred
  calls `pp_history_ensure` first. `check_input_system_tokens` judges system
  headers' tokens from runs the main loop keeps (`pp_system_levels`, "System
  levels"), with the history's judgment (`pp_system_levels_history`) as twin.
  `c_preprocessor_history_test.c` is part of `test-preprocessor`, including its
  Clang diagnostic comparisons. `benchmark-preprocessor --history-cost`
  compares recording it with deferring it and reports retained and temporary
  history bytes.
## Preprocessor tests

`./build/compiler test-preprocessor --compare-clang [--clang PATH] [options]
corpus...` is #125's real-source oracle. It accepts directories or
`--files-from LIST --root DIR`, compares lexical kinds, spellings and locations,
and reports every Clang rejection as an explicit skip. Process/configuration
failures fail the run. `--report NEW.tsv` records every file's result and skip
reason. It refuses `--buster-headers`: both sides search as clang does, which
leaves ours no headers of its own to select. `c_preprocessor_oracle.c`
implements this interface;
[the frozen oracle guide](../../docs/preprocessor-oracle.md) records the pinned
headers/corpora, manifest identities, CI subset and on-demand commands.

`./build/compiler test-preprocessor [--clang] [--keep] [--seed N] [--count N]
[options] [file...]`, or `test-preprocessor --tables path...`:

- without files, the 320 built-in cases (`pp_test_cases`), each preprocessed by
  the vectorized kernels and their scalar twins. They run in 32 parts, the
  header sets below (`--clang`) in 16 and the files (below) a part each (a
  range each past 64), each part in a child process with a directory of its
  own, at most `test_process_cpus()` at once, the output in their order
  (`pp_test_children`, #465). They must give the same stream
  token for token (kind, flags, value and location) and the same directive
  tables, with the expected spellings and the expected number of warnings. The
  cases are of our own:
  - on the topics of the C standard's examples;
  - the behaviors probed against GCC and clang;
  - inputs past the vectorized kernels' limits;
  - directives and builtins that nest through the main loop's frames;
  - #193's review fixes, line markers among them;
  - the `-f` options it models (#281): GNU's keywords, `$` (what follows it
    joining the tokens after it, as clang lexes them), char's signedness and
    `-f[no-]short-wchar`'s width and signedness in `#if`;
  - c89's and C94's lexing without `//` comments, and c89's without digraphs,
    pasted and stringized too, and `-f[no-]digraphs` in four modes (#288);
  - each difference from clang that #109's fuzzing found, minimized, and the
    ones probing around it found;
  - raw-string errors in active text, definitions and unused macro arguments,
    suppressed in excluded group bodies and opaque headers/messages (an
    untaken `#elif` line still diagnoses); exact Clang
    diagnostic/location comparisons; contextual recovery beyond 8/64/256
    repairs with earlier macro snapshots retained, and failure injection for
    allocation, data-index exhaustion and an unchanged recovery mask (#285);
  - named UCNs (`\N{...}`, #452), refused where the lexer reads one in a token
    (an identifier, a pp-number or an unknown token): in active text, a macro's
    definition once and an unused argument, `#if` and `#elif` lines, after line
    splices and as a trigraph, beside `\u`, `\U` and UTF-8 spellings, and with
    names no character has; and no error in a skipped group, a literal or a
    comment, whose `-E` text is clang's;
  - `#embed`: its data, parameters, errors and files, and where `-E` prints it;
  - #119's includes, in files and links the case makes: header names written out
    and computed, `#include_next` through three directories, an inactive group's
    includes, a cycle at the depth limit, one file through a symbolic link and a
    hard link, and clang's recoveries and diagnostics' places; and #254's
    review's: a header name written out that is too long for a path (and an
    `#embed` one longer than its buffer), `__has_include`'s and `__has_embed`'s
    operands as their `#if` line writes them, and the header the depth limit
    refuses.

  A case can also give extra files and options for both sides, and the exact
  text `-E` prints with line markers. Then `__TIMESTAMP__` in two files with
  modification times of their own must give each file's time (in UTC), and
  `SOURCE_DATE_EPOCH`'s when it is set; the `-m` options buster refuses
  though clang takes them (another target's, which clang ignores with a
  warning, and those that change the target) must be refused, and each `-m`
  flag that changes no macro taken; and each of `pp_test_option_sets` must be
  refused, its message naming the option, or taken without one: the `-f`
  options that change clang's keywords, lexing or `-E` output, which
  `pp_parse_options` models or refuses where their family's last option turns
  them on (docs/c-preprocessor.md, "The `-f` options"); the expansion history's checks; #embed's vector/scalar text and lookup checks;
  the named UCNs' places (`pp_test_named_ucns`): each error at its UCN's backslash, by both kernels, and with
  `--clang` where clang's `-pedantic` warning that a named UCN is its extension is, or for a name no character has
  its error, three columns before it (clang reads one past an identifier's first character silently); one in an
  identifier of 40,000, each an error within 10 s (`pp_test_named_ucn_scaling`); and one, or a trigraph's `??/`,
  across the edges of the 64-byte chunks, the 4 KB words of refinement's backslash bitmap and 4 KB windows, by
  both kernels, whole and a window at a time (`pp_test_named_ucn_boundaries`);
  the prelude's failures (#361): the command line's arena push and its `-D`/`-U` and
  `-include` files each failed in turn (the arena cut short, the data table full), each
  a fatal error that stops the prelude and fails the run, beside controls that
  succeed (no `-include` file made without one); and the fuzz batch: its isolation rules checked on small sources (what a case
  leaves open makes clang read it alone), its comparison on pairs of texts
  (whitespace between tokens doesn't count, a literal's contents and joined
  tokens do, and our tokens that don't lex back, as an unterminated literal,
  end where ours do in both texts), with `--clang` its emulation of clang's
  -P printer checked on fixtures of its own, then 1,500 cases of the
  independent harness's generator in each of C17 and C23
  (`c_preprocessor_fuzz.c`), which both kernels must give
  alike (streams, directive tables, diagnostics' counts, history and both
  printers' bytes), with -P text that lexes back into the stream. Then the directive tables (#118,
  `c_preprocessor_directives_test.c`): 2,000 random sources (`--count N`,
  `--seed N`) of directives, `%:`, `#` alone, line markers, unknown names,
  `#`s inside lines, literals and comments and after line splices, and
  conditionals nested to many levels, balanced and not, and nests past the
  vectorized pairing's depth limit, each prepared by both kernels, whose tables
  (directives, line ends, kinds, pairing, notes and include guard) must be the
  oracle's: a walk of the tokens one at a time, of the test's own. And 17
  fixtures of a file's conditional structure (`#else` and `#elif` after
  `#else`, a directive without `#if`, groups left open, in skipped groups and in
  an included file, extra tokens, `#elifdef` before C23), whose diagnostics
  both kernels must give alike, and ten of `#define`'s and `#undef`'s
  constraints and redefinitions (#121). And 20 fixtures of include guards and
  `#pragma once` (#120): the guard's shapes clang's multiple-include
  optimization takes and those it doesn't, `#undef` between inclusions, an
  inclusion from inside the file, the macro defined before the first, links to
  a `#pragma once` file, `#import`. Each run must skip as many inclusions by
  guards and by `#pragma once` as expected, by both kernels. Then a location
  space shrunk to end where an inclusion, and then the first pasted spelling,
  would start (`--location-limit N`, #124): both kernels must stop there, with
  the output before it, and report the inclusion at its `#include` and the
  spelling without a place. And `test-preprocessor` with a file and
  `benchmark-preprocessor` (with `--clang`, and `--compare-clang`), run as
  children on a small file, must take `--location-limit N` with its value as
  ours, kept from clang, succeeding in a space the file fits in and running
  out of one of a byte. Then the windows (#116, `c_preprocessor_windows_test.c`):
  240 generated sources of up to 96 KB, of what a window's end can cut (comments
  and raw string literals over many lines, macros continued over hundreds of
  lines by splices, line splices, `\r\n`, long lines, a byte order mark,
  constructs left open at the end), each prepared whole by both kernels and a
  window at a time (`pp_prepare_windows`) in windows of 1 byte to 8 KB, in five
  language modes: every record must be the whole file's, byte for byte; and
  where a window is cut before a long line (`pp_window_cut`), against a byte at
  a time, on random bytes ending at a `PROT_NONE` page and starting at the end
  of one, and on 9 windows worked out by hand, one faulting if the scan passes
  its last line end; and the directives' places (`pp_directive_places`) against
  their bit-at-a-time twin, each output ending at a `PROT_NONE` page; and 11 files
  made by hand ending at a `PROT_NONE` page (#441: a `\r\n` in a block, across
  64-byte blocks, inside a 4 KB line page and across pages, a `\r` and a `\n`
  alone, a `\r` last), whose line ends `pp_count_lines` must count from many
  starts to every end, as a count a byte at a time does (a `\r\n` at its `\n`,
  so not by a query that ends at the `\n`), the line pages counted whole and in
  two windows cut around each line end, `pp_page_line` at every byte, and
  `pp_line_of`'s lines and columns near each line end from the cursor and from
  the pages;
- with files, each file with `-E`'s options (`-I`, `-isystem`, `-D`, `-std=`,
  `-ffreestanding`, `-target TRIPLE`, ...), by both kernels, which must agree;
  `--window-bytes N` (ours alone, like `--location-limit`) sends every file of
  two windows or more through windows of N bytes, so that the vectorized side's
  windowed preparation of a whole unit is compared too;
- with `--tables`, the directive tables of every `.c` and `.h` file under each
  path, by both kernels, against the oracle (about 0.02 s for `src`, 0.6 s for
  the C library corpus).

In both, the vectorized printer's text must be the scalar printer's byte for
byte, with line markers and without, and must lex back into the stream's
spellings.

Every case and file runs with clang's answers to the feature queries
(`--clang-features`, the oracle's mode), and a check runs a line of queries both
ways: by default they answer what buster implements.

`--clang` also compares every case and file with `clang -E` and the same
options (but `--predefines`, `--location-limit` and `--window-bytes`, ours alone), token spelling by token spelling, and then where clang's line markers
put each token (its presumed line and include stack) with where ours put it. It
prints the first difference with the tokens around it. For files, our side
reads clang's own predefines for the options through `--predefines`, and clang
gets `--buster-headers`, which it hasn't, as ours' whole search
(`pp_clang_arguments`): after `-nobuiltininc`, each directory ours keeps, by
identity and a system directory over a user's (`pp_dedupe_directories`), with
its option, and in clang's resource directory's place a directory of links to
its headers but buster's; or nothing with `-nostdinc` or `-nobuiltininc`. The
files it finds through the links are compared under their names in clang's
resource directory, escaped as clang writes them, as ours names them. The macros at the file's end also compare against `clang -dM -E` with our own predefines. Without
files, `--clang` also checks the built-in predefines, the keywords and the
features:
- in 26 option sets (the ten modes from `c89` to `gnu23`, C94 as
  `-std=iso9899:199409` and as `-std=c89 -fdigraphs`, C94 and `gnu89` without
  digraphs, `-ffreestanding`, `-O2`, two combinations, the six ISA profiles of
  `docs/x86-target.md` and two with `-mtune`), `pp_predefines` must give
  `clang -dM -E` line for line, but for the patch release's three macros;
- `c_predefines.c`'s generated tables (each x86-64 feature's flag, closures and
  macros, each `-march` CPU's macro name and features, the `-mtune` names) must
  be what `c_predefines_test.c`'s generator learns from clang now;
- about 2,200 option sets must give `clang -dM -E`'s macros, and be accepted or
  refused alike: every `-march` CPU and `native`, every feature flag on and off
  on x86-64 and on the CPUs with the most features, each `-O`, `-f`, `-g` and
  `-m` option `c_predefines.c` reads, alone and in the orders that matter (an
  invalid value before the last of an option clang reads only the last of among
  them), each `-m` option that changes no macro (each flag alone, the hardening
  flags against each other, and each value option's rule at its edges), each of
  clang's `-m` aliases alone and against its option in either order, `-m`
  options clang refuses too (`-mavx512`), the 16 sanitizers with a macro or in clang's incompatible pairs, alone,
  in pairs and in pairs with either one turned off after them, each of them and
  each other group with `-fno-sanitize=all` after it and before it, and 400
  random combinations, some with those `-m` flags and aliases (`--seed N`
  changes them);
- `-E -dM` of a file with every kind of definition must be clang's byte for byte
  (but `__BUSTER__`), and `__FLT_EVAL_METHOD__` clang's under
  `-ffp-eval-method=` and without SSE;
- in each mode (the ten and C94), `__is_identifier` of every predefined name, and of clang's
  keyword candidates chosen apart from them (`pp_test_keyword_candidates`),
  must give clang's answer, and so must it in 25 sets of `-f` options
  (`pp_test_keyword_sets`: GNU's keywords on and off, in each order of
  `-f[no-]gnu-keywords` and `-f[no-]asm`, and the refused families turned off
  again);
- each refused option set must change clang's `-E` of a probe (from the set's
  `-std=` alone, or no options), and each taken one must give clang's tokens;
- in each mode, `__has_feature` and `__has_extension` of every name in clang
  23's `Features.def` must give clang's answers, including 45 sanitizer option sets and target queries;
- buster's own headers (`src/buster/include`) against clang's, for each header,
  8 modes, hosted and freestanding (176 sets): with each set, clang's run must
  define the same macros (but the guards and helpers each names its own way),
  expand each to the same tokens, make the same declarations and give the same
  diagnostics; and our preprocessor with `--buster-headers` must give clang's
  tokens on all of them, with no warnings from the headers. Then, in each mode,
  hosted and freestanding, two units (32) include them with the C library's
  headers that define some of their macros or ask them for parts and clang's
  headers they don't supply (`<immintrin.h>` and others), buster's last and
  first: clang with them, searched as `--buster-headers` searches
  (`-Wsystem-headers`), must warn about nothing in the headers and expand every
  macro as clang's own headers do, and our preprocessor must give its tokens,
  with no warnings from the headers; `-nobuiltininc` and `-nostdinc` must
  leave out buster's headers and clang's after them alike; a quoted name
  beside one of clang's headers must come from buster's when buster's has it;
  the files' comparison with `--buster-headers` must agree with clang on a
  unit of buster's and clang's headers (`<inttypes.h>`, which reaches buster's
  `<stdint.h>` through glibc's) and an `-isystem` directory's `<limits.h>`
  given after the flag, also with clang's links in directories whose names it
  escapes (a quote, a backslash, a byte past ASCII), on `__has_include` with
  the flag alone, with `-nobuiltininc` and with `-nostdinc`, and with clang's
  resource directory and buster's given again as `-I`, `-isystem` and
  `-idirafter` (13 ways, through a symbolic link, a trailing slash and `/./`
  too); and the benchmark's clang command, with 300 `-D` options and the
  search, must reach clang whole;
- the fuzz batch's cases must give clang's acceptance, and an accepted case's
  stream, printed with clang's -P spacing, must lex into clang's tokens (with
  a comment's `/` and a line splice's `\` as tokens, which only tokens printed
  next to each other make, and our tokens that don't lex back whole:
  `preprocessor_fuzz_lex_printed`). Where clang's text can't show where such a
  token ends (an unterminated literal off a pragma's line), clang's own tokens
  for the case alone (`clang -Xclang -dump-tokens`) must confirm ours; on a
  pragma's line that goes on past one (`#pragma omp` expands its tokens),
  which clang's tokens don't have, the case counts as unverified; a
  comparison that can't be made leaves its case without a result. With
  `--clang`, the batch first checks these on fixtures of its own, among them
  #263's second review's probes.

- the structure and definition fixtures' diagnostics must be clang's, byte for byte, and the
  guard fixtures' output and skips by guards clang's (`-Xclang -print-stats`),
  as every file's skips by guards must be. `sqlite3.c`, musl 1.2.5's 1,357 sources,
Lua 5.4.8 and this repository's translation units are token-identical;
`docs/c-preprocessor.md` has the configurations. The 9700X job's test step runs the cases
and the repository's translation units with `--clang`, and `--tables src`.

The fuzz batch's options: `--fuzz N` cases per mode (0 for none), `--first N`
the first case's id, `--seed N` (1 by default), `--std c17|c23` one mode,
`--jobs N` worker processes (1 by default; the cases don't depend on it), and
`--print-case N`, which prints case N as clang reads it alone. Each
difference from clang is confirmed on the case alone; the first 4 that each
worker finds in a mode are minimized and saved as `fuzz_failure_N.c` in the
current directory. One block of 64 cases in 128 has every case read alone
too, which must give its batch's result, and a batch clang gives no result
for has its cases read alone. It exits with status 1 on any difference, a
batch that differs from its cases alone, or a case without a result. For a
qualification run (a million cases in each mode take about 7 minutes on the
9700X's 15 threads, and 15 to 24 on a 7940HS's 16, mostly clang's processes):

```sh
./build/compiler test-preprocessor --clang --fuzz 1000000 --seed 11 --jobs 16
```

`docs/preprocessor-fuzz.md` has the comparison's rules, the generator's
constructs and the qualification runs.
`./build/compiler test-preprocessor --generate-targets` prints `c_predefines.c`'s
generated block from clang 23 (on `PATH`), to paste over the old one when clang
changes; `docs/c-preprocessor.md` ("The predefined macros") has the procedure.

## Preprocessor benchmark

The exact commands, pinned inputs and raw reports for the #117 acceptance
refresh are in [the measurement record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/preprocessor-117-2026-10-23/README.md).
Its invocation scripts take a separate normal shared-host lock for each command.

`./build/compiler benchmark-preprocessor [--clang] [--markers] [--only
vectorized|scalar] [--read-limit N,...] [options] file...` preprocesses each file from nothing per
run (a new intern table and preprocessor), with the vectorized kernels and their
scalar twins, 21 passes after a warm-up, and reports the medians of time, cycles
and instructions per input token and per output token, IPC, each phase's share
and time (read, lex, refine, directive tables, main loop/setup), the release after the
run (the files' mappings among it), `-E -P`'s throughput with its printing
(input bytes and output tokens per second, #124) and what a run did
(directives, runs, expansions, invocations, pastes, the `#if` lines and their
lengths, and the inclusions skipped by guards and by `#pragma once`).
It also reports explicit file-probe `open`/`fstat` attempts, including failed
opens and uncached aliases, the subset made by include searches, and the
`stat` attempts for embed lookup and search-directory setup. Repeated cached
include hits and misses make no file-probe calls. These are the preprocessor's
explicit calls, excluding the benchmark harness and libc's directory
enumeration internals. Include lookup time includes path construction, cache
lookup and first byte acquisition; embed lookup time includes its search and
`stat` calls. Both are inclusive intervals that overlap the phase breakdown;
never add them to it or subtract them again. Preparation and execution of the
found header are outside include lookup. The main-loop remainder also contains
initialization, as it did before this label made that boundary explicit.
The final-output copy shares count file runs, buffer runs and direct
object-like copies only when they write to the final stream; intermediate
captures do not enter the numerator. Their denominator is the final output's
tokens, including pragma records and one record per embed resource. The
remaining tokens are emitted individually or lifted from captured pragmas.
The older per-pass run totals still include intermediate captures and must
not be divided by the final output count to claim a share of that output.
`--read-limit` gives a list of the largest files a run reads rather than maps
(16 KB by default; 0 maps every file), each run in turn in every pass, which
must all give the same output.
`--clang` also times `clang -E -P` with the same options, a process per file, with `--buster-headers` translated to ours' search and every argument passed (`pp_clang_preprocess`). A failed clang run fails the benchmark and reports no timing.
Our predefines follow the target options as clang's do; `--predefines FILE`
still replaces them (the benchmark leaves it out of clang's command line). The
runs answer the feature queries as clang does (`--clang-features`), so that they
do the work `clang -E` does. The
default 9700X job runs the smoke command above on `main.c`, pinned to one allowed
CPU, with `--clang` on the daily and dispatched runs and on pull requests that
change `c_preprocessor_benchmark.c` (the `changes` job's `preprocessor_clang`),
and without it on every other run that measures the preprocessor: clang's times,
about 6 s of the step's 12, measure clang rather than this compiler. Like the
others, its measurements are informational.

`./build/compiler benchmark-preprocessor --defines [--only vectorized|scalar]
path...` times definitions and lookups alone (#121): every `.c` and `.h` file
under the paths is read and prepared once, then each of 21 passes after a
warm-up, file by file, defines its `#define`s, compiles its macros and looks up
its identifiers as the main loop's runs test them, by each kernel, which must
make the same macros. It reports the medians in cycles per `#define`, per macro
compiled and per lookup. `docs/c-preprocessor.md` has the numbers for the C
library corpus.

`--guard-cost` times the vectorized kernels with the skip of guarded files and
without it (`PPOptions.no_guard_skip`), in place of the scalar twins: the
output's tokens must be the same (#120).

`--markers` runs and prints as `-E` without `-P` does: with the expansion starts
the line markers read and the history deferred (`PPOptions.expansion_starts` and
`defer_history`, as the `-E` driver sets them), the printing timed with its line
markers and reported as `-E with printing`; of the controls it takes
`--spelling-cost` alone, and refuses `--clang`. `--spelling-cost` times the
vectorized kernels with the printer's spellings by `pp_print_spellings`
(`spellings`) and by its general loop for every chunk (`per-token`,
`PPOptions.print_per_token`), in place of the scalar twins (#649). Every run's
warm-up print, with markers or without, must give the same bytes, whatever the
implementation and the read limit. Both take the printer's own copies, its
line markers' batches among them, so the two rows differ in the spellings
alone, and each row's history in the job is its before. The 9700X job runs
both, with and without `--markers`, on `main.c` and the frozen `sqlite3.c`
where it measures the preprocessor, about 13 s.

`./build/compiler benchmark-preprocessor --tables [--only vectorized|scalar]
path...` times the directive tables alone (#118): every `.c` and `.h` file under
the paths is read and prepared once, then each of 21 passes after a warm-up
builds every file's table by each kernel, whose tables must be the same, and it
reports the medians per input token and per directive, IPC, and the scan's
share. Run it under the 9700X's lock with `taskset -c 2`;
`docs/c-preprocessor.md` has the numbers for the C library corpus. The default
9700X job runs it on `src`, pinned to one allowed CPU.

`./build/compiler benchmark-preprocessor --embed N` measures instead the text
`-E` prints for an `#embed`'s N bytes (random, from a fixed seed), by
`pp_embed_text_vector` and its scalar twin in the printers' pieces, 21 passes
after a warm-up: medians per input byte, and the text's throughput. The two
texts must be the same. Run it pinned to one CPU; `docs/c-preprocessor.md` has
the 9700X's numbers.
