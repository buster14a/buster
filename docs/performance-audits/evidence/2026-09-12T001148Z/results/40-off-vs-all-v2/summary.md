# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.107 | 7.124 | +1.23% | 46.36 / 46.22 | 702 | no substantial regression detected |
| tiny_startup/mir-stack | 6.952 | 7.067 | +1.63% | 46.20 / 46.31 | 708 | no substantial regression detected |
| tiny_startup/fast | 7.103 | 6.894 | -0.89% | 46.61 / 46.25 | 725 | no substantial regression detected |
| tiny_startup/quality | 7.186 | 7.185 | -2.04% | 46.54 / 46.40 | 696 | no substantial regression detected |
| large_function/none | 16.168 | 15.962 | -1.63% | 70.46 / 70.55 | 64467 | no substantial regression detected |
| large_function/mir-stack | 11.784 | 11.930 | +1.39% | 72.37 / 72.34 | 86255 | no substantial regression detected |
| large_function/fast | 11.911 | 12.009 | +0.73% | 72.41 / 72.58 | 85689 | no substantial regression detected |
| large_function/quality | 12.061 | 12.017 | +0.06% | 72.53 / 72.58 | 85628 | no substantial regression detected |
| many_functions/none | 10.394 | 10.499 | +0.48% | 60.46 / 60.46 | 195153 | no substantial regression detected |
| many_functions/mir-stack | 9.964 | 9.928 | -0.13% | 58.31 / 58.55 | 206381 | no substantial regression detected |
| many_functions/fast | 10.003 | 10.094 | +0.79% | 58.36 / 58.37 | 202997 | no substantial regression detected |
| many_functions/quality | 10.047 | 10.136 | +0.53% | 58.48 / 58.55 | 202155 | no substantial regression detected |
| symbol_table/none | 16.795 | 16.792 | +0.25% | 78.70 / 78.68 | 244216 | no substantial regression detected |
| symbol_table/mir-stack | 12.230 | 12.302 | +0.76% | 78.99 / 78.75 | 333367 | no substantial regression detected |
| symbol_table/fast | 12.303 | 12.275 | -0.20% | 78.56 / 78.77 | 334103 | no substantial regression detected |
| symbol_table/quality | 12.346 | 12.407 | +0.41% | 78.69 / 78.79 | 330543 | no substantial regression detected |
| control_flow/none | 31.888 | 32.083 | +0.67% | 112.79 / 112.85 | 103761 | no substantial regression detected |
| control_flow/mir-stack | 28.935 | 28.882 | -0.08% | 110.83 / 110.68 | 115263 | no substantial regression detected |
| control_flow/fast | 25.912 | 26.266 | +1.40% | 108.71 / 108.76 | 126740 | no substantial regression detected |
| control_flow/quality | 25.915 | 26.265 | +1.26% | 108.79 / 108.82 | 126749 | no substantial regression detected |
| backend_pressure/none | 72.164 | 70.884 | -1.82% | 197.30 / 193.04 | 270893 | no substantial regression detected |
| backend_pressure/mir-stack | 59.787 | 60.286 | +0.86% | 191.01 / 190.93 | 318513 | no substantial regression detected |
| backend_pressure/fast | 60.731 | 61.588 | +1.51% | 189.02 / 191.07 | 311779 | no substantial regression detected |
| backend_pressure/quality | 67.431 | 67.169 | -0.37% | 189.41 / 191.38 | 285878 | no substantial regression detected |
| self_host_stage1/fast | 932.598 | 952.804 | +2.14% | 1778.90 / 1772.95 | 396039 | no substantial regression detected |
| self_host_stage2/fast | 4269.951 | 4131.068 | -3.09% | 1791.51 / 1790.55 | 91140 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
