# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 19.024 | 19.136 | -3.37% | 23.40 / 23.37 | 261 | inconclusive |
| tiny_startup/mir-stack | 18.861 | 18.817 | +0.23% | 23.38 / 23.38 | 266 | inconclusive |
| tiny_startup/fast | 18.607 | 18.318 | -1.08% | 23.36 / 23.40 | 273 | inconclusive |
| tiny_startup/quality | 18.528 | 19.209 | +2.26% | 23.40 / 23.46 | 260 | inconclusive |
| large_function/none | 42.088 | 41.080 | -3.08% | 27.35 / 27.57 | 25049 | no substantial regression detected |
| large_function/mir-stack | 29.106 | 29.340 | +3.25% | 29.55 / 29.33 | 35072 | inconclusive |
| large_function/fast | 29.386 | 30.796 | +1.56% | 29.43 / 29.34 | 33414 | inconclusive |
| large_function/quality | 30.644 | 30.507 | -1.10% | 29.68 / 29.45 | 33730 | inconclusive |
| many_functions/none | 28.887 | 29.027 | +0.57% | 30.09 / 30.05 | 70590 | inconclusive |
| many_functions/mir-stack | 25.811 | 26.690 | +2.67% | 29.11 / 29.07 | 76772 | inconclusive |
| many_functions/fast | 26.400 | 25.849 | -1.73% | 28.91 / 28.92 | 79269 | inconclusive |
| many_functions/quality | 25.808 | 25.573 | +0.82% | 28.94 / 28.95 | 80125 | inconclusive |
| symbol_table/none | 42.203 | 42.221 | +1.84% | 29.79 / 29.77 | 97132 | inconclusive |
| symbol_table/mir-stack | 30.812 | 31.683 | +1.78% | 30.62 / 30.67 | 129442 | inconclusive |
| symbol_table/fast | 30.646 | 30.464 | -0.40% | 30.54 / 30.38 | 134638 | inconclusive |
| symbol_table/quality | 30.425 | 30.144 | -0.03% | 30.53 / 30.53 | 136046 | inconclusive |
| control_flow/none | 91.381 | 95.175 | +3.69% | 40.84 / 41.00 | 34978 | inconclusive |
| control_flow/mir-stack | 69.795 | 71.608 | +2.50% | 39.71 / 39.66 | 46490 | inconclusive |
| control_flow/fast | 61.267 | 64.934 | +6.53% | 38.95 / 38.98 | 51268 | inconclusive |
| control_flow/quality | 60.854 | 63.657 | +1.73% | 38.84 / 39.00 | 52297 | inconclusive |
| backend_pressure/none | 207.445 | 199.928 | -2.24% | 57.46 / 57.50 | 96045 | inconclusive |
| backend_pressure/mir-stack | 152.258 | 153.945 | +1.82% | 57.43 / 57.15 | 124733 | inconclusive |
| backend_pressure/fast | 145.249 | 154.597 | +6.77% | 55.30 / 55.80 | 124207 | inconclusive |
| backend_pressure/quality | 162.825 | 167.715 | +4.65% | 55.64 / 56.05 | 114492 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **23**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
