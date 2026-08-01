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
is also exposed as the `test_self_host` Ninja target. Its expanded equivalent
is:

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 src/buster/ide/ide.c -o build/ide-self
build/ide-self cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 src/buster/ide/ide.c -o build/ide-self-stage2
cmp build/ide-self build/ide-self-stage2
build/ide-self-stage2 bench
```

The target and fixed point are currently available on Linux x86-64. Preserve
them when changing preprocessing, C semantics, IR, code generation, object
writing, or linking, and report both self-hosting failures and benchmark
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
`test_self_host`, `test_all_combinations`, `test_all_combinations_ci`.
Flag scope matters: `--sanitize`, `--fuzz`, `--lto`, `--ci`, `--time-trace`,
`--instrument`, `--cc <clang|gcc|tcc|zig|cl>` are accepted **only by
`generate`**; `build` rejects them with an explicit diagnostic.
`--optimize`/`--no-optimize` are configuration shorthands for
Release/Debug and never create separate cached optimization state. `build`
accepts `--config <name>`, `--optimize`, `--target/-t <ninja target>`, and
`--verbose/-v`. Booleans have `--no-` twins; `--` passes the rest through to
Ninja (`build`) or CMake (`generate`).

The Clang-like `ide cc` driver accepts `-march=<model>` and
`-mcpu=<model>` (or their separated forms). CPU names use the canonical
spellings printed by `cpu_model_to_string_os`, such as `baseline`, `native`,
`haswell`, `znver5`, and `apple-m4`; incompatible target/model pairs are
diagnosed. `-v` reports the selected CPU and maximum native vector width.

Ninja targets: `ide`, `test_all` (on Android packages/runs the APK, on iOS
drives the simulator), `bench_all` (desktop only — runs `ide bench`),
`test_self_host` (Linux x86-64 only), `run_ide`, `test_ide`, `debug_ide`,
`buster_shaders`, `apk` (Android), `clang_analyze`. The Vulkan SDK
(`VULKAN_SDK` env) is required whenever Vulkan or Slang shader compilation is
enabled.

## Tests

- All tests use the `ide` executable with no external test framework.
  `test_all` runs `ide test --verbose=1`, which calls `library_tests()` in
  `src/buster/test.c`; fuzz-capable builds then run their bounded fuzz session
  in that process. Desktop CI runs the IDE window/rendering path separately as
  `ide test_app --verbose=1 --ci=1`, so external Vulkan/LLVM sanitizer policy
  cannot weaken unit-test or fuzz coverage. Android and iOS retain the combined
  in-app test and counted graphical smoke-test flow.
- Run from the **repo root**: parser tests open `tests/*.bbb` by relative
  path.
- To add a language test: drop a `.bbb` file in `tests/` **and** append it to
  the hardcoded `parser_file_test_cases` list in
  `src/buster/compiler/frontend/buster/parser.c` (covered by
  `parser_file_tests()`). Valid fixtures must also be appended to
  `analysis_fixture_tests` in `analysis.c` with their exact semantic diagnostic
  count and to `ir_fixture_tests` in `ir.c`; this keeps the complete frontend
  pipeline covered. Invalid-syntax fixtures live in `tests/errors/` and use the
  parser list with exact expected diagnostics plus an expected recovered AST
  expression. Commented-out entries there are known-failing/WIP.
- CI (`.forgejo/workflows/ci.yml`, Forgejo not GitHub) runs
  `./build.sh test_all_combinations_ci` on Linux/macOS/Windows plus
  Debug+Release on an Android emulator and the iOS simulator, on every push.

## Performance tracking

- `ide bench` (built via the `bench_all` ninja target) parses the
  `parser_file_test_cases` corpus 200 times and prints one line,
  `BENCH parse_all_tests iterations=... files=... min_ns=... median_ns=...`.
  It's implemented by `parser_parse_bench()` in
  `src/buster/compiler/frontend/buster/parser.c`, deliberately **not** gated
  behind `BUSTER_INCLUDE_TESTS` (it's a benchmark, not a test, and must stay
  buildable in Release) and deliberately independent of the windowing/
  rendering path `ide test` also drives, so it runs headless on a plain CI
  runner with no display server.
- **`BUSTER_INSTRUMENT`** (CMake option, mirrors `BUSTER_TIME_TRACE`
  end-to-end — `--instrument`/`--no-instrument` on `generate`) compiles in
  finer-grained bench timing, compiled out entirely by default. With it on,
  `ide bench` additionally prints `BENCH_PHASE tokenize|parse min_ns=...
  median_ns=...` (splitting `tokenize()` from `parser_parse()`) and one
  `BENCH_FILE path=... min_ns=... median_ns=...` line per test file, slowest
  first.
- **`ninja_log_summary <build-dir> [--limit N]`** and **`time_trace_summary
  <json-path>... [--limit N]`** (both new `build/build` commands, same
  shape as `cmake_profile_summary` — see `build.c`) are diagnostics for
  *where compile time goes*: the former reads `<build-dir>/.ninja_log`
  directly (only useful for multi-TU/Debug builds — Release is a single
  unity TU); the latter parses one or more clang `-ftime-trace` JSON files
  (enable via `--time-trace`) and reports the slowest `"Total *"` rollups
  clang itself pre-aggregates (`Total Frontend`, `Total Backend`,
  `Total InstantiateFunction`, ...), summed across every file given. Neither
  is wired into `perf-history` or the regression gate — they're printed to
  the CI log (or run manually) for a human to read when the aggregate
  numbers below say something regressed.
- CI's `Perf` steps (one per platform, see `.forgejo/workflows/ci.yml` and
  `.forgejo/scripts/perf_step.sh`) build **both Debug and Release**
  (sanitize/fuzz off, `--instrument --time-trace` on) in dedicated
  `build/perf-<Config>` directories, run `bench_all`, print the two
  diagnostics above, and hand the numbers to
  `.forgejo/scripts/record_perf.sh`. That script compares the run against
  the same `(runner, config)`'s own rolling history on the orphan
  `perf-history` git branch — one row per metric, not one wide line per run
  (`ts=... runner=... config=... commit=... metric=... value=...
  [file=...]`, plain text, no JSON/`jq`) — and emits a **warning without
  failing the CI job** if
  `compile_milliseconds` or `bench_median_ns_per_file` regresses more than
  15% (`PERF_REGRESSION_THRESHOLD`) past that `(runner, config)`'s median.
  Regressions emit both a `::warning` Actions annotation for CI interfaces
  that support workflow commands and a plain stderr warning as a fallback.
  The raw `bench_median_ns` and `bench_file_count` metrics are also retained,
  but the normalized metric is gated so growing the parser corpus does not
  register as a performance regression. `compile_milliseconds` times CMake
  generation plus the clean `ide` build; `bench_all` runs afterward, outside
  that timer, so growing the corpus cannot inflate the compile metric. The
  `bench_all` invocation is wall-clock timed separately and recorded as
  `bench_run_milliseconds` (not gated — it includes ninja/process overhead),
  and each config prints its own
  `PERF_TOTAL config=... compile_milliseconds=... bench_run_milliseconds=...`
  line to the CI log; Debug and Release totals are reported independently,
  never summed. The
  phase (`bench_tokenize_median_ns`, `bench_parse_median_ns`) and per-file
  (`bench_file_median_ns` + `file=`) rows are recorded for trend/diagnostic
  purposes but never gate the build — 20+ per-file checks per run would make
  the job flaky on any one noisy file. History is only appended/pushed on
  `main`; other branches are compared but don't pollute it. Pushing needs a
  `PERF_HISTORY_TOKEN` repo secret with push access, since the main
  `Checkout` step deliberately uses `persist-credentials: false`.

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
- **Warnings are errors** under a very large warning set (see
  `GNU_FAMILY_WARNINGS` in `CMakeLists.txt`), and code must stay clean under
  clang, gcc, tcc, zig cc, and MSVC. Avoid compiler-specific extensions
  unless guarded.
- **Idioms**: arena allocation (`arena.h`) — no malloc/free churn;
  `String8`/`S8("...")` (defined in `base.h`) — no C strings; `STRUCT(Name)`
  declarations; `BUSTER_`-prefixed macros; 4-space indent, snake_case, braces
  on their own line. Prefer function headers, declarations, statements, and
  similar constructs on one line; split them only when doing so is clearer.
  Match the surrounding file.
- **Adding a module** (`foo.c`/`foo.h` under `src/buster/`) takes three
  edits: (1) `buster_register_module(foo ...)` in `CMakeLists.txt`;
  (2) add `foo` to the `MODULES` list of `buster_add_executable(ide ...)`;
  (3) add `#include <buster/foo.c>` to the `BUSTER_UNITY_BUILD` block at the
  top of `src/buster/ide/ide.c` — optimized non-sanitized configs compile as
  unity builds, so forgetting this breaks ordinary Release builds.
- Headers are included as `<buster/...>` (include root is `src/`).
  `compile_commands.json` is exported to `build/` by default.

## Platform and backend boundaries

- In rendering and windowing, keep platform-neutral policy and data flow in
  the module front door (`rendering.c`, `window.c`). Native API calls
  belong in the selected backend implementation.
- Rendering backends live in `src/buster/rendering/*.c`; window backends
  live in `src/buster/window/*.c`. These are implementation files
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

The buster-language parser (`src/buster/compiler/frontend/buster/parser.c`)
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
  second machine IR. Each backend performs a conservative linear-scan
  allocation of same-block scalar IR values to backend-owned caller-saved
  registers. Values crossing calls or control-flow edges, aggregates, and
  excess live values retain stack slots as spill storage; block parameters are
  resolved with parallel edge copies. Executable tests use writable memory
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
  read-only or writable object sections; static locals use deterministic
  internal global symbols. C aggregate initializer lowering uses an explicit
  task stack, and incomplete character-array bounds are inferred from string
  initializers before layout.
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
| `src/buster/` | All product source (see below). |
| `tests/` | Compiler test corpus: `.bbb` programs + `assembly.S` for the asm frontend. Read at runtime by the test suite. |
| `test/` | Empty placeholder; unused. |
| `android/`, `ios/` | Manifest/plist plus CI scripts to package, install, and run the on-device/simulator test suite. |
| `.forgejo/workflows/ci.yml` | CI pipeline, including the per-platform `Perf` steps. |
| `.forgejo/scripts/` | `perf_step.sh` (clean Debug/Release `ide` builds + separately run `bench_all`), `record_perf.sh` (perf-history compare/append/push — see Performance tracking above). |
| `lsan.supp` | LeakSanitizer suppressions. |
| `build/` | Generated build output (ninja files, per-config dirs, `compile_commands.json`, `build/build`). Never edit. |

`src/buster/` — foundation modules (each `name.c` + `name.h`):

| Path | Contents |
|---|---|
| `base.h` | Root header: platform detection, fixed-width types (`u8`…`s64`), `String8` + `S8("...")`, `STRUCT`/`BUSTER_*` macros, build-mode flags. Header-only. |
| `system_headers.h` | Central OS/libc header includes. `tls.h` is an empty placeholder. |
| `apple_runtime.h` | Objective-C runtime includes and ABI-compatible scalar/geometry types shared by Apple windowing and Metal. |
| `os.{c,h}`, `entry_point.{c,h}` | OS abstraction (processes, virtual memory) and per-platform entry glue (`main`, NativeActivity, UIKit). |
| `arena.{c,h}`, `string.{c,h}`, `integer.{c,h}`, `float.{c,h}`, `hash.{c,h}`, `simd.{c,h}`, `file.{c,h}`, `time.{c,h}` | Arena allocator; `String8` operations and `string_print` (`{S8}` placeholders); numeric parse/format; hashing; SIMD; file IO; clocks. |
| `test.{c,h}` | In-process test harness: `UnitTestArguments`, `BatchTestResult`, `library_tests()`. |

UI / graphics:

| Path | Contents |
|---|---|
| `ui_core.{c,h}`, `ui_builder.{c,h}` | Immediate-mode UI core (widget tree, layout, input) and higher-level construction helpers. |
| `window.{c,h}`, `window/internal.h`, `window/*.c` | Shared window/event front door, private backend contract, and xcb/xkbcommon, AppKit/UIKit, Win32, Android, and null backends. |
| `rendering.{c,h}`, `rendering/internal.h`, `rendering/*.c` | Shared CPU-side renderer front door, private backend contract, and Vulkan, Metal, D3D12, and null backends selected at compile time. |
| `truetype.{c,h}`, `font_provider.{c,h}` | From-scratch TrueType rasterizer; system font discovery (fontconfig on Linux). |
| `shaders/` | `rect.slang` (Slang source, compiled by `slangc`), `rect.vert`/`.frag` (GLSL fallback), and their shared `rect_shared.glsl` declarations. Android uses an SSBO vertex path (`BUSTER_VULKAN_VERTEX_SSBO`, Adreno workaround). |

Compiler:

| Path | Contents |
|---|---|
| `compiler/frontend/buster/parser.{c,h}` | Lexer + state-machine parser; owns `parser_file_tests()` and the `.bbb` test list. `main.c` there is a scratch main, not built. |
| `compiler/frontend/asm/asm_main.c` | Assembly frontend prototype; not wired into the build. |
| `compiler/frontend/c/c.{c,h}` | GNU C frontend in progress: source translation, preprocessing tokens/macros/includes/conditionals, non-recursive external-declaration parsing with strong IDs, flattened scalar/pointer/array/function/aggregate types, nested lexical scopes with entity-based identifier binding and canonical redeclarations, and target-aware shared-IR lowering for scalar/pointer/aggregate parameters, locals, static-storage objects, explicit conversions, array decay/indexing, chained field access, control flow, short-circuit and conditional expressions, direct calls, and constant aggregate initialization. |
| `compiler/frontend/buster/analysis.{c,h}` | Semantic analysis. |
| `compiler/ir/ir.{c,h}` | Typed control-flow IR, semantic lowering, validation, printing, and fixture-wide IR tests. |
| `compiler/object/object.{c,h}` | Format-neutral sections, symbols, and relocations; ELF64, COFF, and Mach-O relocatable writers; in-memory object linking. |
| `compiler/link/link.{c,h}` | Multi-object section merging and symbol resolution; from-scratch libc-backed ELF64, PE32+, and Mach-O executable writers. |
| `compiler/driver/driver.{c,h}` | End-to-end source-to-object compilation and libc-backed executable linking. The Clang-like `ide cc` path supports preprocessing, syntax checks, per-input C object emission for every supported target, and multi-translation-unit native executable construction through the format-neutral object merger for the currently lowered subset. |
| `compiler/codegen/codegen.{c,h}` | Direct-IR ABI classification, native instruction emission, executable-memory support, and interpreter/native differential tests. |
| `compiler/ir/interpreter.{c,h}` | Bounded, explicit-stack runtime IR interpreter and end-to-end execution tests. |
| `target.{c,h}`, `x86_64.{c,h}`, `aarch64.{c,h}` | Target/ABI descriptions and per-arch instruction encoders. |

Applications and standalone tools:

| Path | Contents |
|---|---|
| `ide/ide.c` | Main application, the **only CMake executable target**, and the unity-build translation unit (the `BUSTER_UNITY_BUILD` include block at the top). |
| `scrape_llvm.c`, `scrape_xed.c` | Standalone generators scraping LLVM TableGen / Intel XED data into C encoder tables. Not wired into CMake; compile manually. |
| `disk_builder.c` | Standalone MBR/GPT disk-image builder. Not wired into CMake. |
| `sanitizer_coe_win.c` | Windows continue-on-error sanitizer shim (raw `WriteFile` to stderr, no CRT). |
