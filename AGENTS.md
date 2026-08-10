# AGENTS.md

Instructions for coding agents working in this repository. When this document
and the code disagree, the code wins — update this file in the same change.

## Project

**buster** is a from-scratch IDE and compiler for the buster programming
language (`.bbb` files), written entirely in C with zero third-party
dependencies. One executable, `ide`, contains everything: UI toolkit, GPU
renderers (Vulkan/Metal/D3D12), TrueType rasterizer, compiler frontend, IR,
codegen backends (x86_64, aarch64), linker, and the in-process test suite.
Targets: Linux, macOS, Windows, Android, iOS.

## Current compiler priority

Prioritize compiler throughput and producing an artifact as soon as possible.
Do not add optimization passes or spend compile time improving generated code
unless that work is explicitly requested. Prefer direct, predictable lowering
and code generation with minimal analysis overhead.

## Self-hosting — reproduce first

All contributors—humans and coding agents—should reproduce the current
self-hosting fixed point from the repository root before changing the compiler:

```sh
./build.sh test_self_host --config Release
```

`test_self_host` builds the trusted bootstrap compiler, compiles the complete
unity-build IDE twice with buster, requires both generations to be
byte-identical, and runs the stage-2 benchmark. The same build.c-owned workflow
is also exposed as the `test_self_host` Ninja target. On Linux x86-64, its
expanded equivalent is:

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -fsource-metrics=build/ide-self.metrics src/buster/apps/ide/ide.c -o build/ide-self
build/ide-self cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -fsource-metrics=build/ide-self-stage2.metrics src/buster/apps/ide/ide.c -o build/ide-self-stage2
cmp build/ide-self build/ide-self-stage2
build/ide-self-stage2 bench
```

On macOS, both `ide cc` invocations additionally receive the SDK returned by
`xcrun --sdk macosx --show-sdk-path` through `-isysroot`, plus `-framework`
arguments for AppKit, Metal, QuartzCore, and Foundation.

The target and fixed point are currently available on Linux and Windows x86-64,
and on macOS. Preserve them when changing preprocessing, C semantics, IR, code
generation, object writing, or linking, and report both self-hosting failures and benchmark
regressions.

## Build

Three layers: `./build.sh` bootstraps `build/build` from `build.c` using
**tcc**, which then drives CMake + ninja (multi-config, outputs in
`build/<Config>/`).

Keep build orchestration and policy in `build.c`, with the least practical
process-launch and scripting overhead. Shell and PowerShell scripts exist only
to bootstrap `build/build`; do not grow them into build systems. Use CMake only
to generate/cache the platform build graph and let Ninja execute that graph;
do not implement workflows, iteration, comparison, parsing, timing, or other
general scripting in the CMake language when `build.c` can do the work
directly. Prefer one persistent native build-driver process over chains of
shell, CMake, and utility subprocesses.

```sh
./build.sh                          # configure + build (Debug, clang)
./build.sh build -t test_all        # build and run the full test suite
./build.sh build --config Release -t test_all
./build.sh generate --sanitize && ./build.sh build -t test_all   # sanitized run
./build.sh test_all_combinations    # the full local matrix CI runs
```

`build/build` commands: `generate`, `build` (default), `clang_analyze`,
`cmake_profile_summary`, `ninja_log_summary`, `time_trace_summary`,
`time_trace_summary_self_test`, `test_timing_summary`,
`test_timing_summary_self_test`,
`import_assembly_metadata`, `test_self_host`, `test_all_combinations`,
`test_all_combinations_ci`; `self_host_from_existing` is an internal
build-driver worker command used only by the pooled artifact-fanout target.
The combination matrix shares one multi-config build tree across Debug and
Release when their configure-time policy matches. Clang's fuzz-enabled Debug
sanitized and Release non-sanitized configurations use dedicated trees;
dedicated trees generate only the configuration they use, while shared trees
keep Debug and Release in one cross-config Ninja graph. Sanitizer rows are
otherwise Clang-only. GCC and Zig cover unsanitized Debug and Release, and
MSVC covers Debug and Release on Windows. Only optimized, unsanitized
Clang/AppleClang builds use the requested unity build; GCC, Zig, MSVC, and
every other non-Clang compiler use split translation units in Release as well
as Debug. TCC is retained only as the bootstrap compiler for `build.c` and is
omitted from all application/compiler combinations.
The matrix configures its compiler trees in parallel, then uses
`cmake/superbuild/CMakeLists.txt` for one outer `cmake --build` invocation.
Each shared compiler tree builds Debug and Release through one cross-config
Ninja process, so concurrent builds never write the same `.ninja_deps` or
.ninja_log`. One shared CMake job pool admits the outer compiler and test
commands, so a completed tree can release its slot to its tests while other
trees continue compiling. On hosts with four or fewer logical CPUs, it admits
one tree per CPU and sets every inner Ninja and `BUSTER_TEST_JOBS` quota to
one; the seven-tree Windows matrix therefore runs four one-job compiler trees
at a time without nested oversubscription. Larger hosts retain the weighted
allocator: split trees share at least two logical CPUs per admission slot while
unity trees use one job. Tests then run concurrently in the same bounded pool,
with each tree's quota passed through `BUSTER_TEST_JOBS`; future multithreaded test work
must honor that limit. Application builds are multithreaded by default;
`./build.sh generate -DBUSTER_SINGLE_THREADED=ON` is the explicit serial
fallback. Compiler-internal lane widths honor `BUSTER_TEST_JOBS` when it is
present. Trees are declared longest-first — sanitized Debug,
sanitized Release, the unity Release tree that also runs `clang_analyze`, trees
covering two configurations, then the rest — because Ninja admits ready edges
from a shared pool in declaration order and a fresh CI checkout has no
`.ninja_log` for its critical-path scheduler to learn from. Set
`BUSTER_MATRIX_DIRECT=1` only to diagnose the retained legacy scheduler,
`BUSTER_MATRIX_NO_TREE_ORDER=1` to restore the previous declaration order, and
`BUSTER_MATRIX_THREADS=<n>` to state a CPU budget instead of the detected one
(`get_nprocs()` ignores CPU affinity, so `taskset` alone cannot reproduce a
small runner's admission behavior). The last two exist so the ordering can be
A/B measured on one host. When artifact fan-out is enabled on the supported
desktop CI platforms, the canonical trusted Clang Release tree also gets a
self-host worker in this same pool. The build-driver boundary is mandatory:
capture provenance, clean the canonical Release producer, then start the outer
superbuild. The clean preserves the configured CMake/Ninja graph and cache while
forcing producer objects, links, and generated shader outputs to rebuild under
the captured inputs. The worker depends only on the canonical compile target,
consumes the producer's one-shot integrity-checked compiler/tool/cache/graph/
environment provenance record before validating and snapshotting the artifact,
runs the existing direct fixed-point workflow without starting an inner Ninja
process, and remains disabled for the direct scheduler and unsupported platforms.
Each self-host stage and the stage-2 benchmark are bounded at ten minutes
(`SELF_HOST_TIMEOUT_SECONDS`): the work is fixed and costs seconds, so a stage
that does not finish is a compiler that never will, and waiting on one wedges
a serialized CI runner for hours while Ninja buffers the edge's output and the
log says nothing. On expiry the child is killed and the run fails naming the
stage and its command line. Every other run waits indefinitely, because their
cost scales with what they are given.
CI Release builds use `-O2`; local Release builds retain the toolchain default.
Local builds make the optimized tree profilable, which CMake's defaults do not:
`BUSTER_DEBUG_INFO` emits debug information in the configurations that carry no
`-g` (`Release`, `MinSizeRel`) and `BUSTER_FRAME_POINTERS` adds
`-fno-omit-frame-pointer` to every optimized configuration. Both default to off
under `--ci` and on everywhere else, and both are overridable with
`./build.sh generate -DBUSTER_DEBUG_INFO=ON|OFF -DBUSTER_FRAME_POINTERS=ON|OFF`.
`Debug` and `RelWithDebInfo` already carry `-g` from the CMake defaults, `Debug`
is `-O0` and keeps frame pointers anyway, and sanitized trees keep their own
reduced `-gline-tables-only` mode, so neither option disturbs them. MSVC is
excluded from the frame-pointer option because `/Oy` has no x64 meaning.
Measured on Clang 22/Linux x86-64: debug information does not change code
generation and costs compile time and artifact size only (unity Release
`ide.c` 37.3 s -> 46.5 s, `ide` 11.4 MiB -> 19.1 MiB); frame pointers do change
code generation but cost 0.065% of instructions on a unity self-compile
(29.5037 G -> 29.5227 G) with no wall-clock difference above run-to-run noise
and no change to the parser benchmark. CI opts out of both because it profiles
nothing and pays the compile time.
Clang static analysis runs only against unsanitized Release. GUI/GPU smoke
tests run for Debug sanitized and Release non-sanitized configurations; other
combinations run unit tests only.
Flag scope matters: `--sanitize`, `--fuzz`, `--lto`, `--ci`, `--time-trace`,
`--instrument`, `--cc <clang|gcc|zig|cl>` are accepted **only by
`generate`** for public workflows; `build` rejects them with an explicit
diagnostic. The internal `self_host_from_existing` worker is the narrow
exception for build-driver-supplied `--ci/--no-ci` and
`--fuzz/--no-fuzz`, and requires the captured provenance record.
TCC is reserved for compiling `build.c` through `build.sh`/`build.ps1` and
`generate --cc tcc` is rejected as an application compiler.
`--optimize`/`--no-optimize` are configuration shorthands for
Release/Debug and never create separate cached optimization state. `build`
accepts `--config <name>`, `--optimize`, `--target/-t <ninja target>`, and
`--verbose/-v`. Booleans have `--no-` twins; `--` passes the rest through to
Ninja (`build`) or CMake (`generate`).

The Clang-like `ide cc` driver accepts `-march=<model>` and
`-mcpu=<model>` (or their separated forms), ordered target-feature overrides
through `-mattr=+feature,-feature`, and x86 assembly dialect selection through
`-masm=att|intel`. CPU and feature options also accept separated values. CPU names use the canonical
spellings printed by `cpu_model_to_string_os`, such as `baseline`, `native`,
`haswell`, `znver5`, and `apple-m4`; incompatible target/model pairs are
diagnosed. `-v` reports the selected CPU, the sorted effective feature set,
and maximum native vector width. `-target`/`--target` strings are
`arch[-vendor][-os][-environment]`: the vendor and environment components stay
free-form, but a CPU model there is rejected in favor of `-march=`, and so is
anything past the fourth component. Both used to be dropped silently, which
left baseline code generation and no hint that the request was ignored.

Ninja targets: `ide`, `test_all` (on Android packages/runs the APK, on iOS
drives the simulator), `bench_all` (desktop only — runs `ide bench`),
`test_self_host` (Linux x86-64 and macOS), `run_ide`, `test_ide`, `debug_ide`,
`buster_shaders`, `apk` (Android), `clang_analyze`. The Vulkan SDK
(`VULKAN_SDK` env) is required whenever Vulkan or Slang shader compilation is
enabled.

## Tests

- All tests use the `ide` executable with no external test framework.
  `test_all` runs `ide test --verbose=1`, which calls `library_tests()` in
  `src/buster/tests/test.c`; fuzz-capable builds then run their bounded fuzz session
  in that process. Desktop CI runs the IDE window/rendering path separately as
  `ide test_app --verbose=1 --ci=1`, so external Vulkan/LLVM sanitizer policy
  cannot weaken unit-test or fuzz coverage. Android and iOS retain the combined
  in-app test and counted graphical smoke-test flow.
- Module tests live under `src/buster/tests/` as mirrored `*_test.c`/`*_test.h`
  pairs. `test.c` includes their headers and owns registration. In unity builds
  it also includes their implementations before the production sources; in
  non-unity builds CMake compiles `test.c` and every test implementation as an
  independent translation unit. Production sources expose only the narrow
  seams needed by tests. Each `*_test.h` has one `BUSTER_INCLUDE_TESTS` block
  around its test-only declarations and types. Each paired `*_test.c` keeps its
  own header include outside one block guarding the rest of the file, so
  tests-disabled non-unity builds still compile a non-empty translation unit.
  Private data shared by codegen and interpreter tests belongs in their
  `*_internal.h` headers under `src/buster/lib/`.
- Run from the **repo root**: parser tests open `tests/*.bbb` by relative
  path.
- To add a language test: drop a `.bbb` file in `tests/` **and** append it to
  the hardcoded `parser_file_test_cases` list in
  `src/buster/lib/compiler/frontend/buster/parser.c` (covered by
  `parser_file_tests()`) **and** bump `PARSER_FILE_TEST_CASE_COUNT` in
  `parser.h` to match — a compile-time equality check beside the array catches
  drift in every build. Valid fixtures must also be appended to
  `analysis_fixture_tests` in
  `src/buster/tests/compiler/frontend/buster/analysis_test.c` with their exact
  semantic diagnostic count and to `ir_fixture_tests` in
  `src/buster/tests/compiler/ir/ir_test.c`; this keeps the complete frontend
  pipeline covered. Invalid-syntax fixtures live in `tests/errors/` and use
  the parser list with exact expected diagnostics plus an expected recovered
  AST expression. Commented-out entries there are known-failing/WIP.
- CI (`.forgejo/workflows/ci.yml`, Forgejo not GitHub) runs
  `./build.sh test_all_combinations_ci` on Linux/macOS/Windows plus
  Debug+Release on an Android emulator and the iOS simulator, on every push.
  Main-push CI is skipped only when the exact pushed SHA already carries current
  success statuses for all four required matrix contexts; the check lives in
  `.forgejo/scripts/ci_main_gate.sh` and uses Forgejo's commit-status API, so a
  merge strategy other than fast-forward cannot silently land an untested tree.
  The run being gated publishes its own `pending` statuses for those contexts
  before the gate job starts, so the gate reads the newest *terminal* status per
  context and ignores `pending`; reading the newest status of any kind only ever
  sees the gate's own run and can never skip. Every other outcome — a missing
  context, a non-success verdict, an API failure, or a gate job that failed
  outright — runs the full matrix.

## Benchmarking and diagnostics

- **`test_self_host` is the most trustworthy and complete compiler benchmark.**
  It exercises the full self-hosting IDE pipeline, including the trusted
  bootstrap, two complete unity-build IDE compilations, byte-identical stage
  verification, and the stage-2 benchmark. Use its end-to-end result as the
  primary completeness/fixed-point signal for compiler-throughput changes.
  For performance measurements, run the benchmark with the trusted
  Clang-compiled `ide` executable, whose generated host code has the best
  quality; buster-compiled stage executables are useful for self-hosting
  validation, not as the default benchmark host.
- `ide bench` (built via the `bench_all` ninja target) runs both modes over the
  `parser_file_test_cases` corpus 200 times and prints one line per mode:
  `BENCH_IO parse_all_tests iterations=... files=... min_ns=... median_ns=...`
  reads every source during every iteration, while
  `BENCH_PARSE parse_all_tests iterations=... files=... min_ns=... median_ns=...`
  loads every source once and repeatedly tokenizes/parses the preloaded bytes.
  Both modes share `parser_bench_run()` in
  `src/buster/lib/compiler/frontend/buster/parser.c`, deliberately **not** gated
  behind `BUSTER_INCLUDE_TESTS` (they are benchmarks, not tests, and must stay
  buildable in Release) and deliberately independent of the windowing/rendering
  path `ide test` also drives, so they run headless on a plain CI runner with no
  display server.
- **`BUSTER_INSTRUMENT`** (CMake option, mirrors `BUSTER_TIME_TRACE`
  end-to-end — `--instrument`/`--no-instrument` on `generate`) compiles in
  finer-grained bench timing, compiled out entirely by default. With it on,
  `ide bench` additionally prints `BENCH_IO_PHASE`/`BENCH_PARSE_PHASE`
  tokenize/parse lines (splitting `tokenize()` from `parser_parse()`) and
  `BENCH_IO_FILE`/`BENCH_PARSE_FILE` lines per test file, slowest first. The
  mode prefix is part of every diagnostic line so phase and per-file timings
  cannot be confused between the filesystem and preloaded runs.
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
  without `#pragma once` is re-read and re-lexed at each `#include`, so the
  two columns' ratio is the unit's include amplification (currently 283
  files/15.4 MB against 487/18.1 MB). `bytes on disk`/`physical lines` measure
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
  table is populated lazily (see `map_entry` in `c.c`).
- **The `SELF_HOST stage <1|2> throughput` block** is the division done: its
  `workload` row reports bytes, LOC, SLOC and tokens; its `bandwidth` row
  reports MB/s, LOC/s and SLOC/s from the execution time shown immediately
  above it; and, where the platform exposes a hardware counter, its
  `instructions` row reports the total and the per-byte, per-SLOC and per-token
  ratios. These are the numbers to trend across commits, printed
  by `self_host_compare_action` beside the `SELF_HOST deterministic` line. `-v`
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

## Core rules

- **C only.** No C++, no exceptions. `-fwrapv`, `-fno-strict-aliasing`,
  `-funsigned-char`.
- **No third-party code.** External code was deliberately removed from the
  tree. Do not add dependencies or vendor libraries.
- **Recursion is forbidden unless it is trivial and statically bounded.**
  Recursive algorithms must represent recursion in data, using an explicit
  stack, queue, or worklist rather than the C call stack. Never use recursive
  calls when the depth depends on source input, runtime data, or another
  unbounded structure.
- **Compiler control flow must not use callbacks or function-pointer dispatch.**
  Use direct calls, loops, switches, and explicit work structures. Function
  pointer values may model the program being compiled, but the compiler itself
  must not execute through them. The one infrastructure exception is the
  uniform SPMD entry passed to `lane_run`; work inside that entry still uses
  direct control flow and an explicit index or range.
- **Parallelism target: SPMD across threads, SIMD within each thread.**
  Implementation code is written multi-core-by-default in the lane model of
  Ryan Fleury's raddebugger (`lane_run`, `lane_index`/`lane_count`,
  `lane_range`, `lane_sync`, `lane_broadcast` in `<buster/lib/os.h>`): every
  lane of a gang runs the same code, narrows serial work with
  `if (lane_index() == 0)`, and splits parallel work by `lane_range` or an
  atomic take-index — not through job systems, task queues, or callbacks.
  Lane-style code must degrade to serial: `BUSTER_SINGLE_THREADED` builds and
  one-lane gangs run the identical path with no threads and no separate serial
  variant. Within a lane, prefer wide data parallelism in the style of Daniel
  Lemire's simdjson kernels — process data in blocks, classify with tables and
  bitmasks, iterate set bits — over per-element branching, and shape data the
  way Casey Muratori advocates: flat index-linked arrays in arenas transformed
  by batch passes, not pointer graphs walked element-at-a-time. Parallel
  stages must stay deterministic — write results into slots indexed by work
  item, never by completion order — so the self-hosting fixed point stays
  byte-identical at any lane count. `lane_run` keeps a persistent worker gang
  on the calling thread context and reuses it across phases; do not add phase-
  local thread creation. Variable-duration work uses an atomic take-index,
  writes function- or item-local fragments into stable source-indexed slots,
  and merges them only after a lane barrier.
- **A global built on first use is prewarmed, never raced.** Several hot
  tables are derived once and read forever after through plain loads — the
  tokenizer's character classes, keyword slots and operator NFA, the C
  frontend's punctuator dispatch and declaration keywords, the codegen ABI
  target cache, the font provider's resolved paths, and the x86 metadata
  decode plus the three caches over it. That shape is sound only while no
  other thread can be reading, so every such build states
  `BUSTER_CHECK_SERIAL_INITIALIZATION()` (`<buster/lib/os.h>`, always
  compiled in, over `os_is_only_live_thread()`), and every owning module
  publishes a prewarm entry point that fills it on the calling thread:
  `tokenizer_prewarm`, `c_prewarm`, `codegen_prewarm`,
  `font_provider_prewarm`, `buster_x86_metadata_prewarm`, and
  `compiler_prewarm` for the compile pipeline as a whole. **Call the prewarm
  before `lane_run`** — x86 metadata separately, since no part of the compile
  path queries it and its prewarm costs a full table decode. A new lazily
  built global adds both the check and a line in its module's prewarm.
  Publish the flag *after* the state, never before: the x86 metadata decode
  needs two flags for this, one guarding re-entry from the validation it runs
  and one, set last, that every accessor tests. Spelling the character-class
  tables as constant initializers over a predicate macro would remove four of
  these outright, and was measured at **+178.8 M stage-1 instructions
  (+3.5%)** for 112 k extra preprocessed tokens — one predicate expansion per
  byte value per table. A real generator writing the bytes into source would
  not cost that; the preprocessor doing it at every compile does.
- **Microarchitecture tuning target: design for Zen 5's native 512-bit
  width; Zen 4 must break even, Zen 5 collects the upside.** The main
  development machine is a Ryzen 9 7940HS (Zen 4) and the CI x86-64 runners
  cover both generations (the Windows runner is a Zen 5 box), so
  single-thread throughput decisions — SIMD width, table sizes,
  branch-vs-cmov trades — are made for and measured on these cores.
  GNU-family builds compile with `-march=native`, so clang-built binaries on
  these hosts already have AVX-512 (VL/BW/DQ/VBMI/VBMI2) available. Write
  kernels at full 512-bit width: Zen 4 executes them double-pumped over its
  256-bit datapaths at AVX2-equivalent bytes per cycle with fewer
  instructions retired — no downclocking, no penalty, just no width upside —
  while Zen 5's native 512-bit units roughly double the same code's
  throughput. A kernel therefore has to justify itself at Zen 4's effective
  width and is validated there, but its shape (masks, compaction, permutes)
  is chosen for Zen 5. Watch the exceptions on Zen 4: the few instructions
  whose 512-bit form costs more than 2x the 256-bit form (check uops.info
  before leaning on exotic two-source permutes; `vpcompressb` is safe at
  ~9 cycles Zen 4 / ~5 cycles Zen 5), store-throughput-bound kernels (one
  256-bit store per cycle on Zen 4), and unaligned 64-byte loads — keep hot
  streamed buffers 64-byte aligned. Explicit intrinsics must stay behind
  feature/compiler guards with a scalar or SWAR fallback: MSVC builds carry
  no `-march`, aarch64 (macOS/Android/iOS) must keep building, and the
  self-hosted `ide cc` stages compile the same tree without vendor headers —
  performance is only ever quoted from clang-built binaries, so fallback
  paths need correctness, not speed.
- **Write 512-bit kernels in the vocabulary of `<buster/lib/simd.h>`, not in
  `<immintrin.h>`.** That header is a target-fixed list of AVX-512 operations
  — masked 512-bit loads and stores, byte comparisons producing a `Mask64`,
  `vpermt2b`, `vpcompressb` and its compacting store, byte-to-word widening,
  `vpternlogd`, lanewise arithmetic — with three implementations behind one
  spelling: `__builtin_buster_simd_*` for the self-hosted stages, host
  intrinsics for clang and gcc, and a scalar fallback for MSVC, AArch64 and
  pre-AVX-512 x86. `BUSTER_SIMD_512` says which one is in play; guard on it
  only where a *different algorithm* is worth writing, never merely to keep
  the tree building. It is deliberately not a portability layer — every
  operation names one instruction, and an abstraction wide enough to also
  describe NEON would lose exactly the operations that make these kernels
  fast. Adding to it means adding an `IrSimdOperation`, its arity in
  `ir_simd_operation_shape`, its validation, its EVEX lowering in
  `codegen_canonical_x64_simd_operation`, the builtin in `c_ir_simd_builtins`,
  a fallback, and a case in `tests/basic_c_simd.c`; the shape table is the
  single source of truth that keeps a new operation from being half-taught to
  the pipeline. Two consequences are worth knowing before writing a kernel.
  **Everything in the header is a macro**, because `ide cc` lowers directly
  and runs no inliner — not even for `always_inline` — so a function wrapper
  would be a real call per SIMD operation in the self-hosted stages;
  arguments must therefore be free of side effects. A `Simd512` may still
  cross a call boundary by value where that is what the code wants: SystemV
  passes and returns it in a vector register and spills past the eighth, and
  a target whose vector registers are narrower than the value says so with
  `CODEGEN_ERROR_UNSUPPORTED_ABI` rather than encoding a register it does not
  have. And **masks are `u64`, not an opaque k-register handle**: the
  canonical backend allocates no registers, so a mask spills to its frame slot
  either way, the vector instructions pick it back up with a single `kmovq`,
  and mask shifts, Boolean combinations, `mask64_count` and `mask64_first_set`
  stay on the general-purpose ALUs, which retire more of them per cycle on
  Zen 4/5 than the k unit does.
- **SIMD lexing/parsing method: the Validark lineage.** The buster-language
  parser began as a scalar port of Niles Salter's (Validark's) Accelerated
  Zig Parser — local checkout `~/dev/Accelerated-Zig-Parser`, upstream
  `github.com/Validark/Accelerated-Zig-Parser` — and that work plus
  validark.dev (start with `posts/deus-lex-machina` and
  `posts/eine-kleine-vectorized-classification`) and the three Utah Zig
  talks (YouTube `oN8LDpWuPWw`, `FDiUKafPs0U`, `NM1FNB5nagk`) are
  project-endorsed inspiration: when accelerating lexing/parsing, reach for
  these techniques first and keep new code compatible with their shapes.
  The core vocabulary: (1) per-64-byte-chunk classification — every class
  (whitespace, newline/CR, alpha/digit/underscore, quotes, backslash,
  slash, operator chars) becomes one comparison into a `k`-mask bitstring,
  all classes over the same chunk in lockstep sharing one load; (2) token
  extents by mask arithmetic — starts `x & ~(x << 1)`, ends
  `x & ~(x >> 1)`, then either cursor queries (shift by cursor, count
  trailing ones — one tzcnt replaces an unpredictable byte loop) or full
  vector compaction: `vpcompressb` an iota vector through the starts and
  ends masks, subtract, and every token length in the chunk materializes at
  once; kinds come from masked broadcasts into a kinds vector compressed by
  the same starts mask, interleaved with lengths on store; (3) charset
  membership via nibble-decomposed `vpshufb`/`vpermb` tables —
  `table[c & 0xF] & (1 << (c >> 4))` per lane, the upper-nibble
  powers-of-two vector shared across charsets, `vptestmb` folding the AND
  and test on AVX-512; (4) multi-character operators as a bit-channel
  `vpshufb` NFA: three per-position tables whose looked-up bytes AND
  together so each of the 8 bit-channels legalizes one family of 2-/3-char
  sequences, plus a short effectively-branchless reconciliation of
  overlaps; (5) keyword and builtin recognition by perfect hash, never a
  memcmp ladder — `((len << 14) ^ first_two_bytes) * last_two_bytes >> 8`
  to 7 bits, Bagwell array-mapped compression (two u64 bitmaps + popcount
  rank) into a dense table of length-padded entries validated by a single
  wide compare, with compile/startup-time collision checks so editing the
  keyword set stays safe; (6) sentinels instead of bounds checks — a
  leading newline, trailing quote/NUL sentinels, and chunk-aligned
  overallocation let the hot loops drop every length test; (7) upper-bound
  allocation plus post-scan shrink instead of grow-and-check (the
  `2026-08-08k` tokenizer change is this trick alone, `-21.6%` bench
  instructions); (8) length-based token streams — kind + length with a
  0-length escape to a wide length, no per-token start offsets or eagerly
  materialized line/column, offsets rebuilt by a running cursor and line
  numbers recovered by popcount over retained newline bitmasks. Escape-run
  parity uses the simdjson backslash algorithm (simdjson PR #2042 has the
  current best form). All of it sits behind the guard rules of the tuning
  target above; scalar fallbacks keep the exact current semantics.
  **Both lexers now run this architecture**: `tokenize_compact` in
  `parser.c` for the buster language and `c_lex_compact` in `frontend/c/c.c`
  for C, each walking its source in **item-aligned 64-byte windows** so no
  lexer state crosses a window — the item touching a window's last byte is
  deferred and rescanned by the next one, and the shapes the masks do not
  model escape to the scalar path's own single-item scanner
  (`tokenizer_scan_one_token`, `c_lex_scan_one`), which is what makes the two
  paths agree by construction rather than by duplicated reasoning. Read
  `c_lex_compact` before writing a third: it is the one that had to solve the
  general cases, because C skips whitespace and comments instead of
  tokenizing them (so token ends need an explicit boundary mask, not
  `starts >> 1`), and because lookahead loads there are masked by the **file**
  bounds rather than the window's — a delimiter near the window end is
  spelled from bytes the next window owns. Every such emitter must ship a
  differential gate asserting byte-identical agreement with its scalar
  reference over construct cases slid across the window boundary, items
  longer than a window, the real corpus at every window phase, and fuzz
  blobs over the full alphabet; that gate is what catches the ordering bugs,
  and it caught one in each emitter so far.
- **Warnings are errors** under a very large warning set (see
  `GNU_FAMILY_WARNINGS` in `CMakeLists.txt`), and code must stay clean under
  Clang, GCC, Zig cc, and MSVC. TCC is required only to compile/bootstrap the
  native `build.c` driver through `build.sh`/`build.ps1`; application code is
  not promised to compile with TCC. Avoid compiler-specific extensions unless
  guarded.
- **Idioms**: arena allocation (`<buster/lib/arena.h>`) — no malloc/free churn;
  `String8`/`S8("...")` (defined in `<buster/lib/base.h>`) — no C strings; `STRUCT(Name)`
  declarations; `BUSTER_`-prefixed macros; 4-space indent, snake_case, braces
  on their own line. Prefer function headers, declarations, statements, and
  similar constructs on one line; split them only when doing so is clearer.
  Match the surrounding file.
- Test implementations are not registered as modules; add new test pairs under
  `src/buster/tests/`, add
  the implementation to `BUSTER_TEST_SOURCES` and the header to
  `BUSTER_TEST_HEADERS` in `CMakeLists.txt`, and include both from
  `src/buster/tests/test.c` in the existing registration order (the
  implementation only in the `BUSTER_UNITY_BUILD` block).
- **Adding a module** (`foo.c`/`foo.h` under `src/buster/lib/`) takes three
  edits: (1) `buster_register_module(foo ...)` in `CMakeLists.txt`;
  (2) add `foo` to the `MODULES` list of `buster_add_executable(ide ...)`;
  (3) add `#include <buster/lib/foo.c>` to the `BUSTER_UNITY_BUILD` block at
  the top of `src/buster/apps/ide/ide.c` — optimized non-sanitized configs compile as
  unity builds, so forgetting this breaks ordinary Release builds.
- Headers are included as `<buster/lib/...>` or `<buster/tests/...>` (include
  root is `src/`).
  `compile_commands.json` is exported to `build/` by default.

## Platform and backend boundaries

- In rendering and windowing, keep platform-neutral policy and data flow in
  the module front door (`rendering.c`, `window.c`). Native API calls
  belong in the selected backend implementation.
- Rendering backends live in `src/buster/lib/rendering/*.c`; window backends
  live in `src/buster/lib/window/*.c`. These are implementation files
  included by their owning module, not standalone CMake modules, so do not
  register them or add them to the unity-build include list.
- Every backend `.c` includes its directory's `internal.h` before its
  implementation declarations. Those private headers own the shared types,
  helper declarations, and native headers needed for clangd to parse a backend
  independently; do not depend on declarations only earlier in the owning `.c`.
- Share CPU-side draw generation, font/texture orchestration, event-list
  ownership, and lifecycle policy. Keep device resources, synchronization,
  swapchains, native event translation, and native handles backend-specific.
- Renderers consume window-system handles through `WmNativeSurface`; do not
  reach into `WmHandle` or `WmWindowHandle` from a rendering backend.

## Parser rules

The buster-language parser (`src/buster/lib/compiler/frontend/buster/parser.c`)
has hard design constraints:

- **No recursion in C.** The parser is a state machine driven by an explicit,
  arena-backed `ParserState` stack (`state_push`/`state_pop`). New grammar
  constructs get new state kinds and stack frames — never recursive helper
  calls.
- `parser_parse()` writes its public `ParserResult` AST and diagnostics into a
  caller-owned result arena. It also takes a distinct caller-owned expression
  arena, resets it at the start of each parse, and uses it only as reusable
  staging before copying completed expressions into the result arena. Source/
  token storage must outlive the result; neither expression staging nor the
  `ParserState` stack (including pending prefix-unary frames) may escape. The
  state stack is scratch, borrowed via `scratch_begin`/`scratch_end`.
- Invalid user input produces `ParserDiagnostic` entries and synchronizes at a
  statement or top-level declaration boundary. `BUSTER_TODO()`/assertions are
  reserved for internal invariants, never ordinary syntax errors.
- **Unary operators are AST expression nodes** (for example,
  `AST_NODE_UNARY_MINUS`), not part of numeric literal tokens.
- Expression precedence may use binding-power concepts, but the
  implementation must remain state-machine based.

## Semantic analysis and IR rules

- Every valid parser fixture in `tests/*.bbb` must also be run through semantic
  analysis and IR generation. Functions with semantic diagnostics must not
  publish IR; tests must still verify that lowering was attempted and rejected
  for the diagnosed function rather than silently skipping the fixture.
- New semantic or IR behavior needs focused unit coverage in addition to the
  fixture-wide pipeline test. IR tests must validate structural invariants, not
  merely check that generation returned a non-null pointer.
- **Both validators prove instruction ownership before they check anything
  else**, through `ir_function_instruction_owners` and the
  `ir_validate_module_ownership` pre-pass: one `owner[instruction_count]` array
  per function, filled by walking each block's chain, where a second visit to
  an instruction is the error. That one O(instructions + blocks) pass rules out
  cycles, chains shared between blocks, a `last_instruction` the chain never
  reaches, and instructions owned by no block at all while later passes still
  read them out of the dense array — none of which chain traversal alone can
  see. Everything downstream depends on it and must not re-derive it: the
  validators' own walks, the canonical emitter's block loop, and
  `codegen_allocate_registers` all carry no cycle guard and no range test,
  because a counter that only notices a cycle after re-walking the function is
  both slower and weaker than the proof. Keep new consumers on the same array
  rather than adding another local one, and report an ownership failure as
  `IR_VALIDATION_INSTRUCTION_OWNERSHIP`.
- **An `IrSourceRange` is a source, a byte offset and a length — never a line
  or a column.** Lowering builds one range per instruction; a compile asks for
  a line at four places only: diagnostic formatting, DWARF line-table
  generation, CodeView line generation, and source-navigation requests.
  `ir_source_position` is the one place line and column are computed. It
  resolves either through the program's `IrSourceMap` — sorted regions over
  the frontend's byte space, with per-line checkpoints and one stamped
  position per macro expansion, which the C frontend hands over as its
  preprocessing map so ranges carry spelling-space offsets — or by scanning
  `IrSource.text`, for frontends whose ranges index the parsed bytes
  directly. Producers must not resolve a position to fill a range, and
  consumers that record one row per instruction must reject the repeat on the
  range's own offset before resolving: consecutive instructions overwhelmingly
  come from one token, and resolving first puts the search back on the hot
  path it was moved off (measured at `+99 M` stage-1 instructions when it was).
- **A frontend that preallocates `IrFunction.instructions` must preallocate
  `instruction_canonical_sources` beside it.** `ir_function_add_instruction`
  stores a canonical source only into an array that already exists, so sizing
  the instructions alone silently drops every range the lowering built and
  leaves the line table with nothing below one row per function — a gap that
  costs nothing anywhere the compiler checks and shows up only in a debugger.
- The typed IR must not retain parser/AST operation identifiers.
  Unary and binary instructions use IR-native operations that encode the
  semantic domain (integer, float, boolean, or pointer) and signed behavior;
  the IR value type supplies the width.
- Conversions are explicit `IR_OPCODE_CAST` instructions with an IR-native
  operation that records extension, truncation, reinterpretation, or numeric
  domain conversion. Contextually typed literals are materialized directly at
  their final type rather than carrying conversion metadata on their producer.
- `for` loops lower to ordinary blocks, block parameters, comparisons,
  indexing, and arithmetic. Range and reverse operations produce immutable
  iterable values; stateful iterator begin/next/value instructions do not
  belong in the IR.
- Native code generation consumes the typed IR directly. The Buster-language
  backends (`codegen_generate_x86_64`/`codegen_generate_aarch64`) perform a
  conservative linear-scan allocation of same-block scalar IR values to
  backend-owned caller-saved registers. Values crossing calls or control-flow
  edges, aggregates, and excess live values retain stack slots as spill
  storage; block parameters are resolved with parallel edge copies. The
  canonical path (`codegen_generate_canonical_module`, used by the C frontend
  and therefore by self-hosting) runs **no** register allocation: every value
  owns a frame slot and every operand is reloaded from it. Its only register
  reuse is a local peephole that drops an x86-64 `rax` frame reload when the
  immediately preceding emission was the store of `rax` to that same slot,
  tracked by buffer position and reset at every block start so no branch can
  reach the elided load. Codegen publishes format-neutral function
  descriptors with exact code ranges, ordered prolog unwind actions, and
  AArch64 epilog offsets; object formats consume those descriptors instead of
  inferring sizes from the next symbol. Unwind `nop` actions describe prolog
  instructions that do not change unwind state but must remain positionally
  visible to the Windows AArch64 unwinder. Executable tests use writable memory
  only while copying code, then switch it to read/execute before calling it.
- Standalone code generation must consume target layouts before allocating
  locals or aggregate backing storage. Analysis-backed IR ABI decisions come
  from `analysis_classify_function_abi`; canonical IR types own convention-
  indexed multi-part ABI metadata populated as each type layout is completed.
  Backends translate that IR-owned classification rather than maintaining a
  second classifier.
  System V x86-64, Win64, AAPCS64, and Darwin AArch64 classifications include
  split integer/float aggregates, homogeneous floating aggregates, stack
  placement, caller-owned copies for indirect arguments, and hidden result
  pointers (`rdi`, `rcx`, or `x8` as required). Code generation must reject a
  calling convention that is incompatible with the selected architecture and
  validate every part against the type layout, register limits, and outgoing
  stack area before emitting it.
- Module code generation emits each lowered function into an independent
  source-indexed fragment. A stable source-order prefix merge assigns final
  code offsets and combines entries, relocations, line rows, debug-location
  seeds, unwind descriptors, and statistics; completion order must never
  affect output or error selection. Global layout and assembly remain serial,
  and direct-call relocations resolve by `(entity, instantiation)` after the
  merge. Aggregate values use frame-owned backing storage while
  array/slice/range descriptors remain ordinary typed IR values; aggregate
  assignment is a value copy, not descriptor aliasing. The x86-64 and AArch64
  baselines share this value representation even though their stack addressing
  and instruction emission are backend-specific.
- Separate compilation merges format-neutral `ObjectFile` values before native
  serialization. Global definitions resolve by symbol name, private
  object symbols remain object-local, non-exported language definitions use
  deterministic internal names, duplicate definitions are diagnosed, and
  unresolved symbols are retained only when the final platform linker is
  allowed to satisfy them.
- Canonical static-storage objects are `IrGlobal` values. Zero, integer, float,
  byte aggregate, and symbol-address initializers are materialized into
  read-only or writable object sections; mutable non-TLS zero objects use BSS,
  const zero objects retain read-only bytes, and TLS zero objects use TBSS.
  Zero-fill sections preserve virtual size without serialized bytes through
  object merging and native image writing. Static locals use deterministic
  internal global symbols. C aggregate initializer lowering uses an explicit
  task stack, and incomplete character-array bounds are inferred from string
  initializers before layout.
- Debug info is **on by default** for both `ide cc` and `ide compile`; `-g0`
  turns it off and `-g` is accepted for compatibility. Both frontends lower
  through one path: codegen records per-instruction line entries plus a
  function-start row, and the object builders hand those neutral
  function/line descriptors to the emitter selected by the **target's native
  format** — CodeView for Windows, DWARF everywhere else. Canonical IR source
  ranges are one-based; the zero-based buster parser lines are converted at
  the parser→IR boundary.
- DWARF 4 (Linux, macOS, iOS, Android) emits
  `.debug_info`/`.debug_abbrev`/`.debug_line`/`.debug_str` plus
  `.debug_loc`/`.debug_ranges`, with relocations against local text and
  debug-base symbols, so 32-bit cross-section offsets survive object
  concatenation. ELF objects also emit `.eh_frame` independently of source
  debug information and preserve its x86-64 PC-relative and AArch64 PREL32
  relocations through object reading and merging. ELF executable writers
  resolve those relocations into the loaded unwind section, emit a sorted
  `.eh_frame_hdr` plus `PT_GNU_EH_FRAME`, and always append a coherent section
  header table for loaded code/data, BSS, TLS, loader and dynamic-linking
  metadata, debug data, and the section-name string table, including for `-g0`
  images. DWARF relocations are resolved statically before the non-loaded debug
  payload is appended. Mach-O objects and final images likewise carry
  `__TEXT,__eh_frame` independently of source debug information; arm64
  relocatable objects represent PREL32 fields with the canonical paired
  `ARM64_RELOC_SUBTRACTOR`/`ARM64_RELOC_UNSIGNED` form. The Mach-O reader
  converts supported external `__LD,__compact_unwind` frame-pointer and
  frameless rows into neutral DWARF CFI, and rejects personality/LSDA,
  authenticated-frame, vector-save, and unfamiliar-prolog encodings rather
  than silently dropping unwind coverage. Mach-O executables carry the same
  six source-debug sections in a read-only, file-backed
  `__DWARF` segment (each section is flagged `S_ATTR_DEBUG`) placed before
  `__LINKEDIT`; its virtual size is page-rounded to satisfy dyld while the
  ad-hoc code signature covers the complete image. LLDB reads DWARF straight
  from the image, with no dSYM step.
- CodeView (Windows) emits `.debug$S` (C13: `S_OBJNAME`, `S_COMPILE3`,
  per-function `S_GPROC32`/`S_END`, line blocks, file checksums, string
  table) and a minimal `.debug$T`. Its `SECREL32`/`SECTION` relocations
  resolve against each function's own object symbol. Those COFF relocation
  numbers are shared with the TLS relocations, so the reader disambiguates
  them by section. COFF section names longer than eight bytes use the
  `/<offset>` string-table form.
- `pdb_build` turns resolved CodeView modules into a PDB: each input object
  remains a DBI module stream, symbol subsections become that module's symbol
  region, the remaining subsections its C13 region, and checksum entries are
  remapped from object-local string tables onto the PDB `/names` stream. TPI
  records are merged into the shared type stream, but only after every record
  has been rewritten through its own module's index map: type indices are
  object-local, so two records can be byte-identical while naming different
  types, and comparing raw `.debug$T` bytes silently gives one module's symbols
  the other module's types. Merging then runs over the rewritten records, whose
  references are global and therefore comparable. Records reference each other
  in both directions — `LF_STRUCTURE` names an `LF_FIELDLIST` that comes after
  it — so nothing may assume a record only refers to lower indices. Function
  and global symbol locations use their actual final PE section number and
  section-relative offset across text, read-only data, writable data, BSS, and
  TLS. Readers need
  more than the streams they will read — an empty globals or publics stream
  still needs its GSI hash header, and the DBI needs an edit-and-continue name
  table, or module iteration fails. Windows links with debug enabled derive a
  sibling `.pdb`,
  emit a read-only PE `.debug` section containing a matching RSDS record, and
  derive the GUID deterministically from the pre-debug PE image and canonical
  debug-module data. Validate changes with `llvm-pdbutil dump --all`, which is
  strict about all of this.
- Windows x86-64 unwind metadata is emitted independently of source debug
  information. Each generated function has a `.pdata` runtime-function entry
  and x64 `UNWIND_INFO` in `.xdata`; COFF uses `ADDR32NB` relocations and the
  final PE exception directory covers the resolved `.pdata` table. Large
  frames use a constant-size inline guard-page probe loop followed by one
  described stack allocation so the prolog remains representable by the x64
  unwind format.
- Windows AArch64 emits ordered 8-byte `.pdata` runtime-function entries and
  full `.xdata` records with explicit prolog operations and epilog scopes.
  Large frames use a constant-size inline guard-page probe followed by one
  `alloc_l`; its probe instructions and frame-base moves have matching unwind
  `nop` entries, and the x28 save uses a reserved low frame slot so it remains
  directly encodable. The PE writer prepends unwind records for its
  architecture-specific process-entry stub on both Windows architectures and
  points the exception directory at the complete table.
- `object_section_name_for_kind` and `object_section_default_alignment` are
  the single source of truth for section naming and defaults. Use them when
  adding a section kind rather than adding another local table, otherwise
  every reader, writer, and test helper silently misses the new kind.
- Native executables target the platform libc and CRT without invoking a host
  compiler or linker. Buster writes final ELF64 images for Linux and Android,
  PE32+ images for Windows, and ad-hoc-signed Mach-O images for macOS and iOS,
  for both x86-64 and AArch64 where the platform supports the architecture.
  The writers supply process startup where the format requires it, apply
  internal relocations, and emit the dynamic-loader metadata, import veneers,
  symbol tables, and libc/UCRT/libSystem dependencies themselves. Android
  images are PIE and use the system linker and bionic `libc.so`; Apple images
  are PIE and use `LC_MAIN`, dyld rebase/bind opcodes, libSystem, and an
  internally generated SHA-256 CodeDirectory. Freestanding startup remains a
  separate mode.
- Semantic analysis and IR lowering follow the repository-wide recursion rule:
  nested expressions, statements, types, and control flow use arena-backed
  explicit stacks or worklists.
- Runtime IR interpretation follows the same rule: language calls use an
  explicit bounded frame stack, and every execution has explicit instruction
  and call-depth limits. The interpreter is a runtime correctness oracle; it
  must not silently acquire compile-time execution semantics.
- Interpreter memory is arena-owned and bounds-checked. Language pointers are
  symbolic object-plus-offset references, never host addresses; loads track
  byte initialization, aggregate copies preserve symbolic values, and invalid
  memory access must return an execution trap instead of asserting.
- `$` function parameters and `$T` types are compile-time generic inputs.
  Semantic analysis interns concrete instantiations by a canonical key made
  from the declaration identity, normalized type bindings, and normalized
  compile-time values; never use arena-local type/constant IDs as a persistent
  specialization identity. Compile-time parameters are absent from runtime
  signatures and call operands; uninstantiated templates publish no IR. The
  defining module owns exactly one body-analysis job and specialized IR function
  for each key, while interface summaries record deterministic symbols and the
  requesting modules so cached builds preserve specialization demand.

## Repository map

Top level:

| Path | Contents |
|---|---|
| `build.c` / `build.sh` / `build.ps1` | Build driver (C, tcc-bootstrapped) and its POSIX/Windows bootstrap scripts. |
| `CMakeLists.txt` | The real build definition: compiler detection, warning sets, sanitizer runtimes, shader compilation, module registry, targets, Android/iOS packaging. |
| `cmake/` | `embed_d3d12_shaders.cmake`, `embed_metal_shaders.cmake` — turn compiled shaders into embeddable C data. |
| `src/buster/apps/` | Application entrypoints and standalone tools. |
| `src/buster/lib/` | Reusable runtime, UI, platform, compiler, and rendering code. |
| `src/buster/tests/` | Test harness and module test pairs, unity-included or independently compiled according to the build mode. |
| `tests/` | Runtime compiler fixture corpus: `.bbb` language programs and C frontend/driver fixtures. Test implementations themselves live under `src/buster/tests/`. |
| `android/`, `ios/` | Manifest/plist plus CI scripts to package, install, and run the on-device/simulator test suite. |
| `.forgejo/workflows/ci.yml` | CI pipeline for correctness, sanitizer/fuzz, self-host, and mobile tests. |
| `PERFORMANCE_AUDITS.md` | Performance audit history, newest first: what was measured, what was fixed, and the reference numbers the next audit starts from. |
| `lsan.supp` | LeakSanitizer suppressions. |
| `build/` | Generated build output (ninja files, per-config dirs, `compile_commands.json`, `build/build`). Never edit. |

`src/buster/lib/` — foundation modules (each `name.c` + `name.h`):

| Path | Contents |
|---|---|
| `base.h` | Root header: platform detection, fixed-width types (`u8`…`s64`), `String8` + `S8("...")`, `STRUCT`/`BUSTER_*` macros, build-mode flags. Header-only. |
| `system_headers.h` | Central OS/libc header includes. `tls.h` is an empty placeholder. |
| `apple_runtime.h` | Objective-C runtime includes and ABI-compatible scalar/geometry types shared by Apple windowing and Metal. |
| `os.{c,h}`, `entry_point.{c,h}` | OS abstraction (processes, virtual memory) and per-platform entry glue (`main`, NativeActivity, UIKit). |
| `arena.{c,h}`, `string.{c,h}`, `integer.{c,h}`, `float.{c,h}`, `hash.{c,h}`, `file.{c,h}`, `time.{c,h}` | Arena allocator; `String8` operations and `string_print` (`{S8}` placeholders); numeric parse/format; hashing; file IO; clocks. |
| `simd.{c,h}` | The target-fixed 512-bit AVX-512 vocabulary — `Simd512`, `Mask64`, and the macros over them — with three implementations: self-hosted builtins, host intrinsics, and a scalar fallback. Header-only in practice; `simd.c` exists to give the module a translation unit. |
| `target.{c,h}`, `x86_64.{c,h}`, `aarch64.{c,h}` | Target/data-layout descriptions, CPU models/features, and per-architecture instruction encoders. |

UI / graphics:

| Path | Contents |
|---|---|
| `ui_core.{c,h}`, `ui_builder.{c,h}` | Immediate-mode UI core (widget tree, layout, input) and higher-level construction helpers. |
| `window.{c,h}`, `window/internal.h`, `window/*.c` | Shared window/event front door, private backend contract, and xcb/xkbcommon, AppKit/UIKit, Win32, Android, and null backends. |
| `rendering.{c,h}`, `rendering/internal.h`, `rendering/*.c` | Shared CPU-side renderer front door, private backend contract, and Vulkan, Metal, D3D12, and null backends selected at compile time. |
| `truetype.{c,h}`, `font_provider.{c,h}` | From-scratch TrueType rasterizer; system font discovery (fontconfig on Linux). |
| `shaders/` | `rect.slang` is the shared Slang source for SPIR-V, Metal, and HLSL outputs; `rect_shared.h` contains declarations shared with C. Android selects its SSBO vertex path with `BUSTER_VULKAN_VERTEX_SSBO` (Adreno workaround). Generated `rect.vert.*`/`rect.frag.*` files live under `build/shaders/`, never in the source directory. |

Compiler (`src/buster/lib/compiler/`):

| Path | Contents |
|---|---|
| `frontend/buster/parser.{c,h}` | Lexer + explicit-stack parser; owns the `.bbb` fixture array and parser benchmarks. `parser_file_tests()` lives in `src/buster/tests/compiler/frontend/buster/parser_test.c`. |
| `frontend/c/c.{c,h}` | GNU C frontend in progress: source translation, the compaction lexer described below, preprocessing tokens/macros/includes/conditionals, non-recursive external-declaration parsing with strong IDs, flattened scalar/pointer/array/function/aggregate types, nested lexical scopes with entity-based identifier binding and canonical redeclarations, and target-aware shared-IR lowering for scalar/pointer/aggregate parameters, locals, static-storage objects, explicit conversions, array decay/indexing, chained field access, control flow, short-circuit and conditional expressions, direct calls, and constant aggregate initialization. |
| `assembly/assembly.{c,h}` | Standalone target assembly parser and encoder with labels, expressions, relocations, symbols, and structured diagnostics. `assembly/generated/` contains pinned reduced XED/LLVM metadata and its provenance; regenerate it only through `build.c`'s explicit `import_assembly_metadata` command. |
| `frontend/buster/analysis.{c,h}` | Buster semantic indexing, interface resolution, body analysis, layouts, ABI classification, specialization, and dependency scheduling. Fixture-wide tests live in `src/buster/tests/compiler/frontend/buster/analysis_test.c`. |
| `ir/model.h` | Format-neutral canonical typed IR data model shared by frontends, codegen, debug metadata, objects, and the interpreter. |
| `ir/ir.{c,h}` | Buster semantic-to-IR lowering, IR validation, and printing. Fixture-wide and structural tests live in `src/buster/tests/compiler/ir/ir_test.c`. |
| `ir/interpreter.{c,h}`, `ir/interpreter_internal.h` | Bounded, explicit-stack runtime IR interpreter and its private test seam/types. Tests live in `interpreter_test.{c,h}`. |
| `debug/debug.{c,h}` | Target-neutral debug type, scope, variable, inline-site, source, and location model built from canonical IR or Buster analysis. |
| `dwarf/dwarf.{c,h}` | DWARF 4 source-debug and call-frame-information emitter. Produces debug sections plus target-neutral `.eh_frame` bytes and relocations from codegen function descriptors. |
| `codeview/codeview.{c,h}` | CodeView C13 emitter for Windows targets: `.debug$S` symbol/line/checksum/string subsections and `.debug$T`, with per-function `SECREL32`/`SECTION` relocation slots. |
| `pdb/pdb.{c,h}` | PDB writer: MSF container plus the info, TPI, DBI, IPI, globals, publics, section-header, module and `/names` streams. Repackages CodeView modules and remaps checksum entries onto the PDB string table. |
| `object/object.{c,h}` | Format-neutral sections, symbols, and relocations; ELF64, COFF, and Mach-O relocatable writers/readers; assembly printing; in-memory object linking. |
| `link/link.{c,h}` | Multi-object section merging and symbol resolution; from-scratch libc-backed ELF64, PE32+, and Mach-O executable writers. |
| `driver/driver.{c,h}` | End-to-end source-to-object compilation and libc-backed executable linking. The Clang-like `ide cc` path supports preprocessing, syntax checks, per-input C object emission for every supported target, and multi-translation-unit native executable construction through the format-neutral object merger for the currently lowered subset. |
| `codegen/codegen.{c,h}`, `codegen/codegen_internal.h` | Direct typed-IR ABI translation, conservative register allocation, native x86-64/AArch64 emission, executable-memory support, and private codegen test seams. Tests live in `codegen_test.{c,h}`. |

Applications and standalone tools:

| Path | Contents |
|---|---|
| `apps/ide/ide.c` | Main application, the **only CMake executable target**, and the unity-build translation unit (the `BUSTER_UNITY_BUILD` include block at the top). |
| `apps/disk_builder.c` | Standalone MBR/GPT disk-image builder. Not wired into CMake. |
| `lib/sanitizer_coe_win.c` | Windows continue-on-error sanitizer shim (raw `WriteFile` to stderr, no CRT). |
