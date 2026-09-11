# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 23.823 | 23.058 | -1.63% | 50.57 / 51.19 | 217 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 22.894 | 22.712 | -0.96% | 50.62 / 51.32 | 220 | diagnostic (guard disabled) |
| tiny_startup/fast | 21.745 | 20.991 | -2.60% | 50.52 / 51.27 | 238 | diagnostic (guard disabled) |
| tiny_startup/quality | 21.943 | 23.188 | -2.37% | 50.64 / 51.43 | 217 | diagnostic (guard disabled) |
| large_function/none | 27.935 | 24.966 | -2.96% | 52.46 / 53.23 | 5333 | diagnostic (guard disabled) |
| large_function/mir-stack | 23.240 | 23.294 | -1.12% | 52.64 / 53.28 | 5710 | diagnostic (guard disabled) |
| large_function/fast | 22.371 | 22.511 | -0.55% | 52.68 / 53.27 | 5909 | diagnostic (guard disabled) |
| large_function/quality | 23.556 | 24.566 | -0.93% | 52.77 / 53.35 | 5417 | diagnostic (guard disabled) |
| many_functions/none | 23.894 | 22.852 | -1.69% | 52.55 / 53.33 | 11250 | diagnostic (guard disabled) |
| many_functions/mir-stack | 24.065 | 23.799 | -1.61% | 52.46 / 53.16 | 10799 | diagnostic (guard disabled) |
| many_functions/fast | 23.368 | 23.350 | +0.33% | 52.72 / 53.42 | 11029 | diagnostic (guard disabled) |
| many_functions/quality | 23.275 | 22.825 | -0.88% | 52.60 / 53.37 | 11260 | diagnostic (guard disabled) |
| symbol_table/none | 25.522 | 25.609 | -1.77% | 54.56 / 55.32 | 20214 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 25.109 | 24.539 | -1.71% | 54.63 / 55.33 | 21070 | diagnostic (guard disabled) |
| symbol_table/fast | 24.602 | 24.223 | -0.64% | 54.67 / 55.40 | 21344 | diagnostic (guard disabled) |
| symbol_table/quality | 23.970 | 22.473 | -1.30% | 54.65 / 55.39 | 23017 | diagnostic (guard disabled) |
| control_flow/none | 32.117 | 30.896 | -2.07% | 62.68 / 63.31 | 13498 | diagnostic (guard disabled) |
| control_flow/mir-stack | 30.415 | 29.621 | -0.24% | 62.77 / 63.46 | 14081 | diagnostic (guard disabled) |
| control_flow/fast | 29.219 | 28.454 | -2.16% | 62.80 / 63.41 | 14655 | diagnostic (guard disabled) |
| control_flow/quality | 27.519 | 27.165 | -2.34% | 62.71 / 63.45 | 15380 | diagnostic (guard disabled) |
| backend_pressure/none | 50.370 | 48.454 | -0.53% | 78.79 / 79.29 | 49575 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 36.960 | 37.026 | -0.57% | 78.81 / 79.35 | 64982 | diagnostic (guard disabled) |
| backend_pressure/fast | 37.447 | 36.648 | -0.03% | 78.75 / 79.39 | 65587 | diagnostic (guard disabled) |
| backend_pressure/quality | 40.209 | 38.730 | -0.90% | 78.83 / 79.49 | 62044 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
