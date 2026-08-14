# GPU target pipelines

The `ide cc` driver supports external GPU compiler pipelines for SPIR-V,
NVIDIA PTX, AMDGCN/HSA code objects, Apple Metal AIR/metallib, and Microsoft
DXIL. The orchestration code lives in
`src/buster/lib/compiler/gpu/gpu.{c,h}` and is shared by unity and non-unity
builds.

This support deliberately does not route ordinary Buster or C source through
the native compiler frontend. The canonical IR does not yet model GPU address
spaces, kernels, resources, execution scopes, barriers, or shader interfaces.
GPU targets therefore consume the source or intermediate language expected by
the corresponding vendor toolchain and preserve that toolchain's semantics.
The buster executable remains dependency-free; the selected external compiler
must be installed only when a GPU pipeline is executed.

## Target and artifact matrix

| Target spelling | Accepted input | Default/link artifact | `-c` artifact | `-S` artifact |
|---|---|---|---|---|
| `spirv`, `spirv32...`, `spirv64...` | OpenCL, LLVM IR/bitcode, `.spv`; generic `spirv` also accepts HLSL | SPIR-V binary (`.spv`) | SPIR-V binary (`.spv`) | SPIR-V assembly (`.spvasm`) |
| `nvptx64-nvidia-cuda` | CUDA, OpenCL, LLVM IR/bitcode | PTX (`.ptx`) | PTX (`.ptx`) | PTX (`.ptx`) |
| `nvptx-nvidia-cuda` | OpenCL, LLVM IR/bitcode | 32-bit PTX (`.ptx`) | 32-bit PTX (`.ptx`) | 32-bit PTX (`.ptx`) |
| `amdgcn-amd-amdhsa` | HIP, OpenCL, LLVM IR/bitcode | HSA loadable code object (`.hsaco`, ELF `ET_DYN`) | relocatable AMDGPU object (`.o`, ELF `ET_REL`) | AMDGCN assembly (`.s`) |
| `air64-apple-*` | Metal source or `.air` | Metal library (`.metallib`) | AIR (`.air`) | unsupported |
| `dxil-pc-shadermodel6.x-*` | HLSL | DXIL container (`.dxil`) | DXIL container (`.dxil`) | DXIL textual listing (`.dxil.txt`) |

`-E` preprocesses source where the selected frontend supports it.
`-fsyntax-only` invokes the frontend without retaining an artifact. Textual
`-E` and `-S` actions without `-o` are captured and returned by the driver,
rather than leaving an implicit output file in the source directory.

## Target spellings

### SPIR-V

The LLVM target spelling is preserved exactly. Supported architectures are
`spirv`, `spirv32`, and `spirv64`, optionally followed by SPIR-V version
`v1.0` through `v1.6`. Accepted full triples include the `unknown` or `amd`
vendor and the `unknown`, `vulkan`, `vulkan1.2`, `vulkan1.3`, or `amdhsa`
runtime, for example:

```text
spirv
spirv32v1.5-unknown-vulkan1.2
spirv64v1.6-unknown-vulkan1.3
spirv64-amd-amdhsa
```

OpenCL and LLVM IR are sent through the LLVM SPIR-V backend using the exact
triple. HLSL is accepted only by the logical `spirv` target and is compiled by
DXC with `-spirv`; Vulkan triples additionally select the matching DXC SPIR-V
target environment. A single `.spv` input is copied without transformation,
multiple SPIR-V modules are linked with `spirv-link`, and `-S` disassembles the
final module with `spirv-dis`.

Fixed-width `spirv32` and `spirv64` targets are compute-only. The logical
`spirv` target may use HLSL graphics, mesh, library, or ray-tracing profiles.
Non-HLSL SPIR-V inputs remain compute-only.

### NVIDIA PTX

`nvptx64`, `nvptx64-nvidia-cuda`, and the `ptx` alias select NVPTX64.
`nvptx` and `nvptx-nvidia-cuda` select explicit 32-bit NVPTX. Select a GPU
with `--gpu-arch=<sm_xx>` or the compatible `-march=<sm_xx>`/`-mcpu=<sm_xx>`
spellings.

CUDA source uses Clang's device-only CUDA path. OpenCL source uses Clang with
an explicit NVPTX target. LLVM IR or bitcode uses `llc`. CUDA source is rejected
for the explicit 32-bit target because Clang's CUDA frontend is routed through
the 64-bit CUDA device ABI; use NVPTX64, or supply OpenCL/LLVM IR for NVPTX32.

### AMDGCN

`amdgcn`, `amdgpu`, and `amdgcn-amd-amdhsa` select the AMD HSA target. A GPU
architecture is mandatory and may be provided as `--gpu-arch=gfx...`,
`-march=gfx...`, or `-mcpu=gfx...`.

HIP source uses Clang's device-only HIP mode. OpenCL source is compiled for
`amdgcn-amd-amdhsa`. LLVM IR or bitcode is lowered with `llc`; a default link
action then links the relocatable object into a loadable HSA code object. The
executor validates the ELF class, `EM_AMDGPU`, and the `ET_REL` versus `ET_DYN`
contract before returning the artifact.

### Metal AIR and metallib

The accepted targets and SDK mappings are:

| Target | `xcrun -sdk` value |
|---|---|
| `air64-apple-macos` or `metal` | `macosx` |
| `air64-apple-ios` | `iphoneos` |
| `air64-apple-iossimulator` | `iphonesimulator` |
| `air64-apple-tvos` | `appletvos` |
| `air64-apple-tvossimulator` | `appletvsimulator` |
| `air64-apple-visionos` | `xros` |
| `air64-apple-visionossimulator` | `xrsimulator` |

`-c` executes `xcrun -sdk <sdk> metal -c` and produces AIR. The default link
action first compiles every `.metal` source to AIR and then executes
`xcrun -sdk <sdk> metallib` over all AIR inputs. Existing `.air` files can be
passed through with `-c` or mixed with `.metal` inputs when building a
metallib. `--metal-sdk=<sdk>` can override the SDK while retaining strict
validation of the supported values.

### DXIL

`dxil` defaults to compute Shader Model 6.6 with entry point `main`.
`dxil-<stage>` is the compact stage spelling. The complete form is:

```text
dxil-pc-shadermodel6.<minor>-<stage>
```

Shader Model 6.0 through 6.10 is accepted. Mesh and amplification profiles
require 6.5 or newer; library and ray-tracing profiles require 6.1 or newer.
Stages accept long names and common DXC abbreviations, including `compute`/`cs`,
`vertex`/`vs`, `pixel`/`fragment`/`ps`, `mesh`/`ms`, `amplification`/`as`,
`library`/`lib`, and the ray-tracing stages.

`--gpu-stage`, `--shader-model`, and `--gpu-entry` override target components.
Library and ray-tracing profiles use DXC's `lib_6_x` profile and do not pass an
entry point. DXC pipelines reject `-nostdinc` and sysroot options instead of
silently ignoring them.

## Driver options

The standard `-I`, `-isystem`, `-D`, `-U`, `-O0` through `-O3`, `-Ofast`,
`-g`/`-g0`, `-E`, `-S`, `-c`, `-fsyntax-only`, and `-o` options are mapped to
the selected frontend where meaningful. GPU-specific options are:

| Option | Meaning |
|---|---|
| `--gpu-arch=<name>` | NVIDIA `sm_...` or AMD `gfx...` processor |
| `--gpu-stage=<stage>` | HLSL shader stage for DXIL or logical SPIR-V |
| `--gpu-entry=<name>` | HLSL entry point |
| `--shader-model=<6.x>` | DXC shader model |
| `--metal-sdk=<sdk>` | Validated Apple SDK name |
| `--cuda-path=<path>` | CUDA toolkit path for CUDA source |
| `--rocm-path=<path>` | ROCm installation path for AMDGCN |
| `-Xgpu=<argument>` / `-Xgpu <argument>` | Append one backend-specific argument |
| `--gpu-arg=<argument>` | Alias for `-Xgpu=<argument>` |
| `--save-temps` / `-save-temps` | Preserve intermediate SPIR-V, AIR, DXIL, or AMDGPU objects |

Native libraries, frameworks, `-Xlinker`, C dialect selection, native register
allocation, CPU feature overrides, Buster module roots, and source metrics are
rejected for GPU targets. This prevents native ABI or linker state from leaking
into a vendor pipeline.

## Tool selection

Explicit command-line overrides have highest priority, followed by environment
variables, followed by the normal executable name on `PATH`:

| Tool | Driver option | Environment variable | Fallback |
|---|---|---|---|
| GPU-capable Clang | `--gpu-clang` | `BUSTER_GPU_CLANG` | `clang` |
| LLVM static compiler | `--gpu-llc` | `BUSTER_GPU_LLC` | `llc` |
| SPIR-V linker | `--spirv-link` | `BUSTER_SPIRV_LINK` | `spirv-link` |
| SPIR-V disassembler | `--spirv-dis` | `BUSTER_SPIRV_DIS` | `spirv-dis` |
| Apple tool launcher | `--xcrun` | `BUSTER_XCRUN` | `xcrun` |
| DirectX Shader Compiler | `--dxc` | `BUSTER_DXC` | `dxc` |

Tool overrides are target-checked. For example, `--dxc` is accepted only for
DXIL or HLSL-to-SPIR-V, and `--xcrun` is accepted only for Metal.

## Examples

All commands are single-line invocations from the repository root after
building `ide`:

```sh
build/Release/ide cc --target=spirv64v1.6-unknown-vulkan1.3 -c kernel.cl -o kernel.spv
build/Release/ide cc --target=spirv-unknown-vulkan1.3 --gpu-stage=compute --shader-model=6.9 --gpu-entry=main shader.hlsl -o shader.spv
build/Release/ide cc --target=nvptx64-nvidia-cuda --gpu-arch=sm_90a --cuda-path=/opt/cuda kernel.cu -o kernel.ptx
build/Release/ide cc --target=amdgcn-amd-amdhsa --gpu-arch=gfx1201 --rocm-path=/opt/rocm kernel.hip -o kernel.hsaco
build/Release/ide cc --target=amdgcn-amd-amdhsa --gpu-arch=gfx1201 -c kernel.hip -o kernel.o
build/Release/ide cc --target=air64-apple-macos -c shader.metal -o shader.air
build/Release/ide cc --target=air64-apple-macos shader.air -o shader.metallib
build/Release/ide cc --target=dxil-pc-shadermodel6.9-compute --gpu-entry=main shader.hlsl -o shader.dxil
```

## Validation and failure behavior

The planner validates target spelling, source compatibility, action support,
required architecture/profile fields, output/input aliasing, and frontend-
specific options before launching a process. Process failures retain the
complete command and captured output in the compiler diagnostic. Successful
binary artifacts are checked for their expected container signature:

- SPIR-V magic number;
- ELF plus AMDGPU machine and object type for AMDGCN;
- LLVM bitcode signature for AIR;
- `MTLB` for metallib;
- `DXBC` container signature for DXIL;
- a textual PTX header for PTX.

Temporary files are removed after success or failure unless `--save-temps` is
present. A missing executable is reported as a tool-not-found GPU driver error,
not as a native compilation failure.
