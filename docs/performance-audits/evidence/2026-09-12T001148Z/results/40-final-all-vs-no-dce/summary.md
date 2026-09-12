# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.813 | 6.872 | +1.76% | 46.28 / 46.37 | 728 | no substantial regression detected |
| tiny_startup/mir-stack | 6.907 | 6.906 | -0.09% | 46.52 / 46.28 | 724 | no substantial regression detected |
| tiny_startup/fast | 6.791 | 6.851 | +0.12% | 46.36 / 46.35 | 730 | no substantial regression detected |
| tiny_startup/quality | 6.881 | 6.806 | +0.41% | 46.52 / 46.49 | 735 | no substantial regression detected |
| large_function/none | 15.752 | 15.957 | +1.25% | 70.49 / 70.52 | 64486 | no substantial regression detected |
| large_function/mir-stack | 11.616 | 11.659 | +0.07% | 72.49 / 72.44 | 88257 | no substantial regression detected |
| large_function/fast | 11.702 | 11.741 | +0.46% | 72.33 / 72.42 | 87644 | no substantial regression detected |
| large_function/quality | 11.820 | 11.846 | +0.17% | 72.54 / 72.58 | 86866 | no substantial regression detected |
| many_functions/none | 10.098 | 10.082 | -0.25% | 60.58 / 60.64 | 203224 | no substantial regression detected |
| many_functions/mir-stack | 9.591 | 9.631 | +0.45% | 58.59 / 58.41 | 212760 | no substantial regression detected |
| many_functions/fast | 9.722 | 9.708 | -0.29% | 58.46 / 58.37 | 211053 | no substantial regression detected |
| many_functions/quality | 9.783 | 9.804 | +0.02% | 58.57 / 58.61 | 209006 | no substantial regression detected |
| symbol_table/none | 16.623 | 16.646 | +0.39% | 78.62 / 78.69 | 246361 | no substantial regression detected |
| symbol_table/mir-stack | 12.018 | 11.992 | -0.45% | 78.72 / 78.80 | 341969 | no substantial regression detected |
| symbol_table/fast | 12.041 | 11.996 | -0.35% | 78.87 / 78.61 | 341863 | no substantial regression detected |
| symbol_table/quality | 12.114 | 12.168 | +0.56% | 78.82 / 78.79 | 337023 | no substantial regression detected |
| control_flow/none | 32.322 | 32.542 | +0.89% | 112.88 / 112.72 | 102299 | no substantial regression detected |
| control_flow/mir-stack | 29.375 | 29.708 | +1.20% | 110.77 / 110.67 | 112056 | no substantial regression detected |
| control_flow/fast | 26.629 | 26.795 | +0.89% | 108.79 / 110.62 | 124240 | no substantial regression detected |
| control_flow/quality | 26.667 | 26.824 | +0.63% | 108.77 / 110.90 | 124106 | no substantial regression detected |
| backend_pressure/none | 71.971 | 74.736 | +3.82% | 193.14 / 195.09 | 256932 | no substantial regression detected |
| backend_pressure/mir-stack | 61.613 | 62.235 | +1.02% | 191.13 / 193.19 | 308539 | no substantial regression detected |
| backend_pressure/fast | 62.687 | 63.558 | +1.34% | 191.27 / 191.26 | 302120 | no substantial regression detected |
| backend_pressure/quality | 68.355 | 69.938 | +2.43% | 191.45 / 191.28 | 274559 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
