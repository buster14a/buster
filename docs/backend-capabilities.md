# Backend capabilities

Generated from [backend-capabilities-v1.json](backend-capabilities-v1.json) by `python3 tools/backend_capabilities.py --write` — do not edit by hand; owned by [#309](https://github.com/buster14a/buster/issues/309).

Reviewed revision `2b90c52fcd8aa9d9dc59b23e7c02fdbc852e1aef`.

## Reading this table

`supported` has positive evidence, `partial` has bounded evidence and a note, `unsupported-diagnostic` records an explicit rejection, and `not-applicable` marks a separate boundary. `unknown` is not a pass. Evidence levels: `validate` → Parse/validate; `encode` → Exact encode; `object-link` → Object/link; `oracle` → Independent oracle; `execute` → Executed semantics; `integration` → Self-host/real project.

Evidence marked **requires X** depends on tool X. If X is missing, a `skip` requirement means the evidence is skipped, not passed. A `fail` requirement records a test failure. Requirements: `node` (missing: skip): Node.js with WebAssembly support on PATH; `clang` (missing: skip): Clang on PATH as the independent LLVM bitcode consumer; `gcc-and-clang` (missing: fail): Both GCC and Clang on PATH for the Linux scalar ABI oracle (mandatory on supported Linux); `gpu-toolchains` (missing: fail): Pinned toolchains for the selected profile in .github/workflows/gpu-toolchains.yml; an omitted profile reports SKIP_NOT_CONFIGURED and provides no evidence.

A registered target or emitted bytes are not a support claim. Direct code generation and external GPU orchestration are separate rows.

## Summary

| Backend | Scalar calls | Aggregate call ABI | Variadic calls | Indirect calls | Atomics | SIMD/vectors | TLS | Debug info | Object/link | Execution/independent oracle |
|---|---|---|---|---|---|---|---|---|---|---|
| [x86-64 native](compiler-pipeline.md) | supported (integration) | supported (encode, object-link) | supported (object-link) | supported (object-link) | supported (object-link, execute) | partial (encode, execute) | supported (object-link) | supported (object-link) | supported (object-link, integration) | supported (execute, integration) |
| [AArch64 native](compiler-pipeline.md) | unknown | partial (encode) | supported (object-link) | unknown | unknown | partial (encode) | supported (object-link) | unknown | unknown | unknown |
| [UEFI PE32+ (x86-64, AArch64)](uefi-target.md) | unknown | unknown | unknown | unknown | unknown | unknown | unknown | unknown | supported (object-link) | unknown |
| [Wasm64 Memory64 (wasm64-unknown-freestanding)](../WASM64.md) | supported (encode) | unsupported-diagnostic | unknown | supported (encode, oracle, execute) | unsupported-diagnostic | unsupported-diagnostic | unknown | unknown | unknown | supported (oracle, execute) |
| [Wasm32 WASI Preview 1 (wasm32-wasip1)](../WASI.md) | supported (execute) | unsupported-diagnostic | unknown | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unknown | unknown | unknown | supported (execute) |
| [eBPF (bpfel-unknown-linux)](../src/buster/lib/compiler/ebpf/ebpf.c) | supported (encode) | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unknown | unknown | unknown |
| [LLVM bitcode (-emit-llvm)](../LLVM_BITCODE.md) | supported (encode, oracle, execute) | partial (oracle) | supported (encode) | unknown | unknown | unknown | unknown | unknown | not-applicable | supported (oracle) |
| [Direct Vulkan 1.2 SPIR-V compute](spirv-compute.md) | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unsupported-diagnostic | unknown | unknown | unknown | unknown | partial (encode) |
| [External GPU toolchains (SPIR-V, PTX, AMDGCN, Metal, DXIL)](gpu-target-pipelines.md) | not-applicable | not-applicable | not-applicable | not-applicable | not-applicable | not-applicable | not-applicable | not-applicable | partial (oracle) | not-applicable |

## x86-64 native

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | supported | integration | anchor `S8_INITIALIZER("test_self_host")` in `build.c` | Self-host fixed point builds and reruns the compiler with itself. |
| Aggregate call ABI | supported | encode, object-link | `machine_test_native_aggregate` in `src/buster/tests/compiler/codegen/machine_test.c`<br>`compiler_driver_test_sysv_empty_aggregates` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Variadic calls | supported | object-link | `compiler_driver_test_sysv_indirect_variadic` in `src/buster/tests/compiler/driver/driver_test.c`<br>`compiler_driver_test_sysv_wide_aggregate_va_arg` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Indirect calls | supported | object-link | `compiler_driver_test_sysv_indirect_variadic` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Atomics | supported | object-link, execute | `c_test_atomic_compound_assignment_runtime` in `src/buster/tests/compiler/frontend/c/c_test.c`<br>`compiler_driver_test_atomic_pair_contention` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| SIMD/vectors | partial | encode, execute | `machine_test_wide_vector_boundaries` in `src/buster/tests/compiler/codegen/machine_test.c`<br>`machine_test_win64_vector_execute` in `src/buster/tests/compiler/codegen/machine_test.c` | Vector values and ABI boundaries are covered; this row does not claim every SIMD intrinsic form. |
| TLS | supported | object-link | `compiler_driver_test_native_tls` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Debug info | supported | object-link | `compiler_driver_test_dwarf5_objects` in `src/buster/tests/compiler/driver/driver_test.c`<br>`compiler_driver_test_debug_scalar_local_locations` in `src/buster/tests/compiler/driver/driver_test.c` | DWARF evidence; CodeView/PDB are tracked by their own test modules. |
| Object/link | supported | object-link, integration | `compiler_driver_test_dwarf5_objects` in `src/buster/tests/compiler/driver/driver_test.c`<br>anchor `S8_INITIALIZER("test_self_host")` in `build.c` | — |
| Execution/independent oracle | supported | execute, integration | `c_test_atomic_compound_assignment_runtime` in `src/buster/tests/compiler/frontend/c/c_test.c`<br>anchor `S8_INITIALIZER("test_self_host")` in `build.c` | — |

## AArch64 native

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | unknown | — | — | Not yet audited for this backend. |
| Aggregate call ABI | partial | encode | `machine_test_a64_large_aggregate_copy` in `src/buster/tests/compiler/codegen/machine_test.c` | Large aggregate copy lowering only; AAPCS64 aggregate argument evidence not yet audited. |
| Variadic calls | supported | object-link | `compiler_driver_test_aarch64_platform_variadic` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Indirect calls | unknown | — | — | Not yet audited for this backend. |
| Atomics | unknown | — | — | Not yet audited for this backend. |
| SIMD/vectors | partial | encode | `aarch64_direct_simd_tests` in `src/buster/tests/compiler/assembly/aarch64_direct_simd_test.c` | Assembler-level SIMD encoding; canonical vector codegen evidence not yet audited. |
| TLS | supported | object-link | `compiler_driver_test_aarch64_elf_tlsdesc` in `src/buster/tests/compiler/driver/driver_test.c`<br>`compiler_driver_test_aarch64_elf_tls_initial_exec` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Debug info | unknown | — | — | Not yet audited for this backend. |
| Object/link | unknown | — | — | Not yet audited for this backend. |
| Execution/independent oracle | unknown | — | — | Routine Apple CI runs AArch64; per-feature execution evidence not yet mapped. |

## UEFI PE32+ (x86-64, AArch64)

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| Aggregate call ABI | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| Variadic calls | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| Indirect calls | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| Atomics | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| SIMD/vectors | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| TLS | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| Debug info | unknown | — | — | Shares native code generation; UEFI-specific evidence not yet audited. |
| Object/link | supported | object-link | `link_test_uefi_pe64` in `src/buster/tests/compiler/link/link_test.c` | x86-64 and AArch64 PE32+ images. |
| Execution/independent oracle | unknown | — | — | Firmware execution is a separate gate described in docs/uefi-target.md. |

## Wasm64 Memory64 (wasm64-unknown-freestanding)

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | supported | encode | `compiler_driver_test_wasm_integers` in `src/buster/tests/compiler/driver/driver_test.c` | — |
| Aggregate call ABI | unsupported-diagnostic | — | diagnostic `aggregate or unsupported parameter ABI in WebAssembly function` (source only) | Local aggregate copies are supported; aggregate parameter ABI is not. |
| Variadic calls | unknown | — | — | Not yet audited for this backend. |
| Indirect calls | supported | encode, oracle, execute | `compiler_driver_test_wasm64_function_tables` in `src/buster/tests/compiler/driver/driver_test.c` (requires `node`; missing: skip) | Scalar function-pointer table ABI with typed indirect calls. |
| Atomics | unsupported-diagnostic | — | diagnostic `atomic operations are unsupported by WebAssembly` (source only) | — |
| SIMD/vectors | unsupported-diagnostic | — | diagnostic `exact SIMD intrinsics are unsupported by Wasm64; select an explicit source fallback` (source only) | — |
| TLS | unknown | — | — | Not yet audited for this backend. |
| Debug info | unknown | — | — | Not yet audited for this backend. |
| Object/link | unknown | — | — | Emits a core module directly; no relocatable object/link contract is claimed. |
| Execution/independent oracle | supported | oracle, execute | `compiler_driver_test_wasm64_function_tables` in `src/buster/tests/compiler/driver/driver_test.c` (requires `node`; missing: skip) | Node validates and executes the emitted table/typed-call module. compiler_driver_test_wasm_node_policy covers Node process-lifecycle policy only, not engine execution. |

## Wasm32 WASI Preview 1 (wasm32-wasip1)

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | supported | execute | anchor `3/3 wasm32 WASI Preview 1 command executions passed` in `src/buster/tests/compiler/driver/driver_test.c` (requires `node`; missing: skip) | — |
| Aggregate call ABI | unsupported-diagnostic | — | diagnostic `aggregate or unsupported parameter ABI in WebAssembly function` (source only) | — |
| Variadic calls | unknown | — | — | Not yet audited for this backend. |
| Indirect calls | unsupported-diagnostic | — | diagnostic `indirect calls are unsupported by Wasm32` (source only)<br>diagnostic `runtime function addresses are unsupported by Wasm32` (tested) | — |
| Atomics | unsupported-diagnostic | — | diagnostic `atomic operations are unsupported by WebAssembly` (source only) | — |
| SIMD/vectors | unsupported-diagnostic | — | diagnostic `vector/SIMD unary operation is unsupported by WebAssembly` (source only) | — |
| TLS | unknown | — | — | Not yet audited for this backend. |
| Debug info | unknown | — | — | Not yet audited for this backend. |
| Object/link | unknown | — | — | Emits a WASI command module directly; no relocatable object/link contract is claimed. |
| Execution/independent oracle | supported | execute | anchor `3/3 wasm32 WASI Preview 1 command executions passed` in `src/buster/tests/compiler/driver/driver_test.c` (requires `node`; missing: skip) | — |

## eBPF (bpfel-unknown-linux)

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | supported | encode | `codegen_test_ebpf_local_calls` in `src/buster/tests/compiler/codegen/ebpf_call_test_internal.h`<br>`codegen_test_ebpf_scalars` in `src/buster/tests/compiler/codegen/ebpf_test_internal.h` | — |
| Aggregate call ABI | unsupported-diagnostic | — | diagnostic `aggregate eBPF subprogram arguments are unsupported` (source only) | — |
| Variadic calls | unsupported-diagnostic | — | diagnostic `variadic functions are unsupported by eBPF` (source only) | — |
| Indirect calls | unsupported-diagnostic | — | diagnostic `indirect calls are unsupported by the eBPF backend` (source only) | — |
| Atomics | unsupported-diagnostic | — | diagnostic `atomic IR operations are not yet lowered by the eBPF backend` (source only) | — |
| SIMD/vectors | unsupported-diagnostic | — | diagnostic `exact SIMD intrinsics are unsupported by eBPF; select an explicit source fallback` (source only) | — |
| TLS | unsupported-diagnostic | — | diagnostic `thread-local storage is unsupported by eBPF` (source only) | — |
| Debug info | unknown | — | — | Not yet audited for this backend. |
| Object/link | unknown | — | — | Not yet audited for this backend. |
| Execution/independent oracle | unknown | — | — | Kernel verifier/loader evidence not yet mapped. |

## LLVM bitcode (-emit-llvm)

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | supported | encode, oracle, execute | `llvm_bitcode_test_scalar_abi_wire` in `src/buster/tests/compiler/llvm/bitcode_test.c`<br>`llvm_bitcode_test_scalar_abi_runtime` in `src/buster/tests/compiler/llvm/bitcode_test.c` (requires `gcc-and-clang`; missing: fail) | — |
| Aggregate call ABI | partial | oracle | `llvm_bitcode_test_aggregate_va_arg` in `src/buster/tests/compiler/llvm/bitcode_test.c` (requires `clang`; missing: skip)<br>diagnostic `unsupported LLVM aggregate ABI part` (source only) | Only the documented aggregate ABI parts are lowered. |
| Variadic calls | supported | encode | `llvm_bitcode_test_canonical_variadics` in `src/buster/tests/compiler/llvm/bitcode_test.c` | Canonical va_list operations are built directly in IR and encoded; no independent consumer is mapped for this row. |
| Indirect calls | unknown | — | — | Not yet audited for this backend. |
| Atomics | unknown | — | — | Not yet audited for this backend. |
| SIMD/vectors | unknown | — | — | Not yet audited for this backend. |
| TLS | unknown | — | — | Not yet audited for this backend. |
| Debug info | unknown | — | — | Not yet audited for this backend. |
| Object/link | not-applicable | — | — | Bitcode is consumed by external LLVM tools; Buster does not link it. |
| Execution/independent oracle | supported | oracle | `llvm_bitcode_test_consumers` in `src/buster/tests/compiler/llvm/bitcode_test.c` (requires `clang`; missing: skip) | — |

## Direct Vulkan 1.2 SPIR-V compute

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | unsupported-diagnostic | — | diagnostic `SPIR-V compute rejects this instruction/type or buffer/index provenance; calls, source control flow, arbitrary pointers, locals, atomics, barriers and floating point are unsupported` (source only) | — |
| Aggregate call ABI | unsupported-diagnostic | — | diagnostic `SPIR-V compute rejects this instruction/type or buffer/index provenance; calls, source control flow, arbitrary pointers, locals, atomics, barriers and floating point are unsupported` (source only) | — |
| Variadic calls | unsupported-diagnostic | — | diagnostic `SPIR-V compute rejects this instruction/type or buffer/index provenance; calls, source control flow, arbitrary pointers, locals, atomics, barriers and floating point are unsupported` (source only) | — |
| Indirect calls | unsupported-diagnostic | — | diagnostic `SPIR-V compute rejects this instruction/type or buffer/index provenance; calls, source control flow, arbitrary pointers, locals, atomics, barriers and floating point are unsupported` (source only) | — |
| Atomics | unsupported-diagnostic | — | diagnostic `SPIR-V compute rejects this instruction/type or buffer/index provenance; calls, source control flow, arbitrary pointers, locals, atomics, barriers and floating point are unsupported` (source only) | — |
| SIMD/vectors | unknown | — | — | Not yet audited for this backend. |
| TLS | unknown | — | — | Not yet audited for this backend. |
| Debug info | unknown | — | — | Not yet audited for this backend. |
| Object/link | unknown | — | — | Not yet audited for this backend. |
| Execution/independent oracle | partial | encode | `spirv_test_transform` in `src/buster/tests/compiler/spirv/spirv_test.c` | Independent spirv-val validation and device execution are not mapped to a registered test here. |

## External GPU toolchains (SPIR-V, PTX, AMDGCN, Metal, DXIL)

| Feature | Status | Levels | Evidence | Note |
|---|---|---|---|---|
| Scalar calls | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| Aggregate call ABI | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| Variadic calls | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| Indirect calls | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| Atomics | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| SIMD/vectors | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| TLS | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| Debug info | not-applicable | — | — | Source lowering belongs to the external GPU toolchain; Buster orchestrates it. |
| Object/link | partial | oracle | anchor `spirv-dxc-2025.07` in `tools/gpu_toolchains.c` (requires `gpu-toolchains`; missing: fail) | Single-module SPIR-V passthrough is byte-identical and revalidated; no multi-module linking. |
| Execution/independent oracle | not-applicable | — | — | Toolchain acceptance profiles are smoke tests, not shader execution. |

## Updating this catalogue

Edit `docs/backend-capabilities-v1.json`, run `python3 tools/backend_capabilities.py --write`, and commit both files. CI runs `python3 -B tools/backend_capabilities_test.py` and `--check` to verify the catalogue and detect drift.
