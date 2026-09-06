# Buster codebase audit — 6 September 2026

## Outcome and publication status

**Four new defects were identified, three of them have small correctness fixes, and three existing runtime issues received fixes or partial fixes. Three independent patchsets and their regression tests are included. No issue, remote branch, or pull request was published.**

The GitHub connector provided authenticated repository/issue/PR reads but no write actions. The environment had neither an authenticated GitHub CLI nor working container DNS for cloning/pushing. An installed-plugin check did not expose another write route. The included `publish.py` can perform the requested writes from a suitably configured Linux checkout, but only with explicit `--publish`; its live GitHub path was not exercised here.

| Deliverable | Result |
|---|---|
| New issue drafts | 4, each with source location, reproducer, limits, and acceptance criteria |
| Small fixes for new findings | eBPF Boolean branches, eBPF signed bitwise-not, Wasm memory alignment |
| Existing issues addressed | #144, #101, and the arena-string-helper portion of #100 |
| Independent PR-ready patchsets | 3; proposed as drafts pending integration/platform gates |
| Focused validation | 474 eBPF cases, 112 Wasm validation cases, 3 runtime scenarios |
| Full test_all / self-host | Not completed; self-host bootstrap fails because TCC is missing |
| Remote writes | **0 issues, 0 branches, 0 PRs** |

## 1. Revision, method, and actual coverage

The current-main snapshot read through GitHub was [`ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`](https://github.com/buster14a/buster/commit/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b), “Stabilize execution matrix on hosted CPUs.” Network restrictions prevented obtaining a full checkout at that revision.

A previously uploaded archive supplied a clean checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. The GitHub comparison reported current main 63 commits ahead, with none of the five production files patched here changed. Each of those files was additionally authenticated by its Git blob SHA returned by an individual GitHub file read at the pinned main revision. The matching hashes are recorded in [PROVENANCE.md](evidence/PROVENANCE.md) and `manifest.json`.

This distinction matters: **the audited production implementations match current main; the complete local test checkout does not.** Some headers and other pipeline files changed between these revisions. The result is strong evidence for these defects in the pinned implementation, not proof that a full current-main compiler build or all integrations pass.

The audit first triaged the current open-issue inventory and nine open PR descriptions. That covered existing frontend/preprocessor, IR/MIR validation, native selection/allocation/scheduling/encoding, runtime/object handling, and test/CI reports. Detailed new source inspection and executable probes concentrated on eBPF emission, Wasm emission, and runtime ownership/file/string boundaries. This is a substantial targeted audit, not a claim that every function or every open PR diff was exhaustively reviewed.

The new reports were checked against the current issue search, including all-state eBPF searches and a Wasm-alignment search. Existing findings were not re-filed under new names. Nothing was labelled a compiler-throughput improvement merely because a local code change looked smaller.

## 2. New findings

Priority here is audit triage, not a formal project severity label: P1 means a directly demonstrated correctness failure; P2 means demonstrated scaling trouble that still needs workload-level prioritization.

### N1 — P1: eBPF comparisons and Boolean-not skip their true result

**Location:** `src/buster/lib/compiler/ebpf/ebpf.c`, `ebpf_fe_emit_comparison` and the Boolean-not arm of `ebpf_fe_emit_unary`.

The comparison helper emits this structure:

```text
0: result = 0
1: if comparison is true, jump +2
2: unconditional jump +1
3: result = 1
4: subsequent code
```

Offsets are relative to the next instruction. A taken conditional branch at instruction 1 therefore reaches instruction 4, not instruction 3. The false branch also reaches instruction 4. `result = 1` is unreachable, so every comparison handled by this helper returns false. Boolean-not uses the same faulty layout, making `!false` return false as well. The current source is visible [here](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/ebpf/ebpf.c#L1364-L1473).

The probe calls the actual backend emitter, then executes its output in a small host interpreter for the emitted instruction subset. It covers fourteen comparison operations, including signed/unsigned boundary values, pointer equality/inequality, and Boolean equality/inequality. **220/440 comparison cases passed before the fix; all 220 true cases failed. Boolean-not passed only 1/2.**

**Fix:** change the two affected conditional offsets from `2` to `1`; keep their unconditional offsets unchanged. The unrelated Boolean-normalization sequence has a different layout, so its `+2` must not be changed mechanically.

This is not a kernel-verifier or end-to-end C-program execution result. It is a direct semantic test of emitted instructions, reinforced by the backend's own relative-jump patching convention.

Ready issue: [ebpf-boolean.md](issues/ebpf-boolean.md). Fix: patchset 01.

### N2 — P1: eBPF signed bitwise-not loses sign extension

**Location:** `ebpf_fe_emit_unary`, `IR_UNARY_INTEGER_BITWISE_NOT`.

After XOR with -1, the backend explicitly requests unsigned normalization when storing the result. For a signed 32-bit result, that leaves `0x00000000ffffffff` rather than `0xffffffffffffffff`. The lower 32 bits look correct, but the backend's 64-bit signed representation is wrong. Signed uses and widening can consequently see a positive value instead of -1. The adjacent integer-negation arm already requests normalization using the result type's signedness; the bitwise-not arm does not. See the [original unary cases](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/ebpf/ebpf.c#L1449-L1473).

The actual emitted sequence was tested at 8, 16, 32, and 64 bits, signed and unsigned, on four inputs per combination. **23/32 cases passed before the fix; 9 failed.** For signed 32-bit zero input, the observed result was 4,294,967,295 instead of the normalized 64-bit representation of -1.

**Fix:** pass the result type's signedness to `ebpf_fe_store_result`, exactly as the negate arm does. No new normalization algorithm or opcode is introduced.

This reproducer targets the canonical-IR/emitter boundary. It does not claim a complete C translation unit was executed on an eBPF target. Integration tests for spill/reload, signed comparison, and widening remain appropriate gates.

Ready issue: [ebpf-signed-not.md](issues/ebpf-signed-not.md). Fix: patchset 01.

### N3 — P1: over-aligned scalar memory operations produce invalid Wasm

**Location:** `src/buster/lib/compiler/wasm/wasm.c`, `wasm64_fe_load` and `wasm64_fe_store`.

Both helpers encode `log2(type->layout.alignment)` without limiting the memory instruction's hint to its natural access width. Stronger alignment on the underlying object is therefore allowed to produce an illegal instruction immediate. The [original implementation](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/wasm/wasm.c#L1483-L1534) applies this policy to both memory directions.

For a four-byte integer aligned to sixteen bytes, the emitted load bytes are `28 04 00`, and store bytes are `36 04 00`. The alignment exponent is 4 instead of at most 2. Node/V8 rejects the resulting memory64 modules:

```text
invalid alignment; expected maximum alignment is 2, actual alignment is 4
```

The test obtains instruction bytes from the actual C helpers, wraps them in minimal memory64 modules, and validates them with `new WebAssembly.Module` in Node v22.16.0. Eight scalar shapes, seven alignment values, and two directions give **112 cases: 44 valid before, 112 valid after**. The 68 baseline failures are validation failures from the independent engine, not a self-written alignment assertion.

**Fix:** cap the instruction hint at the scalar access width, with an eight-byte maximum for these supported scalar instructions. Keep weaker alignment hints and the object's actual layout unchanged. This patch does not change stack allocation or allocation alignment.

This is separate from existing issue #97, which concerns the Wasm shadow stack. The test validates modules; it does not execute an entire frontend-generated program.

Ready issue: [wasm-alignment.md](issues/wasm-alignment.md). Fix: patchset 02.

### N4 — P2: eBPF symbol insertion performs quadratic work

**Location:** `ebpf_add_symbol_record` → `ebpf_symbol_by_key`, plus the related symbol-resolution callers.

Every new symbol first scans the complete growing symbol-record array to find an existing key. For N unique keys this performs exactly **N(N−1)/2 unsuccessful key comparisons**, even though the caller already has numeric symbol keys. The [source loop](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/ebpf/ebpf.c#L570-L594) establishes the asymptotic cost without a performance-counter estimate.

A probe invoking the actual insertion helper produced this illustrative run:

| Unique keys | Comparisons implied by source | Elapsed helper time |
|---:|---:|---:|
| 2,048 | 2,096,128 | 3.658 ms |
| 4,096 | 8,386,560 | 5.966 ms |
| 8,192 | 33,550,336 | 16.765 ms |
| 16,384 | 134,209,536 | 63.632 ms |

The counts are analytically derived; they are not measured PMU events. The elapsed times are one unpinned virtualized-host microbenchmark run. They are **not Zen 5 results, an end-to-end compiler measurement, or a promised speedup**.

The appropriate experiment is a key-to-record-index side table or compact lookup structure that preserves duplicate-key semantics and stable ELF output. An index should hold record indices rather than pointers that become stale when the vector grows. Synthetic string symbols and relocation lookup need the same ownership contract. SIMD-scanning the growing list is not the first remedy for unnecessary quadratic work.

**No performance patch was prepared.** This change needs a measured data-structure migration and whole-object-emission validation. It is distinct from #102's verifier-stack allocation problem and #117's ELF dynamic-object lookup problem.

Ready issue: [ebpf-symbol-growth.md](issues/ebpf-symbol-growth.md). Probe and raw result: [ebpf_symbols.c](evidence/ebpf_symbols.c), [log](evidence/ebpf-symbol-growth.log).

## 3. Existing issues reproduced and fixed without duplicate reports

### #144 — Returned path storage is rewound by scratch cleanup

The existing [issue #144](https://github.com/buster14a/buster/issues/144) describes `executable_resolve_in_path` and Windows `os_path_absolute` choosing scratch without excluding their output arena. The Linux reproduction returned `/bin/sh` while leaving the output arena's position unchanged; the next allocation overwrote the returned bytes.

Both sites now use `scratch_begin(&arena, 1)`. The final regression uses **each of the two scratch arenas as output** and checks that the returned path survives another allocation. Both pass. The analogous Windows source change is included, but Windows canonicalization was not run and must be covered in CI before merging.

### #101 — Required read mapping rejects a relative path

The POSIX mapping path was gated on the first path byte being `/`, preventing an ordinary relative file name from reaching the OS mapping path. With `map_required=1`, this is an observable failure instead of just falling back to copying.

Removing the absolute-only gate permits the existing mapping implementation to handle relative names. The test writes a small file in a temporary working directory and checks that required read mapping succeeds with exact bytes. The original implementation fails; the patched version succeeds. No duplicate issue draft was created for [#101](https://github.com/buster14a/buster/issues/101).

### #100 — Null empty String8 values reach memcpy

Both `string_duplicate_arena` and `string_join_arena` now skip the copy when length is zero. Before the patch, the null-empty duplicate triggers UBSan's nonnull-argument diagnostic. After the patch, a terminated empty duplicate and a mixed empty/nonempty join produce the expected bytes with no sanitizer diagnostic.

This is **only a partial fix for [#100](https://github.com/buster14a/buster/issues/100)**, whose scope also includes C frontend spelling-space helpers. The runtime PR draft references the issue but deliberately does not close it. It does not alter the separate length-overflow issue #111.

All three changes are in patchset 03, with their runtime regression. Other unrelated file, process, allocation, and String8 contracts remain open as previously tracked.

## 4. How current issues and PRs affected this work

Nine open PR descriptions were inspected for scope and overlap. This was not a full approval review of their diffs.

| Existing work | Audit decision |
|---|---|
| #141, fixes #140: bridge deploy-key pagination | Already has a targeted fix; no duplicate report or competing patch |
| #139: large compiler-throughput series | Avoid overlapping frontend, preprocessor, native selector, allocator, and encoder changes; do not re-state its performance numbers as independently reproduced |
| #134–#138: incoming-edge indexing, SIMD owner lookup, edge snapshots, prologue encoding, opcode layout | Account for their in-flight scope; no competing optimization patch from this audit |
| #127, fixes #126: AVX-512 memory effects | Already tracked correctness work; no duplicate metadata report |
| #94: GitHub CI matrix expansion | Treat as in-flight CI work, not a new gap to re-file |

One important performance caution: #139's description records unsuccessful incoming-edge-index experiments. That is relevant context for #134, not proof that #134 is necessarily bad in its own target/configuration. The next decision needs a same-source measurement; the audit did not approve or reject that PR on hearsay.

The known frontend and verifier reports #146–#156, IR validation #62, resource-check contracts #63, debug-index validation #64, and other runtime/file findings remained in the backlog. They were not recast as new findings. Likewise, broad SSA/native-MIR migration and throughput epics were treated as architectural context, not evidence that the specific new eBPF/Wasm defects were already covered.

## 5. Patchsets and validation

| Patch | Production scope | Focused test |
|---|---|---|
| [01 — eBPF scalar correctness](patches/01-ebpf-scalar-correctness.patch) | Three changed expressions and a branch-layout comment in ebpf.c | `python3 tests/ebpf_scalar_regression.py` |
| [02 — Wasm memory alignment](patches/02-wasm-memory-alignment.patch) | Load/store alignment-hint expressions in wasm.c | `python3 tests/wasm_memory_alignment_regression.py` |
| [03 — Runtime boundaries](patches/03-runtime-boundaries.patch) | Scratch ownership, POSIX mapping gate, null-empty copies | `python3 tests/runtime_boundary_regression.py` |

Across production code, these patches add 18 lines and remove 10. The larger patch sizes are mainly standalone regression fixtures and runners. They add no optimization passes and make no unmeasured compiler-throughput claim.

### Before/after results

| Test group | Original implementation | Patched implementation |
|---|---|---|
| eBPF scalar bytecode semantics | 244/474 pass, 230 fail | **474/474 pass** |
| Wasm memory64 module validation | 44/112 validate, 68 fail | **112/112 validate** |
| Scratch output lifetime | Returned bytes overwritten for first scratch arena | Both scratch output arenas preserve bytes |
| Required relative file map | Fails | Succeeds, bytes checked |
| Null-empty String8 | UBSan nonnull memcpy diagnostic | Duplicate and mixed join pass |

The final focused suite passed in all four configurations: Clang -O0, Clang -O2, Clang -O2 with ASan/UBSan, and GCC -O2. Clang was 17.0.0; GCC was 14.2.0. Sanitizers were set to stop on the first error.

Each patch was also tested **independently**: a separate clean worktree at the recovered base, final regression copied in to demonstrate baseline failure, only that patch applied, then a successful Clang -O2 ASan/UBSan run. This rules out the eBPF or Wasm fix accidentally depending on the runtime patch. `git apply --check` and `git diff --check` passed for each.

Evidence includes the full before/after logs, independent-run logs, and the self-host bootstrap failure. The eBPF mini-interpreter checks only the instruction subset emitted by these fixtures; it is not a kernel verifier. The Wasm oracle validates rather than executes. Standalone tests are not registered in the normal test runner yet.

## 6. What is not established

There is no full current-main compiler build, test_all result, deterministic self-host fixed point, native execution-mode matrix, Windows runtime execution, or Linux-kernel eBPF load result from this session. The attempted self-host bootstrap stops immediately at missing TCC. The recovered test checkout does not silently stand in for current main.

The focused unity translation units leave an unused IR path without `target_data_layout` defined; Clang warns about that path, and linker section garbage collection discards it. The executable probes link and run, but their logs are not proof of a warning-clean full project build. No warning suppression was added to production code.

No end-to-end or Zen 5 performance gain is claimed for the symbol-map proposal, and no performance refactoring was shipped without such a gate. Existing open PR performance claims remain attributed to those PRs, not reproduced by this audit.

The PR descriptions therefore request **draft** publication until normal module/full-suite, self-host, and affected platform gates pass. This report is a set of verified findings and tested local fixes, not a clean bill of health for the entire codebase.

## 7. Publication package

`issues/` contains four complete new reports; `prs/` contains three draft PR bodies; `manifest.json` maps issue placeholders, branches, exact preimages, and patch hashes. `publish.py` defaults to a no-network plan and requires explicit `--publish --checkout /path/to/buster` for any remote change.

The helper uses a freshly fetched GitHub main and isolated worktrees, checks the exact production preimages, reruns the focused sanitizer tests, and then creates/reuses issues and opens draft PRs on new branches. It does not push the old archived history or force-push. Existing closed matches or unexpected branches stop the run for manual review. Exact-title/marker duplicate matching is not semantic triage, so current discussion should still be reviewed before publication.

Only the helper's offline plan and local logic were tested here. Remote writes are not atomic; the helper records successful URLs as it progresses. Until it is explicitly run in a connected environment, the publication status remains **unpublished**.
