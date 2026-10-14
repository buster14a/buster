# Benchmarking and performance audits

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Request decisions and examples

Routine compiler bug fixes and CI work use the post-merge main comparison
by default (#3087). A request to fix a bug, make CI green, resolve conflicts
or refresh a branch does not by itself authorize a PR benchmark request.
Do not mechanically edit `compiler-compare.request` for every compiler PR.
The A/B recipe below describes a planned experiment; it is not an unconditional
per-PR validation step.

Answer three independent questions in the issue/PR handoff:

1. **Correctness CI:** which required correctness and policy checks passed on
   the submitted revision? `Benchmark service workflow policy` validates
   access/routing policy; it is not physical benchmark execution.
2. **Performance claim:** which claim is it (`work-reduction`,
   `native-runtime`, `uarch` or `zen5`), and is there complete evidence of the
   class it requires for the candidate, baseline, workload and configuration?
   A `uarch` or `zen5` claim needs actual Ryzen 7 9700X evidence; the other two
   may complete through the [alternative evidence routes](#alternative-evidence-routes)
   (#4013). Without the required evidence, report that claim as
   **incomplete/unvalidated**. Static analysis, simulated caches and green
   policy checks are never performance evidence.
3. **Task acceptance:** does the owner's request or owning issue explicitly
   require applicable measurements before acceptance or merge? Record that
   reference and the outstanding cells. A real hold remains in force even
   when correctness CI is green; routine post-merge scheduling does not waive it.

An affirmative owner request for a standalone workload, compiler comparison,
scaling experiment or task-specific performance acceptance uses the existing
owner-only path. An already authorized request needs no second unrelated
manual approval. Record the request/acceptance reference, exact candidate and
baseline, selected profile, workload/configuration, request run/attempt,
measurement run/attempt and evidence link in the handoff. The trusted authorizer
continues to bind fresh changes at the exact source head relative to every
parent, owner identity, repository and trusted-main execution; see
[admission](../../benchmarks/9700x/ADMISSION.md#scheduling-policy).

Before creating another request, read the exact-head check and its matching
workflow run/attempt and published receipt. Reuse complete evidence for its
original source/binary, baseline, workload, profile and configuration identities.
An old comment saying "queued" does not override a completed check. Missing,
failed, cancelled or superseded evidence remains unavailable; request a retry
only within an authorized experiment, retaining the earlier attempt.
A changed head or input cannot borrow the earlier result. Record the new
performance claim as unvalidated; create a fresh request only when the owner
experiment or explicit acceptance requirement applies to that new candidate.
Deliberately requested new inputs or experiments retain distinct identities.

| Scenario | Request action and handoff |
| --- | --- |
| Ordinary compiler bug fix | Run applicable correctness CI; leave the request file unchanged. Routine measurement follows the landed main commit. Any performance claim remains unvalidated until relevant evidence exists. |
| CI-only edit | Validate the changed CI/policy behavior on hosted infrastructure. It does not manufacture a physical benchmark request. |
| Branch refresh or conflict repair | Validate the new source revision. Refresh alone is not an explicit experiment; do not append a request line just because the head changed. |
| Owner-requested standalone workload | Add/change the requested C workload or data at the exact head through the existing owner gate; retain the request reference and workload/data identity with its report. |
| Explicit compiler comparison | Add a fresh request line for the authorized candidate experiment; retain its exact head/base, profile, run/attempt and published evidence. |
| Issue-specific pre-merge performance hold | Keep the issue's hold until all required relevant 9700X cells are complete. A report-only workflow verdict and green correctness CI do not release that hold. |
| Already measured exact head | Read the completed check, matching attempt and receipt; reuse that evidence for those same identities. Stale "queued" prose is not a rerun request. |
| Changed head | Keep earlier results attached to the earlier identities. The new claim is unvalidated; request new evidence when the experiment or hold applies, otherwise use the routine post-merge default. |

These examples govern whether an agent creates a request. A source-path
classifier cannot infer human intent. Existing event/admission fixtures prove
machine-verifiable freshness, provenance and routing, including rejection of
implicit PR/merge-group host execution; they do not prove arbitrary future intent.
Documentation-only corrections need documentation/link and policy checks,
with no physical experiment. Preserve required protected checks, existing
request history and other issues' acceptance holds.

## Benchmarking and diagnostics

Record source debug flags explicitly in benchmark recipes and comparisons:
use `ide cc -g0 ...` against a reference compiler with debug output disabled,
or `ide cc -g ...` against a reference using the corresponding debug mode.
Do not infer source debug output from the compiler executable's Release/Debug
build configuration. `ide cc` defaults to no source debug information; older
revisions emitted it by default, so omission across revisions can change the
workload. The self-host fixed-point and stage-1 recipes deliberately pass `-g`;
retain that flag when comparing their historical results.

Performance comparisons must control build provenance as well as runtime noise.
Use the [session-owned worktree setup](workflow.md#parallel-sessions-on-one-machine)
for builds and measurements; it defines `session_root` for the ordinary
profiling and A/B recipes below.
Keep each session's build tree, frozen binaries and captures in that workspace,
and use a new output directory for each attempt. Build compared revisions
serially in the same configured path, freezing each trusted binary before
changing sources. If separate roots are necessary, verify
path normalization and run same-source cross-build controls; identical compiler
flags alone are insufficient. Record source and binary hashes, complete compile
commands, and investigate code/section placement when small effects change across
builds. An A/A test of one immutable binary measures runtime noise, not build-root
sensitivity. Matching paths does not eliminate source-induced layout sensitivity;
keep conclusions scoped to the measured binaries and workloads. See the
[matched-build #791 audit](../performance-audits/2026-09-20T050606Z.md).
For dedicated 9700X calibration, retain a same-root rebuild control and a
cross-root control beside the immutable-binary A/A capture; the
[dedicated-host guide](../../tools/throughput/DEDICATED.md#same-source-cross-build-controls)
describes them. No repository tool currently produces or analyzes these
captures (#2741).

- **`./build.sh bench_throughput`** provides deterministic startup, scaling,
  symbol, CFG, backend and frozen-source self-host workloads with raw paired
  timing/RSS, separate PMU/allocation probes and a conservative CI guard.
  See [`tools/throughput/README.md`](../../tools/throughput/README.md) for the
  experiment contract, reproducible commands, statistical assumptions and limits.
  Recovered macro-expansion and aggregate-ABI inputs are opt-in via repeatable
  `--workload macros --workload aggregate-abi`; custom sets require `--no-guard`
  and preserve the default CI corpus. Their counts/hashes and full job-capacity
  cross product are covered by the native harness tests.
  `scale` measures native multi-TU compile-and-link with `-fcompile-jobs=W` on
  the first W physical cores of an explicit, permitted `--cpu-set`, and reports
  speedup beside CPU-work and memory inflation without a gate; see its
  [README section](../../tools/throughput/README.md#multi-tu-scaling-scale).
  An owner pull request that changes `benchmarks/9700x/scaling.request` runs
  it on the 9700X inside its compiler comparison.
  `check-workload` provides a separate, non-timing preflight for the pinned
  cJSON 1.7.19, Lua 5.4.8 and SQLite 3.53.4 descriptors: it hashes the complete
  staged tree plus compiler and oracle evidence, but always reports
  `admitted=false` and requires fresh admission. The branch-only real-source
  workflow may additionally emit a hosted functional receipt for each fresh
  passing oracle after parsed closure verification, separate object and
  compile-link operations, and an exact runtime transcript. That receipt covers
  only its one direct-SSA/non-PIC/fast cell; it is not timing, dedicated-host,
  A/A or A/B performance admission. Timed real-source runs use
  `run --real-source DESCRIPTOR ROOT`, and their Zen 5 route is the fixed
  `throughput-real-source-v1` profile on `compare-pull` (below).
  This does not replace the canonical self-host/correctness gates below.
  The same optional allocation observer can emit a [per-site census](../allocation-census.md)
  with separate zeroing, alignment and OS request totals for offline analysis.
- **`tools/uarch_lab.py`** is the micro-architecture lab: one command that
  says *where* (symbol, line, instruction), *when* (compiler phase, time in the
  run), *what* and *how many* for time, instructions/IPC, branch misses,
  L1I/L1D/TLB misses, top-down metric groups and page faults (with the faulting
  data address and its mapping) over the stage-1 self-host compile, pinned to
  one CPU. Its steps are `env`, `timed` (N `perf stat` runs, byte-compared
  outputs, min/median/MAD, drift, IPC, effective clock, ns/byte and
  instructions/token from `-fsource-metrics`), `topdown` (a cheap
  `perf stat -M G -- true` dry run, then `perf stat -r 3 -j -M` per discovered
  metric group, one group per invocation), `timeline`
  (`perf stat -I 20` intervals split across phases, CSV and SVG/HTML chart),
  `sampling` (`perf record --call-graph fp` per event, every page fault,
  annotate and srcline listings), `ibs` (`--sudo` only: IBS op/fetch and
  `perf mem` on the pinned CPU, the compiler itself run unprivileged) and
  `micro` (`ide bench`, with the predictor-learning caveat). Every raw file stays
  in the output directory, failed or unsupported counters render as NA, and
  `report DIR` re-renders `report.md`, whose first section lists the hottest
  functions per event, top fault sites, the dominant top-down category and the
  slowest phase as pointers to the raw files:

  ```sh
  python3 tools/uarch_lab.py run --ide "$session_root/src/build/Release/ide" --repo-root "$session_root/src" \
      --cpu 2 --output "$session_root/lab-attempt-1" [--target-minutes 15 | --runs N] [--sudo] [--skip STEP...] \
      [--no-fresh-copy]
  python3 tools/uarch_lab.py report "$session_root/lab-attempt-1" [--perf PATH]
  python3 -B tools/uarch_lab_test.py
  ```

  The offline tests run every fake `perf`/`ide` script under the interpreter
  that runs them (its path becomes the shebang), so a standalone Python works
  even when `PATH` selects another. The retirement fixtures run a lone copy of
  that interpreter as a stage-1 compiler; when the copy cannot find its
  standard library they set `PYTHONHOME` (and the shared-library directory),
  and skip with the probe's errors only if it still cannot start.

  Its `compare` mode is the A/B benchmark for a compiler change; see
  [Benchmarking a compiler change (A/B)](#benchmarking-a-compiler-change-ab).
  Both modes also write `summary.json`, the machine-readable result.

  Contracts learned on the first Zen 5 run (perf 7.2.4):
  - Wall time is the harness's monotonic span around each `perf stat` child
    (`timed/runs.json`, or `commands.log` for older directories), reported
    beside the compiler's `wall_ns` and task-clock. perf 7.2.4 reads
    `duration_time` as 0 whenever it shares the event list, so the lab does
    not count it, and a metric dividing by it renders NA, never 0.
  - A step is `ok` only when its section has real data. `report` re-assesses
    every section from the raw files; a missing or non-positive time (quoted
    with its raw CSV line), a report with no rows from a capture that has
    samples or a failed group makes it `degraded`. Rendering divisions go
    through `ratio()` and each line degrades on its own.
  - Without `--runs`, the timed count is fixed once after three pilot runs so
    the whole lab lands near `--target-minutes` (default 15); the report states
    the chosen count and why. It never adapts to measured results.
  - Groups whose uncore PMU is absent (Zen 5 desktops expose no `amd_umc` or
    `amd_l3` PMU; `memory_controller`, `l3_cache`) are `unavailable`, not
    failures. Uncore groups count system-wide, not per process.
  - `perf stat -x,` prints metrics with display precision (often one decimal).
    The lab uses `-j` (six decimals) with `-x,` as a fallback, marks rounded
    values ("rounded to 0.1", a printed 0.0 as "< 0.05"), and recomputes the
    branch misprediction rate and branch MPKI from the named event pair. A
    per-1k-instruction metric is recomputed only from a run that measured it
    alone, as the sum of its non-instruction events: perf attaches the metric
    to whichever event prints first, and many Zen 5 metrics sum several events.
  - Page-fault regions are resolved against the mapping live at fault time;
    perf records no munmap, so a file mapping later replaced at the same
    address is grouped as a transient file mapping (preprocessor sources).
  - A multiplexed group (counters running less than 100% of the time) is
    re-measured one `-M <metric>` per run; the non-multiplexed values lead and
    the group value is a flagged fallback.
  - IBS and `perf mem` record the pinned CPU system-wide. The compiler renames
    its main thread `main_thread`, so the reports keep the thread ids with
    samples in the workload binary (`perf report --sort pid,dso`, then
    `--tid ... --comms ...`), not the exec name. `report DIR` derives these
    filtered reports from an older directory's raw `ibs/*.data` when perf is
    available. `perf mem` shares are latency-weighted, so the report shows
    each load source's sample count beside its share.
  - Sampling self reports sort by `dso,symbol`, so an unresolved address reads
    `ide 0x18f5ce` rather than an anonymous hex value.

  The phase breakdown needs a binary that accepts `-fmetrics-out=` and writes
  a measured `CC_METRICS_INPUT` record; the
  lab probes for it and otherwise reports the phase sections as NA. Phase
  boundaries come from the compiler's own clock, which starts after argument
  parsing, so the report states how much of the run lies outside it.
- Native-backend retirement has a separate maintainer-approved
  [performance contract](../native-retirement-performance-contract.md). Its
  [October 3 decision](https://github.com/buster14a/buster/issues/36#issuecomment-5969534074)
  moves #512 acceptance to the [direct lab gate below](#native-retirement-gate-512),
  with four allocator-mode cells and one generated-runtime cell. The following
  service binding/replay instructions retain the historical service contract.
  tighter budgets, immutable #508 binding, dedicated-host admission and
  simultaneous uncertainty rules apply only to the #36/#512 acceptance run;
  they do not silently replace the native harness's ordinary CI guard. Before
  any timing verdict, validate the complete immutable binding record with
  `python3 tools/native_retirement_performance_binding.py <record.json>
  --evidence-root <bundle> --repository-root <immutable-checkout>
  --trusted-execution-receipt-sha256 <independently-obtained-sha256>`; the
  evidence-root form joins the exact #508 declaration, manifest, inputs,
  dependencies, environment, rows, independent validator report and
  versioned canonical performance-row artifact. It recomputes unique row
  identities, eligibility and the round-1/round-2/pooled statistical family,
  then checks structured #437 service/profile/A-A/lease receipts and #510
  publication/download/replay receipts. `--repository-root` is an immutable
  checkout used to verify commit-to-tree and source/build/harness identities;
  without it the result is explicitly downgraded and cannot prove those
  relations. The trusted execution-receipt digest must come out of band
  from the authenticated admitted control service for the exact job and attempt;
  never derive it from the result bundle being validated. This validation does
  not itself accept a performance result.
  A run without the evidence root is reported as `proof=structural-only`.
  The binding streams schema-2 result inputs through the existing #615 verifier
  with predeclared 16,777,216-record partitions, then replays #619 through the
  native `tools/throughput/retirement_stats.h` adapter (`bench_throughput
  retirement-replay`). One scope-free family member is one C call emitting
  both rounds and pooled bounds; structural workflow completion is not a
  verdict.
  The acceptance service must use the server-authoritative supervisor lease
  protocol; the existing cooperative throughput lock is not a substitute.

- **`test_self_host` is the most trustworthy and complete compiler benchmark.**
  It exercises the full self-hosting IDE pipeline, including the trusted
  bootstrap, two complete unity-build IDE compilations, byte-identical stage
  verification, and the stage-2 benchmark. Use its end-to-end result as the
  primary completeness/fixed-point signal for compiler-throughput changes.
  For performance measurements, run the benchmark with the trusted
  Clang-compiled `ide` executable, whose generated host code has the best
  quality; self-compiled stage executables are useful for fixed-point
  validation, not as the default benchmark host.
- `ide bench` (also exposed through the `bench_all` target) reads
  `tests/basic_c_operations.c` and measures the complete C frontend path:
  preprocessing, syntax construction, semantic analysis, and canonical-IR
  lowering. It prints one `BENCH_C_FRONTEND` line with minimum and median
  latency plus source throughput. Run it from the repository root.
- **`STEP_INSTRUCTIONS` lines** report instructions retired for every build step
  that already reports wall time, printed straight after its `took ... seconds`
  line. The one to watch is `Self-host stage 1`, which is compile throughput on
  the complete unity translation unit: it is contention-immune and reproducible
  to a few thousand instructions, where wall time on an idle 16-thread desktop
  varies about 10% run to run. Compare it across commits — a change that only
  *adds* source can regress it sharply through a latent quadratic, which is
  invisible to test-time measurement. Linux-only: this needs hardware counters
  and there is no cheap portable equivalent, so nothing is printed elsewhere or
  when `perf_event_open` is unavailable, and its absence is never an error. One
  counter covers the process tree, so a step's number is exact only while it is
  the sole child running; that holds for the sequential self-host stages and
  CMake generation, but concurrent matrix steps overlap and must not be read as
  per-step totals.
- **The `SOURCE` table** gives `STEP_INSTRUCTIONS` a denominator: it reports
  what the C frontend read, in bytes, physical lines, sLOC, comments, blank
  lines, literals, and preprocessing tokens, each with its share of the whole.
  `ide cc -v` prints it, and the self-host stages pass `-v` so it lands in the
  log beside their instruction counts. The `unique` column covers the distinct
  files of the include closure, the `lexed` column every inclusion: a header
  with neither `#pragma once` nor a whole-file `#ifndef` include guard the
  preprocessor recognizes (see `CIncludeGuardTable` in `c_source.c`) is
  re-read and re-lexed at each `#include`, so the two columns' ratio is the
  unit's include amplification (currently 295 files/27,2 MB against
  326/27,3 MB; before the guard optimization the same tree read 448/29,4 MB).
  A `files lexed more than once` block under the table attributes the
  remainder per path — deliberately re-includable files like the builtin
  `stddef.h` and glibc's `bits/wordsize.h`, whose re-reads total 28 KB. `bytes on disk`/`physical lines` measure
  the files as written, `bytes scanned`/`lines scanned` what the lexer saw
  after carriage returns and line splices are folded out, and two partitions
  hold exactly: bytes are code plus comment plus whitespace, and lines are
  code plus comment plus blank once the `both` row is subtracted. A closing
  `preprocessed output` block measures the other side — the tokens actually
  handed to the parser, their spelling bytes, every spelling byte the run
  retained, macro expansions, and `#define` directives — because macro
  expansion multiplies the input by whatever factor the macros ask for, so
  parsing and lowering scale with those and not with the file sizes. The
  measurement is always on and costs 0.19% of stage-1 instructions (measured
  on Clang 22/Linux x86-64), because every source unit falls out of a branch
  the lexer already takes rather than a second pass over the bytes, and the
  output side is one linear sum over the finished token stream. That sum
  feeds only the spelling-bytes figure, so the `cc` command skips it when it
  prints neither this table nor `-fsource-metrics`
  (`CompilerDriverInvocation.omit_spelled_bytes`); API callers get it by
  default. The
  bootstrap's table does not match the self-hosted stages' and is not meant
  to: the bootstrap finds its host compiler's resource headers and the
  self-hosted stages fall back to the builtin ones, which is why the file
  table is populated lazily (see `map_entry` in `c_source.c`).
- **The `SELF_HOST stage <1|2> throughput` block** is the division done: its
  `workload` row reports bytes, LOC, SLOC and tokens; its `bandwidth` row
  reports MB/s, LOC/s and SLOC/s from the execution time shown immediately
  above it; and, where the platform exposes a hardware counter, its
  `instructions` row reports the total and the per-byte, per-SLOC and per-token
  ratios. These are the numbers to trend across commits, printed
  by `self_host_compare_action` beside the `SELF_HOST fixed_point` line. `-v`
  prints the `SOURCE` table for a human;
  `ide cc -fsource-metrics=<path>` writes the same measurement as
  `<group>.<field>=<value>` lines for a program, each self-host stage writes
  one beside its executable, and build.c combines the relevant fields with
  the stage's own `STEP_INSTRUCTIONS` delta and wall time. A file rather than
  another stdout line, because capturing a compile step's output would stop its
  diagnostics from streaming as it runs. `bytes` is `lexed.translated_bytes`,
  `loc` is `lexed.translated_lines`, and `sloc` is `lexed.code_lines`; all
  three legitimately differ between the two stages for the resource-header
  reason above. The LOC/s and SLOC/s values divide those line counts by the
  stage's recorded wall time; MB/s uses decimal megabytes
  (1,000,000 bytes). `tokens` is `preprocessed.tokens`, which does not differ,
  and is the denominator to reach for when comparing the stages to each other.
  The ratios carry three
  decimals because a compile costs a few hundred instructions per byte, and
  whole units would quantize the series to about 0.3% — coarser than the
  regressions the counter exists to catch. Without a usable instruction
  counter the line still prints its units and omits the ratios. Only ever add
  keys to the metrics file: readers take the fields they know and must keep
  working against a newer compiler's file.
  The file also always carries the `c_type_layout.*` work counts from the
  `-v` `C_TYPE_LAYOUT` line; build.c gates stage 1's `solves` and
  `pass_state_types` against fixed ceilings (see
  [self-host work ceilings](build.md#self-host-work-ceilings)).
- **The stages' preprocessed token streams are a fixed point too**, gated
  beside the executable bytes. `test_self_host` fails when the two stages
  disagree on `preprocessed.tokens` or `preprocessed.bytes`, and when the
  executables differ it first says whether the token streams matched — which
  halves the search, since the parser and everything after it are a function
  of that stream alone. Nothing else is compared between the stages: bytes,
  sLOC, comments, `#define` directives and macro expansions all legitimately
  differ for the resource-header reason above (currently 6.088 vs 6.087
  `#define`s and 56.429 vs 56.426 expansions converging on the same 1.378.839
  tokens and 12.124.867 spelling bytes). Changing what the builtin resource
  headers declare can part the streams on purpose; that is the signal and not
  a false alarm, because the two stages are then no longer compiling the same
  program and byte-identical executables would be saying less than they
  appear to. Update the gate deliberately in that case — do not weaken it to
  the token count alone, since equal counts of differently spelled tokens are
  still different programs.
- **`ninja_log_summary <build-dir> [--limit N]`** and **`time_trace_summary
  <json-path>... [--limit N]`** (both build-driver commands, same
  shape as `cmake_profile_summary` — see `build.c`) are diagnostics for
  *where compile time goes*: the former reads `<build-dir>/.ninja_log`
  directly (only useful for multi-TU/Debug builds — Release is a single
  unity TU); the latter parses one or more clang `-ftime-trace` JSON files
  (enable via `--time-trace`) and reports the slowest `"Total *"` rollups
  clang itself pre-aggregates (`Total Frontend`, `Total Backend`,
  `Total InstantiateFunction`, ...), summed across every file given. These
  commands are diagnostics for humans to inspect when aggregate numbers
  suggest a regression, and can be run from CI or locally.
- **`test_timing_summary <test-log>... [--limit N] [--baseline <path>]
  [--update-baseline]`** answers *where test time goes*, the counterpart to
  the two commands above. It parses the `TEST_MODULE_TIMING` lines
  `library_tests()` already prints under `--verbose=1` (which every CI job
  passes) out of a saved matrix run or CI log, reports each module's total
  across the configurations in that run with its share of the whole, then the
  slowest individual (configuration, module) rows with their deltas against a
  stored baseline. `--update-baseline` records the current run.
  This exists because test cost accumulates silently: `x86_64_metadata_tests`
  reached 60% of all CI test CPU time before anyone noticed, and every number
  needed to catch it in week one was already printed on every run and
  discarded with the log.
  Two properties are deliberate and should be preserved. **It is a diagnostic,
  not a gate** — it has no thresholds and no CI failure condition, because
  wall-clock test time is far too noisy to gate on (identical code measured
  290.1 s and 319.8 s for the same matrix on an idle 16-thread desktop, a 10%
  spread); recording history first is the right order, and a gate would need a
  noise model that does not exist yet. **Series are per-runner and
  per-configuration and are never merged** — the same module measured 4.4 s in
  Linux Release, 138 s in sanitized Debug and 192 s on a sanitized Debug
  Windows runner, so a baseline records the runner it was taken on and refuses
  to be compared against another, every runner keys its own baseline (none is
  excluded, including shared or noisy ones), and rows are only compared within
  the same configuration. The runner name defaults to `<platform>-<arch>` and
  is overridable with `BUSTER_TEST_TIMING_RUNNER`; the default baseline path is
  `build/test-timing-baseline-<runner>.txt`. Configurations are recovered from
  the build commands the superbuild echoes ahead of each test block; timing
  rows with no command line in front of them are attributed to numbered
  `unknown:<n>` series rather than merged.
- **`tools/ci_time.py`** answers *what CI actually costs*, over a window rather
  than for one push. It reads the Forgejo Actions API and prints runner hours
  per runner (or per day, week, or branch) beside push latency — the two
  numbers that matter separately, since machine time is what the matrix burns
  and latency is what a push waits for:

  ```sh
  tools/ci_time.py --days 30
  tools/ci_time.py --days 30 --by week
  ```

  The 30 days to 2026-08-28 measured **403 runner-hours over 5,877 jobs** —
  `x86_64-windows-znver5` 131.0 h (median 5.7 min/job), `aarch64-macos-mini`
  113.9 h, `x86_64-linux` 102.1 h, `x86_64-linux-dedicated` 52.7 h, the gate
  3.3 h — and push latency over 1,435 runs of median 6.1 min, mean 8.1, p90
  17.0. Authentication reuses the git credential for the forge, so there is
  nothing to set up.

  The arithmetic is trivial and every trap is in the data, which is why this is
  a script and not a one-liner. **`task.updated_at` is not the end of the
  job**: Forgejo's log-retention sweep re-touches the row long afterwards in
  batches that share one end second (a whole day stamped `23:45:3x`), which
  makes a raw sum overstate a month by ~30x — a five-minute Windows job
  appearing to run for 33 hours. Two physical ceilings repair it: a job cannot
  outlive its run (clamp to the run's `stopped`, joined on `run_number ==
  index_in_repo`) and cannot outlive `ci.yml`'s `timeout-minutes`, which is why
  exact 7201-second rows are real timeout kills; the ~0.3% that survive both
  are imputed from the runner's median and counted in an `imputed` column
  rather than hidden. `actions/tasks` is the only per-job timing source —
  `actions/runs/{id}/jobs` returns `started_at` and `completed_at` as null
  here. And a paged walk must test the **head** of each id-descending page
  against the cutoff, never the tail: runs that never dispatched carry
  `0001-01-01`, which ends the walk hundreds of runs early and silently
  inflates every job whose run can no longer be found. `--self-test` covers the
  repair without touching the network.

  `limit` caps at 50 and the server ignores `Accept-Encoding: gzip`, so a cold
  month is 154 requests and 23 MB (~18 s); terminal rows are cached under
  `build/ci-time-cache.json`, which takes a repeat window to 2 requests and
  ~1 s. Do not raise `--jobs` past its default of 8 to go faster — 16 measured
  slower, the forge being the bottleneck while it is also serving runners.
- **`tools/cache_miss_survey.py`** answers *where the cache misses are*, the
  data-side counterpart to `tools/branch_miss_survey.py` below. It runs the
  stage-1 workload under `perf stat` for the hierarchy budget (L1d accesses
  and misses, how many fills L2 answers, the demand fills that reach DRAM,
  both TLBs, page faults), then takes one `perf record` per event and ranks
  symbols by miss share *beside their cycles share*, because the ratio is what
  separates a symbol that misses because it is big from one that is waiting on
  memory. Top symbols are broken down by source line through
  `llvm-symbolizer`, and `--dwarf` adds a sparser DWARF-unwound capture that
  names the exact `memcpy`/`memset` call sites — worth it here, where a fifth
  of every miss event has its leaf in libc. Three of its rules are the same
  ones this section states for `perf`, and the script encodes them so they do
  not have to be remembered: symbolize `sym+offset` rather than `-F srcline`,
  keep `--no-inline` on `perf script` (inline expansion costs 170 s against
  under a second here) and recover inline chains from `llvm-symbolizer`
  instead, and read a line table as a loop rather than an instruction, since
  AMD's precise sampling facility (IBS) is system-wide only and the script
  stays unprivileged. It also documents the trap that frame-pointer unwinding
  *skips* the caller of a `memcpy`, so an fp callchain names the call site one
  level out. Run it from the repository root on an idle machine after
  `./build.sh build --config Release -t ide`; a full `--dwarf` pass takes
  about 25 seconds and writes its captures and report under
  `build/cache-miss-survey/`.
- **The local Release tree carries profiling information.** `BUSTER_DEBUG_INFO`
  and `BUSTER_FRAME_POINTERS` are both on by default outside `--ci`, so a
  Clang Release `ide` symbolizes to source lines and unwinds through
  `--call-graph fp`, the same cheap unwinding the sanitized Debug tree allows.
  Superluminal reads it directly. For a trusted performance binary, configure
  the idle session worktree explicitly with tests disabled:

  ```sh
  ./build.sh generate --cc clang --no-include-tests
  ./build.sh build --config Release -t ide
  grep -Fx 'BUSTER_INCLUDE_TESTS:BOOL=OFF' build/CMakeCache.txt
  perf record -F 999 -g --call-graph fp -o "$session_root/release.data" -- ./build/Release/ide bench
  ```

  For test-generated image lifetime during captures, see
  [profiling image retention](testing.md#profiling-image-retention).

  The ordinary local default includes tests, which enables
  `BUSTER_IR_TRANSFORM_CHECKS` in Release. Keep tests-on, sanitized,
  instrumented and explicit transform-verification builds identified as
  separate configurations. Retain the source revision, binary hash, cache and
  compile commands when freezing a binary, as the A/B recipe below does.
  The workload's `-DBUSTER_INCLUDE_TESTS=0` controls the compiler produced by
  self-compilation; verify the host compiler's setting in its saved cache.
  The lab currently records the binary hash without reading that cache, so
  retain the cache beside the capture and do not infer its setting from
  `summary.json` or `report.md`. Performance builds complement the separate
  tests-on correctness and self-host validation.

  The `perf script`/`llvm-symbolizer` rules below apply unchanged; the Release
  binary is a clang PIE like the Debug one. Pass
  `-DBUSTER_FRAME_POINTERS=OFF` when the point of the measurement is the
  frame-pointer cost itself, or when reproducing a CI Release number exactly.
- **`tools/remote_superluminal_capture.py` records the same stage-1 workload on
  a remote Linux Zen 5 host.** It requires a clean remote checkout, verifies
  that the source identity stays fixed across the Release build, deploys a
  Superluminal command-line redistributable only when the host lacks one, and
  records/resolves/exports the canonical unity self-compile. The portable
  `.slp`, raw `.linux` capture, source metrics, hashes, and machine/source/tool
  provenance are downloaded together. The capture process needs passwordless
  permission for the narrow `sudo` profiler invocation; profiler installation
  and privilege policy remain operator responsibilities.

  ```sh
  tools/remote_superluminal_capture.py --host david@benchpress
  tools/remote_superluminal_capture.py --self-test
  ```

  This is a sampling diagnostic, not a replacement for the paired native
  throughput harness or its dedicated-host qualification. It refuses a
  non-Zen-5 host by default; `--allow-non-zen5` is an explicit diagnostic-only
  escape for method testing. `--skip-build` reuses the existing Release binary
  and therefore belongs only in a controlled workflow that already established
  that binary's provenance.
- **Zen 5 and microarchitecture claims need Zen 5 execution evidence (#2761,
  narrowed by #4013).** A `uarch` or `zen5` claim requires actual execution of
  its relevant workload on the approved Zen 5 benchmark host. Without complete
  evidence matching the candidate, workload and configuration, report that
  claim as incomplete. Nothing measured elsewhere substitutes for it: hosted
  timing, instrumented counts, static instruction counts, a `znver5` target, a
  request or policy check, or an unrelated self-host benchmark. Work-reduction
  and native-runtime claims use the
  [alternative evidence routes](#alternative-evidence-routes) instead. Correctness and
  native-platform CI stay on their current infrastructure. The comparison
  routes below cover the stage-1 self-host compile and, as profile
  `throughput-corpus-v2`, the default `bench_throughput` corpus under both
  retained FAST and QUALITY modes on the same
  two binaries. A base revision whose history lacks
  `compiler_receipt.THROUGHPUT_HARNESS_FLOOR` still runs the retired none and
  mir-stack modes under `--mode all`. On the pull route hosted authorization
  turns it away before dispatch with an explicit unsupported check, and the
  non-owned host routes re-derive that refusal before any build or lab
  (#3714). Only the named `throughput-corpus-historical-v1` recipe measures
  such a base: the pinned `afe67cbe` harness on the v2 arguments and
  population, discharging its own inventory row. The publisher re-checks the corpus's own summary and metadata
  and binds its compiler hashes to the measured binaries. A corpus exit 1
  (confirmed regression) with a complete exported report is a measured,
  report-only result on both the main and pull-request routes, so a regressing
  main commit is reported rather than left unmeasured. Exit 1 needs positive
  counted confirmed regressions; any other status, a missing report or a field
  inconsistent with the producer's per-round decisions fails the receipt
  (`compiler_receipt.classify_throughput_exit`, #3212). Every entry point
  that can claim performance validation has a row in
  [`docs/performance-validation-v1.json`](../performance-validation-v1.json),
  either a 9700X route with the consumer that checks its evidence, or an
  explicit `NOT VALIDATED` with a resolution and a `claim_route`: the existing
  9700X route a claim about it must use instead, or `none` for a visible gap
  with no Zen 5 route yet. Native macOS and Windows timing is the one
  exception: it is `native-informative` on route `hosted-native`, counted
  separately, never covered and never Zen 5 evidence (see
  [Native hosted timing](#native-hosted-timing)). A tool that includes `<buster/...>` headers cannot
  claim the direct-workload route, because each workload is compiled alone
  against system headers. `tools/bench_direct/performance_inventory_test.py`
  (part of the required benchmark policy check) discovers entry points from
  `build.c`, `uarch_lab.py`, `ide`, `tools/` and the workflows, and fails on an
  unregistered or stale one. Each published 9700X compiler receipt must name
  the observed CPU, the Ryzen 7 9700X; a runner label, target flag or other
  host is refused. The `compare` and `compare-pull` jobs also keep their #2758
  startup record (`machine-specifications/report.json`, through the pinned
  action's `mode: retain`) in the evidence artifact they already upload, and
  `compiler_publish.machine_record` fails publication when that record is
  missing or malformed, when a required fact (schema, run, attempt, job, CPU
  model, architecture) is not `available`, when it names another CPU or
  architecture than the 9700X's x86-64, when its model differs from the
  receipt's observed CPU, or when its run, attempt or job is not the attempt
  being published. Sealed raw evidence for this route (#2918) follows
  [compiler-evidence-v1.md](../compiler-evidence-v1.md): an ordinary attempt
  seals its lab and corpus last, and the publisher verifies the seal natively.
  A pull `snapshot-v1` attempt also seals its closure records and owned-phase
  population.
  An artifact without a seal is `legacy-unsealed`, and its raw bytes are
  reported unverified.
  Optional facts keep the collector's own `unknown` or
  `unavailable` status in the published check and are never filled in. Only
  publication-only recovery may lack it, and only when the bench workflow at
  the original run's authenticated revision has no record retention step
  (`machine_record_required`); an unreadable workflow still requires it. The record is collected by the same job, so it corroborates the
  receipt rather than proving the host independently. The direct workload
  harness, which uploads no artifact, reads the same observed CPU
  model, prints it in its report, and on any other host compiles and runs
  nothing and fails. The inventory test also refuses a `covered` row whose
  evidence consumer never checks the observed host against `APPROVED_HOST`.
  Each published `9700X compiler benchmark` check, for both main and pull
  requests, names the inventory rows that its attempt discharged
  (`compiler_receipt.covered_inventory_rows`, in the check summary, in its
  JSON as `inventory_coverage`, and in the main commit report). The mapping
  `compiler_receipt.INVENTORY_LEGS` ties each receipt leg to its rows. On
  main, the self-host lab discharges `uarch_lab:compare@main` and the corpus
  discharges `build:bench_throughput@main`. On a pull request, the lab
  discharges `uarch_lab:compare`, and the corpus and scaling discharge
  `build:bench_throughput`. Each opt-in profile discharges its own
  `PROFILE_ROWS` row (`compiler-probes-v1` discharges both of its probe rows),
  and the analyzer discharges its two analyzer rows.
  A leg discharges its rows only when the attempt concluded `success`. Every
  other leg is listed as not discharged, with the reason: it was not
  requested, left no record, failed, was pending or skipped, was superseded,
  or the publisher refused its evidence. A row named by several legs is
  discharged only when every requested leg discharged. Rows the check does
  not name still need their own evidence. This is report-only and changes no
  conclusion. The inventory test fails when a `covered` `main-compare` or
  `pull-compare` row can't be reached from any leg of its mode, or when a leg
  names a row that isn't covered on that route. So renaming or deleting a row
  or a leg cannot silently drop coverage, and a `native-informative` or
  `NOT VALIDATED` row can never be discharged by a compare check.
- **The dedicated Ryzen 7 9700X is not a general GitHub Actions executor.**
  The queued benchmark service, its dispatch workflow and the earlier
  `.github/workflows/zen5-audit.yml` are removed (#2708). The only workflow
  admitted to the restricted runner group is
  `.github/workflows/9700x-direct-bench.yml`. It compiles and times the
  owner's own pull-request workloads from `benchmarks/9700x/` and reports
  diagnostic, unsealed process latency; see
  [`benchmarks/9700x/README.md`](../../benchmarks/9700x/README.md) and its
  [admission guide](../../benchmarks/9700x/ADMISSION.md). Once enabled, it
  also runs the routine `uarch_lab.py compare` of each commit after it lands
  on main against its first parent, or against the nearest earlier measured
  main commit when a merge burst left the first parent unmeasured (#2752;
  frozen `compiler-compare-v1`
  profile, report-only, merging never waits). Each comparison is published as
  the `9700X compiler benchmark` check on that main commit, queued before the
  run starts and completed by trusted terminal publication/recovery (#2803),
  plus one maintained report comment on the commit (#2804). The linked
  Actions `compare` job supplies live preparation/measurement state; a queued
  custom check does not prove that the physical job has not started. During merge bursts only
  the newest pending commit is measured; displaced commits remain
  **Not measured**. Native recovery reports verified pre-execution pending
  replacement as neutral, retaining the native cancellation and its exact
  attempt. It claims no successor result or range coverage from a request
  alone. A validated range result does not isolate one commit. A range since the last actual
  measurement that changes only reviewed prose is completed as neutral
  **Not measured** without allocating the 9700X
  ([unchanged-input skip](../compiler-main-unchanged-skip.md), #3923). See the
  [admission guide](../../benchmarks/9700x/ADMISSION.md#main-compiler-comparison).
  An explicitly requested owner experiment can compare a pull request head
  with its merge base before merging by adding a fresh request line to
  `benchmarks/9700x/compiler-compare.request` (#2769). Apply the
  [request decision](#request-decisions-and-examples) first; see the
  [workload guide](../../benchmarks/9700x/README.md#compiler-comparison-of-a-pull-request).
  These ordinary compiler A/B paths run no profile steps and no A/A. The
  separately versioned [sampling qualification](../compiler-main-sampling.md)
  route (#3212) is staged disabled; its explicitly admitted, predeclared research
  controls do not change either immutable ordinary profile. The single
  [MAIN routing switch](../compiler-main-profile-routing-v1.md) stays
  `disabled` until a reviewed certificate qualifies `compiler-main-40pairs-v1`;
  it can route only the authenticated automatic post-merge comparison, keeps
  the full FAST/QUALITY corpus, and rolls back to `compiler-compare-v1`.
  Explicit pull-request and acceptance requests keep their requested profiles. `native-retirement-performance-v1` remains blocked,
  and the `zen5-calibration-v1` producer and its `zen5_*` analysis tools are
  removed (#2741; tag `bench-service-final` retains them). Use the local
  trusted capture methods above for ad-hoc profiling. Historical audit notes retain
  the removed workflows and service job numbers only as provenance.
- **Sampling the sanitized (ASan+UBSan) Debug tree with `perf` works.** It is
  the CI critical path, so it is the configuration most worth profiling. Record
  it exactly like any other build; there is no sanitizer-specific obstacle:

  ```sh
  ./build.sh generate --sanitize && ./build.sh build -t ide
  LD_PRELOAD= perf record -F 999 -g --call-graph fp -o asan.data \
      -- ./build/Debug/ide test --verbose=1
  ```

  Four things must be right, and each one silently produces a *plausible but
  wrong* profile when it is not:
  - **Clear `LD_PRELOAD`.** With one inherited (NoMachine sets
    `LD_PRELOAD=/usr/NX/lib/libnxegl.so`), the shared ASan runtime is not first
    in the initial library list, the process aborts inside `ld.so` before
    `main`, and the capture is ~12 samples that are ~100% of *user-space* time
    in `ld-linux-x86-64.so.2` with no ASan symbols. That result is not a
    symbolization failure and not evidence that ASan defeats sampling — it is
    an empty profile of the dynamic loader. `CMakeLists.txt` already composes
    the runtime ahead of any inherited `LD_PRELOAD` for its own test targets;
    only ad-hoc command lines need the `LD_PRELOAD=` prefix.
  - **Run on a quiet machine.** A contended host produces the same visual
    signature — few samples, attribution smeared into the loader and the
    scheduler. Confirm the sample count and the `TEST_MODULE_TIMING` line agree
    with a serial run before reading anything into the histogram.
  - **`--call-graph fp` is correct and cheap here.** Debug is `-O0` and the
    sanitizer runtime keeps frame pointers, so frame-pointer unwinding resolves
    from inside `libclang_rt.asan` back into buster code. This matters because
    the sanitizer cost has to be charged to the buster function that provokes
    it; a leaf-only profile just says "memcpy".
  - **Do not use `perf script -F srcline` (or `-F ip,sym,srcline`) for line
    attribution.** It resolves the raw return address, which points *after* the
    call, so it reports the following line — measured on this tree it shifts
    call sites by 1–2 lines (real `c_test.c:7227` is reported as `7229`, real
    `7229` as `7230`) and prints `:0` for ~69% of frames. Extract
    `sym+offset` instead and symbolize the byte before the return address:

    ```sh
    perf script -i asan.data --no-demangle -F comm,ip,sym,symoff,dso
    # static vaddr = (readelf -sW addr of sym) + offset; then
    #   llvm-symbolizer --functions=linkage --demangle  <<< "CODE build/Debug/ide 0x<vaddr-1>"
    ```

    Use `sym+symoff`, not `-F dsoff`: `dsoff` is a **file** offset while
    `llvm-symbolizer` wants a **virtual** address, and for a clang PIE the two
    differ by `p_vaddr - p_offset` of the text segment (`0x1000` for
    `build/Debug/ide`). Feeding `dsoff` straight to `llvm-symbolizer` yields
    wrong-but-believable source lines. This is the same class of mistake as
    `perf report` mis-symbolizing buster-produced `ET_EXEC` images by
    `-0x400000`; clang-built binaries are PIE and symbolize normally once the
    right address space is used.
- **Where there is no hardware counter, callgrind is the currency**, and on
  two questions it is the better one. A cloud guest often exposes no PMU at
  all (`perf_event_open` returning `ENOENT`, so `STEP_INSTRUCTIONS` never
  prints); `valgrind --tool=callgrind --cache-sim=no --branch-sim=no` then
  counts instructions exactly and deterministically, which makes an A/B one
  run instead of three alternating pairs and removes the sampling skid and
  period aliasing that two audits paid to discover. Three properties decide
  how to read its numbers.
  - **It counts `rep stosb` at one Ir a byte** (measured: an 8 KB `memset`
    costs 8.229 Ir, a 64 KB one 65.572, a 512-byte one 62). On the machine
    that is one retired instruction, so `instructions:u` cannot see a libc
    fill or an ERMS copy at all, and callgrind's number for one *is the bytes
    it moved*. Use it for the memory-traffic items — the fills and the large
    copies — and say "MB written", never "instructions saved".
  - **Valgrind 3.22 cannot decode EVEX**, so the measured binary has to be
    built `-march=x86-64-v3` and every AVX-512 kernel in the tree is compiled
    out of it and shows as its `simd.h` fallback. Architecture-neutral deltas
    transfer to the AVX-512 build unchanged; an AVX-512 item has to be priced
    by census (count the queries, the tokens, the windows and the useful
    lanes, then price each against the measured cost of the loop it replaces).
  - **A `-march=native` build and a `-march=x86-64-v3` build of the same
    compiler do not produce the same output**, because the host build's
    feature set reaches the compiler's own target defaults. Byte-identity
    gates must compare like with like, and both builds need their own gate.
  - **Recipe.** Configure a separate diagnostic tree; a default `generate`
    keeps `-march=native`, so trusted trees are unchanged:

    ```sh
    ./build.sh generate -DBUSTER_NATIVE_TARGET=x86-64-v3
    ./build.sh build --config Release -t ide
    valgrind --tool=callgrind --cache-sim=no --branch-sim=no \
        --callgrind-out-file=base.cg build/Release/ide cc -c tests/basic_c_operations.c -o /tmp/basic.o
    callgrind_annotate --inclusive=yes base.cg | head -40
    ```

    `BUSTER_NATIVE_TARGET` is the `-march=` value for every host-built target
    (default `native`, and the legacy Clang AVX10 probe only runs for
    `native`). Developer Release trees already carry `-g`, so the annotation
    has file and line records, and `fi=`/`fe=` records charge an inlined
    helper's lines to the physical caller. Build baseline and candidate in the
    same checkout path, one after the other: an `-O3` unity compile of `ide.c`
    takes about 5 GiB, and parallel builds can be OOM-killed. Run each workload
    with the same command line and working directory, and pin `ide cc`'s own
    output target (for example `-march=znver3`), since it defaults to the
    portable baseline. A stage-1 self-compile takes about 10 minutes per profile;
    `--cache-sim=yes --branch-sim=yes` roughly doubles that.
  - **Read the numbers as diagnostics, never as acceptance evidence.**
    `-march=x86-64-v3` compiles out `BUSTER_SIMD_512` kernels such as
    `BUSTER_C_LEX_COMPACT`, so the lexer and other SIMD paths run their
    fallbacks and their counts are not the production binary's. Counts
    elsewhere compare only between two diagnostic binaries built the same way,
    never against a `-march=native` build. Callgrind reports instructions and a
    modeled cache and branch predictor, not time; its small bimodal predictor
    aliases when code moves, so misprediction deltas often land in unchanged
    functions. Two rebuilds of the same source differ by about 0.03% of a
    self-compile's Ir, so a smaller total delta needs per-function or per-line
    attribution of the changed code. `callgrind_annotate` reads sources at
    annotation time: annotate each profile from its own checkout, from the
    build's working directory. The approved 9700X route still owns timing.
- **`tools/branch_miss_survey.py` ranks branch mispredictions by source line,
  not by symbol.** `perf record -e branch-misses` is not a precise event: the
  sample lands past the branch that caused it, so its histogram names the
  function and three audits (`2026-08-22b`, `2026-08-22d`) paid to discover
  that separately. Zen 3 and later carry the AMD Last Branch Record extension,
  so `perf record -j any,u` captures the last sixteen branches of every sample
  with a per-entry mispredict flag — the branch instruction's own address. The
  script records that over the stage-1 self-host compile by default, tallies
  the mispredicted records, symbolizes them through inline frames, and prints a
  ranking per function and per line with each row's estimated absolute misses:

  ```sh
  ./build.sh build --config Release -t ide
  tools/branch_miss_survey.py --repeat 3 --cross-check
  ```

  `--cross-check` re-profiles the same run with `branch-misses:u` and prints
  that share beside each function, which is the check that the address mapping
  is right: a branch record is a runtime address in a PIE and needs the
  `p_vaddr - p_offset` skew above, and dropping it silently reports the
  neighbouring functions. Profile any other workload by passing it after `--`.
  `--self-test` covers the address math without needing `perf`.
- **Validate any profiling method against a non-sampling ground truth before
  trusting it.** The cheap one here is direct timing: bracket the call sites
  under suspicion with `timestamp_take()`/`timestamp_ns_between()` and print
  through `arguments->show`, then compare against the sampled shares of the
  same process. Confirm the extracted return addresses too — each one must
  disassemble to the instruction immediately after a `call` to the callee the
  callchain names (`objdump -d --start-address=... --stop-address=...`).

## Alternative evidence routes

Issue #4013 replaces #2761's universal Zen 5 requirement for claims whose
benefit does not depend on the 9700X's microarchitecture, and keeps the
scarce physical host for the questions only it can answer. Classify the
claim by mechanism, not by the files a change touches.
`docs/performance-validation-v1.json` records the classes (`evidence_classes`)
and the claim map (`claims`), and `performance_inventory_test.py` keeps that
map equal to the `pe_claims` table of `tools/performance_evidence.c`.

| Claim | Class | Sufficient evidence | Does not establish |
|---|---|---|---|
| `work-reduction`: removed traversals, queries, initialization, copies or allocations; better asymptotic work; fewer processes or operations | `work-count` | Repeatable Callgrind or Cachegrind counts (or perf `instructions`) of both arms in one environment, at relevant input sizes, plus existing work counters and correctness checks | Lower elapsed time; hardware constants; Zen 5 behavior |
| `native-runtime`: end-to-end compiler, build, test, memory or I/O change that no particular CPU mechanism decides | `native-host` | Paired uninstrumented wall/CPU/resource observations (`time`, or `perf-stat` where counting is permitted) on an appropriate non-9700X host | Transfer to Zen 5 or another CPU, OS, filesystem or concurrency level |
| `uarch`: SIMD width or intrinsics, branched versus branchless, scheduling, latency or ports, store forwarding, alignment, prefetch, cache or TLB tuning | `zen5-native` | The existing 9700X routes (`pull-compare`, `direct-workload`) | Anything about another CPU |
| `zen5`: an explicit Zen 5 speed claim | `zen5-native` | The existing 9700X routes | Anything about another CPU |

A change with mixed mechanisms is split into separate claims, each measured
by its own route; record why any part escalates to the physical host.
Neither "all scalar code is portable" nor "every change affects a CPU" is a
valid classifier. Reducing allocated bytes and tuning their cache-line
placement are two claims even in one patch. A counter-only result is
reported as exactly what it proves, for example **work reduction validated;
native runtime not measured**. It is not rejected for lacking Zen 5 data
when the agreed criterion is work or scaling. Valid native A/B evidence on a
non-9700X host completes a `native-runtime` claim for that host and workload
and does not stay "diagnostic only".

### Running a comparison

Build the tool once, then check capabilities before an expensive session:

```sh
clang -std=c11 -Isrc -O2 -Wall -Wextra -Werror -Wno-unused-function -fwrapv -fno-strict-aliasing -funsigned-char \
    tools/performance_evidence.c -lm -o build/performance-evidence
build/performance-evidence classify work-reduction
build/performance-evidence preflight --tool callgrind -- BASELINE_IDE cc -w -c input.c -o /dev/null
build/performance-evidence compare --claim work-reduction --tool callgrind \
    --baseline BASELINE_IDE --candidate CANDIDATE_IDE --out NEW_DIR -- cc -w -c input.c -o /dev/null
```

`compare` runs both arms on the same host, alternating which goes first. It
retains each run's stdout, stderr and per-process profiles under the new
directory, and writes `summary.json` (schema `buster-performance-evidence-v1`).
The summary binds the binary paths and SHA-256 hashes, optional
`--baseline-revision`/`--candidate-revision`, the arguments, the tool version,
the observed CPU, OS and kernel, the selected events, the cache model lines
and every raw observation. Its `zen5` field always says it is not Zen 5
evidence. Exit 0 is a complete comparison whatever its verdict; exit 2 is
incomplete or refused; `preflight` exits 3 when the tool cannot run here.

- **Callgrind and Cachegrind (`work-count`)** default to two runs per arm. An
  arm whose counts vary by more than 0.1% is `nonrepeatable`. A verdict needs
  separated arms and at least the minimum effect (`--min-effect-percent`,
  default 1). Children run with `--trace-children=yes`, one profile per
  process; the process counts per run are recorded. A child that forks
  without `exec` inherits its parent's counters, so check the counts before
  summing such a workload. `Ir` is an instrumented executed-instruction count,
  not the PMU retired-instruction count. `--cache-sim` and `--branch-sim`
  enable Valgrind's simplified models, record the model lines and make those
  events required; they are not a model of any real CPU, the 9700X included.
- **Valgrind and `-march=native`.** Valgrind cannot execute EVEX (AVX-512)
  instructions. A binary built for a host that has them fails with an explicit
  ISA reason, never with a zero count (#4009). Build *both* arms with a
  declared profiling-compatible ISA, for example
  `tools/work_ledger/build_subject.sh` (`-march=x86-64-v3`). Keep those
  results separate from native-build identities. Never transfer a
  scalar-fallback result to a production SIMD path.
  The ISA envelope also includes the loader, libc, and link startup objects.
  First try `valgrind --tool=none /usr/bin/true`: a loader SIGILL already
  rules out the application as its cause. On a distribution built for AVX-512,
  compiling Buster for v2/v3 alone may still link a v4 requirement from
  `Scrt1.o`. Inspect `readelf -n` on the executable. Use a compatible generic
  runtime and its startup objects for both profiling arms, retaining their
  identities separately from native builds; do not patch the ELF ISA
  declaration to conceal an unsupported requirement.
- **`time` and `perf-stat` (`native-host`)** default to one warmup and ten
  pairs, with at least six. Each metric uses an exact two-sided sign test over
  paired differences (p ≤ 0.05) plus the minimum effect. A significant change
  below the effect is `within-effect`, and anything else is `inconclusive`.
  `time` records wall, user and system time, peak RSS, faults and context
  switches through `wait4`, which includes reaped descendants. `perf-stat`
  accepts event names with optional `:u` (user) or `:k` (kernel) scope.
  An explicit scope matches only that scope; an unqualified request accepts
  a single observed scope, which must remain the same across every run and
  both arms. The receipt retains the requested spelling in the metric name
  and the observed scope in its `scope` field (`unqualified`, `u`, or `k`).
  Other modifier combinations or event syntax are refused before any run;
  ambiguous matching counters are missing. A comparison must select one
  primary scope (`instructions` for work reduction, `task-clock` for native
  runtime); an absent required primary records an explicit problem, writes
  `complete: false` and exits 2. Multiple primary scopes leave the result
  incomplete. A scoped
  result establishes only the selected scope. `perf-stat` refuses an event
  that is unsupported, not
  counted, or ran less than 50% of the time. A multiplexed event is marked.
  Denied counters are reported as denied; do not weaken host security to
  get them.
- **A/A control.** Passing the same binary as both arms is reported as an A/A
  noise control. Run one on a new host before trusting a small native effect.
- **Scope.** Valgrind results say nothing about native parallel scaling.
  A multithreaded change needs native measurements at its declared worker
  budget. Report compile time, generated-program runtime, code size,
  allocation bytes and RSS separately. A noisy hosted result stays
  `inconclusive`: do not rerun until it turns green.

### Rollout state

This first slice (#4013) supplies the policy, the classes, the inventory
contract and the tool. The trusted planners do not consume these receipts
yet. The routine `main-compare` route still measures every compiler commit
after it lands, as report-only. Until request planning can classify
before allocating, an eligible `work-reduction` or `native-runtime` claim
completes through this route without a `compiler-compare.request` line. Do
not add a 9700X request line just to validate it. A `uarch` or `zen5` claim,
an explicitly requested Zen 5 experiment, and physical-host qualification
keep their existing contracts.

### Optional tools by question

These are optional and purpose-specific. None of them is a dependency or a
CI job.

| Question | Tools | Limit |
|---|---|---|
| Allocation churn, lifetime, unused memory | Valgrind DHAT and Massif, heaptrack, Buster's arena and work counters | A malloc-level profiler does not see arena suballocations; profiler heap is not native RSS |
| System calls, startup, I/O, off-CPU time | strace, perf sched/lock, ftrace, approved bpftrace probes | Tracing changes elapsed time; it is never the timing baseline |
| Cost of building Buster | Clang `-ftime-trace` (`generate --time-trace`), CMake/Ninja/build-driver timing | Keep the host compiler building Buster separate from Buster compiling inputs |
| Native Windows or macOS | ETW with WPR/WPA, Apple Instruments | Validate an OS-specific change on that OS; see [Native hosted timing](#native-hosted-timing) |
| Deeper hardware attribution | AMD uProf (IBS), Intel VTune, `tools/cache_miss_survey.py`, `tools/branch_miss_survey.py` | Complements native execution on the target; never makes a target claim portable |
| Instruction-model hypotheses | llvm-mca, disassembly and code size, FlameGraph | Predictions and visualizations explain evidence; they are not measurements |

The licenses and installed provenance of these external executables have not
been audited here. Record them when admitting any one as a backend.

## Native hosted timing

**These results are not Zen 5 evidence.** They never satisfy, replace or
support a performance-validation claim, and they never fail a check. Zen 5
measurements do not establish macOS or Windows performance, and the approved
9700X cannot run either platform. The maintainer's decision on #2761
(2026-10-11) is that native timing runs on the standard GitHub-hosted runners,
is informative only, and stays visible through a warning and a tracking issue.

The two rows in
[`docs/performance-validation-v1.json`](../performance-validation-v1.json) are
`native-informative` on route `hosted-native`:

| Row | Measuring job | Comparison | Runs |
|---|---|---|---|
| `workflow:throughput-harness-macos.yml` | `timing` on `macos-26` | The existing `bench_throughput_ci` guard: two predeclared 20-pair rounds per case, with a case confirmed only when it crosses the margin in both rounds | Weekly schedule (main against its first-parent commit from 7 days earlier) and dispatch (optional `baseline`). Never on a pull request, so the scarce macOS runner is not held for every harness change |
| `workflow:issue-708-benchmark.yml` | `benchmark` on `windows-2025` | Each of three trials times the baseline and candidate `test_all_combinations_ci` matrix on one VM. A trial crosses when the candidate exceeds the baseline by the guard's wall-time margins (15% and 2 ms); a majority of trials crossing is a regression, a minority is inconclusive | Dispatch with `baseline` and `candidate`; the `bench/708-baseline` pull-request route keeps its existing defaults |

On macOS, `bench_throughput_ci` omits `--cpu auto`, because macOS has no CPU
affinity API. The macOS samples are therefore unpinned.

`tools/bench_direct/native_timing.c` reads each workflow's existing output. It
is native C on the existing `ci_metrics` JSON parser and `gh` transport, and it
is compiled in each job the way `lifecycle.c` is. A read-only `report` job on
`ubuntu-26.04` downloads the measuring job's artifact and runs it with
`--annotate`. For macOS this means the `macos-26` job only measures and
uploads, and the verdict, warning and any failure appear in `report`.

The report gives, in its step summary, the cell (case or trial), the
candidate/baseline ratio, both commits and the run link. No new statistic is
involved: the macOS ratio is the geometric mean of the two rounds' median
paired ratios, as in the guard's `summary.md`, computed as
`sqrt(r0) * sqrt(r1)` so large finite ratios cannot overflow. A number that is
not representable as a finite double (a huge integer, an overflowing exponent)
or a derived ratio that is not finite and positive is malformed evidence. On
Windows the results are read whether PowerShell wrote them as UTF-16LE or
UTF-8; any non-ASCII byte is refused.

| Outcome | Check | Visibility |
|---|---|---|
| Regression | Passes | `::warning::` annotation, step-summary line, tracking issue (trusted runs only) |
| Inconclusive | Passes | `::warning::` annotation and step-summary line; no issue |
| No regression | Passes | Step-summary line |
| Build failure, crash, failed or missing arm, malformed, partial or inconsistent samples, or any guard exit other than 0 or 1 consistent with its counted regressions | **Fails** (exit 2) | `::error::` annotation |

### Alert path

Each platform has one open tracking issue, titled
`Native hosted timing regression: macOS (throughput-harness-macos.yml)` or
`Native hosted timing regression: Windows (issue-708-benchmark.yml)`. It is
authored by `github-actions[bot]` and carries the marker
`<!-- native-hosted-timing platform=... -->`. The first regression opens it.
Each later regression adds a comment with the run, commit, cells and ratios.
Close the issue once the regression is explained or fixed; the next regression
opens a new one.

Only a separate `alert` job holds `issues: write`, alongside `contents: read`
and `actions: read`. It runs only when all of these hold:

- the measured verdict is `regression`;
- the repository is `buster14a/buster`;
- the ref is `refs/heads/main`;
- the event is the macOS schedule or dispatch, or a Windows dispatch;
- `demonstrate_regression` is not set.

It checks out the trusted `main` revision and downloads only its own run's
artifact. It then re-derives the verdict from the raw evidence before
publishing. The measuring jobs, which compile and run both revisions, stay
read-only. A pull-request run, including the Windows `bench/708-baseline` route,
never files or updates an issue.

The `demonstrate_regression` dispatch input exercises the warning path without
a real regression. On Windows it doubles the candidate durations before the
rule is applied. On macOS it confirms the case with the highest wall-time
ratio. The report is labelled a demonstration, and the alert job never runs.

`performance_inventory_test.py` accepts `native-informative` only for a macOS
or Windows workflow row whose `job` runs on one literal GitHub-hosted `macos-*`
or `windows-*` label matching its `platform`. A Linux, matrix-selected or
self-hosted timing job cannot use the status to avoid the 9700X rule. The test
also requires that:

- a read-only job reports through `native_timing.c` with `--annotate`;
- no step continues on error;
- one separate main-gated job compiles the tool and publishes through it;
- the row's `alert` links a heading in this guide.

`native_timing --self-test`, which runs in the benchmark policy check, covers
the analysis, the malformed and extreme-number refusals, the demonstration path
and the issue publisher against synthetic `gh` responses.

## Benchmarking a compiler change (A/B)

`tools/uarch_lab.py compare` answers "did this change make the stage-1
self-host compile faster or slower, and why" with one verdict line, a report
for people and a JSON summary for agents. The reference numbers for the
dedicated Zen 5 host are in the
[baseline audit](../performance-audits/2026-10-03T100722Z.md) (stage-1 compile
about 1.55 s, MAD 0.2%, instructions deterministic to about 12K of 22.29G).

1. Commit the subject revision, build both tests-off Clang Release compilers,
   and freeze them. This example uses one session-owned detached worktree and
   the same configured build path for both revisions. A worktree and output
   root belong to one session; see
   [parallel sessions](workflow.md#parallel-sessions-on-one-machine).
   If separate build roots are necessary, retain the path-normalization and
   same-source A/A cross-root controls described above.

   ```sh
   base_revision=$(git merge-base origin/main HEAD)
   candidate_revision=$(git rev-parse HEAD)
   session_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-session.XXXXXX")
   session_root=$(cd "$session_root" && pwd)
   git worktree add --detach "$session_root/src" "$base_revision"
   mkdir "$session_root/bin"
   (
       set -eu
       cd "$session_root/src"
       ./build.sh generate --cc clang --no-include-tests
       ./build.sh build --config Release -t ide
       grep -Fx 'BUSTER_INCLUDE_TESTS:BOOL=OFF' build/CMakeCache.txt
       cp build/Release/ide "$session_root/bin/ide-base"
       cp build/CMakeCache.txt "$session_root/bin/base.CMakeCache.txt"
       cp build/compile_commands.json "$session_root/bin/base.compile_commands.json"

       git switch --detach "$candidate_revision"
       ./build.sh generate --cc clang --no-include-tests
       ./build.sh build --config Release -t ide
       grep -Fx 'BUSTER_INCLUDE_TESTS:BOOL=OFF' build/CMakeCache.txt
       cp build/Release/ide "$session_root/bin/ide-cand"
       cp build/CMakeCache.txt "$session_root/bin/candidate.CMakeCache.txt"
       cp build/compile_commands.json "$session_root/bin/candidate.compile_commands.json"
       printf '%s\n' "base=$base_revision" "candidate=$candidate_revision" > "$session_root/bin/revisions.txt"
       sha256sum "$session_root/bin/ide-base" "$session_root/bin/ide-cand" > "$session_root/bin/SHA256SUMS"

       # Restore and build the frozen workload's generated-header closure.
       git switch --detach "$base_revision"
       ./build.sh generate --cc clang --no-include-tests
       ./build.sh build --config Release -t ide
   )
   ```

   Stop if setup or a build fails. Keep the binaries, saved caches and compile
   commands together: tests-off is a property of each host compiler build.
   Save both revisions and all configure/compile flags; a historical audit
   with an unstated tests policy cannot establish a matched configuration.

2. Compare them on that frozen, configured source tree (it needs
   `build/generated`; do not edit or rebuild it during the run). Use a new
   session-owned output directory for each attempt; reused lab outputs can
   replace earlier captures and reports.

   ```sh
   python3 tools/uarch_lab.py compare --baseline "$session_root/bin/ide-base" --candidate "$session_root/bin/ide-cand" \
       --repo-root "$session_root/src" --cpu 2 --output "$session_root/ab-attempt-1" [--target-minutes 15 | --pairs N] \
       [--profile-steps topdown,sampling] [--sudo] [--require-identical-output] \
       [--min-effect PCT] [--no-fresh-copy]
   python3 tools/uarch_lab.py report "$session_root/ab-attempt-1"     # re-render report.md and summary.json
   ```

   **Issue #48 opt-in self-hosting comparison.** The existing owner-authorized
   pull-compare route accepts the exact standalone line
   `canonical-inline-self-host-v1` in
   `benchmarks/9700x/compiler-compare.request`. Add it only after the trusted
   harness dependency has landed; the feature pull request can then request its
   own 9700X evidence without changing the ordinary baseline/head profile or
   its workflow and authorization gates.

   After the tests-off candidate compiler is built, the harness runs this
   bounded profile before checking out the merge base for the ordinary
   comparison. It keeps candidate HEAD and that build's `build/generated`
   headers in place, hashes `src/` and `build/generated` before and after,
   and refuses changed inputs, symlinks or special files. Both stages compile
   the same candidate-HEAD unity source. Stage 1 uses the exact same candidate
   compiler binary for A and B, with A omitting `-fcanonical-inline` and B
   passing it. Stage 2 uses those two stage-1 outputs as compilers with the
   same respective modes. Saved stage-1 references retain the produced
   compiler's bytes and permissions; missing execute permission or a failed
   permission copy remains a failure. Each stage is 12 ABBA pairs after one
   warmup. The report records wall time, retired instructions when perf can
   count them,
   peak RSS and executable-section code bytes; unavailable hardware counters
   remain NA. Each mode must reproduce its own stage-1 compiler byte-for-byte
   in stage 2; off/on outputs do not need to match one another. A failed or
   incomplete profile makes the requested evidence incomplete, while a
   slower result remains report-only.

   **Issue #3397 opt-in `-fc-ast-pilot` adoption profile.** The same route
   accepts one fresh exact line `profile: ast-pilot-acceptance-v1` in
   `benchmarks/9700x/compiler-compare.request`. Like the analyzer selectors,
   it is selected only when the head adds it exactly once against every
   parent; a historical occurrence does not replay it, and it cannot be
   combined with an analyzer profile. `ast_pilot_acceptance.py` runs it after
   the tests-off candidate build, on the same frozen candidate-HEAD tree
   (`src/` and `build/generated` hashed before and after). It times the one
   candidate binary as both A (default) and B (`-fc-ast-pilot`) with
   `uarch_lab.py compare --ast-pilot-pair CELL` on the unity source at `-g0`
   without linking: a `-fsyntax-only` cell, then a `-c` cell, each 20 ABBA
   pairs after one warmup on CPU 2 with fresh copies. It records wall time with
   its 95% CI, retired instructions when perf can count them (NA needs a
   recorded reason), peak RSS, and the tree's `retained_bytes` from an untimed
   `-v` syntax-only run of B, which also proves A built no tree. Every run's
   stdout and stderr must match its variant's reference, and A's and B's
   diagnostics and `-c` objects must be identical. The verdict is the
   `docs/agents/frontend/ast.md` stage-1 adoption budget
   (`AST_PILOT_BUDGET`): `-fsyntax-only` B/A at most 0.99 in instructions and
   in the median paired wall ratio, whose CI must also exclude 1; `-c` wall not
   detectably slower (CI low bound at most 1); and in each cell B's median
   peak RSS above A's by at most 1.05 times the retained bytes. It is
   report-only. Request flags, pair counts and other compiler arguments are
   not accepted; a different flag pair needs its own versioned profile.
   `compiler_publish.py` re-validates every raw cell document, the tree probe
   and the recomputed verdict on the 9700X evidence.
   Since `-fc-ast-pilot` became the default (2026-10-13, #3102), variant A
   builds a tree on a head after the switch, so the profile fails closed at
   its tree probe there. The tree's acceptance follows #4058 through the
   ordinary two-binary comparison.

   **Issue #3011 opt-in constant-reuse profile.** The same route accepts one
   fresh exact line `profile: constant-reuse-acceptance-v1`, selected by the
   same every-parent freshness rule. It cannot be combined with an analyzer
   profile, the ast-pilot profile, or the other opt-in legs it excludes in
   `compiler_compare.py`. It runs beside issue #48's inline profile, whose
   exact line in the request history selects it in every pull compare.
   `constant_reuse_acceptance.py` runs
   it after the tests-off candidate build, on the same frozen candidate-HEAD
   tree. It times the one candidate binary as both A (default) and B with
   `uarch_lab.py compare --constant-reuse-pair CELL` on the `-g0` unity
   `-c` compile. The `intern` cell passes `-fcanonical-intern-constants` to B
   and the `cse` cell passes `-fcanonical-fast-cse`. Interning has been on by
   default since its measurement, so the `intern` cell now compares two
   interning compiles; its recorded result is the 9700X run in
   `docs/canonical-fast-pipeline.md`. Each cell is 20 ABBA
   pairs after one warmup on CPU 2 with fresh copies. It records:
   - wall time with its 95% CI;
   - retired instructions when perf can count them (NA needs a recorded
     reason);
   - peak RSS;
   - executable-section code bytes of each object.

   A's and B's diagnostics must be identical. Their objects differ by design.
   The profile is report-only and has no verdict, and it accepts no request
   flags, pair counts or compiler arguments. `compiler_publish.py`
   re-validates every raw cell document on the 9700X evidence. The flags and
   their hosted census are described in
   [the canonical FAST pipeline guide](../canonical-fast-pipeline.md).

   **Issue #1125 opt-in per-process floor profile.** The same route accepts
   one fresh exact line `profile: process-floor-v1`, selected by the same
   every-parent freshness rule. It cannot be combined with an analyzer profile
   or another opt-in leg; like the constant-reuse profile, it runs beside
   issue #48's inline profile. `process_floor_acceptance.py` runs it after the
   tests-off candidate build. It writes `int main(void){return 0;}` as
   `tiny.c` into an otherwise empty directory and runs
   `uarch_lab.py compare --process-floor-pair CELL` there, with the one
   candidate binary as both A and B. A is always `cc -g0 -c tiny.c -o OUT`,
   the native x86-64 object compile. B depends on the cell:
   - `aa`: the same command, the A/A calibration;
   - `aarch64`: `cc -g0 -target aarch64-unknown-linux -c tiny.c -o OUT`;
   - `syntax-only`: `cc -g0 -fsyntax-only tiny.c`.

   Each cell is 100 ABBA pairs after two warmups on CPU 2, with fresh copies
   of the binary. It records:
   - wall time with its 95% CI (the harness span, which includes the pinned
     `perf stat` wrapper);
   - task-clock, the compiler process's own CPU time;
   - the compiler's own `wall_ns` when `-fmetrics-out` works for both
     commands;
   - retired instructions when perf can count them (NA needs a recorded
     reason);
   - peak RSS, which is NA when the compiler stays near the wrapper's own
     footprint.

   Diagnostics must be identical in every cell, and the A/A objects must be
   byte-identical. The report applies the issue's stop rule
   (`process_floor_verdict`): a control resolves the floor only when its wall
   ratio's 95% CI lies wholly outside the A/A cell's. Each control also
   reports A's median minus its own, in wall time and in task-clock. The
   profile is report-only and accepts no request flags, sources, pair counts
   or compiler arguments. `compiler_publish.py` re-validates every raw cell
   document and the recomputed verdict on the 9700X evidence.

   **Issue #2761 opt-in real-source throughput profile.** The same route
   accepts one fresh exact line `profile: throughput-real-source-v1`, selected
   by the same every-parent freshness rule; it cannot be combined with an
   analyzer profile. After the corpus leg,
   `tools/bench_direct/throughput_real_source.py` stages one fresh root per
   pinned input outside timing (`THROUGHPUT_REAL_SOURCE_INPUTS`): the cJSON
   sources from raw GitHub URLs at commit `c859b25d`, the Lua 5.4.8 tarball's
   `src/` and the SQLite 3.53.4 amalgamation zip, each checked against its
   pinned SHA-256, plus `tests/basic_cjson_roundtrip.c` from the merge base.
   Each merge-base descriptor must match its pinned SHA-256. Then the merge
   base's `./build.sh bench_throughput run --real-source` times each
   descriptor's own `compile_link_argv` (source to linked executable, `-O2`,
   direct-SSA, non-PIC) on the matched baseline and candidate binaries, so
   the merge base must contain the `--real-source` option.
   The harness re-verifies every staged tree against its descriptor's
   inventory and tree hash before and after timing.

   Sampling is declared up front: FAST and QUALITY, two warmups per variant,
   then two fixed rounds of 20 adjacent A/B pairs for each of the six
   descriptor/mode cells, on CPU 2 with a 120-second per-sample timeout.
   ABBA blocks and job order follow the recorded seed. Every pair runs: there
   is no early stop, rerun, trimming or best-of-N. The guard is off, so each
   cell is `diagnostic (guard disabled)`: medians and per-round sign tests are
   reported and never gate. The receipt's `throughput_real_source` record and
   the evidence under `throughput_real_source/` (`summary.json`,
   `metadata.json`, `staging.json`, the run's own `cpuinfo.txt` and raw
   samples) are re-validated by `compiler_publish.py`
   (`validate_throughput_real_source`). It refuses a harness exit other than
   0, a missing or extra cell, a round with fewer than 20 pairs, a descriptor,
   tree, archive or file hash other than its pin, compiler hashes other than
   the measured binaries, and a `cpuinfo.txt` that does not name only the
   Ryzen 7 9700X. Missing, partial, failed or cancelled evidence therefore
   fails the check.

   Cost: on a 4-vCPU hosted 2.8 GHz Xeon (diagnostic, not Zen 5), the full
   profile (two warmups plus 40 timed pairs, so 42 passes over the six cells
   and both variants) took 508 s, about 12 s per pass, partly alongside other
   test runs. The slowest cell, SQLite QUALITY, had a 1.96 s median. That
   fits inside the unchanged 90-minute `compare-pull` job, next to the roughly
   14-minute ordinary comparison. The leg has its own 30-minute timeout; no
   job timeout changed. The host needs the
   descriptors' native dependencies (for Lua, the readline headers and
   library) and outbound HTTPS to `raw.githubusercontent.com`, `www.lua.org`
   and `www.sqlite.org`.

   **Issue #2761 opt-in PGO/LTO production profile.** The same route accepts
   one fresh exact line `profile: production-profile-v1`, selected by the same
   every-parent freshness rule. It cannot be combined with an analyzer,
   real-source or ast-pilot profile, so the leg fits the `compare-pull` job.
   After the corpus leg, `measure_production_profile` makes a shared clone of
   the head beside the base checkout, which stays untouched for the closure
   checks. There the head's own
   `./build.sh production_profile --no-benchmark --jobs 16 --build-lanes 4`
   builds the seven trees and trains both profiles on the native corpus. It
   then inspects the PGO+LTO compiler for debug sections and runs `test_all`
   on it, exactly as [production-profile.md](../production-profile.md)
   describes. The merge base's `bench_throughput run` then times that run's
   `release` compiler (base) against its `pgo-lto` production compiler
   (candidate). It uses the `throughput-corpus-v2` arguments plus
   `--require-identical-output`, so sampling, rounds and decisions are those
   of the ordinary corpus leg, and any object that differs between the two
   compilers fails the run. It is report-only: a confirmed regression is
   reported, never refused.

   `compiler_publish.py` re-validates the receipt's `production_profile`
   record against the evidence under `production_profile/`
   (`validate_production_profile`). It refuses:
   - a build exit other than 0;
   - a `summary.txt` that is not `status=passed` for this head, or that lacks
     `correctness=test_all` and `debug_sections=absent`, or that names a
     production compiler hash other than the measured `pgo-lto` binary;
   - a `BUSTER_PGO_PHASES_V1` ledger other than the 23 frozen phases
     (`PRODUCTION_PROFILE_LEDGER_PHASES`), each passed exactly once in slot
     order, so a duplicated, missing, substituted or reordered phase fails;
   - identical release and PGO+LTO binaries;
   - any corpus evidence the ordinary corpus validator refuses.

   The receipt's observed host must be the Ryzen 7 9700X, as for every
   comparison. The host needs `clang`, `ld.lld` (the LTO lanes link with
   `-fuse-ld=lld`), `llvm-profdata`, `llvm-readobj` and Clang's profile
   runtime (`libclang_rt.profile`). A missing tool on `PATH` fails the leg
   before the build starts and the receipt names it; a missing runtime fails
   the build leg with its log.

   Cost: on a hosted 4-vCPU 2.8 GHz Xeon with three build lanes (diagnostic,
   not Zen 5), `production_profile --no-benchmark` passed all 23 phases in
   1200 s. Each of the seven tree builds took 300 to 480 s, both trainings
   about 11 s each, and `test_all` 159 s. The corpus run on its two
   compilers then took 55 s, with identical outputs. The build leg has its
   own 40-minute timeout and the corpus run the ordinary 30-minute one, so
   the leg fits inside the unchanged 90-minute `compare-pull` job next to the
   roughly 14-minute ordinary comparison. No job timeout changed.

   **Issue #2761 opt-in compiler probes.** The same route accepts one fresh
   exact line `profile: compiler-probes-v1`, selected by the same
   every-parent freshness rule. It is the Zen 5 route for a targeted
   register-allocator or scheduler claim, next to the end-to-end self-host.
   It cannot be combined with an analyzer, real-source, ast-pilot or
   production profile; one long opt-in leg per job keeps the budget provable.
   `tools/fast_ra_scaling.c` (the real FAST prepass and prepassed placement on
   empty-value chain and fanout CFGs) and `tools/machine_line_mark_scaling.c`
   (`machine_schedule_function` with one line mark per instruction) include
   compiler internals, so they cannot be direct workloads. After the corpus
   leg, `measure_compiler_probes` compiles the native harness
   `tools/bench_direct/compiler_probes.c` from the trusted checkout. It uses
   only libc, because that checkout holds only `tools/`, so no candidate
   header reaches it. It then runs the harness, which does the following:
   - It makes two shared clones of the candidate checkout, at the merge base
     and at the head. The base checkout and its closure checks stay untouched.
   - It copies the head's probe sources outside both clones, as the audits do,
     and builds each probe once against each clone's own `src/` with clang.
     The commands are those in
     [2026-09-07T222944Z](../performance-audits/2026-09-07T222944Z.md) and
     [2026-09-18T224655Z](../performance-audits/2026-09-18T224655Z.md)
     (line-mark: schedule mode only, `BUSTER_LINE_MARK_HELPER_BENCH=0`). The
     one addition is `codegen.c` in the FAST probe's source list: since #2876
     `machine.c` calls its `codegen_a64_windows_save_area_size`, and the
     audit's command no longer links without it.
   - Its cases are fixed: FAST chain and fanout at 256, 1024, 4096 and 16384
     blocks, and line-mark schedule at 1024, 4096, 16384 and 65536 leaves.
     Each invocation takes 11 timed samples after its own untimed warmup.
   - Per case and binary there are two warmup invocations, then 30 pairs on
     CPU 2. Even pairs run the base first and odd pairs the head first. Every
     pair runs: there is no early stop, rerun or trimming.
   - The statistic is the median of each invocation's samples (`total_ns` or
     `schedule_ns`), the head/base ratio per pair, and per case the median
     ratio with a distribution-free 95% order-statistic interval. It is
     report-only.

   The probes' `key=value` lines are their structured output; nothing was
   added to them. Every non-`_ns` field is the self-check, and the profile's
   `check_fields` fix it exactly: for FAST, `blocks`, `prepass_bytes`,
   `total_bytes`, `spills`, `reloads`, `copies`, `edits` and `frame`; for
   line-mark, `mode`, `leaves`, `rows`, `batch`, `retained_bytes`, `permuted`,
   `displacement` and a 16-digit hex `hash`. Each case's `markers` must match
   too: its size, and for line-mark `mode=schedule`, `rows` = 2 x leaves + 1
   and `batch=1`. The self-check must be identical across every invocation of
   one binary. Whether the base and the head agree is reported, not required,
   since an allocator or scheduler change may change it legitimately.

   `compiler_publish.py` re-validates the receipt's `compiler_probes` record
   against `compiler_probes/summary.json` (`validate_compiler_probes`), which
   keeps every raw sample. It refuses:
   - an exit other than 0, a failed status or any recorded error;
   - a profile, base or head other than the frozen profile and the compared
     revisions;
   - a build list other than one passed build per probe and revision, or a
     missing probe binary or source hash. `compiler_compare` hashes the binaries
     and the copied probe sources itself; the harness reports no hashes;
   - a missing, extra or reordered case, or changed case arguments;
   - fewer than two warmups or 30 pairs, or a pair out of the fixed order;
   - an invocation that failed, timed out or has partial samples, or a sample
     that is not an integer from 1 ns to the 60-second invocation timeout
     (checked before any median or ratio arithmetic), a self-check field that
     is missing, extra or malformed, a workload marker other than the case's,
     or a self-check that changes.

   The harness states no statistics; every one is re-derived from the raw
   samples. Its `--self-test` checks output parsing, pair alternation, JSON
   escaping and a whole fake case, and runs in the benchmark policy job.
   `compiler_test.py` checks that its `--profile` output equals
   `COMPILER_PROBES_PROFILE`, so the native and Python copies cannot drift.

   Missing, partial, failed or cancelled evidence therefore fails the check.
   The binaries are not exported. Their hashes differ even in an A/A run,
   because `-g` records each clone's path.

   Cost: on a hosted 4-vCPU 2.8 GHz Xeon (diagnostic, not Zen 5), a real A/A
   run of the whole leg through `measure_compiler_probes` at `8fa1ca80` took
   241 s, including the harness build. The two clones and four probe builds
   take about 3 minutes (each probe build 35 to 38 s), and the 12 cases about
   40 s. Every case's interval contained 1.0. The leg has its
   own 20-minute timeout, so it fits inside the unchanged 90-minute
   `compare-pull` job next to the roughly 14-minute ordinary comparison. No
   job timeout changed. A head whose probe no longer compiles against the
   merge base (for example after an allocator API change) fails the leg with
   its build log; land the probe change first.

   **Issue #2761 opt-in frontend and selector probes.** The same route
   accepts one fresh exact line `profile: frontend-selector-v1`, selected by
   the same every-parent freshness rule. It cannot be combined with an
   analyzer, real-source, ast-pilot or production profile. That refusal is not
   needed for the budget; it keeps one opt-in leg per attempt so a report
   is never shared between two profiles. After the corpus leg,
   `measure_frontend_selector` runs three probes on the already-built
   `ide-base` and `ide-cand`. All three use CPU 2 and run from the base
   checkout, so the tools are the merge base's:
   - `tools/benchmark_frontend_scaling.py --suite all --warmups 1
     --repetitions 11 --cpu 2` covers 54 generated `-fsyntax-only` and `-c`
     cells. It alternates the order of the binaries, and any `-c` object that
     differs between them fails the run.
   - `tools/selection_benchmark.py --samples 15 --allocators fast quality
     --check --cpu 2` runs on its three generated workloads. Within each
     sample, `bench-select` and `-O2 -c` run under both allocators. `--check`
     executes the programs and exercises the failure paths. The target, the
     selected work counts and the object bytes must be identical across every
     sample of both binaries.
   - `ide bench` runs in 20 pairs of fresh processes under `taskset -c 2`.
     Even pairs run the base first, odd pairs the head first. Each run must
     exit 0 and print one 30-iteration `BENCH_C_FRONTEND` line for
     `tests/basic_c_operations.c` of the base checkout, and every run must
     report the same byte count.

   The frontend tool records its pinned affinity, the observed CPU model and
   a final `complete` marker in `provenance.json`. Its generated sources stay
   out of the artifact; their hashes are in `results.json`. Every timing is
   report-only.

   `compiler_publish.py` re-validates the receipt's `frontend_selector`
   record against the evidence under `frontend_selector/`
   (`validate_frontend_selector`). It refuses:
   - a tool exit other than 0;
   - any record whose binary hashes differ from the measured binaries, or
     that differ before and after the `ide bench` runs;
   - any record whose observed CPU is not the Ryzen 7 9700X;
   - affinity other than CPU 2;
   - a population that is not exactly the 54 frontend cells, three selection
     workloads and 40 `ide bench` runs, each with its full sample count;
   - a malformed sample: a missing or wrongly typed target, work count,
     timing or object hash fails closed before any comparison;
   - a fallback sample, or differing work counts, targets or objects;
   - a tool's reported median that its own retained samples do not give.
     The report's cells are re-derived from the raw samples, never taken
     from a tool's summary;
   - a frozen profile or cell digest that changed.

   Cost: on a hosted 4-vCPU 2.1 GHz Xeon (diagnostic, not Zen 5), the whole
   leg took 57 s on two distinct locally built Release `ide` binaries: 45 s
   of frontend scaling, 6 s of selection and 7 s for the 40 `ide bench` runs.
   It exported 484 KB of evidence, and its largest member is 153 KB. Each
   tool has its own timeout: 20 minutes for
   frontend scaling, 10 minutes for selection and 30 seconds per `ide bench`
   run, 50 minutes in all even if every bound were reached. The leg
   therefore stays inside the unchanged 90-minute
   `compare-pull` job next to the roughly 14-minute ordinary comparison. No
   job timeout changed.

   **Issue #2676 opt-in deep-nesting leg.** The same route accepts one fresh
   exact line `profile: frontend-nesting-v1`, selected by the same
   every-parent freshness rule. It is the Zen 5 route for
   `tool:benchmark_frontend_scaling.py@frontend-nesting`. It cannot be
   combined with any other opt-in profile; both the harness and the
   publisher refuse the combination. After the corpus leg,
   `measure_frontend_nesting` runs the merge base's
   `tools/benchmark_frontend_scaling.py --suite nesting --warmups 1
   --repetitions 11 --cpu 2` from the base checkout on `ide-base` and
   `ide-cand`. Each of the eleven families in `NESTING_FAMILIES` is one
   function whose statements nest 2,000, 8,000 and 32,000 levels deep, timed
   with `-fsyntax-only` and `-c`. Each depth is four times the last, so a
   term growing with the square of the depth shows as a sixteenfold step.
   The 68 cells alternate the order of the binaries, and any `-c` object
   that differs between them fails the run.

   `compiler_publish.py` re-validates the receipt's `frontend_nesting`
   record against `frontend_nesting/provenance.json` and `results.json`
   (`validate_frontend_nesting`, sharing `frontend_scaling_problems` with
   the frontend-selector leg). It refuses a tool exit other than 0, binary
   hashes that are not the measured binaries, an observed CPU other than the
   Ryzen 7 9700X, affinity other than CPU 2, a population that is not exactly
   the 68 profile cells with eleven samples per binary, a reported median its
   samples do not give, an object cell without its identical-object hash, and
   a frozen profile or cell digest that changed. Every timing is report-only.

   Cost: on a hosted 4-vCPU 2.1 GHz Xeon (diagnostic, not Zen 5), the whole
   leg took 190 s and exported 196 KB. Its 20-minute timeout keeps it inside
   the unchanged 90-minute `compare-pull` job next to the roughly 14-minute
   ordinary comparison. No job timeout changed.

   **Issue #2761 opt-in host Wild linker leg.** The same route accepts one
   fresh exact line `profile: wild-linker-v1`, selected by the same
   every-parent freshness rule. It is the Zen 5 route for
   `tool:wild_linker_bench.c` (#2645). It cannot be combined with an analyzer,
   real-source, ast-pilot, production, compiler-probes or frontend-selector
   profile.
   `measure_wild_linker` runs it after the corpus leg:
   - **Linkers.** It finds the host's installed Wild with `command -v wild`
     and never downloads a linker. The comparator is the audit's, mold
     (`command -v mold`). Only when mold is not installed at all does it
     fall back to the host's default `ld`, which must report a GNU ld
     version. The check then states the substitution. Wild must report its
     own identity, `Wild <version> (compatible with GNU linkers)`, which is
     what `Args::linker_identity` prints at wild `e9a9b0cb`. `<version>` is
     any form `wild/build.rs` writes: a release version, a merge-base hash,
     `<release> <hash>-modified`, or `<release> non-git-build`, which is what
     a distribution package such as the 9700X host's reports. Each linker's
     `command -v` path, resolved path, first `--version` line and SHA-256
     (before and after the leg) go into the receipt. A missing Wild, a
     missing comparator, a silent `--version`, or a `mold` that is not
     mold fails the leg with that reason. A `mold` that is not mold never
     falls back.
   - **Inputs.** It links the head's own `ide` objects, not a frozen
     external set. In an isolated shared clone of the head,
     `./build.sh generate --cc clang --linker DEFAULT --no-include-tests
     --no-fuzz --no-sanitize --no-lto` with debug information and frame
     pointers configures the audit's `build-wild-unity` and
     `build-wild-separate` trees. It then builds Release unity, Debug
     (always non-unity) and Release separate. The reasons: no input has to
     be stored or fetched onto the host, the evidence belongs to the revision
     under review, and a Buster change that alters what the linkers see is
     measured. Every object and archive is hashed before timing and must be
     unchanged at the end. The receipt records each manifest's hash. The
     input steps share one 20-minute deadline.
   - **Harness.** The trusted revision's `tools/wild_linker_acceptance.c
     --link-only` (built with `clang -std=c11 -O2 -Wall -Wextra -Werror
     -Wpedantic`) reuses `wild_linker_bench.c`'s process capture, `wait4`
     timing, seeded order and paired bootstrap. It runs under
     `taskset -c 2,3,4,5`, four distinct physical cores, so the
     default-thread cells are not reduced to one CPU. For each of the
     audit's direct cells (`ide-debug`, `ide-unity-release`,
     `ide-separate-release`), it takes the last `ninja -t commands ide`
     step, drops any linker selection, and has Clang print the direct Wild
     argv (`-###`). It appends `--no-gc-sections` and the fixed benchmark
     build ID `0x2645…`. Wild adds `--no-fork --icf=none`. The comparator
     replaces only the executable and adds its own equivalents: mold the
     same two options, GNU ld none, because it neither forks nor folds code.
   - **Sampling.** Frozen before the run. Per cell, there are 2 warmups and
     then 21 seeded (2645) randomized AB/BA pairs at default threads, and
     again with `--threads=1` (GNU ld is single-threaded and adds nothing).
     `ide-debug` also has 9 A/A pairs per linker. There is no adaptive
     stopping: any failed link stops the harness without a ranking. Each
     linker's output is then linked again and must be byte-identical, and
     must compile a probe.

   `compiler_publish.py` re-validates the receipt's `wild_linker` record
   against the evidence under `wild_linker/` (`validate_wild_linker`). It
   refuses:
   - a harness or input-build exit other than 0;
   - an observed CPU other than the Ryzen 7 9700X, or a harness affinity
     other than CPUs 2-5;
   - a Wild whose `--version` is not Wild's own identity, a Wild output
     whose `.comment` does not carry that same identity as
     `Linker: <identity>`, or a comparator output that carries it with or
     without that prefix. Wild writes `Linker: <identity>` into every
     executable it links by default, so another linker installed as `wild`
     is refused
     even when it imitates the version line. `compiler_compare` reads each
     saved output's `.comment` itself, and only after checking that its
     hash is the one the harness recorded;
   - a linker without an absolute path, a version or a SHA-256, a linker
     whose hash changed during the leg, an unsupported comparator kind, an
     unrecorded or false substitution, or Wild and comparator being the same
     executable;
   - an input manifest that differs from the receipt's hash;
   - a population that is not exactly the eight comparisons in the seeded
     order. The generator is replayed in Python, each with every warmup and
     pair linked successfully;
   - a cell whose retained Wild argv (`<cell>-argv.txt`) is not exactly
     Clang's form, one double-quoted word per argument drawn from
     `[A-Za-z0-9_./+=,:@%-]`, or does not start with the recorded Wild,
     write `<config>/ide`, follow the frozen Clang link grammar, and take
     every relative object
     and archive from that cell's hashed input manifest. System startup
     objects must come from `/usr` or `/lib`, and nothing else may come from
     the build root. The grammar (`WILD_ARGV_FLAGS`, `WILD_ARGV_OPERANDS`)
     admits only Clang's own flags, `-z`/`-m`/`-dynamic-linker`/`-o` with
     admitted operands, `-L` system directories, plain `-l` names, CMake's
     `--dependency-file=CMakeFiles/ide.dir/<config>/link.d` for the cell's
     own configuration, and object or archive operands. It refuses response files (`@file`, which
     Wild expands recursively), linker scripts, plugins and any other option
     that would read an input the manifest does not hash. Each relative
     operand, each manifest path and the build root must already be canonical,
     with no `..`, `.` or empty component, and must lie inside the cell's
     tree, so a traversal cannot hide behind a matching raw string;
   - a timed, validated or repeated link command that is not exactly the
     command rebuilt from that argv's admitted tokens and the frozen options.
     No other input, linker or shell text can run, even in a packet that
     rewrites its argv and commands together;
   - a failed byte-identity, probe or manifest check;
   - a summary median, paired ratio or bootstrap interval that the raw
     samples do not give. The report's cells are re-derived from `raw.csv`;
   - a frozen profile, identity record or cell digest that changed.

   Every timing is report-only.

   Cost: on a hosted 4-vCPU 2.3 GHz Xeon (diagnostic, not Zen 5), the input
   steps took 281 s and the harness 24 s. That run used an `ld.lld`
   stand-in named `wild`, because Wild is not installed there, and GNU ld
   as the substituted comparator. The evidence was 3.4 MB. The input
   deadline (20 minutes) and the harness timeout (15 minutes) together
   keep the leg inside the unchanged 90-minute `compare-pull` job next to the
   roughly 14-minute ordinary comparison. No job timeout changed.

   Licenses. Each one is verified for the version named here. A host
   linker of another version records its own version string, and its
   license is unverified until it is checked against that version's
   source.
   - **Wild 0.10.0:** `MIT OR Apache-2.0`
     ([Cargo.toml at `e9a9b0cb`](https://github.com/wild-linker/wild/blob/e9a9b0cb6fd63a23cc7b93aafc931aadd5746be6/Cargo.toml)).
   - **mold 2.42.1:** `MIT`
     ([LICENSE at `9b376bc6`](https://github.com/rui314/mold/blob/9b376bc6a9899d4a16b41777de1f013989459fbc/LICENSE)).
   - **GNU ld 2.42 (substitution only):** `GPL-3.0-or-later`
     ([`ld/ldmain.c` header at tag `binutils-2_42`](https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=ld/ldmain.c;hb=refs/tags/binutils-2_42)).

   Buster invokes all three as external executables. None is vendored,
   linked into Buster or redistributed.

   **Issue #1915 opt-in TLS companion link leg.** The same route accepts one
   fresh exact line `profile: link-tls-v1`, selected by the same
   every-parent freshness rule. It is the Zen 5 route for
   `tool:link_tls_bench.py`, and the whole-link measurement of the x86-64 ELF
   TLS companion lookup that #2247 indexed. It cannot be combined with an
   analyzer, real-source, ast-pilot, production, compiler-probes,
   frontend-selector or wild-linker profile. `measure_link_tls` runs the
   trusted revision's `tools/link_tls_bench.py` after the corpus leg, on the
   same two `ide` binaries:
   - **Inputs.** It writes the profile's generated sources and builds them
     with the host's `clang` and `ar`: `main.o`, which calls `sum_all` twice
     and prints both sums; `tls:N` objects whose `sum_all` reads and
     increments N global `__thread long` variables (two general-dynamic
     sites each) at N = 1024, 4096 and 16384, with `-O0 -fPIC -g0`; a
     `-fno-plt` variant and a one-member archive of the largest; and a
     no-TLS control of 4096 globals and functions. The summary records every
     input's SHA-256 and both tools' version lines.
   - **Correctness first.** Each binary links every cell once with
     `ide cc {-no-pie|-pie} main.o INPUT -o IMAGE`. Both images must be
     byte-identical, exit 0 and print the cell's frozen output, which follows
     from the source (`LinkTlsTest` checks the arithmetic). A cell that fails
     is not timed.
   - **Sampling.** Frozen before the run: per cell, 2 warmups alternating
     baseline and candidate, then 20 pairs, baseline first on even pairs and
     candidate first on odd ones. Each timed link is a fresh
     `taskset -c 2 ide ...` process. Its wall time runs from spawn to a
     blocking `wait4`, which also gives its own user plus system CPU time. A
     timed wait with polling would quantize short links. There is no adaptive
     stopping.

   `compiler_publish.py` re-validates the receipt's `link_tls` record against
   `link_tls/summary.json` (`validate_link_tls`). It refuses:
   - a harness exit other than 0, or a summary that is not complete;
   - measured binaries that are not the receipt's baseline and candidate, or
     that are the same binary;
   - an input set that is not the frozen one with a SHA-256 for each, or an
     unrecorded clang or ar;
   - cells that are not the frozen cells, in order, with their frozen
     arguments;
   - images that differ between the binaries, fail, or print anything but the
     frozen output;
   - a warmup or pair population that is not exact, in the fixed order, with
     every link exiting 0 and a positive integer wall time within the
     60-second invocation timeout;
   - receipt cells that the raw samples do not give. Each cell's median pair
     ratio and its distribution-free 95% interval are re-derived.

   Every timing is report-only. On a pull request the merge base is the
   baseline. To measure a change that has already landed, the owner opens a
   draft whose head restores the earlier behavior, so the ratio reads
   inverted.

   Cost: on a hosted 4-vCPU 2.8 GHz Xeon (diagnostic, not Zen 5), against a
   pre-#2247 baseline whose scan dominates, the whole leg took 248 s and its
   evidence was 68 KB. Its 20-minute timeout keeps it inside the unchanged
   90-minute `compare-pull` job next to the roughly 14-minute ordinary
   comparison. No job timeout changed.

   **Issue #58 opt-in self-built inline-mode leg.** The same route accepts
   one fresh exact line `profile: self-host-inline-compare-v1`, selected by
   the same every-parent freshness rule. It is the Zen 5 route for
   `uarch_lab:compare@self-host-inline-compare`, the compilers each side
   would self-build with `-fcanonical-inline`. The #48 profile above times
   one head against itself, inliner off against on; this one times base
   against head, both with the inliner on. It cannot be combined with any
   other opt-in profile, the scaling leg included; both the harness and the
   publisher refuse the combination. The #48 `canonical-inline-self-host-v1`
   leg is the exception: `inline_acceptance_requested` selects it whenever its
   line is anywhere in the request file, as it has been on main since #48, so
   it runs beside this leg (its 12-pair stages add a few minutes). The
   trusted revision's `tools/bench_direct/selfhost_inline_compare.py` runs in
   two parts:
   - **Stage 1, untimed.** Right after each role's Clang Release build, with
     that revision and its `build/generated` still checked out,
     `build_selfhost_inline_stage1` runs the role's `ide` once with the
     profile's `stage1_command`: the unity source with `-fcanonical-inline`.
     It refuses a checkout that is not the role's revision, and inputs or a
     compiler that change during the build. Each `stage1.json` records the
     building compiler's SHA-256 and the stage-1 compiler's.
   - **Timed comparison.** After the corpus leg, on the ordinary comparison's
     frozen merge-base tree, `measure_selfhost_inline` runs
     `uarch_lab.py compare` on the two stage-1 compilers with fixed
     arguments: `--pairs 30 --warmups 1 --cpu 2 --profile-steps
     topdown,sampling --require-identical-output --min-effect 0.5 --
     -fcanonical-inline`. The outputs must be byte-identical before anything
     is timed. Topdown runs each Zen 5 metric group (PipelineL1 and L2,
     branch, decoder and op-cache, cache and TLB) for both compilers, and
     the report lists the front-end rows. Sampling adds per-symbol movers.

   `compiler_publish.py` re-validates the receipt's `selfhost_inline` record
   against the seven exported documents under `selfhost_inline/`
   (`validate_selfhost_inline` and
   `selfhost_inline_compare.validate_compare_documents`). It refuses:
   - a stage-1 record not built from its role's revision by the receipt's
     binary with the frozen command;
   - a result that did not time those stage-1 compilers on the merge-base
     tree;
   - a run that is not 30 complete deterministic fresh-copy ABBA pairs with
     the fixed flags, CPU, floor and profile steps;
   - outputs that differ, or a prepare, timed, topdown or sampling step that
     did not complete;
   - missing wall-time intervals, unavailable hardware counters, or missing
     instruction, cycle or branch-miss counts;
   - front-end evidence that is not usable (`selfhost_inline_frontend_problems`):
     a malformed topdown row, no level-1 `frontend_bound` metric with finite
     values for both compilers, or a front-end row measured for only one.
     The topdown step is marked ok even when a group fails or is
     unavailable, so cache or backend rows never stand in for it;
   - a receipt digest that the exported summary does not give.

   Every value is report-only. On a pull request the merge base is the
   baseline; for #58 a draft head carries one selector variant against the
   production selector.

   Cost is an estimate (no hosted host has perf, and no Zen 5 group count is
   on record). Each stage-1 build took 4.7 s with the Clang-built compiler on
   a hosted Xeon, and one self-built compile took about 19 s there; the 9700X
   compiles the same source with a Clang-built compiler in about 1.5 s. That
   puts a self-built compile near 5 s, and 30 pairs, both profiles and their
   multiplexed metric re-runs near 220 compiles, about 25 to 30 minutes. Its
   45-minute leg timeout keeps it inside the unchanged 90-minute
   `compare-pull` job. No job timeout changed; record the first receipt's
   `selfhost_inline_seconds` before relying on the estimate.

   **Without perf (#2768).** Before the first timed run the lab runs
   `perf stat -- true` once (`probe_counters`). If perf is missing, exits
   nonzero (for example under a strict `perf_event_paranoid`), or writes no
   task-clock line, every timed run executes the workload directly. Wall time,
   CPU time (`task_clock`, from wait4's user plus system time) and peak RSS are
   still measured, and the verdict still comes from wall time. Every perf
   counter is NA, never zero. `summary.json` records this in `counters`
   (`perf_stat`, `reason`) and adds a warning. A perf that runs but counts
   zero is still a degraded measurement, not a fallback. `--profile-steps`
   need perf: without it, `compare` stops before the first pair with that
   reason. When no pair completes, `compare` exits nonzero, so a failed series
   cannot pass for a neutral result. A cloud container therefore yields a
   usable, diagnostic wall-time A/B; Zen 5 evidence still needs the 9700X
   routes below.

   **Probe orphans (#3709).** The `perf stat -- true` probes
   (`probe_counters`, the `wrapper_rss_bytes` run in `probe_workload`, and the
   top-down dry runs) make the lab a child subreaper for that one command.
   After perf is reaped, the lab kills the rest of the probe's process group
   and reaps any orphan it left. Each one is named as `orphan reaped:
   pid=N stat=[...]` in `commands.log` and in the probe's log. Workload runs
   never do this, so an outer supervisor still adopts, and charges, anything
   the compiler or its wrapper leaves behind.

   Both binaries are probed for `-fsource-metrics`/`-fmetrics-out` before
   either is warmed up. Compare enables `-fmetrics-out` in both variants'
   warm-ups, pilot and timed pairs only when both support it; otherwise it
   omits the flag from both. Capability probes are separate, untimed compiles.
   The report records support and enabled collection separately: timings
   with collection enabled include metrics instrumentation. Single-binary
   `run` continues collecting whenever its own compiler supports the flag.
   With `run --warmups 0`, one untimed reference compile uses those
   capability flags before measured runs start.
   Each binary is checked for byte-identical output across its own runs; whether A and B
   outputs match is reported (`--require-identical-output` stops before timing
   when they differ, for pure refactors). With `--warmups 0`, each variant
   gets one untimed reference compile under the shared collection policy;
   the capability probe's output is never reused as that reference.
   Runs alternate in ABBA blocks; the
   pair count is `--pairs` or is fixed once after a 2-pair pilot so the whole
   comparison fits `--target-minutes` (profile steps included). Profile steps
   are off by default.

   **Fresh binary copies (default).** Before every timed run and every profile
   capture, in both `compare` and `run`, the tool copies that variant's binary
   by plain read/write into a new file under `DIR/<variant>/instances/`
   (a new inode, never a link or reflink), fsyncs and closes it, runs that
   copy and deletes it; the copy (about 0.07 s for the 51 MB `ide`) happens
   outside the timed span and is not counted by `--target-minutes`. Probes and
   warm-ups run the binary in place. This exists because the LAB3 A/A run on
   the Zen 5 host (#36), with binaries run in place, reported two
   byte-identical copies of one `ide` as different: wall B/A 0.9950, CI
   [0.9941, 0.9958] over 190 pairs, identical instruction counts, cycles
   -0.53%, the offset stable in AB and BA pairs and in every tenth of the
   run. The cause is unverified (page-cache placement of each copy's text,
   perhaps read-only file THP, is the leading hypothesis), but it is a fixed
   per-instance offset that the pair-to-pair CI cannot cover. With a fresh
   copy per run, placement varies per run and lands in the CI instead.
   `--no-fresh-copy` restores the old behaviour (the setting that showed the
   bias; the report then warns). `plan.fresh_copy` records which was used.
   With fresh copies `perf record` runs with `--no-buildid-cache` (each
   copy's unique path would otherwise cache one 51 MB copy per capture in
   `~/.debug`), so symbols are resolved while the copy exists; `report DIR`
   cannot re-derive missing IBS reports of such a run later.
3. Read the first line of `report.md` or `verdict` in `summary.json`. Every
   outcome is judged against the practical floor `--min-effect` (default
   0.5%, `verdict.min_effect_percent`; LAB4's A/A with fresh copies measured
   B/A 0.9995 with 95% CI [0.9986, 1.0003], so 0.5% is about six times the
   interval's half-width on that host):
   - `faster`/`slower`: the whole 95% CI of wall-time B/A lies beyond the
     floor (for 0.5%: `ci_high < 0.995` or `ci_low > 1.005`). The ratio is the
     median of per-pair ratios; its CI comes from sign-test order statistics
     (exact and distribution-free; it assumes only independent pairs and needs
     at least 6). A seeded bootstrap CI of the geometric mean is the
     cross-check; a disagreement is a warning that the effect sits at the edge
     of detectability.
   - `below-floor`: the CI excludes 1.0 but reaches inside the floor. Not
     evidence of a code effect: per-instance effects of about 0.5% were seen
     in A/A on this host class. `bound_percent` is the largest change the CI
     admits.
   - `no detectable difference`: the CI includes 1.0; `bound_percent` says how
     large a change the run could have missed. It is not proof of equality.
     More pairs (or a quieter host) narrow it; the verdict says so when the
     bound is wider than the floor.
   - `inconclusive`: fewer than 6 complete pairs.

   Per-metric and per-phase `outcome`s use the same floor (`lower`/`higher`,
   `faster`/`slower`, `below-floor`), except `instructions`, which is
   deterministic and exact. When an effect is near the floor, run an A/A
   comparison (the base binary against a second copy of itself) on the same
   host first; lower `--min-effect` only with an A/A run that shows the host
   resolves it.

   Only wall time (the harness span) decides. Instructions, cycles, IPC,
   branch MPKI, faults and per-phase times explain the change; a proxy
   change without a wall-time change is reported as a warning, never as a win.
   Treat `warnings` as part of the result: an order effect (AB and BA pairs
   disagree), drift (first and second half disagree), nondeterministic
   output, failed runs or a task-clock/wall disagreement each need a look
   before the verdict is used. "identical work, different time" (instructions
   B/A within 1e-6 of 1 while the wall or cycles CI excludes 1.0) points to a
   placement/instance effect, not a code effect, unless the change only moves
   code. A count metric whose ratio of medians and median of per-pair ratios
   differ by more than 5% carries the note "bimodal counts: compare medians,
   not the paired ratio" (page faults in LAB3: 0.921 against 0.9965).
   **Thread timeline (opt-in).** `run --threads` or `compare --profile-steps
   threads` adds one Superluminal-style capture per binary,
   `DIR/[a|b/]threads/threads.data`, for [Hotspot](https://github.com/KDAB/hotspot).
   Hotspot shows a per-thread timeline, off-CPU time, flame graphs and
   caller/callee views. The capture runs `perf record -F 10000 --call-graph fp
   --switch-events` on the binary in place, so its symbols stay resolvable; the
   Release `ide` keeps frame pointers. `step_threads` takes the richest mode the
   host permits and records why richer ones were refused:
   - `full`: kernel and user `cycles` stacks plus `sched:sched_switch` and
     `sched:sched_wakeup`, which give off-CPU waits and `perf sched timehist
     --summary`. It needs `kernel.perf_event_paranoid = -1`, tracefs readable by
     the account, and `kernel.kptr_restrict = 0` for kernel symbol names.
   - `user`: `cycles:u` stacks and context switches; works at `2`.
   - `cpu-clock`: the same with a software clock, for hosts without a PMU.

   The step is never part of the automated 9700X comparisons, whose policy
   forbids profile steps.

   With `--profile-steps`, the top-down table and the per-symbol share movers
   show where the time moved. Movers come from one capture per variant and are
   hints only: a capture with fewer than 2,000 samples on either side is
   marked "too few samples: shares unreliable" and not diffed (LAB3's dTLB
   capture, 506 and 631 samples, swapped about 25 pp between identical
   binaries), and a row reads `exceeds bound` only when its share moved more
   than 3x the binomial 95% bound (LAB3 A/A movers reached about 5x it). A
   row with one share below the report's percent limit has no bound (`-`).
   Unresolved addresses read `[unknown] 0x...` in every table.
4. Record a result worth keeping with `tools/new_audit.py "..."`, quoting the
   verdict line, both binary sha256s, the command, the pair count and seed,
   and the host state.

`summary.json` keys (golden-tested in `tools/uarch_lab_test.py`; a change of
meaning or a removal bumps the schema id):

- `buster-uarch-lab-compare-v2`: `schema`, `directory`, `command`,
  `repo_root`, `cpu`, `host`, `baseline`/`candidate` (`path`, `sha256`,
  `size_bytes`, `runs`, `failed`, `identical_runs`, `deterministic`,
  `metrics_out`, `metrics_out_supported`, `metrics_out_enabled`,
  `source_metrics`), `phase_metrics` (`enabled`, `reason`), `counters` (`perf_stat`: true, false when
  timed without perf, or null for an older directory; `reason`), `outputs_identical`, `plan` (`pairs`,
  `reason`, `order`, `fresh_copy`, `seed`, `confidence`,
  `bootstrap_resamples`, `complete_pairs`), `method`, `verdict` (`metric`,
  `outcome`, `ratio`, `ci_low`, `ci_high`, `ci_coverage`, `change_percent`,
  `bound_percent`, `min_effect_percent`, `n`, `explanation`, `text`),
  `code_bytes` (exact executable-section totals, formats, sections, diagnostic
  file sizes and B/A ratio), `metrics` and `phases` (per name: `unit`, `direction`, `n`, `a_median`,
  `b_median`, `a_min`, `b_min`, `a_mad`, `b_mad`, `delta`, `ratio`, `ci_low`,
  `ci_high`, `ci_coverage`, `geomean_ratio`, `bootstrap_ci_low`,
  `bootstrap_ci_high`, `ratio_of_medians`, `min_ratio`, `change_percent`,
  `outcome`, `note`; metrics also carry `label`), `checks` (`order_effect`,
  `drift`), `profile` (`topdown`; `sampling`/`ibs` with `status` and per
  capture `a_event_count`, `b_event_count`, `a_samples`, `b_samples`,
  `reliable`, `note`, `movers`: `symbol`, `a_share`, `b_share`,
  `delta_share`, `noise_pp`, `beyond_noise`, `exceeds_bound`, `a_estimate`,
  `b_estimate`, `delta_estimate`), `steps`, `warnings`. v2 (LAB3) changed
  the meaning of `outcome` (the practical floor applies, and `below-floor` is
  a new value) and of `noise_pp` (null when one share is NA), and added
  `plan.fresh_copy`, `verdict.min_effect_percent`, `note`, `reliable` and
  `exceeds_bound`; `report DIR` renders an older directory as v2, with
  `fresh_copy` false and the default floor. Metric names: `wall`, `task_clock`, `compiler_wall`,
  `instructions`, `cycles`, `ipc`, `branch_misses`, `branch_mpki`,
  `page_faults`, `minor_faults`, `major_faults`, `peak_rss`; phases are the
  `-fmetrics-out` phases plus `total` (ms), or null.
  The additive collection fields distinguish an accepted flag
  (`metrics_out_supported`) from the flag actually enabled for warm-ups and
  timed pairs (`metrics_out_enabled`); the existing `metrics_out` field
  retains its meaning of a measured `CC_METRICS_INPUT` capability probe.
  `compare.json.phase_metrics` saves the shared policy and each variant's
  `lab.json.collection.metrics_out` saves its enabled setting separately
  from `capabilities`. Older directories have null enabled fields and an
  unknown policy; re-rendering never retroactively claims matched flags.
- `buster-uarch-lab-run-v1`: `schema`, `directory`, `command`, `cpu`, `ide`
  (`path`, `sha256`), `host`, `capabilities`, `steps` (`status`, `problems`),
  `timed` (`runs`, `failed`, `identical`, `plan` (with `fresh_copy`), `metrics` with the metric
  names above, each `n`, `min`, `p10`, `median`, `p90`, `max`, `mean`, `mad`,
  `unit`, `label`), `phases` (`median_ms`, `share`), `work`, `topdown`
  (`group`, `metric`, `value`, `unit`, `source`),
  `dominant_topdown_category`, `hot_symbols` (per capture: `symbol`,
  `share`), `findings`.

## Native-retirement gate (#512)

This is the frozen historical four-mode acceptance protocol. Execution requires
archived compilers supporting `none`, `mir-stack`, `fast` and `quality`;
the current compiler accepts only FAST and QUALITY. Use `uarch_lab.py compare`
for current compiler measurements, and `report` for retained retirement results.

The [maintainer decision](https://github.com/buster14a/buster/issues/36#issuecomment-5969534074)
defines five required cells: a stage-1 self-host compile in each of `none`,
`mir-stack`, `fast` and `quality`, then the generated-runtime cell. The latter
executes the two stage-1 compilers produced by the `fast` cell, compiling the
same frozen source with `fast`. This measures a real generated program's
runtime while checking that its two outputs agree byte for byte.

Build and freeze matched Clang Release baseline (`main`) and candidate
(#522, then the final #514 tree) compilers using the provenance rules above.
Run on `benchpress` CPU 2 with the recorded H1 state, dispatch disabled,
no competing work and the exclusive host lock. Keep the source checkout and
`build/generated` unchanged for the entire campaign. Record their identities,
the build commands, both compiler hashes and the host facts with the result.

```sh
flock -n ~/bench/host.lock python3 tools/uarch_lab.py retirement \
    --baseline /tmp/ide-base --candidate /tmp/ide-cand --repo-root . \
    --cpu 2 --output /tmp/retirement-attempt-1 \
    --baseline-rev BASE_COMMIT --candidate-rev CANDIDATE_COMMIT
python3 tools/uarch_lab.py report /tmp/retirement-attempt-1
```

Each cell uses fresh binary copies and ABBA paired runs. The default is about
12 minutes per cell, with the pair count fixed after its timing-only pilot.
`--pairs N` selects a fixed-count smoke/test plan. `--modes fast` exercises a
partial run, including generated runtime; missing required modes keep the
overall result INCONCLUSIVE. Every attempt needs a new or empty output
directory so earlier observations remain intact.

| Metric | Per-cell B/A limit |
| --- | --- |
| Compiler wall time | Upper 95% sign-test bound at most 1.05 |
| Compiler peak RSS | Upper 95% sign-test bound at most 1.05 |
| Generated code-section bytes | Exact ratio at most 1.01 |
| Generated-program runtime | Upper 95% sign-test bound at most 1.03 |

`retirement.json` (`buster-uarch-lab-retirement-v1`) and `retirement.md`
contain PASS, FAIL or INCONCLUSIVE plus every cell's checks. A passing numeric
result requires the full planned sample population, successful fresh output
production, deterministic outputs, no flagged order/drift problem and all
upper bounds within their limits. A missing sample, unavailable metric or
interval crossing a limit is inconclusive. The ordinary `compare` practical
effect floor does not change these acceptance limits. The historical 1.02
aggregate figures are diagnostic under the decision's per-cell contract.

Peak RSS uses Linux `wait4` high-water bytes for the waited process tree.
The recorded current harness footprint and a `perf stat -- true` probe bound
wrapper overhead; values within 1.5 times that floor are unavailable rather
than reported as compiler memory. Historical harness peaks are not used as
the inherited footprint. Code bytes sum file-backed ELF sections with
`SHF_EXECINSTR`; ELF32/ELF64 and either byte order are covered. Unsupported
formats and malformed sections remain unavailable; whole-file bytes are
diagnostic. Both metrics also appear in ordinary `compare` reports.

The lab's PASS describes its measured cells. Independently run the candidate's
repository self-host fixed point and retain its byte-identical evidence.
After #514 lands, repeat the whole gate on the final integrated source,
retain all attempts and record the accepted result with `tools/new_audit.py`.
Those source, host and fixed-point checks are part of #512/#36 acceptance.

## Performance audit notes

Audit history lives in `docs/performance-audits/`, one file per audit, and
`PERFORMANCE_AUDITS.md` explains it — it is kept out of this file because it
is a growing record rather than a rule. **Read the newest audit before any
performance work** (`tools/new_audit.py --newest` prints its path): it holds
the reference numbers a new measurement is compared against (stage 1
instructions, sanitized and Release `ide test`, `ide bench` medians), the finds
that were deliberately left untaken, and the traps an earlier audit already
paid for. Record a new audit there, not here; this file keeps the method, that
directory keeps the history.

An audit is a **new file**, `docs/performance-audits/<id>.md`, and nothing
else. Never append an entry into an existing audit file, never rewrite one, and
never add a line to the index in `PERFORMANCE_AUDITS.md`: it is closed at the
id in its heading. Every audit used to add a line at its top, so any two open
audit branches conflicted as soon as either landed. `merge=union` in
`.gitattributes` cleared that only for a local merge, because GitHub's
mergeability check does not apply it, and the union merges duplicated lines and
lost their order. Timestamp ids sort chronologically, so the directory orders
every audit past the closing id. `tools/new_audit.py --list` prints the whole
history newest first, and `--check`, which CI runs, verifies the index and the
directory agree.
Their output can be piped to a consumer such as `head`: a closed output pipe
ends quietly, while unrelated I/O failures remain errors.

The id is the **UTC timestamp at which the audit is recorded**,
`2026-08-22T140351Z` — ISO 8601 with the colons dropped, because Windows
forbids them in filenames. `tools/new_audit.py` mints one and creates the
file; use it rather than typing an id by hand, because the id is the one field
two concurrent sessions can independently choose the same value for. Audits
before 2026-08-22 are named by date plus a sequence letter
(`2026-08-08k`), and that is precisely what collided: the letter is picked by
counting the day's existing entries, so two sessions auditing the same day
always picked the same letter, and three of the four PRs open when the history
was split had done exactly that. Those older names are historical — entries
cross-reference each other by them — and stay as written.

Raw evidence under `docs/performance-audits/evidence/` is **byte-exact**: tool
output, logs, and `git format-patch` files are stored as produced, and a
bundle's checksum manifest (`SHA256SUMS`) pins those bytes. Such files carry
trailing whitespace by nature — a format-patch signature separator is `-- `
and the file ends in a blank line — so `.gitattributes` marks that directory
`-whitespace` and `git diff --check` does not report it. Never strip, reflow, or compress evidence
to satisfy a whitespace check. The exemption covers that directory only: the
audit prose in `docs/performance-audits/<id>.md` is authored text and remains
subject to `git diff --check`.

## Source-map finalization

`BUSTER_TEST_JOBS=1 BUSTER_SOURCE_MAP_BENCH=1 build/Release/ide test --ci=1 --verbose=0`
adds record-level finalization measurements to the registered C frontend tests.
Use the trusted Clang Release build. Ordered, equal, reverse, permuted and
byte-boundary key populations cover 1,024/4,096/16,384/65,536 regions. Each has
one warmup and seven samples; construction, payload/stability checks and scratch
poisoning are outside the measured interval. `BENCH_SOURCE_MAP` reports the
production sort's nanoseconds, row size and arena high-water growth, including
scratch allocation and rewind. Arena bytes are not process peak RSS. Ordinary
tests retain bounded populations and no timing thresholds.

Keep these algorithm measurements separate from complete preprocessing with
`ide cc -E` and the native compiler-throughput harness. The source-location
regression separately covers fixed `#line` transitions with macro arguments,
stringification, token pasting and builtin spellings.

## ABI context microbenchmark

Run `BUSTER_ABI_CACHE_BENCH=1 build/Release/ide test --ci=1 --verbose=0` to
include the `ABI_CACHE_BENCH` record from the canonical IR tests. It uses the
existing scalar/aggregate classifier corpus and reports reservation, cold-cache,
warm-cache and uncached-classifier time separately. The warm query count and
unchanged classification count establish the hit rate; timings never gate tests.
`bytes` includes page payloads and the TypeId page directories, and `type_bytes`
is the occupied language-type pool. This small corpus prices the query service;
use paired same-source compiler runs for end-to-end time and peak RSS.

## Canonical construction work populations

For #38/#50/#52/#306, use the existing allocation diagnostic build and its
additive `ir_construction.*` source-metrics fields. The
[construction census contract](../../tools/throughput/README.md#canonical-construction-census-in-allocation-probes)
defines append/growth, frontend finish, place retraction and shared native
edge populations. Keep this instrumented compiler off the timing path,
require its output hash to match the ordinary compiler, and retain raw
metrics. These calling-thread counters do not aggregate persistent lanes,
time appends, or cover every operand decoder. They do not establish a
Zen 5 speedup, live memory reduction, or whole-pipeline cost.

## Whole-compiler work ledger

The same diagnostic build also writes `work.*` counters: exact, mechanism-grouped
work counts (re-derived queries, rollback snapshots, whole-table per-query
work, literal decoding, interning, target-table preparation, machine records,
output bytes) and per-phase minor-fault and arena rows. Use them to rank and
prove removed work, never as timing evidence; see [the work ledger](../work-ledger.md)
for the key contract and the frozen-corpus runner under `tools/work_ledger/`.

## Parameter-edge splitting populations

`tools/machine_edge_split_scaling.c` drives the production pre-allocation
parameter-edge splitter with reversed-edge chain/fanout populations, mixed
duplicates (one non-copy edge followed by two splitting edges), and a no-split
control. It checks complete transformed block, instruction, edge and switch
records against closed-form expectations, including copy offsets and the first
splitting duplicate. Its empty/tiny controls and geometric populations cover
both native MIR targets. These are structural fixtures with synthetic copy
slices, not independently executable programs or allocator-validation inputs;
the registered `machine_tests` supply broader label/debug/definition coverage.

Build the probe from the repository root with the existing Clang/LLVM tools:

```sh
mkdir -p work/edge-split
clang -O2 -g -fprofile-instr-generate -fcoverage-mapping \
  -ffunction-sections -fdata-sections -Wall -Werror \
  -Wno-unused-function -Wno-unused-variable \
  -DBUSTER_INCLUDE_TESTS=0 -DBUSTER_LINK_LIBC=1 -DBUSTER_OPTIMIZE=1 \
  -DBUSTER_SINGLE_THREADED=1 -Isrc -fwrapv -fno-strict-aliasing -funsigned-char \
  tools/machine_edge_split_scaling.c \
  src/buster/lib/compiler/codegen/machine.c src/buster/lib/compiler/codegen/codegen.c \
  src/buster/lib/arena.c src/buster/lib/os.c src/buster/lib/integer.c \
  src/buster/lib/string.c src/buster/lib/float.c src/buster/lib/time.c \
  -Wl,--gc-sections -lpthread -ldl -lm -o work/edge-split/probe
python3 tools/machine_edge_split_scaling_coverage.py \
  --binary work/edge-split/probe --output work/edge-split/attempt-1
```

The collector requires a fresh output directory and checks 56 cells: both
targets, four shapes and 0/1/2/17/257/4097/16384 blocks. It locates source
statements within the splitter and reads their actual LLVM execution regions,
then checks the expected linear counts. Missing profiles/regions or mismatches
fail; the expected formula never substitutes for an observed count. Preserve
the raw `.profraw`/`.profdata` files beside `counts.json` for replay. Source,
probe, collector and binary hashes bind the diagnostic to its inputs; rebuild
the probe after any source change. This uses LLVM tooling outside the compiler
and adds no production counters, rows or permanent indexes.

Each invocation also checks retained allocation and scoped arena high water.
For a splitting function the temporary maps use
`4 * (I + 2E + 3B + 1)` bytes plus alignment; the arena cursor must retain only
the published arrays after the call. A no-split function must preserve its
entire function record with zero allocation/high-water growth and no indexing
walks. Timings include the complete splitter call but exclude fixture creation
and checking. Coverage instrumentation perturbs code and timing; these tools
are diagnostic only. They are not yet a frozen `compiler-probes-v1` leg or a
9700X request route and cannot establish a speedup or close #296's physical
Zen 5, separate Zen 4, whole-compiler or text-budget acceptance cells.

## Object assembly-printer scaling

`BUSTER_TEST_JOBS=1 BUSTER_OBJECT_ASSEMBLY_BENCH=1 build/Release/ide test --ci=1`
adds generated-object printer replays to the registered object tests. Use the
trusted Clang Release build. Ordinary tests cover the small cases without a
timing gate; the opt-in run adds 1,024/2,048/4,096/8,192 definitions in ordered,
reverse and permuted table order. A population N has N six-byte call/return
functions, N function pointers, 2N+1 symbols and 2N relocations. Each case has
one warmup and seven reported samples.

`BENCH_OBJECT_ASSEMBLY` reports wall nanoseconds for `object_print_assembly`,
including index construction, sorting, formatting and scratch rewind. Source
construction, output hashing and optional file writes are outside that interval.
`retained` is output-arena position growth; `peak` is its high-water growth,
including temporary indexes and labels. These are arena bytes, not process RSS.
`bytes` and the deterministic FNV-1a `hash` describe the complete output.
Set `BUSTER_OBJECT_ASSEMBLY_OUTPUT` to an existing directory to save each first
output as `assembly_N_ORDER.s` for exact baseline/candidate comparison. Run from
the repository root, and use separate directories for the two binaries.

Keep this isolated printer replay separate from the native throughput harness's
`--artifact assembly` whole-process measurements. The #116 audit retains a
supplement using the same collector for geometric call/function-pointer C
inputs, object controls, alternating A/B pairs and separate PMU probes.

## ELF imported-data scaling

`BUSTER_TEST_JOBS=1 BUSTER_ELF_DATA_BENCH=1 build/Release/ide test --ci=1`
adds geometric imported-data replays to the registered link and driver tests.
Use the trusted Clang Release executable. Ordinary tests keep the bounded
cases and never gate on timing.

`BENCH_ELF_DATA` times the complete ELF writer, including index construction
and scratch rewind, with generated library export tables and object symbols.
Shape 0 scales separate objects, paired imports, duplicate aliases, unrelated
globals and distinct versions; shape 1 scales one object's duplicate aliases.
There is one warmup and seven samples through 4,096 entries. `temporary` is
the high-water byte count of the dedicated index arena; `retained` is output
arena growth, including writer work arrays and the image. Neither is RSS.
Output size/hash, copy counts, slot sharing, sizing and version binding are
checked outside the interval. Repeated dirty-arena runs must agree exactly.

On native Linux, `BENCH_ELF_DSO` builds real shared libraries and native
objects with the configured host compiler. x86-64 uses `-fPIE`; AArch64 uses
`-fno-pic -fno-pie` and dereferences volatile pointers after address-taking
to exercise supported page/add copy relocations without GOT or LDST64
low-page relocations. Shape 0 has N data objects, 3N
data exports, 2N data imports, N function imports and N unrelated globals.
Shape 1 has one data object, N aliases, N+1 data imports and one function
import. N is 4, 128, 256, 512, 1,024, 2,048 or 4,096. Construction and the
system-linker reference are outside the one-warmup/seven-sample driver timer.
On both architectures the reference compiles the same C as PIC and links a
PIE, so its alias identity comes directly from the DSO, independently of
system-linker COPY-slot allocation. Relinking the x86-64 `-fPIE` object with
`-no-pie` does not provide that under GCC: its direct imported-data references
give each strong alias a separate COPY slot (GitHub #594). Buster still links
and executes the host-compiled `-fPIE` or non-PIC object.
The timed invocation reads the object and DSOs, links and writes its image;
`retained` measures the invocation arena after that work. Both native images
execute afterward, verifying shared addresses and writes visible through the
DSO's own references. Set `BUSTER_ELF_DATA_OUTPUT` to an output directory to
retain generated C, shared libraries, objects and images for paired compiler
runs and independent ELF inspection. Tests use temporary paths by default.

Keep whole-process paired timing/PMU probes separate from these in-process
replays. Compare identical saved objects/libraries, target and flags; require
byte-identical baseline/candidate ELF images before interpreting a speedup.

## QUALITY pin scratch replay

`BUSTER_TEST_JOBS=1 BUSTER_QUALITY_SCRATCH_BENCH=1 build/Release/ide test --ci=1`
adds seven measured full QUALITY placement replays after one warmup for the
existing `wide_live_loop` fixture. The same selected MIR is used with 0, 4,096
and 65,536 unused value IDs before its live values. `BENCH_QUALITY_SCRATCH`
reports allocator nanoseconds, retained output-arena bytes, rows, values and
accepted pins; construction, encoding and exact placement/byte comparisons
are outside timing. Retained bytes exclude temporary scratch and are not RSS.
The ordinary regression covers all six desktop targets and both frontend
forms, including dirty scratch reuse and returning to compact IDs. Compare
the same test harness with baseline and candidate allocators; use the native
throughput harness separately for whole-process time/RSS.
