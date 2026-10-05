# A/B compile-time comparison

**NO DETECTABLE DIFFERENCE: wall time B/A 1.0097, 95% CI [0.9436, 1.0243] includes 1.0 over 6 pairs; any real change is within about +/-5.64% (wider than the 0.5% practical floor: more pairs are needed to rule out an effect of that size). Detectable moves (CI excludes 1; they explain, they do not decide): page faults +0.79%.**

- baseline (A): `/home/david/Documents/Codex/2026-10-05/fi/work/perfbin/ide-base` sha256 `9b162962970223b1723c6cafed3c0d1c5574b43811e8f1e1bbdefec3caca2b1b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- candidate (B): `/home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/pr2483/build-perf/Release/ide` sha256 `8415bc9bb8253aa41d93e364b7ff44865fe0203c47599b7d08c25c6f7281fd3b`; 6 runs, 0 failed, deterministic output; -fmetrics-out support yes, enabled collection yes
- phase metrics collection: enabled; both compilers support -fmetrics-out
- outputs of A and B: byte-identical
- generated code bytes (executable sections, exact): A NA, B NA, B/A NA; baseline: unknown not an ELF, Mach-O or PE file; candidate: unknown not an ELF, Mach-O or PE file
- workload `IDE cc /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/string_offsets.c -c --target=wasm32-wasip1 -o OUT` in `/home/david/Documents/Codex/2026-10-05/fi/work/rootperf`, pinned to CPU 2; 6 complete pairs in ABBA order (--pairs 6)
- binary instances: a fresh copy per timed run and capture; practical floor 0.5% (--min-effect)
- machine-readable: `summary.json` (schema `buster-uarch-lab-compare-v2`); `python3 tools/uarch_lab.py report /home/david/Documents/Codex/2026-10-05/fi/work/foundation_ready/2483-wasm32-uarch` re-renders both files

## Warnings

- baseline: peak RSS NA in 6 of 6 runs (not above 1.5x the harness/perf floor of 32,768,000 bytes, or not measured)
- candidate: peak RSS NA in 6 of 6 runs (not above 1.5x the harness/perf floor of 32,772,096 bytes, or not measured)

## Metrics (B/A per pair)

`B/A` is the median of the per-pair ratios; its 95% CI uses sign-test order statistics (distribution-free; coverage 0.9688). The geometric mean has a seeded bootstrap CI (seed 20261003, 2000 resamples). The verdict uses wall time only.
Outcomes need the whole CI beyond the 0.5% practical floor; `below-floor` means the CI excludes 1.0 but reaches inside it. instructions:u is exact (no floor).

| metric | unit | A median | B median | B/A | 95% CI | change | geomean B/A | bootstrap 95% CI | min B / min A | outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| wall time (harness span) | s | 0.0308 | 0.0306 | 1.0097 | [0.9436, 1.0243] | +0.97% | 0.9961 | [0.9724, 1.0170] | 1.0118 | no detectable difference |
| task-clock | s | 0.0088 | 0.0090 | 1.0109 | [0.9665, 1.0333] | +1.09% | 1.0066 | [0.9872, 1.0254] | 0.9829 | no detectable difference |
| compiler wall_ns | s | 0.0064 | 0.0065 | 1.0255 | [0.9548, 1.0743] | +2.55% | 1.0156 | [0.9830, 1.0492] | 1.0064 | no detectable difference |
| instructions:u | count | 2,180,308 | 2,180,332 | 1.0000 | [1.0000, 1.0000] | +0.00% | 1.0000 | [1.0000, 1.0000] | 1.0000 | no detectable difference |
| cycles:u | count | 4,956,907 | 4,947,600 | 0.9967 | [0.9525, 1.0258] | -0.33% | 0.9928 | [0.9733, 1.0089] | 0.9974 | no detectable difference |
| IPC | ratio | 0.4399 | 0.4407 | 1.0034 | [0.9749, 1.0499] | +0.34% | 1.0073 | [0.9912, 1.0274] | 1.0358 | no detectable difference |
| branch-misses:u | count | 15,132 | 15,532 | 1.0298 | [0.9925, 1.0692] | +2.98% | 1.0295 | [1.0097, 1.0491] | 0.9954 | no detectable difference |
| branch MPKI | per_1k_instr | 6.9403 | 7.1237 | 1.0298 | [0.9925, 1.0692] | +2.98% | 1.0295 | [1.0097, 1.0491] | 0.9954 | no detectable difference |
| page faults | count | 567 | 572 | 1.0079 | [1.0071, 1.0088] | +0.79% | 1.0079 | [1.0073, 1.0085] | 1.0071 | higher |
| minor faults | count | 98 | 98 | 1.0051 | [1.0000, 1.0102] | +0.51% | 1.0051 | [1.0017, 1.0085] | 1.0000 | no detectable difference |
| major faults | count | 469 | 473 | 1.0085 | [1.0085, 1.0085] | +0.85% | 1.0085 | [1.0085, 1.0085] | 1.0085 | higher |
| peak RSS (wait4 ru_maxrss) | bytes | NA | NA | NA | NA | NA | NA | NA | NA | no data |

## Phases (-fmetrics-out, median ms)

| phase | A ms | B ms | delta ms | B/A | 95% CI | change | outcome |
|---|---|---|---|---|---|---|---|
| read | 0.04 | 0.05 | +0.01 | 1.3073 | [0.8238, 1.7617] | +30.73% | no detectable difference |
| preprocess | 1.23 | 1.18 | -0.05 | 1.0050 | [0.8760, 1.1205] | +0.50% | no detectable difference |
| parse | 0.10 | 0.10 | +0.01 | 1.0622 | [0.9977, 1.2279] | +6.22% | no detectable difference |
| analysis | 1.94 | 1.93 | -0.01 | 0.9957 | [0.9324, 1.0317] | -0.43% | no detectable difference |
| ir | 0.15 | 0.15 | +0.00 | 1.0258 | [0.9135, 1.0659] | +2.58% | no detectable difference |
| codegen | 0.00 | 0.00 | +0.00 | NA | NA | NA | no ratio (a zero value) |
| object | 0.00 | 0.00 | +0.00 | NA | NA | NA | no ratio (a zero value) |
| emit | 1.37 | 1.46 | +0.09 | 1.0654 | [0.9008, 1.3964] | +6.54% | no detectable difference |
| total | 4.82 | 4.95 | +0.14 | 1.0459 | [0.9424, 1.1146] | +4.59% | no detectable difference |

## Checks

- order effect: not checked (too few pairs per subset); AB (A ran first) n=3 median 1.0152 CI NA, BA (B ran first) n=3 median 0.9744 CI NA
- drift: not checked (too few pairs per subset); first half n=3 median 1.0152 CI NA, second half n=3 median 1.0042 CI NA
- second run of a pair vs first: x1.0207 (ABBA cancels it in the overall ratio)
- per-tenth median B/A: 1.0097; per-tenth median A wall: 0.0308

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
