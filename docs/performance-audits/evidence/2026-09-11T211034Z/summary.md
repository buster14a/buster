# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 18.244 | 18.208 | +0.29% | 21.65 / 21.65 | 275 | inconclusive |
| tiny_startup/mir-stack | 19.085 | 19.269 | +0.52% | 21.61 / 21.59 | 259 | inconclusive |
| tiny_startup/fast | 18.024 | 18.292 | -2.53% | 21.60 / 21.57 | 273 | no substantial regression detected |
| tiny_startup/quality | 18.051 | 17.893 | -0.23% | 21.63 / 21.63 | 279 | inconclusive |
| large_function/none | 40.323 | 39.582 | -1.32% | 25.61 / 25.72 | 25997 | inconclusive |
| large_function/mir-stack | 29.324 | 28.668 | -2.24% | 27.84 / 27.56 | 35893 | inconclusive |
| large_function/fast | 28.954 | 29.113 | +1.89% | 27.75 / 27.51 | 35346 | inconclusive |
| large_function/quality | 28.963 | 29.101 | +1.01% | 27.83 / 27.69 | 35360 | inconclusive |
| many_functions/none | 27.896 | 27.936 | +0.38% | 28.21 / 28.20 | 73346 | inconclusive |
| many_functions/mir-stack | 24.633 | 25.409 | +0.22% | 27.29 / 27.28 | 80642 | inconclusive |
| many_functions/fast | 25.666 | 25.866 | -0.24% | 27.19 / 27.16 | 79215 | inconclusive |
| many_functions/quality | 25.476 | 26.508 | +3.57% | 27.16 / 27.20 | 77299 | inconclusive |
| symbol_table/none | 40.954 | 40.436 | +1.36% | 27.94 / 27.96 | 101420 | no substantial regression detected |
| symbol_table/mir-stack | 29.163 | 29.699 | +0.34% | 28.96 / 28.92 | 138087 | inconclusive |
| symbol_table/fast | 28.886 | 28.812 | +0.56% | 28.56 / 28.55 | 142336 | inconclusive |
| symbol_table/quality | 29.303 | 29.599 | +0.51% | 28.66 / 28.68 | 138555 | no substantial regression detected |
| control_flow/none | 89.159 | 89.524 | +0.66% | 39.04 / 39.16 | 37186 | inconclusive |
| control_flow/mir-stack | 69.625 | 68.580 | -1.59% | 37.90 / 37.76 | 48542 | inconclusive |
| control_flow/fast | 60.722 | 62.322 | +0.88% | 37.15 / 37.26 | 53425 | inconclusive |
| control_flow/quality | 60.290 | 62.760 | +0.69% | 37.15 / 37.26 | 53062 | inconclusive |
| backend_pressure/none | 200.105 | 199.482 | -2.60% | 55.64 / 55.76 | 96260 | inconclusive |
| backend_pressure/mir-stack | 149.123 | 147.287 | +1.98% | 55.71 / 55.46 | 130371 | inconclusive |
| backend_pressure/fast | 150.889 | 147.812 | -2.48% | 53.46 / 53.96 | 129909 | inconclusive |
| backend_pressure/quality | 161.767 | 164.272 | +0.75% | 53.71 / 54.23 | 116900 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **21**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
