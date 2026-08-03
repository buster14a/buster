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
`test_self_host`, `test_all_combinations`, `test_all_combinations_ci`.
The combination matrix shares one multi-config build tree across Debug and
Release when their configure-time policy matches. Clang's fuzz-enabled Debug
sanitized and Release non-sanitized configurations use dedicated trees;
sanitizer rows are otherwise Clang-only. GCC and Zig cover unsanitized Debug
and Release, while TCC covers Debug. Independent non-sanitized Release unity
compiles run in parallel. CI Release builds use `-O2`; local Release builds
retain the toolchain default. Clang static analysis runs only against
unsanitized Release. GUI/GPU smoke tests run for Debug sanitized and Release
non-sanitized configurations; other combinations run unit tests only.
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
  pairs. They are include-only sources: only `src/buster/tests/test.c`
  includes the test headers and implementations, while `CMakeLists.txt` lists
  them as `HEADER_FILE_ONLY` so IDEs can display them without compiling them as
  independent translation units. Production sources expose only the narrow
  seams needed by tests; `BUSTER_TEST_F_DECL` keeps those seams static in
  production and unity builds and externally linkable in non-unity test builds.
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

## Performance tracking

- **`test_self_host` is the most trustworthy and complete compiler benchmark.**
  It exercises the full self-hosting IDE pipeline, including the trusted
  bootstrap, two complete unity-build IDE compilations, byte-identical stage
  verification, and the stage-2 benchmark. Use its end-to-end result as the
  primary completeness/fixed-point signal for compiler-throughput changes.
  For performance measurements, run the benchmark with the trusted
  Clang-compiled `ide` executable, whose generated host code has the best
  quality; buster-compiled stage executables are useful for self-hosting
  validation, not as the default benchmark host.
- `ide bench` (built via the `bench_all` ninja target) parses the
  `parser_file_test_cases` corpus 200 times and prints one line,
  `BENCH parse_all_tests iterations=... files=... min_ns=... median_ns=...`.
  It's implemented by `parser_parse_bench()` in
  `src/buster/lib/compiler/frontend/buster/parser.c`, deliberately **not** gated
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
  `.forgejo/scripts/perf_step.sh`) build **both Debug and Release** when the
  preceding work succeeds (sanitize/fuzz off, `--instrument --time-trace` on)
  in dedicated `build/perf-<Config>` directories. A failure stops that server
  immediately; matrix servers remain independent. Each completed config runs
  `bench_all`, prints the two diagnostics above, and hands the numbers to
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

## Latest performance audit notes

`2026-08-02` (Linux x86_64, Release + Debug, with `--instrument --time-trace`):

- `build/Release` compile time is currently dominated by the unity TU:
  `CMakeFiles/ide.dir/Release/src/buster/apps/ide/ide.c.o` at `20613 ms`
  in `ninja_log_summary build --limit 20`.
- `build/Debug` split compilation hotspots (same summary):
  `parser.c.o` (`707 ms`), `codegen.c.o` (`582 ms`), `analysis.c.o` (`475 ms`),
  `ir.c.o` (`361 ms`).
- `bench_all` result snapshots:
  - Release: `BENCH parse_all_tests iterations=200 files=59 min_ns=398673 median_ns=418340`.
  - Debug: `BENCH parse_all_tests iterations=200 files=59 min_ns=941358 median_ns=975512`.
- In `BUSTER_INSTRUMENT` mode:
  - Tokenize: `55_363 ns` median (Release), `297_607 ns` median (Debug).
  - Parse: `48_272 ns` median (Release), `340_853 ns` median (Debug).
- `BENCH_FILE` output is consistently slowest-first; `tests/basic_variadic.bbb`
  is the dominant long-tail input across both configs.
- Superluminal capture of `ide bench` shows `parser_parse_bench` paths flowing
  heavily through `file_read -> os_file_open -> __libc_open64`, indicating
  benchmark procedure repeatedly performs disk reads instead of amortizing source
  input.
- Recommended next optimization experiment:
  - Add a preloaded-source benchmark mode to separate parser throughput
    (`tokenize`/`parse`) from filesystem input, then compare both modes via
    `perf_file_median_ns` and `bench_median_ns_per_file`.

`2026-08-02` (Linux x86_64, Release `bench_all`, `BUSTER_INSTRUMENT=1`):

- Added an opt-in mmap benchmark path selected by `BUSTER_BENCH_MMAP=1`:
  - `BENCH parse_all_tests ... median_ns=421736` (baseline: `file_read` each iteration).
  - `BENCH_MMAP parse_all_tests ... median_ns=90831` (files memory-mapped once, then reused).
- Mapped benchmark reduced total wall time by roughly `78.5%` (`421736 -> 90831`).
- Tokenize/parse split moved only modestly:
  - Baseline `BENCH_PHASE tokenize` median: `57241`, parse median: `48711`.
  - Mapped `BENCH_MMAP_PHASE tokenize` median: `50023`, parse median: `39756`.
- The remaining gap between modes is mostly filesystem read overhead in the old
  path; parser/tokenization itself improves only ~12–19%.
- The dominant input is still `tests/basic_variadic.bbb`, but its parse path also
  drops (median `11171 -> 10430`) under mmap reuse.

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
- **Idioms**: arena allocation (`<buster/lib/arena.h>`) — no malloc/free churn;
  `String8`/`S8("...")` (defined in `<buster/lib/base.h>`) — no C strings; `STRUCT(Name)`
  declarations; `BUSTER_`-prefixed macros; 4-space indent, snake_case, braces
  on their own line. Prefer function headers, declarations, statements, and
  similar constructs on one line; split them only when doing so is clearer.
  Match the surrounding file.
- Test implementations are not registered as modules and are not added to the
  unity-build include list; add new test pairs under `src/buster/tests/`, add
  them to the `BUSTER_TEST_FILES` `HEADER_FILE_ONLY` list in `CMakeLists.txt`,
  and include them from `src/buster/tests/test.c` in the existing registration
  order.
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
  second machine IR. Each backend performs a conservative linear-scan
  allocation of same-block scalar IR values to backend-owned caller-saved
  registers. Values crossing calls or control-flow edges, aggregates, and
  excess live values retain stack slots as spill storage; block parameters are
  resolved with parallel edge copies. Codegen publishes format-neutral function
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
| `src/buster/tests/` | Include-only test harness and module test pairs. |
| `tests/` | Runtime compiler fixture corpus: `.bbb` language programs and C frontend/driver fixtures. Test implementations themselves live under `src/buster/tests/`. |
| `android/`, `ios/` | Manifest/plist plus CI scripts to package, install, and run the on-device/simulator test suite. |
| `.forgejo/workflows/ci.yml` | CI pipeline, including the per-platform `Perf` steps. |
| `.forgejo/scripts/` | `perf_step.sh` (clean Debug/Release `ide` builds + separately run `bench_all`), `record_perf.sh` (perf-history compare/append/push — see Performance tracking above). |
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
| `apps/scrape_llvm.c`, `apps/scrape_xed.c` | Standalone generators scraping LLVM TableGen / Intel XED data into C encoder tables. Not wired into CMake; compile manually. |
| `apps/disk_builder.c` | Standalone MBR/GPT disk-image builder. Not wired into CMake. |
| `lib/sanitizer_coe_win.c` | Windows continue-on-error sanitizer shim (raw `WriteFile` to stderr, no CRT). |
