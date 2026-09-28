# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 14.439 | 14.099 | -2.35% | 47.56 / 48.44 | 355 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 24.429 | 14.716 | -38.22% | 48.30 / 47.94 | 343 | diagnostic (guard disabled) |
| tiny_startup/fast | 13.953 | 14.100 | +1.04% | 47.53 / 48.32 | 355 | diagnostic (guard disabled) |
| tiny_startup/quality | 14.014 | 14.561 | +3.94% | 48.49 / 48.49 | 344 | diagnostic (guard disabled) |
| large_function/none | 22.888 | 16.378 | -25.13% | 50.02 / 49.70 | 8122 | diagnostic (guard disabled) |
| large_function/mir-stack | 14.878 | 15.130 | +1.73% | 49.49 / 49.74 | 8791 | diagnostic (guard disabled) |
| large_function/fast | 17.156 | 17.487 | +2.48% | 49.87 / 50.32 | 7753 | diagnostic (guard disabled) |
| large_function/quality | 17.550 | 18.416 | +4.55% | 50.35 / 49.71 | 7394 | diagnostic (guard disabled) |
| many_functions/none | 19.787 | 20.503 | +3.64% | 50.06 / 50.44 | 12536 | diagnostic (guard disabled) |
| many_functions/mir-stack | 14.368 | 15.245 | +6.02% | 50.37 / 49.51 | 16891 | diagnostic (guard disabled) |
| many_functions/fast | 14.566 | 14.485 | -0.57% | 50.24 / 50.18 | 17746 | diagnostic (guard disabled) |
| many_functions/quality | 17.235 | 15.639 | -8.76% | 49.55 / 49.71 | 16518 | diagnostic (guard disabled) |
| symbol_table/none | 22.017 | 16.952 | -20.74% | 51.98 / 52.22 | 30543 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 15.349 | 21.431 | +34.00% | 51.99 / 51.95 | 26192 | diagnostic (guard disabled) |
| symbol_table/fast | 15.367 | 15.341 | -0.18% | 51.74 / 51.43 | 33706 | diagnostic (guard disabled) |
| symbol_table/quality | 15.260 | 18.048 | +18.05% | 51.33 / 51.77 | 28753 | diagnostic (guard disabled) |
| control_flow/none | 24.626 | 24.803 | +0.94% | 60.69 / 59.83 | 17219 | diagnostic (guard disabled) |
| control_flow/mir-stack | 23.226 | 22.622 | -2.45% | 59.38 / 60.71 | 18949 | diagnostic (guard disabled) |
| control_flow/fast | 22.655 | 20.503 | -8.51% | 58.12 / 60.42 | 20471 | diagnostic (guard disabled) |
| control_flow/quality | 22.709 | 19.798 | -11.94% | 57.62 / 59.71 | 21135 | diagnostic (guard disabled) |
| backend_pressure/none | 30.105 | 29.701 | -1.35% | 74.94 / 75.36 | 80886 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 25.108 | 24.755 | -1.40% | 75.80 / 75.04 | 97032 | diagnostic (guard disabled) |
| backend_pressure/fast | 25.954 | 25.731 | -0.86% | 74.55 / 74.71 | 93360 | diagnostic (guard disabled) |
| backend_pressure/quality | 27.041 | 26.831 | -0.77% | 75.11 / 75.23 | 89528 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
