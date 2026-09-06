<!-- buster-audit-2026-09-06:pr-ebpf-scalar -->

## Summary

Correct the two conditional branch offsets that make comparisons and Boolean-not return false, and normalize integer bitwise-not using the signedness of its result type. Three changed production expressions plus a clarifying branch comment; no allocator, target ABI, or scheduling changes.

Fixes {{issue:ebpf-boolean}}
Fixes {{issue:ebpf-signed-not}}

## Regression coverage

`python3 tests/ebpf_scalar_regression.py` compiles the actual emitter and interprets its generated instruction subset. It covers fourteen comparison operations, Boolean-not, and signed/unsigned bitwise-not at 8/16/32/64 bits: 244/474 pass before, 474/474 after.

Clang -O0, Clang -O2, Clang -O2 with ASan/UBSan, and GCC -O2 all pass. This patch also applies and passes ASan/UBSan independently of the runtime and Wasm patches.

## Remaining gates

Draft pending full current-tree eBPF tests, `test_all`, Release self-host, and an emitted-program integration/independent target check. The host-side VM is not the Linux verifier and no kernel eBPF program was loaded. Tests are standalone Linux-host scripts, not yet registered with the normal test target.

## Audit provenance

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.
