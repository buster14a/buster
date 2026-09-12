# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 20.183 | 22.349 | +10.16% | 23.21 / 23.45 | 224 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 20.232 | 19.967 | +2.37% | 23.12 / 23.36 | 250 | diagnostic (guard disabled) |
| tiny_startup/fast | 20.928 | 21.300 | +2.31% | 23.20 / 23.36 | 235 | diagnostic (guard disabled) |
| tiny_startup/quality | 23.011 | 21.097 | -1.94% | 23.28 / 23.39 | 237 | diagnostic (guard disabled) |
| large_function/none | 24.902 | 24.131 | -1.32% | 23.97 / 24.15 | 5513 | diagnostic (guard disabled) |
| large_function/mir-stack | 23.069 | 23.449 | +2.03% | 24.00 / 24.16 | 5681 | diagnostic (guard disabled) |
| large_function/fast | 22.249 | 22.875 | +1.29% | 24.05 / 24.22 | 5814 | diagnostic (guard disabled) |
| large_function/quality | 22.694 | 24.204 | +8.36% | 24.09 / 24.14 | 5496 | diagnostic (guard disabled) |
| many_functions/none | 21.700 | 23.633 | +12.82% | 24.25 / 24.45 | 10875 | diagnostic (guard disabled) |
| many_functions/mir-stack | 22.752 | 22.513 | -0.38% | 23.99 / 24.30 | 11416 | diagnostic (guard disabled) |
| many_functions/fast | 21.102 | 22.868 | +14.58% | 24.05 / 24.27 | 11239 | diagnostic (guard disabled) |
| many_functions/quality | 21.595 | 21.449 | +1.38% | 24.06 / 24.30 | 11983 | diagnostic (guard disabled) |
| symbol_table/none | 26.311 | 25.454 | -1.04% | 24.24 / 24.50 | 20311 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 22.016 | 22.682 | +0.99% | 24.14 / 24.34 | 22803 | diagnostic (guard disabled) |
| symbol_table/fast | 25.672 | 23.603 | +0.56% | 24.12 / 24.29 | 21905 | diagnostic (guard disabled) |
| symbol_table/quality | 21.829 | 21.644 | +2.72% | 24.25 / 24.31 | 23888 | diagnostic (guard disabled) |
| control_flow/none | 30.686 | 33.040 | +7.37% | 25.88 / 26.17 | 12626 | diagnostic (guard disabled) |
| control_flow/mir-stack | 28.251 | 29.838 | +5.42% | 25.62 / 25.95 | 13975 | diagnostic (guard disabled) |
| control_flow/fast | 26.181 | 29.478 | +8.39% | 25.75 / 25.86 | 14159 | diagnostic (guard disabled) |
| control_flow/quality | 27.263 | 27.192 | -1.20% | 25.65 / 25.79 | 15337 | diagnostic (guard disabled) |
| backend_pressure/none | 51.130 | 52.457 | -2.85% | 28.06 / 28.38 | 45808 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 41.383 | 41.358 | +6.44% | 28.27 / 28.47 | 58457 | diagnostic (guard disabled) |
| backend_pressure/fast | 39.254 | 41.904 | +0.62% | 27.93 / 28.20 | 57322 | diagnostic (guard disabled) |
| backend_pressure/quality | 44.652 | 42.829 | -6.10% | 28.20 / 28.54 | 56091 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
