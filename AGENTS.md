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

## Build

Three layers: `./build.sh` bootstraps `build/build` from `build.c` using
**tcc**, which then drives CMake + ninja (multi-config, outputs in
`build/<Config>/`).

```sh
./build.sh                          # configure + build (Debug, clang)
./build.sh build -t test_all        # build and run the full test suite
./build.sh build --config Release -t test_all
./build.sh generate --sanitize && ./build.sh build -t test_all   # sanitized run
./build.sh test_all_combinations    # the full local matrix CI runs
```

`build/build` commands: `generate`, `build` (default), `clang_analyze`,
`cmake_profile_summary`, `test_all_combinations`, `test_all_combinations_ci`.
Flag scope matters: `--sanitize`, `--fuzz`, `--lto`, `--ci`,
`--cc <clang|gcc|tcc|zig|cl>` are accepted **only by `generate`** — passing
them to `build` fails silently with exit 1 and no output. `build` accepts
`--config <name>`, `--optimize`, `--target/-t <ninja target>`,
`--verbose/-v`. Booleans have `--no-` twins; `--` passes the rest through to
ninja (`build`) or CMake (`generate`).

Ninja targets: `ide`, `test_all` (on Android packages/runs the APK, on iOS
drives the simulator), `run_ide`, `test_ide`, `debug_ide`, `buster_shaders`,
`apk` (Android), `clang_analyze`. The Vulkan SDK (`VULKAN_SDK` env) is
required whenever Vulkan or Slang shader compilation is enabled.

## Tests

- All tests are **in-process**: `test_all` runs `ide test --verbose=1`, which
  calls `library_tests()` in `src/buster/test.c` and then runs the IDE
  window/rendering path as a counted app smoke test. No ctest, no external
  framework.
- Run from the **repo root**: parser tests open `tests/*.bbb` by relative
  path.
- To add a language test: drop a `.bbb` file in `tests/` **and** append it to
  the hardcoded `parser_file_test_cases` list in
  `src/buster/compiler/frontend/buster/parser.c` (covered by
  `parser_file_tests()`). Commented-out entries there are known-failing/WIP.
- CI (`.forgejo/workflows/ci.yml`, Forgejo not GitHub) runs
  `./build.sh test_all_combinations_ci` on Linux/macOS/Windows plus
  Debug+Release on an Android emulator and the iOS simulator, on every push.

## Core rules

- **C only.** No C++, no exceptions. `-fwrapv`, `-fno-strict-aliasing`,
  `-funsigned-char`.
- **No third-party code.** External code was deliberately removed from the
  tree. Do not add dependencies or vendor libraries.
- **Warnings are errors** under a very large warning set (see
  `GNU_FAMILY_WARNINGS` in `CMakeLists.txt`), and code must stay clean under
  clang, gcc, tcc, zig cc, and MSVC. Avoid compiler-specific extensions
  unless guarded.
- **Idioms**: arena allocation (`arena.h`) — no malloc/free churn;
  `String8`/`S8("...")` (defined in `base.h`) — no C strings; `STRUCT(Name)`
  declarations; `BUSTER_`-prefixed macros; 4-space indent, snake_case, braces
  on their own line. Match the surrounding file.
- **Adding a module** (`foo.c`/`foo.h` under `src/buster/`) takes three
  edits: (1) `buster_register_module(foo ...)` in `CMakeLists.txt`;
  (2) add `foo` to the `MODULES` list of `buster_add_executable(ide ...)`;
  (3) add `#include <buster/foo.c>` to the `BUSTER_UNITY_BUILD` block at the
  top of `src/buster/ide/ide.c` — optimized configs compile as unity builds,
  so forgetting this breaks Release.
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
  caller-owned result arena. Source/token storage must outlive the result; the
  `ParserState` stack (including pending prefix-unary frames) is scratch,
  borrowed via `scratch_begin`/`scratch_end`, and must not escape.
- Invalid user input produces `ParserDiagnostic` entries and synchronizes at a
  statement or top-level declaration boundary. `BUSTER_TODO()`/assertions are
  reserved for internal invariants, never ordinary syntax errors.
- **Unary operators are AST expression nodes** (for example,
  `AST_NODE_UNARY_MINUS`), not part of numeric literal tokens.
- Expression precedence may use binding-power concepts, but the
  implementation must remain state-machine based.

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
| `.forgejo/workflows/ci.yml` | CI pipeline. |
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
| `compiler/frontend/buster/analysis.{c,h}` | Semantic analysis. |
| `compiler/ir/ir.{c,h}` | Intermediate representation and lowering. |
| `compiler/link/` | `elf.{c,h}`, `jit.{c,h}`, `link.{c,h}` — ELF writer, in-memory execution, linker driver. Not registered as CMake modules yet. |
| `target.{c,h}`, `x86_64.{c,h}`, `aarch64.{c,h}` | Target/ABI descriptions and per-arch instruction encoders. |

Applications and standalone tools:

| Path | Contents |
|---|---|
| `ide/ide.c` | Main application, the **only CMake executable target**, and the unity-build translation unit (the `BUSTER_UNITY_BUILD` include block at the top). |
| `scrape_llvm.c`, `scrape_xed.c` | Standalone generators scraping LLVM TableGen / Intel XED data into C encoder tables. Not wired into CMake; compile manually. |
| `disk_builder.c` | Standalone MBR/GPT disk-image builder. Not wired into CMake. |
| `sanitizer_coe_win.c` | Windows continue-on-error sanitizer shim (raw `WriteFile` to stderr, no CRT). |
