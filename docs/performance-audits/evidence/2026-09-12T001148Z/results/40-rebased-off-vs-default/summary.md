# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.198 | 7.174 | -1.79% | 46.52 / 46.24 | 697 | no substantial regression detected |
| tiny_startup/mir-stack | 7.119 | 7.147 | +0.44% | 46.23 / 46.45 | 700 | no substantial regression detected |
| tiny_startup/fast | 7.138 | 7.160 | -2.08% | 46.35 / 46.43 | 698 | no substantial regression detected |
| tiny_startup/quality | 7.222 | 7.183 | -1.45% | 46.51 / 46.59 | 696 | no substantial regression detected |
| large_function/none | 16.211 | 16.119 | -0.47% | 70.33 / 70.39 | 63836 | no substantial regression detected |
| large_function/mir-stack | 11.912 | 12.081 | +1.92% | 72.70 / 72.48 | 85173 | no substantial regression detected |
| large_function/fast | 12.021 | 12.244 | +1.48% | 72.72 / 72.53 | 84040 | no substantial regression detected |
| large_function/quality | 12.110 | 12.309 | +1.19% | 72.77 / 72.43 | 83595 | no substantial regression detected |
| many_functions/none | 10.403 | 10.524 | +0.30% | 60.45 / 60.43 | 194703 | no substantial regression detected |
| many_functions/mir-stack | 9.984 | 10.094 | +0.78% | 58.59 / 58.32 | 202985 | no substantial regression detected |
| many_functions/fast | 10.151 | 10.159 | +0.68% | 58.41 / 58.38 | 201698 | no substantial regression detected |
| many_functions/quality | 10.180 | 10.245 | +0.46% | 58.66 / 58.60 | 199992 | no substantial regression detected |
| symbol_table/none | 16.807 | 16.862 | +0.56% | 78.68 / 78.78 | 243213 | no substantial regression detected |
| symbol_table/mir-stack | 12.345 | 12.448 | +1.04% | 78.66 / 78.70 | 329457 | no substantial regression detected |
| symbol_table/fast | 12.350 | 12.475 | +1.10% | 78.82 / 78.64 | 328737 | no substantial regression detected |
| symbol_table/quality | 12.511 | 12.592 | +0.74% | 78.78 / 78.87 | 325673 | no substantial regression detected |
| control_flow/none | 31.873 | 32.556 | +2.13% | 112.71 / 112.61 | 102254 | no substantial regression detected |
| control_flow/mir-stack | 28.811 | 29.307 | +1.74% | 110.82 / 110.66 | 113589 | no substantial regression detected |
| control_flow/fast | 26.115 | 26.942 | +3.17% | 108.72 / 108.64 | 123561 | no substantial regression detected |
| control_flow/quality | 26.060 | 26.903 | +3.37% | 108.91 / 108.75 | 123739 | no substantial regression detected |
| backend_pressure/none | 72.244 | 71.859 | -0.53% | 197.14 / 193.30 | 267216 | no substantial regression detected |
| backend_pressure/mir-stack | 59.961 | 61.463 | +2.59% | 190.99 / 191.06 | 312416 | no substantial regression detected |
| backend_pressure/fast | 61.071 | 62.784 | +2.81% | 189.21 / 191.12 | 305840 | no substantial regression detected |
| backend_pressure/quality | 67.446 | 68.360 | +1.33% | 189.58 / 191.30 | 280896 | no substantial regression detected |
| self_host_stage1/fast | 937.289 | 991.813 | +5.80% | 1776.87 / 1768.89 | 380638 | no substantial regression detected |
| self_host_stage2/fast | 4603.497 | 4446.145 | -3.37% | 1793.46 / 1792.62 | 84720 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
