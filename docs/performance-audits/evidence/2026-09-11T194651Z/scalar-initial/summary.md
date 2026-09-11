# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 20.326 | 21.670 | +4.29% | 23.47 / 23.48 | 231 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 21.426 | 21.119 | +0.22% | 23.29 / 23.29 | 237 | diagnostic (guard disabled) |
| tiny_startup/fast | 21.676 | 21.174 | +2.85% | 23.29 / 23.31 | 236 | diagnostic (guard disabled) |
| tiny_startup/quality | 20.069 | 20.016 | -2.08% | 23.28 / 23.32 | 250 | diagnostic (guard disabled) |
| large_function/none | 23.330 | 24.415 | +6.48% | 24.13 / 24.23 | 5448 | diagnostic (guard disabled) |
| large_function/mir-stack | 23.444 | 22.913 | -2.60% | 24.16 / 24.13 | 5811 | diagnostic (guard disabled) |
| large_function/fast | 22.345 | 23.759 | +5.21% | 24.09 / 24.18 | 5600 | diagnostic (guard disabled) |
| large_function/quality | 22.200 | 22.657 | +2.88% | 24.20 / 24.33 | 5870 | diagnostic (guard disabled) |
| many_functions/none | 24.713 | 23.814 | -2.35% | 24.35 / 24.41 | 10793 | diagnostic (guard disabled) |
| many_functions/mir-stack | 23.752 | 22.335 | -6.32% | 24.13 / 24.33 | 11507 | diagnostic (guard disabled) |
| many_functions/fast | 21.265 | 21.903 | -2.09% | 24.20 / 24.38 | 11734 | diagnostic (guard disabled) |
| many_functions/quality | 21.157 | 20.766 | -3.17% | 24.14 / 24.34 | 12383 | diagnostic (guard disabled) |
| symbol_table/none | 26.589 | 23.934 | -9.28% | 24.41 / 24.46 | 21607 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 21.084 | 21.549 | +0.18% | 24.36 / 24.34 | 23992 | diagnostic (guard disabled) |
| symbol_table/fast | 22.438 | 22.278 | +3.42% | 24.28 / 24.39 | 23217 | diagnostic (guard disabled) |
| symbol_table/quality | 23.777 | 22.130 | -1.51% | 24.42 / 24.38 | 23364 | diagnostic (guard disabled) |
| control_flow/none | 34.521 | 30.798 | -6.65% | 25.98 / 26.11 | 13540 | diagnostic (guard disabled) |
| control_flow/mir-stack | 27.715 | 28.309 | +3.46% | 25.78 / 25.81 | 14733 | diagnostic (guard disabled) |
| control_flow/fast | 26.052 | 26.466 | -4.14% | 25.78 / 25.79 | 15761 | diagnostic (guard disabled) |
| control_flow/quality | 26.324 | 26.170 | +2.54% | 25.79 / 25.88 | 15935 | diagnostic (guard disabled) |
| backend_pressure/none | 47.518 | 47.484 | -0.96% | 28.10 / 28.34 | 50628 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 38.633 | 38.837 | +0.39% | 28.34 / 28.47 | 61904 | diagnostic (guard disabled) |
| backend_pressure/fast | 39.472 | 37.650 | -8.01% | 28.10 / 28.18 | 63805 | diagnostic (guard disabled) |
| backend_pressure/quality | 41.756 | 45.778 | +5.91% | 28.39 / 28.59 | 52482 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
