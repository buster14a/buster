<!-- buster-audit-2026-09-06:wasm-alignment -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.


## What is wrong

`wasm64_fe_load` and `wasm64_fe_store` write `log2(type->layout.alignment)` into the memory instruction without limiting the hint to the instruction's natural access width. A stronger object alignment can therefore make otherwise ordinary scalar bytecode invalid.

For an i32 with size 4 and alignment 16, the backend emits `28 04 00` for load and `36 04 00` for store: alignment exponent 4 where the opcode permits at most 2. Node/V8 rejects the resulting memory64 modules:

```text
invalid alignment; expected maximum alignment is 2, actual alignment is 4
```

Start in [wasm.c](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/wasm/wasm.c#L1483-L1534).

## Reproduction and oracle

`tests/wasm_memory_alignment_regression.c` calls the actual load/store helpers for eight scalar type shapes, seven alignments (1 through 64), and both memory directions. The Python runner embeds their exact bytes in 112 minimal memory64 modules and validates each using `new WebAssembly.Module` in Node v22.16.0, with `--experimental-wasm-memory64`.

Original implementation: 44/112 modules validate; 68 fail. Patched implementation: 112/112 validate. This is a real independent engine validator, not just a handwritten decoder. It validates modules, rather than executing a full C-produced program.

## Required fix

Clamp the alignment hint to the scalar access width (at most 8 bytes for these supported scalar instructions). Preserve weaker/unaligned hints and do not change object allocation or layout alignment. Keep unsupported type handling separate.

## Validation / definition of done

Apply `02-wasm-memory-alignment.patch`, then run `python3 tests/wasm_memory_alignment_regression.py` with a memory64-capable Node. `NODE` and `AUDIT_NODE_FLAGS` can select a different engine executable/flag spelling.

All 112 cases passed with emitted helpers built using Clang -O0/-O2, Clang -O2 with ASan/UBSan, and GCC -O2. Run the standard Wasm suite, `test_all`, and Release self-host. Integrate an over-aligned frontend-to-Wasm witness when a complete current checkout is available.

## Existing work / scope

This is separate from #97 (Wasm shadow-stack reservation/placement). A search for `repo:buster14a/buster wasm alignment` returned no matching issue. None of the nine open PR descriptions covered this path.

The standalone tests compile the repository's actual implementation. They are not mocks of the emitter, but they do not replace full `test_all`, Release self-host, or target integration tests. The audit host could not bootstrap self-host because TCC was missing. Current GitHub source was readable, but a full current checkout and remote publishing were unavailable.
