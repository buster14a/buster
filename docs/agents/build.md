# Build and self-hosting

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

**`generate` deletes and recreates its selected build directory** (`build/`
by default). Finish any build, test, or compatibility-harness run using that
directory before regenerating it; otherwise the running compiler and its
outputs can disappear and be reported as compiler failures. Reuse the
configured tree with `build` for ordinary rebuilds.

## Self-hosting — reproduce first

All contributors—humans and coding agents—should reproduce the current
self-hosting fixed point from the repository root before changing the compiler:

```sh
./build.sh test_self_host --config Release
```

`test_self_host` builds the trusted bootstrap compiler, compiles the complete
unity-build compiler executable twice with its own C compiler, requires both
generations to be byte-identical, and runs the stage-2 benchmark. The same build.c-owned workflow
is also exposed as the `test_self_host` Ninja target. The build-driver command
configures a missing build tree automatically. On Linux x86-64, its expanded
equivalent below assumes a configured tree; on a fresh checkout, first run
`./build.sh generate`:

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -fsource-metrics=build/ide-self.metrics src/buster/apps/ide/ide.c -lm -o build/ide-self
build/ide-self cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -fsource-metrics=build/ide-self-stage2.metrics src/buster/apps/ide/ide.c -lm -o build/ide-self-stage2
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

The stronger Linux x86-64 gate is `./build.sh test_self_host_audit --config Release`
(on an already configured tree). It repeats three generations, compares token,
IR, MIR, diagnostic and binary evidence, and checks every child in all four
allocator modes before reuse. `./build.sh self_host_audit_self_test` exercises
the checker without building the compiler. See [the invariant and evidence
contract](../self-host-audit.md); this does not replace the ordinary gate.

## Build

Three layers: `./build.sh` bootstraps `build/build` from `build.c` using
**tcc**, which then drives CMake + ninja (multi-config, outputs in
`build/<Config>/`).

The GitHub-hosted workflows are the bootstrap exception: the supplementary
privacy broker and `.github/workflows/ci.yml` both compile `build.c` with the
Clang already on the hosted image, because those images ship no TCC and modern
macOS cannot run it, and because the broker must not download an unpinned
compiler before it receives source. Neither produces a trusted or reusable
compiler artifact. The broker also does not run the multi-compiler combination
matrix, because it installs nothing; `.github/workflows/ci.yml` does, after
installing a pinned and checksummed Zig and the distribution's mold, both of
which the images lack. Canonical local and Forgejo workflows continue to
bootstrap with TCC.

Keep build orchestration and policy in `build.c`, with the least practical
process-launch and scripting overhead. Shell and PowerShell scripts exist only
to bootstrap `build/build`; do not grow them into build systems. Use CMake only
to generate/cache the platform build graph and let Ninja execute that graph;
do not implement workflows, iteration, comparison, parsing, timing, or other
general scripting in the CMake language when `build.c` can do the work
directly. Prefer one persistent native build-driver process over chains of
shell, CMake, and utility subprocesses.

```sh
./build.sh generate                 # configure a fresh tree (Debug, clang)
./build.sh                          # build the configured tree (Debug by default)
./build.sh build -t test_all        # build and run the full test suite
./build.sh build --config Release -t test_all
./build.sh generate --sanitize && ./build.sh build -t test_all   # sanitized run
./build.sh test_all_combinations    # the full local matrix CI runs
```

`build/build` commands: `generate`, `build` (default), `clang_analyze`, `test_cjson`, `test_zlib`, `test_lua`, `test_yyjson`, `test_stb`, `test_lz4`, `test_sqlite`, `test_sbase`, `test_doom`, `test_quickjs`, `test_musl`, `test_cpython`,
`cmake_profile_summary`, `ninja_log_summary`, `time_trace_summary`,
`time_trace_summary_self_test`, `test_timing_summary`,
`test_timing_summary_self_test`, `musl_directory_self_test`,
`import_assembly_metadata`, `import_arm_a64_metadata`,
`import_arm_a64_sysregs`, `test_self_host`, `test_mode_matrix`, `test_differential`,
`x86_64_completion_census`,
`test_all_combinations`,
`test_all_combinations_ci`; `self_host_from_existing` is an internal
build-driver worker command used only by the pooled artifact-fanout target.

`musl_directory_self_test` checks complete, unique directory inventories through
two capacity growths, opposite creation orders, manifest sorting and architecture
replacement, empty/error paths, and symbolic-link refusal. It runs before the
local and CI combination matrices on Windows and POSIX; Windows reports when
the host lacks permission to create the test link.

`test_mode_matrix` (`./build.sh test_mode_matrix --config Release`, also a
Ninja target) is the execution-mode cross product: every register-allocator
mode (`none`, `mir-stack`, `fast`, `quality`) against every native target the
toolchain cross-links from any host — x86-64 and AArch64, each as ELF, PE and
Mach-O, 24 legs. Where `test_self_host` is deep on one mode and one target,
this matrix is wide: each leg links a small self-checking fixture corpus
(`basic_c_call_abi`, `basic_c_x86_64_i128_stack_abi`, `basic_c_float_abi`,
`basic_c_vector_register_pressure`) at the default CPU model and then takes
the strongest verification avenue the host offers — native execution when
host and target agree, `qemu-aarch64` for AArch64 ELF, `wine` for x86-64 PE,
and an `llvm-objdump` disassembly oracle for images nothing on the host can
run. A leg whose avenue tool is missing still compiles, links and
oracle-checks, and reports the downgraded avenue in its `MODE_MATRIX` row
rather than vanishing. A leg that must fail belongs in
`mode_matrix_expected_failures` in `build.c` with its issue number; the leg
is then required to fail, so a regression and a silently landed fix are both
caught. Fixture runs are deliberately uncaptured — wine's background services
inherit captured pipe ends and stretch a 10 ms run to seconds — and every
child is bounded by `MODE_MATRIX_TIMEOUT_SECONDS`. The whole matrix costs
about four seconds on a warm tree; CI runs it on the dedicated Linux runner
and on macOS (where the Mach-O rows execute natively), and skips the Windows
runner because that box is the CI wall-time gate and its PE rows already run
under wine on Linux. GitHub CI runs it on all four of its Unix runners, which
is what makes the ELF and Mach-O rows execute natively at both x86-64 and
AArch64; those images carry no wine, so their PE rows stay on the oracle.
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

Ninja targets: `ide`, `test_all` (on Android packages/runs the APK, on iOS
drives the simulator), `bench_all` (desktop only — runs `ide bench`),
`test_self_host` (Linux and Windows x86-64, and macOS), `test_mode_matrix`
(same platforms), `run_ide`,
`test_ide`, `debug_ide`,
`buster_shaders`, `apk` (Android), `clang_analyze`. Rendering backends and
shader compilation are retained as opt-in infrastructure and default off. The
Vulkan SDK (`VULKAN_SDK` env) is required only when Vulkan or Slang shader
compilation is explicitly enabled.
