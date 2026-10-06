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
operation and the lifetime of outer allocations.

Canonical module constructor and destructor registrations emit
`llvm.global_ctors` and `llvm.global_dtors` as appending arrays of
`{i32, ptr, ptr}`. Each row retains its priority and callback symbol; the
associated-data pointer is null. The native default-priority sentinel 65536
maps to LLVM's default 65535. LLVM runs constructors in increasing priority
and destructors in decreasing priority; equal-priority order is unspecified.
Selected modules contribute to at most one array of each kind. Empty kinds
add no types, globals or constants, preserving zero-registration output.
The existing canonical validator rejects malformed registration targets and
priorities; duplicate reserved linkage names or record/value count overflow
fail without publishing artifact bytes.

Canonical scalar integer leading-zero count, trailing-zero count, and
population count emit overloaded `llvm.ctlz.iN`, `llvm.cttz.iN`, and
`llvm.ctpop.iN` declarations for widths 1 through 64. The first two pass
`is_zero_poison = true`: canonical zero inputs are undefined, as with the native
builtins; population count of zero remains zero. Wider integer-count operations
produce an explicit diagnostic. Repeated operations reuse declarations, with
stable type, constant, and value IDs. The canonical regression covers each
width from 1 through 64, rejection at 128, and mixed-width declarations in one
module, with independent LLVM compilation at widths 1, 8, 16, 32, and 64. The C
fixture executes 32/64-bit builtins against a separately compiled caller at
LLVM consumer `-O0` and `-O2`, including guarded zero for count-leading/trailing
and unguarded zero for population count.

Canonical operations that do not yet have an LLVM record mapping, including
slice/reverse helpers, inline assembly, SIMD, label addresses, and indirect
branches, are deliberate diagnostics.

Instruction-cache clearing (`__builtin___clear_cache`) lowers to a call of a
synthetic `void @llvm.clear_cache(ptr, ptr)` declaration, added once per module
when used; both operands are the canonical pointer values. The regression fills
a buffer, clears its range and reads it back, with a separately compiled caller
at LLVM consumer `-O0` and `-O2` for both frontend modes.

A canonical debug trap (`__builtin_debugtrap`) lowers to a call of a
synthetic `void @llvm.debugtrap()` declaration, added once per module when any
selected function uses it. Unlike a trap, it is not a terminator: no
`unreachable` follows and execution may continue. The regression places traps
in branches that are not taken and runs the result against a separately
compiled caller at LLVM consumer `-O0` and `-O2` for both frontend modes.

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

Aggregates passed through `...` on x86-64 follow the same classification at the
call site: System V eightbytes ride registers as scalar arguments (with the
register-exhaustion rollback to byval stack storage) and Win64 copies values over
eight bytes behind a pointer. A call with such arguments gets its own call-site
attribute list so byval storage is described on the variadic parameters. Reading
them back with `va_arg` of an aggregate inside an LLVM-emitted definition is not
implemented.

Aggregate function signatures on AArch64, Wasm64, and eBPF, aggregate
variadic arguments on those targets, aggregate
parameters/results without value fields, and System V unions containing
128-bit floating values currently produce an
explicit diagnostic. Scalar signatures and aggregate local storage remain
available on those targets. The emitter does not substitute a raw LLVM record
signature for an unimplemented target ABI.

Source-level debug metadata and LLVM optimization pipelines are outside the
current emitter. Add new mappings only with deterministic byte-level tests and
validation through an LLVM consumer that can parse the generated module.


## Narrow scalar ABI validation

The scalar ABI regressions pin Clang 21.1.8's extension contract, separately
for fixed parameters and returns. `signext` and `zeroext` are ABI attributes:
declarations, definitions and direct or indirect calls need matching attributes.
They preserve narrow LLVM types; the LLVM consumer performs the extension
required by its target ABI.

| Convention | Signed char/short parameter and return | Unsigned char/short parameter and return | Bool parameter and return |
|---|---|---|---|
| System V x86-64 | `signext` | `zeroext` | `zeroext` |
| Win64 x86-64 | None | None | `zeroext` |
| Linux AAPCS64 | None | None | None |
| Darwin AArch64 | `signext` | `zeroext` | `zeroext` |
| Windows AArch64 | None | None | None |

The primary contracts are LLVM's
[ABI attribute documentation](https://github.com/llvm/llvm-project/blob/2078da43e25a4623cab2d0d60decddf709aaea28/llvm/docs/LangRef.rst#L1187),
[Clang x86 classifiers](https://github.com/llvm/llvm-project/blob/2078da43e25a4623cab2d0d60decddf709aaea28/clang/lib/CodeGen/Targets/X86.cpp#L2596)
and [Clang AArch64 classifiers](https://github.com/llvm/llvm-project/blob/2078da43e25a4623cab2d0d60decddf709aaea28/clang/lib/CodeGen/Targets/AArch64.cpp#L364).
The commit is the resolved `llvmorg-21.1.8` tag. Wire enum values are
`ATTR_KIND_S_EXT = 24` and `ATTR_KIND_Z_EXT = 34`.

`llvm_bitcode_test_scalar_abi_wire` reads binary attribute groups and lists,
function types, module symbols and direct/indirect call records. It checks
all five default conventions plus explicit canonical x86-64 System V on
Windows and Win64 on Linux. Ordinary C i32/i64 and aggregate-coerced i8
parameters/results receive no scalar extension. A separate canonical
integer i1 control distinguishes Win64's bool exception from a width rule.
Canonical BOOLEAN neighbors with zero or storage-width bit_width fields retain
LLVM i1 and the same bool policy; that field cannot turn BOOLEAN into INTEGER.
The x86-64 aggregate rows retain typed hidden-result and by-value attributes,
alignment and shifted parameter indices. Fixed narrow parameters of variadic
signatures retain their attributes; anonymous promoted i32 operands gain none.
Repeated emission must remain byte-identical. The signature cache now adds
source-scalar extension groups to return index zero and fixed parameter indices,
including the hidden-result shift. Declarations, definitions and direct/indirect
calls share that attribute list. Enum attributes use their own four-operand wire
record; typed byval/sret and alignment records retain their existing encoding.
Enum scalars use their compatible integer type and effective signedness. Ordinary
Win64 integer i1 is not the BOOLEAN exception. The architecture guard does not
expand the existing explicit AArch64 calling-convention support.

`llvm_bitcode_test_scalar_abi_runtime` executes on native Linux x86-64 and
AArch64. It writes three original, distinct C translation units and requires
all-original GCC and Clang reference executables at O0 and O2 to pass before
mixed tests. Clang O2 independently compiles the counterpart and checker.
Each Buster unit is emitted through both frontend forms, consumed by Clang
at O0 and O2, and linked with those independent objects: eight mixed executable
rows check both producer directions. The inputs cover signed minima, -1,
zero and maxima; unsigned boundaries; bool normalization; direct and indirect
calls; narrow returns; stack parameters; and promoted variadic arguments.
Hidden-result and by-value aggregate execution is restricted to the currently
admitted x86-64 LLVM aggregate ABI.

Original C compilation and Buster emission preserve GNU17, `-g0`,
`-fwrapv`, `-fno-strict-aliasing`, `-funsigned-char` and non-PIE transport.
LLVM consumers select O0/O2 and non-PIE object code; C-only semantics are
already encoded in the bitcode. Every child owns a process group, captures
bounded stdout/stderr, and has a finite build or execution deadline. Launch,
exit, capture, timeout and group-cleanup failures remain failed. A lost or
retained group reservation stops child admission. Source and bitcode receipts
include SHA-256, mapped readback equality and repeated-output equality;
the fixture deletes only its exclusively created directory.

The raw x86-64 calling-convention controls do not claim C source support for
`ms_abi`/`sysv_abi`, or expand explicit AArch64 convention support. Return
coverage also does not establish a previously observed narrow-return defect.
No textual LLVM IR intermediate, LLVM library, new dependency or imported
implementation is used.

## Integer wire validation

`llvm_bitcode_tests` exhausts all 131,070 bit patterns at widths 1 through 16
and checks zero, one, neighboring sign boundaries, all-ones and high-bit
truncation at every width through 64. A private test-only boundary calls the
production integer operand encoder without allocating or emitting a module
per pattern. An independent inverse of LLVM's signed rotation checks the
decoded bits; exact signed-minimum operands for i1/i8/i16/i32/i64 are
`3`, `257`, `65537`, `4294967297`, and `1`. The oracle rejects the historical
narrow-sign operand `1` at every width below 64 and accepts its i64 sentinel.

The existing complete-module serialized-reader checks and separately compiled
Clang consumer fixtures remain complementary: the scalar sweep does not test
record framing or replace consumer execution. Consumer availability and native
platform guards retain their existing policy. Run the registered module with
`build/Release/ide test --module=llvm_bitcode_tests --verbose=1 --ci=1`.

## Stack scope validation

Fixed-size canonical locals, aggregate ABI conversion/result storage, bit-field
aggregate construction storage and variadic-list temporaries allocate once in
the LLVM function entry block, after entry PHIs and before stack saves. Their
loads, stores, zero initialization and calls retain their canonical positions.
Dynamic `STACK_ALLOCATE` records remain at their original block positions;
scoped stack restore therefore releases dynamic storage without invalidating
fixed slots. Stable slot numbering and each instruction's slot consumption are
checked separately from its value-producing records.

The registered `llvm_bitcode_test_fixed_allocas` independently reads original
binary records to check allocation block positions for both frontend forms on
six native target triples. Fixed local/bit-field subjects and x86-64 ABI/list
subjects require every allocation in entry; a loop combining fixed and dynamic
arrays requires exactly one allocation outside entry. Repeated artifacts and
source/output readbacks retain byte equality. Linux x86-64/AArch64 Clang
controls consume the original C and bitcode at `-O0`/`-O2`; the separate observer
checks 65,536 iterations of 256-byte storage and cycling bit-field values.
Linux x86-64 also checks direct and indirect aggregate ABI temporaries,
aggregate definitions, compound bit-field values and copied variadic lists.
Whole-value bit-field structs and unions (copy, return, by-value argument,
`{0}`/brace construction of a union, member reads after a copy) share the
opaque byte-array representation and are cross-checked against Clang in the
`basic_c_llvm_bit_field_aggregates` consumer fixture.
Consumer processes have 30-second deadlines, bounded capture and stop further
admission if process-tree ownership or cleanup fails. Hosted execution is
required to establish results; registration alone is not passing evidence.

`llvm_bitcode_tests` checks two saves, dynamic allocations and void restores
in one canonical function, including a saved token passed through a block
parameter. It compares repeated output byte for byte and rejects a malformed
restore without publishing bytes. The C fixture runs nested and repeated VLAs,
continue, break, outward goto, early return, and a live outer allocation;
the independently compiled observer reads only live elements. The 1024-iteration
16 KiB case exposes an omitted loop restore by exhausting a typical stack.
The test module also checks (using the still-unsupported inline assembly) that a later unsupported operation cannot replace
an existing output. When Clang is available, the fixture is consumed and run
at both `-O0` and `-O2` for both frontend modes.

## Lifecycle registration validation

The registered `llvm_bitcode_test_lifecycle` fixture preserves the C frontend's
canonical `IrModule.initializers` boundary across both frontend forms and six
Linux, Windows and macOS target rows. It checks callback symbols, definition
linkage, priorities and constructor/destructor kinds before asking the direct
serializer for deterministic bytes. Removing registrations must restore the
zero-registration control; changing priority or kind must change the bytes.
Malformed callback symbols and priorities must fail without publishing bytes.
The canonical native default-priority sentinel must encode identically to the
explicit LLVM default priority 65535.

On native Linux x86-64 and AArch64, the fixture requires Clang and GCC. Original
inline C units and a separately compiled observer provide reference output at
`-O0` and `-O2`; Clang also consumes the Buster-produced bitcode in both frontend
forms at those optimization levels. The two-translation-unit case contains
only static callback definitions and an external observer declaration. Unique
priorities require increasing constructor order and decreasing destructor
order, with exactly one default callback of each kind. Constructor-only,
destructor-only, default-only, explicit-65535 and no-registration controls
avoid equal-priority ordering assumptions. The observer returns normally from
`main`, so its captured fixed bytes include destructor output after `main`.
A normal exit with missing callbacks still fails the byte oracle. Compile/run
children have 30-second deadlines, and source and emitted bytes remain checked
against their original inputs after consumption. Other hosts retain the
canonical target checks and report native execution as unsupported.

The fixture covers the lifecycle records tracked by
[#1336](https://github.com/buster14a/buster/issues/1336). The LLVM contract is
[`llvm.global_ctors` and `llvm.global_dtors`](https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable):
appending arrays of priority, function pointer and associated-data pointer.
