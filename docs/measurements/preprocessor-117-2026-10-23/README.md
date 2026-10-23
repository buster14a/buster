# Preprocessor acceptance, 2026-10-23

This records the remaining acceptance work under
[#117](https://github.com/buster14a/buster/issues/117), after the implementation
stack and the independent literal-run work in #304 landed. Historical results
in [the design](../../c-preprocessor.md) retain their original inputs and scope.

## Source and machine

The candidate compiler source is
`9b70ee7c9f6a285f5bdd175cbbabf0b938edcdc9`, which combines the accounting change,
repaired fixtures and independent scalar reference with main
`21d632c202fa9be20841ba3630220a2640087847`.
The baseline compiler uses that main revision. Both compilers read the same
frozen repository input from `635434f2a4d153c6de1dd2f1149055c4507c6f46`.
Later documentation commits do not change these measured compiler sources
or inputs.

| Archive | SHA-256 |
| --- | --- |
| Candidate | `172ec00f7d0de882c93da42211595f9b00590c26be66836d01e948b37c40e841` |
| Baseline | `153fd8b801124f235e05b988643b5b971f0aa3c86a3cb128d693122dbc2a8d7e` |
| Frozen repository input | `8faf61f7982ef6272cab79ec25554f6723e55323f85091222a586bed1b433acc` |

The approved host is benchpress, AMD Ryzen 7 9700X. Every manual build, test
and measurement takes its own normal `exclusive.sh` lock, with CI priority.
Measurements are pinned to CPU 2. Compiler binary hashes, exact commands and input
hashes accompany the results below. No local-machine timing is acceptance
evidence.

The provenance receipt records Linux `6.18.50-2-lts`, Clang 23.1.1 and the
`performance` CPU governor and energy preference. Both compilers were built
with the repository's `./build.sh`, which selected `/usr/bin/wild`; the retained
compile-command records include `-O3 -march=native -fwrapv -funsigned-char
-fno-strict-aliasing` and the x86-64 Linux target. Hardware-counter results
are positive rather than unavailable zero placeholders.

Both frozen compilers include #318's raw-string diagnostics and recovery.
That change adds work during ordinary GNU17 preparation and execution, so the
earlier built pair was superseded before any manual corpus or controlled
timing campaign ran.
The candidate's accounting and scalar-reference patch is unchanged against
this matched baseline. The source manifests contain 438 files per compiler;
the correctness inventory covers the candidate's 169 C implementation files.
That inventory is separate from the frozen performance workload.

The external corpus is the immutable
`/home/david/buster-fast-bench/preprocessor-corpora-astra-01a158dd` bundle;
`MANIFESTS.sha256` identifies it as
`391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd`.
[The oracle guide](../../preprocessor-oracle.md) records the individual
manifests, source versions, pinned headers, reference compiler and flags.
SQLite is 3.53.2, musl is patched 1.2.5, and the libc inventory is Zig 0.14.1's
8,983 C/header files. New reports use the original frozen lists, with all
four musl profiles kept separate. A Clang rejection is an explicit skip with
its reason, never a passing comparison. `SOURCE_DATE_EPOCH=1792454400` fixes
date/time macros.

The frozen performance `main.c` also includes `linux/memfd.h`, which the
original 778-header bundle does not contain. The first main benchmark and
macro comparison failed with that missing header; both Clang and Buster
reported it. Their logs are retained. The main-only profile therefore appends
`main-headers-v1/usr/include` after all original pinned system directories.
This separate supplement contains only `linux/memfd.h`, copied from benchpress's
`linux-api-headers 7.2-1`, SHA-256
`d06dd37df2ad2df29391c38239c1af6944efef8dc4040ed41833169cd4c4a3c9`.
Its sole transitive include, `asm-generic/hugetlb_encode.h`, already belongs to
the original bundle and matches the installed file byte for byte. Both
compilers, Clang, final macro comparisons and guard controls use the same
supplemented main profile. No source input or original corpus header changed;
SQLite, musl and libc retain the original #125 configuration. Header bytes
remain on the host, with package provenance and their manifest recorded here.
The same supplement is also used for a separate follow-up on the 11 composed
implementation files rejected solely because `linux/memfd.h` was missing.
The original implementation report retains those skips.

Corpus comparisons check scalar/vector streams, expansion history and directive
tables, print/relex round trips, both printers with and without line markers,
and Clang token spellings and presumed file/line/include-stack locations. These
semantic checks apply to Clang-accepted inputs; rejected files remain explicit
skips. The separate raw directive-table oracle covers every discovered C/header
file, including rejected inputs. The file-mode final macro comparisons normalize
only `__clang_patchlevel__`, `__clang_version__`, `__VERSION__` and `__BUSTER__`;
reported agreement means equality after those documented normalizations.

## Measurement boundaries

The ordinary benchmark runs 21 measured passes after a warm-up, with a new
translation unit and intern table each time. It checks repeat stability within
each kernel; exact scalar/vector agreement comes from the separate file oracle
on these frozen inputs. The guard-cost benchmark also checks a normalized
output-token digest across its two controls; it excludes locations, whose exact
agreement is checked by the separate file oracle. The fork-and-exec Clang timing uses the same input and
options on the same host. Memory release and printing remain separately visible.
The operating-system page cache is warm; compiler-owned state is rebuilt.
Each displayed phase time is its own median, so rounded phase medians need
not sum to the displayed whole-run median.
Hardware cycles and instructions count user-space execution; wall time also
includes kernel work. Phase intervals use timestamp-counter ticks converted
with the measured whole-run wall time. The baseline/candidate medians provide
context for the accounting change, without establishing a speedup or a bound
on measurement noise.

The `main loop/setup` phase is the existing remainder after read, lex, refine
and directive tables. It includes initialization. Include-search timing is an
inclusive interval covering candidate construction, cache lookup and first
byte acquisition, ending before header preparation/execution. Embed-search
timing covers candidate construction and `stat`, before resource reads.
These intervals overlap the phase breakdown and are not additional phases.

File-call counts cover explicit preprocessor `open`/`fstat` attempts, including
failures and uncached identity aliases. Include counts are a subset. Embed
and search-directory `stat` attempts are separate. Harness I/O and libc's
directory enumeration are outside these counts. The counters belong to each
translation unit even when prepared files are shared.

The final-output copy numerator counts only file runs, buffer runs and direct
object-like copies whose destination is the final stream. Intermediate
captures contribute nothing. The denominator is final output tokens, including
pragma records and one token per embed resource. The remaining output is
emitted individually or lifted from captured pragma records. The old run-work
totals include captures and cannot be used as this numerator.

## Validation and measurements

The earlier required [9700X CI run](https://github.com/buster14a/buster/actions/runs/39943492933)
passed on 2026-10-23, before the #318 integration. It checked out merge composition
`d3347ec014d3a61e3bf5f65e5b2136fc14a432dd`, whose parents are main
`42630114aeb0553c3e2fabfd7fe9bc84b0ce6126` and candidate
`6db52d0a2231337a11f7656772d7a02f9daae585`. The preprocessor, its benchmark,
fixtures, history, refinement and predefines sources are identical to the
earlier candidate `6db52d0`. The build succeeded and all 48 test commands passed.
Required CI on the final published composition is a separate gate, tracked on
[PR #323](https://github.com/buster14a/buster/pull/323). The checks below describe
this earlier receipt and do not substitute for that gate. Keeping the live CI
and merge receipts on the PR avoids changing the tested head merely to append
its own later result to this report.

The accounting fixtures passed in scalar/vector and timing-disabled modes.
The independent scalar regression passed with inverted cached macro bits,
and history-copy checks passed for 66 lengths and both overlap directions.
The no-file `test-preprocessor --clang` run passed 320 cases, 2,004 random
directive sources/nests, the structure/definition/guard/location checks,
and 1,500 differential cases per C17/C23 mode with zero differences or
unverified results. The repository file oracle compared 31 units and
19,099,101 tokens with zero differences.

| Area | Completed checks in that CI run |
| --- | --- |
| #118 | 2,004 random directive sources/nests; 26 structure and seven diagnostic-text fixtures agree with Clang |
| #119 | Exact lookup accounting in scalar/vector, timing-disabled and shared-cache modes; two embed-lookup fixtures |
| #120 | 20 guard/once fixtures agree on output and skip counts |
| #121 | Ten definition-diagnostic fixtures; 2,178 target/predefine option sets agree with Clang's macro dumps |
| #122 | Inverted macro-bit regression; 66 history-copy lengths; final-copy accounting; 1,500 C17 and 1,500 C23 differential cases |
| #123 | Generated arithmetic, signedness/limits, short-circuit, nested-ternary and conditional-nest checks in the 1,500-case C17/C23 batches; 251 feature names in 11 modes; 54 sanitizer/target option sets; 70 accepted/refused option sets |
| #124 | Both location-limit exhaustion cases; all five command-option checks; 1,204 embed-text checks; token/location and printer checks in the repository oracle |

The independent fuzz-harness tests also passed 1,481 checks in each of C17 and
C23. These short runs do not replace #109's separate long campaigns.

The CI frozen subset verified the pinned manifests and reference compiler,
then reported SQLite 1 identical, musl 4 identical, and libc 5 identical plus
2 explicit Clang-rejection skips, all with zero differences/errors. Those
subset counts are not a full-corpus qualification.

The fresh manual campaign uses the composed candidate `9b70ee7`. Source and
corpus provenance checks passed before execution. The original corpus profiles
reported:

| Corpus/profile | Identical | Clang-rejection skips | Differences/errors |
| --- | ---: | ---: | ---: |
| SQLite | 1 | 0 | 0 / 0 |
| musl ordinary | 1,587 | 0 | 0 / 0 |
| musl crt | 1 | 0 | 0 / 0 |
| musl crt-pic | 2 | 0 | 0 / 0 |
| musl ldso | 1 | 0 | 0 / 0 |
| libc C/header inventory | 5,129 | 3,854 | 0 / 0 |
| Frozen #125 repository | 23 | 0 | 0 / 0 |
| Composed implementation, original profile | 157 | 12 | 0 / 0 |
| Eleven missing-header implementation files, supplemented profile | 11 | 0 | 0 / 0 |

All 1,591 musl sources passed under their corresponding profiles. Each libc
skip retains Clang's reason in the TSV and diagnostic log. The independent
raw directive-table oracle checked all 8,983 libc files and all 209 C/header
files in the complete candidate snapshot: 9,192 files, 14,960,481 tokens,
603,202 directives and 4,126 guards, with identical tables.

The implementation's 12 skips comprise 11 missing-`linux/memfd.h` rejections
and `c_lexer_variant.c`, which requires the build's include-filename define.
The latter is not a standalone preprocessing input under this generic profile.
All 11 affected files passed the separate supplemented follow-up, comparing
22,378,484 tokens. Together the two profiles qualify 168 distinct implementation
files; the one configuration-dependent variant remains an explicit skip.
File-mode comparisons on SQLite (706,237 output tokens) and the supplemented
frozen main (2,282,574) passed, including normalized final macro dumps and guard
counts. The main result supersedes only the failed main comparison.

The full-libc microbenchmarks used 21 passes on CPU 2:

| Kernel | Vector | Scalar |
| --- | ---: | ---: |
| Directive tables, ms | 5.604 | 15.502 |
| Table cycles/input token | 2.361 | 6.527 |
| Table cycles/directive | 51.90 | 143.48 |
| Cycles/`#define` | 117.9 | 118.0 |
| Cycles/compiled macro | 130.8 | 131.0 |
| Cycles/macro lookup | 3.70 | 3.37 |

The table benchmark covers 13,152,141 tokens and 598,310 directives in all
8,983 libc files. Its scan-only costs are 0.637 vector and 3.933 scalar cycles
per token. These isolated costs are not whole-preprocessor timings.

The definition benchmark visits 386,790 raw `#define` directives, compiles
386,777 macros and performs 6,115,884 identifier lookups (9.9% macro hits).
It measures raw definitions file by file outside conditional execution.
Both lookup rows share the harness's 64-kind SIMD classification; the scalar
row changes membership lookup to the direct table. Their difference is not
a whole-preprocessor performance claim.

### SQLite whole-workload measurement

Both compiler versions read 10,305,768 bytes, produce 1,242,327 input tokens
and 706,237 output tokens. Whole time excludes the separately timed release
and printing steps.

| Compiler/kernel | Whole ms | Cycles/input token | Cycles/output token | With `-E -P` printing, ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline vector | 11.923 | 41.87 | 73.65 | 16.426 |
| Candidate vector | 12.001 | 42.31 | 74.42 | 16.444 |
| Baseline scalar | 24.534 | 98.02 | 172.42 | 35.607 |
| Candidate independent scalar | 24.237 | 96.71 | 170.12 | 36.027 |

The baseline's scalar mode still used cached macro membership and bulk
temporary-origin copies. The candidate scalar row measures the repaired,
independent reference; it is not the same scalar implementation.

The candidate's vector printing cost is 4.437 ms, with whole printed
throughput of 627 MB/s of input and 42.9 million output tokens/s. Release is
0.476 ms. Fork-and-exec `clang -E -P`, writing to `/dev/null`, takes 58.731 ms
in the candidate run and 58.634 ms in the baseline run. The independent scalar
prints at 286 MB/s and 19.6 million output tokens/s. One matched pair of
21-pass medians does not establish a candidate improvement or regression
beyond measurement variability.
Clang includes process startup, while the Buster in-process figures exclude
startup and release; their ratio is not an equivalent end-to-end speedup.

Candidate vector phase medians are read 0.719 ms, lex 2.439 ms, refine 1.711 ms,
directive tables 0.301 ms and main loop/setup 6.846 ms. The last two sum to
approximately **7.147 ms**, from independently rounded medians. This is about
3.57 times the retained **2 ms** budget; that budget is **not met**.

Inclusive include resolution takes 0.629 ms vector and 0.633 ms scalar. Per
pass, direct calls are 480 `open`, 170 `fstat`, zero embed `stat` and three
search-directory `stat` attempts. The include subset is 361 searches, 346
found, 479 `open` and 169 `fstat`; no embed searches occur. Those include times
overlap read/main-loop work and must not be added to or subtracted from the
phase sum.

Final output contains 559,953 file-run tokens, 108,261 buffer-run tokens and
37,807 direct-object tokens: **706,021 / 706,237 = 99.9694%** copied by the fast
paths, leaving 216 other tokens. File/buffer runs account for 94.6161% and
direct objects for 5.3533%. The raw log rounds their total to `100.0%`; it does
not mean every token was copied. Intermediate buffer captures account for
another 12,336 run-work tokens and are correctly excluded from this share.

In the matched guard controls, skipping enabled takes 11.889 ms and disabled
takes 11.888 ms, with identical normalized output-token digests. The enabled path skips 98
inclusions by guards and zero by `#pragma once`. This run shows no measurable
wall-time saving from guard skips; it records the requested control rather
than claiming a benefit from noise.

### Frozen main whole-workload measurement

The supplemented main profile reads 14,351,286 bytes, with 1,890,827 input
tokens and 2,282,574 output tokens. These inputs are identical for both
compilers and the Clang comparisons.

| Compiler/kernel | Whole ms | Cycles/input token | Cycles/output token | With `-E -P` printing, ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline vector | 34.775 | 84.05 | 69.63 | 49.586 |
| Candidate vector | 35.033 | 84.73 | 70.19 | 49.574 |
| Baseline scalar | 63.735 | 168.64 | 139.70 | 100.013 |
| Candidate independent scalar | 63.938 | 169.18 | 140.14 | 101.874 |

Candidate vector printing costs 14.560 ms, with whole printed throughput of
289 MB/s and 46.0 million output tokens/s; release costs 0.905 ms. Independent
scalar printing costs 37.889 ms, with whole printed throughput of 141 MB/s and
22.4 million tokens/s. Fork-and-exec Clang takes 168.126 ms in the candidate
campaign and 167.872 ms in the baseline campaign, with the same startup/release
boundary caveat as SQLite. These pairs do not establish a speedup or overhead
bound.

Candidate vector phase medians are read 2.210 ms, lex 3.155 ms, refine 3.443 ms,
directive tables 0.290 ms and main loop/setup 25.937 ms. Include resolution is
an overlapping 2.379 ms vector and 2.388 ms scalar interval. Each pass performs
1,233 direct `open` and 433 `fstat` attempts plus five search-directory `stat`
attempts. The include subset is 1,350 searches, 1,317 found, 1,232 `open` and
432 `fstat`; embed searches and `stat` attempts are zero.

The final-copy paths account for 1,488,497 file-run, 579,910 buffer-run and
213,138 direct-object tokens: **2,281,545 / 2,282,574 = 99.9549%**, with 1,029
other tokens. File/buffer runs contribute 90.6173% and direct objects 9.3376%.
The raw `100.0%` again reflects rounding. The aggregate buffer-run count of
594,331 includes 14,421 intermediate copies excluded from the numerator.

Guard skipping enabled takes 35.050 ms, versus 35.125 ms disabled, with
identical normalized output-token digests. The enabled path skips 771 inclusions by guards and
zero by `#pragma once`. This small observed control difference is recorded
without a claim that it exceeds run-to-run variability.

The manual campaign completed successfully at 14:56:58 UTC on 2026-10-23.
Both initial missing-header failures remain in the evidence. Final required
CI and protected integration are separate receipts tracked on PR #323.

## Retained commands and receipts

- [Source/corpus verification](verify-provenance.sh) checks the complete
  frozen source inventories, every corpus byte, reference compiler, binary
  hashes, compile commands and host identity.
- [Original campaign commands](run-acceptance.sh) record all four corpus
  profiles, the raw table oracle, table/definition costs, whole-workload
  measurements and final macro comparisons.
- [Completion commands](complete-acceptance.sh) add the separately pinned
  header profile, rerun only the affected main comparison, complete unfinished
  main/guard measurements, and qualify the 11 affected implementation files.
  [The affected list](implementation-memfd-v1.txt) is separate from the
  [complete implementation inventory](implementation-c.txt).
- [The execution wrapper](window-run.sh) ran with
  `BUSTER_117_WINDOW_LIMIT_EPOCH=0` throughout this campaign: normal access,
  separate locks, no coordinated hold or deadline. It does not reserve the
  machine between commands.
- `raw/` retains exact-command logs, successful receipts, both initial
  missing-header failures, the build/provenance logs, compiler command records
  and every per-file TSV result. Logs use lossless gzip with fixed metadata.
  [libc.tsv](raw/libc.tsv) retains every skip reason.
- `inputs/` retains the original argument files, compressed inventories and
  corpus manifests. `source-identities/` records every frozen source path and
  byte hash; `main-header-provenance/` records the supplement's hash and
  package origin. No external source corpus, system header or compiler binary
  is committed.
- [SHA256SUMS](SHA256SUMS) covers every retained report, script, inventory and
  receipt in this directory except the checksum manifest itself.

The actual host root is
`/home/david/buster-fast-bench/codex-117-01a16812`. The original invocation
scripts there are named `117-runs-9b70ee7.sh`,
`117-provenance-9b70ee7.sh`, `117-complete-9b70ee7.sh` and
`117-complete-9b70ee7-v2.sh`; the completion copy here includes the final
implementation follow-up. Successful campaign receipts give complete arguments
and acquisition/completion timestamps; failed receipts preserve their commands
and diagnostics. Each command takes the normal host
lock itself; do not wrap an entire campaign script in another lock. The
scripts refuse to replace an existing receipt. A later campaign must select
a fresh evidence directory while preserving the recorded input identities.

## Acceptance boundaries

| Issue | Evidence required by this refresh |
| --- | --- |
| #118 | Independent full-libc directive-table oracle and table-cost measurement |
| #119 | Exact lookup-accounting fixtures and SQLite/main include costs |
| #120 | Guard/once fixtures and matched guard-skip on/off measurements |
| #121 | Definition fixtures, final macro-table comparison and target `--defines` costs |
| #122 | Independent scalar macro recognition and token copies; scalar/vector and short-fuzz agreement, exact final-copy shares and whole costs |
| #123 | Expression/feature-mode fixtures and frozen-corpus comparisons |
| #124 | Token/location/printer round trips, location limits and printed throughput |
| #125 | Existing completed oracle implementation; fresh results are recorded here |

The three #117 deliverables are correctness/corpus reporting, target-host
benchmark reporting, and documentation. Completing those deliverables does
not waive #115's separate 2 ms directive-table-plus-main-loop ceiling.
[The coordination decision](https://github.com/buster14a/buster/issues/115#issuecomment-6322649228)
retains that budget. #117 remains open if the budget is not demonstrated.
Header replay (#111), refinement (#116), and the ten-million-case campaigns
on both required hosts (#109) retain their own scopes. No timing improvement
is attributed to the added accounting.

The acceptance audit also found that scalar run scans still consulted the
macro-name bitset. #122 explicitly requires an independent scalar reference.
The candidate now reads the macro table directly in scalar expansion and
copies temporary origins one element at a time. New regressions invert the
cached macro bits across definition changes and check overlap in both
directions. Independent source review found no further issue. The independent
regression and short-fuzz receipts, together with the composed candidate's
corpus checks, supply the acceptance evidence; final CI and issue reconciliation
are recorded on PR #323. Existing byte-identical scalar/vector results do not
by themselves prove independence from the shared cached bits.
