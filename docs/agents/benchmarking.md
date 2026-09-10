# Benchmarking and performance audits

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Benchmarking and diagnostics

- **`./build.sh bench_throughput`** provides deterministic startup, scaling,
  symbol, CFG, backend and frozen-source self-host workloads with raw paired
  timing/RSS, separate PMU/allocation probes and a conservative CI guard.
  See [`tools/throughput/README.md`](../../tools/throughput/README.md) for the
  experiment contract, reproducible commands, statistical assumptions and limits.
  This does not replace the canonical self-host/correctness gates below.
  The same optional allocation observer can emit a [per-site census](../allocation-census.md)
  with separate zeroing, alignment and OS request totals for offline analysis.

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
  output side is one linear sum over the finished token stream. The
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
  <json-path>... [--limit N]`** (both new `build/build` commands, same
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
- **The local Release tree is profilable as built.** `BUSTER_DEBUG_INFO` and
  `BUSTER_FRAME_POINTERS` are both on by default outside `--ci`, so
  `./build.sh build --config Release -t ide` produces a `-O3` binary that
  symbolizes to source lines and unwinds through `--call-graph fp`, the same
  cheap unwinding the sanitized Debug tree allows. Superluminal reads it
  directly. Record it like any other build:

  ```sh
  ./build.sh build --config Release -t ide
  perf record -F 999 -g --call-graph fp -o release.data -- ./build/Release/ide bench
  ```

  The `perf script`/`llvm-symbolizer` rules below apply unchanged; the Release
  binary is a clang PIE like the Debug one. Pass
  `-DBUSTER_FRAME_POINTERS=OFF` when the point of the measurement is the
  frame-pointer cost itself, or when reproducing a CI Release number exactly.
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

## Performance audit notes

Audit history lives in `PERFORMANCE_AUDITS.md`, newest first — it is kept out
of this file because it is a growing record rather than a rule. **Read its
newest entry before any performance work**: it holds the reference numbers a
new measurement is compared against (stage 1 instructions, sanitized and
Release `ide test`, `ide bench` medians), the finds that were deliberately
left untaken, and the traps an earlier audit already paid for. Record a new
audit there, not here; this file keeps the method, that file keeps the
history.

An audit is a **new file**, `docs/performance-audits/<id>.md`, plus one line at
the top of the index in `PERFORMANCE_AUDITS.md`. Never append an entry into an
existing audit file and never rewrite one: the split exists so that two audit
branches open at once touch disjoint files, which is what the single prepended
history could not do. The index line is the only shared text, and
`.gitattributes` marks `PERFORMANCE_AUDITS.md` `merge=union` so concurrent
inserts keep both lines rather than conflicting — after such a merge, check
that the newest id is on top, because union does not know which line is newer.

The id is the **UTC timestamp at which the audit is recorded**,
`2026-08-22T140351Z` — ISO 8601 with the colons dropped, because Windows
forbids them in filenames. `tools/new_audit.py` mints one, creates the file and
inserts the index line; use it rather than typing an id by hand, because the id
is the one field two concurrent sessions can independently choose the same
value for. Audits before 2026-08-22 are named by date plus a sequence letter
(`2026-08-08k`), and that is precisely what collided: the letter is picked by
counting the day's existing entries, so two sessions auditing the same day
always picked the same letter, and three of the four PRs open when the history
was split had done exactly that. Those older names are historical — entries
cross-reference each other by them — and stay as written.

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
