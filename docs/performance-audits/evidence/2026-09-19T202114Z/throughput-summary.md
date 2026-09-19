# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 20.747 | 21.067 | +1.33% | 46.16 / 46.29 | 237 | no substantial regression detected |
| tiny_startup/mir-stack | 20.742 | 20.985 | +0.81% | 46.04 / 46.30 | 238 | no substantial regression detected |
| tiny_startup/fast | 20.476 | 20.760 | +1.69% | 46.03 / 46.29 | 241 | no substantial regression detected |
| tiny_startup/quality | 20.866 | 20.900 | +0.74% | 46.16 / 46.36 | 239 | inconclusive |
| large_function/none | 46.611 | 46.284 | -0.60% | 70.22 / 70.49 | 22232 | no substantial regression detected |
| large_function/mir-stack | 33.283 | 32.694 | -1.04% | 72.27 / 72.54 | 31474 | no substantial regression detected |
| large_function/fast | 34.029 | 34.008 | -1.18% | 72.29 / 72.43 | 30258 | no substantial regression detected |
| large_function/quality | 33.759 | 33.782 | -0.42% | 72.64 / 72.54 | 30460 | no substantial regression detected |
| many_functions/none | 30.881 | 31.009 | -0.58% | 58.28 / 58.42 | 66078 | no substantial regression detected |
| many_functions/mir-stack | 29.063 | 29.188 | +0.10% | 58.38 / 58.29 | 70200 | no substantial regression detected |
| many_functions/fast | 29.506 | 29.640 | -0.28% | 58.16 / 58.46 | 69128 | no substantial regression detected |
| many_functions/quality | 29.814 | 29.579 | -0.49% | 58.54 / 58.55 | 69271 | no substantial regression detected |
| symbol_table/none | 48.808 | 49.975 | +2.00% | 76.57 / 76.59 | 82061 | no substantial regression detected |
| symbol_table/mir-stack | 34.998 | 36.280 | +4.84% | 76.51 / 76.51 | 113037 | no substantial regression detected |
| symbol_table/fast | 35.370 | 36.727 | +4.23% | 76.79 / 76.70 | 111661 | no substantial regression detected |
| symbol_table/quality | 35.122 | 36.614 | +3.99% | 76.54 / 76.67 | 112007 | no substantial regression detected |
| control_flow/none | 99.615 | 105.962 | +6.55% | 110.34 / 112.76 | 31417 | no substantial regression detected |
| control_flow/mir-stack | 81.254 | 87.751 | +8.37% | 106.68 / 110.71 | 37937 | no substantial regression detected |
| control_flow/fast | 75.457 | 81.261 | +7.84% | 106.38 / 108.58 | 40967 | no substantial regression detected |
| control_flow/quality | 75.059 | 81.413 | +8.53% | 106.54 / 108.70 | 40890 | no substantial regression detected |
| backend_pressure/none | 214.588 | 211.507 | -1.14% | 190.87 / 191.03 | 90787 | no substantial regression detected |
| backend_pressure/mir-stack | 169.300 | 167.778 | -1.45% | 186.97 / 187.08 | 114449 | no substantial regression detected |
| backend_pressure/fast | 174.065 | 173.444 | -1.11% | 186.94 / 187.10 | 110710 | no substantial regression detected |
| backend_pressure/quality | 189.804 | 187.950 | -1.35% | 187.14 / 187.35 | 102165 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **1**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
