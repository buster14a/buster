# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.396 | 23.993 | +23.17% | 23.30 / 23.33 | 212 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 21.487 | 18.094 | -14.30% | 23.26 / 23.32 | 277 | diagnostic (guard disabled) |
| tiny_startup/fast | 18.253 | 18.212 | -0.28% | 23.24 / 23.38 | 275 | diagnostic (guard disabled) |
| tiny_startup/quality | 16.782 | 17.828 | +6.22% | 23.32 / 23.38 | 281 | diagnostic (guard disabled) |
| large_function/none | 41.936 | 39.219 | -6.31% | 27.33 / 27.39 | 26251 | diagnostic (guard disabled) |
| large_function/mir-stack | 30.183 | 27.319 | -8.94% | 29.45 / 29.52 | 37669 | diagnostic (guard disabled) |
| large_function/fast | 33.885 | 28.572 | -13.67% | 29.44 / 29.35 | 36238 | diagnostic (guard disabled) |
| large_function/quality | 28.124 | 27.543 | -2.10% | 29.40 / 29.46 | 37387 | diagnostic (guard disabled) |
| many_functions/none | 29.394 | 28.021 | -4.65% | 30.04 / 30.05 | 73123 | diagnostic (guard disabled) |
| many_functions/mir-stack | 27.069 | 25.170 | -5.94% | 29.12 / 29.02 | 81423 | diagnostic (guard disabled) |
| many_functions/fast | 30.323 | 29.748 | -3.89% | 28.87 / 28.86 | 71937 | diagnostic (guard disabled) |
| many_functions/quality | 35.044 | 30.722 | -10.71% | 28.90 / 28.95 | 66736 | diagnostic (guard disabled) |
| symbol_table/none | 40.624 | 43.433 | +6.98% | 29.61 / 29.78 | 94855 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 29.448 | 31.200 | +5.86% | 30.55 / 30.42 | 131890 | diagnostic (guard disabled) |
| symbol_table/fast | 42.069 | 38.507 | -8.52% | 30.34 / 30.25 | 109781 | diagnostic (guard disabled) |
| symbol_table/quality | 35.902 | 31.797 | -11.24% | 30.46 / 30.52 | 129220 | diagnostic (guard disabled) |
| control_flow/none | 93.939 | 91.294 | -2.82% | 41.92 / 41.81 | 36480 | diagnostic (guard disabled) |
| control_flow/mir-stack | 70.953 | 72.068 | +1.66% | 40.38 / 40.39 | 46194 | diagnostic (guard disabled) |
| control_flow/fast | 99.824 | 64.007 | -30.21% | 39.76 / 39.94 | 52040 | diagnostic (guard disabled) |
| control_flow/quality | 96.092 | 66.295 | -30.33% | 39.80 / 39.84 | 50406 | diagnostic (guard disabled) |
| backend_pressure/none | 269.043 | 249.258 | -7.37% | 57.55 / 57.38 | 78733 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 157.839 | 160.290 | +1.53% | 57.38 / 57.60 | 119924 | diagnostic (guard disabled) |
| backend_pressure/fast | 230.057 | 161.112 | -26.65% | 55.19 / 55.35 | 119982 | diagnostic (guard disabled) |
| backend_pressure/quality | 180.355 | 169.981 | -5.56% | 55.41 / 55.41 | 112969 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
