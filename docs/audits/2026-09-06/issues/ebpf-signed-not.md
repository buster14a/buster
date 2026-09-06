<!-- buster-audit-2026-09-06:ebpf-signed-not -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.


## What is wrong

`IR_UNARY_INTEGER_BITWISE_NOT` emits XOR with -1, then calls `ebpf_fe_store_result(..., true, false)`. The last argument forces unsigned normalization even for signed integer result types. The adjacent integer-negation case already uses the result type's signedness correctly.

For a signed 32-bit zero operand the actual emitted sequence leaves `0x00000000ffffffff`, while the backend's normalized signed representation should be `0xffffffffffffffff` (-1). The same mistake affects negative results at widths 8 and 16. The low-width bits are correct, but the high bits are wrong for later 64-bit operations and signed use. The cast emitter normalizes to the destination width, so a wider destination is not a substitute for establishing the source's signed representation.

Start in [ebpf_fe_emit_unary](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/ebpf/ebpf.c#L1449-L1464) and compare its negate case, `ebpf_fe_normalize`, `ebpf_fe_store_result`, and `ebpf_fe_emit_cast`.

## Reproduction

The supplied real-emitter bytecode test checks bitwise-not for 8/16/32/64-bit integers, signed and unsigned, on four inputs per combination. Before the fix 23/32 cases pass and 9 fail. The signed-32 zero input produces 4294967295, not the 64-bit representation of -1.

The witness is at the canonical-IR/emitter boundary. It is not a claim that an end-to-end C translation unit was compiled and executed on a kernel eBPF target during this audit.

## Required fix

Pass `type && type->kind == IR_TYPE_INTEGER && type->is_signed` to result normalization, matching integer negate. Preserve unsigned and 64-bit behavior.

## Validation / definition of done

`python3 tests/ebpf_scalar_regression.py` includes these 32 cases plus comparison and logical-not cases. The combined suite passes 474/474 after the two independent correctness fixes, on Clang -O0/-O2, Clang -O2 with ASan/UBSan, and GCC -O2.

Add integration coverage for storing/reloading the narrow result, signed comparisons, and widening, then run the normal eBPF tests, `test_all`, and Release self-host. Preserve enum/type normalization contracts rather than guessing additional signedness rules.

## Existing work / scope

Separate from #102 and from the comparison-branch issue. Both simple fixes share `01-ebpf-scalar-correctness.patch`; this issue describes only signed result normalization.

The standalone tests compile the repository's actual implementation. They are not mocks of the emitter, but they do not replace full `test_all`, Release self-host, or target integration tests. The audit host could not bootstrap self-host because TCC was missing. Current GitHub source was readable, but a full current checkout and remote publishing were unavailable.
