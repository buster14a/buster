# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.816 | 6.857 | +0.85% | 46.43 / 46.43 | 729 | no substantial regression detected |
| tiny_startup/mir-stack | 6.820 | 6.856 | -0.00% | 46.35 / 46.30 | 729 | no substantial regression detected |
| tiny_startup/fast | 6.769 | 6.802 | +0.67% | 46.25 / 46.39 | 735 | no substantial regression detected |
| tiny_startup/quality | 6.829 | 6.853 | -0.03% | 46.44 / 46.30 | 730 | no substantial regression detected |
| large_function/none | 15.652 | 15.568 | -0.38% | 70.41 / 70.39 | 66098 | no substantial regression detected |
| large_function/mir-stack | 11.603 | 11.605 | +0.08% | 72.69 / 72.56 | 88671 | no substantial regression detected |
| large_function/fast | 11.697 | 11.698 | +0.19% | 72.48 / 72.52 | 87967 | no substantial regression detected |
| large_function/quality | 11.829 | 11.805 | -0.07% | 72.63 / 72.65 | 87167 | no substantial regression detected |
| many_functions/none | 10.075 | 10.067 | -0.62% | 60.57 / 60.51 | 203529 | no substantial regression detected |
| many_functions/mir-stack | 9.631 | 9.573 | -0.62% | 58.46 / 58.70 | 214046 | no substantial regression detected |
| many_functions/fast | 9.748 | 9.723 | -0.08% | 58.22 / 58.33 | 210742 | no substantial regression detected |
| many_functions/quality | 9.815 | 9.825 | +0.24% | 58.59 / 58.70 | 208554 | no substantial regression detected |
| symbol_table/none | 16.537 | 16.574 | +0.08% | 78.78 / 78.80 | 247437 | no substantial regression detected |
| symbol_table/mir-stack | 12.025 | 12.017 | +0.22% | 78.66 / 78.82 | 341273 | no substantial regression detected |
| symbol_table/fast | 12.059 | 12.078 | +0.26% | 78.62 / 78.76 | 339537 | no substantial regression detected |
| symbol_table/quality | 12.119 | 12.160 | -0.09% | 78.82 / 78.89 | 337240 | no substantial regression detected |
| control_flow/none | 32.313 | 32.344 | -0.09% | 112.78 / 112.67 | 102926 | no substantial regression detected |
| control_flow/mir-stack | 29.081 | 29.026 | -0.15% | 110.73 / 110.78 | 114692 | no substantial regression detected |
| control_flow/fast | 26.655 | 26.629 | +0.05% | 108.84 / 109.01 | 125012 | no substantial regression detected |
| control_flow/quality | 26.671 | 26.634 | -0.18% | 108.91 / 108.76 | 124993 | no substantial regression detected |
| backend_pressure/none | 71.897 | 71.871 | -0.04% | 193.06 / 193.05 | 267173 | no substantial regression detected |
| backend_pressure/mir-stack | 61.424 | 61.407 | -0.05% | 191.36 / 191.17 | 312698 | no substantial regression detected |
| backend_pressure/fast | 62.622 | 62.638 | +0.01% | 191.31 / 191.34 | 306554 | no substantial regression detected |
| backend_pressure/quality | 68.324 | 68.266 | +0.00% | 191.22 / 191.48 | 281282 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
