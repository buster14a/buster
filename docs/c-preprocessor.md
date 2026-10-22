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
| This repository's translation units (at 8ee1e16, with `main` at c93219a), with `-march=native` (AVX-512 intrinsic headers) | 19 | 12,021,842 |
| Lua 5.4.8 (`-DLUA_USE_LINUX -DLUA_COMPAT_5_3`) | 34 | 588,066 |
| The built-in cases (`pp_test_cases`) | 222 | |
| #109's generated cases, against `clang -E -P` ([the fuzzing](preprocessor-fuzz.md)) | 40,000,000 | |

The line markers agree too: where clang's `-E` markers put each token (its
presumed line, and its file and the files that include it), ours put it, on the
cases, `sqlite3.c`, musl, this repository's translation units and Lua.

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

The third review's fixes come with #192's keyword list from clang:
- comma elision;
- `#__VA_OPT__`'s space;
- definitions checked where they are made;
- the signedness of character constants;
- `__TIMESTAMP__`;
- the features by mode;
- `#line`'s operands;
- the line markers' presumed positions.

Together they add 0.6% to 0.7% instructions and 1.1% to 1.4% time against their
reviewed commit (this branch's `main.c`, which has grown since the first table):

| Translation unit | Reviewed commit | With the fixes | Instructions/input token |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 11.99 ms | 12.12 ms | 127.07 → 127.99 |
| `main.c` | 18.74 ms | 19.00 ms | 358.03 → 360.01 |

`-E` costs about the same, timed as whole processes with `perf stat -r 21` over
four rounds on `sqlite3.c`:
- with markers, 19.34 → 19.41 ms;
- with `-P`, 17.75 → 17.65 ms.

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

## Object-macro eligibility cache experiment (#110)

The cheap first step of [#110](https://github.com/buster14a/buster/issues/110)
was measured on October 21, 2026 and **rejected**: caching whether an
object-like macro's body needs rescanning did not save more time than the
measured spread. No cache state, switch or benchmark overhead from this
experiment is retained in the compiler.

The reproducible prototype is
[`d8109dd8f40c03db6c0288056154308ca552ecc5`](https://github.com/buster14a/buster/commit/d8109dd8f40c03db6c0288056154308ca552ecc5),
on `codex/preprocessor-cache-110`, based on
`ea83392b5b7d0dbef35441c2b96a1eadd407da0c`. It adds a definition epoch,
advanced by definitions, undefinitions and macro push/pop, and an epoch-tagged
eligibility flag per macro. It caches both answers to "the body contains no
currently defined macro and no paste", leaving the existing body-copy,
spacing, location and rescan paths intact. Disabled macro names still prevent
direct copying; changing the disabled set does not invalidate this conservative
classification. Epoch wrap clears the tags. `PPMacro` grows from 56 to 64 bytes.

The classification has an exact scalar twin. Its vector path checks kind
bytes with bounded masked AVX-512 loads, then tests the macro bitset with
scalar loads for the selected identifiers; it uses no gather. Target assembly
was inspected. Both measured variants use this classifier, so their difference
isolates the epoch lookup and reuse within the instrumented prototype.

### Inputs and method

All target builds, tests and measurements took the shared 9700X lock, separately
per command. `build.sh` used Clang 23.1.1, `-O3 -march=native`, on the Ryzen 7
9700X. Timings were pinned to CPU 2. Each process ran one warm-up and 21 measured
passes per variant, rotating their order, with a fresh preprocessor and intern
table for each translation unit. Initialization, file reads, lexing, refinement,
directives and expansion are included; teardown, printing and external Clang
are outside the timed interval.
The table gives the median and the 10th/90th percentile samples. These are
end-to-end interleaved measurements, not isolated vector-kernel conditioning
experiments or a claim about a particular power-cap state.

The frozen inputs are under
`~/buster-fast-bench/preprocessor-corpora-astra-01a158dd/` on the 9700X:

- SQLite 3.53.2's `sqlite/sqlite3.c`, with the frozen Clang 23 and glibc headers:
  1,242,079 input tokens and 706,237 output tokens. Its SHA-256 is
  `0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d`.
- The libc workload is the 1,587 sources in `config/musl-ordinary.txt`, from
  Zig 0.14.1's patched musl 1.2.5, with the frozen generated-header overlay:
  18,698,194 input tokens and 6,005,081 output tokens per pass. This partition
  includes foreign-architecture sources and excludes four CRT/loader sources
  that require different flags. It is a preprocessing workload, not a musl
  build or a claim to cover all 8,983 C/header inputs in the mixed libc corpus.
  The input-list SHA-256 is
  `bf47f8562fd1df039a81464164df642c0efb7653e7cb91b0be964978929bb4dc`;
  `manifests/libc.sha256` hashes to
  `bf593a94954f3ed9431dd12e60233ec0229f7226a1fc270a701614a20f86baeb`.

These inputs differ from the earlier measurements above, so compare cache on
and off within this experiment rather than comparing their absolute times or
token counts with the earlier tables. The corpus README and manifests retain
the upstream versions, headers, generated overlay and licenses. No corpus
source is imported into this repository.

### Measurements and decision

| Input / process | Cache off, ms | Off p10–p90, ms | Cache on, ms | On p10–p90, ms |
| --- | ---: | ---: | ---: | ---: |
| SQLite, round 2 | 12.169 | 12.122–12.261 | 12.212 | 12.120–12.292 |
| SQLite, round 3 | 12.199 | 12.100–12.333 | 12.159 | 12.076–12.274 |
| SQLite, round 4 | 12.175 | 12.087–12.281 | 12.119 | 12.062–12.208 |
| SQLite, round 5 | 12.254 | 12.168–12.523 | 12.213 | 12.099–12.327 |
| musl, round 1 | 582.319 | 576.483–589.217 | 584.071 | 574.638–588.091 |
| musl, round 2 | 574.152 | 572.050–582.799 | 576.499 | 572.525–584.839 |
| musl, round 3 | 576.837 | 573.781–586.469 | 576.376 | 571.776–585.285 |

SQLite round 1 was a pilot with the same expansion/benchmark implementation
but an earlier test/main layout; it is excluded from the table. The unchanged
base compiler also took 12.126 ms on the same SQLite input. This control is
consistent with no demonstrated gain, but lacks the prototype's added scan
counters and is not an isolated measurement of their cost.

| Per pass | SQLite | musl |
| --- | ---: | ---: |
| Cache hits | 24,265 | 144,118 |
| Cache misses | 7,302 | 48,471 |
| Body tokens inspected, cache off → on | 56,561 → 10,731 | 542,337 → 103,197 |
| Instructions/input token, cache off → on | 129.64 → 128.82 | 227.65 → 227.21 |
| Object expansions already copied directly | 28,180 / 31,567 | 182,990 / 192,589 |

The scan counts fall by about 81%, but the uncached scan already inspects only
1.79 tokens per object expansion on SQLite and 2.82 on musl. Instruction counts
fall by 0.63% and 0.19%, respectively, including instrumentation. The time
difference changes sign on both corpora and is smaller than the pass spread.
Neither corpus demonstrates a repeatable gain. The added epoch checks and
macro-table field therefore do not earn their place under #110's acceptance
rule. Across processes, the medians themselves span 0.085/0.094 ms on SQLite
and 8.167/7.695 ms on musl (cache off/on), also exceeding the paired changes.

This result covers the eligibility cache only. Fully rescanned output replay
and argument-keyed function-like caching were not implemented or measured.
They remain separate hypotheses, requiring a profile that identifies enough
repeated expansion work to repay storage, lookup and provenance replay. An
epoch alone is insufficient for arbitrary output replay: with `A` expanding
to `B` and `B` to `A`, the painted result depends on which macro is disabled,
without a definition change. Dynamic builtins, `_Pragma`, function-like tails
and #108's expansion histories also have to be handled. This negative result
does not establish that every possible expansion cache would lose.

### Correctness and reproduction

The prototype runs every test with caching on and off, in vector and scalar
modes. On the 9700X the 88 existing Clang-compared fixtures, six cache fixtures,
guarded classifier tails (lengths 0–129, identifiers in every position), forced
epoch wrap, predefined-macro/keyword/feature checks and 432 generated cases
passed with zero cache mismatches. Generated cases use the unchanged #109
generator: 72 case IDs, seeds 1/7/99, C17 and C23. This is a cache equivalence
check, not #109's larger independent-oracle qualification. SQLite and all
1,587 musl sources also matched Clang in their 706,237 and 6,005,081 output
tokens, respectively, with zero cache mismatches.

For provenance composition, the prototype's core and tests were applied to
#108's draft at `42fbc1ed7654cd10420bfcfbc1636e1c6432968e`, preserving its
history hooks. Local tests passed the same cases, all 26 Clang diagnostic
checks and history run/lifetime checks; `main.c` matched in 1,482,336 tokens.
`pp_same_streams` compared expansions, substitutions, generated tokens, source
records and output runs as well as tokens and locations, with zero cache
mismatches. This composition is correctness evidence, not a 9700X performance
measurement. The standalone prototype also passed the local no-recursion
call-graph check (17 translation units, 6,234 functions, zero cycles).

Logs, source checksums and target assembly are retained on the 9700X under
`~/buster-fast-bench/cache-110/`. Every tracked prototype source matched the
`d8109dd` checkout. The tested/timed candidate binary SHA-256 is
`c64180d9476601c67c18f43c04a8266fed85a452123e9f46a17b9adf1a708a18`;
the unchanged control is
`f449cef953cff2ed28c44e79ceda3214b3bd6279d8b5784a1d6b95d990e57f65`.
To reproduce, use a separate checkout of `d8109dd` on the 9700X, build through
the shared lock, and run from that checkout (Bash):

```sh
EXCLUSIVE_OWNER=cache-110-repro ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=cache-110-repro ~/buster-fast-bench/exclusive.sh ./build/compiler test-preprocessor --clang
corpus="$HOME/buster-fast-bench/preprocessor-corpora-astra-01a158dd"
EXCLUSIVE_OWNER=cache-110-repro ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-preprocessor \
    -nostdinc -isystem "$corpus/headers/usr/lib/clang/23/include" -isystem "$corpus/headers/usr/include" "$corpus/sqlite/sqlite3.c"
mapfile -t units < "$corpus/config/musl-ordinary.txt"
for i in "${!units[@]}"; do units[$i]="$corpus/libc/musl/${units[$i]}"; done
EXCLUSIVE_OWNER=cache-110-repro ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-preprocessor \
    -std=c99 -ffreestanding -nostdinc -D_XOPEN_SOURCE=700 -Os \
    -I "$corpus/libc/musl/arch/x86_64" -I "$corpus/libc/musl/arch/generic" \
    -I "$corpus/musl-generated/obj/src/internal" -I "$corpus/libc/musl/src/include" \
    -I "$corpus/libc/musl/src/internal" -I "$corpus/musl-generated/obj/include" \
    -I "$corpus/libc/musl/include" "${units[@]}"
```

The prototype's default benchmark compares `vectorized` (cache off) with
`cached`; `--only all` adds both scalar variants. For corpus correctness,
replace `benchmark-preprocessor` with `test-preprocessor --clang`, keeping
the same input list and preprocessing flags.

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

- **Lexing by the language mode.** The lexer has no language mode, so two of
  clang's rules are applied around it:
  - The `u`, `U` and `u8` prefixes are C11's, and `u8` on a character constant
    is C23's (`L` is every mode's). Before those modes, clang lexes `u'a'` as
    the identifier `u` and the constant `'a'`, so a macro named `u` expands
    there. After lexing, `pp_count_splits_vector` looks the kinds up 64 at a
    time (one `vpermi2b` over the mode's mask), and the rare literal it finds
    is split in place into the prefix and the literal from its quote
    (`pp_split_prefixes`), before refinement interns the prefix. The prefix
    ends after its last `u`, `U` or `8`, so a line splice before the quote stays
    between the two, where refinement reads it as no whitespace. Scratch text
    (`_Pragma`, pastes) is lexed the same way, and a paste that would form such
    a literal is an error, as in clang.
  - Trigraphs are replaced in the ISO modes before C23 (`-std=c89` to
    `-std=c17`), clang's default, and wherever `-trigraphs` or `-ftrigraphs`
    asks for them (`-fno-trigraphs` turns them off). The lexer, refinement and
    the printer read them with `C_TOKENIZER_TRIGRAPHS`.
  - Spellings the preprocessor makes are read again as clang reads them. Some
    are final, as clang's clean tokens are: stringized strings, and the
    builtins' strings and numbers. Their scratch arena (`pp_scratch_clean`)
    tells `pp_without_splices` to leave them alone, so `S(??\`, a line
    splice, then `=)` stays `"??="`. Pastes and `_Pragma`'s text are lexed
    again, so their line splices and trigraphs still come out (`pp_scratch`).

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

- **Directives** run in their file's order (`pp_directive`), even among a
  macro's arguments, as clang runs them there. The exceptions are the ones
  clang rejects there: `#include`, `#include_next`, `#import`, `#pragma` and
  `#embed` are an error, and the invocation goes on without them. `#if` lines
  are expanded first (#123): with `#define BAD(x,y) x`, `#if 0 && BAD(1)` is an
  error, as in clang, while `#if 0 && (1/0)` is not. They are then evaluated by
  `pp_evaluate` with an explicit stack, over C23's integer constants: binary,
  digit separators, `true` and `false` in C23 mode, and character constants
  with clang's values.
- **Includes** (#119) are searched as clang searches them. A quoted name looks
  in the includer's directory, then the `-iquote` directories. Both forms then
  look in `-I`, `-isystem`, clang's resource directory (the highest version
  under `/usr/lib/clang`; with `--buster-headers`, buster's own headers and
  then that directory without their names, as "The compiler's own headers"
  says), `/usr/local/include`, `/usr/include` and `-idirafter`. `#include_next`
  resumes after the directory that found the includer: this inclusion of it,
  which each file context keeps, since a file can be found through two entries
  (`-iquote d` and `-I d`). `__has_include` and `__has_include_next` use the
  same search.
  - As in clang, directories that don't exist are dropped, and so are later
    copies of a directory (by device and inode). A non-system directory that is
    also a system one is dropped in favor of the system one. Otherwise a
    repeated `-I` would make `#include_next` find the same file again.
  - A header name is read without its line splices, whether it is written out,
    comes from a macro, or is `__has_include`'s operand.
  - `<...>` is read from the source up to the first `>` on the line, so `//`
    and `/*...*/` there are part of the name, as in clang: `<a//b.h>` and
    `<a/*x*/b.h>` (#119). Without a `>` on the line, the line's tokens up to a
    `>` make the name, as clang's `LexHeaderName` joins them: a space for each
    one that had whitespace before it, the `>` too. So with a comment that runs
    on to the next line, `<a/*x` then `*/b.h>` names `a b.h`, and `< c.h >`
    from macros names ` c.h `.
  - A `<...>` with its `>` on the line is a name written out however long it
    is, never a computed one: its identifiers aren't expanded (#254's review).
    `#include` reads the whole name, so one too long for a path gets clang's
    error about the path; `#embed` reads up to `PATH_MAX` bytes, and refuses
    a longer name.
  - `__has_include`'s and `__has_embed`'s operand, when its `#if` line writes
    it (with the `(` before it), is read as `#include` reads a name: `<...>` up
    to the first `>` on the line, as clang's `LexHeaderName` lexes it from its
    file. So `__has_include(<  x.h>)` names `  x.h`, as `#include <  x.h>`
    does. The same operand from a macro or a macro's argument is joined from
    its tokens and names ` x.h` (#254's review).
  - The checks and their diagnostics go in clang's order
    (`HandleHeaderIncludeOrImport`):
    - an empty name is an error;
    - a name that ends in a space or a dot gets a nonportable-path warning;
    - a place the search can't look in is fatal: a path too long for one, or a
      file that can't be opened for another reason than there being none, as
      clang's `getFileAndSuggestModule` reports them ("cannot open file
      './...': File name too long"), in `__has_include`'s search too. Nothing
      is reported after it;
    - a file not found is fatal, unless one of clang's two recoveries finds it.
      Each is an error that still includes the file: an angled name found as a
      quoted one, and a name found once the characters other than letters and
      digits at its ends are dropped ("did you mean");
    - an `#include` in the 199th file below the main one is an error, as in
      clang, and the run goes on. Then a file included before isn't entered
      again, which stops a cycle. The file that `#include` named counts as
      included, since clang marks a file included before its depth check: its
      other inclusions are left out, without errors (#254's review).

    Each diagnostic is at the name's token (for a name a macro made, at the
    expansion, with its notes; the depth limit's too), or at the line's end
    for what the line lacks.
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
  physical ones. One without a file name keeps the presumed name. The number and
  the file name are read as translation phase 2 leaves them (a line splice inside
  `1\` and `00` makes `100`), and the name's escapes are decoded as a string
  literal's; clang rejects numeric escapes there, and so do we. Each `#line`
  adds a line note to its inclusion's range (`PPLineDirective`), as does
  `#pragma GCC system_header`, after which the file is a system header.
  Diagnostics, `__LINE__`, `__FILE__` and `-E`'s line markers give the presumed
  file and line. `__LINE__` in a macro's expansion is the line of the end of the
  outermost expansion, as in clang. Where clang and GCC differ, we follow clang:
  - The lines after a `#line` count from the line where clang lexes its
    number. For digits right after a line splice, clang reads the splice as part
    of the token, so that is the splice's line: `#line \` then `100` on the next
    line counts from the `#` line. With blanks between the splice and the digits,
    it is the digits' line.
  - `__BASE_FILE__` is the presumed name at the root of the include stack:
    - a `#line` with a name in the main file renames it;
    - a forced include's root is the `<built-in>` file that holds the
      `-include` lines.

### The compiler's own headers

`src/buster/include` holds buster's own freestanding headers (#117's item 6, as
#156 decided): `<float.h>`, `<iso646.h>`, `<limits.h>`, `<stdalign.h>`,
`<stdarg.h>`, `<stdatomic.h>`, `<stdbool.h>`, `<stdckdint.h>`, `<stddef.h>`,
`<stdint.h>` and `<stdnoreturn.h>`. They are written from C23 and from what
clang 23's resource headers give, and carry the project's license
([LICENSING.md](../LICENSING.md) has the decision).

- **Selecting them.** `--buster-headers` puts them where clang's resource
  directory goes in the search. They are found from the executable: the tree
  whose `build/` holds it. Clang's resource directory stays the default.
- **Clang's other headers.** Not among buster's: the intrinsics' headers
  (`<immintrin.h>` and the rest, the larger part of #156's own-headers work),
  `<cpuid.h>`, `<unwind.h>`, and clang's wrappers of the C library's
  `<inttypes.h>`, `<tgmath.h>` and `<endian.h>`. With `--buster-headers`,
  clang's resource directory follows buster's for them, and supplies no name
  buster's directory has, to the search or to a quoted include beside one of
  clang's headers (`pp_find_include`), so:
  - buster's headers' `#include_next` goes on to the C library's, past clang's
    header of the same name: hosted, `<limits.h>` and `<stdint.h>` include
    glibc's, and `<stdatomic.h>` and `<stdckdint.h>`, which glibc doesn't
    have, keep their own definitions;
  - every other name is found as without the flag.

  Clang's options can't place it so. Right after buster's directory
  (`-isystem`), the four headers' `#include_next` would reach clang's copies.
  After the system directories (`-idirafter`), `<limits.h>` and `<stdint.h>`
  would reach glibc's, but `<stdatomic.h>` and `<stdckdint.h>` would still
  reach clang's, and `<inttypes.h>`, `<tgmath.h>` and `<endian.h>` would be
  glibc's, not clang's wrappers of them. Freestanding, buster's headers include
  nothing next.
- **Leaving them out.** `-nobuiltininc`, clang's flag, leaves out the
  compiler's headers, whichever they are: with `--buster-headers`, buster's and
  clang's after them alike. `-nostdinc` leaves out both with the other standard
  directories.
- **What they give.** Like clang's, each gives what its language mode has, and
  takes the target's types and limits from the predefined macros.
- **Working with glibc.**
  - Hosted, `<limits.h>`, `<stdint.h>`, `<stdckdint.h>` and `<stdatomic.h>`
    use the C library's own first, through `#include_next`.
  - `<stddef.h>` and `<stdarg.h>` answer the `__need_*` requests that glibc's
    headers make.
  - The guards other headers test (`_SIZE_T`, `__int8_t_defined`,
    `_GCC_LIMITS_H_` and the like) keep clang's and GCC's names.
  - A macro that another header may define first is kept, or replaced, without
    a warning. `<stdint.h>` keeps `<wchar.h>`'s `WCHAR_MAX` and `WCHAR_MIN`,
    and `<float.h>` replaces the signaling NaNs, as clang's do. `<limits.h>`
    replaces the C23 widths that glibc's `<limits.h>` defines before it, where
    clang's redefines them with a warning (shown with `-Wsystem-headers`).
- **`__FLT_EVAL_METHOD__`** is now a builtin, as in clang (0 on x86-64): both
  headers' `<float.h>` define `FLT_EVAL_METHOD` with it.

`test-preprocessor --clang` checks them against clang's: 11 headers, 8 language
modes, hosted and freestanding, 176 sets in all. For each set, clang
preprocesses the header with its own set and with ours
(`-nobuiltininc -isystem`), and:

- the two must define the same macros. Guards and helpers reserved to the
  implementation are exempt, as each names them its own way, but the ones other
  headers test are compared too;
- each macro must expand to the same tokens;
- the declarations must be the same, in any order;
- clang's primary diagnostics must be the same.

Then our preprocessor with `--buster-headers` must give clang's tokens on all
the headers and their macros in each mode, and warn no more than clang does in
the unit itself: it reports warnings in system headers too, which clang hides
without `-Wsystem-headers`, and the headers must give none. In strict C89,
clang's own `<stdint.h>` and `<stdckdint.h>` put their `//` comments in the
output as tokens; the check drops those lines from clang's side.

Then the headers among others (#255's review), in each mode, hosted and
freestanding: two units include buster's 11, the C library's headers that
define some of their macros or ask them for parts (`<wchar.h>`, `<math.h>`,
`<unistd.h>`, `<stdio.h>` and others), and clang's headers that buster doesn't
supply (`<immintrin.h>`, `<x86intrin.h>`, `<cpuid.h>`, `<mm_malloc.h>`,
`<unwind.h>`, `<inttypes.h>`, `<tgmath.h>` and `<endian.h>`), buster's last in
one and first in the other, then name every compared macro. Clang
preprocesses each with its own headers, and with buster's in the search
`--buster-headers` makes (`-nobuiltininc -isystem src/buster/include -isystem
DIR`, where DIR links to clang's resource headers but those buster has), both
with `-Wsystem-headers`, and:

- with buster's, clang must warn about nothing in the headers, and give the
  unit itself the diagnostics it gives with its own (`ATOMIC_VAR_INIT` is
  deprecated in C17);
- each macro must expand to the same tokens with either. The declarations
  aren't compared: clang's own freestanding `<stdint.h>` leaves helper macros
  defined (`__int_least16_t` and the like) that rename glibc's typedefs of
  those names in the headers after it, and buster's leaves none;
- our preprocessor with `--buster-headers` must give clang's tokens with
  buster's, and no warnings in the headers.

In strict C89 the units leave clang's other headers out. They are full of `//`
comments, which clang reads as tokens in that mode, and our lexer as comments.

Last, `-nobuiltininc` and `-nostdinc` must leave out buster's headers and
clang's after them alike: which headers `__has_include` finds with
`--buster-headers` and each must be what clang finds with it alone. And a
quoted name beside one of clang's headers must come from buster's when
buster's has it, and from beside it otherwise; no header of clang 23's
includes one of buster's names so, so the check asks the search itself.

With these headers, `sqlite3.c` (also in C23) and Lua 5.4.8 preprocess token
for token as clang does, and compile with clang.

### `#embed`

`#embed` (C23 6.10.3) works as clang 23's does (#117).

- **The header name** is read as an `#include`'s (`pp_read_header_name`, which
  `pp_include` shares): `<...>` or a string literal as written, or a computed
  one from the line's expanded tokens.
- **The parameters** are read expanded, as clang reads them. When a macro's
  name is among them, or the name is computed, the line goes through a capture
  (`BUSTER_PP_CAPTURE_EMBED`, `pp_finish_embed`); otherwise it is read as it is.
  `pp_embed_parameters` follows clang's `LexEmbedParameters`, which
  `__has_embed` shares:
  - A name is an identifier that isn't a keyword, or `scope::name`. `__` around
    the whole spelling doesn't change it: `__limit__` is `limit`, and
    `__clang::offset__` is `clang::offset`.
  - `limit` and `clang::offset` take a `#if` expression, which must be at least
    0 and can't contain `defined`. Clang's ends at a `)` that closes no `(` of
    its own.
  - `prefix`, `suffix` and `if_empty` take tokens whose closing brackets must
    match. They end at a `)` unless a `(` came since the last `)`: clang keeps a
    flag there, not a depth, so `prefix(((a)b)c)` ends after `b`, and
    `prefix({)` is accepted.
  - An unknown parameter is an error, and for `__has_embed` an answer of 0. One
    given twice is an error, and its later value counts.

  Each error has clang's message at clang's column. A fixture checks all 18
  kinds, and that the line is dropped and the run goes on.
- **The file** is found as clang's `LookupEmbedFile` finds it (`pp_find_embed`).
  An absolute name is used as it is. A quoted name is looked for in the
  directory of the file the directive is in, then in the working directory,
  then in the `--embed-dir=` directories; an angled one in those alone. A
  missing file is fatal, and a device file an error.
- **The data** is one output token, `BUSTER_PP_KIND_EMBED`, as clang's is one
  `annot_embed`. A file of N bytes doesn't become 2N - 1 numbers and commas,
  which would take about 28 bytes of output columns per byte. Its value indexes
  `Preprocessor.embeds` (`PPEmbed`: the file's data, an offset and a length),
  and its location is the directive's `#`. It comes after `prefix`'s tokens and
  before `suffix`'s. When no bytes are left after `clang::offset` and then
  `limit`, `if_empty`'s tokens come alone. They go in as a buffer whose tokens
  never expand, as clang enters them with macro expansion off, having read them
  expanded.
- **`__has_embed`** answers as clang 23's does. A `limit` past the bytes left
  gives 2 (empty), not 1, and a device file's size is 0.
- **`-E`** prints the data as clang does: the values, separated by `, `.
  `pp_embed_text_vector` formats 16 bytes per iteration:
  - each value's tens and hundreds by multiplications in 16-bit lanes
    (`v * 205 >> 11` is `v / 10` below 1029);
  - a leading zero as a zero byte;
  - each byte's `, ` and three digits put in an 8-byte slot by one `vpermb`
    per 8 bytes;
  - the slots' nonzero bytes packed by `vpcompressb`, and stored whole.

  `pp_embed_text_scalar`, its twin, formats a byte at a time.
  `test-preprocessor` checks both against `snprintf` on every count to 300,
  random and every value in turn, at the end of a readable page before a
  `PROT_NONE` one. `benchmark-preprocessor --embed N` times them (see below).
  The text is the only part of `#embed` that grows with the file. The rest is
  scalar: reading the parameters, looking up the file and the join's scan of the
  bytes after the line. It costs a line's worth of work once per directive.

  On the 9700X (CPU 2, `benchmark-preprocessor --embed 16777216`, 16 MB of
  random bytes, medians of 21 passes):

  | kernel | cycles/byte | ns/byte | instructions/byte | IPC | text, GB/s |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | `pp_embed_text_vector` | 1.12 | 0.202 | 3.70 | 3.31 | 22.7 |
  | `pp_embed_text_scalar` | 17.6 | 3.167 | 30.7 | 1.75 | 1.44 |

  The vector kernel is 15.7 times as fast. On 4 KB formatted again and again,
  the scalar twin reads 6.8 cycles a byte, since the predictor learns its
  branches on the digit counts, as it can't on a real file's bytes; the vector
  kernel reads 1.13 there too. What `#embed` adds to the main loop costs
  nothing measurable: the pending join and the file-change records. On
  `main.c`, the vectorized kernels take 29.5 and 29.7 ms (428.1 instructions
  per input token), against 30.1 and 29.7 ms (429.3) on #109's branch before
  it.
- **Where `-E` prints it.** No token of an `#embed` starts a line, so the data
  goes on the line of the output before it. After its tokens, the file's next
  token joins their line too (`pp_embed_joins`, `BUSTER_PP_JOIN_LINE`). Clang's
  lexer starts no line there, because the end of the token stream that the
  directive entered passes no line start on. The join holds unless a newline
  outside comments comes before the token, or the token is a directive's `#`.
  A change of file or of presumed position starts a line in clang's printer
  (`FileChanged`): an included file entered or left, or a `#line`. An
  `#embed`'s first tokens after one, with no output since, go at the place the
  change left the printer: the file's first line, the line after the
  `#include`, or `#line`'s number. That is where clang prints them, though the
  `#embed` may be further down (`Preprocessor.file_change_token`).

### Built-in macros and the predefines

`__FILE__`, `__LINE__`, `__COUNTER__`, `__INCLUDE_LEVEL__`, `__BASE_FILE__`,
`__FILE_NAME__`, `__FLT_EVAL_METHOD__` (0 on x86-64, as clang gives it), and
`__DATE__`, `__TIME__` and `__TIMESTAMP__` (from
`SOURCE_DATE_EPOCH` when it is set) are built in. So are the feature queries:
`__has_attribute`, `__has_builtin`, `__has_feature`, `__has_extension`,
`__has_c_attribute`, `__has_declspec_attribute`, `__has_warning`,
`__is_identifier` and the `__is_target_*` queries. Without `SOURCE_DATE_EPOCH`,
`__TIMESTAMP__` is the modification time of the file being read, in local time,
as clang's is.

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
4. For `__has_feature` and `__has_extension`, ask about every `FEATURE` and
   `EXTENSION` name of clang 23's `Features.def` (251 names), in each of the ten
   modes from `c89` to `gnu23`. Only `__has_feature` depends on the mode: C11's
   features hold from C11 on, and `c_fixed_enum` from C23. `PPFeature.standard`
   keeps the first standard of each, while `__has_extension` answers 1 in every
   mode.

The configuration is clang's (the owner's decision in #117): `-std=gnu17` by
default, clang's include search, and clang's predefined macros plus
`__BUSTER__` defined to 1. The predefined macros are clang 23.1.3's for
`x86_64-pc-linux-gnu` in gnu17 (`clang -dM -E -x c /dev/null`), without the
ones the language mode and options decide. `pp_predefines` adds those:
`__STDC_VERSION__`, `__STRICT_ANSI__` or `linux` and `unix`, the inline
semantics, `__STDC_HOSTED__`, `__OPTIMIZE__` or `__NO_INLINE__`, C23's
additions, and `__BUSTER__`. The C23 additions are the difference between that
command's output under `-std=c23` and under the default.

`-march=` selects the target feature and CPU identity macros described in
[Generated-code instruction set](x86-target.md), with x86-64 baseline as the
default. `-mtune=` is independent and changes no ISA macros. `--predefines FILE`
replaces the built-in predefines (`__BUSTER__` too) with a file of `#define` lines, such
as `clang -dM -E` gives for the same options. The oracle uses this, so its runs
start from clang's macros alone.

### Tokens, locations and the output

A token in a buffer or the output has four parts:

- a kind: `C_TOKEN_*`, or a keyword's kind in the final output, or one of the
  output's own: `BUSTER_PP_KIND_PRAGMA_END` after a pragma's tokens, and
  `BUSTER_PP_KIND_EMBED` for an `#embed`'s data;
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
(`pp_needs_separation`). Each source line goes on a line of its own, and
pragmas on lines of their own.

Unless `-P`, line markers give every line its presumed position, as clang's
printer does. Both printers share the code (`pp_print_place`), which starts
with `# 1 "main.c"` and then marks:

- the file and line that `#line` gives;
- flag 1 for each file the include stack goes into, and 2 for each it goes back
  to. The main file roots the stack: clang includes its `<built-in>` buffer,
  and with it the forced includes, from the main file;
- 3 on every marker in a system header, or 3 and 4 in the standard
  directories, which clang wraps in `extern "C"` for C++. A system header is a
  file from `-isystem`, `-idirafter` or the resource directory, a file that a
  system header includes, or the rest of a file after `#pragma GCC
  system_header`;
- names escaped as clang writes them (`\\`, `\"`, and octal for other bytes).

A line that a macro's expansion starts goes on the line of the macro's name, as
clang places a token by its expansion location. A `_Pragma`'s pragma goes on
that line too, and the line after a pragma gets a marker when it is the pragma's
own line. A jump forward of up to 8 lines is blank lines, and any other jump is
a marker.

`PPExpansionStart` records where such an expansion read from a file starts in
the output: one whose name starts a line, or a `_Pragma`. The records are kept
only when `-E` prints markers (`PPOptions.expansion_starts`). On `sqlite3.c`,
most expansions start lines (`assert`, `testcase`), and the records cost 0.4
instructions per input token. The common line start (the same file, no new
line note, and a jump of up to 8 lines) is decided inline. With that, `-E`
costs fewer instructions than before markers gave presumed positions.

### Expansion history and diagnostics

`c_preprocessor_history.c` implements #108. The four output columns stay the
same: `kinds`, `flags`, `values`, and spelling `locations`. Origin handles live
only in temporary argument, capture and expansion buffers. Completed output
keeps sorted, maximal eight-byte `(token index, origin)` runs. Ordinary file
copies write no history; the preceding expansion records its closing boundary.
Buffer copies build the runs outside their token-copy loop, comparing adjacent
origins in groups of eight and compressing packed run records directly into the
table (register compression, then a masked store). There is an exact
scalar builder. `pp_history_at` finds one token's origin; `pp_history_lookup`
runs 16 binary searches together, with scalar record loads to avoid Zen 5's
expensive gathers, and has a scalar twin.

The append-only tables belong to the translation unit:

- `PPExpansion` (28 bytes) snapshots the macro name and definition, the name
  and closing token of this invocation, and both endpoints' parent origins.
  Redefining a macro cannot change an earlier expansion's definition.
- `PPSubstitution` (12 bytes) retains the expansion, parameter occurrence and
  source origin for each argument run. Reusing an expanded argument retains
  its original expansion and creates distinct substitution occurrences.
- `PPGenerated` (24 bytes) distinguishes pastes and stringifications.
  A paste retains both operand locations and origins; a stringification keeps
  its raw argument's token locations and origins in `PPSource` records (eight
  bytes each). These survive reuse of the temporary argument buffers. Builtins
  retain their identity through the expansion record, even when `0` and `1`
  reuse the same scratch spelling; they need no duplicate generated record.

Tagged handles distinguish these records from plain file tokens. Every table
has a checked limit; exhausting it is a fatal diagnostic. The final stream has
no per-token origin allocation. Temporary origin columns are released with
their token buffers; all retained tables are released with `pp_release`.
The eight tables/columns start with four-kilobyte slices in one 32 KiB arena
of base pages. A slice that fills moves once to its own large reservation;
origins are indices, so the move preserves links. The initial arena lives until
release, including the old slices. Small translation units avoid reserving and
advising eight separate mappings. Argument substitution writes its final
origin wrappers directly, without first copying origins it would overwrite.

`pp_diagnose_output` is the later parser's entry point: it takes an output token
index. It uses the same renderer as preprocessor errors. The renderer walks
history iteratively, prints the include stack, the presumed primary location,
and expansion notes from outermost to innermost. Plain argument forwarding
points through to the argument; a macro name expanded after substitution also
shows the parameter it passed through, matching Clang. Generated spellings get
an additional scratch-space note. Per-file physical line tables are built only
when a diagnostic needs them, with scalar/vector builders; `#line` remains
scoped to each inclusion. Source bytes, definition locations and scratch
spellings remain alive for the whole unit. Source excerpts and carets are not
implemented.

`test-preprocessor --clang` compares 33 original diagnostic fixtures with
Clang, including preprocessor failures and calls to the later-token renderer.
It checks object/function expansions, arguments, nested and cross-context
invocations, pastes, stringification, builtins, redefinition, `#line`, CRLF,
include stacks, and #254's review's include errors: header names too long for
a path, and the depth limit's error, at a name a macro made too. Each
fixture's directory is an `-I` directory as well. Only scratch-buffer line
numbers are normalized, since they depend on each compiler's allocation order.
Real and presumed locations, columns, note order and text are compared
exactly. Every existing case and corpus comparison also compares the complete
scalar/vector history tables.
Separate checks cover guarded input tails, maximal runs across split copies,
batched lookups, generated-input lifetimes, growth of all eight initial slices,
and recording-on/off token equality.

`benchmark-preprocessor --history-cost [options] file...` interleaves the same
vectorized preprocessor with recording enabled and disabled, reversing order
each pass. It checks an output digest including spelling locations outside the
timed interval. It reports retained record bytes per unit (mean and maximum)
and temporary-origin high-water bytes separately. These are used bytes, not
RSS or virtual reservations; unused/retired space in the initial 32 KiB arena,
allocator page rounding and existing source/token storage are not included.

Measured on 2026-10-21 UTC on the approved Ryzen 7 9700X, Clang 23.1.1,
`./build.sh` (`-O3 -march=native`), CPU 2, with each run holding the shared
exclusive lock. The measured source is `ffedfd316e4ddc6918272fbf97fffe8d60ff5856`.
The later lookup-loop pragmas only prevent Clang from turning diagnostic
record loads into gathers; those loops are not called by the benchmark.

Each row below is a separate process, with one warmup and 21 measured pairs.
Elapsed time includes initialization, file reads and preprocessing; release
and the four-column digest check are outside the timed interval. Hardware
cycles/instructions exclude the kernel. Both variants use the vectorized
preprocessor; the control is `PPOptions.no_history`, not the scalar compiler.

| Corpus | Run | Recording | No recording | Recording cost |
| --- | ---: | ---: | ---: | ---: |
| SQLite 3.53.2 amalgamation, frozen GNU17 headers | 1 | 13.306 ms | 12.297 ms | +8.21% |
| Same | 2 | 13.459 ms | 12.417 ms | +8.39% |
| Same | 3 | 13.303 ms | 12.317 ms | +8.01% |
| Zig 0.14.1 libc corpus: 1,587 ordinary musl C99 units | 1 | 612.994 ms | 586.330 ms | +4.55% |
| Same | 2 | 604.719 ms | 578.853 ms | +4.47% |
| Same | 3 | 616.932 ms | 590.376 ms | +4.50% |

The under-5% target is met on this musl partition, **not on SQLite**. SQLite's
8.0–8.4% remains a performance limitation. Reporting the cost does not imply
that the target was achieved on both corpora. Output digests agree in every
recording/control pass. SQLite has 1,242,311 input and 706,237 output tokens;
musl has 19,068,706 input and 6,000,225 output tokens across its 1,587 units.

| Corpus | Retained record payload, total | Mean per TU | Maximum per TU | Temporary origins, maximum per TU |
| --- | ---: | ---: | ---: | ---: |
| SQLite | 2,933,720 B | 2,933,720 B | 2,933,720 B | 2,004 B |
| Musl | 40,805,452 B | 25,712.3 B | 113,452 B | 1,988 B |

These are logical payload sizes, with the allocation exclusions above. The
initial arena has 32 KiB capacity per active unit, shared by the eight slices;
some live payload counted above occupies it, while unused and retired slices
add allocation overhead. SQLite retains 55,530 expansions, 44,280 argument runs, 3,573
paste/stringification records, and 88,382 output runs. Musl retains 1,078,679
expansions, 311,310 argument runs, 9,807 paste/stringification records, and
809,597 output runs in total. Builtin identity is already in the expansion
record, saving a second record for each builtin result.

The frozen corpus is `~/buster-fast-bench/preprocessor-corpora-astra-01a158dd`.
Its `MANIFESTS.sha256` hashes to
`391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd`;
`sqlite/sqlite3.c` hashes to
`0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d`;
`config/musl-ordinary.txt` hashes to
`bf47f8562fd1df039a81464164df642c0efb7653e7cb91b0be964978929bb4dc`.
The full 5,129-file Clang-accepted C/header subset was also attempted, but
stopped at the existing `__is_target_arch` handling in
`include/any-macos-any/TargetConditionals.h`; it produced no valid timing.
The table therefore covers the explicit musl partition, excluding the four
separately configured crt/loader sources, not every libc file.

Reproduce one process per corpus (repeat three times, releasing the lock each
time). The frozen predefines and include directories are part of the input:

```sh
corpus="$HOME/buster-fast-bench/preprocessor-corpora-astra-01a158dd"
history_binary="$PWD/build/compiler"
EXCLUSIVE_OWNER=history-sqlite ~/buster-fast-bench/exclusive.sh \
    env SOURCE_DATE_EPOCH=1792454400 taskset -c 2 "$history_binary" benchmark-preprocessor --history-cost \
    -nostdinc --predefines "$corpus/provenance/gnu17-pinned-predefines.txt" \
    -isystem "$corpus/headers/usr/lib/clang/23/include" -isystem "$corpus/headers/usr/local/include" \
    -isystem "$corpus/headers/usr/include" "$corpus/sqlite/sqlite3.c"

mapfile -t history_inputs < "$corpus/config/musl-ordinary.txt"
cd "$corpus/libc/musl"
EXCLUSIVE_OWNER=history-musl ~/buster-fast-bench/exclusive.sh \
    env SOURCE_DATE_EPOCH=1792454400 taskset -c 2 "$history_binary" benchmark-preprocessor --history-cost \
    -std=c99 -ffreestanding -nostdinc -D_XOPEN_SOURCE=700 -Os \
    --predefines "$corpus/provenance/musl-c99-predefines.txt" \
    -I "$corpus/libc/musl/arch/x86_64" -I "$corpus/libc/musl/arch/generic" \
    -I "$corpus/musl-generated/obj/src/internal" -I "$corpus/libc/musl/src/include" \
    -I "$corpus/libc/musl/src/internal" -I "$corpus/musl-generated/obj/include" \
    -I "$corpus/libc/musl/include" "${history_inputs[@]}"
```

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

The real-source command is `test-preprocessor --compare-clang [--clang PATH]`.
[Frozen preprocessor oracle](preprocessor-oracle.md) records its token-kind
and location checks, explicit skip/error accounting, pinned input manifests,
full-corpus reproduction and frozen CI subset.

`./build/compiler test-preprocessor [--clang] [--keep] [options] [file...]`:

- **Cases.** Without files, it runs the 222 built-in cases. These are cases of
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
    - `#line` continued with its number indented;
  - the cases from #193's third review:
    - GNU's comma elision when a variadic macro's only argument is omitted, and
      C17's, which keeps the comma;
    - the space before a stringized `__VA_OPT__`;
    - an unused definition whose `#` names no parameter;
    - the signedness of `u'a'`, `U'a'`, `L'a'` and (C23) `u8'a'` in `#if`;
    - the standard features in C99, GNU17 and C23;
    - `#line`'s number and file name across line splices and with escapes;
    - line markers: `#line` in the main file and in a header, lines that macros
      start, the line after `_Pragma`, the include stack, system headers and
      escaped names, each with the exact text `-E` prints;
  - lexing by the language mode: the literal prefixes before C11, `u8'a'`
    before C23 and both in C23, a paste that would form `u8'a'` before C23, and
    trigraphs in the ISO modes and in the GNU ones;
  - `#pragma` and `#include` among a macro's arguments, which fail as clang's
    do and must then give clang's output;
  - the cases from #202's review: a line splice before the quote of a split
    literal (with `u`, `U` and `u8`, and a trigraph splice), and a trigraph a
    stringized argument spells, which stays, beside one a paste forms, which is
    replaced;
  - a case for each difference from clang that #109's fuzzing found
    (minimized) and for each that probing around one found:
    - the order arguments expand in;
    - `_Pragma` in arguments and in directives' lines;
    - GCC poison;
    - `## ##` and GNU's comma, before `__VA_OPT__` past empty arguments too;
    - substitution spacing around `__VA_OPT__`, its content's pastes left to
      right, and `__VA_OPT__` in a macro without parameters;
    - `_Pragma`'s operand, read expanded a token at a time;
    - line splices (a trigraph's too) before a `)` and a `#line`'s number;
    - a line marker's tokens read expanded, and a `_Pragma`'s text that ends
      in a backslash;
    - `#line` and `#ident` among a macro's arguments, read unexpanded, and
      the parts of `#ident`, `#line`, a computed `#include`, `push_macro`
      and `pop_macro` that clang reads unexpanded;
    - a pragma line among a macro's arguments while a `_Pragma` waits for its
      `(`;
    - character constants in `#if`, and a prefix before an empty one;
    - the line after a `_Pragma` that prints nothing, and lines among a
      macro's arguments, which `-E` moves to their expansion's line;
    - a disabled macro's name among the arguments its expansion gives;
    - the attribute queries' operands, one at the file's end among them;
    - `#line` and line markers;
    - extra tokens after directives;
    - every pragma clang handles;
    - `__has_warning`, `__has_embed` and the target queries;
    - `-undef`;
    - UCN spellings;
    - lexing by the mode
    ([the fuzzing](preprocessor-fuzz.md) lists them).

  A case can name more files, and options for both sides (`-I`, `-include`,
  with `%d` for its directory), and the text `-E` prints with line markers. A
  case that fails can still give the output it must have after its error, as
  clang's does. Texts are lexed by the case's language mode. Each
  case is preprocessed by both kernels, with the built-in predefines. Their
  streams and directive tables must be the same, and their tokens' spellings
  must be the expected ones. With `--clang`, each case is also preprocessed by
  `clang -E`. It must give the same spellings, and its line markers must place
  every token as ours do: on the same presumed line, in the same include stack
  (each file's name as the marker spells it, and its system flags).
- **Fuzzing.** Then 1,500 generated cases in each of C17 and C23
  (`c_preprocessor_fuzz.c`, `--fuzz N` for more). Both kernels must agree on
  each case. With `--clang`, each case must also have clang's acceptance and
  tokens ([the fuzzing](preprocessor-fuzz.md)).
- **Timestamps.** `__TIMESTAMP__` in a file and in the header it includes,
  written with modification times of their own, must give each file's time in
  UTC, and `SOURCE_DATE_EPOCH`'s time when that is set, by both kernels and with
  `--clang` by clang.
- **Predefines and keywords.** The cases and files under `--clang` give our side
  clang's predefines, so `--clang` checks our built-in ones separately:
  - In ten language modes (`c89` to `gnu23`), with `-ffreestanding` and with
    `-O2`, the six target profiles and independent tuning, 22 option sets in all,
    `pp_predefines` must give `clang -dM -E` line
    for line. Only the three macros that name clang's patch release are left
    out.
  - In each mode, `__is_identifier` of every predefined name (keywords,
    directive names, the preprocessor's names) must give clang's answer, and so
    must that of 568 candidates chosen apart from them, so that a keyword
    missing from our names fails too. The candidates are every spelling of a
    keyword, alias or type trait in clang 23's `TokenKinds.def` and its
    companions, and the ARM keyword attributes.
  - In each mode, `__has_feature` and `__has_extension` of every name in clang
    23's `Features.def` (251) must give clang's answers.
- **Files.** With files, each is preprocessed with the options, by both
  kernels, and compared. With `--clang`, our side reads clang's own predefines
  for the same options (`clang -dM -E`, through `--predefines`), so both start
  from the same macros. clang's `-E` text, without its line markers' lines, is
  lexed with `c_tokenize` and compared with our stream's spellings, token for
  token. Then the places its markers give the tokens are compared with ours.
  The first difference is printed with the tokens around it.

In both, the two printers must give the same bytes, with line markers and
without, and the text must lex back into the stream's spellings.

So "token-identical" means the same sequence of lexical kinds and spellings. Whitespace
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

The C library corpus (Zig 0.14.1's `lib/libc`) contains headers and sources for
many targets without their build configurations. The corpus oracle tries each
C/header input alone, compares every one Clang accepts and records every
rejection's reason. Musl is also checked under its upstream build configuration.

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

- `#embed`'s line is expanded whole before its parameters are read, where clang
  expands it as it reads each one. The difference shows in one corner:
  `__has_include` and `__has_embed` inside `limit(...)` or `clang::offset(...)`
  are errors here, and clang evaluates them there as in `#if` (but not in
  `prefix(...)`). `-dE` (printing the directive rather than its data) isn't
  supported. A FIFO or a socket that `#embed` names is reported as a device
  file, where clang would read it.
- Individual feature switches (`-m*`, other than the supported `-march=` and
  `-mtune=`) are rejected. Other options such as `-funsigned-char` still need
  `--predefines` for their macro effects. The target-builtin query table remains
  a bounded subset; see [the target contract](x86-target.md).
- The feature tests (`__has_attribute` and the others) always answer from
  clang's table, which #117 and #125 keep for the oracle's mode. Outside it, they
  are to answer what the compiler implements (#123, #148).
- The printer's spacing differs from clang's in places. It puts a space between
  `<` and `>`, where clang doesn't, and it doesn't indent a line's first token
  to its column. It keeps apart tokens that clang's printer joins into one,
  such as a number and a UTF-8 identifier. Line markers differ in form but not
  in what they say: clang marks every file it enters, even one that gives no
  tokens, and every `#line`, and we mark only where a token needs it.
  #109's comparison prints our stream with clang's spacing
  ([the fuzzing](preprocessor-fuzz.md), "Clang's `-P` printer, emulated").
- Diagnostics include presumed locations, include stacks and macro backtraces
  (#108, above). Source excerpts and carets remain unimplemented.
- C++ (`-x c++`), dependency output (`-M`) and `-dM` aren't supported.
- `-E`'s line markers for the pragma lines that a `_Pragma` without its `(`
  reads past (a directive's among a macro's arguments, another `_Pragma`'s):
  we place them on the `_Pragma`'s line, and clang each on its own. Under
  `-P` the tokens agree.
- `-E`'s line markers when an attribute query's operand runs past an
  included file's end. Clang prints the file's exit before the answer and
  places the answer on the query's line; we place it in the included file.
  Under `-P` the tokens agree.
- A `_Pragma` in a computed `#include`'s line, where clang reads the string
  of a `_Pragma` written in the line itself as a header name and rejects it,
  but runs one from a macro. We reject both.
- Clang's `#pragma clang __debug` commands, a UCN that names a basic
  character (`\u0041`, which clang rejects in an identifier), and warnings
  that don't change acceptance (`-Wvariadic-macros`, `-Wexpansion-to-defined`
  and others). #109's generator writes none of these.
- After an `#include`'s written header name, clang reads one token expanded
  before the rest as it is (`CheckEndOfDirective`): `#include "a.h" f(`,
  with `f` a function-like macro, is an unterminated invocation there, and
  `#include "a.h" E`, with `E` empty, gives no warning. We warn about extra
  tokens and expand none. A computed `#include` reads them as clang does.
- A file that isn't a regular one (`#include "/dev/null"`) can't be included,
  where clang reads it as empty. #109's generator includes only missing
  files.
- `#pragma clang deprecated`, `final` and `restrict_expansion` read their
  `(`, `,`, `)` and message expanded and only the macro's name as it is
  (`HandleMacroAnnotationPragma`); we read them all as written, so a `(` or a
  message from a macro is an error here. #109's generator writes them
  literally.
- In `#if`, a character constant's `\N{name}` escape is refused (there is no
  table of Unicode's names), and `-funsigned-char` doesn't make ordinary
  character constants unsigned, as it does in clang.
- `-E -P` keeps no line numbers, so it doesn't move the token after a
  `_Pragma` that prints nothing to its own line, as clang's printer does when
  that token is on another line; `-E` with line markers does.
- Before C23, the digit-separator pass (`pp_lex_mode`) costs about 1.2% of
  `sqlite3.c`'s instructions: it reads every number's bytes for a `'`. A lexer
  flag that leaves `'` out of numbers would make it free (`c_lexer.c`'s
  `apostrophe_separator`).

## Next steps

- **The main loop** costs about 170 cycles per event: a run, an expansion, an
  invocation, an argument or a paste. On `sqlite3.c`, directives and the main
  loop take 5.6 ms (31 million cycles at 5.5 GHz) for 176,000 such events.
  Candidates, each to be measured on the 9700X:
  - keep the hot fields of `Preprocessor` and `PPTokens` in locals across a
    step: with `-fno-strict-aliasing`, every byte store reloads them;
  - resolve chains of object-like aliases without a context each;
  - reconsider fully rescanned object-macro replay only with evidence of
    repeated expansion work; #110's cheaper eligibility cache was measured
    and rejected (see "Object-macro eligibility cache experiment" above).
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
- **Per-file jobs** cached for the process (#150).

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
