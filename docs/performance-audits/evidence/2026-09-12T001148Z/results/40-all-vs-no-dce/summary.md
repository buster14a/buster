# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.919 | 6.998 | +0.92% | 46.32 / 46.31 | 715 | no substantial regression detected |
| tiny_startup/mir-stack | 7.046 | 6.952 | -0.39% | 46.40 / 46.38 | 719 | no substantial regression detected |
| tiny_startup/fast | 7.042 | 6.830 | -0.35% | 46.48 / 46.56 | 732 | no substantial regression detected |
| tiny_startup/quality | 6.965 | 6.940 | +0.51% | 46.50 / 46.37 | 720 | no substantial regression detected |
| large_function/none | 15.840 | 16.137 | +2.11% | 70.34 / 70.41 | 63767 | no substantial regression detected |
| large_function/mir-stack | 11.812 | 11.866 | +0.73% | 72.32 / 72.32 | 86719 | no substantial regression detected |
| large_function/fast | 11.916 | 11.960 | +0.06% | 72.58 / 72.46 | 86037 | no substantial regression detected |
| large_function/quality | 12.014 | 12.037 | +0.59% | 72.47 / 72.72 | 85489 | no substantial regression detected |
| many_functions/none | 10.287 | 10.217 | -0.54% | 60.53 / 60.61 | 200543 | no substantial regression detected |
| many_functions/mir-stack | 9.822 | 9.896 | +0.37% | 58.50 / 58.43 | 207050 | no substantial regression detected |
| many_functions/fast | 9.977 | 9.963 | -0.67% | 58.58 / 58.42 | 205671 | no substantial regression detected |
| many_functions/quality | 9.990 | 9.961 | +0.35% | 58.61 / 58.65 | 205697 | no substantial regression detected |
| symbol_table/none | 16.680 | 16.654 | -0.42% | 78.81 / 78.55 | 246242 | no substantial regression detected |
| symbol_table/mir-stack | 12.134 | 12.132 | -0.14% | 78.74 / 78.72 | 338037 | no substantial regression detected |
| symbol_table/fast | 12.199 | 12.181 | +0.11% | 78.68 / 78.86 | 336667 | no substantial regression detected |
| symbol_table/quality | 12.240 | 12.197 | -0.31% | 78.96 / 78.90 | 336242 | no substantial regression detected |
| control_flow/none | 31.984 | 32.235 | +1.07% | 112.74 / 112.79 | 103273 | no substantial regression detected |
| control_flow/mir-stack | 28.766 | 29.083 | +1.19% | 110.74 / 110.57 | 114467 | no substantial regression detected |
| control_flow/fast | 26.189 | 26.343 | +0.55% | 108.62 / 110.75 | 126370 | no substantial regression detected |
| control_flow/quality | 26.118 | 26.313 | +0.74% | 108.68 / 110.49 | 126518 | no substantial regression detected |
| backend_pressure/none | 70.805 | 73.508 | +3.94% | 193.22 / 195.31 | 261222 | no substantial regression detected |
| backend_pressure/mir-stack | 60.148 | 60.914 | +1.13% | 191.22 / 193.17 | 315232 | no substantial regression detected |
| backend_pressure/fast | 61.469 | 62.301 | +1.41% | 191.10 / 191.12 | 308212 | no substantial regression detected |
| backend_pressure/quality | 67.035 | 68.616 | +2.35% | 191.35 / 191.07 | 279848 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
