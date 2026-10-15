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
| `compiler.embedding` | In-memory compiler/JIT module retained; packaged SDK/example targets and their automatic workflow are not maintained. | [Embedding example and contract](../../tools/embed_example/README.md) and [public header](../../src/buster/lib/compiler/embed/embed.h). |
| `compiler.hot-reload-demo` | Preserved, unmaintained Linux x86-64 counter example; no default target or automatic lifecycle workflow. | [Runnable workflow and support contract](../../tools/hot_replace_probe/README.md). Application dispatch stays outside compiler internals. |
| `compiler.wasm64` | Direct core Wasm64 output. | [Wasm64 contract](../../WASM64.md). |
| `compiler.llvm-bitcode` | Direct binary LLVM bitcode output. | [Bitcode contract](../../LLVM_BITCODE.md). |
| `compiler.ir-oracle` | Test-only bounded canonical IR interpreter and isolated native comparison. | [Executable-semantics boundary](../canonical-ir-oracle.md). |

These are navigation entries, not an exhaustive language or target-support matrix.
The target/ABI/mode and limitations in each detailed contract remain decisive.
External GPU tool orchestration is not direct C-to-GPU code generation, and a
produced object is not executed-semantics evidence.

## x86 metadata base64 coverage

The registered `x86_64_metadata_tests` module prints an
`X86_METADATA_BASE64` line. `selected` names the compile-time production
decoder exercised by the all-blob comparison. That comparison checks each
generated metadata byte through the selected decoder and scalar decoder; when
the test build supports the target-attributed kernel and runtime CPUID reports
AVX-512F, BW and VBMI, it also decodes every blob directly through VBMI and
checks each byte against the generated reader. Fixed inputs cover partial
groups, invalid high-bit input, output canaries and 63/64/65-character
boundaries.

The `direct_vbmi_test` field reports `passed`, `failed`, `cpu-unsupported` or
`not-built`. The last value means the test-only kernel was not compiled for that
compiler/platform configuration, including MSVC and Zig, Windows, non-x86
targets, and self-host builds; `cpu-unsupported` means the x86 test kernel
compiled but the running CPU lacks one of the required features. In either case
the direct VBMI leg is skipped, while the scalar and production-selected checks
still run when the build target is supported by the host. Production decoder
selection remains compile-time; the runtime check gates only the test-only
direct call.

## JIT runtime admission

The reusable [object JIT](../../src/buster/lib/compiler/jit/jit.h) links
host-native text/data with explicit host bindings. It has no initializer or
finalizer execution protocol, so a nonempty INIT_ARRAY or FINI_ARRAY returns
`JIT_ERROR_INIT_FINI_UNSUPPORTED` before scratch/image allocation. Section size
includes both stored bytes and virtual extent. Zero-size arrays without
relocations remain inert placeholders; relocations in an empty array are
invalid input, while ordinary debug-only relocations remain ignored. An object
that requires an executable stack returns `JIT_ERROR_EXECUTABLE_STACK`, also
before allocation. The JIT is the only in-process object loader; the object
layer's former loader was retired in its favor (#2677).

Registered `jit_tests` check direct and serialized native-format objects with
legitimate function-pointer entries, malformed relocation controls, zero-size
placeholders and normal text/data symbol lookup. The trusted hot-reload
consumer independently refuses constructor/destructor-bearing modules through
its existing module-state policy; its lifecycle self-test covers both cases.
Linking and releasing a JIT mapping do not execute runtime initialization or
finalization.

`ide run <C compiler options and inputs> [-- <program arguments>]` compiles C
through the ordinary frontend, canonical IR, native code generation and
`link_objects`, then links the merged object in memory with this JIT and calls
`main(argc, argv, envp)` in-process, without an intermediate object or
executable file. It binds only an explicit libc-function allowlist, is not a
sandbox, and is executed and tested on x86-64 Linux hosts and targets only. See
the [driver guide](../agents/driver.md#in-memory-execution-ide-run) for its
full contract and refusals.

## Validation and work

The maintained build and automatic CI cover the headless compiler on Linux,
Windows and macOS. Application, GUI, image/font and mobile-hosting source is
preserved without a maintenance or CI-support promise. It is excluded from
the default build, compiler tests/fuzzing and analyzer database.

Compiler target backends (GPU, SPIR-V, eBPF, Wasm, LLVM bitcode and UEFI)
remain in scope. This change removes ancillary maintenance, not compiler targets.


Use the existing [test/CI guide](../agents/testing.md), focused regression cases
and applicable self-host/target gates. Keep source revision, configuration,
commands, actual results and unavailable execution legs on the relevant PR or
evidence report; this catalogue does not turn historical passes into current ones.

The canonical DWARF v4 writer measures the location lists reached through
function scopes before allocating their storage. It preserves unavailable gaps,
empty lists and repeated scope references, and refuses location expressions or
`.debug_loc` offsets that exceed the format's two-byte and four-byte fields. The existing
[DWARF tests](../../src/buster/tests/compiler/dwarf/dwarf_test.c) cover the encoded
model boundaries; external DWARF consumers and debugger observations remain
separate validation gates.

[#309](https://github.com/buster14a/buster/issues/309) owns the specialized backend
capability/conformance catalogue. Extend that work rather than implementing a
second matrix here. See the [backend capability catalogue](../backend-capabilities.md).
Implementation, evidence and advertised support are separate.
A performance candidate also needs its applicable acceptance evidence; SIMD code
or a merged implementation alone is not a speedup.

The removed custom-language/editor implementation remains preservation material
under [DORMANT_CUSTOM_COMPILER.md](../../DORMANT_CUSTOM_COMPILER.md), not a feature
of the current compiler.
