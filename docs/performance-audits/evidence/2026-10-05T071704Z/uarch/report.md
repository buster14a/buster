# A/B compile-time comparison

**NO DETECTABLE DIFFERENCE: wall time B/A 0.9971, 95% CI [0.7297, 1.0089] includes 1.0 over 6 pairs; any real change is within about +/-27.03% (wider than the 0.5% practical floor: more pairs are needed to rule out an effect of that size). Detectable moves (CI excludes 1; they explain, they do not decide): instructions +0.00%; branch MPKI -0.38% (below floor); phases emit +0.0 ms.**

- baseline (A): `/home/david/Documents/Codex/2026-10-05/fi/work/perfbin/ide-base` sha256 `9b162962970223b1723c6cafed3c0d1c5574b43811e8f1e1bbdefec3caca2b1b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- candidate (B): `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/pr2483/build-perf/Release/ide` sha256 `8415bc9bb8253aa41d93e364b7ff44865fe0203c47599b7d08c25c6f7281fd3b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- phase metrics collection: enabled; both compilers support -fmetrics-out
- outputs of A and B: byte-identical
- generated code bytes (executable sections, exact): A 13,780,790, B 13,780,790, B/A 1.000000
- workload `IDE cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT` in `/home/david/Documents/Codex/2026-10-05/fi/work/rootperf`, pinned to CPU 2; 6 complete pairs in ABBA order (--pairs 6)
- binary instances: a fresh copy per timed run and capture; practical floor 0.5% (--min-effect)
- machine-readable: `summary.json` (schema `buster-uarch-lab-compare-v2`); `python3 tools/uarch_lab.py report /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/2483-uarch` re-renders both files

## Warnings

- none

## Metrics (B/A per pair)

`B/A` is the median of the per-pair ratios; its 95% CI uses sign-test order statistics (distribution-free; coverage 0.9688). The geometric mean has a seeded bootstrap CI (seed 20261003, 2000 resamples). The verdict uses wall time only.
Outcomes need the whole CI beyond the 0.5% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside it. instructions:u is exact (no floor).

| metric | unit | A median | B median | B/A | 95% CI | change | geomean B/A | bootstrap 95% CI | min B / min A | outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| wall time (harness span) | s | 3.7447 | 3.7437 | 0.9971 | [0.7297, 1.0089] | -0.29% | 0.9480 | [0.8532, 1.0021] | 1.0002 | no detectable difference |
| task-clock | s | 3.6808 | 3.6794 | 0.9943 | [0.9897, 1.0069] | -0.57% | 0.9958 | [0.9914, 1.0009] | 0.9925 | no detectable difference |
| compiler wall_ns | s | 3.7199 | 3.7154 | 0.9958 | [0.7293, 1.0093] | -0.42% | 0.9476 | [0.8528, 1.0020] | 0.9979 | no detectable difference |
| instructions:u | count | 20,398,662,242 | 20,398,829,016 | 1.0000 | [1.0000, 1.0000] | +0.00% | 1.0000 | [1.0000, 1.0000] | 1.0000 | higher |
| cycles:u | count | 9,879,512,300 | 9,866,038,661 | 0.9945 | [0.9896, 1.0052] | -0.55% | 0.9958 | [0.9917, 1.0005] | 0.9928 | no detectable difference |
| IPC | ratio | 2.0647 | 2.0676 | 1.0055 | [0.9948, 1.0105] | +0.55% | 1.0042 | [0.9995, 1.0084] | 1.0092 | no detectable difference |
| branch-misses:u | count | 77,090,442 | 76,837,084 | 0.9962 | [0.9929, 0.9979] | -0.38% | 0.9960 | [0.9947, 0.9972] | 0.9950 | below-floor |
| branch MPKI | per_1k_instr | 3.7792 | 3.7667 | 0.9962 | [0.9929, 0.9979] | -0.38% | 0.9960 | [0.9947, 0.9972] | 0.9949 | below-floor |
| page faults | count | 3,950 | 3,816 | 0.9982 | [0.9187, 1.0810] | -0.18% | 0.9874 | [0.9462, 1.0303] | 0.9971 | no detectable difference |
| minor faults | count | 1,772 | 1,641 | 0.9975 | [0.8290, 1.1913] | -0.25% | 0.9737 | [0.8861, 1.0700] | 0.9951 | no detectable difference |
| major faults | count | 2,178 | 2,175 | 0.9986 | [0.9986, 0.9986] | -0.14% | 0.9986 | [0.9986, 0.9986] | 0.9986 | below-floor |
| peak RSS (wait4 ru_maxrss) | bytes | 962,289,664 | 962,394,112 | 0.9999 | [0.9989, 1.0009] | -0.01% | 1.0000 | [0.9995, 1.0005] | 0.9998 | no detectable difference |

- minor faults: bimodal counts: compare medians, not the paired ratio (ratio of medians 0.9258, median of ratios 0.9975)

## Phases (-fmetrics-out, median ms)

| phase | A ms | B ms | delta ms | B/A | 95% CI | change | outcome |
|---|---|---|---|---|---|---|---|
| read | 0.01 | 0.01 | +0.00 | 1.0063 | [0.5964, 1.4769] | +0.63% | no detectable difference |
| preprocess | 245.56 | 249.51 | +3.96 | 1.0178 | [0.9858, 1.0222] | +1.78% | no detectable difference |
| parse | 82.10 | 81.90 | -0.20 | 1.0019 | [0.9800, 1.0335] | +0.19% | no detectable difference |
| analysis | 1992.06 | 1984.24 | -7.81 | 0.9883 | [0.9670, 1.0199] | -1.17% | no detectable difference |
| ir | 288.98 | 294.25 | +5.27 | 1.0312 | [0.9614, 1.0384] | +3.12% | no detectable difference |
| codegen | 958.30 | 949.47 | -8.83 | 0.9886 | [0.9833, 1.0186] | -1.14% | no detectable difference |
| object | 71.10 | 72.41 | +1.31 | 1.0251 | [0.9113, 1.0569] | +2.51% | no detectable difference |
| emit | 0.03 | 0.04 | +0.01 | 1.2948 | [1.1006, 2.0020] | +29.48% | slower |
| total | 3636.07 | 3635.51 | -0.57 | 0.9946 | [0.9881, 1.0076] | -0.54% | no detectable difference |

## Checks

- order effect: not checked (too few pairs per subset); AB (A ran first) n=3 median 0.9918 CI NA, BA (B ran first) n=3 median 0.9987 CI NA
- drift: not checked (too few pairs per subset); first half n=3 median 0.9987 CI NA, second half n=3 median 0.9955 CI NA
- second run of a pair vs first: x0.9965 (ABBA cancels it in the overall ratio)
- per-tenth median B/A: 0.9971; per-tenth median A wall: 3.7447

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
