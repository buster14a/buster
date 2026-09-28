# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 16.539 | 15.545 | -5.73% | 47.65 / 47.23 | 326 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 13.933 | 13.982 | +0.43% | 47.62 / 47.57 | 358 | diagnostic (guard disabled) |
| tiny_startup/fast | 14.774 | 19.284 | +30.66% | 47.03 / 47.25 | 260 | diagnostic (guard disabled) |
| tiny_startup/quality | 13.393 | 14.036 | +4.83% | 47.67 / 47.26 | 357 | diagnostic (guard disabled) |
| large_function/none | 15.655 | 19.535 | +22.61% | 49.85 / 49.01 | 7052 | diagnostic (guard disabled) |
| large_function/mir-stack | 18.131 | 15.080 | -15.48% | 49.05 / 49.86 | 8826 | diagnostic (guard disabled) |
| large_function/fast | 16.171 | 15.473 | -4.36% | 49.19 / 49.27 | 8608 | diagnostic (guard disabled) |
| large_function/quality | 18.024 | 18.091 | +0.47% | 49.67 / 49.24 | 7675 | diagnostic (guard disabled) |
| many_functions/none | 18.740 | 18.197 | -4.47% | 49.62 / 49.93 | 14803 | diagnostic (guard disabled) |
| many_functions/mir-stack | 17.220 | 17.470 | +0.13% | 49.71 / 50.18 | 15481 | diagnostic (guard disabled) |
| many_functions/fast | 14.952 | 18.137 | +18.33% | 49.82 / 49.83 | 14899 | diagnostic (guard disabled) |
| many_functions/quality | 14.140 | 14.110 | -0.20% | 49.03 / 49.90 | 18214 | diagnostic (guard disabled) |
| symbol_table/none | 16.758 | 16.232 | -2.96% | 51.74 / 51.18 | 31859 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 14.605 | 15.438 | +5.71% | 51.63 / 51.75 | 33488 | diagnostic (guard disabled) |
| symbol_table/fast | 14.703 | 15.156 | +3.12% | 51.74 / 51.18 | 34115 | diagnostic (guard disabled) |
| symbol_table/quality | 19.201 | 19.245 | +0.36% | 51.19 / 51.18 | 28052 | diagnostic (guard disabled) |
| control_flow/none | 28.712 | 27.290 | -5.26% | 58.95 / 60.20 | 15944 | diagnostic (guard disabled) |
| control_flow/mir-stack | 23.325 | 23.229 | -0.21% | 59.35 / 59.87 | 18267 | diagnostic (guard disabled) |
| control_flow/fast | 24.113 | 27.501 | +16.72% | 57.70 / 59.90 | 15193 | diagnostic (guard disabled) |
| control_flow/quality | 24.825 | 25.377 | +2.32% | 57.78 / 59.35 | 17037 | diagnostic (guard disabled) |
| backend_pressure/none | 37.576 | 32.764 | -12.33% | 73.92 / 75.15 | 73323 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 26.165 | 25.832 | -1.25% | 73.45 / 74.81 | 93000 | diagnostic (guard disabled) |
| backend_pressure/fast | 29.096 | 26.716 | -7.85% | 73.91 / 74.53 | 89910 | diagnostic (guard disabled) |
| backend_pressure/quality | 31.233 | 29.797 | -4.38% | 74.41 / 74.41 | 80952 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
