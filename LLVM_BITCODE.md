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
macOS metadata uses the `macosx` spelling. Target parsing accepts both `macos`
and `macosx`, including deployment versions, so ABI planning can read it back.
The emitter currently provides metadata for x86-64, AArch64, Wasm64, and
eBPF targets across the operating-system combinations supported by `Target`,
except AArch64 UEFI. The driver rejects `-emit-llvm` for that target before
reading source inputs or touching output files: its native LP64/AAPCS64 ABI
must not be exported as the Windows AArch64 variadic convention. Native
AArch64 UEFI compilation remains supported; x86-64 UEFI and the other
AArch64 rows retain their existing bitcode behavior. See
[the UEFI ABI contract](docs/uefi-aarch64-abi.md).
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
and unreachable forms used by the current C frontend. Scoped dynamic stack
allocation maps canonical stack saves and restores to LLVM's `llvm.stacksave`
and `llvm.stackrestore` intrinsics, preserving the block position of each
operation and the lifetime of outer allocations. Canonical operations that do
not yet have an LLVM record mapping, including instruction-cache clearing,
slice/reverse helpers, inline assembly, SIMD, label addresses, indirect branches, and
debug traps, are deliberate diagnostics.

Scalar `va_start`, `va_copy`, `va_end`, and `va_arg` are admitted only for
x86-64 Linux (System V) and x86-64 Windows (Win64) variadic definitions using
the target's public `va_list` layout. `va_arg` accepts promoted 32- or 64-bit
integers, `double`, and pointers. The list operations preserve separate cursor
storage for copies; the writer declares `llvm.va_start`, `llvm.va_copy`, and
`llvm.va_end` as needed and emits LLVM's typed `va_arg` instruction. Calls to
variadic declarations with scalar anonymous arguments remain supported. Win64
32-bit integer reads consume an eight-byte variadic slot before truncation.

| Target of `-emit-llvm` | List operations | `va_arg` types |
|---|---|---|
| x86-64 Linux, System V | Start, copy, end | i32, i64, double, pointer |
| x86-64 Windows, Win64 | Start, copy, end | i32, i64, double, pointer |
| Other targets, or mismatched explicit calling convention | Diagnostic | None |

Use `va_arg(ap, int)` and `va_arg(ap, double)` for arguments promoted from
narrow integer and float expressions. Smaller integer/floating types, 128-bit
integers, wide floats, aggregates and other unsupported reads receive an
explicit diagnostic. Aggregate anonymous call arguments remain a separate
unsupported boundary. The consumer regression compiles the other side of
calls and public-list exchanges with Clang at `-O0` and `-O2` on admitted
native hosts; cross-target object validation does not substitute for execution.

Aggregate storage preserves canonical field offsets, packing, and tail padding.
Global pointer initializers may reference data or function symbols with a
signed byte addend. Relocation-bearing byte initializers use packed LLVM
constant storage with pointer slots and exact byte runs, so pointer tables,
packed records, array elements, and forward/external references retain their
canonical size and offsets. The writer does not claim an addend is in bounds;
the source and canonical IR must supply a valid address for its use. TLS symbol
references, label addresses, unsupported pointer index widths and malformed
or overlapping relocation ranges remain explicit errors. This is LLVM
constant emission, not native object relocation processing.
Mach-O linking may reject unaligned pointer fixups even when the bitcode
preserves their byte offsets; pointer slots in linkable Mach-O data need
target-supported alignment.
Aggregate function parameters and results follow the x86-64 System V or Win64
C calling convention, including indirect calls, register exhaustion, by-value
stack arguments, and hidden result pointers. LLVM parameter attributes describe
these ABI storage requirements.

Aggregate function signatures on AArch64, Wasm64, and eBPF, aggregate
parameters/results without value fields, aggregate variadic arguments, and System V unions containing
128-bit floating values currently produce an
explicit diagnostic. Scalar signatures and aggregate local storage remain
available on those targets. The emitter does not substitute a raw LLVM record
signature for an unimplemented target ABI.

Source-level debug metadata and LLVM optimization pipelines are outside the
current emitter. Add new mappings only with deterministic byte-level tests and
validation through an LLVM consumer that can parse the generated module.

## Stack scope validation

`llvm_bitcode_tests` checks two saves, dynamic allocations and void restores
in one canonical function, including a saved token passed through a block
parameter. It compares repeated output byte for byte and rejects a malformed
restore without publishing bytes. The C fixture runs nested and repeated VLAs,
continue, break, outward goto, early return, and a live outer allocation;
the independently compiled observer reads only live elements. The 1024-iteration
16 KiB case exposes an omitted loop restore by exhausting a typical stack.
The test module also checks that a later unsupported operation cannot replace
an existing output. When Clang is available, the fixture is consumed and run
at both `-O0` and `-O2` for both frontend modes.
