# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 17.570 | 17.820 | +0.58% | 23.36 / 23.31 | 281 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 20.351 | 19.722 | +0.52% | 23.25 / 23.27 | 254 | diagnostic (guard disabled) |
| tiny_startup/fast | 17.773 | 17.987 | +1.44% | 23.35 / 23.27 | 278 | diagnostic (guard disabled) |
| tiny_startup/quality | 18.611 | 18.500 | -3.48% | 23.38 / 23.29 | 270 | diagnostic (guard disabled) |
| large_function/none | 40.253 | 40.728 | -0.16% | 27.31 / 27.22 | 25265 | diagnostic (guard disabled) |
| large_function/mir-stack | 29.349 | 29.207 | +0.73% | 29.40 / 29.45 | 35249 | diagnostic (guard disabled) |
| large_function/fast | 29.952 | 29.166 | -2.29% | 29.38 / 29.44 | 35281 | diagnostic (guard disabled) |
| large_function/quality | 33.736 | 29.716 | -9.49% | 29.55 / 29.62 | 34632 | diagnostic (guard disabled) |
| many_functions/none | 29.159 | 29.291 | -0.05% | 29.92 / 29.97 | 69952 | diagnostic (guard disabled) |
| many_functions/mir-stack | 24.757 | 25.346 | +2.62% | 29.04 / 28.91 | 80844 | diagnostic (guard disabled) |
| many_functions/fast | 24.841 | 25.837 | +4.49% | 28.79 / 28.74 | 79323 | diagnostic (guard disabled) |
| many_functions/quality | 25.418 | 26.052 | +1.58% | 28.80 / 28.79 | 78666 | diagnostic (guard disabled) |
| symbol_table/none | 44.487 | 42.307 | -1.62% | 29.76 / 29.74 | 96938 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 28.835 | 29.930 | +1.44% | 30.53 / 30.60 | 137024 | diagnostic (guard disabled) |
| symbol_table/fast | 30.425 | 31.649 | +2.02% | 30.38 / 30.33 | 129616 | diagnostic (guard disabled) |
| symbol_table/quality | 31.462 | 30.497 | -0.97% | 30.43 / 30.46 | 134494 | diagnostic (guard disabled) |
| control_flow/none | 90.810 | 93.966 | -0.62% | 40.78 / 40.77 | 35432 | diagnostic (guard disabled) |
| control_flow/mir-stack | 78.004 | 71.138 | -13.26% | 39.54 / 39.46 | 46801 | diagnostic (guard disabled) |
| control_flow/fast | 63.119 | 61.105 | -1.79% | 38.72 / 38.72 | 54480 | diagnostic (guard disabled) |
| control_flow/quality | 60.450 | 61.141 | +4.31% | 38.75 / 38.71 | 54448 | diagnostic (guard disabled) |
| backend_pressure/none | 206.274 | 203.565 | -1.91% | 57.57 / 57.18 | 94331 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 153.070 | 149.160 | -6.15% | 57.33 / 57.30 | 128735 | diagnostic (guard disabled) |
| backend_pressure/fast | 153.557 | 151.231 | -3.61% | 55.18 / 55.05 | 126975 | diagnostic (guard disabled) |
| backend_pressure/quality | 166.514 | 175.919 | +7.16% | 55.42 / 55.32 | 109383 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
