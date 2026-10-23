# Refinement budget qualification, 2026-10-23

This draft experiment addresses the remaining #116 budget: the actual
`benchmark-preprocessor` refine phase for raw SQLite and its system headers,
including any cost moved into adjacent phases. It makes no performance claim
until repeated 9700X results are recorded. Production refinement remains the
baseline while the candidates are compared.

The baseline refinement source is unchanged between `c62cd840` and
`5d78cc2b1e6b608c6ce04b6147ef0ba7f3f86d4f`. The latter is the integration base
for the draft CI comparison. Every candidate patch applies to that baseline,
and every executable includes the same current preprocessor benchmark.

| Candidate | Change from the baseline |
| --- | --- |
| base | Unchanged implementation |
| compress | Extract the first matching ID from the loaded bucket with a register compress |
| batch4 | Compress plus four probe results packed into one vector store |
| fused4 | Compress plus probing four identifiers immediately after their vector hash |
| bitmap | Compress plus reuse of the existing backslash-chunk bitmap |
| short16 | Bitmap plus first-16-byte hashing, with complete rehashing for longer identifiers |
| outline | Baseline probe with the hash batch kept out of line |
| combined | Fused4, bitmap reuse, and four-result vector stores |

The first screen's candidates preserve the existing hash values, normalized spellings, first-seen IDs,
leading-space bits and physical literal spellings. Fused4 and combined probe
the next batch before the current batch's deferred inserts. Misses are retried
in token order, and table growth invalidates and recomputes the prefetched
batch. The new regression cases force growth at eight batch boundaries.

The compress idea was also tried and discarded in the earlier cloud session
recorded in [#116's handoff](https://github.com/buster14a/buster/issues/116#issuecomment-6353068245).
This experiment was independently written against the same first-party
implementation and must earn its place with target-host measurements. No
external implementation is imported. The small C entry points include the
repository's benchmark and tests, with their existing component notices.
SQLite and installed system headers are runtime inputs and are not vendored.

## Inputs and procedure

| Runtime input | SHA-256 |
| --- | --- |
| SQLite 3.53.4, source ID `bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` | `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189` |
| SQLite 3.53.2, source ID `d6e03d8c777cfa2d35e3b60d8ec3e0187f3e9f99d8e2ee9cac695fd6fcdf1a24` | `0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d` |

The draft workflow uses frozen copies in the host's shared directory and checks
both hashes before building. It compiles each overlay with the normal flags,
runs the scalar/oracle correctness suite, and uses the native `ab` command to
rotate all eight executables across three processes per input. Each process
runs the existing benchmark's 21 passes from fresh preprocessor state. CPU 2
is fixed; the runner job hooks hold the shared host lock. Manual reproduction
must add `--lock /home/david/buster-fast-bench/exclusive.sh` to `ab` and use
`exclusive.sh` for each build, test and profile run separately.

The extra workflow step is temporary and restricted to this draft branch. It
does not change the required tests, CI priority or timeout. It will be removed
before the final implementation is submitted for merge. The candidates are
experiments, not production alternatives or runtime dispatch paths.

## Correctness so far

Local diagnostic validation passed the scalar and independent-oracle suite for
all candidates: guarded probe patterns, structured hashes, 300,000 interned
spellings, eight growth boundaries, all 64 alignments, splice/UCN cases,
random inputs and guarded page edges. The combined candidate also passed
ASan/UBSan with seed 42 and repository sources plus frozen SQLite. These local
results do not establish Zen 5 performance or replace target qualification.

An additional baseline run with seed 116 reports one unchanged hash in 14,861
aligned two-bit-flip cases, while scalar/vector hashes and all identifier,
whitespace and table checks agree. This pre-existing hash-quality assertion is
being isolated separately; none of these candidates changes the hash function.
The draft comparison uses the validated seed 42 identically for every variant.
The seed-116 failure remains recorded and is not a passing validation result.

## First 9700X screen

[Run 39943511804](https://github.com/buster14a/buster/actions/runs/39943511804)
passed the required job on draft head `761eec22` merged with `42630114`.
[The raw comparison](screen-first-9700x.txt) contains 72 successful processes,
each with 21 passes; [the summary](screen-first-9700x.tsv) retains the ranges.
On SQLite 3.53.4 the baseline refine phase was 1.693–1.712 ms, median 1.696 ms.
Compress and batch4 reached medians 1.674 and 1.673 ms; their whole-run ranges
overlapped the baseline. Fused4 and combined were slower, at 2.039 and 2.091 ms.
None meets the 1.5 ms acceptance requirement. No candidate has been selected.

## Second screen

[The hash regression](hash-regression.md) isolates the seed-116 failure to
discarding the high half of the folded AES state. The proposed final mixing
round fixes all four deterministic pairs and the 128-seed diagnostic sweep.
It changes internal table hashes, preserving scalar/vector equality, normalized
spellings, IDs and layouts. Its cost is part of the next measured comparison.

The second screen compares the original hash/refinement (`base`), the hash fix
alone (`finalmix`), and the fix with compress, batch4, ahead2 and ahead4. The
last two keep hashing and probing in separate stages, but prefetch two or four
batches ahead through a bounded ring. Growth invalidates bucket offsets before
probing, and inserts retain token order. Both passed the full seed-116 suite
over repository sources, SQLite and the Zig libc corpus locally.

The original hash is only a timing control: its known failure remains recorded,
and it is excluded from the new fixed-fold tests. Every proposed implementation
runs those tests with seed 116 on the 9700X. The screen rotates three processes
per input, and also measures the refinement kernel back to back and after
100 ms of scalar work. Separate cycle profiles are diagnostic and excluded from
the comparison. The user confirmed that the 1.5 ms target remains required.

[Run 39950101933](https://github.com/buster14a/buster/actions/runs/39950101933)
passed the required job on head `28afc09b9ef19b1224b23cadba7e643e62aa9565`.
Its tested merge was `1b91ac6e70693e7da1567f74f3341ebc844f66cb`.
The [54-process comparison](screen-second-9700x.txt), [summary](screen-second-9700x.tsv),
[36-process kernel comparison](kernels-second-9700x.txt) and
[cycle-profile symbol shares](profile-second-symbols.txt) are retained.
All proposed variants passed the target seed-116 checks, including the four
fixed hash cases. Both comparison commands reported zero failed processes.

| SQLite 3.53.4 | Refine range, ms | Median, ms | Whole-run range, ms |
| --- | ---: | ---: | ---: |
| Original hash/refinement | 1.695–1.715 | 1.711 | 11.842–11.855 |
| Hash fix alone | 1.724–1.734 | 1.732 | 11.877–11.924 |
| Hash fix + compress | 1.680–1.692 | 1.687 | 11.813–11.882 |
| Hash fix + batch4 | 1.679–1.694 | 1.688 | 11.825–11.856 |
| Hash fix + ahead2 | 1.637–1.654 | 1.647 | 11.770–11.818 |
| Hash fix + ahead4 | 1.661–1.668 | 1.666 | 11.784–11.909 |

Ahead2 also reduced the refine phase on SQLite 3.53.2 (1.633–1.638 ms versus
1.699–1.724) and main.c (3.454–3.474 versus 3.585–3.605), although main.c's
whole-run ranges overlap. It is the comparison candidate for further work;
the budget remains unmet, even before charging the previously moved arena fault.
Its warm raw-SQLite kernel is 1.338–1.344 ms, so its principal advantage here
is in the actual cold preparation path. The hash fix alone costs about 0.02 ms
on the SQLite refine phase in this screen.

The separate cycle profiles locate samples at the vector-to-scalar transfers
used to form prefetch addresses, as well as at the probes. They motivate a
follow-up comparison that reads the stored offsets after hashing, and larger
token blocks to amortize the prefetch pipeline's startup. These are hypotheses,
not established gains. A separate local prototype preserves refinement state
across blocks and passes the full corpus suite plus 3,072 guarded boundary
cases under ASan/UBSan. No lexer/preprocessor interface change has been made.

## Third screen

The next comparison keeps the original control and ahead2, and screens:

- prefetchloop1/2: prefetch the stored offsets after hashing the batch, with
  one/two batches of lookahead;
- block2k/4k/8k: ahead2 with 2,048/4,096/8,192 tokens per dense block;
- loop2block2k: the prefetch loop with two-batch lookahead and 2,048-token blocks;
- prefix28: two 32-byte slots per cache line, each holding 28 prefix bytes and
  the ID, with twice as many initial buckets to preserve capacity. The public
  16-byte `intern_first_bytes` helper stays intact for shared-file preparation.

Every variant passed the full local seed-116 refinement suite over repository
sources, SQLite and Zig libc. Prefix28 also passed `test-files --seed 116
--id-order 19`, including shared placement/growth, different lane counts,
reordered IDs and allocation failures. Its guarded probe test enumerates all
32,768 low/high-ID prefix patterns for the two wider slots.

The separate setup control measures fresh table initialization as well as the
refine phase and whole preprocessing run. A wider table's extra setup cost must
be charged to the budget; a lower refine time alone is insufficient. The profiles
from the second screen are retained [with their annotated assembly](profile-second-9700x.txt).

[Run 39956592698](https://github.com/buster14a/buster/actions/runs/39956592698)
passed the required job for head `84a6b0dc6e58270713d580b7066d76d763f0d0d1`,
tested as merge `e4ab6c1301bbac50003577f4f654c7fc162e2fe0` with main
`21d632c202fa9be20841ba3630220a2640087847`. All proposed variants passed the
seed-116 refinement checks; prefix28 passed the shared-file suite. The
[81-process comparison](screen-third-9700x.txt), [summary](screen-third-9700x.tsv),
[54-process kernel comparison](kernels-third-9700x.txt),
[18-process setup control](setup-third-9700x.txt) and
[three profiles with annotated assembly](profile-third-9700x.txt) are retained.
All comparison commands reported zero failed processes.

| SQLite 3.53.4 | Refine range, ms | Median, ms | Whole-run range, ms |
| --- | ---: | ---: | ---: |
| Original hash/refinement | 1.703–1.723 | 1.719 | 11.851–11.952 |
| ahead2 | 1.648–1.653 | 1.651 | 11.805–11.841 |
| prefetchloop1 | 1.702–1.705 | 1.704 | 11.812–11.901 |
| prefetchloop2 | 1.662–1.667 | 1.664 | 11.763–11.854 |
| block2k | 1.646–1.651 | 1.648 | 11.913–12.010 |
| block4k | 1.645–1.646 | 1.645 | 11.763–11.845 |
| block8k | 1.618–1.655 | 1.636 | 11.787–11.829 |
| loop2block2k | 1.628–1.640 | 1.633 | 11.703–11.734 |
| prefix28 | 1.538–1.540 | 1.538 | 11.821–11.905 |

Prefix28's fresh initialization costs 0.138–0.140 ms versus 0.069–0.071 ms
for the unchanged layout. Charging that extra setup and the previously moved
approximately 0.03 ms arena fault leaves about 1.64 ms. Its main.c whole-run
range also regresses to 36.806–36.820 ms from the control's 36.419–36.522 ms.
No third-screen candidate meets 1.5 ms. The setup wrapper intentionally omits
expansion history, so its output token counts differ from the regular benchmark;
all variants of that wrapper have identical token counts and digests.

## Fourth screen: consume completed lexer blocks

The next experiment uses the existing lexer range routines without changing
`c_lexer.c` or its interface. It keeps completed logical lines, resumes their
unfinished suffix in the next source window, and refines complete token blocks
while their columns and source are still cached. A growing window handles a
long logical line; scalar-only modes and C++ retain complete-file preparation.
The public prepared-file representation is unchanged. Lexer restart work stays
in the lex phase; the temporary leading-space buffer, copy, and refinement state
are charged to refinement. Any lex-phase increase must also be charged to the
1.5 ms budget. These are experiments, not accepted production changes.

The private preparation slice does not change #117's active macro-run or
include/embed accounting functions. The overlay is based on `447941a3`, which
merges main `de751b9`; its relative includes use the compiler source directory
as an additional include path. The hash remains the fixed hash. Four source
window sizes (16, 64, 256 and 1,024 KiB) are compared with the original control
and ahead2, all with the same preprocessor benchmark and frozen inputs.

Local qualification passed 12,288 guarded lexer/refinement cases and 57,408
guarded preparation cases under ASan/UBSan. The latter compare the scalar
complete-file path's IDs and spellings, leading-space bits, physical positions,
raw-string errors, unterminated comments, directive tables, notes and guards
at 13 window sizes, in three literal-prefix modes and all 64 alignments. They
also compare both SQLite inputs and main.c and force allocation failures.
The shared-file suite passes with 64-byte source windows, forcing the path on
small fixtures, including lane/ID permutations and 48 constrained-memory runs.
Clang comparison of main.c and SQLite agrees on all 3,217,305 output tokens.
Replaying the archived patches also passes the full scalar/oracle suite with
seed 116 on 9,171 repository/SQLite/libc files (115,706,051 bytes).
These local checks establish correctness evidence, not 9700X performance.

[Run 39963776327](https://github.com/buster14a/buster/actions/runs/39963776327)
passed the required job, testing merge `df4bbd88e784eca1c9ac19994fcebfbcbd6eed72`
of `b1cb4be1bf7d3f41735b10a809cf221f5dde8cfa` and main
`de751b9996157d61d3a112f91a5d7d86e056811e`. Both guarded suites, shared-file tests
and Clang comparison passed on the target. All 54 preprocessing and 18 kernel
processes passed. The [full comparison](screen-fourth-9700x.txt),
[summary including lex time](screen-fourth-9700x.tsv),
[kernel comparison](kernels-fourth-9700x.txt) and
[separate cycle profiles](profile-fourth-9700x.txt) are retained.

| SQLite 3.53.4 | Refine range, ms | Lex median, ms | Whole-run range, ms |
| --- | ---: | ---: | ---: |
| Original hash/refinement | 1.707–1.719 | 2.436 | 11.823–11.949 |
| ahead2 | 1.644–1.666 | 2.428 | 11.782–11.875 |
| 16 KiB source windows | 1.596–1.605 | 2.632 | 11.936–11.967 |
| 64 KiB | 1.585–1.591 | 2.450 | 11.720–11.770 |
| 256 KiB | 1.581–1.590 | 2.391 | 11.682–11.787 |
| 1,024 KiB | 1.629–1.648 | 2.410 | 11.844–11.887 |

The 256 KiB window avoids the small-window restart cost but still misses the
budget after charging the approximately 0.03 ms arena fault. On main.c it
reduces refinement slightly but increases the whole-run median to 36.690 ms
from 36.455 ms for ahead2. No production preparation change is selected.

An additional local fixture run initially failed because this directory's
small launcher did not dispatch the child test/benchmark commands used by the
location-limit test, and was outside the usual build/source directory layout.
The launcher now supports those commands and the Clang-oracle dispatcher.
With the normal directory layout, all fixtures and header checks pass; the
separate 3,000-case C17/C23 fuzz run agreed with both scalar and Clang results.

The next bounded screen tests the probe mask operations and 16-byte prefixes
without increasing the initial table allocation. It also tests 28-byte prefixes
in the original 16,384 buckets, growing at 7/8 occupancy instead of 5/8; this
trades more collision work for avoiding the extra huge page. The latter two
layouts are also combined with 256 KiB source windows. Layout capacity, growth,
shared placement and setup costs remain part of correctness and acceptance.

## Fifth screen: unchanged allocation size

This manual screen uses the frozen `b1cb4be1bf7d3f41735b10a809cf221f5dde8cfa`
source archive, Clang 23.1.1, the same frozen SQLite 3.53.4 and system headers,
CPU 2, and the ordinary exclusive wrapper for every build, test and benchmark
process. CI retains priority. The runs span a long queue wait, recorded in the
[raw output](screen-fifth-9700x.txt); each variant has three 21-pass processes.
The [build log](build-fifth-9700x.txt) records binary hashes, and the
[test log](tests-fifth-9700x.txt) records six successful seed-116 runs over all
9,171 repository/SQLite/libc files. The [summary](screen-fifth-9700x.tsv)
includes refinement, lexing, directive and whole-run timings.

| Candidate | Refine range, ms | Whole-run range, ms |
| --- | ---: | ---: |
| ahead2 | 1.653–1.661 | 11.756–11.830 |
| mask-register probe arithmetic | 1.638–1.640 | 11.687–11.803 |
| 16-byte prefixes | 1.705–1.728 | 11.782–11.962 |
| 28-byte prefixes, tighter table | 1.766–1.785 | 11.895–11.918 |
| 16-byte prefixes + 256 KiB windows | 1.634–1.642 | 11.733–11.757 |
| tighter 28-byte prefixes + windows | 1.704–1.711 | 11.762–11.875 |

All 18 processes pass. None reaches 1.5 ms before adjacent/setup charges, so
none is accepted. Avoiding the larger initial allocation loses the wider
prefix layout's benefit to collision work. Candidate patches named
`<variant>-c_<component>.patch` apply independently to the corresponding
`b1cb4be` source file; all include the fixed hash by default. Combined window
variants use `BUSTER_REFINE_SOURCE_CHUNK=262144`.

Two additional exact-name prototypes passed local scalar/oracle checks but
regressed local refinement substantially. `names` classifies sparse token
groups in pass A; `namesdense` classifies compacted identifiers and scatters
their results. Both use the sixteen predefined spellings in
[keyword-table.c](keyword-table.c), whose deterministic search emits
[refine-names.inc](refine-names.inc). This is first-party generated data, not an
external keyword table. The generated file goes beside the patched
`c_refine.c`. The corrected dense prototype clamps mask counts before `bzhi`;
the first prototype attempt exposed the instruction's modulo-256 count and
was rejected by the existing growth tests. The retained local test outputs
are [sparse](names-local.txt) and [dense](names-dense-local.txt), with separate
[sparse](names-local-timing.txt) and [dense](names-dense-local-timing.txt)
diagnostic timings. These local results are not target-machine acceptance.

An additional 256-slot short-identifier cache uses exact zero-padded source
words, vector gathers and duplicate-safe scatter updates. It also passes the
local scalar/oracle suite but regresses local refinement; its extra filter,
compaction and result-scatter work outweighs the saved main-table operations.
Its [patch](candidates/shortcache-c_refine.patch), [tests](shortcache-local.txt)
and [local diagnostic timing](shortcache-local-timing.txt) are retained.

## Sixth screen: hash handoff and hot-source prefetches

This manual screen retains the frozen source and ordinary per-process locking.
It compares compact four-byte hash offsets, separate prefetch consumption,
two-group hash scheduling, vector collection of four probe IDs, and L1/L2/no
source-column prefetches with 256 KiB source windows. All seven new variants
pass the full target seed-116 repository/SQLite/libc refinement suite, and all
72 preprocessing processes pass. The [raw output](screen-sixth-9700x.txt),
[summary](screen-sixth-9700x.tsv), [earlier builds](build-sixth-9700x.txt) and
[orchestration](sixth-run.sh) are retained. `fifth-main.c`, `screen-setup.c` and
`fifth-build.sh` are the shared standalone driver and target build command.

| SQLite 3.53.4 | Refine range, ms | Whole-run range, ms |
| --- | ---: | ---: |
| mask-register probe control | 1.648–1.650 | 11.709–11.778 |
| packed offsets | 1.639–1.660 | 11.731–11.846 |
| packed offsets, separate prefetch loop | 1.688–1.705 | 11.800–11.829 |
| two hash groups scheduled together | 1.657–1.669 | 11.672–11.775 |
| vector collection of four probe IDs | 1.660–1.666 | 11.875–12.007 |
| 256 KiB windows, L1 source/column prefetch | 1.593–1.598 | 11.703–11.734 |
| windows, no source/column prefetch | 1.540–1.547 | 11.614–11.643 |
| windows, L2 source/column prefetch | 1.589–1.600 | 11.653–11.692 |

Removing the prefetches for source and columns that were just produced improves
the window path. Table and record prefetches remain. The same candidate takes
1.536–1.541 ms on SQLite 3.53.2, and main.c's whole-run median is 36.298 ms
versus 36.616 ms for the control, with some overlap in the whole-run ranges.
It still misses 1.5 ms before the approximately 0.03 ms arena charge, so no
production candidate is selected. Packed offsets, separated prefetches and
vector result collection are rejected.

The seventh screen varies source window size, token-block size, identifier
batch size and prefetch distance around this improved path. It also combines
the wider-prefix table with the hot-source path, with the extra table setup
still charged. Production refinement and preparation remain unchanged while
that qualification runs.

## Seventh screen: bounded-window parameters

All seven new variants passed the full target seed-116 suite. The
[raw build, test and comparison output](screen-seventh-9700x.txt) records
72 successful preprocessing processes and six setup-control processes. The
[summary](screen-seventh-9700x.tsv), [build command](seventh-build.sh) and
[orchestration](seventh-run.sh) retain the same frozen b1cb4be source and
per-process locking. `tune` supplies the parameterized original-table path;
`prefix28hot` supplies the larger-table alternative.

| SQLite 3.53.4 | Refine range, ms | Lex median, ms | Whole-run range, ms |
| --- | ---: | ---: | ---: |
| 256 KiB window control | 1.544–1.547 | 2.409 | 11.623–11.643 |
| 64 KiB windows | 1.535–1.542 | 2.464 | 11.637–11.767 |
| 128 KiB windows | 1.535–1.543 | 2.418 | 11.583–11.693 |
| 512-token blocks | 1.589–1.593 | 2.409 | 11.654–11.789 |
| 2,048-token blocks | 1.544–1.545 | 2.412 | 11.599–11.648 |
| 32-identifier batches | 1.711–1.713 | 2.415 | 11.863–11.922 |
| One-batch prefetch distance | 1.571–1.579 | 2.416 | 11.655–11.704 |
| Wider prefixes, 256 KiB windows | 1.468–1.472 | 2.411 | 11.629–11.648 |

The 128 KiB original-table variant is still above 1.5 ms before the historical
approximately 0.03 ms arena charge. Its SQLite 3.53.2 range is 1.531–1.549 ms;
main.c's whole-run range is 36.444–36.757 ms. The 64 KiB window's additional
lexing cost must also be charged, and its whole-run result is worse.

The apparent 1.47 ms wider-prefix result does not satisfy the budget: fresh
setup costs about 0.132 ms versus 0.069–0.071 ms for the original table. With
that extra 0.062 ms and the historical charge, it costs about 1.56 ms. Its
main.c whole-run range also regresses to 36.810–37.054 ms. No performance
candidate is selected. A fresh profile of the 128 KiB path and a smaller
intern-span representation are the next bounded checks.

## Eighth screen: record prefetch and compact spans

The [36-process target comparison](screen-eighth-9700x.txt),
[summary](screen-eighth-9700x.tsv), [earlier build](build-eighth-9700x.txt),
[orchestration](eighth-run.sh) and [fresh cycle profile](profile-eighth-9700x.txt)
retain the frozen source, 21-pass processes and ordinary per-run locking.
Both new variants pass the full target seed-116 suite. `earlyrecords` moves
record prefetches immediately after each four bucket probes. `span32` stores
only record offsets, deriving lengths from the next offset and an end sentinel;
ordinary and parallel bulk insertion preserve contiguous records in ID order.
Its [shared-file checks](span32-shared-local.txt),
[guarded preparation checks](span32-prepare-local.txt) and
[sanitizer run](span32-sanitizers-local.txt) also pass locally.

| SQLite 3.53.4 | Refine range, ms | Whole-run range, ms |
| --- | ---: | ---: |
| 256 KiB window control | 1.538–1.551 | 11.580–11.674 |
| 128 KiB window control | 1.530–1.542 | 11.565–11.769 |
| Earlier record prefetch, 256 KiB | 1.540–1.547 | 11.628–11.646 |
| Compact spans, 128 KiB | 1.544–1.554 | 11.614–11.683 |

Neither candidate improves the required result. Compact spans also increase
main.c's whole-run median from 36.646 ms to 37.084 ms. Both are rejected.
The cycle profile remains diagnostic: profiling perturbs timings, and cycle
samples can skid. Most refinement samples remain in pass B's hashing/probing;
the source scan and token-column passes are smaller contributors.

Additional local-only patches retain negative experiments: `gatherprobe`
probes eight buckets with vector gathers; `noscan`, `gapgather` and `gapinline`
replace the source scan with gap checks; `compact75`/`compact87` start the
original layout with fewer buckets and higher occupancy. They passed the local
refinement suite but regressed diagnostic timings and have no target result.

## Ninth screen: reuse lexer backslash detection

The #117 owner released its completed source claim after PR #323 and main CI
passed. The open-PR inventory had no other lexer/refinement/intern edits. This
bounded experiment adds an optional internal bitmap output to the lexer chunk
loop; public `c_tokenize` behavior is unchanged, and its existing wrappers pass
no bitmap. C modes with and without raw strings/digit separators have separate
specializations. Skipped comments and scalar declines conservatively mark all
covered regions, and overlapping/restarted prefixes only add bits. Other modes
retain the existing source scan. Initialization and lexer costs are measured.

The [target output](screen-ninth-9700x.txt),
[summary](screen-ninth-9700x.tsv) and [orchestration](ninth-run.sh) record the
full refinement suite, 57,408 guarded preparation cases, both SQLite inputs,
main.c and allocation failures passing, plus 18 successful preprocessing
processes. [Local preparation sanitizers](lexgap-preparation-sanitizers-local.txt)
also pass. The initial local prototype did not enable the no-digit-separator
mode used by the SQLite benchmark; the archived variant and target screen
include that mode explicitly.

SQLite 3.53.4 refinement falls from 1.532–1.539 ms to 1.507–1.511 ms, but
lexing rises from a 2.409 ms median to 2.442 ms. Whole time rises from
11.559–11.591 ms to 11.600–11.631 ms. The shifted work defeats acceptance.
The next prototype marks only the lexer's existing possible-splice path,
avoiding a per-chunk bitmap check and irrelevant literal backslashes.

## Tenth screen: finalization, flag reuse and intermediate table size

The [target output](screen-tenth-9700x.txt),
[summary](screen-tenth-9700x.tsv) and [orchestration](tenth-run.sh) record three
new variants passing the full target refinement suite, scaled-bucket shared
placement/growth checks, 36 preprocessing processes and six setup processes.
`foldxor` folds the high and low qwords after the two AES rounds instead of
adding the final AES round. It passes the fixed regressions and the
[local 128-seed sweep](foldxor-hash-seeds-local.txt), but loses on timing.
`hashflags` carries length classifications from hashing into probing, avoiding
a repeated length load; it also loses on timing.

`prefix28scaled` uses 24,576 two-slot buckets with multiplicative hash mapping
and explicit probe wrap, intermediate between the earlier 16K and 32K layouts.
All scalar/vector/shared consumers use the same mapping. Its
[local shared-file suite](prefix28scaled-shared-local.txt) passes too.

| SQLite 3.53.4 | Refine range, ms | Whole-run range, ms |
| --- | ---: | ---: |
| 128 KiB original-table control | 1.529–1.541 | 11.598–11.619 |
| 24K wide-prefix buckets | 1.503–1.505 | 11.538–11.696 |
| XOR finalization | 1.552–1.557 | 11.599–11.634 |
| Reused length flags | 1.557–1.563 | 11.591–11.621 |

The intermediate table initializes in 0.053–0.054 ms versus 0.061–0.063 ms
for the control, so it avoids the former setup penalty. It still exceeds the
budget, including the historical charge, and main.c refinement regresses to
4.301–4.321 ms from 3.427–3.487 ms; whole time rises to 36.989–37.258 ms from
36.394–36.687 ms. It is not selected. A local footprint diagnostic shows that
the current compiler-source workload crosses two growth thresholds with this
capacity. The next screen tests compact spans with higher capacity, the more
selective splice handoff, their combination and sixteen-ID probe publication.

## Eleventh screen: selective splice reuse

The [target output](screen-eleventh-9700x.txt),
[summary](screen-eleventh-9700x.tsv), [orchestration](eleventh-run.sh) and
[overlay hashes](eleventh-overlay-sha256.txt) retain 54 preprocessing processes
and six setup processes. All four new variants pass the target refinement
suite on 9,171 files (115,706,051 bytes). The combined variant also passes
shared-file checks, 57,408 guarded preparation cases, both SQLite inputs,
main.c, allocation failures and
[local preparation sanitizers](compactsplices-preparation-sanitizers-local.txt).
These overlays apply to frozen `b1cb4be`, as do screens five through ten.

`lexsplices` marks regions only inside the lexer's existing possible-splice
branch. `probe16` publishes sixteen probe IDs with one vector store.
`prefix28compact` raises the scaled wide-prefix table's capacity and derives
record lengths from compact offsets. `compactsplices` combines that layout
with selective splice reuse.

| SQLite 3.53.4 | Refine range, ms | Lex median, ms | Whole-run range, ms |
| --- | ---: | ---: | ---: |
| 128 KiB original-table control | 1.534–1.546 | 2.414 | 11.589–11.617 |
| Scaled wide-prefix control | 1.489–1.504 | 2.410 | 11.534–11.638 |
| Sixteen-ID probe publication | 1.534–1.542 | 2.423 | 11.592–11.663 |
| Selective splice reuse | 1.473–1.483 | 2.419 | 11.509–11.581 |
| Compact wide-prefix table | 1.491–1.514 | 2.408 | 11.570–11.602 |
| Compact table and splice reuse | 1.444–1.454 | 2.429 | 11.521–11.600 |

Selective splice reuse improves both SQLite inputs: SQLite 3.53.2 refinement
takes 1.470–1.483 ms and the whole run 11.513–11.546 ms. It preserves main.c's
whole-run median (36.486 ms versus 36.494 ms). Counting the historical arena
charge and its small extra lexing cost still puts it slightly above 1.5 ms.
It is promising, but does not yet establish acceptance.

The compact wide-prefix combination's smaller raw SQLite number does not make
it a suitable implementation: main.c takes 37.410–37.547 ms, versus the
original table's 36.419–36.948 ms. Increasing capacity did not cure the
wide-table regression. The next experiment keeps the original table, records
precise splice regions, and restores the leading-space allocation to the
permanent arena so refinement directly measures its first-touch cost.
