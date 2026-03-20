# Repository Guidelines

## Project Structure & Module Organization
- `src/buster/` holds the core C codebase, including:
  - Base/Core utilities: `base.h`, `memory.c/h`, `arena.c/h`, `string*.c/h`, `path.c/h`, `file.c/h`, `time.c/h`, `integer.c/h`, `assertion.c/h`, `arguments.c/h`, `test.c/h`, `target.c/h`, `entry_point.c/h`, `disk_builder.c`, `system_headers.h`.
  - Build system: `src/buster/build/`
  - Compiler frontends: `src/buster/compiler/frontend/cc/` (C compiler), `src/buster/compiler/frontend/asm/` (assembler)
  - Backend code generation: `src/buster/compiler/backend/`
  - Intermediate representation: `src/buster/compiler/ir/`
  - Linker (ELF, JIT): `src/buster/compiler/link/`
  - OS-specific code: `src/buster/os/`, `os.c/h`, `window.c/h`
  - Architecture-specific: `x86_64.c/h`, `x86_64_instructions.c`, `scrape_xed.c`, `aarch64.c/h`
  - GUI & Rendering: `src/buster/ui_core.c/h`, `src/buster/ui_builder.c/h`, `src/buster/ide/`, `src/buster/rendering.c/h`, `src/buster/font_provider.c/h`, `src/buster/truetype.c/h`, `src/buster/shaders/`, `src/buster/stb_truetype.h`
- `src/martins/` contains third-party/utility code (e.g., `md5.h`).
- `tests/cc/` contains external C test cases; keep these small and focused.
- `preamble.c` bootstraps the build, compiles `build.c` via clang, drives a unity build, and handles toolchain downloading when `BUSTER_CI=1`; `build/` is generated output.

## Build, Test, and Development Commands
- `./build.sh build` builds the project (Linux/macOS). Output goes under `build/<target>/` (e.g., `build/x86_64-linux-unknown/`).
- `./build.sh test` runs unit tests (`builder_tests`) and external tests (via `cc test`).
- `./build.sh test_all` runs the full test sweep (unit + external) across multiple configurations (Fuzz/Optimize/Sanitize/Debug/Unity). This iterates through all combinations.
- `./build.sh debug` builds with debug-friendly settings (currently relies on `preamble` defaults; `build.c` implementation is pending).
- Windows equivalent: `.\build.ps1 build` (same commands/flags).
- Common flags:
  - `--optimize=1` / `--optimize=0` — Enable/disable optimizations (-O2 vs -O0)
  - `--sanitize=1` — Enable ASan/UBSan/bounds sanitizers
  - `--has-debug-information=1` — Include debug symbols (defaults to 1 if not specified)
  - `--unity-build=1` — Single-TU compilation (defaults to value of `--optimize` if not specified)
  - `--fuzz=1` — Enable libFuzzer
  - `--fuzz-duration=<seconds>` — Fuzzing duration (default: 2s local, 10s CI, 360s on main branch CI)
  - `--ci=1` — CI mode; enables toolchain download when configured
  - `--just-preprocessor=1` — Only run the preprocessor (-E)

## Coding Style & Naming Conventions
- Indent with 4 spaces, braces on their own line, and keep line widths readable.
- Naming: `snake_case` for functions/variables, `PascalCase` for types, and `UPPER_SNAKE_CASE` for macros/constants.
- Prefer `let` for variable declarations over explicit type specifiers to avoid type mismatch bugs.
- For globals or function-local globals, use `BUSTER_GLOBAL_LOCAL` instead of `static`.
- Prefer designated initializers for aggregate types.
- When an array is indexed by an enum, prefer designated initializers using enum values as indexes.
- For enum-indexed arrays, include an enum `_COUNT` sentinel.
- For enum-indexed arrays, prefer leaving the array size unspecified (`T array[] = { ... }`) and then `static_assert(BUSTER_ARRAY_LENGTH(array) == SOME_ENUM_COUNT);`.
- Avoid abbreviations in variable/function names; use full words (e.g., `pointer` instead of `ptr`, `record` instead of `rec`).
- Prefer `..._count` suffix over `num_...` prefix for count variables (e.g., `table_count` instead of `num_tables`).
- Prefer adding `.h` and `.c` pairs under `src/buster/` and keep module names consistent with directory names.
- The build supports both unity builds and normal separate-translation-unit builds. Write modules so their `.c` files compile correctly as standalone objects, and also work when intentionally included by a unity-build entry file.
- Do not include `.c` files from header files. `.c` inclusions are only allowed from translation-unit `.c` files that intentionally assemble a build, such as unity-build entry points or standalone implementation units that aggregate generated implementation sources.
- Prefer enum-backed, ID-backed, or index-backed representations over string-based lookup for static compiler data.
- If data is scraped offline and compiled into the compiler, treat it as static and resolve names during scraping whenever practical so the compiler consumes numeric references instead of strings.
- Use string-based runtime lookup only when the input is genuinely dynamic, such as commands, diagnostics, or external text interfaces.
- When a function takes an `Arena*` parameter, it should be the first parameter.
- Avoid early returns; prefer a result variable and a single return at end of function scope.

## Testing Guidelines
- Use the custom test macros in `src/buster/test.h` for unit-style tests:
  - `BUSTER_STRING8_TEST(args, a, b)` — Assert two String8 values are equal
  - `BUSTER_STRING16_TEST(args, a, b)` — Assert two String16 values are equal
  - `BUSTER_OS_STRING_TEST(args, a, b)` — Platform-appropriate string comparison
  - `BUSTER_TEST_ERROR(format, ...)` — Report test error with file/line info
  - Tests receive a `TestArguments*` containing an arena and optional show callback.
- Add external tests as minimal C files in `tests/cc/` (e.g., `tests/cc/basic.c`).
- **Note:** Currently, `src/buster/compiler/frontend/cc/cc_main.c` hardcodes `tests/cc/basic.c` for compilation. Adding new external tests requires updating `cc_main.c` to include them.
- Run tests via `./build.sh test` locally; use `test_all` for broader coverage.
- Test code is conditionally compiled with `BUSTER_INCLUDE_TESTS` define.

## Commit & Pull Request Guidelines
- Commit messages are short, sentence-case summaries without prefixes (e.g., “Improve error API”).
- PRs should include a clear summary, testing notes (commands run), and any platform-specific considerations.
