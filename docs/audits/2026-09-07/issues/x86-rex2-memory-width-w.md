<!-- buster-audit-2026-09-07:x86-rex2-memory-width-w -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated emission of a 32-bit operation where a 64-bit one was written.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

`scalar_memory_width_rex_w` implements the rule that a folded memory operand's width is an authority for REX.W, but gates it on `PREFIX_LEGACY || PREFIX_REX` and omits `PREFIX_REX2`. An r16-r31 base or index is exactly what forces a REX2 form, so on those forms the qword memory width never reaches the W bit and the operation silently narrows to 32 bits.

Two per-iclass workarounds already exist for IMUL `F7 /5` and MOV at `:6708-6716`, which is why only those two encode correctly today.

**Source locations**

- src/buster/lib/compiler/assembly/x86_64_metadata.c:6729-6730 (`scalar_memory_width_rex_w` in `buster_x86_metadata_emit_form_to_scratch`)

## Reproduction and measured evidence

```asm
    .text
    .globl f
f:  addq $1, (%r16)
    ret
```

```
clang 18 : d5 18 83 00 01    addq $0x1,(%r16)
ide      : d5 10 83 00 01    addl $0x1,(%r16)
```

The bytes differ only in the W bit. A 4-byte read-modify-write is emitted where an 8-byte one was written, leaving the upper half of the target stale.

The same applies to `adcq sbbq subq andq orq xorq cmpq testq incq decq negq notq mulq divq idivq shlq shrq sarq rolq rorq rclq rcrq btq btsq btrq btcq` with any EGPR memory operand, to the atomic form `lock addq $1, %fs:(%r16)`, and via Intel syntax (`add qword ptr [r16], 1`). One sweep found 359 affected cases.

## Required fix

Add `|| form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2` to the `scalar_memory_width_rex_w` condition, then remove the two per-iclass workarounds at `:6708-6716`, which become redundant.

## Validation / definition of done

Add an encoding differential over qword memory-operand forms with an EGPR base and with an EGPR index, across the full arithmetic/logical/shift/bit-test set above, in both AT&T and Intel syntax, with and without `lock` and segment overrides.

## Existing work / scope

Section S2 of the 2026-09-07 audit.
