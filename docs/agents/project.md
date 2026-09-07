# Project and repository map

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

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
| `.forgejo/` | Forgejo CI workflows/scripts and the source-free GitHub broker workflow template. |
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
| `assembly/` | Standalone x86-64/AArch64 assembly parsing, metadata, semantics, and encoders. `assembly_unit.{c,h}` is the whole-file layer above them: sections, directives, labels, and relocations. Generated metadata stays under `assembly/generated/`. |
| `codegen/machine*.{c,h}` | Machine IR, instruction selection, scheduling, ABI lowering, and target-specific emission. |
| `codegen/register_allocator_*.c` | Fast and quality register allocators. |
| `codegen/codegen.{c,h}` | Canonical-IR-to-native-code orchestration and codegen statistics. |
| `debug/`, `dwarf/`, `codeview/`, `pdb/` | Canonical debug model and platform debug-format emitters. |
| `object/object.{c,h}` | Format-neutral object model plus ELF64, COFF, and Mach-O readers/writers. |
| `link/link.{c,h}` | Section merging, symbol resolution, hosted native executable linking, and imports-free PE32+ UEFI application output. |
| `jit/jit.{c,h}` | Host-native in-process object loader with explicit imports and W^X finalization. |
| `wasm/wasm.{c,h}` | Direct canonical-IR-to-core-Wasm64 emitter using Memory64. |
| `ebpf/ebpf.{c,h}` | Linux eBPF direct backend with its own instruction, ELF, relocation, and BTF encoders. |
| `gpu/gpu.{c,h}` | Target parsing, deterministic command planning/execution, tool discovery, temporary ownership, and artifact validation for external SPIR-V, NVPTX/PTX, AMDGCN/HSA, Metal AIR/metallib, and DXIL pipelines. |
| `llvm/bitcode.{c,h}` | Dependency-free canonical typed-IR to binary LLVM bitcode emitter. It writes the bitstream directly, preserves deterministic value numbering, records target metadata, and diagnoses unsupported IR instead of routing through textual LLVM IR. |
| `driver/driver.{c,h}` | Clang-like C command-line parsing and end-to-end preprocess/compile/assemble/object/link dispatch, including hosted and freestanding UEFI links, plus direct LLVM bitcode output and isolated external GPU-pipeline orchestration. |

Applications:

| Path | Contents |
|---|---|
| `src/buster/apps/ide/ide.c` | Headless `ide` executable: `cc`, `test`, `bench`, fuzzing, and x86-64 completion census. The name is retained for build compatibility. |
| `src/buster/apps/disk_builder.c` | Standalone disk-image builder; not part of the default CMake target. |
