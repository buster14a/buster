# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.010 | 7.119 | +1.43% | 46.57 / 46.31 | 702 | no substantial regression detected |
| tiny_startup/mir-stack | 6.831 | 6.950 | +1.92% | 46.27 / 46.41 | 719 | no substantial regression detected |
| tiny_startup/fast | 6.845 | 6.993 | +0.89% | 46.56 / 46.28 | 715 | no substantial regression detected |
| tiny_startup/quality | 6.985 | 6.949 | +0.61% | 46.63 / 46.39 | 720 | no substantial regression detected |
| large_function/none | 15.878 | 15.896 | -0.42% | 70.42 / 70.42 | 64732 | no substantial regression detected |
| large_function/mir-stack | 11.789 | 11.753 | -0.61% | 72.43 / 72.40 | 87549 | no substantial regression detected |
| large_function/fast | 11.952 | 11.896 | -0.20% | 72.41 / 72.47 | 86497 | no substantial regression detected |
| large_function/quality | 11.972 | 11.990 | +0.33% | 72.77 / 72.55 | 85820 | no substantial regression detected |
| many_functions/none | 10.294 | 10.268 | -0.73% | 60.67 / 60.50 | 199556 | no substantial regression detected |
| many_functions/mir-stack | 9.853 | 9.878 | +0.51% | 58.44 / 58.31 | 207430 | no substantial regression detected |
| many_functions/fast | 9.944 | 9.886 | -0.61% | 58.46 / 58.58 | 207257 | no substantial regression detected |
| many_functions/quality | 10.016 | 9.957 | -0.24% | 58.51 / 58.79 | 205778 | no substantial regression detected |
| symbol_table/none | 16.772 | 16.733 | -0.13% | 78.73 / 78.61 | 245085 | no substantial regression detected |
| symbol_table/mir-stack | 12.124 | 12.224 | +0.55% | 78.66 / 78.74 | 335483 | no substantial regression detected |
| symbol_table/fast | 12.196 | 12.184 | -0.03% | 78.80 / 78.58 | 336577 | no substantial regression detected |
| symbol_table/quality | 12.255 | 12.284 | +0.32% | 78.73 / 78.83 | 333857 | no substantial regression detected |
| control_flow/none | 32.027 | 31.910 | -0.16% | 112.88 / 112.91 | 104325 | no substantial regression detected |
| control_flow/mir-stack | 28.751 | 28.700 | -0.31% | 110.79 / 110.55 | 115995 | no substantial regression detected |
| control_flow/fast | 26.130 | 26.139 | -0.25% | 108.66 / 108.87 | 127358 | no substantial regression detected |
| control_flow/quality | 26.141 | 26.094 | -0.13% | 109.01 / 108.90 | 127576 | no substantial regression detected |
| backend_pressure/none | 70.884 | 70.825 | +0.17% | 193.02 / 193.31 | 271120 | no substantial regression detected |
| backend_pressure/mir-stack | 60.212 | 60.305 | +0.23% | 191.13 / 191.10 | 318413 | no substantial regression detected |
| backend_pressure/fast | 61.471 | 61.543 | +0.17% | 191.25 / 191.24 | 312009 | no substantial regression detected |
| backend_pressure/quality | 67.186 | 67.090 | -0.07% | 191.32 / 191.34 | 286212 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
