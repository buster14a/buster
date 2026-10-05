# A/B compile-time comparison

**NO DETECTABLE DIFFERENCE: wall time B/A 1.1352, 95% CI [0.9421, 1.3727] includes 1.0 over 6 pairs; any real change is within about +/-37.27% (wider than the 0.5% practical floor: more pairs are needed to rule out an effect of that size). Detectable moves (CI excludes 1; they explain, they do not decide): instructions +0.11%.**

- baseline (A): `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/perf-main2f00/ide-base` sha256 `9fd5acd8fcae1a5996f0b07e06839a89b3b8a0996e96d04f5204aee413f26be7`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- candidate (B): `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/2622-frozen-perf/ide-cand-samepath` sha256 `0c210cf5002fa7ba6b8984c3ecfe644bbc0dc84b6b3257fd3053f82be734ee00`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- phase metrics collection: enabled; both compilers support -fmetrics-out
- outputs of A and B: differ
- generated code bytes (executable sections, exact): A 13,808,870, B 13,759,566, B/A 0.996430
- workload `IDE cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT` in `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/pr2622-baseline`, pinned to CPU 2; 6 complete pairs in ABBA order (--pairs 6)
- binary instances: a fresh copy per timed run and capture; practical floor 0.5% (--min-effect)
- machine-readable: `summary.json` (schema `buster-uarch-lab-compare-v2`); `python3 tools/uarch_lab.py report /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/2622-uarch` re-renders both files

## Warnings

- baseline and candidate outputs differ (expected for a code-generation change; a pure refactor should be identical)
- wall: the bootstrap CI of the geometric mean (slower) disagrees with the sign-test verdict (no detectable difference); the effect is at the edge of detectability
- instructions changed +0.11% but wall time shows no detectable difference: a proxy is not a win

## Metrics (B/A per pair)

`B/A` is the median of the per-pair ratios; its 95% CI uses sign-test order statistics (distribution-free; coverage 0.9688). The geometric mean has a seeded bootstrap CI (seed 20261005, 2000 resamples). The verdict uses wall time only.
Outcomes need the whole CI beyond the 0.5% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside it. instructions:u is exact (no floor).

| metric | unit | A median | B median | B/A | 95% CI | change | geomean B/A | bootstrap 95% CI | min B / min A | outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| wall time (harness span) | s | 3.2026 | 3.7513 | 1.1352 | [0.9421, 1.3727] | +13.52% | 1.1228 | [1.0202, 1.2464] | 0.9950 | no detectable difference |
| task-clock | s | 3.1512 | 3.4116 | 1.0780 | [0.9467, 1.2136] | +7.80% | 1.0835 | [1.0060, 1.1662] | 0.9965 | no detectable difference |
| compiler wall_ns | s | 3.1806 | 3.7306 | 1.1350 | [0.9460, 1.3620] | +13.50% | 1.1223 | [1.0214, 1.2396] | 0.9948 | no detectable difference |
| instructions:u | count | 20,477,645,256 | 20,500,835,934 | 1.0011 | [1.0011, 1.0011] | +0.11% | 1.0011 | [1.0011, 1.0011] | 1.0011 | higher |
| cycles:u | count | 9,696,431,750 | 9,986,660,476 | 1.0060 | [0.9668, 1.0876] | +0.60% | 1.0174 | [0.9865, 1.0518] | 0.9992 | no detectable difference |
| IPC | ratio | 2.1119 | 2.0528 | 0.9952 | [0.9205, 1.0355] | -0.48% | 0.9841 | [0.9518, 1.0149] | 0.9889 | no detectable difference |
| branch-misses:u | count | 77,128,980 | 77,314,032 | 1.0027 | [0.9961, 1.0058] | +0.27% | 1.0021 | [0.9993, 1.0044] | 1.0005 | no detectable difference |
| branch MPKI | per_1k_instr | 3.7665 | 3.7713 | 1.0016 | [0.9950, 1.0046] | +0.16% | 1.0010 | [0.9982, 1.0032] | 0.9994 | no detectable difference |
| page faults | count | 4,120 | 3,968 | 0.9567 | [0.9224, 1.1019] | -4.33% | 0.9743 | [0.9341, 1.0313] | 1.0034 | no detectable difference |
| minor faults | count | 1,942 | 1,792 | 0.9080 | [0.8354, 1.2414] | -9.20% | 0.9446 | [0.8607, 1.0719] | 1.0088 | no detectable difference |
| major faults | count | 2,178 | 2,177 | 0.9995 | [0.9995, 0.9995] | -0.05% | 0.9995 | [0.9995, 0.9995] | 0.9995 | below-floor |
| peak RSS (wait4 ru_maxrss) | bytes | 955,348,992 | 957,790,208 | 1.0025 | [1.0017, 1.0032] | +0.25% | 1.0025 | [1.0020, 1.0029] | 1.0021 | below-floor |

## Phases (-fmetrics-out, median ms)

| phase | A ms | B ms | delta ms | B/A | 95% CI | change | outcome |
|---|---|---|---|---|---|---|---|
| read | 0.01 | 0.01 | +0.00 | 1.0905 | [0.6061, 1.8296] | +9.05% | no detectable difference |
| preprocess | 197.50 | 229.47 | +31.97 | 1.1059 | [0.8790, 1.3584] | +10.59% | no detectable difference |
| parse | 68.36 | 76.06 | +7.70 | 1.1434 | [0.8066, 1.5339] | +14.34% | no detectable difference |
| analysis | 1673.67 | 1828.72 | +155.05 | 1.0771 | [0.9337, 1.3403] | +7.71% | no detectable difference |
| ir | 251.56 | 260.83 | +9.27 | 1.0173 | [0.9701, 1.1802] | +1.73% | no detectable difference |
| codegen | 853.25 | 868.94 | +15.69 | 1.0171 | [0.9743, 1.2412] | +1.71% | no detectable difference |
| object | 61.06 | 58.84 | -2.22 | 0.9671 | [0.9351, 1.1344] | -3.29% | no detectable difference |
| emit | 0.03 | 0.03 | -0.00 | 0.9923 | [0.8878, 1.0436] | -0.77% | no detectable difference |
| total | 3112.59 | 3367.91 | +255.32 | 1.0763 | [0.9479, 1.2170] | +7.63% | no detectable difference |

## Checks

- order effect: not checked (too few pairs per subset); AB (A ran first) n=3 median 1.0610 CI NA, BA (B ran first) n=3 median 1.2093 CI NA
- drift: not checked (too few pairs per subset); first half n=3 median 0.9950 CI NA, second half n=3 median 1.2138 CI NA
- second run of a pair vs first: x0.9367 (ABBA cancels it in the overall ratio)
- per-tenth median B/A: 1.1352; per-tenth median A wall: 3.2026

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
