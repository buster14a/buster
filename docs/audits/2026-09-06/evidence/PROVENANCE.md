# Provenance and test boundaries

Repository: buster14a/buster. Audit date: 2026-09-06.

GitHub main observed through the authenticated read connector: `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`.
Recovered local checkout: `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`.
GitHub compare: main ahead by 63 commits, behind by zero; the five production files patched here are unchanged. In addition, individual GitHub file responses at the pinned main commit returned these exact Git blob hashes, matching the recovered checkout:

| File | Git blob SHA |
|---|---|
| src/buster/lib/compiler/ebpf/ebpf.c | 1b384582dc8ce38d171efd5bc765c46c6ce9c15d |
| src/buster/lib/compiler/wasm/wasm.c | 2efa251bbfccf090d3407e82efb19760d87e235f |
| src/buster/lib/os.c | 2af79fa8ddc6cd0ac9148a4d1cd931ec87f14e54 |
| src/buster/lib/file.c | 77da7223b4cf602484a09167b93df3742e30dbfa |
| src/buster/lib/string.c | 2015b26554163e541ab0cf49a4ea04f691027da8 |

The full checkout is not current main. Other source/header changes, including model.h and frontend/native pipeline updates, were not imported. Matching production blobs establishes that the audited defects are present at the pinned main revision; it does not establish successful whole-tree integration on that revision.

Local patch commits (not pushed):
- 041badb9: eBPF scalar correctness (use patch header for exact hash).
- c0e4f988: Wasm memory alignment.
- 2f9f8404: runtime boundaries.

Compiler versions: Clang 17.0.0 (Swift LLVM build), GCC 14.2.0 (Debian 14.2.0-19), Node v22.16.0.

The normal `./build.sh test_self_host --config Release` attempt failed at bootstrap with `tcc: command not found`. No full test_all, self-host fixed point, native execution-mode matrix, Windows runtime run, or kernel eBPF verifier run is claimed.

The tests compile real implementations in standalone unity test translation units with section garbage collection. An unused IR path yields a Clang warning that target_data_layout has internal linkage but is not defined in the focused translation unit. That path is discarded and the focused executables link and execute; these logs are not evidence of a warning-clean full build.

All three final patches were independently applied to separate clean worktrees at the recovered base. Each final regression failed before its patch and passed with ASan/UBSan after that patch alone. The full four-configuration focused matrix was also rerun after final test edits.

Runtime group: three scenarios, with both scratch arenas covered by the first scenario. Baseline null-empty failure is caught at duplication before the later mixed-join assertion; the fixed run exercises both.

The symbols microbenchmark's key-comparison counts are analytically derived, not instrumented. Timings are a single unpinned host run; no Zen 5 or end-to-end speedup claim.

No issue, remote branch, or PR was created. The provided publication helper is a separate opt-in action; its live GitHub path has not been exercised in this environment.
