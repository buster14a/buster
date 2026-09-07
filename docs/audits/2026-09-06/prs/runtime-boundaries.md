<!-- buster-audit-2026-09-06:pr-runtime-boundaries -->

## Summary

- Exclude the output arena from temporary scratch selection in `executable_resolve_in_path` and Windows `os_path_absolute`.
- Let the POSIX read-only mapping path accept relative names instead of rejecting them before the OS sees them.
- Avoid zero-length `memcpy` calls on null empty String8 values in the arena join and duplicate helpers.

Fixes #144
Fixes #101
Related to #100 — deliberately not closing it; frontend spelling-space helpers are outside this patch.

## Regression coverage

`python3 tests/runtime_boundary_regression.py` checks returned-path lifetime using each of the two scratch arenas, required mapping of a relative file, and null-empty duplicate/mixed join with termination preserved.

Before the fixes the first scratch arena's returned path is overwritten by the next allocation, required relative mapping fails, and UBSan reports a null argument to memcpy in duplication. After the fixes all three scenarios pass under Clang -O0/-O2, Clang -O2 with ASan/UBSan, and GCC -O2. The patch passes independently of both emitter patches.

## Remaining gates

Draft pending full OS/file/string module tests, sanitized `test_all`, and Release self-host on current main. Windows path canonicalization received the analogous source fix but was not executed here; Windows CI and path-canonicalization coverage are required for #144. The focused tests are Linux-host standalone scripts, not yet in normal test registration.

## Audit provenance

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.
