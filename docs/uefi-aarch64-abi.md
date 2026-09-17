# AArch64 UEFI ABI contract

Issue [#664](https://github.com/buster14a/buster/issues/664) is resolved by
preserving the existing native ABI, not by importing an unmerged LLVM target
proposal. This document specifies the contract; a patch is not release-qualified
until its applicable CI and firmware gates pass on the final revision.

## Supported native contract and explicit exclusion

`aarch64-unknown-uefi` is a supported **native LP64/AAPCS64** compiler target.
Its firmware execution qualification is the pinned QEMU/AAVMF gate described
in [UEFI target support](uefi-target.md), not a claim about every firmware or
EDK2 compiler flavour. It is not Windows ARM64 and has no implicit LLP64 mode.

| Property | AArch64 UEFI | x86-64 UEFI control |
| --- | --- | --- |
| Pointer size / alignment | 8 / 8 | 8 / 8 |
| `long` size / alignment | 8 / 8 | 4 / 4 |
| `long double` storage / alignment | binary128, 16 / 16 | binary64, 8 / 8 |
| Public `__builtin_va_list` extent / alignment | 32 / 8 | 8 / 8 |
| `wchar_t` | unsigned, 16 bits | unsigned, 16 bits |
| Calling convention | AAPCS64 | Microsoft x64 |
| Object / executable / unwind container | COFF / PE32+ / `.pdata` and `.xdata` | COFF / PE32+ / `.pdata` and `.xdata` |

AArch64 plain `char` is unsigned. The AAPCS64 public list consists of three
pointers (`__stack`, `__gr_top`, `__vr_top`) and two 32-bit offsets
(`__gr_offs`, `__vr_offs`): 32 bytes at alignment 8. This is separate from the
callee's register-save area, whose extent is not `sizeof(va_list)`.

Do not mix this ABI with Windows/LLP64 AArch64 object libraries merely because
both use COFF. Firmware protocol declarations must retain their specified
fixed-width types and calling convention. Binary128 storage assertions do not
qualify every floating-point operation or runtime helper on physical firmware.

**AArch64 UEFI `-emit-llvm` is intentionally unsupported.** The execution API
rejects it before reading source inputs or creating/truncating output files,
including programmatically constructed and multiple-input invocations. Native
assembly, object and EFI-image generation remain available. The other AArch64
OS rows and x86-64 UEFI retain their existing bitcode behavior.

## Why a Windows LLVM triple is not an ABI adapter

The earlier driver published `aarch64-unknown-windows` and a Windows data-layout
string for an LP64/AAPCS64 translation unit. The data-layout string by itself
does not choose C `long`, `long double`, or the public list type. The **triple's
backend calling-convention selection** is the actual conflict.

`tests/basic_c_llvm_uefi_varargs.c` calls a variadic function with `(7, 1.0,
11UL)`. AAPCS64 uses `w0 = 7`, `d0 = 1.0`, and `x1 = 11`. Windows variadic
lowering instead uses `x1` for the double's bits and `x2` for 11. Rejecting
`va_start` intrinsics in the bitcode writer does not reject this scalar external
variadic call. Changing only the public list extent would not repair it.

The driver no longer publishes a Windows triple/layout for AArch64 UEFI.
Re-enabling export requires a supported LLVM representation of the selected
native contract, with independently checked caller/callee lowering, rather
than substituting a different OS triple or weakening the native ABI.

## Reference provenance

* Arm's [AAPCS64](https://github.com/ARM-software/abi-aa/blob/2e9635cff24a62f538788988e63b87fe00dcc478/aapcs64/aapcs64.rst),
  revision `2e9635cff24a62f538788988e63b87fe00dcc478`, specifies the parameter
  registers, stack interface, LP64 mappings and variable-argument list.
* EDK2 [`edk2-stable202402` AArch64 ProcessorBind.h](https://github.com/tianocore/edk2/blob/edk2-stable202402/MdePkg/Include/AArch64/ProcessorBind.h),
  blob `c03594d9240f57f0ce8f626fe3b34c7a55d6e843`, defines 64-bit native integers,
  16-bit `CHAR16`, 16-byte stack alignment and an empty `EFIAPI` for this
  binding. This is a firmware interface reference, not proof that every EDK2
  toolchain configuration shares the same C data model.
* The existing runtime gate pins QEMU `1:8.2.2+ds-0ubuntu1.18` and AAVMF/OVMF
  `2024.02-2ubuntu0.9` at Ubuntu snapshot `20260828T000000Z`, including firmware
  hashes. Its machine parameters and positive/negative acceptance rules remain
  in `tools/uefi_boot.c` and [the target guide](uefi-target.md).
* The independent compile reference used during development was Clang 17.0.0,
  revision `10999b6d034fe318f3d56c83bddb6572593a8bb0`, with
  `--target=aarch64-none-elf -ffreestanding -fshort-wchar -D__UEFI__`.
  This is an AAPCS64 C/layout and assembler reference, **not** a released Clang
  UEFI target or a firmware execution result. The x86-64 control uses
  `--target=x86_64-unknown-windows -ffreestanding -D__UEFI__`.
* LLVM [PR #162950](https://github.com/llvm/llvm-project/pull/162950), head
  `567708cbd08dca0b4203856a80d2d902670ff5fe`, was still open, draft and unmerged
  when checked on 2026-09-16. Its LLP64/Windows proposal is not the authority
  for this existing Buster target.

## Consumer trace and regression ownership

`target_parse_triple` preserves the UEFI OS discriminator. `target_data_layout`
and the `target_uses_llp64_data_model`, `target_uses_16_bit_wchar`,
`target_uses_unsigned_wchar` policies provide the frontend's C layouts and
predefines. No layout policy or shared `target.c` row changes in this repair.
The existing target-parser tests include both UEFI architectures; the new
registered boundary test also checks parsed architecture/OS and LLP64/unwind
policy alongside ordinary compiled-C results.

The canonical IR ABI selection in `compiler/ir/ir.c` keeps AAPCS64 distinct from
Darwin and Windows AArch64 for parameter/result classification. Frontend
variadic operations retain builtin-list identity and lower through VA_START,
VA_ARG, VA_COPY and VA_END rather than treating the type as a pointer macro.
The native consumers continue to use the existing 32-byte AAPCS list; no
32-to-8-byte storage change or backend-dispatch change is made. The compiled
and runtime regressions below exercise the actual producer/consumer boundary.

`compiler_driver_llvm_target_triple`, `compiler_driver_llvm_data_layout` and
`compiler_driver_execute_invocation` own the repaired export boundary. COFF/PE
writing and `target_uses_pe_unwind` remain separate format policies. LLVM's
other deliberate unsupported-IR diagnostics are unchanged.

`llvm_bitcode_test_uefi_boundary` is registered in the existing bitcode test
module, which is already included in `test_all`. It checks Linux, Android,
macOS, iOS, Windows and UEFI AArch64, plus x86-64 UEFI. Ordinary C controls keep
the LP64/LLP64, binary128/binary64, short-wchar and pointer/record-list distinctions.
Both UEFI architectures compile under NONE, MIR_STACK, FAST and QUALITY with
both frontend SSA settings. Negative bitcode tests require no artifact, no new
output, exact diagnostics and preservation of an existing output sentinel for
parsed, direct-API and multiple-input invocations.

`tests/uefi_abi_contract.h` is shared by the existing structural C fixture and
`tests/uefi_boot.c`. It checks size, alignment and record offsets, consumes all
integer argument registers and five stack arguments, copies a partially
consumed list, advances both cursors independently, and checks volatile guards
immediately before and after each public list object. The runtime also checks
unsigned short `wchar_t` behavior.

The AArch64 runtime oracle is not just a Buster-to-Buster call. A hand-written
AAPCS64 leaf producer writes x0-x7 and the five reserved outgoing stack slots,
then tail-calls the C variadic consumer. A separate leaf receiver inspects the
register/stack interface produced by C, with a deliberately incorrect last
argument as a negative control. Both leaves preserve SP/LR and all callee-saved
registers, so neither adds a frame requiring its own unwind description.
These are integer ABI checks, not a floating-point firmware service test.

## Qualification and reproduction

Run the normal `test_all`, mode-matrix, sanitizer and self-host gates on the
final candidate. The existing firmware gate now requires the variadic oracle
before printing its PASS marker:

```sh
./build.sh build --config Release -t test_all
./build.sh test_mode_matrix --config Release
./build.sh test_self_host --config Release
./build.sh test_uefi build/Release/ide build/uefi-abi-664
```

Use a fresh firmware evidence directory. Missing QEMU or pinned firmware is
unavailable execution evidence, not success. Object compilation and EFI linking
are not emulated execution; QEMU/AAVMF execution is not physical-firmware
qualification. Record exact candidate and tool revisions and retain the normal
positive and deliberately failing boot controls. See the PR's final-revision
check results for what actually ran; these commands are not a claim that every
gate ran in the development container.
