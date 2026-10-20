# The C preprocessor

`src/buster/compiler/c_preprocessor.c` is translation phase 4 for one
translation unit. It reads the refined tokens of each file it includes
(`c_lexer.c`, then `c_refine.c`, `docs/c-refinement.md`) and produces one token
stream, plus `-E`'s text from that stream. It is #15's stage 2 and roadmap
item 2, tracked in #117 and its parts #118 to #125. This document records the
design, how it is checked against clang, the Zen 5 measurements, and what comes
next.

## Results

**Token-identical to `clang -E`** (clang 23.1.1 on the 9700X, 23.1.3 locally),
under the configuration `test-preprocessor --clang` defines (see "The oracle"):

| Input | Translation units | Output tokens |
| --- | ---: | ---: |
| `sqlite3.c` 3.53, with glibc's headers | 1 | 707,153 |
| musl 1.2.5, every source of `src/*/*.c` and `src/*/x86_64/*.c`, as its Makefile builds them | 1,357 | 5,124,927 |
| This repository's translation units on top of `main` at 75261db, with `-march=native` (AVX-512 intrinsic headers) | 8 | 4,571,031 |
| Lua 5.4.8 (`-DLUA_USE_LINUX -DLUA_COMPAT_5_3`) | 34 | 588,066 |
| The built-in cases (`pp_test_cases`) | 63 | |

**Speed**, measured on 2026-10-20 on the Ryzen 7 9700X (clang 23.1.1,
`build.sh`'s `-O3 -march=native`), pinned to CPU 2 and under the
`exclusive.sh` lock. Each run starts from nothing, as a compile of one
translation unit does: a new intern table and preprocessor, with every file
read, lexed, refined and indexed. Figures are medians of 21 passes;
`clang -E -P` was timed by the benchmark's fork-and-exec harness, writing to
`/dev/null`.

| Translation unit | Input tokens | Output tokens | Vectorized | Scalar twins | `clang -E` |
| --- | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 1,243,768 | 707,153 | **12.00 ms** | 22.51 ms | 59.34 ms |
| `main.c` (this compiler, on top of `main` at 75261db) | 759,671 | 953,361 | **15.82 ms** | 24.13 ms | 89.83 ms |

| Translation unit | Cycles/input token | Cycles/output token | Instructions/input token | IPC |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 42.54 | 74.83 | 125.26 | 2.94 |
| `main.c` | 93.19 | 74.26 | 350.22 | 3.76 |

#193's review fixes (spacing through empty expansions, `__VA_OPT__` pastes and
the others in "Directives and `#if`") add 0.5% to 0.6% instructions and no
measurable time. In an A/B against the reviewed commit, medians of five
interleaved rounds:

| Translation unit | Reviewed commit | With the fixes |
| --- | ---: | ---: |
| `sqlite3.c` | 12.09 ms | 12.03 ms |
| `main.c` | 15.65 ms | 15.67 ms |

The second review's fixes (the inclusion's search position, the
`#__VA_OPT__` and `#line` spacing and anchors, `__BASE_FILE__`, and #192's new
hash) were also within noise against their reviewed commit:

| Translation unit | Reviewed commit | With the fixes | Instructions/input token |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 11.99 ms | 11.97 ms | 127.04 → 127.07 |
| `main.c` | 15.63 ms | 15.66 ms | 354.05 → 354.17 |

That is 4.9 times `clang -E` on `sqlite3.c` and 5.7 times on `main.c`. clang's
time includes starting a process, which ours doesn't. With printing, as whole
processes (`perf stat -r 21`), `-E` takes 19.08 ms on `sqlite3.c` (17.59 ms with
`-P`) against `clang -E`'s 57.43 ms (57.18): 3.0 and 3.3 times. On `main.c` it
takes 23.33 ms (22.57). Where the preprocessing time goes, per phase:

| Translation unit | Read | Lex | Refine | Directive tables | Main loop (with directives) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 2.8% | 20.0% | 29.8% | 1.9% | 45.4% |
| `main.c` | 2.9% | 8.9% | 16.2% | 1.0% | 71.0% |

`sqlite3.c` is mostly text: 79% of its output comes from runs of tokens copied
straight from the files. `main.c` is mostly expansion: the encoder's form
tables and the name lists are X-macros, so 100,629 object-like expansions,
21,730 invocations with 162,318 arguments, and 66,530 pastes make more output
tokens than there are input tokens.

**Budget.** The front-end plan (#115) proposed 2 ms to preprocess `sqlite3.c`,
about 16 cycles per output token, next to 2 ms for lexing every input and
1.5 ms for refinement. Measured: 12.00 ms in all, of which lexing is 2.4 ms,
refinement 3.6 ms, and directive tables and the main loop 5.7 ms, 2.8 times
the 2 ms proposed. Each phase's time includes the kernel clearing the fresh
pages it writes, 13% of the samples (see "Where the time goes"). "Next steps"
lists what would close the gap.

## Design

### What is data-parallel, and what is ordered

Each piece is designed vectorized and branch-free first, with an exact scalar
path for its rare cases and a scalar twin (`--scalar`, `PPOptions.scalar`) that
`test-preprocessor` runs on every case and file. The ordered part is C's macro
state: definitions, the disabled macros, and the invocations in progress.
That part steps from one event to the next (a macro name, a directive, the end
of a buffer), never from one token to the next. On `sqlite3.c` it handles
176,000 events for 707,153 output tokens. "Data-parallel design, piece by
piece" below gives each piece's design, its scalar twin and its 9700X
comparison.

### Files

Every file is read once per translation unit. It is mapped with `MAP_POPULATE`,
lexed, refined and given a directive table (`pp_prepare_data`), and it stays
for the whole translation unit, because macro definitions point into the tokens
of the file that defines them. Files are found by path (`pp_lookup_path`, a
table that starts at 1,024 slots and doubles) and shared by device and inode,
so that two paths to one file are one file.

- **Directive tables** (#118). A vectorized scan (`pp_scan_directives_vector`)
  finds every `#` that starts a line (the lexer's start-of-line bit), with its
  kind and the token where its line ends. `pp_finish_directives` pairs the
  conditionals with an explicit stack: each conditional points to the next
  directive of its group (`#elif`, `#else` or `#endif`), so skipping a group
  that isn't taken is a jump. An unterminated group points nowhere, and the
  error comes when the file ends.
- **Include guards and `#pragma once`** (#120). A file whose tokens all lie
  inside `#ifndef X` … `#endif`, or `#if !defined X` / `#if !defined(X)`, has
  `X` as its guard. An inclusion while `X` is defined reads nothing, as an
  inclusion after `#pragma once` doesn't: `sqlite3.c` skips 98 inclusions this
  way, as clang does.
- **Lines.** `pp_line_of` counts newlines (64 bytes at a time, with
  popcounts) from a per-file cursor, because the preprocessor asks about later
  places as it goes (`__LINE__`, diagnostics, line markers). Each file's lines
  are counted about once.
- **Memory.** Each column of the files' tokens has an arena of its own: kinds,
  starts and values. A file's columns are pushed for the lexer's bound, one
  token per byte, then trimmed to the tokens it has. Before this, the columns
  of every file shared one arena, and the unused room between them was cleared
  too wherever a huge page held a used byte: 9 bytes per input byte.

### Macros

The macro table is indexed by `IdentId` (`macro_of`). A bitset of the names
that are macros (`is_macro`) is what the main loop tests. A definition records
where its replacement list is in the defining file's tokens, and nothing is
copied (#121). A redefinition that differs (C 6.10.4p2: the same form,
parameters, replacement list, and whitespace in the same places) is diagnosed,
and replaces the old definition either way.

A function-like macro is compiled at its first expansion (`pp_compile_macro`)
into body items: tokens, parameters (by index), `#` with its parameter, `##`,
and `__VA_OPT__` with its content. Each parameter gets a bit saying whether it
is ever substituted expanded, so arguments only `#` or `##` use are never
expanded.

### The main loop

The main loop (`pp_run_contexts`) reads from a stack of contexts. A context is
either a file (its tokens up to its next directive) or a buffer of tokens (a
macro's expansion, an argument being expanded, or a directive's line):

- **Runs.** Tokens that name no macro go straight to the output, 64 at a time.
  `pp_copy_file_run_vector` finds a run's identifiers with one compare and tests
  their ids against `is_macro` one by one; on Zen 5 a gather is 64 uops. It
  then stores every column with whole-vector stores: kinds without the
  start-of-line bit, flags from the leading-space and start-of-line bits, and
  values and locations offset into the translation unit's location space.
  `pp_copy_buffer_run_vector` does the same for buffers, and paints a name of a
  disabled macro (`BUSTER_PP_NO_EXPAND`). A painted name never expands again, as
  in clang.
- **Object-like macros.** One whose body names no macro and has no `##` is
  copied to the output directly, the commonest expansion (a constant: 28,197 of
  `sqlite3.c`'s 31,587). Any other is built in a buffer and pushed as a context,
  with the macro disabled until that context is popped.
- **Function-like macros** (`pp_invoke`). When the `(` and `)` are both in the
  top context, which is the common case, `pp_collect_arguments` finds the `)`
  and the separating commas 64 kinds at a time and copies the arguments in one
  go. Otherwise the main loop collects them a token at a time
  (`pp_collect_step`), from the contexts below (popping the ones they exhaust,
  which enables their macros again), and runs any directive on the way. A
  directive among a macro's arguments is undefined behavior that clang accepts
  (and warns about only with `-pedantic`), so we accept it too.
- **Nothing nests through calls.** An argument that names an enabled macro is
  expanded before substitution by pushing it as a barrier context with a
  capture. The main loop fills the capture until the barrier ends, and then
  `pp_finish_argument` goes on with the invocation (`PPInvocation` keeps the
  state of each invocation in progress). A `#if`, `#elif`, computed `#include`
  or `#line` line, and a builtin's operand (collected like arguments), are
  expanded the same way, each with a capture whose continuation takes it when
  its barrier ends (`pp_finish_capture`: `pp_finish_condition`,
  `pp_finish_include`, `pp_finish_line`, `pp_finish_builtin`). So a directive
  among a macro's arguments whose line needs expanding, or a builtin's operand
  inside a `#if` line, is one more frame on an explicit stack. There is no
  recursion, which `./build.sh check-call-graph` (#112) checks.
- **Substitution** (`pp_substitute`, C 6.10.4) copies runs of body tokens in one
  go and substitutes each parameter: by its expanded argument, or the raw one
  next to `##`. It also handles `#` (`pp_stringize`), `##` (`pp_paste`, with
  placemarkers for empty arguments), `__VA_OPT__`, and GNU's
  `, ## __VA_ARGS__`, which drops the comma when the variadic argument is
  omitted.
- **`__VA_OPT__` next to `##`** (C 6.10.5.2) is a placemarker when the
  variadic argument is absent or the content gives no tokens. Otherwise `##`
  pastes the content's first token (`##` on its left) or its last (`##` on its
  right). As in clang:
  - an empty argument at either end of the content is a placemarker for that
    end's `##`: `__VA_OPT__(a x) ## y` gives `a y` for an empty `x`;
  - content made only of placemarkers gives no tokens.

  GCC differs in one case: `a ## __VA_OPT__(x y) ## b`, with both arguments
  empty, is `ab` in clang and `a b` in GCC.
- **Spacing.** An expansion that gives no tokens passes its macro name's
  leading space and start of line to the next token read from the same context.
  So does an empty argument, and so does a `__VA_OPT__` to the first token after
  it. A context's `pending_flags` and `trailing_flags` carry these, as clang's
  token lexers do. The spacing is visible in strings: with `#define EMPTY`,
  stringizing `a EMPTY+b` gives `"a +b"`.

The three stacks of tokens (arguments, captures and expansions) are LIFO
arenas. Popping a context gives its tokens back when they are on top.

### Directives and `#if`

- **Directives** run in their file's order (`pp_directive`). `#if` lines are
  expanded first (#123): with `#define BAD(x,y) x`, `#if 0 && BAD(1)` is an
  error, as in clang, while `#if 0 && (1/0)` is not. They are then evaluated by
  `pp_evaluate` with an explicit stack, over C23's integer constants: binary,
  digit separators, `true` and `false` in C23 mode, and character constants
  with clang's values.
- **Includes** (#119) are searched as clang searches them. A quoted name looks
  in the includer's directory, then the `-iquote` directories. Both forms then
  look in `-I`, `-isystem`, clang's resource directory (the highest version
  under `/usr/lib/clang`), `/usr/local/include`, `/usr/include` and
  `-idirafter`. `#include_next` resumes after the directory that found the
  includer: this inclusion of it, which each file context keeps, since a file
  can be found through two entries (`-iquote d` and `-I d`). `__has_include`
  and `__has_include_next` use the same search.
  - As in clang, directories that don't exist are dropped, and so are later
    copies of a directory (by device and inode). A non-system directory that is
    also a system one is dropped in favor of the system one. Otherwise a
    repeated `-I` would make `#include_next` find the same file again.
  - A header name is read without its line splices, whether it is written out,
    comes from a macro, or is `__has_include`'s operand.
  - `__has_include` also works inside a macro's argument in a `#if` line.
- **Pragmas** are acted on when they are `once`, `GCC system_header`,
  `push_macro`/`pop_macro` and `GCC poison`. Every other pragma, and `#ident`,
  goes to the output as `#`, its name and its line's tokens (never expanded),
  followed by a `BUSTER_PP_KIND_PRAGMA_END`, and `-E` prints it on a line of its
  own as clang does. So `#pragma pack` reaches the parser at its place in the
  stream (#124). `_Pragma` from an expansion is destringized and handled the
  same way. An operand that isn't a string literal is expanded first, as in
  clang and GCC, and must then be one.
- **`#line` and line markers** set presumed lines and files apart from the
  physical ones. One without a file name keeps the presumed name. `__LINE__`
  in a macro's expansion is the line of the end of the outermost expansion, as
  in clang. Where clang and GCC differ, we follow clang:
  - The lines after a `#line` count from the line where clang lexes its
    number. For digits right after a line splice, clang reads the splice as part
    of the token, so that is the splice's line: `#line \` then `100` on the next
    line counts from the `#` line. With blanks between the splice and the digits,
    it is the digits' line.
  - `__BASE_FILE__` is the presumed name at the root of the include stack:
    - a `#line` with a name in the main file renames it;
    - a forced include's root is the `<built-in>` file that holds the
      `-include` lines.

### Built-in macros and the predefines

`__FILE__`, `__LINE__`, `__COUNTER__`, `__INCLUDE_LEVEL__`, `__BASE_FILE__`,
`__FILE_NAME__`, and `__DATE__`, `__TIME__` and `__TIMESTAMP__` (from
`SOURCE_DATE_EPOCH` when it is set) are built in. So are the feature queries:
`__has_attribute`, `__has_builtin`, `__has_feature`, `__has_extension`,
`__has_c_attribute`, `__has_declspec_attribute`, `__has_warning`,
`__is_identifier` and the `__is_target_*` queries.

`__is_identifier(x)` is 1 when `x` is an identifier that isn't a keyword in the
language mode (`c_name_is_keyword`), as clang answers. Its operand isn't
expanded, so a macro's name is an identifier.

The feature queries answer from tables of what clang 23 answers nonzero, for
x86-64 Linux in C. They were generated by asking clang about every name the
corpora query:

1. Collect the arguments of every `__has_attribute(...)`, `__has_builtin(...)`,
   `__has_feature(...)`, `__has_extension(...)`, `__has_c_attribute(...)` and
   `__has_warning("-W...")` that is an identifier (or `ns::name`, or a warning
   option). Take them from `/usr/include`, clang's resource directory,
   musl, `sqlite3.c` and Zig 0.14.1's `lib/libc`. Add the common GNU attribute
   names, with their `gnu::` spellings for `__has_c_attribute`.
2. For each kind, write one line `@@ __has_KIND(NAME)` per name and run
   `clang -E -P -undef -x c -std=gnu17 -` with `CPATH` unset. Repeat with
   `-std=c23`; the answers were the same. Read the values back in order.
3. Keep the nonzero answers, sorted, as the `PPFeature` tables. Attribute names
   are stored without leading and trailing `__`, because clang normalizes them.
   An earlier version kept `__x__` spellings, and `sqlite3.c` differed from
   clang at glibc's `sys/cdefs.h`.

The configuration is clang's (the owner's decision in #117): `-std=gnu17` by
default, clang's include search, and clang's predefined macros plus
`__BUSTER__` defined to 1. The predefined macros are clang 23.1.3's for
`x86_64-pc-linux-gnu` in gnu17 (`clang -dM -E -x c /dev/null`), without the
ones the language mode and options decide. `pp_predefines` adds those:
`__STDC_VERSION__`, `__STRICT_ANSI__` or `linux` and `unix`, the inline
semantics, `__STDC_HOSTED__`, `__OPTIMIZE__` or `__NO_INLINE__`, C23's
additions, and `__BUSTER__`. The C23 additions are the difference between that
command's output under `-std=c23` and under the default.

Target options such as `-march` don't change them yet, though #117 asks for
clang's macros for the same `-march`. Until then, `--predefines FILE` replaces
the built-in predefines (`__BUSTER__` too) with a file of `#define` lines, such
as `clang -dM -E` gives for the same options. The oracle uses this, so its runs
start from clang's macros alone.

### Tokens, locations and the output

A token in a buffer or the output has four parts:

- a kind: `C_TOKEN_*`, or a keyword's kind in the final output;
- flags: `BUSTER_PP_SPACE`, `BUSTER_PP_START_OF_LINE`, `BUSTER_PP_NO_EXPAND`
  and `BUSTER_PP_FROM_MACRO`;
- a value: an identifier's `IdentId`, else the location of its end;
- a location.

Locations are offsets in the translation unit's location space. Each inclusion
of a file gets a range of its own, as do the scratch spellings that pasting,
stringizing and the builtins make (`PPRange`), so any token's spelling is the
bytes from its location to its end. A pp-number stays a pp-number:
`int x = 1e+;` inside `#if 0` is fine.

At the end, `pp_resolve_keywords_vector` gives identifiers their keyword kinds
for the language mode, 64 tokens at a time. A keyword stayed an identifier
until then, because it can be a macro name.

`pp_print` writes `-E`'s text. A token gets a space where whitespace was before
it, or where it would otherwise lex together with the token before
(`pp_needs_separation`). Each source line goes on a line of its own, pragmas on
lines of their own, and unless `-P`, a line marker (`# 12 "file.h" 2`) appears
wherever the output moves to another file or skips lines.

### Vectorized kernels and their scalar twins

`PPOptions.scalar` selects the scalar twin of each vectorized kernel:

- refinement;
- the directive scan, and the conditionals' pairing;
- the file and buffer runs;
- the bulk copies (arguments, body runs, object-like bodies), and the
  argument scan;
- identifier pastes, which the vectorized kernels intern without lexing;
- the keyword pass;
- the printer.

`test-preprocessor` requires the two to produce the same stream, token for
token (kind, flags, value and location), and the same directive tables.
Both printers must give the same bytes, with line markers and without.

## Data-parallel design, piece by piece

Measured on the 9700X as in "Results". An A/B ran each pair of builds by
turns, four rounds of 21 passes each, because two builds of the same code
differ by up to 1% from code placement alone.

| Piece | Vectorized design | Scalar twin and rare cases | 9700X comparison |
| --- | --- | --- | --- |
| Refinement (#116) | Pass A per 64 tokens; pass B batches of 64 identifiers (`docs/c-refinement.md`) | `refine_tokens_scalar`; spliced, UCN and long identifiers | 4.2 times the scalar twin on `sqlite3.c` |
| Directive scan (#118) | A compare of 64 kinds with `#` and the start-of-line bit | Token at a time | Part of the directive tables' 1.9% |
| Conditional pairing (#118) | Depth as a prefix sum, pairing as nearest smaller values by pointer jumping, 32 conditionals at a time | The stack; files with a continuation too many | 16% faster on a file of 36,868 conditionals; within noise on `sqlite3.c` |
| File and buffer runs (#122) | 64 tokens per step: one compare finds the identifiers, whole-vector stores | Token at a time | The scalar twins: 22.8 ms against 12.0 ms in all |
| Argument scan (#122) | Paren depth as a byte prefix sum; the `)` and the top-level commas from compares | Token at a time, through the contexts below | Equal on `sqlite3.c`; 0.4% faster on `main.c`, 1.5% fewer instructions |
| Macro-name test (#122) | A scalar load per identifier a run's compare finds (a gather is 64 uops) | The same | 1.5% of `sqlite3.c`'s time; a postings index costs more to build |
| Pastes (#122) | Two identifiers, or an identifier and a number, interned without lexing | Lexing every paste | `main.c` (66,520 pastes) 16.5 to 15.2 ms |
| `#if` (#123) | Scalar: the lines are short (below) | Precedence parser with explicit stacks | Under 0.3% of `sqlite3.c`'s time |
| Keyword pass (#124) | 64 kinds at a time against a table of the language mode's keywords | Token at a time | Not measured on its own |
| Printer (#124) | Spellings, then spaces for 64 tokens from class tables (`vpermi2b`), then masked 64-byte copies into one buffer | `pp_print_scalar`, through stdio | `-E` on `sqlite3.c` 25.5 to 18.9 ms; `-E -P` on `main.c` 29.3 to 22.1 ms |

**Conditional pairing.** A conditional's level is the number of groups open
before it, less one for a continuation (`#elif`, `#else`, `#endif`). The
directive after `d` in `d`'s group is then the nearest continuation after `d`
at `d`'s level or lower: a nearest-smaller-values problem.

`pp_pair_conditionals_vector` works in four steps:

1. It compresses the conditionals out of the directive table, 16 at a time
   (`vpcompressd`).
2. It computes their levels 32 at a time, as an exclusive prefix sum of +1 at
   an opener and −1 at an `#endif`. Five `vpermw` shift-and-add steps do this
   (`vpermw`: one uop, latency 4 on Zen 5).
3. Each lane follows a pointer from the next lane. A lane whose candidate is an
   opener, or is deeper, takes over that candidate's own pointer. In a
   balanced file this never passes the lane's answer, and about five rounds
   settle 32 lanes.
4. Lanes still unanswered at a block's end are the open groups, as many as the
   nesting depth. Each later block answers them with one compare of its levels
   against theirs.

A continuation without an open group (an `#endif` too many) sends the file to
the stack, whose pairing the directives' error messages need. On a synthetic
file of 60,000 directives, 36,868 of them conditionals, the directive tables
took 0.326 ms against the stack's 0.387 ms (−16%; the whole run −2.3%). On
`sqlite3.c`, where the headers' conditionals are sparse, the directive tables
stay at 1.9% of the time either way.

**The argument scan.** For 64 kinds at a time, `pp_collect_arguments` works
without a branch per token:

- The depth of parentheses after each token is the prefix sum of +1 at `(` and
  −1 at `)` on the depth before. Six `vpermb` steps compute it
  (`pp_prefix_sum_bytes`).
- The invocation's `)` is the first token after which the depth is −1 (a
  compare and a `tzcnt`).
- The separating commas are those at depth 0 before it. In a variadic macro,
  `pdep` keeps the commas the named parameters take, and the variadic argument
  keeps the rest.

The remaining loop runs per argument, not per token. Depths past 63
(8-bit lanes), more than 64 arguments, or a `)` beyond the top context send
the invocation to the token-at-a-time path. Cases for each are in the tests.
The A/B against the previous scan, which branched on each parenthesis and
comma, gave:

- `sqlite3.c`: 12.03 and 12.08 ms, within noise;
- `main.c`, whose invocations average 7.5 arguments: 15.35 and 15.29 ms
  (−0.4%), with 1.5% fewer instructions.

**The macro-name test.** A run's identifiers come from one compare of 64 kinds.
Each one's id is then tested in the `is_macro` bitset with a scalar load: a
gather (`vpgatherdd zmm`) is 64 uops on Zen 5. In the profile of `sqlite3.c`,
that loop's lines account for 1.5% of the time (0.18 ms).

The alternative removes the per-token test. Per file, it would keep the token
positions of each distinct id (postings), so that entering a file and each
`#define` or `#undef` could set a per-token macro bitset that runs read 64 bits
at a time. Building postings means bucketing every identifier occurrence by
its id: two passes over `sqlite3.c`'s 450,000 identifiers with a counter
increment each. That already costs about what the test does, before the bitset
updates. So the scalar loads stay. Postings would pay where files are cached
across translation units (#150, #152), where building them is amortized.

**`#if` expressions.** A vector evaluator would build each line's expression
tree as the parser research does. Precedence, with the depth of parentheses
folded in, gives each operator's parent as the nearer of its nearest
lower-precedence operators (nearest smaller values). The tree is then
evaluated bottom-up by level, with table lookups, marking the operands that
`&&`, `||` and `?:` leave unevaluated, so that `0 && (1/0)` raises no error.
It was not built, for two reasons:

- `sqlite3.c` evaluates 1,738 lines of 5.3 tokens on average after expansion
  (34 longer than 32 tokens, the longest 105), and `main.c` 790 lines of 7.4.
- All the evaluation together is under 0.3% of the run, so no line is long
  enough to pay for the setup.

`pp_evaluate` stays a precedence parser with explicit stacks, and the
benchmark reports the line lengths, so generated code with long conditions
would show.

**The printer.** `pp_print_vector` prints 64 tokens at a time into a 1 MB
buffer, written with `fwrite` as it fills, in three steps:

1. Each token's spelling: an identifier's from the intern table, any other
   token's bytes at its location. The location is found through a directory of
   256-location pages instead of a binary search over the ranges. A masked
   64-byte load checks a literal for the rare backslash.
2. The spaces of all 64 tokens at once. A token at the start of a line gets
   none. Any other token gets one if whitespace was before it, or if its first
   character would lex together with the previous token's last. That is a
   lookup in a 5 × 8 table of character classes, with the classes from
   `vpermi2b` lookups and the previous token's class moved up a lane by one
   `vpermb`.
3. The copies: a line break, a space, and one masked 64-byte store per token,
   with branches only for line markers (which `-P` doesn't print), a run of
   blank lines, and spellings longer than 64 bytes.

Line markers count lines from the last marked token in the same file
(`pp_print_line`), not through `pp_line_of`'s per-file cursor and column scan.
The scalar printer, `pp_print_scalar`, writes a token at a time through stdio.
The two give the same bytes on every test. A variant whose first step had no
branch on the kind (it looked up both spellings and picked one) was 1–2%
slower in an A/B: the location lookups it adds for identifiers cost more than
the mispredictions it saves.

## The oracle

`./build/compiler test-preprocessor [--clang] [--keep] [options] [file...]`:

- **Cases.** Without files, it runs the 63 built-in cases. These are cases of
  our own on the topics of C 6.10.4.5's examples (rescanning and
  redefinitions, `#` and `##` with indirection, placemarkers, variadic macros),
  inputs past the vectorized kernels' limits, and the behaviors probed against
  GCC 16 and clang 23 (the research fixtures among them):
  - a splice makes a function-like macro, but a comment doesn't;
  - stringizing across splices and comments;
  - painting;
  - an alias's invocation;
  - arguments that aren't expanded;
  - `#if 0 && BAD(1)`;
  - `true` in C17 and C23;
  - `__VA_OPT__`, and GNU's comma;
  - pastes, including `1 ## .e+`;
  - stringizing spacing and escapes;
  - builtins;
  - guards and `#pragma once`;
  - pragmas;
  - `#if` arithmetic;
  - `#elif` chains;
  - identifiers written with UCNs (printed as UTF-8, but stringized as
    written);
  - redefinitions: `1` and `01`, `[` and `<:` and renamed parameters warn,
    while other amounts of whitespace and spellings across line splices don't
    (each case states its exact number of warnings);
  - `1e+` stays one pp-number, in active code and in a skipped group;
  - `#if 0 && (1/0)` skips an `#error`;
  - an argument across two 64-token blocks, parentheses 70 deep and 70
    arguments (the argument scan's slow path);
  - conditionals nested 40 deep, a group around 50 others, and unbalanced
    groups (the pairing's blocks and its fallback to the stack);
  - directives among a macro's arguments (a `#if` line expanded there, a
    `#define`), a builtin's operand from another context and with a `#define`
    in it, `_Pragma` from a macro, a computed `#include`, and `#line` with a
    macro (the main loop's frames);
  - the cases from #193's review:
    - missing headers by absolute path;
    - `__VA_OPT__` on either side of `##`;
    - spaces through empty expansions;
    - `__has_include` in a macro's argument;
    - header names with line splices;
    - a repeated `-I` with `#include_next`;
    - `_Pragma` of a macro;
    - `#line` without a file name;
    - `__is_identifier`;
    - `true` in C99;
    - the two places where we follow clang rather than GCC;
  - the cases from #193's second review:
    - a directory under both `-iquote` and `-I` with `#include_next`;
    - spliced literals from macros as header names;
    - pending spaces in `#__VA_OPT__`;
    - `__BASE_FILE__` after `#line`;
    - `#line` continued with its number indented.

  A case can name more files, and options for both sides (`-I`, `-include`,
  with `%d` for its directory). Each case is preprocessed by both kernels, with
  the built-in predefines. Their streams and directive tables must be the same,
  and their tokens' spellings must be the expected ones. With `--clang`, each
  case is also preprocessed by `clang -E -P` and must give the same spellings.
- **Predefines and keywords.** The cases and files under `--clang` give our side
  clang's predefines, so `--clang` checks our built-in ones separately:
  - In ten language modes (`c89` to `gnu23`), with `-ffreestanding` and with
    `-O2`, 14 option sets in all, `pp_predefines` must give `clang -dM -E` line
    for line. Only the three macros that name clang's patch release are left
    out.
  - In each mode, `__is_identifier` of every predefined name (keywords,
    directive names, the preprocessor's names) must give clang's answer.
- **Files.** With files, each is preprocessed with the options, by both
  kernels, and compared. With `--clang`, our side reads clang's own predefines
  for the same options (`clang -dM -E`, through `--predefines`), so both start
  from the same macros. clang's `-E -P` text is lexed with `c_tokenize` and
  compared with our stream's spellings, token for token. The first difference
  is printed with the tokens around it.

In both, the two printers must give the same bytes, with line markers and
without, and the text must lex back into the stream's spellings.

So "token-identical" means the same sequence of token spellings. Whitespace
and line breaks may differ: clang's `-E` spacing heuristics aren't part of the
contract.

The configurations behind the results table:

```sh
./build/compiler test-preprocessor --clang
./build/compiler test-preprocessor --clang sqlite3.c
# in musl-1.2.5, after generating obj/include/bits/alltypes.h, obj/include/bits/syscall.h and
# obj/src/internal/version.h as its Makefile does:
../build/compiler test-preprocessor --clang -std=c99 -nostdinc -ffreestanding -D_XOPEN_SOURCE=700 \
    -I./arch/x86_64 -I./arch/generic -Iobj/src/internal -I./src/include -I./src/internal -Iobj/include \
    -I./include $(ls src/*/*.c src/*/x86_64/*.c)
# in this repository:
./build/compiler test-preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char \
    src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
# in lua-5.4.8/src:
../../build/compiler test-preprocessor --clang -std=gnu99 -O2 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX *.c
```

The C library corpus (Zig 0.14.1's `lib/libc`) is a collection of headers and
sources for many targets without their build configurations, so it isn't a set
of translation units to preprocess. Refinement checks it file by file
(`docs/c-refinement.md`), and musl stands for it here.

## Where the time goes

The benchmark counts what a run does (`PPStatistics`). On `sqlite3.c`, four
counts match clang's own statistics (`-Xclang -print-stats`, in #117) exactly:
31,587 object-like expansions (28,197 copied straight out, 3,390 through a
buffer), 21,755 function-like invocations, 3,312 pastes, and 98 includes
skipped by guards. The run also does:

- 16,746 directives;
- 31,626 file runs, which copy 560,742 tokens;
- 44,722 buffer runs, which copy 120,722 tokens;
- 42,978 arguments, 3,383 of which are expanded before substitution;
- 1,738 `#if` and `#elif` lines, of 5.3 tokens on average after expansion.

The 9700X profile of `sqlite3.c` (vectorized, `perf record` at 15 kHz, the
shares of all samples above 0.6%):

| Function | Share |
| --- | ---: |
| `refine_tokens_64_plain` (refinement) | 21.84% |
| `tokenize_c` (the lexer) | 14.42% |
| `clear_page_erms` (the kernel clearing fresh pages) | 13.30% |
| `pp_expand_current` (with `pp_invoke` and `pp_expand_object` inlined) | 7.94% |
| `pp_run_contexts` (with the run copies inlined) | 7.82% |
| `pp_substitute` | 4.86% |
| `intern_find_or_add_hashed` (new spellings) | 3.21% |
| `pp_append_tokens` | 2.52% |
| `pp_continue_invocation` | 1.57% |
| `pp_directive` | 1.47% |
| `pp_enter_file` | 1.42% |
| `pp_line_of` | 1.28% |
| `unmap_page_range` (the kernel, at each run's end) | 0.89% |
| `pp_condition` | 0.54% |

These rounds of changes, each measured on the 9700X on `sqlite3.c` (vectorized,
medians of 21 passes):

| Change | ms | Cycles/output token | Instructions/input token |
| --- | ---: | ---: | ---: |
| First complete version (token-identical on every input above) | 15.71 | 78.10 | 139.26 |
| Each array of records and each token column in an arena of its own | 13.98 | 78.25 | 139.08 |
| Files mapped (`MAP_POPULATE`) instead of read into the arena | 13.43 | 80.09 | 139.44 |
| Each file's token columns trimmed to its tokens, in arenas of their own | 13.07 | 80.37 | 140.54 |
| The path table starts at 1,024 slots instead of 2 million | 12.69 | 80.13 | 140.55 |
| Arguments collected in bulk from the top context; body runs and object-like bodies copied in bulk; placemarkers removed only when there are any | 12.39 | 77.83 | 131.33 |
| Bulk copies store only the groups of 16 that hold tokens | 12.00 | 74.66 | 126.39 |
| Leading-space words allocated exactly, after lexing | 11.83 | 74.34 | 126.42 |
| A paste of two identifiers (or an identifier and a number) interned without lexing it | 11.96 | 75.26 | 125.25 |
| The argument scan by a prefix sum, the conditionals' pairing by nearest smaller values (each within noise in its A/B) | 12.00 | 74.83 | 125.26 |

(Lines were already counted from a per-file cursor in the first row; a line
table built per file had taken 9% of an earlier profile. The pastes mattered for
`main.c`, 16.5 ms before and 15.2 ms after, with 66,530 of them. Runs differ by
about 2%.)

The early versions spent 18 to 31% of their time in the kernel's
`clear_page_erms`, because the arenas' layout touched far more huge pages than
the tokens needed. The arenas now touch about what they use, but every run
still clears its memory once. For `sqlite3.c` that is about 18 MB of tokens
(the files' columns at 9 bytes per input token, the output's at 10 per output
token), the intern table, and the unused part of each arena's last huge page.
In a process that preprocesses many translation units (#152), that memory is
cleared once and reused.

## Not yet

- `#embed` is an error, and `__has_embed` isn't answered (C23).
- Target options (`-march`, `-m*`, `-funsigned-char`) don't change the built-in
  predefines; they come only through `--predefines`. #117's configuration asks
  for clang's macros for the same `-march`.
- The oracle uses the installed `/usr/include`, not the pinned copy of the
  headers that #125's decision asks for (with its hashed manifest).
- The feature tests (`__has_attribute` and the others) always answer from
  clang's table, which #117 and #125 keep for the oracle's mode. Outside it, they
  are to answer what the compiler implements (#123, #148).
- Line markers in `-E` output aren't compared with clang's; the oracle compares
  `-P` output. The printer's spacing also differs from clang's in places: it
  puts a space between `<` and `>`, where clang doesn't.
- Diagnostics give `file:line:column: error: message` and no more: no notes, no
  macro backtraces (#108), and no carets.
- C++ (`-x c++`), dependency output (`-M`) and `-dM` aren't supported.
- Stringizing an identifier that pasting made from a UCN gives its UTF-8
  spelling, where clang keeps the UCN.
- `defined` that a macro's argument brings into a `#if` line (`ID(defined X)`)
  is an error here. clang evaluates it in the line, with a warning that it is
  undefined behavior.

## Next steps

- **The main loop** costs about 170 cycles per event: a run, an expansion, an
  invocation, an argument or a paste. On `sqlite3.c`, directives and the main
  loop take 5.6 ms (31 million cycles at 5.5 GHz) for 176,000 such events.
  Candidates, each to be measured on the 9700X:
  - keep the hot fields of `Preprocessor` and `PPTokens` in locals across a
    step: with `-fno-strict-aliasing`, every byte store reloads them;
  - resolve chains of object-like aliases without a context each;
  - memoize the expansions of macros without arguments (#110).
- **Clearing pages.** In a single run, prefaulting the arenas on another core
  while the first files are lexed would hide most of the 13%. Across many
  translation units, the arenas are reused (#152).
- **Lexing and refinement** are 50% of `sqlite3.c`'s time. Both kernels run at
  about their own benchmarks' speeds here: `tokenize_c`'s samples come to 7.8
  cycles per input token and `refine_tokens`' to about 12, against 7.89 and
  10.50 in their benchmarks. So their next steps apply (`docs/c-lexer.md`,
  `docs/c-refinement.md`).
- **Streaming to the parser.** The first version preprocesses a whole
  translation unit before the parser reads it. Streaming needs a rule for when
  a block of output is final, since a function-like macro name at the end of an
  expansion can take a `(` from the input after it (#117).
- **The rest of C23** (`#embed`), the compiler's own headers (#117's open
  question; this version uses clang's resource directory), and per-file jobs
  cached for the process (#150).

## Reproduction

```sh
./build.sh
./build/compiler test-preprocessor --clang
./build/compiler test-preprocessor --clang path/to/sqlite3.c
taskset -c 2 ./build/compiler benchmark-preprocessor --clang path/to/sqlite3.c
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
taskset -c 2 ./build/compiler benchmark-preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char \
    --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler -E [-P] [-o file] [options] file.c
```

On the 9700X, run each one under `EXCLUSIVE_OWNER=<name>
~/buster-fast-bench/exclusive.sh`. `benchmark-preprocessor --only vectorized`
runs one implementation, for `perf record`.
