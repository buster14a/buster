# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.857 | 6.916 | +1.65% | 46.47 / 46.36 | 723 | no substantial regression detected |
| tiny_startup/mir-stack | 6.859 | 6.920 | +0.48% | 46.28 / 46.33 | 723 | no substantial regression detected |
| tiny_startup/fast | 6.832 | 6.815 | -0.26% | 46.32 / 46.32 | 734 | no substantial regression detected |
| tiny_startup/quality | 6.870 | 6.948 | +0.98% | 46.64 / 46.51 | 720 | no substantial regression detected |
| large_function/none | 15.901 | 15.736 | -0.56% | 70.48 / 70.66 | 65390 | no substantial regression detected |
| large_function/mir-stack | 11.724 | 11.656 | +0.04% | 72.58 / 72.28 | 88284 | no substantial regression detected |
| large_function/fast | 11.757 | 11.795 | +0.20% | 72.50 / 72.40 | 87238 | no substantial regression detected |
| large_function/quality | 11.907 | 11.891 | +0.04% | 72.61 / 72.62 | 86534 | no substantial regression detected |
| many_functions/none | 10.132 | 10.134 | -0.27% | 60.60 / 60.75 | 202198 | no substantial regression detected |
| many_functions/mir-stack | 9.680 | 9.676 | +0.19% | 58.43 / 58.33 | 211762 | no substantial regression detected |
| many_functions/fast | 9.847 | 9.816 | -0.33% | 58.55 / 58.47 | 208741 | no substantial regression detected |
| many_functions/quality | 9.911 | 9.828 | -0.24% | 58.53 / 58.67 | 208476 | no substantial regression detected |
| symbol_table/none | 16.725 | 16.762 | +0.03% | 78.63 / 78.89 | 244661 | no substantial regression detected |
| symbol_table/mir-stack | 12.100 | 12.076 | +0.16% | 78.56 / 78.60 | 339589 | no substantial regression detected |
| symbol_table/fast | 12.099 | 12.058 | -0.29% | 78.57 / 78.77 | 340093 | no substantial regression detected |
| symbol_table/quality | 12.156 | 12.202 | -0.07% | 78.90 / 78.78 | 336098 | no substantial regression detected |
| control_flow/none | 32.373 | 32.343 | -0.22% | 112.71 / 112.73 | 102928 | no substantial regression detected |
| control_flow/mir-stack | 29.424 | 29.395 | -0.03% | 110.78 / 110.69 | 113251 | no substantial regression detected |
| control_flow/fast | 26.661 | 26.641 | -0.01% | 108.80 / 108.50 | 124956 | no substantial regression detected |
| control_flow/quality | 26.672 | 26.708 | -0.01% | 108.90 / 108.82 | 124645 | no substantial regression detected |
| backend_pressure/none | 72.022 | 71.926 | -0.05% | 193.02 / 193.24 | 266968 | no substantial regression detected |
| backend_pressure/mir-stack | 61.531 | 61.579 | +0.00% | 191.11 / 191.19 | 311826 | no substantial regression detected |
| backend_pressure/fast | 62.722 | 62.746 | +0.02% | 191.23 / 191.25 | 306030 | no substantial regression detected |
| backend_pressure/quality | 68.436 | 68.401 | -0.16% | 191.36 / 191.22 | 280727 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
