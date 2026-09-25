# Wasm64 and Memory64

Buster emits direct core WebAssembly for the target:

```text
wasm64-unknown-freestanding
```

For the wasm32 WASI command target, see [`WASI.md`](WASI.md).

This is a Memory64-only target. It does not emit wasm32 modules and does not
silently narrow pointers. C pointers, Buster pointers, linear-memory
addresses, stack addresses, data relocations, and active data-segment offset
expressions are all 64-bit WebAssembly `i64` values. The memory declaration
sets the Memory64 limits flag.

## Compile

```sh
build/Release/ide cc -target wasm64-unknown-freestanding -nostdinc -O1 -o app.wasm app.c
```

The dormant Buster-language frontend previously supported the corresponding
`.bbb` command below. It is retained as reactivation documentation and is not
accepted by the current C-only driver:

```sh
build/Release/ide cc -target wasm64-unknown-freestanding -O1 -o app.wasm app.bbb
```

`-c source.c` writes `source.wasm`; a link action without `-o` writes
`a.wasm`. Native object files, archives, libraries, frameworks, linker
arguments, textual assembly, and non-freestanding Wasm64 triples are rejected.

The C data model is LP64:

| Property | Size |
|---|---:|
| pointer | 8 bytes |
| `int` | 4 bytes |
| `long` | 8 bytes |
| `long long` | 8 bytes |
| `float` | 4 bytes |
| `double` / `long double` | 8 bytes |
| ABI stack alignment | 16 bytes |

The frontend defines `__wasm__`, `__wasm64__`,
`__wasm_memory64__`, `__LP64__`, and `_LP64`, and defines
`__STDC_HOSTED__` as zero.

## Host contract

The module exports Memory64 linear memory as `memory`. Externally linked
function definitions are exported by link name. Referenced declarations are
imports from `env` by default. A link name of `module#name` selects an explicit
import module and name; this keeps versioned host APIs outside the instruction
backend.

Scalar function pointers use a module-private, fixed-size `funcref` table.
Slot zero stays null; every imported and defined function has one nonzero C
handle equal to its function index plus one, stored in an `i64` pointer slot.
Imports precede definitions in the deterministic emitter order. Table indices
remain `i32`, and indirect calls reject an `i64` handle above `UINT32_MAX`
before narrowing it. The table and its active element segment map those
handles back to function indices; null, missing slots and incompatible Wasm
signatures trap. Neither a handle nor a table index is a Memory64 linear-memory
address. Data pointers continue to use 64-bit memory addresses. Global,
array and struct initializers may relocate function symbols into pointer
slots using the same handle mapping. The table is not exported or grown.

The scalar call ABI uses WebAssembly's `i32`, `i64`, `f32` and `f64` signature
types, including pointer arguments and results as `i64`. Indirect calls need
a prototyped function pointer. Calls with aggregate or variadic signatures,
and nonzero addends on function-address relocations, remain diagnostics. The
host supplies imports with their declared signatures; a wrong host import is
rejected by the engine during instantiation or call.

The emitter owns an upward-growing stack above static data in linear memory. The statically
allocated region is the half-open range from 64 KiB through the static-data end rounded up to a
16-byte boundary. That aligned end is the stack's inclusive lower bound and initial pointer. The
exclusive upper bound is exactly 64 KiB later, and the declared memory minimum contains that
complete half-open stack region without relying on page-rounding slack or extra host memory.

Every fixed frame starts at the caller's pointer, has its object offsets laid out at their canonical
alignments, and rounds its end to the 16-byte ABI stack alignment. Dynamic allocations align their
starting address to the IR-requested power-of-two alignment and then advance by the requested byte
count; a zero-byte allocation is permitted when its aligned starting address remains in bounds.
Function returns restore the entry pointer, and canonical stack-save/restore operations used by VLA
scopes may restore only to an address at or above the fixed-frame end and no later than the stack's
upper bound. This preserves caller frames across nested calls, recursion, repeated exports, and
normal or early returns.

All layout, alignment, page-count, fixed-frame, and dynamic-allocation additions are checked before
they can wrap, overlap static data, or cross the exclusive upper bound. A generated allocation may
end exactly at the upper bound; any larger request deliberately executes WebAssembly `unreachable`.
The backend does not issue `memory.grow` and does not acquire a runtime allocator dependency. The
host must not replace or resize memory incompatibly while a guest call is active.

Runtime support for Memory64 is not yet universal. Select a runtime mode that
enables the proposal and validate the module before instantiation.

## Graphics and windows

`wasi:webgpu` is still a Phase 2 proposal and the faster-moving
`wasi-gfx:surface` and `wasi-gfx:frame-buffer` packages are experimental.
They are useful when the application ships a pinned host, but they are not a
portable contract across arbitrary WASI runtimes. Current component tooling
also commonly assumes wasm32 canonical memories.

For those reasons graphics is not part of the Wasm64 instruction target and
the compiler does not claim a stable wasi-gfx ABI. The optional
`<buster/lib/wasm64_gfx.h>` target header exposes a small, versioned
Buster-owned window/framebuffer import boundary. A controlled host can bridge
those imports to a pinned wasi-gfx runtime (beginning with frame-buffer), a
browser shim, or another native graphics stack. WebGPU and generated WIT
bindings can be added behind that adapter when the selected Component Model
toolchain supports Memory64 end to end.

Keep the following policy for such a bridge:

- Pin exact WIT package and host-runtime revisions.
- Generate Canonical ABI bindings instead of hand-copying WebGPU declarations.
- Keep resource representations behind Buster handles.
- Stress creation, destruction, and error paths on each runtime update.
- Do not describe the experimental bridge as a generally portable WASI ABI.

## Deliberate diagnostics

Local nonempty array, struct and union values can be constructed and copied
through private shadow-stack storage. Aggregate loads snapshot their bytes
immediately; changing the original object cannot change an earlier value.
The emitter uses Memory64 bulk `memory.copy`/`memory.fill`, with 64-bit
addresses and byte counts. Private snapshot alignment is limited to 16 bytes.
Aggregate block parameters, bit-field aggregate construction, atomic aggregate
values and aggregate function ABIs remain explicit unsupported cases. These
local copies do not add aggregate arguments/results or indirect calls.

`tests/basic_c_local_aggregate_copy.c` covers arrays, nested and packed records,
unions and independent source/copy mutations. The driver suite emits it with
both frontend forms and, when Node is available, executes the result using
`tests/wasm_local_aggregate_execution.js`.

The scalar core rejects constructs it cannot represent correctly, including
variadic and aggregate function ABIs, unprototyped indirect calls, function
address addends, atomics/threads/TLS/SIMD, inline assembly,
computed labels and indirect branches, and Component Model packaging. These
are explicit errors, not silent native fallbacks.

The driver test generates isolated C and Node fixtures for callbacks, imports,
function pointer globals, arrays, records and loops in both frontend forms.
It compares repeated module bytes and, when Node is available, validates and
executes the module independently. The Node fixture also checks null,
out-of-range, high-bit and wrong-signature traps as ABI-negative probes; those
inputs are not asserted to be defined C behavior.
