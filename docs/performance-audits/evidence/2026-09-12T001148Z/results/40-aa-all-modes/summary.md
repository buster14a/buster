# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.201 | 7.180 | +1.14% | 46.32 / 46.30 | 696 | no substantial regression detected |
| tiny_startup/mir-stack | 7.010 | 7.134 | +0.37% | 46.37 / 46.20 | 701 | no substantial regression detected |
| tiny_startup/fast | 7.067 | 7.104 | +1.31% | 46.31 / 46.29 | 704 | no substantial regression detected |
| tiny_startup/quality | 7.224 | 7.175 | -2.68% | 46.52 / 46.39 | 697 | no substantial regression detected |
| large_function/none | 16.155 | 16.173 | +0.11% | 70.47 / 70.46 | 63625 | no substantial regression detected |
| large_function/mir-stack | 11.853 | 11.862 | +0.61% | 72.45 / 72.39 | 86746 | no substantial regression detected |
| large_function/fast | 11.994 | 11.965 | +0.45% | 72.65 / 72.50 | 86004 | no substantial regression detected |
| large_function/quality | 12.115 | 12.037 | -0.32% | 72.46 / 72.79 | 85484 | no substantial regression detected |
| many_functions/none | 10.475 | 10.434 | +0.03% | 60.63 / 60.61 | 196377 | no substantial regression detected |
| many_functions/mir-stack | 9.928 | 9.934 | -0.00% | 58.56 / 58.27 | 206257 | no substantial regression detected |
| many_functions/fast | 10.020 | 10.071 | +0.24% | 58.36 / 58.40 | 203464 | no substantial regression detected |
| many_functions/quality | 10.127 | 10.130 | +0.09% | 58.53 / 58.60 | 202279 | no substantial regression detected |
| symbol_table/none | 16.801 | 16.808 | +0.04% | 78.79 / 78.72 | 243985 | no substantial regression detected |
| symbol_table/mir-stack | 12.259 | 12.288 | -0.55% | 78.67 / 78.68 | 333752 | no substantial regression detected |
| symbol_table/fast | 12.361 | 12.362 | -0.01% | 78.74 / 78.69 | 331739 | no substantial regression detected |
| symbol_table/quality | 12.388 | 12.395 | +0.02% | 78.89 / 78.89 | 330853 | no substantial regression detected |
| control_flow/none | 31.987 | 31.974 | +0.10% | 112.80 / 112.74 | 104117 | no substantial regression detected |
| control_flow/mir-stack | 28.968 | 29.012 | +0.16% | 110.69 / 110.74 | 114744 | no substantial regression detected |
| control_flow/fast | 25.980 | 25.933 | -0.14% | 108.76 / 108.68 | 128370 | no substantial regression detected |
| control_flow/quality | 25.978 | 25.967 | -0.02% | 108.91 / 109.06 | 128203 | no substantial regression detected |
| backend_pressure/none | 72.236 | 72.142 | -0.19% | 197.13 / 197.21 | 266169 | no substantial regression detected |
| backend_pressure/mir-stack | 59.932 | 59.907 | -0.04% | 191.07 / 191.25 | 320530 | no substantial regression detected |
| backend_pressure/fast | 60.964 | 60.974 | +0.03% | 189.28 / 189.21 | 314920 | no substantial regression detected |
| backend_pressure/quality | 67.548 | 67.527 | -0.06% | 189.32 / 189.28 | 284360 | no substantial regression detected |
| self_host_stage1/fast | 934.187 | 933.931 | -0.01% | 1778.98 / 1778.96 | 404043 | no substantial regression detected |
| self_host_stage2/fast | 4268.602 | 4273.782 | +0.08% | 1791.51 / 1791.51 | 88096 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
