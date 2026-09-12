# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.114 | 7.062 | +1.95% | 46.54 / 46.45 | 708 | no substantial regression detected |
| tiny_startup/mir-stack | 6.981 | 7.115 | +0.90% | 46.32 / 46.53 | 703 | no substantial regression detected |
| tiny_startup/fast | 7.087 | 7.151 | +1.37% | 46.41 / 46.28 | 699 | no substantial regression detected |
| tiny_startup/quality | 7.005 | 7.253 | +2.11% | 46.38 / 46.47 | 689 | no substantial regression detected |
| large_function/none | 16.212 | 16.129 | -0.12% | 70.54 / 70.69 | 63799 | no substantial regression detected |
| large_function/mir-stack | 12.189 | 12.156 | -0.15% | 72.41 / 72.43 | 84647 | no substantial regression detected |
| large_function/fast | 12.233 | 12.301 | +0.22% | 72.34 / 72.33 | 83650 | no substantial regression detected |
| large_function/quality | 12.360 | 12.382 | -0.22% | 72.58 / 72.45 | 83105 | no substantial regression detected |
| many_functions/none | 10.632 | 10.538 | -0.73% | 60.59 / 60.45 | 194440 | no substantial regression detected |
| many_functions/mir-stack | 10.190 | 10.155 | -0.19% | 58.43 / 58.55 | 201771 | no substantial regression detected |
| many_functions/fast | 10.270 | 10.227 | -0.38% | 58.43 / 58.52 | 200348 | no substantial regression detected |
| many_functions/quality | 10.297 | 10.314 | -0.17% | 58.58 / 58.45 | 198658 | no substantial regression detected |
| symbol_table/none | 16.977 | 16.975 | -0.31% | 78.77 / 78.79 | 241590 | no substantial regression detected |
| symbol_table/mir-stack | 12.524 | 12.557 | -0.10% | 78.81 / 78.61 | 326604 | no substantial regression detected |
| symbol_table/fast | 12.541 | 12.529 | -0.22% | 78.63 / 78.72 | 327325 | no substantial regression detected |
| symbol_table/quality | 12.623 | 12.638 | +0.10% | 78.82 / 78.87 | 324502 | no substantial regression detected |
| control_flow/none | 32.652 | 32.542 | -0.23% | 112.71 / 113.13 | 102299 | no substantial regression detected |
| control_flow/mir-stack | 29.394 | 29.367 | -0.15% | 110.89 / 110.49 | 113357 | no substantial regression detected |
| control_flow/fast | 27.030 | 27.010 | -0.15% | 108.76 / 108.67 | 123249 | no substantial regression detected |
| control_flow/quality | 26.999 | 26.994 | -0.02% | 108.76 / 108.81 | 123324 | no substantial regression detected |
| backend_pressure/none | 72.187 | 72.088 | -0.14% | 193.16 / 193.34 | 266367 | no substantial regression detected |
| backend_pressure/mir-stack | 61.592 | 61.681 | +0.20% | 191.15 / 191.15 | 311309 | no substantial regression detected |
| backend_pressure/fast | 62.998 | 62.753 | -0.34% | 191.16 / 191.26 | 305991 | no substantial regression detected |
| backend_pressure/quality | 68.553 | 68.590 | -0.07% | 191.13 / 191.25 | 279953 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
