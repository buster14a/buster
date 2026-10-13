# A/B compile-time comparison

**Candidate is SLOWER: wall time B/A 1.0260 (+2.60%), 95% CI [1.0232, 1.0299] over 12 pairs, beyond the 0.5% practical floor. Detectable moves (CI excludes 1; they explain, they do not decide): instructions +1.48%; cycles +2.59%; IPC -1.08%; phases ir +34.1 ms, codegen +6.2 ms, object +1.3 ms.**

- baseline (A): `/var/lib/buster-github-runner/actions-runner/_work/_temp/compiler-bench/work/bin/ide-cand` sha256 `4ead3e21537f3973f99f3c232ea6d40a1ef95dbef29f21eff2e8a2a02fc28eb1`; 12 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- candidate (B): `/var/lib/buster-github-runner/actions-runner/_work/_temp/compiler-bench/work/bin/ide-cand` sha256 `4ead3e21537f3973f99f3c232ea6d40a1ef95dbef29f21eff2e8a2a02fc28eb1`; 12 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- phase metrics collection: enabled; both compilers support -fmetrics-out
- outputs of A and B: differ
- generated code bytes (executable sections, exact): A 15,257,022, B 15,372,926, B/A 1.007597
- workload `IDE cc -march=native -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o OUT` in `/var/lib/buster-github-runner/actions-runner/_work/buster/buster/candidate`, pinned to CPU 2; 12 complete pairs in ABBA order (--pairs 12)
- binary instances: a fresh copy per timed run and capture; practical floor 0.5% (--min-effect)
- machine-readable: `summary.json` (schema `buster-uarch-lab-compare-v2`); `python3 tools/uarch_lab.py report /var/lib/buster-github-runner/actions-runner/_work/_temp/compiler-bench/work/inline-acceptance/stage1` re-renders both files

## Warnings

- baseline and candidate outputs differ (expected for a code-generation change; a pure refactor should be identical)

## Metrics (B/A per pair)

`B/A` is the median of the per-pair ratios; its 95% CI uses sign-test order statistics (distribution-free; coverage 0.9614). The geometric mean has a seeded bootstrap CI (seed 20261003, 2000 resamples). The verdict uses wall time only.
Outcomes need the whole CI beyond the 0.5% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside it. instructions:u is exact (no floor).

| metric | unit | A median | B median | B/A | 95% CI | change | geomean B/A | bootstrap 95% CI | min B / min A | outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| wall time (harness span) | s | 1.5757 | 1.6163 | 1.0260 | [1.0232, 1.0299] | +2.60% | 1.0258 | [1.0217, 1.0290] | 1.0280 | slower |
| task-clock | s | 1.5684 | 1.6098 | 1.0266 | [1.0244, 1.0298] | +2.66% | 1.0274 | [1.0256, 1.0294] | 1.0281 | slower |
| compiler wall_ns | s | 1.5667 | 1.6078 | 1.0264 | [1.0242, 1.0298] | +2.64% | 1.0273 | [1.0255, 1.0293] | 1.0279 | slower |
| instructions:u | count | 23,803,734,720 | 24,156,458,380 | 1.0148 | [1.0148, 1.0148] | +1.48% | 1.0148 | [1.0148, 1.0148] | 1.0148 | higher |
| cycles:u | count | 8,354,400,472 | 8,571,234,282 | 1.0259 | [1.0235, 1.0299] | +2.59% | 1.0267 | [1.0248, 1.0289] | 1.0272 | higher |
| IPC | ratio | 2.8492 | 2.8183 | 0.9892 | [0.9853, 0.9915] | -1.08% | 0.9884 | [0.9863, 0.9902] | 0.9871 | lower |
| branch-misses:u | count | 73,906,654 | 75,263,623 | 1.0186 | [1.0126, 1.0208] | +1.86% | 1.0177 | [1.0154, 1.0201] | 1.0164 | higher |
| branch MPKI | per_1k_instr | 3.1048 | 3.1157 | 1.0037 | [0.9978, 1.0059] | +0.37% | 1.0028 | [1.0006, 1.0052] | 1.0015 | no detectable difference |
| page faults | count | 5,151 | 5,254 | 0.9946 | [0.9391, 1.0558] | -0.54% | 1.0008 | [0.9734, 1.0295] | 0.9978 | no detectable difference |
| minor faults | count | 5,151 | 5,254 | 0.9946 | [0.9391, 1.0558] | -0.54% | 1.0008 | [0.9734, 1.0295] | 0.9978 | no detectable difference |
| major faults | count | 0 | 0 | NA | NA | NA | NA | NA | NA | no ratio (a zero value) |
| peak RSS (wait4 ru_maxrss) | bytes | 1,126,813,696 | 1,138,124,800 | 1.0102 | [1.0095, 1.0110] | +1.02% | 1.0102 | [1.0098, 1.0105] | 1.0103 | higher |

## Phases (-fmetrics-out, median ms)

| phase | A ms | B ms | delta ms | B/A | 95% CI | change | outcome |
|---|---|---|---|---|---|---|---|
| read | 0.00 | 0.00 | -0.00 | 1.0403 | [0.9343, 1.0730] | +4.03% | no detectable difference |
| preprocess | 104.72 | 104.87 | +0.15 | 1.0017 | [0.9990, 1.0067] | +0.17% | no detectable difference |
| parse | 53.14 | 53.19 | +0.05 | 1.0020 | [0.9856, 1.0147] | +0.20% | no detectable difference |
| analysis | 817.52 | 817.39 | -0.13 | 1.0015 | [0.9960, 1.0054] | +0.15% | no detectable difference |
| ir | 148.30 | 182.45 | +34.15 | 1.2304 | [1.2260, 1.2324] | +23.04% | slower |
| codegen | 386.78 | 393.01 | +6.24 | 1.0154 | [1.0132, 1.0174] | +1.54% | slower |
| object | 34.37 | 35.70 | +1.33 | 1.0370 | [1.0294, 1.0419] | +3.70% | slower |
| emit | 0.42 | 0.42 | +0.00 | 1.0031 | [0.9789, 1.0123] | +0.31% | no detectable difference |
| total | 1547.55 | 1588.31 | +40.76 | 1.0264 | [1.0243, 1.0301] | +2.64% | slower |

## Checks

- order effect: ok; AB (A ran first) n=6 median 1.0243 CI [1.0083, 1.0347], BA (B ran first) n=6 median 1.0288 CI [1.0242, 1.0316]
- drift: ok; first half n=6 median 1.0251 CI [1.0083, 1.0316], second half n=6 median 1.0288 CI [1.0232, 1.0347]
- second run of a pair vs first: x0.9978 (ABBA cancels it in the overall ratio)
- per-tenth median B/A: 1.0083 1.0316 1.0226 1.0260 1.0251 1.0254 1.0299 1.0232 1.0289 1.0317; per-tenth median A wall: 1.6032 1.5706 1.5777 1.5726 1.5739 1.5761 1.5761 1.5788 1.5725 1.5711

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
