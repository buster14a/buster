# GPU toolchain acceptance

`build/build test_gpu_toolchains` runs pinned source fixtures through the real
`ide cc` GPU driver, then submits its artifacts to external consumers. The
native C harness lives in `tools/gpu_toolchains.c`, included by `build.c`. It
shares the differential runner's bounded process/evidence primitives and the
existing throughput tool's SHA-256 implementation. It adds no compiler dependency.
The fast registered planner/signature tests remain part of `ide test`.

## Profiles and acceptance

Every `--profile` selects a **required** lane. Missing tools, mismatched versions,
compiler/consumer errors, timeouts, missing outputs, changed fixture hashes or
failed evidence writes fail the command. An unknown or duplicate profile fails
argument parsing. Omitted profiles print `SKIP_NOT_CONFIGURED`; an all-skipped
run is availability information and provides no semantic acceptance evidence.

| Profile | Supported tools | Artifact and independent acceptance |
|---|---|---|
| `spirv-dxc-2025.07` | DXC release `v1.8.2505.1`, commit `b106a961`; SPIRV-Tools 2025.1 | HLSL compute SM 6.0 to Vulkan 1.2 SPIR-V; `spirv-val --target-env vulkan1.2 artifact` |
| `ptx-llvm18-cuda12.4` | LLVM 18.1.x; NVIDIA ptxas 12.4.131 | NVVM-annotated LLVM IR to `sm_70` PTX; `ptxas -v -arch=sm_70 artifact -o accepted.cubin`, requiring the kernel name in its assembly report |
| `amdgcn-llvm18` | Clang, LLD, llvm-readobj and llvm-objdump 18.1.x | OpenCL to `gfx900` ET_REL and HSA ET_DYN; read headers, symbols and decoded kernel metadata; disassemble and require `s_endpgm` without unknown instructions |
| `metal-xcode16.4` | macOS, Xcode 16.4, macOS SDK, accessible Metal device | Metal source to AIR; real metallib linker accepts AIR; separately built C Metal-framework consumer loads the metallib, resolves `buster_gpu_smoke`, and creates its compute pipeline |
| `dxil-dxc-2025.07` | DXC and dxv from `v1.8.2505.1` | HLSL compute SM 6.0 to DXIL; `dxv artifact`, plus `dxc -dumpbin artifact` finding `main` |

The DXC release's version banner says `1.9(dev;4950-b106a961)` despite its
`v1.8.2505.1` release tag. The harness checks the revision, not that tag as a
version substring. `dxv` has no version switch: its help and executable hash
are recorded, together with DXC's companion `libdxil` version. Provision both
from the same release. Patch versions within LLVM 18.1.x are deliberately
supported, and the exact banners and executable hashes remain in the evidence.

Each consumer must also reject a malformed artifact with a normal nonzero exit
and diagnostic. The SPIR-V negative has a valid header but no instructions; PTX
has valid header directives and an invalid instruction; AMD ELF retains its
header but is truncated; DXIL/metallib retain their container magic. Metal's
linker also rejects truncated AIR. A crash, timeout, missing executable or
silent nonzero exit never counts as successful rejection. The positive and
negative outcomes are both required.

These are toolchain acceptance smoke tests, not shader execution tests. Linux
profiles require no GPU or driver. Metal creates a pipeline through the public
framework without submitting GPU commands; it does require a Metal device.
AMDGPU reader/disassembly acceptance does not claim HSA runtime loading, and
none of these profiles proves numerical execution or broader language coverage.

## Run locally

Build `ide` according to [the build guide](agents/build.md), then run from the
repository root. Use a fresh output directory for each invocation:

```sh
./build.sh test_gpu_toolchains --self-test
./build.sh test_gpu_toolchains --out build/gpu-observation
./build.sh test_gpu_toolchains --ide build/Release/ide --out build/gpu-linux-1 --profile spirv-dxc-2025.07 --profile ptx-llvm18-cuda12.4 --profile amdgcn-llvm18 --profile dxil-dxc-2025.07
DEVELOPER_DIR=/Applications/Xcode_16.4.app/Contents/Developer ./build.sh test_gpu_toolchains --out build/gpu-metal-1 --profile metal-xcode16.4
```

`--timeout SECONDS` bounds every child (default 60, allowed 1–3600). The runner
claims the output directory atomically and refuses reuse, including an empty
existing directory, so stale artifacts cannot turn a failed tool into a pass.
The self-test exercises rejection classification and SHA-256, and reuses the
observer's subprocess, timeout, argv, and output-ownership regressions. It never
reports a real GPU profile as passed.

All tool overrides designate **one executable path**, not a shell command:

| Environment variable | Default |
|---|---|
| `BUSTER_GPU_CLANG` | `clang-18` |
| `BUSTER_GPU_LLC` | `llc-18` |
| `BUSTER_GPU_TEST_LLD` | `/usr/lib/llvm-18/bin/ld.lld` (its directory is passed through `-B`; basename must be `ld.lld`) |
| `BUSTER_GPU_TEST_READOBJ` | `llvm-readobj-18` |
| `BUSTER_GPU_TEST_OBJDUMP` | `llvm-objdump-18` |
| `BUSTER_DXC` | `dxc` |
| `BUSTER_GPU_TEST_DXV` | `dxv` |
| `BUSTER_GPU_TEST_SPIRV_VAL` | `spirv-val` |
| `BUSTER_GPU_TEST_PTXAS` | `ptxas` |
| `BUSTER_XCRUN` | `xcrun` |
| `BUSTER_GPU_TEST_XCODEBUILD` | `xcodebuild` |

Windows hosts can select SPIR-V, PTX or DXIL with the matching Windows vendor
packages and these overrides; this does not claim a native Windows replay.
The fixture bytes are pinned in the harness and forced to LF by `.gitattributes`.
An intentional fixture edit must update its SHA-256 and be reviewed as a corpus
change. No fixture is generated from compiler output.

## Evidence and CI

The output directory preserves:

- `commands.tsv`: each command prefix, observation kind, exit and native status,
  sanitizer detection and elapsed microseconds;
- numbered `.argv` files with lossless NUL-delimited argv and exact `.stdout` /
  `.stderr` diagnostics, including version probes and negative consumers;
- `hash-*.txt`: SHA-256, size, path and match result for the pinned sources,
  compiler, selected Linux tools, artifacts and negative controls;
- compiler outputs and consumer outputs, plus Git revision/worktree state;
- `summary.txt`: explicit PASS, FAIL or SKIP_NOT_CONFIGURED for every profile.

`.github/workflows/gpu-toolchains.yml` runs all four Linux profiles when the
repository's existing `GH_ACTIONS_CI_ENABLED` switch is enabled. Ubuntu supplies
the LLVM 18.1 and SPIRV-Tools 2025.1 families. DXC's archive is SHA-256 pinned;
NVIDIA's small ptxas wheel is pinned in `tests/gpu/ptxas-requirements.txt` and
installed with `--require-hashes --no-deps`. Python is only a tool provisioner;
the build, execution and acceptance logic are native C. The required profiles
never downgrade failures to skips. Evidence uploads run after failures too.

The separate optional Metal job uses `GPU_METAL_RUNNER`, an administrator-selected
runner label with Xcode 16.4 at the documented path. Use an ephemeral eligible
Mac. Fork PRs never reach that runner. Without the variable, Metal is explicitly
skipped in Linux's profile summary. Do not call that a Metal validation pass.
The new checks supplement existing CI and do not change branch protection.

Consumer contracts: [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools),
[NVIDIA ptxas](https://docs.nvidia.com/cuda/parallel-thread-execution/),
[LLVM AMDGPU ABI](https://releases.llvm.org/18.1.8/docs/AMDGPUUsage.html),
[DXC release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1),
and [Apple's Metal library loader](https://developer.apple.com/documentation/metal/mtldevice/makelibrary(filepath:)).
