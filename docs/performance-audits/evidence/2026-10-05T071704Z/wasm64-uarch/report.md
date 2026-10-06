# A/B compile-time comparison

**NO DETECTABLE DIFFERENCE: wall time B/A 1.0157, 95% CI [0.9906, 1.0249] includes 1.0 over 6 pairs; any real change is within about +/-2.49% (wider than the 0.5% practical floor: more pairs are needed to rule out an effect of that size). Detectable moves (CI excludes 1; they explain, they do not decide): instructions +0.00%; page faults +0.53% (below floor); phases read +0.0 ms.**

- baseline (A): `/home/david/Documents/Codex/2026-10-05/fi/work/perfbin/ide-base` sha256 `9b162962970223b1723c6cafed3c0d1c5574b43811e8f1e1bbdefec3caca2b1b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- candidate (B): `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/pr2483/build-perf/Release/ide` sha256 `8415bc9bb8253aa41d93e364b7ff44865fe0203c47599b7d08c25c6f7281fd3b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- phase metrics collection: enabled; both compilers support -fmetrics-out
- outputs of A and B: byte-identical
- generated code bytes (executable sections, exact): A NA, B NA, B/A NA; baseline: unknown not an ELF, Mach-O or PE file; candidate: unknown not an ELF, Mach-O or PE file
- workload `IDE cc /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/string_offsets.c -c --target=wasm64-unknown-freestanding -o OUT` in `/home/david/Documents/Codex/2026-10-05/fi/work/rootperf`, pinned to CPU 2; 6 complete pairs in ABBA order (--pairs 6)
- binary instances: a fresh copy per timed run and capture; practical floor 0.5% (--min-effect)
- machine-readable: `summary.json` (schema `buster-uarch-lab-compare-v2`); `python3 tools/uarch_lab.py report /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/2483-wasm64-uarch` re-renders both files

## Warnings

- none

## Metrics (B/A per pair)

`B/A` is the median of the per-pair ratios; its 95% CI uses sign-test order statistics (distribution-free; coverage 0.9688). The geometric mean has a seeded bootstrap CI (seed 20261003, 2000 resamples). The verdict uses wall time only.
Outcomes need the whole CI beyond the 0.5% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside it. instructions:u is exact (no floor).

| metric | unit | A median | B median | B/A | 95% CI | change | geomean B/A | bootstrap 95% CI | min B / min A | outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| wall time (harness span) | s | 0.0310 | 0.0314 | 1.0157 | [0.9906, 1.0249] | +1.57% | 1.0099 | [0.9994, 1.0202] | 0.9933 | no detectable difference |
| task-clock | s | 0.0089 | 0.0090 | 1.0190 | [0.9702, 1.0430] | +1.90% | 1.0104 | [0.9902, 1.0277] | 1.0195 | no detectable difference |
| compiler wall_ns | s | 0.0064 | 0.0066 | 1.0192 | [0.9890, 1.0830] | +1.92% | 1.0216 | [1.0006, 1.0488] | 1.0190 | no detectable difference |
| instructions:u | count | 2,189,271 | 2,189,332 | 1.0000 | [1.0000, 1.0000] | +0.00% | 1.0000 | [1.0000, 1.0000] | 1.0000 | higher |
| cycles:u | count | 4,848,712 | 4,909,886 | 1.0092 | [0.9979, 1.0292] | +0.92% | 1.0103 | [1.0029, 1.0186] | 1.0093 | no detectable difference |
| IPC | ratio | 0.4515 | 0.4459 | 0.9909 | [0.9717, 1.0022] | -0.91% | 0.9898 | [0.9818, 0.9972] | 1.0009 | no detectable difference |
| branch-misses:u | count | 15,536 | 15,714 | 1.0064 | [0.9874, 1.0315] | +0.64% | 1.0082 | [0.9948, 1.0226] | 1.0088 | no detectable difference |
| branch MPKI | per_1k_instr | 7.0964 | 7.1775 | 1.0064 | [0.9874, 1.0315] | +0.64% | 1.0081 | [0.9948, 1.0226] | 1.0087 | no detectable difference |
| page faults | count | 566 | 569 | 1.0053 | [1.0018, 1.0106] | +0.53% | 1.0053 | [1.0029, 1.0080] | 1.0035 | below-floor |
| minor faults | count | 99 | 98 | 0.9899 | [0.9697, 1.0204] | -1.01% | 0.9898 | [0.9764, 1.0050] | 0.9796 | no detectable difference |
| major faults | count | 467 | 471 | 1.0086 | [1.0086, 1.0086] | +0.86% | 1.0086 | [1.0086, 1.0086] | 1.0086 | higher |
| peak RSS (wait4 ru_maxrss) | bytes | 38,268,928 | 37,179,392 | 0.9991 | [0.9410, 1.0569] | -0.09% | 0.9900 | [0.9610, 1.0199] | 1.0003 | no detectable difference |

## Phases (-fmetrics-out, median ms)

| phase | A ms | B ms | delta ms | B/A | 95% CI | change | outcome |
|---|---|---|---|---|---|---|---|
| read | 0.04 | 0.06 | +0.01 | 1.3010 | [1.0171, 1.4437] | +30.10% | slower |
| preprocess | 1.21 | 1.18 | -0.04 | 0.9929 | [0.9305, 1.0435] | -0.71% | no detectable difference |
| parse | 0.10 | 0.11 | +0.01 | 1.0270 | [0.9338, 1.2473] | +2.70% | no detectable difference |
| analysis | 1.92 | 1.99 | +0.07 | 1.0337 | [0.9833, 1.1288] | +3.37% | no detectable difference |
| ir | 0.15 | 0.15 | -0.00 | 1.0020 | [0.8406, 1.1268] | +0.20% | no detectable difference |
| codegen | 0.00 | 0.00 | +0.00 | NA | NA | NA | no ratio (a zero value) |
| object | 0.00 | 0.00 | +0.00 | NA | NA | NA | no ratio (a zero value) |
| emit | 1.28 | 1.33 | +0.05 | 0.9522 | [0.8494, 1.4297] | -4.78% | no detectable difference |
| total | 4.70 | 4.95 | +0.25 | 1.0191 | [0.9744, 1.1255] | +1.91% | no detectable difference |

## Checks

- order effect: not checked (too few pairs per subset); AB (A ran first) n=3 median 1.0124 CI NA, BA (B ran first) n=3 median 1.0190 CI NA
- drift: not checked (too few pairs per subset); first half n=3 median 1.0199 CI NA, second half n=3 median 1.0124 CI NA
- second run of a pair vs first: x0.9968 (ABBA cancels it in the overall ratio)
- per-tenth median B/A: 1.0157; per-tenth median A wall: 0.0310

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
