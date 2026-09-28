# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 20.197 | 20.819 | +3.09% | 48.63 / 47.43 | 240 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 24.692 | 24.593 | -0.27% | 47.38 / 48.65 | 211 | diagnostic (guard disabled) |
| tiny_startup/fast | 20.131 | 19.657 | -2.34% | 47.97 / 48.60 | 254 | diagnostic (guard disabled) |
| tiny_startup/quality | 25.050 | 22.263 | -9.93% | 47.59 / 47.58 | 226 | diagnostic (guard disabled) |
| large_function/none | 29.323 | 28.561 | -1.84% | 49.79 / 50.13 | 4831 | diagnostic (guard disabled) |
| large_function/mir-stack | 21.566 | 24.774 | +14.29% | 49.74 / 49.95 | 5424 | diagnostic (guard disabled) |
| large_function/fast | 29.394 | 28.578 | -3.02% | 49.54 / 50.70 | 4695 | diagnostic (guard disabled) |
| large_function/quality | 24.071 | 24.345 | +1.06% | 49.68 / 49.68 | 5526 | diagnostic (guard disabled) |
| many_functions/none | 26.482 | 26.612 | +0.62% | 49.71 / 49.55 | 9960 | diagnostic (guard disabled) |
| many_functions/mir-stack | 22.976 | 21.260 | -6.88% | 49.94 / 49.59 | 12088 | diagnostic (guard disabled) |
| many_functions/fast | 23.305 | 25.830 | +9.58% | 50.31 / 49.89 | 10333 | diagnostic (guard disabled) |
| many_functions/quality | 21.213 | 21.326 | +0.52% | 49.58 / 49.71 | 12054 | diagnostic (guard disabled) |
| symbol_table/none | 29.213 | 29.655 | +1.13% | 51.93 / 51.52 | 18199 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 26.748 | 24.820 | -6.48% | 52.02 / 52.17 | 21319 | diagnostic (guard disabled) |
| symbol_table/fast | 22.238 | 21.796 | -1.99% | 51.76 / 52.19 | 23726 | diagnostic (guard disabled) |
| symbol_table/quality | 27.200 | 27.450 | +1.23% | 52.14 / 51.66 | 19503 | diagnostic (guard disabled) |
| control_flow/none | 36.539 | 36.281 | -1.12% | 59.59 / 60.16 | 12084 | diagnostic (guard disabled) |
| control_flow/mir-stack | 35.977 | 33.708 | -7.52% | 60.72 / 60.20 | 12974 | diagnostic (guard disabled) |
| control_flow/fast | 30.917 | 33.346 | +6.58% | 58.20 / 59.66 | 13049 | diagnostic (guard disabled) |
| control_flow/quality | 33.484 | 30.977 | -7.42% | 58.35 / 60.14 | 13820 | diagnostic (guard disabled) |
| backend_pressure/none | 50.860 | 53.888 | +5.25% | 75.52 / 74.92 | 47053 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 34.713 | 40.074 | +14.20% | 75.60 / 74.98 | 61272 | diagnostic (guard disabled) |
| backend_pressure/fast | 36.255 | 35.789 | -1.27% | 75.54 / 74.48 | 67116 | diagnostic (guard disabled) |
| backend_pressure/quality | 38.727 | 38.562 | -0.43% | 74.45 / 74.95 | 62294 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
