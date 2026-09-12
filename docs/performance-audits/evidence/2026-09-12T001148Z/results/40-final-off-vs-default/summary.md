# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.973 | 7.105 | -0.02% | 46.24 / 46.15 | 704 | no substantial regression detected |
| tiny_startup/mir-stack | 7.022 | 7.062 | -0.51% | 46.43 / 46.27 | 708 | no substantial regression detected |
| tiny_startup/fast | 7.065 | 7.157 | +0.53% | 46.29 / 46.29 | 699 | no substantial regression detected |
| tiny_startup/quality | 7.172 | 7.157 | -0.87% | 46.42 / 46.56 | 699 | no substantial regression detected |
| large_function/none | 16.286 | 16.099 | -0.97% | 70.52 / 70.35 | 63918 | no substantial regression detected |
| large_function/mir-stack | 11.881 | 12.018 | +1.66% | 72.60 / 72.39 | 85623 | no substantial regression detected |
| large_function/fast | 11.934 | 12.099 | +1.02% | 72.38 / 72.40 | 85047 | no substantial regression detected |
| large_function/quality | 11.978 | 12.245 | +1.77% | 72.48 / 72.57 | 84033 | no substantial regression detected |
| many_functions/none | 10.417 | 10.501 | +0.82% | 60.47 / 60.48 | 195120 | no substantial regression detected |
| many_functions/mir-stack | 9.909 | 10.041 | +1.29% | 58.49 / 58.45 | 204054 | no substantial regression detected |
| many_functions/fast | 10.063 | 10.138 | +0.94% | 58.60 / 58.61 | 202108 | no substantial regression detected |
| many_functions/quality | 10.100 | 10.208 | +0.64% | 58.50 / 58.56 | 200718 | no substantial regression detected |
| symbol_table/none | 16.786 | 16.918 | +0.83% | 78.53 / 78.62 | 242410 | no substantial regression detected |
| symbol_table/mir-stack | 12.235 | 12.366 | +0.99% | 78.60 / 78.54 | 331645 | no substantial regression detected |
| symbol_table/fast | 12.295 | 12.356 | +0.66% | 78.60 / 78.74 | 331905 | no substantial regression detected |
| symbol_table/quality | 12.400 | 12.410 | +0.28% | 78.82 / 78.71 | 330461 | no substantial regression detected |
| control_flow/none | 31.738 | 32.512 | +2.48% | 112.83 / 112.79 | 102394 | no substantial regression detected |
| control_flow/mir-stack | 29.106 | 29.495 | +1.42% | 110.66 / 110.86 | 112867 | no substantial regression detected |
| control_flow/fast | 25.988 | 26.740 | +3.14% | 108.81 / 108.65 | 124497 | no substantial regression detected |
| control_flow/quality | 25.889 | 26.809 | +3.60% | 108.76 / 108.85 | 124175 | no substantial regression detected |
| backend_pressure/none | 72.539 | 72.053 | -0.58% | 197.19 / 193.23 | 266500 | no substantial regression detected |
| backend_pressure/mir-stack | 60.034 | 61.510 | +2.52% | 191.09 / 191.20 | 312175 | no substantial regression detected |
| backend_pressure/fast | 61.170 | 62.796 | +2.70% | 189.19 / 191.19 | 305784 | no substantial regression detected |
| backend_pressure/quality | 67.680 | 68.507 | +1.22% | 189.29 / 191.35 | 280291 | no substantial regression detected |
| self_host_stage1/fast | 934.500 | 989.246 | +5.81% | 1776.84 / 1772.93 | 381474 | no substantial regression detected |
| self_host_stage2/fast | 4587.931 | 4438.528 | -3.38% | 1789.45 / 1788.49 | 84832 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
