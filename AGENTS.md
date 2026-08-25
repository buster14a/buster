# AGENTS.md

Instructions for coding agents working in this repository. When this document
and the code disagree, the code wins — update this file in the same change.

## Project

**buster** is a from-scratch C compiler and toolchain written in C. The C
frontend is the sole source-language frontend. The `ide` executable is a
headless compiler, test runner, benchmark driver, fuzz entrypoint, and metadata
tool; the target name is retained for build-script compatibility. The `ide cc`
driver also orchestrates optional external shader toolchains for SPIR-V,
NVPTX/PTX, AMDGCN/HSA code objects, Metal AIR/metallib, and DXIL; these
pipelines add no linked or vendored dependency to the executable.
Native outputs include freestanding PE32+ UEFI applications for x86-64 and
AArch64 alongside the hosted desktop and mobile targets.

The compiler pipeline is:

```text
C source -> preprocessing/parsing/semantic analysis -> canonical IR
         -> native machine IR/code generation -> object writer -> linker
         -> direct core Wasm64 emission for wasm64 targets
```

The experimental custom-language frontend, its semantic model, its
source-level interpreter/JIT entrypoints, the editor model built around it,
and their C implementation and test files are intentionally removed. The
`.bbb` fixture corpus and detailed former guidance remain as preservation
material for a future redesign, but are absent from the generated build graph,
unity translation unit, test registration, benchmarks, packaging, and CI.
Shared active facilities remain: the canonical IR, optimizers, machine
selection and scheduling, register allocators, x86-64 and AArch64 encoders,
debug emitters, object formats, linker, generic object JIT, assembler, and
Wasm64 backend. See `DORMANT_CUSTOM_COMPILER.md` for the historical reference.

## Current compiler priority

Prioritize compiler throughput and producing an artifact as soon as possible.
Do not add optimization passes or spend compile time improving generated code
unless that work is explicitly requested. Prefer direct, predictable lowering
and code generation with minimal analysis overhead.

The long-term implementation direction is to make the compiler broadly
SIMD-friendly: prefer compact contiguous data, batchable lane work, masks for
tails and irregular active sets, branch-regular loops, and layouts that can
evolve from scalar to vector processing. Reducing branch and cache misses is
therefore an architectural goal as well as a present-day performance lever.
Keep the measurement discipline explicit: record SIMD-readiness and miss-rate
improvements, but do not describe a scalar throughput regression as a current
speedup merely because a proxy improved.

### Data-oriented compiler design

- Optimize the real workload, especially its common cases, rather than every
  theoretically possible case. Organize and measure the data before optimizing
  the code that transforms it.
- Where there is one, look for many — including along the time axis. Before
  designing a scalar one-off path, look for the population across instructions,
  values, blocks, functions, modules, repeated queries, or successive compiler
  phases, and preserve enough context to process that population together.
- Put frequently processed values together and separate different states and
  cold metadata. Maximize useful information per cache line; avoid large hot
  objects, padding, scattered flags, and pointer graphs that spend bandwidth
  carrying data the current operation does not use.
- Replace per-object, last-minute decisions with explicit classification
  followed by homogeneous batch processing. Prefer masks, compact indices,
  active counts, command buffers, state-specific arrays, and other forms that
  can evolve into SIMD lanes over repeatedly testing independent booleans.
- Do not reread or recompute facts already available. Hoist invariant reads,
  calls, and branches even when an optimizing compiler might discover the same
  transformation. Make constraints explicit so hot interfaces retain the
  context needed to avoid generic work.
- Precompute offline or during serial prewarm whenever practical. The cheapest
  runtime operation is one that no longer exists; keep cold malformed-input and
  fallback handling outside regular hot loops where the public contract allows
  it.
- Estimate before implementing: organize the cases, rank them by
  `probability * count`, calculate approximate memory and compute cost, then
  measure. Track useful information per cache line and passes per item, not
  merely time attributed to a function.
- Apply the process recursively. After improving one hot data transformation,
  inspect the next largest remaining source of waste. Preserve maintainability,
  debuggability, determinism, concurrency, and portability while doing so;
  good data organization should improve all of them.
- For buster, favor SoA or compact projections for hot compiler facts, explicit
  active-lane masks and counts instead of fixed-maximum scans, grouped machine
  instruction states where ordering permits, and prewarmed compact plans rather
  than repeated metadata interpretation. Treat AVX-512 as a consequence of a
  sound data layout, not the starting point: a compiler can optimize the final
  instruction sequence, but it cannot repair wasteful data movement or context
  that the program discarded.

### SIMD transformation rules

- Choose the data layout before choosing SIMD instructions. Prefer SoA for
  homogeneous transforms, use hybrid/tiled layouts only when measurements show
  a scalar/SIMD tradeoff, and locally project fixed external AoS input into the
  compact form a hot transform needs. Public interfaces do not have to mirror
  internal storage.
- For small divergent operations, compute candidate results and select with a
  mask. For larger or more expensive divergence, first partition or compact
  stable indices, then run a dedicated homogeneous kernel over each set. A hot
  data-dependent branch needs strong measured predictability; a predictable
  loop branch is not a problem merely because it is a branch.
- Treat compare masks as useful data. Prefer compare -> mask -> compact/select
  pipelines, advance compact outputs by mask population count, and use
  precomputed permutation controls when a direct compress instruction is not
  available. Preserve stable order whenever output order is observable.
- Do not add software prefetch to linear streams the hardware already handles.
  Consider it only for measured, sufficiently distant irregular pointer/index
  accesses, and validate every supported microarchitecture. Likewise, avoid
  habitual unrolling beyond the natural SIMD width: inspect generated code,
  register pressure, and spills first.
- Use non-temporal loads or stores only to prevent demonstrated cache pollution,
  after the ordinary kernel is correct. Their ordering and visibility require
  an explicit fence and ownership proof; they are never a cosmetic replacement
  for normal memory operations.
- Inspect the generated instructions for every explicit SIMD kernel. Count
  useful lanes, loads/stores, shuffles, dependencies, spills, and work per
  iteration. Wider code is a win only when the measured bottleneck is compute
  rather than memory bandwidth and the target hardware executes that width
  economically.

## Self-hosting — reproduce first

All contributors—humans and coding agents—should reproduce the current
self-hosting fixed point from the repository root before changing the compiler:

```sh
./build.sh test_self_host --config Release
```

`test_self_host` builds the trusted bootstrap compiler, compiles the complete
unity-build compiler executable twice with its own C compiler, requires both
generations to be byte-identical, and runs the stage-2 benchmark. The same build.c-owned workflow
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

The opt-in cJSON compatibility harness takes an external, pristine cJSON
1.7.19 checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
./build/build test_cjson --config Release /path/to/cjson-v1.7.19
```

The checkout must be commit
`c859b25da02955fef659d658b8f324b5cde87be3` with no tracked or untracked
changes. The harness compiles `cJSON.c`, `cJSON_Utils.c`, Unity and every
upstream test with Buster, links/runs the 18 core and 3 utility tests through
the system linker, compares a deterministic parse/print round trip with
Clang, and exercises FAST, NONE, MIR_STACK and QUALITY. Generated objects,
metrics and logs remain under `build/cjson-v1.7.19-<pid>/`.

The opt-in zlib compatibility harness takes an external, pristine zlib v1.3.1
checkout; upstream sources are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_zlib --config Release /path/to/zlib-v1.3.1
```

The checkout must be commit
`51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` with no tracked or untracked
changes. The harness explicitly compiles all 15 zlib library translation units
with Clang and Buster and the example, minigzip, and infcover test units with
Buster, builds `libz.a`, exercises FAST, NONE, MIR_STACK and QUALITY, cross-links both
archive directions, compares a deterministic compression/decompression probe
and minigzip corpus hashes with Clang, and records source metrics plus actual
compression-workload throughput. After the explicit manifest passes, it
extracts a clean upstream archive and runs its unmodified configure script and
`libz.a` make target with the selected Buster driver. Generated objects, metrics,
archives, and logs remain under `build/zlib-v1.3.1-<pid>/`.

`build/build` commands: `generate`, `build` (default), `clang_analyze`, `test_cjson`, `test_zlib`,
`cmake_profile_summary`, `ninja_log_summary`, `time_trace_summary`,
`time_trace_summary_self_test`, `test_timing_summary`,
`test_timing_summary_self_test`,
`import_assembly_metadata`, `test_self_host`, `test_all_combinations`,
`test_all_combinations_ci`; `self_host_from_existing` is an internal
build-driver worker command used only by the pooled artifact-fanout target.
The combination matrix shares one multi-config build tree across configurations
when their configure-time policy matches. Clang omits unsanitized Debug because
sanitized Debug provides the stronger coverage; it builds and runs unsanitized
Release plus sanitized Debug and Release. Non-Apple Clang configurations use
dedicated trees because their fuzz-runtime policy differs, while AppleClang's
two sanitized configurations share one cross-config Ninja graph. GCC and Zig
compile unsanitized Debug only and do not execute it; MSVC does the same on
Windows. Only optimized, unsanitized Clang/AppleClang builds use the requested
unity build; every other build uses split translation units. TCC is retained
only as the bootstrap compiler for `build.c` and is omitted from all
application/compiler combinations.
The matrix configures its compiler trees in parallel, then uses
`cmake/superbuild/CMakeLists.txt` for one outer `cmake --build` invocation.
Each shared compiler tree builds Debug and Release through one cross-config
Ninja process, so concurrent builds never write the same `.ninja_deps` or
.ninja_log`. One shared CMake job pool admits the outer compiler and test
commands, so a completed tree can release its slot to its tests while other
trees continue compiling. On hosts with four or fewer logical CPUs, it admits
one tree per CPU and sets every inner Ninja and `BUSTER_TEST_JOBS` quota to
one; the six-tree Windows matrix therefore runs four one-job compiler trees
at a time without nested oversubscription. Larger hosts retain the weighted
allocator: split trees share at least two logical CPUs per admission slot while
unity trees use one job. Clang tests then run concurrently in the same bounded
pool, with each tree's quota passed through `BUSTER_TEST_JOBS`; future multithreaded test work
must honor that limit. Application builds are multithreaded by default;
`./build.sh generate -DBUSTER_SINGLE_THREADED=ON` is the explicit serial
fallback. A single compile is serial throughout: the compiler library starts no
lanes of its own, so build-level concurrency is the only thing that has to be
budgeted. Trees are declared longest-first — sanitized Debug,
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
forcing producer objects and links to rebuild under the captured inputs. The worker depends only on the canonical compile target,
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
The fixed-point pair continues to use the default FAST allocator, and the
existing non-Windows machine stage continues to compile and run its benchmark
with `-fregister-allocator=mir-stack`. That stage is followed by a canonical
compile-only gate: the stage-2 compiler builds `ide-stage2-none` with
`-fregister-allocator=none`. It does not run that output because the regression
this gate protects against is a canonical argument-capture crash while
compiling `ide.c`; launching a second benchmark would add CI work without
covering that path. NONE is the only allocator stage here that reaches the
canonical emission path; QUALITY exercises the same machine path already
covered by FAST/MIR_STACK and is covered by focused/all-mode tests, so another
full unity compile would add CI cost without distinct self-host coverage.
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
Clang static analysis runs only against unsanitized Release. Every Clang matrix
configuration runs `test_all`; GCC, Zig, and MSVC are compile-only, and platform
packages use their native test runner.
The one carve-out inside `test_all` is a **whole-table audit**: a module whose
result is a function of the generated metadata tables and the repository's
source text alone, so no compiler, configuration or optimization level can
change its answer. Those modules run on the same single canonical tree per
platform that already owns `clang_analyze` — unsanitized optimized Clang —
because re-deriving one identical answer in eight to ten configurations cost
more than any other single thing in CI. Mark such a module with
`table_audit` in the `test_descriptors` table of `src/buster/tests/test.c`;
`x86_64_completion_census_tests` is the current one. The superbuild opts a
tree out with `BUSTER_TEST_TABLE_AUDITS=0`, and **the default is on**, so a
bare `ide test`, a single-tree build, or any runner that does not set the
variable keeps full coverage. Never mark a module whose result depends on
generated code, the host, or the sanitizer.
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
Native x86-64 and AArch64 compilation uses the FAST register allocator at
every optimization level, including the default and `-O0`, while
`-fno-register-allocator` selects the canonical stack emitter. Advanced and
diagnostic callers may select `none`, `mir-stack`, `fast`, or `quality` with
`-fregister-allocator=<mode>`; when several allocator-affecting options are
present, the last one wins.
The allocators run on x86-64 under both System V and Win64, and on AArch64
everywhere but the PE-unwind targets, which still take the canonical path
whole. Win64 differs from System V in the file it allocates — RSI and RDI are
callee-saved there, so the allocator has seven callee-saved registers instead
of five and the vector class keeps only the volatile ZMMs — and in how a call
is built: the outgoing arguments and the callee's shadow space are written
into a fixed area at the bottom of the frame instead of pushed, because the
stack pointer must not move inside the body of a function whose unwind data
can only carry a frame-pointer offset up to 240 bytes. Its prologue pushes the
callee-saved registers before establishing the frame pointer for the same
reason. Shapes the Win64 subset does not build yet — variadic definitions and
calls, 128-bit integers, vector signatures, indirect (non 1/2/4/8-byte)
aggregate arguments, and dynamic stack allocation — fall back per function,
which `-v`'s `fallback_functions` and `CODEGEN_FALLBACK` lines report.
`-emit-llvm` emits binary LLVM bitcode directly from canonical typed IR for C
inputs. It writes `<input>.bc` by default, accepts `-o` for a single
input, and rejects native objects, archives, libraries, frameworks, linker
arguments, `-E`, `-S`, and `-fsyntax-only`. The writer has no LLVM dependency;
see `LLVM_BITCODE.md` for its target metadata, API, and supported boundary.

Ninja targets: `ide`, `test_all` (on Android packages/runs the APK, on iOS
drives the simulator), `bench_all` (desktop only — runs `ide bench`),
`test_self_host` (Linux x86-64 and macOS), `run_ide`, `test_ide`, `debug_ide`,
`buster_shaders`, `apk` (Android), `clang_analyze`. Rendering backends and
shader compilation are retained as opt-in infrastructure and default off. The
Vulkan SDK (`VULKAN_SDK` env) is required only when Vulkan or Slang shader
compilation is explicitly enabled.

## Tests

- All tests run inside the `ide` executable; there is no external unit-test
  framework. From the repository root, run `ide test --verbose=1 --ci=1` or
  build the `test_all` target.
- Test modules live under `src/buster/tests/` as mirrored `*_test.c` and
  `*_test.h` pairs. `src/buster/tests/test.c` owns registration. Unity builds
  include implementations into the main translation unit; non-unity builds
  compile each test source independently.
- C frontend and driver fixtures live under `tests/` and use `.c`, `.h`, native
  object, archive, and shell-script inputs. Keep fixture paths relative to the
  repository root because tests intentionally exercise the real file loader.
- Dormant `.bbb` fixtures remain under `tests/` as preservation material. Do
  not register, compile, parse, benchmark, package, or execute them in the
  default build or CI until the custom frontend is deliberately reactivated.
- A new production module or public behavior must receive a focused module
  test. Frontend changes should cover preprocessing, parsing/diagnostics,
  semantic typing, canonical-IR lowering, and driver behavior as applicable.
- Keep test-only declarations behind `BUSTER_INCLUDE_TESTS`. Private structures
  shared with tests belong in a narrow `*_internal.h` seam rather than being
  exposed through a production public header.
- CI is defined under `.forgejo/`, not GitHub. Preserve Debug/Release,
  unity/non-unity, sanitizer/fuzz, self-host, and supported-platform coverage
  when changing build orchestration or the compiler pipeline.

## Benchmarking and diagnostics

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

## Forge, issues, and pull requests

The forge is Forgejo at `https://code.buster14a.com/buster/buster`, not GitHub;
CI lives under `.forgejo/`. Talk to it with **`fj`**, the Forgejo CLI. It reads
the repository from the git remote, so run it from inside a checkout (or pass
`-C <path>` / `-r buster/buster`).

`fj` needs a token once per machine. The password half of the
`code.buster14a.com` line in `~/.git-credentials` is a valid API token, so
authentication is a pipe, not a browser round trip:

```sh
grep code.buster14a.com ~/.git-credentials \
  | sed 's|https://[^:]*:||; s|@code.buster14a.com.*||' \
  | fj auth add-token -H code.buster14a.com
fj whoami          # verify: davidgmbb@code.buster14a.com
```

`fj issue create "<title>" --body-file <path> --no-template`,
`fj issue search [-s open|closed|all]`, `fj issue view <n>`,
`fj issue comment <n>`, and `fj pr create --base main --head <branch>
--body-file <path>` are the whole working set; `fj pr search`, `fj pr status`
and `fj pr view` read the other side. Two things to know before scripting it:
the subcommand for listing issues is `search`, not `list`, and **omitting both
`--body` and `--body-file` opens `$EDITOR`**, which hangs a non-interactive
session — always pass a body file. Write the body as a file rather than a
shell string: backticks inside `$(cat <<EOF)` get command-substituted by zsh,
which has mangled a commit message before.

`fj` supersedes the older workarounds. `tea`'s login for this host has no
token, and the raw-`curl` recipe that went with it needed a browser
`User-Agent` to get past Cloudflare's `403 error code: 1010`; `fj` is not
subject to either problem.

**Issues are the task queue.** Work that is real but not being done right now
becomes an issue, not a paragraph in an audit that nobody will find — a chip
filed against a memory is invisible to the next agent, while an issue is
something a fresh session can pick up cold. Write the body as a **prompt**: what
is wrong and how it was diagnosed, the file and symbol names to start from,
the constraints and do-not-retries that earlier work already paid for, how to
validate the fix (which oracle, which harness, which counters), and a
definition of done. State what was measured and when, so a stale claim is
recognisable as stale; the tree moves fast enough that a count quoted without
a date is a trap. Issues #537-#549 are the current examples of the form.

## Core rules

- **Code must stay human-maintainable.** Every major file opens with an
  orientation header — what it owns, its entry points, and, for large files,
  a layout map anchored to greppable definition names, never line numbers.
  Update the header in the same change that moves what it describes. A value
  that must agree across several sites (a chunk size that a capacity
  computation and an emitter both depend on, a limit one file checks and
  another file exploits) is a named constant, not a respelled literal; a
  genuinely one-off format field keeps its literal with a comment naming it.
  Comments state constraints the code cannot show; a claim that has not been
  verified against the code does not get written down.
- **One return per function.** A function has a single exit: compute the
  answer into one result variable, let control flow converge, and `return` it
  once at the end. Guard clauses, mid-loop `return`s, and per-case `return`s in
  a `switch` all become assignments to that variable followed by ordinary
  structured flow — an `else`, a loop condition, a `break`. Prefer restructuring
  the condition over adding nesting; where an early exit skipped work that is
  now merely wasted, keep it skipped with a `done` flag or a loop guard rather
  than reintroducing a second `return`. A single exit is what makes an epilogue
  — a cleanup, a trace point, an arena reset — impossible to leak past. `goto`
  to a shared tail is a second exit spelled differently; it is allowed only for
  the error-unwind ladders that already exist, never as a way to keep an early
  return. Returns in mutually exclusive `#if`/`#elif`/`#else` arms are one
  return — every configuration compiles exactly one of them — so a
  platform-dispatch body already satisfies this rule and does not need a result
  variable threaded through the preprocessor. A `switch` that used to return a
  value per case becomes an undecorated `Type result;`, one `result = ...` per
  case, and `default: BUSTER_TODO();` for the impossible case: `BUSTER_TODO`
  never falls through, which is what leaves `result` definitely assigned without
  a placeholder initializer that a real bug could hide behind. Do not initialize
  the result variable when every path already assigns it: `clang_analyze` reports
  that store as dead and fails the Release tree, and the rule's whole point is
  that the paths, not an initializer, decide the answer.
- `BUSTER_F_DECL` belongs on header declarations only. In a `.c` file, a
  module-local function is `BUSTER_GLOBAL_LOCAL`, and a function that a header
  already declares carries no macro at all — the header declaration is what
  gives it internal linkage in the unity build.

## Machine instruction selection and scheduling

- `MachineInstruction` is the 24-byte hot row. Keep static form, scheduling,
  memory, bundle, fixed-register, tie, early-clobber, implicit-physical, and
  implicit-resource facts in `MachineOpcodeInfo`, accessed through the
  `machine_opcode_*` helpers.
- `MachineFunction` owns CFG edges, block parameters, and incoming edge
  parallel-copy sources. Edge source `i` maps to destination block parameter
  `i`; keep these copies parallel through allocation so cycles are resolved as
  copies rather than serialized selector moves. Replay files include all three
  arrays and use the current replay version.
- Shared canonical-IR facts and the generated FAST/QUALITY rule decision tree
  live in `machine_select.{c,h}`, `machine_select_rules.h`, and
  `machine_select_generated.c`. Target selectors may retain custom ABI and
  complex lowering, but must consume shared facts instead of introducing a
  third permanent graph IR.
- x86 ADD/SUB/AND/OR/XOR/IMUL rows are three-operand machine SSA with operand
  0 tied to operand 1. Allocators satisfy the physical two-address constraint;
  selectors must not reintroduce a MOV plus mutable USE_DEFINE result.
- QUALITY scheduling remains pressure-first and deterministic. Pressure is
  counted per register class; metadata supplies barriers, memory membership,
  and vector scheduling membership while compatibility opcode classifiers
  cover legacy rows during migration.

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
  by batch passes, not pointer graphs walked element-at-a-time. Wojciech
  Muła's notes (`http://0x80.pl/notesen.html`; fetch over plain HTTP — the
  host's HTTPS certificate is broken) are the project-endorsed catalogue of
  these kernel shapes: before writing a byte- or word-granularity loop, check
  whether one of his notes already solves it branchless with masks, `vpshufb`
  or `vpermb` lookups, `vpternlogd`, or compaction. Start from "Modern
  perfect hashing for strings", "SIMD-ized check which bytes are in a set",
  "AVX512VBMI — remove spaces from text", "AVX512VBMI2 and packed varuint
  format", "Parsing decimal numbers" parts 1–2, "SIMD-friendly algorithms
  for substring searching", and "AVX-512 conflict detection". Parallel
  stages must stay deterministic — write results into slots indexed by work
  item, never by completion order — so the self-hosting fixed point stays
  byte-identical at any lane count. `lane_run` keeps a persistent worker gang
  on the calling thread context and reuses it across phases; do not add phase-
  local thread creation. Variable-duration work uses an atomic take-index,
  writes function- or item-local fragments into stable source-indexed slots,
  and merges them only after a lane barrier.
- **A global built on first use is prewarmed, never raced.** Several hot
  tables are derived once and read forever after through plain loads — the C
  frontend's character classes, compact lexer tables, punctuator dispatch and
  declaration keywords, the codegen ABI target cache, the font provider's
  resolved paths, and the x86 metadata decode plus the three caches over it.
  That shape is sound only while no other thread can be reading, so every such
  build states `BUSTER_CHECK_SERIAL_INITIALIZATION()` (`<buster/lib/os.h>`,
  always compiled in, over `os_is_only_live_thread()`), and every owning
  module publishes a prewarm entry point that fills it on the calling thread:
  `c_prewarm`, `codegen_prewarm`, `font_provider_prewarm`,
  `buster_x86_metadata_prewarm`, and `compiler_prewarm` for the compile
  pipeline as a whole. **Call the prewarm
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
  its spelling in `c_symbol_predefined` (`c_source.c` — without it the
  self-hosted stages fail with "could not lower unbound identifier"),
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
- **SIMD C lexing method: the Validark lineage.** `c_lex_compact` in
  `frontend/c/c_source.c` draws on Niles Salter's (Validark's) Accelerated Zig
  Parser — local checkout `~/dev/Accelerated-Zig-Parser`, upstream
  `github.com/Validark/Accelerated-Zig-Parser` — plus validark.dev (start with
  `posts/deus-lex-machina` and
  `posts/eine-kleine-vectorized-classification`) and the three Utah Zig talks
  (YouTube `oN8LDpWuPWw`, `FDiUKafPs0U`, `NM1FNB5nagk`). When accelerating the
  C lexer, reach for these techniques first and keep new code compatible with
  their shapes. The core vocabulary: (1) per-64-byte-chunk classification —
  every class (whitespace, newline/CR, alpha/digit/underscore, quotes,
  backslash, slash, operator chars) becomes one comparison into a `k`-mask
  bitstring, all classes over the same chunk in lockstep sharing one load; (2)
  token extents by mask arithmetic — starts `x & ~(x << 1)`, ends
  `x & ~(x >> 1)`, then either cursor queries (shift by cursor, count trailing
  ones — one tzcnt replaces an unpredictable byte loop) or full vector
  compaction: `vpcompressb` an iota vector through the starts and ends masks,
  subtract, and every token length in the chunk materializes at once; kinds
  come from masked broadcasts into a kinds vector compressed by the same
  starts mask, interleaved with lengths on store; (3) charset membership via
  nibble-decomposed `vpshufb`/`vpermb` tables —
  `table[c & 0xF] & (1 << (c >> 4))` per lane, the upper-nibble powers-of-two
  vector shared across charsets, `vptestmb` folding the AND and test on
  AVX-512; (4) multi-character operators as a bit-channel `vpshufb` NFA: three
  per-position tables whose looked-up bytes AND together so each of the 8
  bit-channels legalizes one family of 2-/3-char sequences, plus a short
  effectively-branchless reconciliation of overlaps; (5) keyword and builtin
  recognition by perfect hash, never a memcmp ladder —
  `((len << 14) ^ first_two_bytes) * last_two_bytes >> 8` to 7 bits, Bagwell
  array-mapped compression (two u64 bitmaps + popcount rank) into a dense table
  of length-padded entries validated by a single wide compare, with
  compile/startup-time collision checks so editing the keyword set stays safe;
  (6) sentinels instead of bounds checks — a leading newline, trailing
  quote/NUL sentinels, and chunk-aligned overallocation let the hot loops drop
  every length test; (7) upper-bound allocation plus post-scan shrink instead
  of grow-and-check (the `2026-08-08k` tokenizer change is this trick alone,
  `-21.6%` bench instructions); (8) length-based token streams — kind + length
  with a 0-length escape to a wide length, no per-token start offsets or
  eagerly materialized line/column, offsets rebuilt by a running cursor and
  line numbers recovered by popcount over retained newline bitmasks.
  Escape-run parity uses the simdjson backslash algorithm (simdjson PR #2042
  has the current best form). All of it sits behind the guard rules of the
  tuning target above; scalar fallbacks keep the exact current semantics.
  `c_lex_compact` walks source in **item-aligned 64-byte windows** so no lexer
  state crosses a window: the item touching a window's last byte is deferred
  and rescanned by the next one, and shapes the masks do not model escape to
  the scalar single-item scanner `c_lex_scan_one`. C skips whitespace and
  comments instead of tokenizing them, so token ends need an explicit boundary
  mask rather than `starts >> 1`; lookahead loads are masked by the **file**
  bounds rather than the window's because a delimiter near the window end can
  be spelled from bytes the next window owns. Every SIMD lexer change must keep
  the differential gate asserting byte-identical agreement with the scalar
  reference over construct cases slid across the window boundary, items longer
  than a window, the real corpus at every window phase, and fuzz blobs over the
  full alphabet.
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

## C frontend and canonical IR rules

- The public frontend API is `compiler/frontend/c/c.h`. In non-unity builds the
  implementation is split across `c_source.c`, `c_parse.c`, and `c_gen.c`;
  `c.c` preserves the unity include order and diagnostic mapping.
- Keep the frontend pipeline explicit: source loading and preprocessing,
  parsing and semantic construction, then canonical-IR lowering. Do not add a
  parallel frontend-specific IR or route code generation around canonical IR.
- Invalid user input must produce structured C diagnostics and a failed driver
  result. Assertions and `BUSTER_TODO()` are for violated internal invariants,
  never ordinary syntax or semantic errors.
- Arena ownership is part of the API contract. Returned source, syntax,
  semantic, and IR structures may reference earlier-stage storage; callers must
  retain the translation-unit arena until every downstream consumer finishes.
- Zero-initialize aggregate tables before publishing a partially resolved type.
  Recursive and mutually dependent declarations can expose an aggregate while
  later members are still unresolved; an uninitialized `IrField` must never be
  mistaken for a valid type or source reference.
- Canonical IR owns format-neutral functions, blocks, instructions, values,
  symbols, source ranges, types, and relocations. Shared layers must not contain
  frontend entity IDs, parser AST pointers, or language-specific invalid
  sentinels.
- Validate IR before machine selection or Wasm emission. A diagnosed frontend
  failure must not publish an apparently valid partial function to codegen.
- `IrFunction.opcode_summary` answers *may this function contain opcode X*
  without a scan, and it answers only for the `IR_OPCODE_SUMMARY_TRACKED`
  list: an opcode outside that list is never recorded, so
  `ir_function_may_contain_opcodes` rejects a query naming one rather than
  reporting a confident absence. Adding a query means adding its opcode to
  the list in the same change. The summary is exact only for functions
  created by `ir_module_add_function` and filled by
  `ir_function_add_instruction`; IR whose rows were written straight into
  `instructions` reads as unknown and every consumer keeps a scan for it.
- Source diagnostics in shared layers use canonical `IrSourceRange` and
  `IrSourcePosition`. Do not reintroduce parser-specific source-range APIs into
  codegen, debug information, object writing, or the linker.
- Native lowering is `canonical IR -> machine IR -> scheduling/register
  allocation -> encoding`. Selection patterns and scheduling classes remain
  separate metadata domains even when they share instruction-form IDs.
- The generic JIT loads already-produced host-native objects and resolves
  explicit bindings. It is not a second source-language compiler and must stay
  independent of frontend semantic structures.
- The command-line driver accepts C source/preprocessed C plus native
  objects/archives where the selected action permits them. Unknown languages,
  retired module-root options, and unsupported source extensions must fail
  explicitly rather than being forwarded or guessed.
- The Wasm64 backend consumes canonical IR directly. Unsupported ABI or
  instruction shapes must be diagnosed; never silently fall back to a native
  backend.

## Repository map

Top level:

| Path | Contents |
|---|---|
| `build.c`, `build.sh`, `build.ps1` | Native build driver and its bootstrap scripts. |
| `CMakeLists.txt` | Compiler detection, warnings, sanitizers, module graph, targets, and platform packaging. |
| `src/buster/apps/` | Command-line application entrypoints and standalone tools. |
| `src/buster/lib/` | Runtime, platform, compiler, assembler, linker, JIT, and retained UI/rendering libraries. |
| `src/buster/tests/` | In-process unit/module tests. |
| `tests/` | C frontend, driver, object/archive, fuzz, and CI-script fixtures. |
| `tools/` | Python generators, scanners, and measurement scripts run by hand; outside the build graph. |
| `.forgejo/` | Forgejo CI workflows and scripts. |
| `PERFORMANCE_AUDITS.md` | Index of the append-only measurement history; one line per audit. |
| `docs/performance-audits/` | One file per audit, named for its id; older entries may describe components that no longer exist. |
| `WASM64.md` | Direct core Wasm64 target contract and usage. |
| `LLVM_BITCODE.md` | Direct LLVM bitcode output, driver usage, emitter API, validation, and current limitations. |
| `docs/uefi-target.md` | Freestanding UEFI target contract, driver usage, firmware ABI, image layout, relocations, and limitations. |
| `build/` | Generated output. Never edit or archive it as source. |

Compiler (`src/buster/lib/compiler/`):

| Path | Contents |
|---|---|
| `frontend/c/c.h` | Public C frontend API. |
| `frontend/c/c_source.c` | Source loading, lexing, preprocessing, includes, macros, and source metrics. |
| `frontend/c/c_parse.c` | Parsing, declarations, scopes, types, semantic analysis, and diagnostics. |
| `frontend/c/c_gen.c` | Target-aware lowering from analyzed C into canonical IR. |
| `frontend/c/c.c` | Unity-build aggregator for the three C frontend implementation files. |
| `ir/model.h`, `ir/ir.{c,h}` | Canonical typed IR model, construction, validation, and printing. |
| `assembly/` | Standalone x86-64/AArch64 assembly parsing, metadata, semantics, and encoders. Generated metadata stays under `assembly/generated/`. |
| `codegen/machine*.{c,h}` | Machine IR, instruction selection, scheduling, ABI lowering, and target-specific emission. |
| `codegen/register_allocator_*.c` | Fast and quality register allocators. |
| `codegen/codegen.{c,h}` | Canonical-IR-to-native-code orchestration and codegen statistics. |
| `debug/`, `dwarf/`, `codeview/`, `pdb/` | Canonical debug model and platform debug-format emitters. |
| `object/object.{c,h}` | Format-neutral object model plus ELF64, COFF, and Mach-O readers/writers. |
| `link/link.{c,h}` | Section merging, symbol resolution, hosted native executable linking, and imports-free PE32+ UEFI application output. |
| `jit/jit.{c,h}` | Host-native in-process object loader with explicit imports and W^X finalization. |
| `wasm/wasm.{c,h}` | Direct canonical-IR-to-core-Wasm64 emitter using Memory64. |
| `gpu/gpu.{c,h}` | Target parsing, deterministic command planning/execution, tool discovery, temporary ownership, and artifact validation for external SPIR-V, NVPTX/PTX, AMDGCN/HSA, Metal AIR/metallib, and DXIL pipelines. |
| `llvm/bitcode.{c,h}` | Dependency-free canonical typed-IR to binary LLVM bitcode emitter. It writes the bitstream directly, preserves deterministic value numbering, records target metadata, and diagnoses unsupported IR instead of routing through textual LLVM IR. |
| `driver/driver.{c,h}` | Clang-like C command-line parsing and end-to-end preprocess/compile/assemble/object/link dispatch, including hosted and freestanding UEFI links, plus direct LLVM bitcode output and isolated external GPU-pipeline orchestration. |

Applications:

| Path | Contents |
|---|---|
| `src/buster/apps/ide/ide.c` | Headless `ide` executable: `cc`, `test`, `bench`, fuzzing, and x86-64 completion census. The name is retained for build compatibility. |
| `src/buster/apps/disk_builder.c` | Standalone disk-image builder; not part of the default CMake target. |
