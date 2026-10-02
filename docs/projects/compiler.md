# Compiler and toolchain

[Catalogue](../../PROJECTS.md) · Area: `compiler` · [Tracking](../project-tracking.md)

## Purpose and entry points

Translate C source into artifacts and expose reusable compiler/toolchain modules.
The [headless `ide` application](../../src/buster/apps/ide/ide.c) hosts compiler,
test, benchmark and metadata commands. The [source map](../agents/project.md)
locates the frontend, canonical IR, encoders, object/link/JIT and backend modules.
Use the [build guide](../agents/build.md) for the current invocation and self-host
contract; this page does not maintain a second copy of command-line flags.

## Capability entry points

| Stable feature ID | Observable scope | Authoritative detail |
|---|---|---|
| `compiler.c` | Preprocess, parse, analyze and lower the active C frontend. | [Frontend guide](../agents/frontend.md) and its topic guides. |
| `compiler.gnu-callback-storage` | GNU-dialect function-pointer storage through `void *` on native Linux/macOS; strict-C and other target restrictions remain explicit. | [Calls contract](../agents/frontend/calls.md#gnu-callback-storage-through-void-pointers) and [pinned evidence](../capability-portfolios/callback-storage.md). |
| `compiler.artifacts` | Compile, assemble and link through the headless driver; reusable toolchain modules have their own boundaries. | [Driver guide](../agents/driver.md) and [source map](../agents/project.md). |
| `compiler.hot-reload-demo` | Opt-in trusted-module Linux x86-64 counter consumer: edit/rebuild/reload at explicit safe points with host-owned state. | [Runnable workflow and support contract](../../tools/hot_replace_probe/README.md). Application dispatch stays outside compiler internals. |
| `compiler.wasm64` | Direct core Wasm64 output. | [Wasm64 contract](../../WASM64.md). |
| `compiler.llvm-bitcode` | Direct binary LLVM bitcode output. | [Bitcode contract](../../LLVM_BITCODE.md). |

These are navigation entries, not an exhaustive language or target-support matrix.
The target/ABI/mode and limitations in each detailed contract remain decisive.
External GPU tool orchestration is not direct C-to-GPU code generation, and a
produced object is not executed-semantics evidence.

## Validation and work

Use the existing [test/CI guide](../agents/testing.md), focused regression cases
and applicable self-host/target gates. Keep source revision, configuration,
commands, actual results and unavailable execution legs on the relevant PR or
evidence report; this catalogue does not turn historical passes into current ones.

[#309](https://github.com/buster14a/buster/issues/309) owns the specialized backend
capability/conformance catalogue. Extend that work rather than implementing a
second matrix here. Implementation, evidence and advertised support are separate.
A performance candidate also needs its applicable acceptance evidence; SIMD code
or a merged implementation alone is not a speedup.

The removed custom-language/editor implementation remains preservation material
under [DORMANT_CUSTOM_COMPILER.md](../../DORMANT_CUSTOM_COMPILER.md), not a feature
of the current compiler.
