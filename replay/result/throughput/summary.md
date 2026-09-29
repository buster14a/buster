# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 25.342 | 23.617 | -7.50% | 48.34 / 47.77 | 220 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 24.918 | 23.621 | -5.67% | 48.08 / 47.67 | 220 | diagnostic (guard disabled) |
| tiny_startup/fast | 28.778 | 28.487 | -0.98% | 47.47 / 47.52 | 176 | diagnostic (guard disabled) |
| tiny_startup/quality | 25.380 | 23.502 | -7.90% | 47.59 / 47.60 | 223 | diagnostic (guard disabled) |
| large_function/none | 26.363 | 23.566 | -10.06% | 50.27 / 49.64 | 5655 | diagnostic (guard disabled) |
| large_function/mir-stack | 31.442 | 24.742 | -22.70% | 49.80 / 49.70 | 5571 | diagnostic (guard disabled) |
| large_function/fast | 22.181 | 20.542 | -7.39% | 49.54 / 50.30 | 6475 | diagnostic (guard disabled) |
| large_function/quality | 21.875 | 22.965 | +4.01% | 49.36 / 50.26 | 5901 | diagnostic (guard disabled) |
| many_functions/none | 22.647 | 21.063 | -6.96% | 50.30 / 49.38 | 12202 | diagnostic (guard disabled) |
| many_functions/mir-stack | 24.551 | 24.227 | -2.25% | 49.54 / 49.61 | 10947 | diagnostic (guard disabled) |
| many_functions/fast | 27.076 | 24.586 | -9.04% | 49.46 / 50.19 | 10853 | diagnostic (guard disabled) |
| many_functions/quality | 26.180 | 26.500 | +3.03% | 50.52 / 49.14 | 9728 | diagnostic (guard disabled) |
| symbol_table/none | 30.674 | 27.421 | -9.89% | 51.43 / 51.15 | 19402 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 27.455 | 26.155 | -4.97% | 52.15 / 51.62 | 20733 | diagnostic (guard disabled) |
| symbol_table/fast | 33.285 | 28.119 | -15.60% | 51.45 / 51.79 | 18422 | diagnostic (guard disabled) |
| symbol_table/quality | 22.327 | 21.078 | -5.60% | 51.43 / 51.43 | 24532 | diagnostic (guard disabled) |
| control_flow/none | 35.746 | 35.617 | -0.32% | 59.39 / 59.77 | 12206 | diagnostic (guard disabled) |
| control_flow/mir-stack | 32.098 | 29.826 | -7.12% | 60.45 / 59.66 | 14229 | diagnostic (guard disabled) |
| control_flow/fast | 34.007 | 32.206 | -5.62% | 57.92 / 59.03 | 13518 | diagnostic (guard disabled) |
| control_flow/quality | 26.660 | 29.960 | +11.21% | 57.86 / 59.53 | 14214 | diagnostic (guard disabled) |
| backend_pressure/none | 48.125 | 48.417 | -0.63% | 75.23 / 76.71 | 50856 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 44.927 | 43.871 | -2.31% | 74.45 / 75.41 | 57323 | diagnostic (guard disabled) |
| backend_pressure/fast | 36.509 | 42.479 | +14.71% | 74.31 / 74.18 | 58180 | diagnostic (guard disabled) |
| backend_pressure/quality | 47.792 | 37.082 | -21.82% | 74.46 / 74.72 | 64782 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
