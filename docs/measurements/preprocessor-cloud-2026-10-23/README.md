# Preprocessor batch optimization: cloud evidence

The retained change skips unnecessary argument-depth scans and records literal
run lengths once per compiled macro. In the final paired cloud comparison,
the long-literal workload's median preprocessing time was **8.340 → 6.975 ms**.
The median of matched candidate/baseline ratios was **0.8211**, an observed
17.9% reduction in preprocessing time; including printing reduced the observed
benefit to 4.5%. The real compiler-source and SQLite observations do **not**
establish a general compiler-throughput improvement.

These measurements used an AMD EPYC 9V74 cloud host and Clang 18.1.3. They are
not qualification on the project's Ryzen 9700X or Clang 23. No laptop, SSH
session, physical benchmark host or self-hosted workflow was used. The draft
requires the target checks below before merge.

## Change and comparison boundary

The frozen source baseline is
`cd6bf0359f36c0b6dc7541dbeff088b70b86d99a`. Advancing the branch to
`1f1e116ad358bed8d5a97824b916908d7cb44e96` incorporated only the new
producer/consumer documentation in `docs/coding-style.md`; compiler and build
sources are unchanged between those baseline commits.

Only two production changes remain:

| Producer / consumer | Work removed | Cost and contract |
| --- | --- | --- |
| `pp_collect_arguments` | A flat 64-kind block uses its existing opener, closer and comma masks directly. A block without parentheses inside a nested argument also skips the six-stage prefix sum. | Existing masked loads, depth/argument limits, separator order and nested-transition fallback remain. The scalar collector is unchanged. |
| `pp_compile_macro` → `pp_substitute` | A backward pass records consecutive literal-token suffixes when the macro recipe is first compiled, replacing a repeated next-item scan on each vector substitution. | `PP_ITEM_TOKEN.parameter` stores a capped 16-bit suffix count; `PPItem` stays eight bytes. The lazy producer cost is timed. There is no extra allocation. Recipe and defining-token indices must both be adjacent; `__VA_OPT__` boundaries and the active optional-content bound remain enforced. Runs beyond 65,536 tokens split into bounded copies. The scalar substitution loop is unchanged. |

The full ownership, lifetime, bounds and failure contract is in
[the preprocessor guide](../../c-preprocessor.md#macros).
Macro compilation remains lazy; this does not introduce work for every unused
definition. No instruction-generation or generated-program optimization is
claimed.

All paired wrappers use
`-O3 -g -march=native -mmwaitx -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-bitwise-instead-of-logical`.
The cloud-only `-mmwaitx` and `-Wno-bitwise-instead-of-logical` accommodations
permit the existing source to build with Clang 18; no MWAITX-dependent
execution was used. The production build, host
policy and runtime CPU dispatch are unchanged. The build-only adapter is
[cloud_clang.c](cloud_clang.c); runtime predefines and oracle calls use the real
Clang binary, never that adapter.

## Method and measured work

[pp_pair_variant.c](pp_pair_variant.c) compiles the existing preprocessor
benchmark twice, with separate static state and explicit `pp_a_` / `pp_b_`
entry points. Each sample constructs fresh intern/preprocessor state and
rebuilds the input/header cache. Input reads, lexing, refinement, directive
preparation, lazy recipe construction, expansion and output-token production
are all inside preprocessing time. Printing to `/dev/null` and release have
separate durations. Option parsing, startup, output-digest checking and parsing
the resulting tokens are outside this stage comparison. The fresh-process
comparison below covers startup and the available syntax driver separately.

Each process ran three warmup pairs, then 31 retained pairs, with A/B order
alternating every pair, pinned to CPU 2. The OS page cache was warm after the
discarded rounds. No LTO or identical-code folding was used. B/B controls use
two independently compiled copies of the baseline, preserving the duplicated
code and static-state layout. They do not just call the same copy twice.

[pp_pair_reduce.c](pp_pair_reduce.c) verifies the complete 69-row structure and
A/B assignments, both output digests, source/output counts and all 18
`PPStatistics` fields before reducing
a file. Its p10, median and p90 select sorted indices 3, 15 and 27 of the 31
samples. A paired ratio divides B by A within each round before sorting;
ratios below one mean lower time in B. Observed p10–p90 ranges are **not
confidence intervals**. Ratios of independent medians can differ from medians
of matched ratios. Both appear in the TSV; neither is silently substituted
for the other.

Hardware performance counters were unavailable (`perf_event_open` denied).
Zero counter fields in preliminary benchmark output mean unavailable, not
zero work. Internal phase TSC values are wall-time estimates, not measured
core cycles. No instruction-count, IPC, energy or Zen 5 power-state claim is
made. The cloud allowed CPUs 0–8 with an eight-CPU quota and 8 GiB memory limit.
The collaborating agents kept builds/tests out of timing windows. Earlier
stage-window process checks found no competing compiler/benchmark process;
the final fresh-process inventory was unavailable, as recorded below. These
controls do not eliminate shared-host scheduling variability.

## Final retained stage results

Milliseconds are independent medians. Parentheses on the paired ratios are
their observed p10–p90. Complete absolute quantiles, release time and phase
estimates are in [final_pp_pair_summary.tsv](final_pp_pair_summary.tsv); every
warmup and sample is preserved in `final_pp_pair_*.csv`.

| Input | Baseline PP, ms | Retained PP, ms | Paired PP ratio (p10–p90) | Paired PP + print ratio (p10–p90) |
| --- | ---: | ---: | ---: | ---: |
| Frozen compiler main | 75.927 | 77.477 | 0.9851 (0.9331–1.1365) | 0.9781 (0.9043–1.1073) |
| SQLite 3.54.1 | 27.849 | 27.627 | 0.9740 (0.9077–1.0713) | 0.9945 (0.9205–1.0678) |
| Flat unused argument | 8.362 | 7.856 | 0.9795 (0.8126–1.0743) | 0.9798 (0.8132–1.1418) |
| Nested unused argument | 7.335 | 7.246 | 0.9771 (0.9191–1.0809) | 0.9798 (0.9019–1.0571) |
| Long literal run | 8.340 | 6.975 | **0.8211 (0.7703–0.9239)** | 0.9545 (0.8885–1.0047) |
| Repeated argument copies | 8.385 | 8.672 | 1.0454 (0.9039–1.1506) | 1.0210 (0.9341–1.1333) |

The literal workload emits 2,113,546 tokens per pass: about 253.4 → 303.0 million
output tokens/s when calculated from the independent median times. PP + print
medians were 30.647 → 29.501 ms, so printing substantially reduces the relative
effect. Its PP medians had p10–p90 ranges 7.990–9.215 and 6.562–7.815 ms.
The flat/nested shortcuts have modest observed differences with broad spread;
the repeated-argument row has a higher candidate median. This evidence does
not establish that every workload improves.

The earlier accepted B/B controls had paired PP ratios of 1.0082 (main),
0.9887 (SQLite), 1.0094 (flat), 1.0064 (nested), 0.9922 (literal) and 0.9430
(argument copies). Their raw samples and complete ranges are in
`captured_pp_pair_bb_*.csv` and
[captured_pp_pair_summary.tsv](captured_pp_pair_summary.tsv). They ran in an
earlier window, so they describe layout/noise sensitivity and are **not a
correction factor** for the final measurements.

Every final pass retained identical output digests, useful-work counts,
history sizes and temporary-origin peaks. Main consumed 1,765,958 input tokens
and emitted 2,155,897; SQLite consumed 1,247,157 and emitted 708,832. Their
history sizes were 18,211,764 and 2,666,660 bytes, and temporary-origin peaks
230,316 and 1,876 bytes. The literal case retained 294,912 history bytes and a
2,076-byte temporary-origin peak. No history records were removed to obtain
these results. Full counts are in
[final_pp_pair_summary.counts.txt](final_pp_pair_summary.counts.txt).
Same-process RSS is a cumulative high-water mark and cannot be attributed to
one variant; no per-variant RSS claim is made from those samples.

## Fresh-process frontend results

The exact final compiler was compared against the frozen baseline in four
fresh-process runs, each with three warmup pairs and 31 measured pairs on
CPU 2, alternating order. All **272 child processes exited successfully**.
These wall times include startup, initialization, preprocessing, printing or
parsing as selected, release and process shutdown. The driver currently
provides preprocessing and syntax parsing; this is not a completed code
generation pipeline or a generated-program benchmark.

| Input / command | Baseline wall, ms | Retained wall, ms | Paired wall ratio (p10–p90) | Median peak RSS, baseline / retained, KiB |
| --- | ---: | ---: | ---: | ---: |
| Compiler main, `-E -P` | 118.200 | 118.251 | 1.0049 (0.9365–1.0999) | 131,364 / 131,416 |
| SQLite, `-E -P` | 39.068 | 39.114 | 0.9990 (0.9037–1.0193) | 72,388 / 72,408 |
| Compiler main, `-fsyntax-only` | 138.774 | 137.806 | 0.9746 (0.8606–1.2014) | 351,432 / 349,932 |
| SQLite, `-fsyntax-only` | 49.437 | 49.550 | 0.9853 (0.9387–1.1202) | 164,908 / 164,880 |

The paired ranges are broad and cross one. The complete frontend comparison
does not establish a general speedup or a memory reduction. Older
fresh-process observations from discarded combinations are not used here.
`final_process_*.csv`, the metadata/capture records and
[final_process_summary.tsv](final_process_summary.tsv) preserve every sample,
absolute wall/CPU-time quantiles, RSS and fault counts. The independent C
[process reducer](process_reduce.c) validates all rounds, assignments and
successful child statuses. Its supplementary CPU-time ratio columns use a
zero sentinel when the baseline CPU time is zero; those ratios are not
interpreted here. All wall-time and RSS denominators are positive.

The team ran no concurrent build/test workload during this window. The
before/after `ps` inventory failed with `fatal library error, lookup self`, so
there is no successful process-inventory evidence for this final window.
Cgroup throttle counters did not increase. These observations cannot exclude
shared-host interference; small differences remain inconclusive.
The recorded snapshots span 2026-10-22 23:38:52.653587805 to
23:41:36.493785198 UTC, including orchestration/persistence gaps; see
[before](final_process_window_before.txt) and
[after](final_process_window_after.txt). Exact invocations are in
[final_process_commands.txt](final_process_commands.txt).

## Experiments rejected

| Experiment | Decision and evidence |
| --- | --- |
| Register-compressed refinement probe | Correctness passed, but isolated cloud comparisons did not establish a useful gain. Restored the original code. The proposed change is preserved in `register-probe.patch`. |
| Reuse intern insertion slot after a miss | Correctness passed, including reserve/rehash handling. Isolated results were mixed; restored the original code. Preserved in `reuse-slot.patch`. |
| Explicit SIMD argument-column copies, also with a 16-token threshold | The seven-variant preliminary screen did not establish an isolated benefit, and the combined argument-heavy workload was slower. Restored original copying. `03-substitution.patch` records the proposal; `screen.tsv` retains every per-process median, including the outlier. |
| SIMD equal-origin history-run scan | The accepted 18-process comparison did not show a benefit beyond B/B scatter. History-only PP ratios were 1.0141 (main), 0.9996 (SQLite), 1.0382 (literal), and 0.9595 (argument copies), where the argument-copy B/B control itself was 0.9430. Restored original history recording. See `history-origin-scan.patch`, `captured_pp_pair_history_*.csv` and the captured summary. |
| Reuse ASCII facts for identifier paste | Correctness and exact-output checks passed, but useful timing benefit was inconclusive and code size grew. Rejected; `paste-ascii.patch` preserves the proposal. |

The paste-only paired PP ratios were 1.0100 (main), 1.0096 (SQLite), 0.9914
(100,000 repeated pastes), and 1.0035 (100,000 unique pastes). The respective
paste-heavy B/B controls were 0.9885 and 1.0402, with broad scatter. The
flat+literal+paste combination also did not justify retaining it. All these
rows and their distributions remain in the final summary and raw CSVs.
`generate_paste_fixture.c` produces the two original C17 inputs; repeated
pastes cycle through 256 names, while the unique input uses 100,000 names.

`captured_pp_pair_current3_*.csv` describes a discarded combination of
flat+literal+history. It is retained to preserve the full earlier control
series and is not presented as the final compiler. The refinement and copy
screens were exploratory, not acceptance evidence. Five missing-metric
preprocessor screen logs were excluded and given explicitly labelled later
replacements; [invalid-replacement-manifest.tsv](invalid-replacement-manifest.tsv)
records them. Two refinement screen invocations also produced no metrics and
were treated as invalid. No missing observation was counted as zero time.

## Generated assembly

The Clang 18 standalone baseline and retained benchmark assembly was inspected
with `objdump -d -Mintel`, with `addr2line` checking the source mappings.
[review.txt](review.txt) and the adjacent `*-excerpt.txt` files record the
addresses and observed lowering. The flat branch skips six dependent
`vpermb` stages, while retaining shared mask/setup cost. Literal selection
loads the 16-bit suffix and clamps the optional bound; the old next-item scan
is absent. The backward producer loop is present inside the timed pass.

The retained functions grew: `pp_compile_macro` by 800 bytes,
`pp_expand_current` by 41 bytes and `pp_substitute` by 128 bytes. The rejected
paste change grew `pp_substitute` by 1,952 bytes and `pp_paste_in_place` by
882 bytes in its isolated build. Removing a source loop therefore did not
mean a smaller total function. These observations establish removed work and
code-size tradeoffs, not target-host instruction savings or performance.

## Correctness and required qualification

The final retained binary passed the default preprocessor suite: 320 existing
fixtures, **522 boundary macro calls plus two long-literal/optional-content
invocations**, history/directive/lifetime/diagnostic/CLI/determinism checks,
and 1,500 fuzz cases in each of C17 and C23, with no scalar/vector difference.
The new cases compare kinds, spellings, flags, locations and complete
expansion history. They exercise lengths 0–129, file/buffer/prescan contexts,
variadic commas, newlines, brace/bracket comma behavior and 65,537-token
literal runs on both sides of `__VA_OPT__`, with present/absent content.

The new generated cases also agreed with Clang 18 when checked on the
intermediate history candidate; their source and the retained production
changes are unchanged. The full external oracle gate remains **pending
Clang 23**. Running the full suite against installed Clang 18 produced
51/320 fixture mismatches, 54/54 sanitizer/target-option mismatches and a
history-growth oracle failure on both the unchanged baseline and the
intermediate candidate. Its external fuzz path explicitly refuses Clang 18.
These are not reported as passing the project's required oracle gate.
Selected failing-oracle observations and full-log identities are preserved in
[clang18-oracle-excerpts.txt](clang18-oracle-excerpts.txt); those examples are
not an exhaustive list of the older oracle's differences.

The exact retained binary also passed:

- **Parser:** 230 snippets, four hand-checked trees, 2,000 random streams,
  14,720 alignment cases, ten 100,000-level nesting cases, 176 CLI driver
  runs, and valid/invalid allocation-failure cleanup checks.
- **Refinement:** 200,000 hash spellings, 88,064 structured spellings,
  14,856 aligned flips, 300,000 intern-table entries, 22 fixtures at 64
  misalignments, 50,570 spelling cases, 20,000 random inputs, 9,600 guarded
  tails, and the 135-file candidate source corpus.
- **Required call graph:** 30 translation units, 15,309 functions, 83,588
  calls, zero unallowed cycles and three existing allowed cycles. The build
  script's inline-cycle capture control also passed.
- **Real-source output:** the retained `-E -P` output is byte-identical to
  the frozen baseline on compiler main (9,177,016 bytes) and SQLite
  (2,340,348 bytes). The baseline's independent Clang 18 comparison was
  identical on both inputs, totaling 2,864,729 output tokens. All 422 frozen
  input/header hashes passed the final manifest check.

[final-preprocessor-excerpts.txt](final-preprocessor-excerpts.txt),
[final-parser.log](final-parser.log), [final-refinement.log](final-refinement.log),
[final-call-graph.log](final-call-graph.log) and
[final-output-and-input-validation.log](final-output-and-input-validation.log)
record commands, exits, binary identity and outcomes; the baseline external
comparison is in [baseline-real-oracle.tsv](baseline-real-oracle.tsv).
The compact check inventory is [validation.tsv](validation.tsv).
The compiler SHA-256 remained unchanged across these checks. An initial
manifest check used the wrong working directory for its relative SQLite
entry; the log preserves that error and the successful check from the
manifest directory, without changing inputs.

Before merge, obtain the repository's Clang 23 validation and approved
9700X measurements, including sustained vector work and work immediately
after a 100–300 ms lighter phase. Also obtain authentic DCO certification:
`BUSTER_DCO_SIGN_OFF` was absent in this session, so no human sign-off may be
fabricated. No target qualification, workflow execution or merge is claimed
by this cloud-only draft.

## Input and executable identities

| Item | SHA-256 |
| --- | --- |
| Retained `c_preprocessor.c` | `cd2ca8c39e6212c60eb6f59b04ff03691c7bf73bfa91034562029658b0db2d92` |
| Baseline full compiler | `a17539b0179ad2b472be80f7f60acccbbadde34adddf0286c971a2e04d95a03c` |
| Retained full compiler | `48e7f97e6fcd963c4396cc496c13a9cdc6338b41294d799cd912ff09eb256dff` |
| Retained paired executable | `7dbe6cee2c96317fb0757da1155c93918e8ba1a6fad4373b40cc39ed7f1b611d` |
| Frozen Clang 18 GNU17 native predefines | `563182aab0488703b6fac3db7dd3c6203c5eef069d43a6d33127a92c672f035f` |
| 422-file input/header manifest | `5371d7561384eb5369c8a14c036531c77a84455c41cb12b26f3c1cd3df02211a` |
| SQLite `sqlite3.c` | `0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52` |

[inputs-and-headers.sha256](inputs-and-headers.sha256) records exact external
headers and frozen repository sources; all 422 were rechecked after the final
stage series. Paths name the original measurement workspace. Map only the
workspace prefix when recreating it; different header contents are a different
input environment. Relocation can change `__FILE__` spellings and output
digests, so both variants must consume the same relocated paths and record
their new identities. Synthetic file contents are reproduced by the included
C generators; [fixture-identities.sha256](fixture-identities.sha256) records
the measured inputs. Main and SQLite use explicit system include paths and the frozen
predefines, with `SOURCE_DATE_EPOCH=1792454400`.

SQLite 3.54.1 was downloaded unchanged from
<https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip>.
Its archive SHA3-256 was verified as
`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`.
SQLite, system/Clang headers and preprocessed outputs are external runtime
inputs, not vendored implementation. Source/build identities for the paste
experiments are in [final_pp_pair_identities.sha256](final_pp_pair_identities.sha256).

## Capture integrity

An earlier file-streaming paired series was invalidated in its entirety when
independent rereads found two incomplete CSVs despite preceding completion
checks. Its cause was not established. The accepted series instead accumulated
all stdout through final process exit, verified sequence/work identities,
wrote the complete captured bytes, and independently hashed the files again
after the suite. The 18-case accepted series passed every capture/readback
hash and the C reducer with no retry or replacement; its raw manifest and
`captured_pp_pair_observations.json` preserve the checks.

The final 16-case tool cell was interrupted after twelve complete CSVs had
been saved, from 23:20:08 to 23:20:56 UTC on 2026-10-22. No completed case was
rerun. Only the four missing cases ran after resumption, from 23:28:31 to
23:28:57 UTC: combined on repeated pastes, then combined/paste-only/B/B on
unique pastes. Any unsaved output from an interrupted invocation is unavailable
and excluded. This is not one uninterrupted 16-process time block; each
retained comparison remains a complete alternating A/B process.

For the first ten files, the delivered pre-write hashes survived and match
the saved bytes. For the next two (B/B and paste-only repeated-paste), the
pre-write hash messages were lost in the interruption: their full rows were
independently C-validated and frozen before resuming, and those hashes stayed
unchanged. The four resumed cases have persisted `.capture.sha256` sidecars
matching their readbacks. All sixteen final files have 69 lines and passed the
C reducer. The final manifest, first-ten capture hashes, before-resume hashes
and resumed sidecars retain these distinct checks without conflating them.

All four final fresh-process captures passed the independent C reducer and
capture/readback hash comparisons. The first capture, main `-E -P`, has tool
stdout/stderr interleaving in its **header only**. Its complete original raw
capture is retained; the known CSV header was extracted/reassembled for the
CSV file. All 68 numeric rows were intact, with no numeric value repaired,
reconstructed or discarded. The other three captures used separate stderr
and their stdout was pure CSV. The per-case `.capture.json`, `.capture.sha256`
and `.metadata.txt` files document that distinction. No completed
fresh-process case was rerun to select another sample.
The portable [final_process_manifest.sha256](final_process_manifest.sha256)
retains the original captured hashes with only their workspace path prefix
removed; the original absolute-path capture/reduction hash files are also
preserved.

## Reproduction

The original absolute build/argv records are in
[pp_pair_commands.txt](pp_pair_commands.txt),
[captured_pp_pair_commands.txt](captured_pp_pair_commands.txt) and
[final_pp_pair_commands.txt](final_pp_pair_commands.txt). The helper sources
here differ from their measured copies only in provenance/comments; those
changes do not alter executable statements. Binary hashes can still differ
when source paths/debug line tables change. Rebuild and record new identities.

For a matching cloud toolchain, set these paths to a clean frozen-baseline
checkout, the retained candidate checkout, this report and an empty output
directory. These examples use Bash solely to invoke the C tools:

```bash
buster_baseline=/path/to/baseline
buster_candidate=/path/to/candidate
buster_report="$buster_candidate/docs/measurements/preprocessor-cloud-2026-10-23"
buster_work=/path/to/measurement
buster_clang=/usr/lib/llvm-18/bin/clang
mkdir -p "$buster_work/fixtures"
cd "$buster_work"
buster_flags=(-O3 -g -march=native -mmwaitx -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-bitwise-instead-of-logical)
"$buster_clang" "${buster_flags[@]}" -I "$buster_baseline/src" -DBUSTER_PAIR_PREFIX=pp_a_ -c "$buster_report/pp_pair_variant.c" -o a.o
"$buster_clang" "${buster_flags[@]}" -I "$buster_candidate/src" -DBUSTER_PAIR_PREFIX=pp_b_ -c "$buster_report/pp_pair_variant.c" -o b.o
"$buster_clang" "${buster_flags[@]}" -I "$buster_baseline/src" -DBUSTER_PAIR_PREFIX=pp_b_ -c "$buster_report/pp_pair_variant.c" -o control.o
"$buster_clang" -O3 -g -march=native -Wall -Wextra -Werror -c "$buster_report/pp_pair_driver.c" -o driver.o
"$buster_clang" driver.o a.o b.o -o pp_pair_flat_literal
"$buster_clang" driver.o a.o control.o -o pp_pair_bb
"$buster_clang" -O2 -Wall -Wextra -Werror "$buster_report/pp_pair_reduce.c" -o pp_pair_reduce
"$buster_clang" -O2 -Wall -Wextra -Werror "$buster_report/generate_fixtures.c" -o generate_fixtures
"$buster_clang" -O2 -Wall -Wextra -Werror "$buster_report/generate_paste_fixture.c" -o generate_paste_fixture
./generate_fixtures "$buster_work/fixtures"
./generate_paste_fixture "$buster_work/fixtures"
```

To rebuild both full compiler executables with the same cloud accommodations
and build the fresh-process runner:

```bash
mkdir -p "$buster_work/cloud-bin"
"$buster_clang" -O2 -Wall -Wextra -Werror "$buster_report/cloud_clang.c" -o "$buster_work/cloud-bin/clang"
(cd "$buster_baseline" && PATH="$buster_work/cloud-bin:$PATH" ./build.sh)
(cd "$buster_candidate" && PATH="$buster_work/cloud-bin:$PATH" ./build.sh)
"$buster_clang" -O2 -Wall -Wextra -Werror "$buster_report/paired_measure.c" -o "$buster_work/paired_measure"
"$buster_clang" -O2 -Wall -Wextra -Werror "$buster_report/process_reduce.c" -o "$buster_work/process_reduce"
```

At runtime, restore `PATH=/usr/lib/llvm-18/bin:$PATH`. The fresh-process runner
accepts two direct argv lists separated by `--`:
`paired_measure --rounds 31 --warmup 3 --cpu 2 -- BASELINE ARGS -- CANDIDATE ARGS`.
Both lists must use the same fixed input paths, predefines and options. The
runner includes `fork`/`exec`/`wait`, uses `CLOCK_MONOTONIC_RAW` and records
Linux `wait4` CPU time, RSS and fault counts. Record stderr separately so its
argv/summary output cannot interleave with stdout CSV in a combined capture.

Recreate native predefines with the real Clang 18 executable using GNU17,
`-march=native`, `-dM -E -x c /dev/null`; verify the recorded hash. Use the
exact argv in the command records for real inputs. For example, the literal
case is:

```bash
SOURCE_DATE_EPOCH=1792454400 PATH=/usr/lib/llvm-18/bin:$PATH \
  ./pp_pair_flat_literal --rounds 31 --warmup 3 --cpu 2 -- \
  -std=c23 "$buster_work/fixtures/literal-runs.c"
```

Preserve complete stdout through successful exit, validate all 69 rows, then
save it; independently check the saved hash. Run both B/B and B/C, preserving
their order and all raw rows rather than selecting favorable samples.
The included CSV evidence can be checked without running the compiler again:

```bash
cd "$buster_report"
sha256sum -c final_pp_pair_manifest.sha256
sha256sum -c captured_pp_pair_manifest.sha256
sha256sum -c final_process_manifest.sha256
"$buster_work/pp_pair_reduce" final_pp_pair_*.csv > /tmp/final-summary.tsv 2> /tmp/final-counts.txt
"$buster_work/pp_pair_reduce" captured_pp_pair_*.csv > /tmp/captured-summary.tsv 2> /tmp/captured-counts.txt
"$buster_work/process_reduce" final_process_*.csv > /tmp/process-summary.tsv 2> /tmp/process-counts.txt
```

`01-flat.patch`, then `02-literal.patch`, reconstruct the retained preprocessor
from the frozen baseline (`git apply -p0`). `03-substitution.patch` is a
subsequent rejected copy experiment. The probe, slot-reuse, history and
paste-only patches apply to a clean frozen baseline; check each patch's path
prefix (`-p0` for `src/...`, default `-p1` for `a/src/...`). The helpers and
fixture generators are original project material. Included existing compiler
and benchmark modules retain their own notices and component licenses.
