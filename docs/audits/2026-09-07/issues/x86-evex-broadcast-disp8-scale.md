<!-- buster-audit-2026-09-07:x86-evex-broadcast-disp8-scale -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated emission of a load from the wrong address.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`buster_x86_metadata_emit_tuple_scale` derives the compressed-disp8 scale N from `form->tuple_kind` and the vector length alone. The broadcast state (`query.attributes.broadcast_elements`) is never passed into `buster_x86_metadata_emit_address`, so it cannot be consulted.

When EVEX.b selects a broadcast, N is the **element** size (4 for a Full-tuple 32-bit broadcast, 8 for 64-bit), not the vector byte width. Encoding with N = 64/32/16 divides the intended displacement by the wrong factor.

**Source locations**

- src/buster/lib/compiler/assembly/x86_64_metadata.c:5860 (`buster_x86_metadata_emit_tuple_scale`)
- src/buster/lib/compiler/assembly/x86_64_metadata.c:6021 and :6026 (call sites in `buster_x86_metadata_emit_address`)

## Reproduction and measured evidence

```asm
    .text
    .globl f
f:  vaddps 64(%rax){1to16}, %zmm1, %zmm2
    ret
```

```
clang 18 : 62 f1 74 58 58 50 10    vaddps 0x40(%rax){1to16},%zmm1,%zmm2
ide      : 62 f1 74 58 58 50 01    vaddps 0x4(%rax){1to16},%zmm1,%zmm2
```

The broadcast loads from `rax+4` instead of `rax+64`. Any displacement that is a nonzero multiple of the vector byte width is silently divided by 16, 32 or 64 instead of by the element size. A sweep found 88 distinct wrong-address cases, including `vaddpd 512(%rax){1to8}` -> `0x40`, `vaddps 8128(%rax){1to16}` -> `0x1fc`, `vpaddd -128(%rax){1to16}` -> `-0x8`, and the same for `vfmadd132ps` and for xmm/ymm `{1to4}`/`{1to8}`.

Displacements that are not multiples of the wrong scale fall back to disp32 and encode correctly, which is why the tests pass.

## Required fix

Pass the broadcast flag into `buster_x86_metadata_emit_address` and have `emit_tuple_scale` return the element size when EVEX.b is set, for FULL and HALF tuple kinds.

## Validation / definition of done

Add an encoding differential over broadcast memory operands: each supported broadcast width, displacements at 0, +/- element size, +/- vector width, +/- 127*N and just outside disp8 range, for xmm, ymm and zmm. Compare bytes against clang or gas.

## Existing work / scope

Section S2 of the 2026-09-07 audit. Directly on the AVX-512 target line.
