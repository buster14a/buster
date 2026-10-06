# A/B compile-time comparison

**NO DETECTABLE DIFFERENCE: wall time B/A 1.0127, 95% CI [0.4670, 1.0851] includes 1.0 over 6 pairs; any real change is within about +/-53.30% (wider than the 0.5% practical floor: more pairs are needed to rule out an effect of that size). Detectable moves (CI excludes 1; they explain, they do not decide): no counter or phase moved detectably.**

- baseline (A): `/home/david/Documents/Codex/2026-10-05/fi/work/perfbin/ide-base` sha256 `9b162962970223b1723c6cafed3c0d1c5574b43811e8f1e1bbdefec3caca2b1b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- candidate (B): `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/pr2493/build-perf/Release/ide` sha256 `38655c865845df2b0f7f843dc68369aee52b2da808bed4d308b8dd46e71fbd43`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- phase metrics collection: enabled; both compilers support -fmetrics-out
- outputs of A and B: byte-identical
- generated code bytes (executable sections, exact): A 13,780,790, B 13,780,790, B/A 1.000000
- workload `IDE cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT` in `/home/david/Documents/Codex/2026-10-05/fi/work/rootperf`, pinned to CPU 2; 6 complete pairs in ABBA order (--pairs 6)
- binary instances: a fresh copy per timed run and capture; practical floor 0.5% (--min-effect)
- machine-readable: `summary.json` (schema `buster-uarch-lab-compare-v2`); `python3 tools/uarch_lab.py report /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/2493-vendor-uarch` re-renders both files

## Warnings

- none

## Metrics (B/A per pair)

`B/A` is the median of the per-pair ratios; its 95% CI uses sign-test order statistics (distribution-free; coverage 0.9688). The geometric mean has a seeded bootstrap CI (seed 20261003, 2000 resamples). The verdict uses wall time only.
Outcomes need the whole CI beyond the 0.5% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside it. instructions:u is exact (no floor).

| metric | unit | A median | B median | B/A | 95% CI | change | geomean B/A | bootstrap 95% CI | min B / min A | outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| wall time (harness span) | s | 4.8324 | 4.9292 | 1.0127 | [0.4670, 1.0851] | +1.27% | 0.8373 | [0.6380, 1.0457] | 1.0294 | no detectable difference |
| task-clock | s | 4.7521 | 4.8572 | 1.0139 | [0.5606, 1.0873] | +1.39% | 0.8632 | [0.6927, 1.0474] | 1.0307 | no detectable difference |
| compiler wall_ns | s | 4.7431 | 4.8586 | 1.0128 | [0.4640, 1.0815] | +1.28% | 0.8365 | [0.6358, 1.0436] | 1.0299 | no detectable difference |
| instructions:u | count | 20,398,696,256 | 20,398,795,284 | 1.0000 | [1.0000, 1.0000] | +0.00% | 1.0000 | [1.0000, 1.0000] | 1.0000 | no detectable difference |
| cycles:u | count | 10,082,357,850 | 10,187,942,560 | 1.0016 | [0.9118, 1.0402] | +0.16% | 0.9929 | [0.9564, 1.0210] | 0.9991 | no detectable difference |
| IPC | ratio | 2.0233 | 2.0023 | 0.9984 | [0.9614, 1.0968] | -0.16% | 1.0071 | [0.9795, 1.0456] | 1.0741 | no detectable difference |
| branch-misses:u | count | 78,044,226 | 77,874,719 | 0.9978 | [0.9602, 1.0010] | -0.22% | 0.9915 | [0.9784, 0.9996] | 1.0003 | no detectable difference |
| branch MPKI | per_1k_instr | 3.8259 | 3.8176 | 0.9978 | [0.9602, 1.0010] | -0.22% | 0.9915 | [0.9784, 0.9996] | 1.0003 | no detectable difference |
| page faults | count | 31,302 | 128,722 | 1.0725 | [0.9780, 28.2049] | +7.25% | 2.0742 | [1.0345, 6.2678] | 1.0768 | no detectable difference |
| minor faults | count | 29,124 | 126,544 | 1.1245 | [0.9545, 58.2822] | +12.45% | 2.3765 | [1.0423, 9.0726] | 1.1799 | no detectable difference |
| major faults | count | 2,178 | 2,178 | 1.0000 | [1.0000, 1.0000] | +0.00% | 1.0000 | [1.0000, 1.0000] | 1.0000 | no detectable difference |
| peak RSS (wait4 ru_maxrss) | bytes | 936,542,208 | 818,157,568 | 0.9841 | [0.8286, 0.9995] | -1.59% | 0.9506 | [0.8970, 0.9941] | 0.9901 | below-floor |

- page faults: bimodal counts: compare medians, not the paired ratio (ratio of medians 4.1122, median of ratios 1.0725)
- minor faults: bimodal counts: compare medians, not the paired ratio (ratio of medians 4.3450, median of ratios 1.1245)

## Phases (-fmetrics-out, median ms)

| phase | A ms | B ms | delta ms | B/A | 95% CI | change | outcome |
|---|---|---|---|---|---|---|---|
| read | 0.01 | 0.01 | -0.00 | 0.9066 | [0.2844, 1.5230] | -9.34% | no detectable difference |
| preprocess | 354.17 | 351.80 | -2.37 | 0.9761 | [0.3194, 1.1920] | -2.39% | no detectable difference |
| parse | 110.47 | 101.94 | -8.53 | 0.9215 | [0.3573, 1.0222] | -7.85% | no detectable difference |
| analysis | 2482.09 | 2576.82 | +94.73 | 1.0149 | [0.4241, 1.1032] | +1.49% | no detectable difference |
| ir | 352.26 | 341.10 | -11.16 | 0.9683 | [0.4817, 1.0787] | -3.17% | no detectable difference |
| codegen | 1227.07 | 1169.25 | -57.82 | 1.0040 | [0.4604, 1.0755] | +0.40% | no detectable difference |
| object | 107.11 | 101.93 | -5.18 | 0.9765 | [0.7441, 1.2443] | -2.35% | no detectable difference |
| emit | 0.09 | 0.05 | -0.04 | 0.6869 | [0.4084, 1.6953] | -31.31% | no detectable difference |
| total | 4628.16 | 4745.74 | +117.58 | 1.0142 | [0.4617, 1.0850] | +1.42% | no detectable difference |

## Checks

- order effect: not checked (too few pairs per subset); AB (A ran first) n=3 median 1.0272 CI NA, BA (B ran first) n=3 median 0.9981 CI NA
- drift: not checked (too few pairs per subset); first half n=3 median 0.6442 CI NA, second half n=3 median 1.0272 CI NA
- second run of a pair vs first: x1.0145 (ABBA cancels it in the overall ratio)
- per-tenth median B/A: 1.0127; per-tenth median A wall: 4.8324

## Method

- bootstrap: percentile bootstrap 95% CI of the geometric-mean ratio, resampling pairs with the recorded seed (cross-check)
- ci: exact distribution-free 95% CI for the median ratio from sign-test order statistics (assumes only independent pairs)
- drift: first-half and second-half median-ratio CIs must overlap; per-tenth medians shown
- floor: --min-effect percent (default 0.5) applies to every time and counter outcome except instructions (exact); LAB3 measured a 0.5% per-binary-instance offset in A/A that the pair-to-pair CI does not cover
- fresh_copy: each timed run and profile capture executes a new read/write copy of its binary (new inode, fsync'd) that is deleted afterwards, so page-cache placement varies per run instead of biasing one variant (--no-fresh-copy disables)
- order_effect: AB and BA pairs' median-ratio CIs must overlap
- pairing: one A run and one B run per pair, alternating ABBA blocks (A,B then B,A); the pair count is fixed before the series
- ratio: median of the per-pair ratios B/A
- verdict: wall time (harness span) only: faster/slower when its whole CI lies beyond 1 -/+ the practical floor; below-floor when the CI excludes 1.0 but reaches inside the floor; no detectable difference when it contains 1.0

Raw files: `compare.json` (config, plan, step states), `pairs.json` and `pairs/NNNN-{a,b}.csv|.ccmetrics` (one per run), `a/` and `b/` (probes, warm-up, reference output, profile steps).
