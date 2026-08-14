# Wasm64 and Memory64

Buster emits direct core WebAssembly for the target:

```text
wasm64-unknown-freestanding
```

This is a Memory64-only target. It does not emit wasm32 modules and does not
silently narrow pointers. C pointers, Buster pointers, linear-memory
addresses, stack addresses, data relocations, and active data-segment offset
expressions are all 64-bit WebAssembly `i64` values. The memory declaration
sets the Memory64 limits flag.

## Compile

```sh
build/Release/ide cc -target wasm64-unknown-freestanding -nostdinc -O1 -o app.wasm app.c
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

The emitter owns an upward-growing stack above static data in linear memory. The host must not
replace or resize memory incompatibly while a guest call is active.

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

The scalar core rejects constructs it cannot represent correctly, including
variadic and aggregate function ABIs, indirect calls and function tables,
function-pointer data relocations, atomics/threads/TLS/SIMD, inline assembly,
computed labels and indirect branches, and Component Model packaging. These
are explicit errors, not silent native fallbacks.
