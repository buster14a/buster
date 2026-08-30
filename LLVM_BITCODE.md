# LLVM bitcode

Buster can emit binary LLVM bitcode directly from its canonical typed IR. The
writer is implemented in C, links no LLVM libraries, and does not generate or
parse textual LLVM IR as an intermediate step.

## Compile

Build the compiler, then pass `-emit-llvm` to the Clang-like driver:

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -emit-llvm source.c
build/Release/ide cc -emit-llvm -o source.bc source.c
```

Without `-o`, each source writes a sibling `.bc` file. A single `-o` cannot
name the output of multiple inputs. Bitcode output is binary, so `-emit-llvm`
cannot be combined with preprocessing (`-E`), textual assembly (`-S`), or
syntax-only (`-fsyntax-only`) actions.

The bitcode path stops before native linking. Native object files, archives,
libraries, frameworks, and linker arguments are rejected rather than being
silently ignored.

## Targets

The module records the target triple and data layout selected by the driver.
The emitter currently provides metadata for x86-64, AArch64, Wasm64, and
eBPF targets across the operating-system combinations supported by `Target`.
Wasm64 bitcode uses 64-bit pointers and is distinct from the direct core
Memory64 module documented in `WASM64.md`.

The bitcode writer serializes canonical IR; it does not run LLVM optimization
passes or invoke an LLVM backend. The eventual LLVM consumer remains
responsible for validating the module against its own version and selecting
the rest of its compilation pipeline.

## Emitter API

`<buster/lib/compiler/llvm/bitcode.h>` exposes three entry points:

- `llvm_bitcode_emit` emits an explicit module range with default options.
- `llvm_bitcode_emit_with_options` also accepts a target triple, data layout,
  source filename, deterministic-output policy, and optional IR validation.
- `llvm_bitcode_emit_program` emits every module in an `IrProgram`.

The returned `LlvmBitcodeArtifact` owns no storage: its three byte-range names
alias the same arena-owned buffer. It includes an error with function, block,
instruction, symbol, and opcode context plus statistics for the emitted
modules, functions, globals, types, constants, blocks, instructions, and
binary size. Call `llvm_bitcode_artifact_is_valid` before consuming the bytes.

## Determinism and diagnostics

Deterministic emission is the default. Types, constants, globals, functions,
basic blocks, and instruction results receive stable IDs before records are
written, so identical canonical IR and options produce identical bitcode.
The unit test emits the same module twice, compares every byte, verifies the
bitcode magic, and checks the reported counts.

The emitter fails explicitly when canonical IR validation fails or when it
encounters an unsupported type, instruction, global initializer, symbol
resolution, duplicate symbol, value-numbering inconsistency, or bitstream
encoding error. It never falls back to native code or emits a partial module
as a successful artifact.

## Current boundary

The implemented lowering covers the canonical scalar, pointer, aggregate,
memory, atomic, call, cast, arithmetic, comparison, branch, switch, return,
and unreachable forms used by the current C frontend. Canonical
operations that do not yet have an LLVM record mapping, including stack
save/restore, instruction-cache clearing, slice/reverse helpers, variadic
intrinsics, inline assembly, SIMD, label addresses, indirect branches, and
debug traps, are deliberate diagnostics.

Source-level debug metadata and LLVM optimization pipelines are outside the
current emitter. Add new mappings only with deterministic byte-level tests and
validation through an LLVM consumer that can parse the generated module.
