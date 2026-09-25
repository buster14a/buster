# WASI Preview 1

Buster emits direct core WebAssembly command modules for the canonical target:

```text
wasm32-wasip1
```

The legacy `wasm32-wasi` spelling is accepted as an alias. This target is
separate from `wasm64-unknown-freestanding`: WASI Preview 1 uses wasm32 linear
memory and an ILP32 C data model, while Buster's Wasm64 target continues to use
the Memory64 proposal and 64-bit pointers.

## Compile from the compiler driver

```sh
build/Release/ide cc --target=wasm32-wasip1 -nostdinc -O1 -o app.wasm app.c
```

With `-c`, the default output name is the source basename plus `.wasm`; for a
link-style command without `-o`, it is `a.wasm`.

## Command entry point

A program can define either:

```c
void _start(void);
```

or a conventional `main` with one of these shapes:

```c
int main(void);
int main(int argc, char** argv);
int main(int argc, char** argv, char** envp);
```

A `void` return type is also accepted. When `_start` is defined, Buster exports
it directly and does not add a startup adapter. Otherwise the emitter exports a
synthetic `_start`, obtains command arguments and environment strings through
WASI Preview 1 where required, calls `main`, and terminates through
`proc_exit`.

## Imports and exports

The module exports its linear memory as `memory`. Externally linked function
definitions are exported by link name. A referenced declaration is imported
from `env` by default. A link name of `module#name` selects an explicit import
module and field:

```c
extern unsigned short wasi_fd_write(unsigned int fd, const void* iovs,
                                    unsigned int iovs_len,
                                    unsigned int* nwritten)
    __asm__("wasi_snapshot_preview1#fd_write");
```

Startup-owned imports are deduplicated against compatible user declarations.
A declaration with the same WASI module and field but an incompatible function
signature is rejected rather than encoded as a conflicting duplicate.

## C target model

The target uses little-endian ILP32:

| Property | Size |
|---|---:|
| pointer | 4 bytes |
| `int` | 4 bytes |
| `long` | 4 bytes |
| `long long` | 8 bytes |
| `float` | 4 bytes |
| `double` | 8 bytes |
| `long double` | 16 bytes |
| ABI stack alignment | 16 bytes |

The C frontend defines `__wasm__`, `__wasm32__`, `__wasi__`, `__wasip1__`,
`__ILP32__`, `_ILP32`, and `__wasm_mutable_globals__`; `__STDC_HOSTED__` is
one. `-emit-llvm` is not supported for this direct wasm32 target.

## Sysroots

`--sysroot=<path>` adds these header search locations, in order:

```text
<path>/include/wasm32-wasip1
<path>/include/wasm32-wasi
<path>/include
```

This permits preprocessing against a wasi-sdk/wasi-libc sysroot. The current
direct backend emits final core modules and does not yet consume WebAssembly
object files, archives, libc startup objects, or linker metadata. Therefore a
sysroot currently supplies headers only; libc calls must still have supported
direct imports or program-provided implementations.

## Current backend boundary

The scalar direct emitter deliberately diagnoses unsupported core-ABI cases,
including aggregate and variadic function ABIs, indirect calls/function tables,
function-pointer data relocations, atomics/threads/TLS/SIMD, inline assembly,
computed labels and indirect branches, and Component Model packaging. These
are explicit errors rather than native fallbacks.

The synthetic startup reads Preview 1 arguments and environment into guest
memory and grows memory if they exceed the initial 64 KiB stack reserve. A
failed growth or an address/size overflow exits with status 1. This target
emits final core modules only: it does not host `ide` under WASI or link
wasi-libc objects and archives.
