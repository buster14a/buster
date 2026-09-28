# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.297 | 19.619 | +1.71% | 48.04 / 48.08 | 255 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 19.885 | 20.537 | +3.27% | 47.84 / 47.75 | 244 | diagnostic (guard disabled) |
| tiny_startup/fast | 27.028 | 27.725 | +2.58% | 47.32 / 47.36 | 180 | diagnostic (guard disabled) |
| tiny_startup/quality | 21.157 | 19.697 | -6.56% | 47.53 / 47.45 | 254 | diagnostic (guard disabled) |
| large_function/none | 21.734 | 26.359 | +19.36% | 50.57 / 50.04 | 5209 | diagnostic (guard disabled) |
| large_function/mir-stack | 20.837 | 23.130 | +10.83% | 49.46 / 50.01 | 5769 | diagnostic (guard disabled) |
| large_function/fast | 21.384 | 21.619 | +1.08% | 49.46 / 50.22 | 6155 | diagnostic (guard disabled) |
| large_function/quality | 20.574 | 20.810 | +1.10% | 49.41 / 50.54 | 6397 | diagnostic (guard disabled) |
| many_functions/none | 20.097 | 20.610 | +2.55% | 50.52 / 50.13 | 12471 | diagnostic (guard disabled) |
| many_functions/mir-stack | 24.577 | 24.112 | -1.81% | 50.11 / 49.88 | 11052 | diagnostic (guard disabled) |
| many_functions/fast | 24.259 | 24.179 | +0.14% | 50.17 / 49.88 | 10920 | diagnostic (guard disabled) |
| many_functions/quality | 20.587 | 21.079 | +2.37% | 49.57 / 49.58 | 12196 | diagnostic (guard disabled) |
| symbol_table/none | 24.108 | 22.545 | -6.28% | 51.98 / 52.66 | 22936 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 23.281 | 21.533 | -7.10% | 52.13 / 52.32 | 24010 | diagnostic (guard disabled) |
| symbol_table/fast | 26.391 | 29.707 | +13.74% | 51.49 / 52.82 | 17404 | diagnostic (guard disabled) |
| symbol_table/quality | 21.466 | 21.730 | +1.23% | 51.71 / 51.69 | 23792 | diagnostic (guard disabled) |
| control_flow/none | 37.542 | 39.032 | +4.50% | 60.27 / 60.20 | 10686 | diagnostic (guard disabled) |
| control_flow/mir-stack | 36.204 | 29.152 | -16.82% | 59.50 / 59.29 | 14499 | diagnostic (guard disabled) |
| control_flow/fast | 36.390 | 36.484 | +0.25% | 57.90 / 58.61 | 11432 | diagnostic (guard disabled) |
| control_flow/quality | 25.993 | 29.945 | +13.34% | 57.70 / 57.98 | 14387 | diagnostic (guard disabled) |
| backend_pressure/none | 50.157 | 49.330 | -1.59% | 74.64 / 74.47 | 50562 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 33.822 | 33.660 | -0.48% | 74.96 / 74.88 | 71376 | diagnostic (guard disabled) |
| backend_pressure/fast | 52.696 | 45.211 | -15.59% | 74.14 / 74.42 | 54912 | diagnostic (guard disabled) |
| backend_pressure/quality | 37.382 | 37.219 | -0.43% | 74.76 / 74.15 | 64538 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
