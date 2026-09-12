# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.805 | 6.831 | +1.54% | 46.21 / 46.52 | 732 | no substantial regression detected |
| tiny_startup/mir-stack | 6.894 | 6.858 | +1.29% | 46.48 / 46.28 | 729 | no substantial regression detected |
| tiny_startup/fast | 6.756 | 6.831 | +0.48% | 46.35 / 46.58 | 732 | no substantial regression detected |
| tiny_startup/quality | 6.819 | 6.842 | -0.16% | 46.57 / 46.39 | 731 | no substantial regression detected |
| large_function/none | 15.629 | 15.860 | +1.17% | 70.56 / 70.38 | 64880 | no substantial regression detected |
| large_function/mir-stack | 11.613 | 11.632 | +0.35% | 72.35 / 72.36 | 88459 | no substantial regression detected |
| large_function/fast | 11.718 | 11.769 | +0.17% | 72.35 / 72.40 | 87434 | no substantial regression detected |
| large_function/quality | 11.809 | 11.875 | +0.65% | 72.60 / 72.58 | 86653 | no substantial regression detected |
| many_functions/none | 10.090 | 10.105 | +0.15% | 60.45 / 60.49 | 202776 | no substantial regression detected |
| many_functions/mir-stack | 9.587 | 9.611 | +0.07% | 58.30 / 58.31 | 213190 | no substantial regression detected |
| many_functions/fast | 9.784 | 9.766 | -0.12% | 58.48 / 58.35 | 209815 | no substantial regression detected |
| many_functions/quality | 9.785 | 9.820 | +0.65% | 58.55 / 58.53 | 208646 | no substantial regression detected |
| symbol_table/none | 16.544 | 16.580 | +0.30% | 78.80 / 78.69 | 247353 | no substantial regression detected |
| symbol_table/mir-stack | 12.046 | 12.024 | -0.07% | 78.60 / 78.69 | 341064 | no substantial regression detected |
| symbol_table/fast | 12.065 | 12.056 | -0.13% | 78.71 / 78.71 | 340172 | no substantial regression detected |
| symbol_table/quality | 12.157 | 12.147 | -0.09% | 78.88 / 78.97 | 337601 | no substantial regression detected |
| control_flow/none | 32.341 | 32.570 | +0.74% | 112.68 / 112.76 | 102212 | no substantial regression detected |
| control_flow/mir-stack | 29.053 | 29.408 | +1.39% | 110.59 / 110.66 | 113202 | no substantial regression detected |
| control_flow/fast | 26.630 | 26.838 | +0.83% | 108.81 / 110.79 | 124039 | no substantial regression detected |
| control_flow/quality | 26.670 | 26.870 | +0.62% | 108.99 / 110.84 | 123894 | no substantial regression detected |
| backend_pressure/none | 71.913 | 74.454 | +3.55% | 193.18 / 195.14 | 257903 | no substantial regression detected |
| backend_pressure/mir-stack | 61.446 | 62.162 | +1.11% | 191.12 / 193.28 | 308903 | no substantial regression detected |
| backend_pressure/fast | 62.667 | 63.539 | +1.43% | 191.01 / 191.27 | 302209 | no substantial regression detected |
| backend_pressure/quality | 68.248 | 69.872 | +2.36% | 191.37 / 191.66 | 274818 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
