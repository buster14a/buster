# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.068 | 7.215 | +1.41% | 46.32 / 46.38 | 693 | no substantial regression detected |
| tiny_startup/mir-stack | 7.121 | 7.087 | +0.89% | 46.31 / 46.24 | 706 | no substantial regression detected |
| tiny_startup/fast | 7.180 | 7.223 | -0.30% | 46.18 / 46.34 | 692 | no substantial regression detected |
| tiny_startup/quality | 7.186 | 7.213 | +1.03% | 46.57 / 46.35 | 693 | no substantial regression detected |
| large_function/none | 16.185 | 16.149 | +0.07% | 70.38 / 70.62 | 63718 | no substantial regression detected |
| large_function/mir-stack | 11.888 | 11.908 | +0.30% | 72.41 / 72.34 | 86410 | no substantial regression detected |
| large_function/fast | 12.017 | 12.021 | +0.24% | 72.86 / 72.41 | 85600 | no substantial regression detected |
| large_function/quality | 12.173 | 12.167 | -0.51% | 72.73 / 72.47 | 84572 | no substantial regression detected |
| many_functions/none | 10.469 | 10.488 | -0.18% | 60.60 / 60.53 | 195373 | no substantial regression detected |
| many_functions/mir-stack | 10.024 | 10.044 | +0.42% | 58.38 / 58.51 | 204008 | no substantial regression detected |
| many_functions/fast | 10.032 | 10.119 | +1.22% | 58.62 / 58.37 | 202482 | no substantial regression detected |
| many_functions/quality | 10.194 | 10.193 | +0.32% | 58.74 / 58.67 | 201019 | no substantial regression detected |
| symbol_table/none | 16.837 | 16.862 | +0.16% | 78.71 / 78.58 | 243216 | no substantial regression detected |
| symbol_table/mir-stack | 12.318 | 12.322 | -0.01% | 78.85 / 78.67 | 332828 | no substantial regression detected |
| symbol_table/fast | 12.432 | 12.371 | -0.39% | 78.68 / 78.57 | 331496 | no substantial regression detected |
| symbol_table/quality | 12.450 | 12.447 | -0.20% | 78.71 / 78.76 | 329490 | no substantial regression detected |
| control_flow/none | 31.863 | 31.838 | -0.01% | 112.48 / 112.73 | 104561 | no substantial regression detected |
| control_flow/mir-stack | 28.773 | 28.783 | +0.24% | 110.69 / 110.75 | 115659 | no substantial regression detected |
| control_flow/fast | 26.096 | 26.063 | -0.39% | 108.70 / 108.74 | 127731 | no substantial regression detected |
| control_flow/quality | 26.086 | 26.060 | +0.02% | 108.81 / 108.84 | 127746 | no substantial regression detected |
| backend_pressure/none | 72.061 | 72.140 | +0.09% | 197.18 / 197.11 | 266175 | no substantial regression detected |
| backend_pressure/mir-stack | 59.848 | 59.913 | -0.04% | 191.00 / 191.05 | 320499 | no substantial regression detected |
| backend_pressure/fast | 60.949 | 61.009 | +0.05% | 189.19 / 189.24 | 314740 | no substantial regression detected |
| backend_pressure/quality | 67.512 | 67.536 | -0.16% | 189.35 / 189.33 | 284323 | no substantial regression detected |
| self_host_stage1/fast | 937.119 | 937.122 | +0.04% | 1776.80 / 1776.93 | 402852 | no substantial regression detected |
| self_host_stage2/fast | 4603.971 | 4602.241 | -0.09% | 1793.45 / 1793.46 | 81847 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
