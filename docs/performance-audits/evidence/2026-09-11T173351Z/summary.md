# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 25.367 | 24.224 | -2.89% | 49.31 / 49.31 | 208 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 21.352 | 20.151 | -6.87% | 49.36 / 49.34 | 248 | diagnostic (guard disabled) |
| tiny_startup/fast | 21.323 | 18.056 | -5.09% | 49.34 / 49.29 | 277 | diagnostic (guard disabled) |
| tiny_startup/quality | 19.358 | 19.769 | +2.19% | 49.41 / 49.44 | 253 | diagnostic (guard disabled) |
| large_function/none | 28.916 | 27.707 | -3.51% | 51.29 / 51.32 | 4803 | diagnostic (guard disabled) |
| large_function/mir-stack | 21.230 | 21.294 | -1.19% | 51.36 / 51.32 | 6248 | diagnostic (guard disabled) |
| large_function/fast | 22.423 | 21.880 | -1.13% | 51.47 / 51.32 | 6079 | diagnostic (guard disabled) |
| large_function/quality | 30.183 | 26.569 | -1.69% | 51.35 / 51.39 | 5009 | diagnostic (guard disabled) |
| many_functions/none | 23.742 | 19.896 | -9.19% | 51.32 / 51.49 | 12918 | diagnostic (guard disabled) |
| many_functions/mir-stack | 23.549 | 22.920 | -4.47% | 51.33 / 51.36 | 11254 | diagnostic (guard disabled) |
| many_functions/fast | 21.032 | 24.361 | +5.09% | 51.43 / 51.35 | 10563 | diagnostic (guard disabled) |
| many_functions/quality | 21.183 | 21.194 | +2.67% | 51.41 / 51.28 | 12127 | diagnostic (guard disabled) |
| symbol_table/none | 26.089 | 23.660 | -4.41% | 53.35 / 53.37 | 21854 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 28.776 | 30.227 | +9.62% | 53.32 / 53.34 | 17109 | diagnostic (guard disabled) |
| symbol_table/fast | 27.454 | 24.489 | -9.35% | 53.42 / 53.39 | 21114 | diagnostic (guard disabled) |
| symbol_table/quality | 22.779 | 22.537 | +4.14% | 53.36 / 53.38 | 22943 | diagnostic (guard disabled) |
| control_flow/none | 29.035 | 31.429 | +1.26% | 61.44 / 61.40 | 13374 | diagnostic (guard disabled) |
| control_flow/mir-stack | 24.517 | 25.822 | +4.67% | 61.38 / 61.45 | 16166 | diagnostic (guard disabled) |
| control_flow/fast | 32.107 | 31.608 | -0.97% | 61.42 / 61.40 | 13211 | diagnostic (guard disabled) |
| control_flow/quality | 32.032 | 24.545 | -9.70% | 61.42 / 61.41 | 16993 | diagnostic (guard disabled) |
| backend_pressure/none | 47.712 | 42.888 | -3.15% | 77.48 / 77.42 | 56021 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 38.794 | 42.470 | +1.55% | 77.52 / 77.44 | 56866 | diagnostic (guard disabled) |
| backend_pressure/fast | 35.799 | 38.505 | +6.28% | 77.41 / 77.48 | 62382 | diagnostic (guard disabled) |
| backend_pressure/quality | 37.392 | 35.955 | -2.28% | 77.54 / 77.46 | 66828 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
