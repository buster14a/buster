# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.542 | 19.491 | +0.29% | 23.37 / 23.39 | 257 | inconclusive |
| tiny_startup/mir-stack | 18.955 | 18.993 | -1.49% | 23.35 / 23.38 | 263 | inconclusive |
| tiny_startup/fast | 18.927 | 19.213 | +0.95% | 23.51 / 23.39 | 260 | inconclusive |
| tiny_startup/quality | 18.750 | 18.439 | -1.29% | 23.47 / 23.36 | 271 | inconclusive |
| large_function/none | 43.818 | 41.582 | -2.13% | 27.42 / 27.59 | 24746 | inconclusive |
| large_function/mir-stack | 29.872 | 29.937 | +1.13% | 29.52 / 29.32 | 34372 | inconclusive |
| large_function/fast | 30.238 | 30.468 | +0.52% | 29.45 / 29.31 | 33773 | inconclusive |
| large_function/quality | 30.445 | 31.313 | +4.12% | 29.69 / 29.50 | 32862 | inconclusive |
| many_functions/none | 28.747 | 29.409 | +0.69% | 30.07 / 30.04 | 69671 | no substantial regression detected |
| many_functions/mir-stack | 25.950 | 25.812 | -1.20% | 29.05 / 28.98 | 79382 | inconclusive |
| many_functions/fast | 25.363 | 26.327 | +1.26% | 28.94 / 28.92 | 77831 | inconclusive |
| many_functions/quality | 26.551 | 26.161 | +1.72% | 28.95 / 29.09 | 78323 | inconclusive |
| symbol_table/none | 43.076 | 42.015 | -1.65% | 29.77 / 29.79 | 97608 | no substantial regression detected |
| symbol_table/mir-stack | 30.387 | 29.694 | -1.90% | 30.66 / 30.78 | 138108 | inconclusive |
| symbol_table/fast | 29.073 | 30.388 | +3.14% | 30.48 / 30.38 | 134956 | inconclusive |
| symbol_table/quality | 30.138 | 30.297 | +0.23% | 30.55 / 30.52 | 135361 | inconclusive |
| control_flow/none | 95.804 | 95.071 | +1.98% | 40.85 / 40.95 | 35017 | inconclusive |
| control_flow/mir-stack | 73.577 | 73.046 | +1.61% | 39.61 / 39.70 | 45574 | inconclusive |
| control_flow/fast | 62.858 | 66.396 | +7.38% | 38.87 / 39.00 | 50138 | inconclusive |
| control_flow/quality | 63.603 | 65.206 | +4.79% | 38.91 / 39.01 | 51056 | inconclusive |
| backend_pressure/none | 205.347 | 206.137 | +2.30% | 57.49 / 57.47 | 93152 | inconclusive |
| backend_pressure/mir-stack | 151.194 | 158.689 | +2.31% | 57.61 / 57.18 | 121005 | inconclusive |
| backend_pressure/fast | 148.464 | 159.623 | +6.63% | 55.46 / 55.82 | 120299 | inconclusive |
| backend_pressure/quality | 164.166 | 168.685 | +1.12% | 55.59 / 56.09 | 113835 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **22**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
