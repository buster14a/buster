# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.663 | 19.877 | -1.13% | 23.42 / 23.39 | 252 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 18.466 | 18.931 | +0.44% | 23.35 / 23.28 | 264 | diagnostic (guard disabled) |
| tiny_startup/fast | 18.644 | 17.637 | -4.40% | 23.26 / 23.27 | 283 | diagnostic (guard disabled) |
| tiny_startup/quality | 18.103 | 18.536 | +2.15% | 23.26 / 23.31 | 270 | diagnostic (guard disabled) |
| large_function/none | 43.549 | 44.808 | -2.42% | 27.35 / 27.45 | 23009 | diagnostic (guard disabled) |
| large_function/mir-stack | 29.929 | 28.127 | -5.81% | 29.49 / 29.54 | 36585 | diagnostic (guard disabled) |
| large_function/fast | 29.227 | 29.258 | +2.67% | 29.33 / 29.35 | 35187 | diagnostic (guard disabled) |
| large_function/quality | 29.855 | 29.574 | +2.23% | 29.58 / 29.70 | 34796 | diagnostic (guard disabled) |
| many_functions/none | 27.746 | 29.995 | +8.59% | 29.98 / 30.03 | 68320 | diagnostic (guard disabled) |
| many_functions/mir-stack | 24.828 | 25.886 | +3.01% | 28.87 / 29.03 | 79159 | diagnostic (guard disabled) |
| many_functions/fast | 25.753 | 27.012 | +2.67% | 28.80 / 28.99 | 75934 | diagnostic (guard disabled) |
| many_functions/quality | 25.366 | 24.586 | +0.29% | 28.84 / 28.97 | 83342 | diagnostic (guard disabled) |
| symbol_table/none | 43.308 | 44.935 | +3.20% | 29.77 / 29.78 | 91268 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 29.055 | 29.555 | +3.55% | 30.53 / 30.52 | 138760 | diagnostic (guard disabled) |
| symbol_table/fast | 31.325 | 29.712 | -4.52% | 30.28 / 30.39 | 138036 | diagnostic (guard disabled) |
| symbol_table/quality | 31.627 | 31.528 | +0.42% | 30.44 / 30.46 | 130081 | diagnostic (guard disabled) |
| control_flow/none | 93.554 | 96.169 | +2.11% | 40.80 / 41.80 | 34617 | diagnostic (guard disabled) |
| control_flow/mir-stack | 76.713 | 74.706 | -0.50% | 39.57 / 40.57 | 44562 | diagnostic (guard disabled) |
| control_flow/fast | 65.875 | 61.532 | +2.50% | 38.79 / 39.75 | 54104 | diagnostic (guard disabled) |
| control_flow/quality | 65.635 | 67.015 | +2.31% | 38.74 / 39.87 | 49679 | diagnostic (guard disabled) |
| backend_pressure/none | 207.417 | 213.569 | +3.04% | 57.33 / 57.57 | 89910 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 151.859 | 160.856 | +4.74% | 57.29 / 57.62 | 119378 | diagnostic (guard disabled) |
| backend_pressure/fast | 157.485 | 157.124 | -3.27% | 55.26 / 55.41 | 122209 | diagnostic (guard disabled) |
| backend_pressure/quality | 164.168 | 168.841 | +5.04% | 55.45 / 55.60 | 113744 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
