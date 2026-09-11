# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.166 | 19.962 | +1.89% | 23.35 / 23.39 | 250 | inconclusive |
| tiny_startup/mir-stack | 19.270 | 19.807 | +2.65% | 23.35 / 23.40 | 252 | inconclusive |
| tiny_startup/fast | 19.085 | 19.561 | +1.48% | 23.36 / 23.41 | 256 | inconclusive |
| tiny_startup/quality | 18.944 | 19.523 | +2.77% | 23.44 / 23.38 | 256 | inconclusive |
| large_function/none | 44.552 | 42.770 | -2.45% | 27.54 / 27.55 | 24059 | inconclusive |
| large_function/mir-stack | 30.736 | 31.924 | +0.03% | 29.29 / 29.30 | 32233 | inconclusive |
| large_function/fast | 31.990 | 30.758 | +0.18% | 29.33 / 29.29 | 33455 | inconclusive |
| large_function/quality | 31.404 | 31.440 | -0.78% | 29.47 / 29.43 | 32728 | inconclusive |
| many_functions/none | 30.809 | 30.139 | -0.21% | 30.07 / 30.05 | 67986 | no substantial regression detected |
| many_functions/mir-stack | 26.078 | 26.049 | +0.01% | 29.05 / 29.06 | 78659 | inconclusive |
| many_functions/fast | 27.382 | 27.160 | -0.25% | 28.95 / 28.93 | 75442 | inconclusive |
| many_functions/quality | 27.168 | 26.993 | -2.62% | 28.96 / 28.94 | 75909 | inconclusive |
| symbol_table/none | 43.304 | 42.899 | +0.32% | 29.80 / 29.77 | 95597 | inconclusive |
| symbol_table/mir-stack | 31.310 | 30.951 | -1.02% | 30.68 / 30.64 | 132503 | inconclusive |
| symbol_table/fast | 31.773 | 30.405 | -3.33% | 30.43 / 30.38 | 134883 | inconclusive |
| symbol_table/quality | 32.897 | 33.969 | +0.54% | 30.58 / 30.61 | 120731 | inconclusive |
| control_flow/none | 97.242 | 95.758 | -1.29% | 40.82 / 40.85 | 34765 | inconclusive |
| control_flow/mir-stack | 80.358 | 73.769 | -3.02% | 39.57 / 39.53 | 45127 | inconclusive |
| control_flow/fast | 66.068 | 65.439 | -1.74% | 39.00 / 39.00 | 50871 | inconclusive |
| control_flow/quality | 65.820 | 67.016 | +0.74% | 39.04 / 39.07 | 49675 | inconclusive |
| backend_pressure/none | 206.538 | 204.724 | -0.94% | 57.46 / 57.43 | 93795 | inconclusive |
| backend_pressure/mir-stack | 155.045 | 154.631 | -0.84% | 57.20 / 57.18 | 124180 | inconclusive |
| backend_pressure/fast | 159.951 | 159.492 | -1.89% | 55.90 / 56.05 | 120395 | inconclusive |
| backend_pressure/quality | 170.981 | 171.459 | +1.58% | 56.10 / 56.06 | 112009 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **23**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
