# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.929 | 7.192 | +1.05% | 46.32 / 46.47 | 695 | no substantial regression detected |
| tiny_startup/mir-stack | 7.108 | 7.144 | +0.97% | 46.28 / 46.34 | 700 | no substantial regression detected |
| tiny_startup/fast | 6.929 | 7.174 | +1.58% | 46.64 / 46.31 | 697 | no substantial regression detected |
| tiny_startup/quality | 7.095 | 7.102 | +2.79% | 46.53 / 46.53 | 704 | no substantial regression detected |
| large_function/none | 16.226 | 16.411 | +1.22% | 70.41 / 70.36 | 62702 | no substantial regression detected |
| large_function/mir-stack | 12.063 | 12.055 | -0.41% | 72.32 / 72.54 | 85360 | no substantial regression detected |
| large_function/fast | 12.200 | 12.146 | -0.69% | 72.49 / 72.54 | 84722 | no substantial regression detected |
| large_function/quality | 12.201 | 12.255 | +0.73% | 72.62 / 72.61 | 83967 | no substantial regression detected |
| many_functions/none | 10.502 | 10.547 | +0.40% | 60.45 / 60.61 | 194279 | no substantial regression detected |
| many_functions/mir-stack | 10.038 | 10.037 | -0.11% | 58.51 / 58.54 | 204136 | no substantial regression detected |
| many_functions/fast | 10.206 | 10.122 | -1.44% | 58.33 / 58.44 | 202436 | no substantial regression detected |
| many_functions/quality | 10.297 | 10.228 | -0.69% | 58.47 / 58.55 | 200325 | no substantial regression detected |
| symbol_table/none | 16.948 | 17.002 | +0.51% | 78.79 / 78.68 | 241211 | no substantial regression detected |
| symbol_table/mir-stack | 12.369 | 12.393 | -0.06% | 78.59 / 78.59 | 330915 | no substantial regression detected |
| symbol_table/fast | 12.418 | 12.471 | +0.23% | 78.60 / 78.71 | 328842 | no substantial regression detected |
| symbol_table/quality | 12.513 | 12.488 | -0.04% | 78.69 / 78.89 | 328406 | no substantial regression detected |
| control_flow/none | 32.476 | 32.458 | -0.04% | 112.67 / 112.70 | 102563 | no substantial regression detected |
| control_flow/mir-stack | 29.642 | 29.832 | +0.55% | 110.71 / 110.57 | 111593 | no substantial regression detected |
| control_flow/fast | 26.877 | 26.724 | -0.59% | 108.88 / 108.92 | 124569 | no substantial regression detected |
| control_flow/quality | 26.868 | 26.692 | -0.65% | 108.87 / 108.81 | 124720 | no substantial regression detected |
| backend_pressure/none | 72.135 | 73.831 | +2.39% | 193.18 / 197.04 | 260080 | no substantial regression detected |
| backend_pressure/mir-stack | 61.777 | 61.539 | -0.55% | 191.19 / 191.22 | 312028 | no substantial regression detected |
| backend_pressure/fast | 62.995 | 62.580 | -0.69% | 191.16 / 189.18 | 306837 | no substantial regression detected |
| backend_pressure/quality | 68.587 | 69.209 | +0.85% | 191.29 / 189.26 | 277451 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
