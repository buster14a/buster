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
| This repository's translation units (at fbddd2e, with `main` at e71d7fd), with `-march=native` (AVX-512 intrinsic headers) | 19 | 12,070,090 |
| Lua 5.4.8 (`-DLUA_USE_LINUX -DLUA_COMPAT_5_3`) | 34 | 588,066 |
| The built-in cases (`pp_test_cases`) | 320 | |
| #109's generated cases in C17 and C23, against `clang -E -P`: the last campaign's and those after it, but for 4 that #247 fixes ([the fuzzing](preprocessor-fuzz.md), "Qualification") | 42,800,000 | |

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

**#109's cost.** The fixes for what #109's fuzzing found cost more. In an
A/B on the 9700X against `main` (e71d7fd), with this branch merged into it
(4c6160f), medians of two interleaved rounds (each round's figures within
0.4% of the other's):

| Translation unit | `main` | With #109's fixes | Instructions/input token |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 11.21 ms | 11.90 ms | 123.28 → 133.31 |
| `main.c` (`main`'s) | 25.23 ms | 26.37 ms | 340.41 → 372.80 |

At `4c6160f`, before #264, `sqlite3.c`'s lexing takes 0.36 ms more (the digit
separators, below) and
the main loop 0.38 ms more. On `main.c`, the main loop takes 0.79 ms more.

What they pay for, each a difference from clang that the fuzzing found:
- the spaces in a substitution, which follow clang's `NextTokGetsSpace` item
  by item, and the checks `## ##` and GNU's comma make at every paste;
- arguments expanded in the order of their parameters' first uses: a macro
  without `__VA_OPT__` lists that order when it is compiled, and one with it
  walks its body;
- before C23, the search for digit separators, which read every number's
  bytes (until #264, below), and the prefixes the mode lacks on unknown
  tokens (`u8''`);
- a paste's check of its operands for bytes past 0x7F, 64 at a time: an
  identifier written with UCNs pastes as written.

Their first versions cost 21% more instructions on `main.c` and 7.5% on
`sqlite3.c` than the branch's base did (on the 7940HS). Listing the arguments'
order and checking pastes 64 bytes at a time took back more than half of
`main.c`'s.

**#264's digit separators.** The lexer now ends a number before a `'` itself
before C23 (`C_TOKENIZER_NO_DIGIT_SEPARATORS`, in "Lexing by the language
mode" below), and the search is gone. `pp_prepare_options` sets it in
`PPPrepare.refine_flags`, which the lanes that prepare a cache's files take
too; the lexer's own default keeps C23's numbers. In an A/B on the 9700X of
`main` (f8e4834), #109's branch (7d5779b) and #264's merged with it
(e9f86d6), medians of two interleaved rounds:

| Translation unit | `main` | #109's fixes | With #264 | Lex phase | Instructions/input token |
| --- | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 11.35 ms | 12.00 ms | 11.71 ms | 2.39 → 2.76 → 2.40 ms | 123.22 → 132.59 → 130.79 |
| `main.c` (`main`'s) | 26.15 ms | 27.40 ms | 27.27 ms | 2.30 → 2.57 → 2.35 ms | 320.23 → 346.71 → 344.59 |

The lex phase is `main`'s again, within 0.01 ms on `sqlite3.c` and 0.05 ms on
`main.c`: the chunk loop without separators is the C23 one with another table
(`docs/c-lexer.md`, "Numbers without digit separators"). The scalar twins'
lex phase goes from 3.84 to 3.21 ms on `sqlite3.c` and from 3.70 to 2.89 ms on
`main.c`. What remains above `main`'s (2.43 and 2.36 ms) is their count of the
prefixes the mode lacks, which tests every token for an unknown one (`u8''`):
about 12 instructions per token, where the vectorized count takes one.

**Budget.** The front-end plan (#115) proposed 2 ms to preprocess `sqlite3.c`,
about 16 cycles per output token, next to 2 ms for lexing every input and
1.5 ms for refinement. Measured: 12.00 ms in all, of which lexing is 2.4 ms,
refinement 3.6 ms, and directive tables and the main loop 5.7 ms, 2.8 times
the 2 ms proposed. Each phase's time includes the kernel clearing the fresh
pages it writes, 13% of the samples (see "Where the time goes"). "Next steps"
lists what would close the gap. Since then, #116's budget work has brought
refinement to 1.67 ms and the whole run to 10.0 ms (measured on 2026-10-22,
`docs/c-refinement.md`).

**Small files read.** Since #261, a unit reads its files of up to 16 KB rather
than mapping them ("Files"). Measured on 2026-10-22, that takes 1.9% to 2.3% off
a run and its release on `sqlite3.c`, `main.c` and musl's units, and 1.2% and
1.4% off whole `-E` processes on `sqlite3.c` (17.96 to 17.74 ms) and `main.c`
(37.60 to 37.08 ms).

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

Every file is read once per process: a file of up to 16 KB into an arena of the
file cache's, by a unit and by the lanes alike, and a larger one mapped (with
`MAP_POPULATE` when a unit or one lane maps it; "Small files read" below). It is
lexed, refined and given a directive table (`pp_prepare_into`), and it stays as
long as the file cache that holds it, because macro definitions point into the
tokens of the file that defines them. The cache (`FileCache`) keeps a record per
file identity (device and inode), so that two paths to one file are one file,
and a path entry per path looked up, with the file there or none, so that a
search makes no system call for a path the cache knows. A translation unit finds
files by path in a table of its own (`pp_lookup_path`, which starts at 1,024
slots and doubles) and asks the cache the first time; it copies a record's
prepared fields when it first enters the file, and keeps its own state about it
(`#pragma once`, the line cursor). Without a cache in `PPOptions.files`, a unit
makes its own; with one, the units and lanes that share it read each file once
(`docs/c-files.md`, #150).

- **Lexing by the language mode.** The lexer takes some of clang's rules as
  flags, and the others are applied around it:
  - clang's `c89` (`c90`, `iso9899:1990`) has neither digraphs nor `//`
    comments under `-E`, and its `iso9899:199409` (C94) is c89 with digraphs;
    `-f[no-]digraphs` sets digraphs in any mode ("The `-f` options"). The lexer
    lexes them so with `C_TOKENIZER_NO_DIGRAPHS` (`<:`, `:>`, `<%`, `%>` and `%:`
    are two punctuators each, so `%:define` is no directive and `<%=` is `<` and
    `%=`) and `C_TOKENIZER_NO_LINE_COMMENTS` (`//` is two slashes, so
    `a //* c */ b` is `a`, `/` and `b`), which `c_language_lexer_flags` gives
    for the mode (#288). They are among the prepare flags, so files, `_Pragma`'s
    text, pastes (`<` `##` `:` and `/` `##` `/` form no token in c89) and the
    file cache's key follow them. The vectorized lexer has a chunk loop for
    each mode: without digraphs it is the default loop with another table of
    first-character channels (the same instructions), and without `//` comments
    its line-comment marks are empty (`docs/c-lexer.md`). clang compiles c89
    otherwise: there `//` starts a comment, with a warning, but for a `//*`
    before a file's first line comment, which is `/` and a block comment.
  - Digit separators are C23's. Before it, a `'` ends a pp-number and starts a
    character constant: clang lexes `1'000` as `1` and the unterminated
    `'000`. The lexer does that itself with `C_TOKENIZER_NO_DIGIT_SEPARATORS`,
    which `pp_prepare_options` sets in those modes, in both of its paths (the
    chunk loop's copy for it classifies with a table in which no character
    follows a separator, and is otherwise the same code), so files, scratch
    text and pastes are all lexed so, at no cost. Until #264 a pass after the
    lexer read every number's bytes for a `'` and lexed the rest of the file
    again from each one (#109's cost, above).
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
  conditionals (`pp_pair_conditionals_vector`, or the stack): each conditional
  points to the next directive of its group (`#elif`, `#else` or `#endif`), so
  skipping a group that isn't taken is a jump. An unterminated group points
  nowhere, and the error comes when the file ends.
- **The structure's notes** (#118). The table also keeps what a skip must
  still report, since clang reports it while it skips: an `#elif`, `#elifdef`,
  `#elifndef` or `#else` after its group's `#else`, a group without its
  `#endif` (when the skip goes to the end of the file, innermost first), and
  `#elifdef` and `#elifndef`, a C23 extension before C23 (`pp_note_directives`,
  `pp_report_skipped`). The vectorized pairing tells whether a file has any
  (a group whose `#else` isn't last or whose answer is missing), and only then
  are they built: none of the C library corpus's 8,983 files has any, and a
  skip over a file without notes checks one count. The scalar kernels build
  them for every file.
- **Include guards and `#pragma once`** (#120). A file's guard is the one
  clang's multiple-include optimization finds (`pp_find_guard`). The file is
  null directives (`#` alone), then `#ifndef X`, with extra tokens or not, or a
  `#if` whose line is `defined X` or `defined(X)` under an odd number of `!`
  and any parentheses (`#if ((!(defined(X))))`). The group has no other
  branch, and only null directives follow its `#endif`'s line. Comments don't
  count; any other directive or token does. Like clang, the guard skips the
  file only once an inclusion that took the guard's group has ended
  (`PPContext.guard_taken`, `PPFileData.guarded`). So a file included from
  inside itself is entered again (`xmmintrin.h` through `emmintrin.h`), and so
  is every inclusion while `X` was defined before the first. After
  `#pragma once` or `#import` no inclusion reads the file. The skips are
  counted apart (`PPStatistics.skipped_guards` and `skipped_once`):
  `sqlite3.c` skips 98 inclusions by guards and `main.c` 497, clang's counts
  exactly. The skip saves little, since entering a guarded file again is
  already a jump from its `#ifndef` to its `#endif`.
  `benchmark-preprocessor --guard-cost` measures it against runs that enter
  the files (`PPOptions.no_guard_skip`). On the 9700X (medians of three runs
  of 21 passes, CPU 2) it saves 0.2%: `sqlite3.c` takes 13.33 ms against
  13.36 ms, with 98 skips, and `main.c` 26.45 ms against 26.50 ms, with 512.
- **Lines.** `pp_line_of` counts newlines (64 bytes at a time, with
  popcounts) from a per-file cursor, because the preprocessor asks about later
  places as it goes (`__LINE__`, diagnostics, line markers). Each file's lines
  are counted about once.
- **Memory.** Each column of the files' tokens has an arena of its own: kinds,
  starts and values. A file's columns are pushed for the lexer's bound, one
  token per byte, then trimmed to the tokens it has. Before this, the columns
  of every file shared one arena, and the unused room between them was cleared
  too wherever a huge page held a used byte: 9 bytes per input byte.
- **Small files read** (#261). A unit reads each file of up to 16 KB that it
  opens into its lane's bytes arena in the cache
  (`BUSTER_FILE_CACHE_READ_LIMIT`), as `c_files.c`'s lanes read theirs, and maps
  the larger ones. A mapping costs an `mmap` and, when the unit is released, an
  `munmap`; a read costs a copy into the arena, whose pages the kernel clears
  when they are first touched. `benchmark-preprocessor --read-limit` measured
  the limits against each other on the 9700X on 2026-10-22, each limit in turn
  in every pass of one process, pinned to CPU 2. Each figure is in ms, the
  median of three processes' medians of 21 passes: the run, then the release
  after it (`pp_release` and `intern_release`), which the run's time leaves out:

  | Files read | `sqlite3.c` | Release | `main.c` | Release | musl's 1,357 units in all | Release |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: |
  | None (every file mapped) | 11.40 | 0.66 | 25.54 | 1.02 | 450.3 | 176.8 |
  | Up to 4 KB | 11.38 | 0.50 | 25.48 | 0.75 | 444.9 | 166.3 |
  | Up to 16 KB (the default) | 11.35 | 0.45 | 25.45 | 0.60 | 449.3 | 163.6 |
  | Up to 64 KB | 11.37 | 0.42 | 25.60 | 0.49 | 451.8 | 162.5 |
  | Up to 256 KB | 11.54 | 0.44 | 26.02 | 0.38 | 452.8 | 164.0 |
  | Every file | 12.04 | 0.28 | 26.20 | 0.35 | not run | not run |

  With 16 KB, the run and its release take 2.2% less on `sqlite3.c` (12.07 to
  11.81 ms), 1.9% on `main.c` (26.55 to 26.06) and 2.3% on musl's units (627.1
  to 612.6). Most of it is the release, which no longer unmaps the small files:
  `sqlite3.c`'s unit reads 159 of the 175 files it opens, and `main.c`'s unit
  293 of the 397 it opens. The run gains 0.2% to 0.4%, in its probes: 0.69 to
  0.66 ms on `sqlite3.c`, 1.55 to 1.47 on `main.c` and 65.2 to 61.9 on musl.
  Reading larger files loses: `sqlite3.c` itself is 9.5 MB, which a read copies
  into fresh huge pages where a mapping takes the page cache's own (its probes
  take 1.40 ms). On musl, 4 KB runs 1.0% faster than 16 KB and releases 1.7%
  slower, 0.2% less in all: at 16 KB, 228 of the 1,357 units read more than the
  arena's first 64 KB, which are base pages, and touch a huge page that the
  kernel clears whole (15 µs; at 4 KB, a unit reads 12 KB on average, and none
  does). On `sqlite3.c` and `main.c`, 16 KB is 0.8% and 0.7% better than 4 KB.

  As whole `-E` processes (`perf stat -r 21` of `-E -P` into `/dev/null`, pinned
  to CPU 2, three rounds by turns with `main`'s compiler at 7b1b0ce; the median
  round's mean), `sqlite3.c` took 17.74 ms against 17.96 (1.2% less), and
  `main.c` 37.08 ms against 37.60 (1.4%). musl's 1,357 units, a process each,
  one after another, took 1.914 s either way: the 11 µs a unit saves is within
  that loop's noise (0.7%), since a unit's run and release are 0.45 ms of each
  process's 1.41 ms.

### Macros

The macro table is indexed by `IdentId` (`macro_of`). The vectorized main loop
tests a bitset of the names that have macro records (`is_macro`); the scalar
reference tests `macro_of` directly. Both include the sentinel records for
poisoned names and `defined`, which must stop a run even though `#ifdef` does
not consider them definitions. A definition records
where its replacement list is in the defining file's tokens, and nothing is
copied (#121). A redefinition that differs (C 6.10.4p2: the same form,
parameters, replacement list, and whitespace in the same places) is diagnosed,
and replaces the old definition either way.

A function-like macro is compiled at its first expansion (`pp_compile_macro`)
into body items: tokens, parameters (by index), `#` with its parameter, `##`,
and `__VA_OPT__` with its content. Each parameter gets a bit saying whether it
is ever substituted expanded, so arguments only `#` or `##` use are never
expanded.

The producer `pp_compile_macro` also records literal runs for the consumer
`pp_substitute`. A `PP_ITEM_TOKEN` uses its otherwise unused 16-bit
`parameter` field for the number of consecutive literal items after it,
capped at 65,535; `PPItem` remains eight bytes. A backwards pass computes
every suffix once, including suffixes that substitution can enter after a
paste. Items must be adjacent both in the recipe and in the defining file's
token indices. Removing a `__VA_OPT__` closing parenthesis therefore ends a
run, and substitution additionally checks its active optional-content bound.
The metadata is immutable after successful compilation and lives with the
macro's recipe; a failed compilation discards its items as before. Longer
runs become copies of at most 65,536 tokens, without a new allocation or
changing the scalar substitution loop. Measurements include this one-time
producer cost as well as repeated substitution.

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
  go. At depth zero, if no opener precedes the first closer in a block, its
  comma and closer masks already identify the separators and end; no depth
  prefix scan is needed. A block with no parentheses at positive depth also
  needs no prefix scan. Blocks containing nested transitions use the existing
  scan, and the same masked input bounds, separator limits and fallback rules
  apply to all paths. Otherwise the main loop collects them a token at a time
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
  with clang's values (unsigned with `-funsigned-char`: "The `-f` options").
- **The conditionals' structure** is reported as clang reports it: `#else` and
  `#elif` after `#else`, a directive without `#if` and a group left open, also
  where a skip passes over them (the notes above), and in an included file as
  unterminated there, the includer's `#endif` then without `#if`. Each is at
  the directive's name, where clang's lexer starts it (a line splice right
  before it included: `pp_token_location`), `#elifdef` and `#elifndef` are
  named as such, before C23 with the C23 extension warning, and the extra
  tokens of `#else`, `#endif` and `#elifdef` come in clang's order: before the
  structure in a group it reads, after it where it enters an `#else` from a
  skip, and not at all where it skips past a taken group (`PPConditional`'s
  `skipping`).
- **Definitions** (#121) are checked as clang checks them, in its order, and
  each error is reported in its words and at its place (`pp_define`,
  `pp_define_parameters`, `pp_check_replacement_list`):
  - the parameter list as clang reads it: expected identifier, expected
    comma, invalid token, missing `)`, a duplicate parameter by name, and
    `__VA_ARGS__` allowed as a name;
  - `##` at the start of the list, then `#` without a parameter and
    `__VA_OPT__`'s errors (missing `(`, nested, `##` at either end of its
    argument, left open), then `##` at the end. An unterminated `__VA_OPT__`
    names the list's last token as clang does (`'+'`, `int`, `identifier`,
    `<numeric_constant>`), with a note at its `(`;
  - warnings for:
    - an object-like macro's name without whitespace after it (C99's, or
      C90's two);
    - `__VA_OPT__` outside a variadic macro;
    - redefining or undefining a builtin, `__STDC_*` from the predefines
      among them (`pp_language_defined`);
    - `#undef`'s extra tokens;
    - a redefinition, with a note at the previous definition;
  - a missing name, at the line's end (`pp_line_end_location`).
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
  - `<...>` is read from the source up to the first `>` on the line that no
    backslash escapes, so `//` and `/*...*/` there are part of the name, as in
    clang: `<a//b.h>` and `<a/*x*/b.h>` (#119). A backslash takes the
    character after it into the name, a `>` too, as clang's
    `LexAngledStringLiteral` reads it: `<a\>b.h>` names `a\>b.h`. The rest of
    the line is what clang lexes on from the `>`. The file was lexed before the
    name was known, so where one of its tokens or comments runs past the `>`
    (a `//` in the name began a comment to the line's end, or the `>` starts a
    `>>` or a `>=`), the rest of the line is lexed again from there
    (`pp_header_name_tail`), on past a block comment's newlines as clang's
    line goes, and the file goes on after that line (`pp_resume_after`):
    `#include <a//b.h> x` warns about `x`, and in
    `#if __has_include(<a//b.h>) && X` the `&& X` counts. Without a `>` on the
    line, the line's tokens up to a
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
    its tokens and names ` x.h` (#254's review). When the rest of the line is
    lexed again from the `>`, its source is read in pieces before the line's
    own tokens, which are left (`pp_continue_relex`). Each piece ends after a
    `(`, so a later query can read its own header raw too: several `//` names
    and a later `<  x.h>` keep their meanings. Geometrically growing windows
    retain complete token prefixes, withholding the last three tokens at an
    unfinished window's edge; each byte is lexed a bounded number of times.
    `__has_include(<x.h>>)` keeps a `>` in the operand, an error, as clang's
    does. If the corrected source hides directives in a comment, conditional
    navigation and skipped structural diagnostics count from the first
    unconsumed directive, using the corrected nesting instead of stale links
    or notes (`pp_skip_relexed_group`).
  - A block-comment opener inside a literal name can make the ordinary lexer
    hide the following source, as in `<dir/*x.h>`. After an executing direct
    directive or expanded query confirms that name, `pp_recover_header_comment`
    masks its interior in this inclusion's private lexical view and prepares
    the file again. Original source offsets and macro snapshots remain valid;
    other inclusions and the shared cache keep their own data. Only an
    unterminated comment or one crossing the name's logical newline needs this
    full repair; same-line and `//` suffixes keep the piecewise path. Gap
    detection uses the current lexical bytes and advances a token cursor once
    per view, so repeated names cannot rescan preceding tokens or masks.
    Evaluated `#if`/`#elif` lines read their own trailing-comment diagnostic
    after confirmed names are restored, immediately before evaluation.
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

The files' comparison (`test-preprocessor --clang --buster-headers file...`)
must work in that mode too (#255's review): a unit that includes clang's
`<inttypes.h>`, whose `#include_next` reaches glibc's, which includes buster's
`<stdint.h>`, then more of buster's headers and clang's `<cpuid.h>`, with an
`-isystem` directory's `<limits.h>` given after the flag, whose token's include
stack shows that it comes before buster's; and `__has_include` of buster's,
clang's and glibc's headers with the flag alone, with `-nobuiltininc` and with
`-nostdinc`. Each run has an `-iquote` whose value spells `-nostdinc`, which
must stay a directory's name. The unit compares too with clang's links in
directories whose names clang escapes in its line markers (a quote, a
backslash, a byte past ASCII), as `$TMPDIR` can give them, and a unit of
`<stdint.h>`, `<stdatomic.h>` and `<inttypes.h>` with clang's resource
directory and buster's given again, 13 ways: as `-I`, `-isystem` and
`-idirafter`, through a symbolic link, with a trailing slash and with `/./`,
freestanding too. `benchmark-preprocessor --clang` times clang with the same
search, in a command that holds every argument (`pp_clang_preprocess`, checked
with 300 `-D` options), and fails rather than report a time when a clang run
fails.

With these headers, `sqlite3.c` (also in C23) and Lua 5.4.8 preprocess token
for token as clang does, and compile with clang.

### `#embed`

`#embed` (C23 6.10.3) works as clang 23's does (#117).

- **The header name** is read as an `#include`'s (`pp_read_header_name`, which
  `pp_include` shares): `<...>` or a string literal as written, or a computed
  one from the line's expanded tokens. The file was lexed before the name was
  known, so after a written `<...>` one of its tokens or comments may run past
  the `>`: in `<a//b.bin> limit(1)`, the `//` began a comment to the line's
  end. Then the rest of the line is lexed again from the `>`, as clang lexes on
  after the name (`pp_header_name_tail`, `pp_copy_relexed`). That is when the
  first token clang lexes past the `>` isn't the file's next one, or, with
  none of the file's tokens past the name on its line, when the two lines end
  at different newlines: a block comment right after the name carries clang's
  on. The line lexed again runs to its first newline outside a block comment,
  which carries the line on past its newlines (`pp_relex_end`, which lexes
  each piece of the line once, from where the comment before it ended), with
  no space where only a line splice was. An unterminated block comment at its
  end is clang's error. Its poisoned names are errors, once: `pp_directive`
  leaves the line past an angled name to `pp_embed`, which checks the line
  lexed again, or else the file's tokens past the name. The file goes on after
  that line, past what its own tokens held there, a directive in the comment
  among them (`pp_resume_after`). The file's conditional pairing counted such
  a directive, so a context that hid one counts the nesting again when it
  skips a group (`pp_skip_group`).
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
  then in the `--embed-dir=` directories; an angled one in those alone. An
  empty `--embed-dir=` is the working directory: the name gets no `/` before
  it. The file is only looked at: `#embed` reads it (through the file cache),
  and `__has_embed` needs its size alone. A missing file is fatal, and a device
  file an error.
- **The data** is one output token, `BUSTER_PP_KIND_EMBED`, as clang's is one
  `annot_embed`. A file of N bytes doesn't become 2N - 1 numbers and commas,
  which would take about 28 bytes of output columns per byte. Its value indexes
  `Preprocessor.embeds` (`PPEmbed`: the file's data, an offset and a length),
  and its location is the directive's `#`. It comes after `prefix`'s tokens and
  before `suffix`'s. When no bytes are left after `clang::offset` and then
  `limit`, `if_empty`'s tokens come alone. They go in as a buffer whose tokens
  never expand, as clang enters them with macro expansion off, having read them
  expanded.
- **`__has_embed`** answers as clang 23's does, from the file's size without
  reading it (clang's `LookupEmbedFile` without opening the file), so that a
  file too big or unreadable for `#embed` is there. A `limit` past the bytes
  left gives 2 (empty), not 1, and a device file's size is 0.
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
  token joins their line too (`pp_embed_joins`, `BUSTER_PP_JOIN_LINE`),
  counted from the line's end as clang lexes it. Clang's
  lexer starts no line there, because the end of the token stream that the
  directive entered passes no line start on. The join holds unless a newline
  outside comments comes before the token, or the token is a directive's `#`.
  The scans between tokens read comment delimiters as the lexer does, through
  line splices (`pp_skip_blanks`): `*\` with a `/` on the next line closes a
  comment. A change of file or of presumed position starts a line in clang's
  printer (`FileChanged`): an included file entered or left, a `#line`, or
  `#pragma GCC system_header`. An `#embed`'s first tokens after one, with no
  output since, go at the place the change left the printer: the file's first
  line, the line after the `#include` or the pragma, or `#line`'s number. That
  is where clang prints them, though the `#embed` may be further down
  (`Preprocessor.file_change_token`). So the pragma's note of a system header
  starts there, at the line after it, before blanks and lines without tokens.

### Built-in macros and the predefines

`__FILE__`, `__LINE__`, `__COUNTER__`, `__INCLUDE_LEVEL__`, `__BASE_FILE__`,
`__FILE_NAME__`, `__FLT_EVAL_METHOD__` (0 on x86-64, as clang gives it), and
`__DATE__`, `__TIME__` and `__TIMESTAMP__` (from
`SOURCE_DATE_EPOCH` when it is set) are built in. So are the feature queries:
`__has_attribute`, `__has_builtin`, `__has_constexpr_builtin`, `__has_feature`,
`__has_extension`, `__has_c_attribute`, `__has_declspec_attribute`,
`__has_warning`, `__is_identifier` and the `__is_target_*` queries. Without `SOURCE_DATE_EPOCH`,
`__TIMESTAMP__` is the modification time of the file being read, in local time,
as clang's is.

`__is_identifier(x)` is 1 when `x` is an identifier that isn't a keyword in the
language mode (`c_name_is_keyword`, with GNU's keywords as the `-f` options
below set them), as clang answers. Its operand isn't expanded, so a macro's name
is an identifier.

The feature queries about the compiler have two sets of answers (the owner's
decisions in #154 and on #117's configuration):

- **By default, what buster implements** (`pp_implemented_*`). Until the
  compiler implements everything clang supports, they say exactly what it does:
  of builtins only the preprocessor's own `__is_target_*` (which clang's
  `__has_builtin` answers 1 for too), and no attribute, feature, extension or
  constexpr builtin. The parser, the checker and the code generators add theirs
  to these tables as they implement them (#127, #133, #147, #148).
- **In the oracle's mode, clang's** (`--clang-features`, #125). That is
  `__has_builtin`, `__has_constexpr_builtin`, `__has_attribute`,
  `__has_c_attribute`, `__has_feature` and `__has_extension`.
  `test-preprocessor` turns it on for every case and file it compares with
  clang, and `benchmark-preprocessor` for its runs, so that they do the work
  `clang -E` does on the same headers.

`__has_warning` and `__has_declspec_attribute` answer as clang does in both
modes, and so do the queries that aren't about the compiler (`__has_include`,
`__has_embed`, `__is_identifier`, `__is_target_*`).

In the oracle's mode, clang's sanitizer features (`address_sanitizer`,
`undefined_behavior_sanitizer`, `coverage_sanitizer` and the rest of
`Features.def`'s) follow the sanitizers the options leave on, which the
predefines read too ("The sanitizers" below). `__is_target_vendor` answers for the
target triple's vendor: `pc` (clang's default triple is `x86_64-pc-linux-gnu`)
without `-target`, the triple's own, or `unknown` for one that names none. The
four `__is_target_*` compare a name as clang does, ASCII case aside, and read an
operating system or an environment as clang's triple parser does (`linux2` is
Linux, and `gnu2` is GNU but `gnux32` isn't).

clang's answers come from tables of what clang 23 answers nonzero, for x86-64
Linux in C. They were generated by asking clang about every name the corpora
query:

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
5. For `__has_constexpr_builtin`, ask about every name of `__has_builtin`'s table
   (no corpus queries it). 38 answer 1, in gnu17 and c23 alike.

The configuration is clang's (the owner's decision in #117): `-std=gnu17` by
default, clang's include search, and clang's predefined macros for the same
options plus `__BUSTER__` defined to 1. `pp_predefines` writes them in three
parts:

- the fixed part, `pp_predefines_base`: clang 23.1.3's for
  `x86_64-pc-linux-gnu` in gnu17 (`clang -dM -E -x c /dev/null`), without the
  ones the language mode and the options decide, and clang's two
  `#pragma push_macro` of `__GCC_DESTRUCTIVE_SIZE` and
  `__GCC_CONSTRUCTIVE_SIZE`, so that `#pragma pop_macro` gets them back;
- the language mode's: `__STDC_VERSION__` (`199409L` for
  `-std=iso9899:199409`), `__STRICT_ANSI__` or `linux` and `unix`,
  `__STDC_HOSTED__` and C23's additions (the difference between that command's
  output under `-std=c23` and under the default);
- the target's and the other options', from `c_predefines.c`
  (`predefines_text`, "The predefined macros" below), and `__BUSTER__`.

`--predefines FILE` still replaces all of them (`__BUSTER__` too) with a file of
`#define` lines, such as `clang -dM -E` gives for the same options. The oracle's
file runs use it, so that they start from clang's macros alone and compare only
preprocessing: the 9700X's clang is 23.1.1, whose `__clang_patchlevel__` and
`__clang_version__` differ from the generator's.

`__FLT_EVAL_METHOD__` is a builtin, as in clang: 0, or 1 and 2 with
`-ffp-eval-method=double` and `extended`, and 2 when SSE is off (x87 math).
Under `-E`, clang's `#pragma clang fp eval_method` doesn't change it, and it
doesn't here.

`-E -dM` prints every macro defined at the end instead of the tokens, as
`clang -dM -E` does (`pp_print_macros`): sorted by name, the parameters as
written without spaces (`__VA_ARGS__` as `...`, GNU's `x...`), one space before
the replacement list and one before each token that had whitespace before it,
each token spelled as clang spells it (an identifier's interned name, or its
spelling as written when it has a UCN). On an empty file it prints clang's
output byte for byte, but `__BUSTER__`.

### The predefined macros

`c_predefines.c` gives the macros that the target and the command-line options
decide, as clang 23 gives them for x86-64 Linux: `-march`, the `-m` feature
flags, `-march=native`, and `-O`, `-f...`, `-g`, `-pthread`, `-mcmodel` and
`-mlong-double`.

**The target.** clang's model, read from its driver and LLVM's
`X86TargetParser`:

- A CPU has a set of features. `-march` picks it (x86-64 by default), and
  x86-64 always has `sse2`.
- The `-m` flags turn features on and off, in command-line order. The driver
  keeps only each feature's last flag (a later `-mavx2` drops an earlier
  `-mno-avx2`, so `-mavx2 -mno-avx2` leaves `avx` as the CPU has it).
- Turning a feature on turns on everything it implies (its `implies` closure).
  Turning it off turns off everything that implies it (`implied_by`).
- Then four soft rules: `popcnt` and `crc32` come with `sse4.2`, `mmx` with
  `sse` and `xsave` with `avx`, unless the list turned them off.
- Special flags:
  - `-msse4` is `-msse4.2`, and `-mno-sse4` is `-mno-sse4.1`;
  - `-m80387` is `-mx87`;
  - `-mgeneral-regs-only` is `-mno-x87 -mno-mmx -mno-sse` in its place;
  - `-mapxf` turns on APX's features but `cf`, and `-mapx-features=` and
    `-mno-apx-features=` name them one by one;
  - `-mapx-inline-asm-use-gpr32` comes after the others.
- Each feature has its own macros (`avx512f` has `__AVX512F__`, `sse2` has
  `__SSE2__`, `__SSE2_MATH__` and the `__FLT16_*` macros, `cx16` has
  `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16`), but two need several:
  - `__APX_F__` needs the eight features `-mapxf` turns on;
  - `__APX_INLINE_ASM_USE_GPR32__` needs `egpr` and
    `-mapx-inline-asm-use-gpr32`.
- A CPU has macros of its own (`__znver5`, `__znver5__`, `__tune_znver5__`;
  most Intel CPUs are `corei7`), which `-mtune` doesn't change.

**The generated tables.** The CPUs, the features and their closures and macros
are clang's answers, and `./build/compiler test-preprocessor --generate-targets`
prints them as `c_predefines.c`'s generated block (`predefines_features`,
`predefines_cpus`, `predefines_tunes`). It asks clang 23 (on `PATH`, with
`CPATH` unset) these questions:

- The CPUs: from its errors for an unknown `-march` (the 79 of x86-64) and an
  unknown `-mtune` (105).
- The features: the IR's `"target-features"` of a function, which lists the
  whole feature map. That is every CPU's features, the names `-march=native`
  passes, `x87` and `inline-asm-use-gpr32` (117 in all).
- Each feature's closures: its `implies` from a configuration that turns
  nothing else on, then `-mFEATURE` (or `-mapx-features=`); its `implied_by`
  from `-mno-FEATURE`'s list. The two must agree.
- Each CPU's features, without what only a soft rule adds. That is checked by
  turning each rule's trigger off: none of clang 23's CPUs needs it.
- Each feature's own macros: those that `config(implies)` defines and
  `config(implies without it)` doesn't. config(T) is `-march=x86-64` with
  exactly the features T: `-mno-` of x86-64's features and of the soft rules'
  features outside T, then T's flags. The macros that no feature owns, in the
  configuration with every feature, must be the two above.

**`-march=native`** finds the host's CPU and features from CPUID and XCR0, by
LLVM 23's rules (`Host.cpp`'s `getHostCPUName` and `getHostCPUFeatures`, read
for their rules and written again here):

- the CPU from the vendor, family and model, with LLVM's guesses from the
  features for unknown Intel models;
- each feature from its CPUID bit, with AVX's, AVX-512's, AMX's and APX's
  state saved by the OS (XCR0).

The driver passes the host's features before the `-m` flags, so a later flag
for one of them wins. An unknown host is x86-64, as in clang's driver.

**The other options**, each read as clang's driver reads it:

| Options | Macros |
| --- | --- |
| `-O0` to `-O3` (higher is 3), `-O`, `-Og`, `-Os`, `-Oz`, `-Ofast`; `-fno-inline`, `-finline`, `-f[no-]inline-functions`, `-finline-hint-functions` | `__OPTIMIZE__`, `__OPTIMIZE_SIZE__`, `__NO_INLINE__` (at -O0, or when only always-inline functions inline) |
| The floating-point options, through clang's state machine (`RenderFloatingPointOptions`): `-f[no-]fast-math`, `-ffp-model=`, `-f[no-]finite-math-only`, `-f[no-]math-errno`, `-f[no-]honor-infinities`, `-f[no-]honor-nans`, `-f[no-]signed-zeros`, `-f[no-]associative-math`, `-f[no-]reciprocal-math`, `-f[no-]approx-func`, `-f[no-]trapping-math`, `-ffp-exception-behavior=`, `-f[no-]rounding-math`, `-f[no-]unsafe-math-optimizations`, `-Ofast` (when it is the last `-O`) | `__FAST_MATH__`, `__FINITE_MATH_ONLY__`, `__NO_MATH_ERRNO__` |
| `-ffp-eval-method=` | `__FLT_EVAL_METHOD__`'s value |
| `-f[no-]signed-char`, `-f[no-]unsigned-char` | `__CHAR_UNSIGNED__` |
| `-f[no-]short-wchar` | the `wchar_t` macros |
| `-f[no-]pic`, `-f[no-]PIC`, `-f[no-]pie`, `-f[no-]PIE` (level 2 PIE by default) | `__pic__`, `__PIC__`, `__pie__`, `__PIE__` |
| `-f[no-]stack-protector[-strong\|-all]` (this clang's default is `-strong`, which `-fstack-protector` keeps) | `__SSP__`, `__SSP_STRONG__`, `__SSP_ALL__` |
| `-f[no-]asynchronous-unwind-tables`, `-f[no-]unwind-tables`, `-f[no-]dwarf2-cfi-asm`, `-f[no-]exceptions`, the `-g` options (the last of clang's `g_Group`, `-gcodeview` among them; `-gmodules` turns debug information on unless `-gno-modules` follows it), `-ffreestanding` | `__GCC_HAVE_DWARF2_CFI_ASM` |
| `-f[no-]exceptions`; `-fsjlj-exceptions`, `-fseh-exceptions`, `-fdwarf-exceptions`, `-fwasm-exceptions` | `__EXCEPTIONS`; `__USING_SJLJ_EXCEPTIONS__`, `__SEH__` |
| `-fgnuc-version=` (0 turns them off); `-f[no-]gnu89-inline` | `__GNUC__` and the others, GCC's atomics; `__GNUC_GNU_INLINE__` or `__GNUC_STDC_INLINE__` |
| `-pthread` | `_REENTRANT` |
| `-fcf-protection[=]` | `__CET__` |
| `-f[no-]sanitize=` and the options its rules read (below) | `__SANITIZE_*__` (address, hwaddress, thread and alloc-token, a kernel form sharing its user space form's) |
| `-f[no-]blocks`; `-f[no-]openmp[=]`, `-fopenmp-version=`; `-fopenacc` | `__block`, `__BLOCKS__`; `_OPENMP`; `_OPENACC` |
| `-f[no-]define-target-os-macros`, `-f[no-]pascal-strings`, `-f[no-]constant-cfstrings` and `-m[no-]constant-cfstrings` (two families, each by its last: either's `no-` drops the macro), the profile-generating options | `TARGET_OS_*`, `__PASCAL_STRINGS__`, `__CONSTANT_CFSTRINGS__`, `__LLVM_INSTR_PROFILE_GENERATE` |
| `-municode` (joined: `-municodeANYTHING` too) | `UNICODE`, in `<command line>` before the command line's `-D` and `-U`, as clang's driver adds it |
| `-mcmodel=`, `-mlong-double-64`/`80`/`128` | `__code_model_*__`, the `long double` macros |
| `-mfpmath=` | none, but it must agree with SSE |

The options that would make clang predefine or compile something buster doesn't
follow are refused by name: `-m32`, `-mx32`, `-m16`, `-miamcu`, `-mcpu=`, `-mkernel`,
`-fapple-kext`,
`-foffload-via-llvm`, the profile-using and pointer-field-protection options,
and the CUDA, HIP, SYCL and pointer-authentication ones. So are a `--target`
other than x86-64 Linux with glibc (`x86_64` or `amd64`, any vendor or none,
`linux-gnu`: clang's predefines are the same for every vendor), and values clang
refuses (an unknown CPU, a bad `-fcf-protection=`, `-fsanitize=address,thread`,
...). Where clang's driver reads only an option's last value (`-target`,
`-march`, `-mtune`, `-mfpmath=`, `-mcmodel=`, `-mabi=`, `-fveclib=`, `-O`,
`-fgnuc-version=`, `-fcf-protection=`, `-fopenmp=`'s runtime, `-flto=`,
`-fvisibility=` and `-std=`), `predefines_finish` checks that one after every
option, so an earlier invalid one doesn't count. Any other `-f` option changes
no macro and is accepted.

**The `-m` options** are taken only where clang 23.1.3 uses them for x86-64
Linux: those above, and those that change no macro (`predefines_neutral`, found
by trying each option `clang --autocomplete=-m` lists): 41 flags, the Spectre,
LVI and SESES hardening flags, and 19 options with a value. `predefines_finish`
checks them as clang does: the hardening flags against each other, as its
driver weighs them (one of Spectre's and one of LVI's not together, and SESES
with neither but LVI's CFI), `-mfunction-return=thunk-extern` against
`-mcmodel=large`, and each value option's last value as clang's driver and then
cc1 read it (cc1 reads a number in any radix: `0x10`, or `010` in octal),
`-mlarge-data-threshold=`'s only under `-mcmodel=medium` or `large`, and
`-mrecip=`'s estimates as `parseMRecipOption` does. clang's `-m` aliases of
other options (`Options.td`'s `Alias`) are read as those options
(`predefines_alias`, before anything else reads the argument), so the last of
either spelling wins: `-m[no-]pascal-strings` as `-f[no-]pascal-strings`,
`-m[no-]zvector` and `-m[no-]unsafe-fp-atomics` as their `-f` options
(`-mzvector` is refused outside assembly, like `-fzvector`, even before
`-mno-zvector`),
`-mno-fp-ret-in-387` as `-mno-x87`, and `-mips1` to `-mips64r6` as
`-march=mips1` and so on (an unknown CPU, unless a later `-march` wins).
`-msse4`, `-m80387`, `-mapxf` and `-mpure-code` keep branches of their own.
Every other `-m` option is refused, so that no switch silently leaves the
baseline's macros: one clang doesn't know (`-mavx512`, for `-mavx512f`) or
refuses for the target, another target's, which clang takes with a warning that
it ignores it (`-mthumb`, `-moutline-atomics`), and `-mcpu=help` and
`-mtune=help`, which ask clang for its CPUs instead of compiling.

**The sanitizers.** `predefines_finish` reads the sanitizer options after every
option, as clang 23's driver does (`SanitizerArgs`): from the last back, so a
later `-fno-sanitize=` turns off what an earlier `-fsanitize=` turned on and
resolves a pair the whole set wouldn't allow. Each of clang's 63 sanitizers keeps
its own state (`address` and `kernel-address` share `__SANITIZE_ADDRESS__`, not
their state), and a group (`undefined`, `integer`, `cfi`, ...) gives its members.
`-fno-sanitize=all` turns off every sanitizer and every group, as clang's `all`
holds every bit, a group's own too: an earlier `-fsanitize=memtag`, which x86-64
doesn't support, is then no error.
`-fsanitize=object-size` is off without `-O` or with `-O0` (`-O00` keeps it, as
clang's does), `-fwrapv` (or `-fno-strict-overflow`) keeps `undefined` from
checking signed overflow and `-fwrapv-pointer` pointer overflow,
`-mexecute-only` drops `function` and `kcfi`, and `-fsanitize-cfi-cross-dso`
drops `cfi-mfcall`. Then the rules that need the whole set, in clang's order and
words: its incompatible pairs (each dropping the others), the sanitizers x86-64
Linux doesn't support (MTE's and `objc-cast`), `-flto` for CFI, `-fvisibility=`
for CFI's class checks, `address` for the pointer pairs, `-mcmodel=small` for an
explicit `function`, `-frtti` for `vptr`, and `kcfi` beside CFI.
`-fsanitize-coverage=` and `-fno-sanitize-coverage=` (and `fuzzer`) decide
`coverage_sanitizer`, and `-fsanitize-ignore-for-ubsan-feature=` the checks
`undefined_behavior_sanitizer` leaves out. clang's rules that read
`-fsanitize-trap=`, `-fsanitize-minimal-runtime` and `-fsanitize-recover=`
aren't followed: they only refuse `vptr` or a recovery mode, and change no macro
or feature. `test-preprocessor --clang` compares every sanitizer that has a macro
or appears in clang's incompatible pairs alone, in pairs and in pairs with
either one turned off after them (376 sets), each of them and each other group
with `-fno-sanitize=all` after it and before it, and beside `memtag` (81), and
the features under 46 option sets.

This is cold code: it runs once per translation unit, on a few dozen options
and two-word feature sets, so it has no vector path. Generating the text costs
about 13,000 instructions (an empty file's `-E` run is 1.6 million, mostly
preprocessing the 470 `#define` lines), and `-march=native`'s CPUID adds a few
microseconds.

**The generated code's instruction set.** `predefines_target` gives
`x86_target.c`'s `X86Target` from the same resolution
([Generated-code instruction set](x86-target.md)): the features it knows by
name, with RDTSCP and CLFLUSH, which clang has no feature for, from its profile
where `-march` names one of its six (`native`'s from CPUID), and the profile's
architecture (`X86_ARCH_OTHER` for clang's other CPUs). The assembler's forms
and `__has_builtin`'s target builtins follow it, and the test checks the map
against the generated tables (each name a feature, with `x86_target.c`'s macro).

### The `-f` options

Most `-f` options change only macros, which `predefines_option` models as
described above, or nothing `-E` shows. #281 has the audit
of clang 23.1.3's 1,246 `-f` options that found the others: each was run through
`clang -E` on a probe in `gnu17`, `c17`, `c89` and `assembler-with-cpp`, and its
output compared with the run without it. Those come in families of an option
and its `-fno-` form, of which clang's driver reads only the last.
`pp_take_family` records each family's last option, and `pp_settle_families`
decides after every argument:

- **Modeled.**
  - GNU's keywords (`asm`, `typeof`, and `inline` before C99: TokenKinds.def's
    `KEYGNU`), which GNU modes have, follow the last `-fgnu-keywords` or
    `-fno-gnu-keywords`, or else the last of GCC's `-fasm` and `-fno-asm`.
    clang's driver passes those on as `-f[no-]gnu-keywords` before the explicit
    one, so `-fno-gnu-keywords -fasm` turns them off. `CLanguage.gnu_keywords`
    holds the choice (`c_language_gnu_keywords`), which `__is_identifier` and the
    output's keyword kinds read. C23 keeps `typeof` without them.
  - Raw strings follow the last `-fraw-string-literals` or
    `-fno-raw-string-literals`, or the mode: on in GNU modes from C99,
    including assembly. `R` and `LR` work in every mode with raw strings;
    `uR`, `UR` and `u8R` need C11. Files, cache entries, pastes and scratch
    text share these flags. Raw bodies keep line splices and trigraphs when
    printed or stringized; `_Pragma`, line file names and `__has_warning`
    read their bodies. Raw strings cannot supply header names.
  - `$` in identifiers and pp-numbers: `-fno-dollars-in-identifiers` makes it
    punctuation in C, as it is in assembly, and `-fdollars-in-identifiers` gives
    assembly's identifiers it back (`pp_dollar_punctuation`, which the file
    cache and pasting follow). The scalar lexer lexes `$` as punctuation itself
    (`C_TOKENIZER_NO_DOLLARS`), which the scalar kernels and pastes use. The
    vectorized lexer takes it into identifiers and pp-numbers, so
    `pp_split_dollars` lexes again, with the scalar lexer, each run of tokens
    that holds one (tokens with nothing but line splices between them): what
    follows a `$` then joins the tokens after it as in clang (`$L"x"` is `$` and
    a wide string literal, and `x$1.5` is `x`, `$` and `1.5`). A run that ends in
    an unknown token other than `$` (an unterminated literal: C23's digit
    separators can free a quote, as in `$1'2'`) has the whole file lexed so
    instead, since the literal takes the rest of the line. The literal prefixes
    the mode doesn't have are split after that.
  - Digraphs follow the last of `-fdigraphs` and `-fno-digraphs`, or else the
    mode, which has them but in `c89` (#288). So `-std=c89 -fdigraphs` is C94,
    whose `__STDC_VERSION__` is `199409L` (clang's `LangOptions::getCLangStd`
    gives it to any C89 mode that isn't GNU and has digraphs), and
    `-std=iso9899:199409 -fno-digraphs` is c89, without `__STDC_VERSION__`.
    `CLanguage.digraphs` holds the choice (`c_language_digraphs`), and
    `-std=iso9899:199409` sets it. No option changes `//` comments.
  - Character constants in `#if` follow the last of `-funsigned-char` and
    `-fno-signed-char` (unsigned) and `-fsigned-char` and `-fno-unsigned-char`
    (signed, the default), from `predefines_option`'s `char_unsigned`. With unsigned char, `'\xff'` is 255 and an ordinary
    character constant is an unsigned value, so `'a' - 'b' > 0`, as in clang's
    `#if`. A multi-character constant is its bytes' concatenation as an int, as
    clang has it: its first byte isn't sign-extended (`'\377\377'` is 65535),
    and the int is, unless char is unsigned (`'\377abc' < 0`).
  - Wide character constants follow `predefines_option`'s `short_wchar`:
    `-fshort-wchar` makes them 16-bit unsigned values in `#if`, so
    `(L'\0' - 1) > 0` and `L'\xffff' - 65536 > 0`. A later
    `-fno-short-wchar` restores the default 32-bit signed values.
- **Refused**, with an error that names the option, where the family's last
  option turns it on (its `-fno-` form, alone or last, is taken):
  - keyword sets buster doesn't have: `-fms-extensions` and `-fms-compatibility`
    (which also change lexing, pasting, pragmas and includes),
    `-fborland-extensions`, `-fdeclspec`, `-fzos-extensions`, `-ffixed-point`,
    `-fdefer-ts`, and `-fzvector` whatever follows it (clang's driver never
    passes `-fno-zvector` on), but in assembly, where clang ignores it;
  - `-E`'s other modes (`-fdirectives-only`, `-fkeep-system-includes`,
    `-fminimize-whitespace`, `-fuse-line-directives`, `-frewrite-includes` and
    `-frewrite-imports`) and modules (`-fmodules` and
    `-fimplicit-module-maps`); and, wherever they come, `-fbuiltin-module-map`,
    `-fmodule-name=`, `-fmodule-map-file=`, `-fdriver-only`,
    `-fproc-stat-report[=]`, and `__FILE__`'s remappings `-fmacro-prefix-map=`
    and `-ffile-prefix-map=`;
  - a last `-fexec-charset=` or `-finput-charset=` other than UTF-8 in any case,
    which clang refuses too.

The feature queries that some of the other options change (`-fblocks`,
`-fno-builtin`, `-fno-gnu-inline-asm`, `-fxray-instrument`, `-fenable-matrix`
and others) answer from clang's default table ("Not yet").

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

`test-preprocessor --clang` compares 36 original diagnostic fixtures with
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
- macro-name lookup, directly through `macro_of` instead of the cached name
  bitset;
- the bulk copies (arguments, body runs, object-like bodies), and the
  argument scan;
- temporary expansion-origin copies and output-history compression;
- identifier pastes, which the vectorized kernels intern without lexing;
- the keyword pass;
- the printer.

`test-preprocessor` requires the two to produce the same stream, token for
token (kind, flags, value and location), and the same directive tables.
Both printers must give the same bytes, with line markers and without.

Scalar preprocessing copies token columns and temporary origins one element
at a time. Its macro eligibility checks, including those inside shared
expansion and directive helpers, use `macro_of[id] != 0`. The vector run
loops still call their bitset lookup directly. Macro updates keep both tables
current, allowing the definition benchmark to change kernels on one state;
`-dM` also enumerates the bitset. Neither is part of scalar expansion lookup.
The scalar-reference regression deliberately makes every known identifier's
cached bit disagree with its macro record before use, including after an
undefine/redefine phase. It compares explicit expected tokens and diagnostics
with the vector run's full stream, history and directive tables. Separate
history-copy cases cover both overlap directions and empty copies.

## Data-parallel design, piece by piece

Measured on the 9700X as in "Results". An A/B ran each pair of builds by
turns, four rounds of 21 passes each, because two builds of the same code
differ by up to 1% from code placement alone.

| Piece | Vectorized design | Scalar twin and rare cases | 9700X comparison |
| --- | --- | --- | --- |
| Refinement (#116) | Pass A per 64 tokens; pass B batches of 64 identifiers (`docs/c-refinement.md`) | `refine_tokens_scalar`; spliced, UCN and long identifiers | 6.2 times the scalar twin on `sqlite3.c` (4.2 before #116's budget work) |
| Directive scan (#118) | A compare of 64 kinds with `#` and the start-of-line bit | Token at a time | Part of the directive tables' 1.9%; 0.62 cycles per token on the C library corpus, against 3.99 |
| Directive tables (#118) | The scan, the kinds and line ends, the pairing below, and the notes only where the pairing finds a file can have any | The stack, and the notes for every file | 2.31 cycles per token and 51 per directive on the C library corpus (`benchmark-preprocessor --tables`), against 6.50 and 143 |
| Conditional pairing (#118) | Depth as a prefix sum, pairing as nearest smaller values by pointer jumping, 32 conditionals at a time | The stack; files with a continuation too many | 16% faster on a file of 36,868 conditionals; within noise on `sqlite3.c` |
| File and buffer runs (#122) | 64 tokens per step: one compare finds the identifiers, whole-vector stores | Token at a time | Earlier scalar twins, still sharing the macro-name bitset: 22.8 ms against 12.0 ms in all |
| Argument scan (#122) | Paren depth as a byte prefix sum; the `)` and the top-level commas from compares | Token at a time, through the contexts below | Equal on `sqlite3.c`; 0.4% faster on `main.c`, 1.5% fewer instructions |
| Macro-name test (#122) | A scalar bitset load per identifier a run's compare finds (a gather is 64 uops) | `macro_of[id] != 0`, independent of the bitset | 1.5% of the vector run's `sqlite3.c` time; a postings index costs more to build |
| Pastes (#122) | Two identifiers, or an identifier and a number, interned without lexing | Lexing every paste | `main.c` (66,520 pastes) 16.5 to 15.2 ms |
| `#if` (#123) | Scalar: the lines are short (below) | Precedence parser with explicit stacks | Under 0.3% of `sqlite3.c`'s time |
| Keyword pass (#124) | 64 kinds at a time against a table of the language mode's keywords | Token at a time | Not measured on its own |
| Printer (#124) | Spellings, then spaces for 64 tokens from class tables (`vpermi2b`), then masked 64-byte copies into one buffer | `pp_print_scalar`, through stdio | `-E` on `sqlite3.c` 25.5 to 18.9 ms; `-E -P` on `main.c` 29.3 to 22.1 ms |

**The directive tables alone** (`benchmark-preprocessor --tables`, the 9700X,
CPU 2, three runs of 21 passes on the C library corpus: 8,983 files, 13,152,141
tokens, 598,310 directives, one per 22 tokens):

| Kernels | Time | Cycles/token | Cycles/directive | Instructions/token | IPC | The scan, cycles/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Vectorized | 5.47–5.55 ms | 2.31–2.34 | 50.7–51.4 | 4.58 | 1.97 | 0.62 |
| Scalar twins | 15.43–15.50 ms | 6.50–6.53 | 142.9–143.5 | 30.31 | 4.65 | 3.99 |

The scan is a quarter of the vectorized tables; the rest is per directive:
its kind, its line's end (`pp_next_line`), the pairing and the guard, about
38 cycles each, at an IPC near 2. The notes cost nothing measurable: the same
runs' `sqlite3.c` took 13.42 ms against main's 13.40 ms, and `main.c` 26.62
against 26.60 (medians of five interleaved rounds).

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

- **Cases.** Without files, it runs the 320 built-in cases. These are cases of
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
  - the `-f` options buster models (#281): GNU's keywords off in `gnu89`, on in
    `c89` with `-fasm`, and `-f[no-]gnu-keywords` against a later `-f[no-]asm`;
    `$` as punctuation in C, in a paste too and stringized (beside an
    identifier written with a UCN), and in assembly's identifiers; what follows a
    `$` joining the tokens after it, in C, C23 and assembly (#283's review):
    prefixed literals (`u8'x'` from C23 only), one across a line splice,
    pp-numbers, and a quote a digit separator frees; and character constants in
    `#if` with unsigned and signed char, multi-character ones among them;
  - c89's and C94's lexing (#288): digraphs and `//` in both (`%:define`, `<%=`,
    `<::>`, `//*` and a macro whose body takes `//`), stringized and pasted
    (`<` `##` `:`, `/` `##` `/` and `%:` `##` `%:` in c89, which fail, and in
    C94, where `%:%:` pastes), and `-f[no-]digraphs` in `c89`, C94, `gnu89` and
    `gnu17`.
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
  - In ten language modes (`c89` to `gnu23`) and C94 (`iso9899:199409`, and
    `c89` with `-fdigraphs`), with `-ffreestanding` and with `-O2`, without
    digraphs in C94 and `gnu89`, the six target profiles and independent tuning,
    26 option sets in all, `pp_predefines` must give `clang -dM -E` line
    for line. Only the three macros that name clang's patch release are left
    out.
  - `c_predefines.c`'s generated tables must be what the generator learns from
    clang now, field by field (`c_predefines_test.c`).
  - About 1,300 option sets must give `clang -dM -E`'s macros, and both must
    accept or refuse each. They are every `-march` CPU and `native`; every
    feature flag on and off, on x86-64 and on the CPUs with the most features
    (`diamondrapids`, `znver5`, `native`); each option of the table above,
    alone and in the orders that matter (last-wins families, the floating-point
    state machine, the soft rules, a feature's last flag); and 400 random
    combinations of them (`--seed N`).
  - `-E -dM` of a file with every kind of definition must be clang's byte for
    byte (but `__BUSTER__`), and `__FLT_EVAL_METHOD__` clang's under
    `-ffp-eval-method=` and without SSE.
  - In each mode, `__is_identifier` of every predefined name (keywords,
    directive names, the preprocessor's names) must give clang's answer, and so
    must that of 568 candidates chosen apart from them, so that a keyword
    missing from our names fails too. The candidates are every spelling of a
    keyword, alias or type trait in clang 23's `TokenKinds.def` and its
    companions, and the ARM keyword attributes. The same holds in 25 sets of `-f`
    options (`pp_test_keyword_sets`): GNU's keywords turned on and off, in both
    orders of `-f[no-]gnu-keywords` against GCC's `-f[no-]asm` and of each
    against itself, and each refused family turned off again, which changes no
    keyword.
  - In each mode, `__has_feature` and `__has_extension` of every name in clang
    23's `Features.def` (251) must give clang's answers.
- **`-f` options** (#281, `pp_test_option_refusals`). Each of 63 option sets
  must be refused, with a message naming the option, or taken without one: every
  refused family where its last option turns it on, after its `-fno-` form, and
  turned off again; `-fzvector` before `-fno-zvector` and in assembly; digraphs
  on and off in `c17`, on in `c89` and off in C94, and both modes alone (#288);
  the charsets in either case. With `--clang`, clang's `-E`
  of a probe must change with each refused set, from the set's `-std=` alone or
  no options, which is why it is refused; and each taken set must give clang's
  tokens, by both kernels.
- **Directive tables and structure** (#118, `c_preprocessor_directives_test.c`).
  2,000 random sources (`--count`, `--seed`) and nests past the vectorized
  pairing's depth limit are prepared by both kernels, whose tables must be an
  oracle's: a walk of the tokens one at a time, of the test's own, that finds
  each directive and its line's end, classifies it by its name's spelling,
  pairs the conditionals with a stack and finds the notes and the include
  guard. The sources mix every directive name, `%:`, `#` alone, line markers,
  unknown names, `#`s inside lines, in literals and comments and after line
  splices, CRLF lines, unterminated comments, and conditionals nested to 64
  levels, balanced and not. With `--tables PATH...`, every `.c` and `.h` file
  under the paths is checked the same way: this repository's sources and the
  C library corpus (8,983 files, 598,310 directives) agree. Then 26 fixtures
  of the conditionals' structure must give the same diagnostics by both
  kernels, and under `--clang` clang's, byte for byte, columns included.
  Seven diagnostic-text fixtures also compare literal comments, quotes,
  splices and trailing blanks in `#error` and `#warning` messages. Active
  messages preserve the source after their logical newline; excluded ones
  retain the skipped group's raw lexing. Exceptional lexical overlaps get a
  private prepared view for that inclusion, retaining macro snapshots and
  shared cache/once/guard state. At most 64 direct and 8 query repairs per
  inclusion bound full-file copying/preparation and retained storage by
  `O((64 + 8) * file size)`; capacity exhaustion is an explicit fatal error.
  Ordinary diagnostic lines consume no repairs. Boundary checks exercise
  64 successful repairs, the 65th fatal repair, and 65 ordinary messages.
- **Definitions** (#121). Ten fixtures of `#define`'s and `#undef`'s
  constraints and of redefinitions (the groups above, C90's and C99's
  whitespace, and identical and different redefinitions) must give the same
  diagnostics by both kernels, and under `--clang` clang's, byte for byte.
  They avoid redefining a predefined macro other than `__STDC_*`: clang's note
  for it gives a line of its `<built-in>` buffer, whose lines ours doesn't
  keep in clang's order.
- **The location space** (#124). A file runs in a space shrunk to end where
  an inclusion would start (`PPOptions.location_limit`, `--location-limit N`),
  and another where its first pasted spelling would: both kernels must stop
  there, with the output before it, reporting the inclusion at its `#include`
  and the spelling without a place, as clang reports running out. Then the
  commands that split `-E`'s options from their files, run on a small file,
  must take `--location-limit N` with its value as an option of ours, which
  clang doesn't get: `test-preprocessor` with the file and
  `benchmark-preprocessor`, with `--clang` when the test has it, and
  `--compare-clang`. Each must succeed in a space the file fits in, and run
  out of a space of one byte.
- **Include guards** (#120). 20 fixtures:
  - the issue's: a guarded file included twice, `#undef` between inclusions,
    `#if !defined X` and `#if !defined(X)`, `#else` in the group, a token
    before the `#ifndef` or after the `#endif`, comments outside the group;
  - the guard's other shapes: parentheses and three `!`, two `!`, `|| 0`,
    `#elif`, directives before and after the group, null directives around
    it, extra tokens;
  - the macro defined before the first inclusion, an inclusion from inside
    the file, an inclusion in a skipped group;
  - `#pragma once` reached through a symbolic link and a hard link, and
    `#import`.

  Each must make its skips, by guards and by `#pragma once`, by both kernels,
  and under `--clang` give clang's output and clang's own count of skips by
  guards (`-Xclang -print-stats`). Every file under `--clang` must skip as many
  inclusions by guards as clang does too.
- **Files.** With files, each is preprocessed with the options, by both
  kernels, and compared. With `--clang`, our side reads clang's own predefines
  for the same options (`clang -dM -E`, through `--predefines`), so both start
  from the same macros. clang has no `--buster-headers`, so it gets ours'
  whole search instead of the directories' options (`pp_clang_arguments`):
  after `-nobuiltininc`, each directory ours keeps, with its option (`-iquote`,
  `-I`, `-isystem` or `-idirafter`), and in clang's resource directory's place
  a directory of links to its headers but buster's names; clang adds the
  standard directories after them, as ours has them. So clang searches what
  `pp_dedupe_directories` kept, which its links would otherwise hide from it:
  the resource directory given again as `-I` or `-idirafter` is dropped for the
  system one, and as `-isystem` keeps its place, where clang's headers are
  found and the links are left out (#255's review). With `-nostdinc` or
  `-nobuiltininc`, which leave out both, nothing is translated. The files clang
  finds through the links are compared under their names in clang's resource
  directory, where ours finds them, as clang escapes them in its line markers.
  clang's `-E` text, without its line markers' lines, is
  lexed with `c_tokenize` and compared with our stream's spellings, token for
  token. Then the places its markers give the tokens are compared with ours.
  The first difference is printed with the tokens around it. Last, with our own
  predefines, the macros defined at the file's end must be `clang -dM -E`'s
  byte for byte, but for the patch release's three macros and `__BUSTER__`
  (`pp_test_macros`, #121): the whole table, predefines, definitions and
  `#undef`s. `main.c` ends with 10,894 macros, `sqlite3.c` with 5,596.

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

The benchmark reports explicit lookup work separately from its phase totals.
`open` and `fstat` counts cover file-cache probes, including failed opens,
aliases not yet cached and nonregular candidates; cached paths, including
cached misses, make neither call. Include-search counts are a subset of these
file probes. Embed lookup's `stat` attempts and search-directory setup's
`stat` attempts are separate. These are calls made explicitly by the
preprocessor, excluding the harness and libc's directory-enumeration calls;
they are not process-wide syscall totals. Counts belong to the translation
unit, including when the prepared-file cache is shared.

Include lookup's inclusive interval starts before constructing candidates and
ends after locating or acquiring the file's bytes, before preparing or
executing its header. Embed lookup's interval covers constructing and checking
its candidate paths, before reading a resource. Both overlap the ordinary
phase breakdown and must not be added to it or subtracted again. The
`main loop/setup` remainder includes initialization as well as execution.

The final-output copy shares count file runs, buffer runs and direct
object-like copies at their destination. An intermediate capture contributes
nothing until tokens reach the final stream, where each copied token is
counted once. The denominator includes the final stream's pragma records and
its one record per embed resource; it does not expand an embed into bytes.
The rest of the stream comes from individual emissions or lifted pragma
records. The older run-work counters below include intermediate captures,
so their sum is not a final-output share. Timing-disabled preprocessing does
not collect the lookup intervals or final-output copy counters.

The [cloud macro-batching comparison](measurements/preprocessor-cloud-2026-10-23/README.md)
records the flat-argument shortcut and compiled literal-run metadata, including
their construction cost, generated assembly, differential tests and complete
paired samples. The later [9700X/Clang 23 paired measurements](https://github.com/buster14a/buster/pull/304#issuecomment-6361692970)
and [control reduction](https://github.com/buster14a/buster/pull/304#issuecomment-6361693442)
support a literal-run gain, smaller flat/nested gains and a small observed
argument-copy slowdown. They do not establish a general frontend gain:
real-main differences are comparable to the baseline/baseline controls.

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

## Assembly output spacing

In `-x assembler-with-cpp` mode, the lexer splits `$` from names and
numbers so that macros beside it still expand. Both printers use that same
boundary: `$VALUE` can print as `$4`, and `foo$bar` stays a single GNU as
symbol in the printed text. Whitespace and comments that separate tokens in
the input still produce spaces. C mode retains its identifier-separation rules.
The boundary is `pp_dollar_punctuation`'s, so C with
`-fno-dollars-in-identifiers` prints `$` as assembly does, and assembly with
`-fdollars-in-identifiers` as C does ("The `-f` options").

The vector printer clears `$`'s entry in both byte-class tables once per
call, and applies the same classification to the token carried between
64-token chunks. Its scalar twin's word-character test takes the same
boundary. `test-preprocessor` checks exact output bytes, including assembly macro
arguments, explicit whitespace, and the chunk boundary; comparing token
spellings alone would miss this change (#272).

## Not yet

- In the oracle's mode (`--clang-features`), the feature queries give clang's
  answers without options, but for the sanitizers': `-fblocks`, `-fno-builtin`,
  `-fno-gnu-inline-asm` and the other options that change clang's answers
  (#281's item 5) don't change ours.
- Invalid raw delimiters and unterminated raw strings still lack Clang's
  lexer diagnostics. Their token spelling is retained, including a line
  splice inside an invalid delimiter.
- A header query whose builtin identity appears during macro expansion can
  require a private prepared view when raw-looking header bytes swallowed
  later source. Each inclusion permits eight such repairs. A ninth produces
  a fatal capacity diagnostic. Executing direct directives have a separate
  budget of 64 exceptional repairs; ordinary directives consume none. The
  combined full-file copy/preparation work and retained storage are bounded
  by `O((64 + 8) * N)` for a file of `N` bytes. Unlimited contextual recovery
  remains follow-up work in #285. Excluded directives retain ordinary lexing,
  and ordinary multiline raw macro arguments retain their original tokens.
- c89 and C94 are lexed as clang's `-E` lexes them, without `//` comments. A
  compile driver would need clang's compiling rule there (`//` comments but for a
  `//*` before a file's first one), or to refuse those modes (#288).
- `#embed`'s line is expanded whole before its parameters are read, where clang
  expands it as it reads each one. The difference shows in one corner:
  `__has_include` and `__has_embed` inside `limit(...)` or `clang::offset(...)`
  are errors here, and clang evaluates them there as in `#if` (but not in
  `prefix(...)`). `-dE` (printing the directive rather than its data) isn't
  supported. A FIFO or a socket that `#embed` names is reported as a device
  file, where clang would read it.
- The target-builtin query table remains a bounded subset; see
  [the target contract](x86-target.md). Target feature switches and the
  predefined macros they affect are implemented as described in "The
  predefined macros" above. Feature queries use the compiler's implemented
  answers by default and Clang's answers with `--clang-features`.
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
- C++ (`-x c++`), dependency output (`-M`) aren't supported.
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
  table of Unicode's names).
- `-E -P` keeps no line numbers, so it doesn't move the token after a
  `_Pragma` that prints nothing to its own line, as clang's printer does when
  that token is on another line; `-E` with line markers does.

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
  `docs/c-refinement.md`). Refinement has since come to 1.67 ms of a 10.0 ms
  run. Its phase takes 0.12 ms more than its kernel does with everything in
  the caches. The columns, the source and the table come from memory, because
  a whole file is lexed before it is refined. A lexer that hands over a block
  at a time would keep them in the caches (`docs/c-refinement.md`, "Next
  steps").
- **Streaming to the parser.** The first version preprocesses a whole
  translation unit before the parser reads it. Streaming needs a rule for when
  a block of output is final, since a function-like macro name at the end of an
  expansion can take a `(` from the input after it (#117).
- **Per-file jobs** cached for the process, which lanes fill ahead of the
  preprocessor, are `docs/c-files.md`'s (#150).

## Reproduction

```sh
./build.sh
./build/compiler test-preprocessor --clang
./build/compiler test-preprocessor --clang path/to/sqlite3.c
taskset -c 2 ./build/compiler benchmark-preprocessor --clang path/to/sqlite3.c
taskset -c 2 ./build/compiler benchmark-preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char \
    src/buster/compiler/main.c
./build/compiler -E [-P] [-dM] [-o file] [options] file.c
./build/compiler test-preprocessor --generate-targets   # c_predefines.c's generated tables, from clang
./build/compiler test-preprocessor --tables src path/to/libc
taskset -c 2 ./build/compiler benchmark-preprocessor --tables path/to/libc
```

On the 9700X, run each one under `EXCLUSIVE_OWNER=<name>
~/buster-fast-bench/exclusive.sh`. `benchmark-preprocessor --only vectorized`
runs one implementation, for `perf record`.
