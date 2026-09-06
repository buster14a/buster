<!-- buster-audit-2026-09-06:pr-wasm-alignment -->

## Summary

Clamp scalar load/store memory-argument alignment hints to the access width. An object's stronger alignment remains unchanged; it must not make the instruction's hint invalid.

Fixes {{issue:wasm-alignment}}

## Regression coverage

`python3 tests/wasm_memory_alignment_regression.py` feeds bytes emitted by the actual C helpers into Node/V8 memory64 validation. Eight scalar shapes, seven alignments, and load/store give 112 modules: 44 validate before, all 112 after.

Clang -O0, Clang -O2, Clang -O2 with ASan/UBSan, and GCC -O2 pass. This patch independently applies and passes without the other audit patches.

## Remaining gates

Draft pending full current-tree Wasm tests, `test_all`, Release self-host, and a frontend-to-Wasm over-aligned fixture. The focused test checks validation, not execution of complete C programs. It needs a memory64-capable Node; `AUDIT_NODE_FLAGS` controls experimental flags. Standalone Linux-host regression is not yet in the normal test target.

## Audit provenance

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.
