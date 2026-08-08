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
build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -o build/ide-self
build/ide-self cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -o build/ide-self-stage2
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
must honor that limit. Trees are declared longest-first — sanitized Debug,
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
and maximum native vector width.

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

## Latest performance audit notes

`2026-08-08b` (Linux x86_64, `perf record -F 999 --call-graph fp`, instruction
counts from `STEP_INSTRUCTIONS` and `perf stat -e instructions`). Two
configurations were audited together for the first time: **compile throughput**
(clang-built Release `ide` compiling the unity TU, the `Self-host stage 1` step)
and the sanitized Debug `ide test` CI critical path. Every number here comes
from a clang-built binary; buster-compiled stage executables are self-hosting
validation only and none of their timings are quoted. Net result across six
commits: stage 1 `27.436 G` to `17.000 G` instructions (`-38.0%`, `3.19 s` to
`2.16 s`) and sanitized `ide test` `430.25 G` to `332.16 G` (`-22.8%`), at the
byte-identical self-host fixed point with all 29 modules passing in both
configurations and `ide bench` unchanged throughout.

- **Five whole-table scans, four of them quadratic, in one audit.** Each was a
  query answered by re-deriving a global property per item, and each is the
  shape the `2026-08-08` entry below told the next audit to look for. In the
  order they were fixed, with the stage 1 instruction count after each:
  - `c_ir_type_is_flexible_array` scanned every type and every member of every
    aggregate, from depth 5 inside the `type_mapping_round` / `pass` /
    `type_index` fixpoint nest — `21.2%` of the compile on its own.
    `c_ir_collect_flexible_array_types` marks the set once per round.
    `27.436 G` to `21.216 G` (`-22.7%`).
  - `c_ir_incomplete_array_has_initializer` scanned every declaration and every
    entity, once per array type. Now indexed by type in one pass.
    `21.216 G` to `20.549 G` (`-3.1%`).
  - `c_ir_function_signature` called `ir_prepare_program_abi`, sweeping every
    type in the program, once per function declaration. Removed outright:
    every ABI read outside the resolver goes through `ir_type_abi_value`,
    which resolves on demand, so the sweep was pre-warming only. `20.549 G` to
    `19.666 G` (`-4.3%`).
  - `c_ir_label_metadata_store_for_place` scanned every value in the function
    on every store, relocating each place to ask whether it shared the root
    just written. Places are now filed once into an intrusive list headed by
    their root. `19.672 G` to `17.000 G` (`-13.6%`), and sanitized `ide test`
    `366.44 G` to `332.16 G` (`-9.4%`) — it was the one hotspot both
    configurations shared.
  - `x86_64_metadata` string consumers read the pool one
    `buster_x86_metadata_string_byte` call per byte. See below.
- **The optimizer's inline hint does not exist in the tree CI measures.**
  `BUSTER_INLINE` expanded to nothing when `BUSTER_OPTIMIZE` was off, so the
  already-decoded guard `c343ab8` added to `buster_x86_metadata_decode_tables`
  was still an out-of-line call in sanitized Debug, at `3.2%` of `ide test`.
  That commit's sanitized win came from shrinking the guard's `-O0` frame, not
  from inlining. `BUSTER_ALWAYS_INLINE` now holds in every configuration and
  `BUSTER_INLINE` is defined in terms of it; `nm` on the sanitized Debug `ide`
  no longer lists the guard. Worth `-0.9%`. Reserve the new spelling for
  one-test guards on per-byte or per-record paths — forcing a large body inline
  everywhere costs Debug compile time and steppability.
- **The decoded string pool is contiguous, so stop reading it a byte at a
  time.** The one-time decode already flattens the chunked generated pool into
  a host array, but every consumer still went `string_byte` -> `pool_byte` ->
  the decode guard: three calls per byte over a `1.7 MB` pool, roughly `27%` of
  the sanitized run across substring searches, NUL scans, and the pattern
  tokenizer that walks all `11013` forms. `buster_x86_metadata_pool_span` /
  `_string_span` hand out a view once. Sanitized `ide test` `424.46 G` to
  `366.44 G` (`-13.7%`), `x86_64_metadata_tests` `8.21 s` to `5.17 s` (`-37%`).
  `string_span` is public because the metadata tests are a separate translation
  unit in non-unity builds.
- **A correct incremental fix can still be worthless; measure it.** The first
  attempt at the ABI sweep resumed from the longest already-resolved prefix,
  which is sound because a resolved ABI is never invalidated. It measured
  `-0.2%` and left `c_ir_function_signature` at `6.1%` of the profile, because
  `ir_resolve_type_abi` returns without resolving when a type's layout is
  unresolved, so one early incomplete type pins the prefix near zero. Profiling
  after the change, not just before it, is what caught this.
- Shares after all six commits, stage 1 out of `17.000 G`: `c_lower_to_ir`
  `24.3%`, `c_lex` `4.6%`, `c_ir_body_task_set_loop_cleanup_targets` `4.1%`,
  `object_from_canonical_codegen_module` `3.9%`,
  `codegen_generate_canonical_module` `3.6%`, `c_ir_lower_frame_fallback`
  `2.8%`. No single quadratic dominates any more and the profile is flat.
- Shares after all six commits, sanitized `ide test` out of `332.16 G`:
  `__asan_memcpy` `19.5%`, `string_equal` `7.1%`,
  `buster_x86_metadata_string_offset_terminated` `6.4%`,
  `buster_x86_metadata_emit_token_equal` `5.7%`,
  `x86_64_metadata_test_string_contains` `4.2%`,
  `c_ir_lower_assignment_statement_step` `4.1%`, `c_token_is_punctuator`
  `2.7%`. Module totals `c_frontend_tests` `8.70 s` and
  `x86_64_metadata_tests` `5.23 s`. `string_offset_terminated` *rose* in share
  because the run shrank around it: it re-finds a NUL from an offset every time
  a record's string length is wanted, and caching the length in the decoded
  record is the obvious next step.
- Reference points for the next audit, all clang-built: Release `ide test`
  `c_frontend_tests` `693 ms`, `x86_64_metadata_tests` `517 ms`. Release
  `ide bench` `BENCH_IO` median `263 us`, `BENCH_PARSE` median `92 us` — both
  unchanged by every commit here, which is the expected result and was checked
  each time rather than assumed.
- Process note: `--sanitize` is a sticky `generate`-time flag. A measurement
  script that runs the sanitized pass last and then measures Release without
  `--no-sanitize` silently profiles a sanitized tree; self-host aborts in
  `ld.so` and Release test times triple. Also do not build while a measurement
  runs — two overlapping runs produce plausible wall times that are pure
  contention. Instruction counts survive both mistakes; wall times do not.

`2026-08-08` (Linux x86_64, sanitized Debug clang, `perf record -F 999
--call-graph fp`, `25650` samples, instruction counts from `perf stat -e
instructions`). Supersedes the shares in the `2026-08-07` entry below, whose
mechanism was fixed by `23a574e`: `CToken` no longer crosses the spelling
predicates by value, and ASan memcpy shadow scanning is `8.8%` of the run
rather than half of `c_frontend_tests`.

- **A lazily-decoded table's guard cost 20% of the whole test suite.**
  `buster_x86_metadata_decode_tables` decodes once behind
  `buster_x86_metadata_tables_decoded`, but every accessor called it
  out-of-line, including the per-byte string accessors, so the decode was free
  and the already-decoded test was not. Splitting it into a `BUSTER_INLINE`
  test plus a cold `buster_x86_metadata_decode_tables_once` body moved the run
  from `678.81 G` to `537.66 G` instructions (`-20.8%`) and `32.93 s` to
  `25.80 s`; `x86_64_metadata_tests` `14.90 s` to `8.25 s` (`-45%`) and
  `assembly_tests` `1.10 s` to `0.66 s` (`-40%`). Self-hosting stays at the
  byte-identical fixed point. Look for this shape wherever a self-initializing
  accessor is called per byte or per record.
- Module shares after that change: `c_frontend_tests` `61.6%` (`15.79 s`),
  `x86_64_metadata_tests` `31.8%` (`8.25 s`), `assembly_tests` `2.6%`. Before
  it the two were effectively tied (`15.79 s` against `14.90 s`), so
  "`c_frontend_tests` is the largest cost" became true only once the metadata
  module was fixed.
- **The sanitized tree runs UBSan as well as ASan, so each instrumented
  struct-field access costs roughly 20 instructions** — an inline
  `__ubsan_handle_type_mismatch_v1` null/alignment test plus an ASan shadow
  test. `c_token_is_punctuator` is 94 instructions and `string_equal` is 274.
  The consequence is counter-intuitive and was measured three times: **splitting
  an aggregate into separate field accesses is slower than passing it by
  value**, because the by-value copy is one instrumented `memcpy` while each
  field read pays the full pair of checks. Against a `678.81 G` baseline, a
  length/first-byte fast path in `c_token_spelling_equal` measured `695.19 G`,
  the same path with `BUSTER_INLINE` on both predicates `701.03 G`, and routing
  the comparison through a scalar-argument helper `577.96 G` against the
  `537.66 G` state it was applied to. All three were reverted. Do not retry a
  local rewrite of these predicates; only removing the comparison entirely wins.
- **Giving the token a `CPunctuator` id removed another 20% of the suite.**
  `c_token_is_punctuator` reaching `string_equal` had been `14.1%` of the whole
  run, `~19%` counting its own frame and `string_equal`'s ASan fake stack, from
  `1155` call sites over a closed set of 56 spellings. `c_punctuator_length`
  now returns the id it already matched, `CToken` carries it in a `u16` that
  shares the word `pack_alignment` used to own — so `CToken` stays 48 bytes,
  locked by a `BUSTER_CT_CHECK` — and the predicate is `token->punctuator ==
  punctuator`. The run went from `537.91 G` to `430.69 G` instructions
  (`-19.9%`), `24.98 s` to `20.9-21.8 s`; `c_frontend_tests` `15.27 s` to
  `10.7-11.4 s` (`-26%`, and quote the instruction count instead — the wall-time
  spread across two runs of the identical binary is that wide). Compile
  throughput moved with it: self-host stage 1 `29.50 G` to `27.25 G` (`-7.6%`)
  and stage 2 `442.85 G` to `394.65 G` (`-10.9%`), at the byte-identical fixed
  point.
- Two properties make the single compare sound, and a change that breaks either
  one is silently wrong rather than loud. **Only a `C_TOKEN_PUNCTUATOR` token
  may carry a nonzero id**, so the predicate needs no kind test; every site that
  retypes an existing token assigns `punctuator` alongside `kind`.
  **Digraphs keep ids distinct from the punctuators they spell** (`<:` is not
  `[`), because every caller was asking about a spelling. The one place that
  edits a spelling in flight, `c_ir_compound_assignment_operator`, drops the
  `'='` and hands the result to the spelling-driven `c_conditional_operator`, so
  it clears the id that no longer describes it.
- Converting the punctuator tests that went through `c_token_spelling_equal`
  was worth `0.04%` (`430.84 G` to `430.68 G`): they sit on the directive path,
  not the hot loop. It was kept for the invariant, not the number. The
  `C_CONDITIONAL_MATCH` chain in `c_conditional_operator` is still a linear walk
  of string compares and was deliberately left alone — it does not appear in the
  profile, and converting it means reworking the spelling truncation above.
- After the change `string_equal` is `5.6%` and no longer punctuator work: it is
  `c_parse_apply_vector_attribute` and `c_ir_type_name_prefix` matching keywords
  and type names. The largest single buster frame is now
  `c_ir_label_metadata_store_for_place` at `7.4%`, and module shares are
  `c_frontend_tests` `51.5%` (`10.72 s`) against `x86_64_metadata_tests` `39.9%`
  (`8.31 s`) — the two modules are close to tied again, so the next audit should
  not assume the C frontend is where the time is.
- Self-host stage ratio, for the canonical codegen path that runs no register
  allocation: stage 1 `3.45 s` / `29.50 G` instructions, stage 2 `37.96 s` /
  `442.85 G`. That is `11.0x` by wall time but `15.0x` by instructions retired;
  quote the instruction ratio, since wall time on this host varies ~10%.

`2026-08-07` (Linux x86_64, sanitized Debug clang, `perf record -F 999
--call-graph fp` on a quiet host, `65466` samples). This is the CI critical
path; `ide test` totals `~65 s` here:

- `c_frontend_tests` is `72.7%` of the sanitized run (`47.7 s` by
  `TEST_MODULE_TIMING`); `x86_64_metadata_tests` is the only other large module
  at `23.5%` (`15.4 s`). Everything else together is under `4%`.
- **Half of `c_frontend_tests` is ASan's memcpy shadow check.**
  `QuickCheckForUnpoisonedRegion`, inlined into `___interceptor_memcpy` in
  `libclang_rt.asan-x86_64.so`, is `50.3%` of the module's samples.
- Charging that sanitizer cost to the buster frame that provokes it, the module
  is dominated by two one-line predicates in
  `src/buster/lib/compiler/frontend/c/c.c`:
  `c_token_is_punctuator` (`46.3%`) and `c_token_spelling_equal` (`21.4%`),
  then `string_equal` (`8.9%`), `c_ir_label_metadata_store_for_place` (`3.2%`),
  `c_ir_lower_assignment_statement_step` (`2.8%`) and
  `c_type_parse_parenthesized_step` (`2.7%`).
- Mechanism: `CToken` is a 40-byte aggregate passed **by value** into
  `c_token_is_punctuator`, which forwards it **by value again** to
  `c_token_spelling_equal`. At `-O0` clang materializes each by-value aggregate
  argument with a real `call memcpy` (visible in the disassembly of
  `c_token_is_punctuator`, alongside a per-call `__asan_stack_malloc_1`), and
  under ASan every one of those goes through the interceptor and its shadow
  scan. The predicate itself is one enum compare plus one string compare.
- The same profile of an unsanitized Debug build has the same shape without the
  amplification: `c_token_is_punctuator` `38.9%`, `string_equal` `16.8%`,
  `c_token_spelling_equal` only `4.7%`. Sanitizing multiplies the module by
  `3.8x` overall and concentrates the extra cost on the by-value token copies.
- Cost by call site inside `c_frontend_tests` (sanitized): `c_test.c:7229`
  `30.1%`, `:6974` `18.1%`, `:7166` `14.6%`, `:6726` `13.3%`, `:6312` `9.1%`,
  `:7227` `4.3%`. Five of these six are the `c_lower_to_ir`/`c_parse` calls of
  the inline stress blocks; `c_lower_to_ir` accounts for roughly `68%` of the
  module and `c_parse` for `21%`.
- Method validation (independent, non-sampling): direct
  `timestamp_take()` brackets around those call sites in the *same* process
  agree with the sampled shares to within `0.04` percentage points sanitized
  (`7229` `30.99%` measured vs `31.03%` sampled; `6974` `19.76`/`19.77`; `7166`
  `14.59`/`14.61`; `6726` `12.24`/`12.25`; `6312` `8.45`/`8.42`; `7227`
  `4.01`/`4.03`) and to within `0.3` points unsanitized. Every extracted return
  address disassembles to the instruction immediately after a `call` to the
  callee the callchain names.
- **Correction to an earlier note:** `c_test.c:7227` is *not* ~70% of
  `c_frontend_tests`. It is `4.3%` sanitized and `3.6%` unsanitized
  (`0.49 s` of `13.8 s` by direct timing). The `~70%` figure came from
  `perf script -F ip,sym,srcline`, whose raw-return-address resolution shifts
  every call site by 1–2 lines. The dominant single site is `c_test.c:7229`,
  the `c_lower_to_ir` of the same vla-assembly stress block — so the block was
  identified correctly and the line and magnitude were not.

`2026-08-02` (Linux x86_64, Release + Debug, with `--instrument --time-trace`):

- `build/Release` compile time is currently dominated by the unity TU:
  `CMakeFiles/ide.dir/Release/src/buster/apps/ide/ide.c.o` at `20613 ms`
  in `ninja_log_summary build --limit 20`.
- `build/Debug` split compilation hotspots (same summary):
  `parser.c.o` (`707 ms`), `codegen.c.o` (`582 ms`), `analysis.c.o` (`475 ms`),
  `ir.c.o` (`361 ms`).
- Pre-refactor `bench_all` result snapshots (the default was the I/O mode):
  - Release: `BENCH_IO parse_all_tests iterations=200 files=59 min_ns=398673 median_ns=418340`.
  - Debug: `BENCH_IO parse_all_tests iterations=200 files=59 min_ns=941358 median_ns=975512`.
- In `BUSTER_INSTRUMENT` mode:
  - Tokenize: `55_363 ns` median (Release), `297_607 ns` median (Debug).
  - Parse: `48_272 ns` median (Release), `340_853 ns` median (Debug).
- Pre-refactor `BENCH_FILE` output was consistently slowest-first; `tests/basic_variadic.bbb`
  is the dominant long-tail input across both configs.
- Superluminal capture of the old `ide bench` shows `parser_parse_bench` paths flowing
  heavily through `file_read -> os_file_open -> __libc_open64`, indicating
  benchmark procedure repeatedly performs disk reads instead of amortizing source
  input.
- The two-mode benchmark now separates parser throughput from filesystem input;
  compare `BENCH_PARSE` and `BENCH_IO` using their mode-specific phase and
  per-file rows when investigating parser throughput.

`2026-08-02` (Linux x86_64, Release `bench_all`, `BUSTER_INSTRUMENT=1`):

- Before the two-mode refactor, an opt-in preloaded-source benchmark path produced:
  - `BENCH_IO parse_all_tests ... median_ns=421736` (baseline: `file_read` each iteration).
  - `BENCH_PARSE parse_all_tests ... median_ns=90831` (files memory-mapped once, then reused).
- The preloaded benchmark reduced total wall time by roughly `78.5%` (`421736 -> 90831`).
- Tokenize/parse split moved only modestly:
  - Baseline `BENCH_IO_PHASE tokenize` median: `57241`, parse median: `48711`.
  - Preloaded `BENCH_PARSE_PHASE tokenize` median: `50023`, parse median: `39756`.
- The remaining gap between modes is mostly filesystem read overhead in the old
  path; parser/tokenization itself improves only ~12–19%.
- The dominant input is still `tests/basic_variadic.bbb`, but its parse path also
  drops (median `11171 -> 10430`) under preloaded-source reuse.

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
  must not execute through them.
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
- The single typed IR must not retain parser/AST operation identifiers.
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
- Native code generation consumes the typed IR directly; do not introduce a
  second machine IR. The Buster-language backends
  (`codegen_generate_x86_64`/`codegen_generate_aarch64`) perform a conservative
  linear-scan allocation of same-block scalar IR values to backend-owned
  caller-saved registers. Values crossing calls or control-flow edges,
  aggregates, and excess live values retain stack slots as spill storage; block
  parameters are resolved with parallel edge copies. The canonical path
  (`codegen_generate_canonical_module`, used by the C frontend and therefore by
  self-hosting) runs **no** register allocation: every value owns a frame slot
  and every operand is reloaded from it. Its only register reuse is a local
  peephole that drops an x86-64 `rax` frame reload when the immediately
  preceding emission was the store of `rax` to that same slot, tracked by
  buffer position and reset at every block start so no branch can reach the
  elided load. Codegen publishes format-neutral function
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
- Module code generation emits all lowered functions into one image and
  resolves direct-call relocations by `(entity, instantiation)`. Aggregate
  values use frame-owned backing storage while array/slice/range descriptors
  remain ordinary typed IR values; aggregate assignment is a value copy, not
  descriptor aliasing. The x86-64 and AArch64 baselines share this value
  representation even though their stack addressing and instruction emission
  are backend-specific.
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
  records are merged into the shared type stream. Function and global symbol
  locations use their actual final PE section number and section-relative
  offset across text, read-only data, writable data, BSS, and TLS. Readers need
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
| `lsan.supp` | LeakSanitizer suppressions. |
| `build/` | Generated build output (ninja files, per-config dirs, `compile_commands.json`, `build/build`). Never edit. |

`src/buster/lib/` — foundation modules (each `name.c` + `name.h`):

| Path | Contents |
|---|---|
| `base.h` | Root header: platform detection, fixed-width types (`u8`…`s64`), `String8` + `S8("...")`, `STRUCT`/`BUSTER_*` macros, build-mode flags. Header-only. |
| `system_headers.h` | Central OS/libc header includes. `tls.h` is an empty placeholder. |
| `apple_runtime.h` | Objective-C runtime includes and ABI-compatible scalar/geometry types shared by Apple windowing and Metal. |
| `os.{c,h}`, `entry_point.{c,h}` | OS abstraction (processes, virtual memory) and per-platform entry glue (`main`, NativeActivity, UIKit). |
| `arena.{c,h}`, `string.{c,h}`, `integer.{c,h}`, `float.{c,h}`, `hash.{c,h}`, `simd.{c,h}`, `file.{c,h}`, `time.{c,h}` | Arena allocator; `String8` operations and `string_print` (`{S8}` placeholders); numeric parse/format; hashing; SIMD; file IO; clocks. |
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
| `frontend/c/c.{c,h}` | GNU C frontend in progress: source translation, preprocessing tokens/macros/includes/conditionals, non-recursive external-declaration parsing with strong IDs, flattened scalar/pointer/array/function/aggregate types, nested lexical scopes with entity-based identifier binding and canonical redeclarations, and target-aware shared-IR lowering for scalar/pointer/aggregate parameters, locals, static-storage objects, explicit conversions, array decay/indexing, chained field access, control flow, short-circuit and conditional expressions, direct calls, and constant aggregate initialization. |
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
