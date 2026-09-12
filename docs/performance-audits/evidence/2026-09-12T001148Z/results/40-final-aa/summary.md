# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.141 | 7.247 | +1.75% | 46.58 / 46.40 | 690 | no substantial regression detected |
| tiny_startup/mir-stack | 7.126 | 7.060 | +0.03% | 46.31 / 46.25 | 708 | no substantial regression detected |
| tiny_startup/fast | 7.134 | 7.189 | +0.66% | 46.34 / 46.39 | 696 | no substantial regression detected |
| tiny_startup/quality | 7.113 | 7.163 | -0.26% | 46.52 / 46.60 | 698 | no substantial regression detected |
| large_function/none | 16.243 | 16.264 | +0.15% | 70.50 / 70.58 | 63268 | no substantial regression detected |
| large_function/mir-stack | 11.887 | 11.856 | +0.60% | 72.50 / 72.29 | 86791 | no substantial regression detected |
| large_function/fast | 11.979 | 11.976 | +0.22% | 72.41 / 72.53 | 85921 | no substantial regression detected |
| large_function/quality | 12.064 | 12.069 | -0.06% | 72.47 / 72.55 | 85262 | no substantial regression detected |
| many_functions/none | 10.421 | 10.467 | -0.34% | 60.49 / 60.42 | 195767 | no substantial regression detected |
| many_functions/mir-stack | 9.967 | 9.971 | -0.01% | 58.21 / 58.39 | 205492 | no substantial regression detected |
| many_functions/fast | 10.022 | 10.082 | +1.38% | 58.49 / 58.54 | 203241 | no substantial regression detected |
| many_functions/quality | 10.085 | 10.149 | +0.07% | 58.67 / 58.53 | 201894 | no substantial regression detected |
| symbol_table/none | 16.890 | 16.858 | +0.15% | 78.48 / 78.71 | 243269 | no substantial regression detected |
| symbol_table/mir-stack | 12.251 | 12.257 | +0.10% | 78.69 / 78.78 | 334582 | no substantial regression detected |
| symbol_table/fast | 12.317 | 12.286 | -0.38% | 78.72 / 78.65 | 333795 | no substantial regression detected |
| symbol_table/quality | 12.401 | 12.389 | -0.46% | 78.80 / 78.93 | 331028 | no substantial regression detected |
| control_flow/none | 31.703 | 31.807 | +0.40% | 112.69 / 112.68 | 104662 | no substantial regression detected |
| control_flow/mir-stack | 29.111 | 29.138 | -0.12% | 110.69 / 110.74 | 114251 | no substantial regression detected |
| control_flow/fast | 26.039 | 26.039 | +0.26% | 108.66 / 108.73 | 127848 | no substantial regression detected |
| control_flow/quality | 26.026 | 25.972 | -0.24% | 108.71 / 108.83 | 128178 | no substantial regression detected |
| backend_pressure/none | 72.487 | 72.332 | -0.15% | 197.15 / 197.17 | 265472 | no substantial regression detected |
| backend_pressure/mir-stack | 60.051 | 60.030 | -0.05% | 191.22 / 191.21 | 319873 | no substantial regression detected |
| backend_pressure/fast | 61.156 | 61.191 | +0.04% | 189.24 / 189.23 | 313804 | no substantial regression detected |
| backend_pressure/quality | 67.686 | 67.750 | +0.20% | 189.32 / 189.37 | 283426 | no substantial regression detected |
| self_host_stage1/fast | 934.752 | 934.720 | +0.02% | 1776.68 / 1777.07 | 403727 | no substantial regression detected |
| self_host_stage2/fast | 4590.479 | 4590.374 | +0.03% | 1789.45 / 1789.45 | 82026 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
