# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.999 | 20.000 | +0.02% | 47.66 / 48.37 | 250 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 18.845 | 19.290 | +2.36% | 48.55 / 47.24 | 259 | diagnostic (guard disabled) |
| tiny_startup/fast | 23.881 | 23.967 | +0.10% | 47.27 / 47.91 | 214 | diagnostic (guard disabled) |
| tiny_startup/quality | 23.370 | 22.314 | -4.08% | 48.02 / 47.41 | 227 | diagnostic (guard disabled) |
| large_function/none | 27.555 | 27.776 | +0.77% | 49.68 / 49.85 | 4943 | diagnostic (guard disabled) |
| large_function/mir-stack | 25.149 | 25.239 | +0.14% | 50.17 / 49.38 | 5441 | diagnostic (guard disabled) |
| large_function/fast | 21.432 | 21.297 | -0.49% | 49.90 / 49.75 | 6245 | diagnostic (guard disabled) |
| large_function/quality | 25.090 | 25.416 | +1.37% | 50.62 / 49.80 | 5372 | diagnostic (guard disabled) |
| many_functions/none | 21.024 | 20.729 | -1.43% | 49.71 / 50.30 | 12403 | diagnostic (guard disabled) |
| many_functions/mir-stack | 20.899 | 20.248 | -3.10% | 49.97 / 50.14 | 12696 | diagnostic (guard disabled) |
| many_functions/fast | 23.288 | 24.686 | +5.08% | 49.49 / 49.46 | 10720 | diagnostic (guard disabled) |
| many_functions/quality | 20.532 | 22.339 | +8.48% | 49.98 / 50.02 | 11576 | diagnostic (guard disabled) |
| symbol_table/none | 28.011 | 22.437 | -18.68% | 51.76 / 52.12 | 23063 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 21.766 | 23.651 | +8.22% | 51.74 / 51.59 | 22041 | diagnostic (guard disabled) |
| symbol_table/fast | 22.189 | 21.324 | -3.91% | 51.58 / 52.16 | 24263 | diagnostic (guard disabled) |
| symbol_table/quality | 25.271 | 25.966 | +3.09% | 51.98 / 52.07 | 20352 | diagnostic (guard disabled) |
| control_flow/none | 34.970 | 34.018 | -2.20% | 60.12 / 59.99 | 12658 | diagnostic (guard disabled) |
| control_flow/mir-stack | 28.849 | 26.370 | -8.22% | 59.66 / 60.16 | 15816 | diagnostic (guard disabled) |
| control_flow/fast | 25.846 | 25.822 | -0.09% | 57.43 / 60.32 | 16149 | diagnostic (guard disabled) |
| control_flow/quality | 30.823 | 31.720 | +2.82% | 58.62 / 60.31 | 13580 | diagnostic (guard disabled) |
| backend_pressure/none | 48.736 | 49.860 | +2.01% | 74.95 / 75.22 | 50161 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 33.990 | 34.672 | +2.00% | 74.89 / 75.19 | 69297 | diagnostic (guard disabled) |
| backend_pressure/fast | 35.234 | 35.089 | -0.37% | 73.99 / 74.19 | 68456 | diagnostic (guard disabled) |
| backend_pressure/quality | 37.519 | 45.718 | +19.57% | 75.01 / 74.54 | 54580 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
