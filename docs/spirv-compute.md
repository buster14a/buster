# Direct C to SPIR-V compute

`spirv-vulkan1.2-compute` selects one direct backend for the Vulkan 1.2
execution environment. The active C frontend lowers to validated canonical
IR; the backend writes a SPIR-V 1.5 binary module directly. Compilation does
not invoke Clang, LLVM, DXC, a SPIR-V assembler, or a shader compiler.
The existing [external GPU pipelines](gpu-target-pipelines.md) retain their
separate target spellings and source contracts.

## Kernel interface and source subset

The translation unit contains one function definition with this interface:

```c
void kernel(unsigned *buffer, unsigned index)
{
    buffer[index] = (buffer[index] * 3u + 17u) ^ 0xa5a5a5a5u;
}
```

The required function name `kernel` is also the SPIR-V entry-point name. Parameter names have
no semantic significance. The target's C data model is ILP32: `int`, `long`
and the frontend pointer layout are 32 bits. The buffer parameter is an
interface marker; no host pointer value or native C calling convention is
transferred to the device. The emitted entry function returns void and has
no SPIR-V function parameters.

Only unsigned 32-bit scalar values are admitted. Constants, scalar SSA
temporaries and `+`, `-`, `*`, `&`, `|`, `^` are supported. Addition,
subtraction and multiplication retain their low 32 bits, matching unsigned
C arithmetic modulo 2^32. Representation-preserving copies/casts retain
their scalar or interface provenance. A pointer cannot become an integer,
and an integer cannot become a pointer.

Every buffer access must use the original buffer parameter and the original
invocation index, allowing only representation-preserving copies. Arithmetic
on the index, another buffer element, pointer arithmetic, pointer escape and
arbitrary pointers are rejected. The function must contain at least one
buffer store. With one invocation per x index this prevents accesses to
another invocation's element and removes any cross-invocation sharing from
the admitted contract.

The source body is straight-line. Leave its implicit void return at the end;
an explicit source `return` can introduce an extra canonical block and is
outside the single-block subset. Branches, loops, switches, short-circuit
expressions and other source control flow are rejected. Calls, recursion,
atomics, barriers, floating point, signed arithmetic, division, remainder,
shifts, comparisons, unary operations, volatile accesses, aggregates, global
data, local memory requiring addressable storage and additional function
definitions are outside this slice. Unsupported canonical operations fail
with a backend diagnostic; they never select an external compiler fallback.

The complete repository example is
[`tests/gpu/direct_transform.c`](../tests/gpu/direct_transform.c). Its CPU
oracle is written independently in the registered compute tests rather than
obtained by running the kernel source through another shader compiler.

## Binary and dispatch contract

| Property | Contract |
|---|---|
| Module | SPIR-V 1.5, `Shader` capability, `Logical` addressing, `GLSL450` memory model |
| Entry | `GLCompute`; one entry point; void result and no function parameters |
| Local workgroup size | `LocalSize 1 1 1` |
| Index input | `GlobalInvocationId`, `Input` storage, vector of three unsigned 32-bit integers; x component supplies the C index |
| Buffer | One `StorageBuffer` variable, descriptor set 0, binding 0; Vulkan storage-buffer descriptor |
| Layout | `Block` structure with one runtime array of unsigned 32-bit integers; member offset 0; array stride 4 bytes |
| Entry interface | Lists both the invocation input and the storage-buffer variable, as required for SPIR-V 1.5 |
| Bound | `OpArrayLength` obtains the runtime array length from the descriptor range; invocations with index at or beyond that length skip the source body |
| Control flow | A generated `OpSelectionMerge` and conditional branch guard the source body; the merge block returns |

Canonical `ARGUMENT`, `CONSTANT_INTEGER`, `INDEX`, `LOAD`, `STORE`, `BINARY`,
representation-preserving `CAST` and void `RETURN` already express the
admitted body. Resource layout, entry-point interface, invocation input and
the dispatch bound are target-specific interface machinery. This slice does
not introduce generic canonical address spaces, a frontend IR or a kernel
source language.

An executing host must provide a Vulkan 1.2 device with a compute-capable
queue. The emitted module uses 32-bit integer operations and ordinary
storage-buffer accesses and requests no optional integer-width, variable
pointer, atomic, subgroup or Vulkan-memory-model capabilities. The host binds
a buffer created with storage-buffer usage, a descriptor range containing
whole 4-byte elements and a valid descriptor offset. It must respect the
device's storage-buffer range and offset-alignment limits.

Dispatch y and z counts must both be 1: larger values would run multiple
invocations against each x element. Dispatch x must fit the device's
`maxComputeWorkGroupCount[0]`. Dispatch x equal to the element count covers
every element; a larger valid dispatch demonstrates the generated bounds
guard. Host/device memory visibility, noncoherent mapping flush/invalidate,
queue completion and synchronization remain the executing host's Vulkan
responsibility. This compiler slice supplies no Vulkan execution runner.

## Independent validation

After building `ide` through the repository build driver, a correctness
runner with the existing SPIRV-Tools consumer can run:

```sh
build/Release/ide cc --target=spirv-vulkan1.2-compute -c tests/gpu/direct_transform.c -o build/direct_transform.spv
spirv-val --version
spirv-val --target-env vulkan1.2 build/direct_transform.spv
spirv-dis build/direct_transform.spv -o build/direct_transform.spvasm
```

Only `-c` binary emission is admitted. Without `-o`, the output is the input
path plus `.spv`. Explicit `-g`, native allocation/PIC/linking/verification,
external GPU options, and LLVM output are rejected. Frontend source locations
remain available for diagnostics; this slice emits no device debug information.

The consumer is the existing independently provisioned SPIRV-Tools package
`2026.1-1`; it is development validation tooling, not a compiler dependency.
Record its exact package version, executable hash, version banner and normal
exit status with the emitted module hash and compiler revision. Upstream
source inspection at the revision below establishes the validator contract;
it does not claim the packaged binary is byte-identical to an upstream
build.

Source rejection cases must cover unsupported control flow, calls, floating
point, signed and non-32-bit data, invalid signatures, pointer/index changes,
atomics and barriers. A consumer negative control changes the independently
accepted module's `ArrayStride` from 4 to 1. `spirv-val --target-env vulkan1.2`
must reject this invalid uint32 buffer layout with a normal nonzero exit and
an explanatory diagnostic. A crash, timeout or missing validator is a failed
validation attempt.

At initial documentation preparation, primary-source inspection is complete;
compilation, registered regressions, sanitizer checks, self-hosting and
independent module validation are pending execution. Actual run results and
exact compiler revisions belong on the owning issue/PR. An independent
word-level CPU interpreter/reference comparison can establish the tested
module's integer results on a CPU. It must be identified as CPU execution.
SPIR-V validation and CPU interpretation supply no physical GPU execution
evidence. Device execution and qualified performance acceptance remain
pending until an authorized device environment is available.

## Primary contracts and provenance

These references were inspected for numeric encodings, interface rules and
validation behavior. No external implementation, header or fixture is copied
or vendored.

| Material | Inspected revision and provenance | License |
|---|---|---|
| SPIR-V specification | [Khronos unified specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html), version 1.6 revision 8; its versioned rules for SPIR-V 1.5 apply to emitted modules. Record a SHA-256 of the fetched normative document with validation evidence and reject an unexpected revision. | Khronos specification copyright terms; conditional grant for unmodified reproduction. This specification grant is separate from the headers' license. |
| Numeric instruction grammar | [SPIRV-Headers grammar](https://github.com/KhronosGroup/SPIRV-Headers/blob/3f17b2af6784bfa2c5aa5dbb8e0e74a607dd8b3b/include/spirv/unified1/spirv.core.grammar.json), commit `3f17b2af6784bfa2c5aa5dbb8e0e74a607dd8b3b`, blob `dde0114f5ee56d15011954ad83800b1b4b76653a`; grammar 1.6 revision 4, with the admitted core encodings available in 1.5. | [Khronos MIT-style license](https://github.com/KhronosGroup/SPIRV-Headers/blob/3f17b2af6784bfa2c5aa5dbb8e0e74a607dd8b3b/LICENSE). `tools/buildHeaders/jsoncpp` has a public-domain/MIT exception; that component is unused. |
| Vulkan environment | [Vulkan-Docs SPIR-V appendix](https://github.com/KhronosGroup/Vulkan-Docs/blob/3a4dc41cc86b4215e9995f20f2be744a106887d5/appendices/spirvenv.txt) and [interfaces](https://github.com/KhronosGroup/Vulkan-Docs/blob/3a4dc41cc86b4215e9995f20f2be744a106887d5/chapters/interfaces.txt), tag `v1.2.198`, commit `3a4dc41cc86b4215e9995f20f2be744a106887d5`. | Source documents are `CC-BY-4.0` per their SPDX notices. The [repository license inventory](https://github.com/KhronosGroup/Vulkan-Docs/blob/3a4dc41cc86b4215e9995f20f2be744a106887d5/LICENSE.adoc) separately lists script/XML and font licenses; those components are unused. |
| Validator source | [SPIRV-Tools validator](https://github.com/KhronosGroup/SPIRV-Tools/blob/fbe4f3ad913c44fe8700545f8ffe35d1382b7093/tools/val/val.cpp), tag `v2026.1`, commit `fbe4f3ad913c44fe8700545f8ffe35d1382b7093`; `--target-env vulkan1.2` selects Vulkan validation rather than the default universal environment. | [Apache-2.0](https://github.com/KhronosGroup/SPIRV-Tools/blob/fbe4f3ad913c44fe8700545f8ffe35d1382b7093/LICENSE). External projects listed in upstream `DEPS` have separate licenses; none is introduced into Buster by this slice. |

The specification's structural and shader validation rules, instruction
definitions, and Vulkan's storage-buffer layout and built-in interface rules
jointly define validity. A magic-number check alone is insufficient. The
bounded unsigned scalar path does not imply OpenCL support, graphics stages,
general C GPU compilation, another SPIR-V environment, or other GPU targets.
