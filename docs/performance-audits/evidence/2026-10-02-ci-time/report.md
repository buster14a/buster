# Buster CI: time breakdown and process-heavy workload audit

**Report date:** 2 October 2026. **Repository:** buster14a/buster.

## Executive result

Build-graph generation is a small part of the measured desktop work. In the production-layout sample, the three principal phases divide into **0.87% configure/generation, 46.95% build, and 52.18% test payload**. In the split-layout experiment they divide into **0.93%, 36.67%, and 62.40%**, respectively. These are workload-phase proportions, not percentages of workflow latency or CPU consumption.

For time-to-green, the strongest immediate target is the sanitized Debug `compiler_driver_tests` tail. For process counts, inspect driver, C frontend, metamorphic, and bitcode tests. Process-heavy does not establish process-startup-bound: existing driver operation evidence contains substantial **in-process compiler work** and existing observer/link caches.

The split checks experiment is the strongest measured scheduling candidate. Its existing nine-run campaign reports a 26.12% lower median workflow latency at essentially unchanged median runner work, but full qualification remains pending. Do not adopt the Windows all-builds barrier or restart an unqualified batching project simply because its process count is lower.

## Scope and provenance

I downloaded and verified **26 desktop artifacts plus one analyzer artifact**, totaling 6,003,709 compressed bytes. Every raw ZIP SHA-256 matches the digest published by GitHub. The desktop coverage of this audit is **44 configured trees and 26 captured runtime invocations**, with 1,420 module-timing records. Every inspected retained phase summary declares complete, has no reported errors, and matches the selected source/run/attempt. Runtime log hashes match their native receipts, which report complete capture, unchanged binary, and a zero test result.

This audit aggregates existing retained summaries and parses ASCII diagnostic records from raw log bytes. It does **not** claim to have rerun the repository's full phase-journal or assertion qualification. The existing campaign separately reports its independent replay. No compiler build, test run, benchmark, CI dispatch, source change, policy change, laptop execution, or 9700X use was performed here.

Both detailed samples use source **`e424b387fcb51b00c1d19e87e2d369ae8a212792`**, tree **`b2ab099b499f7d5c8e91de92312f270c475c0a8f`**, and attempt 1. These are frozen campaign samples, not measurements of every subsequent main commit. Current repository instructions/workflow were inspected at main **`d4ca72fb4e97ae0a410b0d03258b00495fa45e13`**.

| Sample | Run | Layout | Detailed desktop artifacts | Whole-workflow elapsed | Executed runner time |
|---|---|---|---:|---:|---:|
| A3 | 36991068435 | Combined checks, overlapping admission; production-layout sample | 10 | 28m58s | 196.40 min |
| C4 | 36996168725 | Split checks, overlapping admission; experimental layout | 16 | 17m40s | 192.93 min |

Whole-workflow and runner totals above are from the existing independently checked campaign ledger. This audit independently recomputed the desktop phase and test-log breakdown below. These two successful samples do not represent a weekly/account-wide usage total or a causal A/B estimate. Exact-source reuse on main can skip previously proved workloads, so full validation and reused-main runs must not be averaged into one phase ratio.

## 1. Generation, compilation, and tests

The following table sums **per-tree elapsed durations**, including parallel trees. It measures aggregate task time. A minute in two concurrent trees contributes two task-minutes, although only one minute passes on the runner. It must not be added to or subtracted from runner occupancy to infer CPU usage or overhead.

| Measured phase | A3 combined task-minutes | Share of the three phases | C4 split task-minutes | Share of the three phases |
|---|---:|---:|---:|---:|
| CMake configure / build-graph generation | 1.204 | 0.87% | 0.998 | 0.93% |
| Build phase | 64.883 | 46.95% | 39.463 | 36.67% |
| Test payload | 72.112 | 52.18% | 67.156 | 62.40% |
| **Total, three principal phases** | **138.199** | **100%** | **107.617** | **100%** |

Separate retained phases are not hidden inside those three percentages:

| Other measured desktop phase | A3 task-minutes | C4 task-minutes |
|---|---:|---:|
| Post-test work | 4.376 | 4.237 |
| Self-host validation | 4.458 | 4.394 |
| Explicit evidence callbacks | 0.034 | 0.038 |
| **All six measured phases** | **147.067** | **116.285** |

### What the names mean

**Generation here means the observed CMake configure/build-graph-generation phase.** It is not a separately measured total for all source-code generators or compiler code generation. Source-generation commands inside a build are included in that build phase; upstream driver bootstrap/setup is outside the per-tree observations. All compiler code generation occurring inside a test remains part of the test payload.

**Build is not pure compiler CPU.** It includes compile/link work and the observer's documented pre-test build/clean/wrapper interval. **Test payload is not only executing already-built test programs:** compiler tests also invoke Buster in process, compile independent references, link executables, and run subprocesses. Source-generation, compile/link, and execution subphases inside these payloads require the existing operation-level instrumentation to be joined before they can be charged separately.

Native, mobile, UEFI, workflow-tool regression, checkout, installation, upload, runner queue, and dependent-job wait are not assigned to these desktop three-way totals. Their absence is a scope limit, not zero cost. The analyzer is reported separately below rather than mislabeled compilation or tests.

## 2. The latency bottleneck: sanitized Debug tests

The current-layout A3 sample already shows the imbalance inside each Debug tree:

| Debug tree within combined checks | Configure (s) | Build (s) | Tests (s) |
|---|---:|---:|---:|
| Windows x86-64 | 3.674 | 140.295 | 989.676 |
| Linux x86-64 | 0.477 | 104.111 | 792.112 |
| Linux AArch64 | 1.836 | 116.334 | 697.983 |

The C4 split experiment makes attribution especially clear:

| Isolated sanitized Debug lane | Configure (s) | Build (s) | Test payload (s) | `compiler_driver_tests` within payload (s) | Driver share of payload |
|---|---:|---:|---:|---:|---:|
| Windows x86-64 | 2.225 | 58.807 | 829.812 | 816.354 | 98.38% |
| Linux x86-64 | 0.332 | 38.619 | 653.563 | 645.578 | 98.78% |
| Linux AArch64 | 0.236 | 43.291 | 659.282 | 653.265 | 99.09% |

The driver module overlaps with the other test process, so its duration must not be added to every other module to estimate wall time. The existing partition is already two isolated groups under a four-worker budget, with two workers per group. In C4 the driver group outlived the rest group by approximately **116.9 seconds on Windows x86-64, 159.2 seconds on Linux x86-64, and 319.3 seconds on Linux AArch64**. Those are observed imbalance intervals, not guaranteed removable time.

My priority is to reuse fixture/operation scopes to locate repeated in-process compiler/setup work, then evaluate a bounded fixture-level partition or quota allocation that retains the same total CPU budget. The collected run does not contain fixture-duration records, so launch counts alone cannot rank fixtures by time. Keep exact fixture identities, independent references, isolation, deadlines, cleanup, and sanitizer coverage.

## 3. Process-heavy tests: measured logged launches

A3 contains **22,564** `Launched [` records across its 13 captured desktop runtime invocations; C4 contains **22,566** across its 13. These are diagnostic log-record counts, not a complete census of OS process creations: toolchain descendants and unlogged processes can be absent, and a log count has no process-startup CPU duration attached. The two-count difference is not evidence for or against identical test coverage.

| C4 sanitized Debug invocation | All logged launches | Driver module | C frontend | Metamorphic | LLVM bitcode |
|---|---:|---:|---:|---:|---:|
| Linux x86-64 | 2,530 | 1,815 | 315 | 192 | 120 |
| Windows x86-64 | 1,223 | 612 | 243 | 192 | 116 |
| Linux AArch64 | 1,470 | 845 | 243 | 192 | 106 |

Within Linux x86-64 Debug, attribution by the preceding fixture marker identifies **379 launch records in `compiler_driver_test_wide_vector_boundaries`**, **367 in `compiler_driver_test_assembly_symbol_binding`**, and **248 in `compiler_driver_test_native_frame_vectors`**. On Windows Debug, native-frame/vector tests produce 239 records. These are good investigation targets, not a claim that all launches are redundant.

Several Linux child log lines already expose user/system time. They have not been joined into a complete descendant-aware phase ledger here; isolated child CPU records do not establish whole-phase CPU time. Native phase CPU and peak RSS remain unknown in the official phase reports.

### Avoid repeating rejected or deferred work

The existing frame/vector implementation already compiles Buster cases in-process, hoists independent observer compilation, and caches linked executables using object identity with byte comparison. Adding those same caches again is not an optimization.

The historical Windows operation census summarized in #1885 measured 272.788 of 280.195 fixture seconds in primary in-process Buster compilation, approximately 97.36%. Its external host compile/link/run component was about 5.64 seconds. This is old diagnostic evidence, not a current percentage; it demonstrates why a large process count cannot prove launch overhead dominates. The later Linux capture showed a different wait profile, reinforcing the need for configuration-specific attribution.

#1885 / PR #1892 were closed unmerged on 2 October as a deferred optional proposal. The disposition explicitly asserts neither a batching regression nor an accepted speedup. Do not create a duplicate project or revive it based only on PID counts. Reopen the existing ownership only when fresh evidence supports it.

## 4. Analyzer: substantial work, bounded process concurrency

C4 analyzer artifact 11222445904 retains:

| Analyzer observation | Value |
|---|---:|
| Eligible / checked translation units | 138 / 138 |
| Logical shards | 8 |
| Actual worker budget / peak pending workers | 2 / 2 |
| Analyzer run elapsed | 468.248 s / 7m48s |
| Peak sampled live processes | 5 |
| Sampled simultaneous process-tree RSS | 771,153,920 bytes / 735.4 MiB |
| Largest single waited-child RSS | 574,726,144 bytes / 548.1 MiB |

Eight shards do not mean eight concurrent compiler processes. The full diagnostic run reports only two workers and a peak of five live processes. The slowest shard reports 285.604 seconds; the shortest 24.173 seconds. This is a strong imbalance signal but the shards execute in waves, so these extremes alone do not establish an avoidable terminal tail.

A bounded next experiment is **cost-weighted shard assignment using retained per-unit timings**, keeping all 138 units and the same two-worker budget. Another is a separately qualified worker-budget comparison after confirming available CPU/memory and contention. Neither has been run in this audit. Do not replace split translation-unit coverage with one unity translation unit merely to reduce process count; preserve existing analyzer contracts and canonical unity obligations separately. Same-source analyzer comparison reuse is already active in this sample (`selection=skip`, `reason=same-revision`).

## 5. Scheduling, runner cost, and changes worth pursuing

The existing #2119/#2120 campaign—not a newly executed benchmark—records:

| Variant, three successful first attempts each | Median whole-workflow latency | Median executed runner-seconds |
|---|---:|---:|
| Combined checks, overlap | 23m44s | 11,784 |
| Windows all-builds barrier | 26m50s | 11,900 |
| Split checks, overlap | 17m32s | 11,779 |

Split checks show **26.12% lower median latency** with **0.042% lower median runner work**. This is promising, not full causal qualification. Host CPU differences and feature-dependent assertion counts, Windows raw log decoding, complete non-desktop conditions, and resource/reliability review remain explicit blockers in that campaign. Resolve these against existing retained artifacts before more blind dispatches or default changes.

The all-builds barrier instead worsened median Windows checks by 14.30% and whole workflow by 13.06%. Keep overlap rather than applying a blanket “finish every build before any test” rule.

A3 also included **229 seconds of actual queue delay for the final CI-complete job**. Keep that separate from build and test time. The split campaign's failed C2 first attempt consumed 13,474 runner-seconds, with a further 561 runner-seconds for its partial diagnostic retry. These remain reliability/rerun costs, not successful test payload or silently excluded samples.

Exact-source main CI reuse is already delivered. The #709/#1808 checkpoint records a same-SHA source/main observation dropping executed occupancy from 194.78 to 5.73 runner-minutes and latency from 25m38s to 2m14s. This is not a randomized counterfactual and is not attributed to a new change here. Preserve the fail-closed reuse proof and report reused-main runs separately from full runs.

## 6. Ranked recommendations

| Priority | Action | Why / guardrail |
|---|---|---|
| 1 | Finish the existing split-layout qualification, without changing defaults prematurely | Best whole-workflow signal; first resolve raw-byte evidence ingestion, host-feature census, and resource/reliability gaps. |
| 2 | Attribute and optimize the Debug driver tail with existing operation instrumentation | Driver spans 98–99% of the split Debug payload. Target demonstrated repeated useful work/setup or a bounded fixed-budget fixture partition, not unproved spawn overhead. |
| 3 | Evaluate analyzer cost-weighted partitioning at the current worker budget | 138 units, eight highly unequal shard durations, two active workers. Preserve the unit census; compare full-job latency and runner cost. |
| 4 | Investigate high-count external compile/link/run fixtures only after CPU and wait attribution | Wide vectors, symbol binding, and frame vectors have hundreds of logged launches. Preserve independent ABI oracles and subprocess-sensitive semantics; existing caches already cover some repetition. |
| 5 | Extend the existing telemetry rather than introducing a separate profiler framework | Add code-generation/build/test subscopes, process-tree CPU, spawn counts, peak concurrency, and resource scopes to the current observer/operation records. |
| Low | Optimize CMake generation | Under 1% of this measured desktop three-phase task sum; it is not the primary source of long CI latency. |

## 7. The missing measurement contract

For every observed task, retain run/attempt/job, source/tree, toolchain, configuration, fixture identity, CPU model/features, worker quota, queue/dependency/admission/start/end times, and actual completion authority. Distinguish **graph generation, generated-source tools, compiler/linker execution, test-runner execution, reference compilation, and generated-program execution** as nested scopes rather than renaming a mixed step.

Add process-tree user/system CPU and process counts to those scopes. On Windows, the existing contained process hierarchy can expose Job Object `TotalUserTime`, `TotalKernelTime`, `TotalProcesses`, and `ActiveProcesses`; CPU totals cover associated running and terminated processes. On POSIX, use existing waited-child accounting with explicit descendant coverage and no ancestor/child double-counting. Sampled tree RSS, single-child peak RSS, and allocator arena counters are distinct metrics. Missing fields stay unknown, not zero.

Keep instrumentation optional and outside compiler-performance acceptance. Observe before/after instrumentation cost, preserve raw byte hashes and strict proof-record parsing, and retain failed/cancelled attempts. Report CPU utilization only when complete CPU and wall/budget scopes match. A lower PID count, a successful correctness run, or a favorable single sample does not itself qualify an optimization.

## Sources and retained evidence

- Detailed samples: https://github.com/buster14a/buster/actions/runs/36991068435 and https://github.com/buster14a/buster/actions/runs/36996168725
- Nine-run campaign and qualification blockers: https://github.com/buster14a/buster/issues/2120#issuecomment-5950979830
- Current CI ledger / reuse observation: https://github.com/buster14a/buster/issues/709#issuecomment-5948407998
- Phase semantics: https://github.com/buster14a/buster/blob/d4ca72fb4e97ae0a410b0d03258b00495fa45e13/docs/ci-matrix-phases.md
- Driver operation census and already-present caches: https://github.com/buster14a/buster/issues/1885
- Deferred batching disposition: https://github.com/buster14a/buster/issues/1885#issuecomment-5948290674
- Windows process accounting: https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_accounting_information
- Artifact IDs, exact hashes, sizes, and source links: `verified_artifacts.csv` / `.json` in the companion evidence bundle.

**License/provenance:** Buster's first-party license grant remains unselected/unresolved in `LICENSES/README.md` at the inspected source. No upstream implementation, dependency, or vendored code was imported. Analysis uses Python's standard library on retained GitHub artifacts.

## Appendix A: all A3 desktop lanes

All values are seconds. Columns sum tree-phase elapsed intervals; overlapping rows/trees are not serial wall time. Compile-only rows correctly have zero test payload.

| Desktop lane | Configure / graph generation (s) | Build (s) | Test payload (s) | Post-test (s) | Self-host (s) | Evidence (s) |
|---|---:|---:|---:|---:|---:|---:|
| linux-aarch64-checks | 17.665 | 506.886 | 829.883 | 4.763 | 0.000 | 0.000 |
| linux-aarch64-release | 0.148 | 174.072 | 185.736 | 32.283 | 0.000 | 0.000 |
| linux-x86_64-checks | 2.675 | 683.556 | 881.497 | 6.035 | 0.000 | 0.000 |
| linux-x86_64-release | 0.135 | 100.841 | 133.552 | 35.596 | 86.667 | 0.716 |
| macos-aarch64-checks | 20.581 | 672.809 | 503.260 | 4.590 | 0.000 | 0.000 |
| macos-aarch64-release | 3.178 | 108.826 | 59.169 | 80.978 | 103.571 | 0.429 |
| windows-aarch64-checks | 7.336 | 18.171 | 0.000 | 0.000 | 0.000 | 0.000 |
| windows-aarch64-release | 1.078 | 96.978 | 252.805 | 36.823 | 0.000 | 0.000 |
| windows-x86_64-checks | 18.067 | 1389.851 | 1177.747 | 12.516 | 0.000 | 0.000 |
| windows-x86_64-release | 1.352 | 141.017 | 303.055 | 48.978 | 77.240 | 0.899 |

## Appendix B: all C4 desktop lanes

| Desktop lane | Configure / graph generation (s) | Build (s) | Test payload (s) | Post-test (s) | Self-host (s) | Evidence (s) |
|---|---:|---:|---:|---:|---:|---:|
| linux-aarch64-portability | 0.755 | 61.236 | 0.000 | 0.000 | 0.000 | 0.000 |
| linux-aarch64-release | 0.149 | 172.061 | 186.374 | 32.560 | 0.000 | 0.000 |
| linux-aarch64-sanitized-debug | 0.236 | 43.291 | 659.282 | 3.389 | 0.000 | 0.000 |
| linux-aarch64-sanitized-release | 0.190 | 74.212 | 129.268 | 1.325 | 0.000 | 0.000 |
| linux-x86_64-portability | 1.472 | 81.087 | 0.000 | 0.000 | 0.000 | 0.000 |
| linux-x86_64-release | 0.147 | 124.682 | 189.336 | 45.003 | 104.995 | 1.040 |
| linux-x86_64-sanitized-debug | 0.332 | 38.619 | 653.563 | 4.578 | 0.000 | 0.000 |
| linux-x86_64-sanitized-release | 0.276 | 122.272 | 88.736 | 1.751 | 0.000 | 0.000 |
| macos-aarch64-checks | 27.627 | 663.333 | 524.150 | 4.676 | 0.000 | 0.000 |
| macos-aarch64-release | 1.672 | 70.172 | 50.785 | 63.444 | 81.501 | 0.331 |
| windows-aarch64-checks | 7.884 | 19.137 | 0.000 | 0.000 | 0.000 | 0.000 |
| windows-aarch64-release | 1.093 | 98.601 | 233.795 | 36.710 | 0.000 | 0.000 |
| windows-x86_64-portability | 12.481 | 413.831 | 0.000 | 0.000 | 0.000 | 0.000 |
| windows-x86_64-release | 1.173 | 142.759 | 305.687 | 48.252 | 77.123 | 0.904 |
| windows-x86_64-sanitized-debug | 2.225 | 58.807 | 829.812 | 8.887 | 0.000 | 0.000 |
| windows-x86_64-sanitized-release | 2.175 | 183.651 | 178.574 | 3.631 | 0.000 | 0.000 |
