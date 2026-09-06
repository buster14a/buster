<!-- buster-audit-2026-09-06:ebpf-boolean -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.


## What is wrong

`ebpf_fe_emit_comparison` emits `MOV result,0; Jcc +2; JA +1; MOV result,1`. A branch offset is relative to the next instruction; taken `Jcc +2` lands after `MOV result,1`, and the false path's `JA +1` also lands there. Consequently every supported comparison lowered through this helper returns false, including true integer, pointer, and Boolean comparisons.

`IR_UNARY_BOOLEAN_NOT` in `ebpf_fe_emit_unary` repeats the same four-instruction pattern, so `!false` also returns false.

Start in [ebpf.c](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/ebpf/ebpf.c#L1364-L1473). The file's own `ebpf_patch_jump` computes `target - instruction - 1`, independently confirming the offset convention.

## Reproduction and measured evidence

The supplied `tests/ebpf_scalar_regression.c` emits real backend bytecode and executes the relevant instruction subset in a small host-side interpreter. It tests all fourteen comparison operations (including signed boundary values) and both Boolean-not inputs. This does not load any code into the Linux kernel.

Before the fix, comparison cases pass 220/440 and Boolean-not passes 1/2. Every true comparison case fails. Example: integer equality of zero and zero yields zero instead of one. Both false and true arms are exercised; this is not just an opcode-byte assertion.

## Required fix

Change the two affected conditional-branch offsets from `2` to `1`. Keep the following unconditional branch at `1`. Do not blanket-replace all offsets: the separate Boolean-normalization sequence has a different layout and its conditional `+2` is correct.

## Validation / definition of done

Apply the eBPF scalar patchset and run `python3 tests/ebpf_scalar_regression.py`. The combined suite (including the separate signed bitwise-not issue) improves from 244/474 to 474/474. Each fix must preserve signed/unsigned predicates, Boolean values, and distinct input/result registers.

The focused suite passed with Clang at -O0 and -O2, Clang -O2 with ASan/UBSan, and GCC -O2. Run the eBPF module suite, `test_all`, and Release self-host in the full current tree; add an emitted-program integration test or kernel/independent VM check before treating the target as comprehensively validated.

## Existing work / scope

This is separate from #102 (verifier-stack slot reuse), #43 (vector/intrinsic semantics), and the native-machine performance PRs. No matching report was found in the current issue search or open-PR inventory. The small correctness patch is in `01-ebpf-scalar-correctness.patch`.

The standalone tests compile the repository's actual implementation. They are not mocks of the emitter, but they do not replace full `test_all`, Release self-host, or target integration tests. The audit host could not bootstrap self-host because TCC was missing. Current GitHub source was readable, but a full current checkout and remote publishing were unavailable.
