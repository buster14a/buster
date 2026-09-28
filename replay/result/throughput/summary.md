# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 12.583 | 13.187 | +4.74% | 48.33 / 47.38 | 380 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 12.659 | 13.110 | +3.56% | 47.03 / 47.20 | 381 | diagnostic (guard disabled) |
| tiny_startup/fast | 14.263 | 13.563 | -4.22% | 48.02 / 46.95 | 371 | diagnostic (guard disabled) |
| tiny_startup/quality | 12.884 | 12.652 | -1.80% | 47.02 / 47.05 | 395 | diagnostic (guard disabled) |
| large_function/none | 14.560 | 16.807 | +14.59% | 49.09 / 49.57 | 8040 | diagnostic (guard disabled) |
| large_function/mir-stack | 15.432 | 15.091 | -2.19% | 49.10 / 49.55 | 8938 | diagnostic (guard disabled) |
| large_function/fast | 13.612 | 13.960 | +2.48% | 48.85 / 49.64 | 9545 | diagnostic (guard disabled) |
| large_function/quality | 15.893 | 16.699 | +5.22% | 49.16 / 49.30 | 7987 | diagnostic (guard disabled) |
| many_functions/none | 13.546 | 13.218 | -2.44% | 49.18 / 49.71 | 19451 | diagnostic (guard disabled) |
| many_functions/mir-stack | 13.525 | 13.520 | -0.04% | 49.31 / 48.84 | 19013 | diagnostic (guard disabled) |
| many_functions/fast | 13.689 | 13.539 | -1.12% | 48.93 / 49.63 | 18993 | diagnostic (guard disabled) |
| many_functions/quality | 15.378 | 15.024 | -2.77% | 49.24 / 49.50 | 17368 | diagnostic (guard disabled) |
| symbol_table/none | 14.918 | 14.333 | -3.92% | 51.69 / 51.12 | 36081 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 13.897 | 14.273 | +2.70% | 51.14 / 51.73 | 36233 | diagnostic (guard disabled) |
| symbol_table/fast | 14.888 | 13.862 | -6.78% | 51.06 / 52.02 | 37343 | diagnostic (guard disabled) |
| symbol_table/quality | 14.096 | 17.711 | +23.70% | 50.88 / 50.97 | 30117 | diagnostic (guard disabled) |
| control_flow/none | 19.386 | 19.875 | +1.98% | 59.47 / 60.48 | 21266 | diagnostic (guard disabled) |
| control_flow/mir-stack | 17.067 | 16.979 | -0.52% | 59.79 / 59.87 | 24561 | diagnostic (guard disabled) |
| control_flow/fast | 18.750 | 19.264 | +2.25% | 57.29 / 59.06 | 22202 | diagnostic (guard disabled) |
| control_flow/quality | 15.900 | 16.472 | +3.60% | 57.57 / 59.73 | 25315 | diagnostic (guard disabled) |
| backend_pressure/none | 25.793 | 25.698 | -0.36% | 74.06 / 74.33 | 93473 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 22.210 | 21.729 | -2.08% | 74.69 / 74.31 | 110576 | diagnostic (guard disabled) |
| backend_pressure/fast | 23.644 | 21.812 | -7.61% | 74.10 / 73.83 | 110124 | diagnostic (guard disabled) |
| backend_pressure/quality | 27.585 | 27.403 | +0.02% | 74.51 / 74.23 | 89281 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
