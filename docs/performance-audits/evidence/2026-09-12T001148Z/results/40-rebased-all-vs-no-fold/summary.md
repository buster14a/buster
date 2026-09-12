# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.164 | 7.232 | +0.86% | 46.25 / 46.51 | 691 | no substantial regression detected |
| tiny_startup/mir-stack | 7.147 | 7.226 | +1.20% | 46.26 / 46.33 | 692 | no substantial regression detected |
| tiny_startup/fast | 7.123 | 7.098 | +0.17% | 46.41 / 46.46 | 704 | no substantial regression detected |
| tiny_startup/quality | 7.129 | 7.083 | +1.14% | 46.46 / 46.46 | 706 | no substantial regression detected |
| large_function/none | 16.125 | 16.303 | +1.44% | 70.60 / 70.43 | 63118 | no substantial regression detected |
| large_function/mir-stack | 12.113 | 12.103 | -0.11% | 72.49 / 72.45 | 85018 | no substantial regression detected |
| large_function/fast | 12.212 | 12.191 | +0.16% | 72.54 / 72.49 | 84407 | no substantial regression detected |
| large_function/quality | 12.288 | 12.217 | -0.13% | 72.44 / 72.65 | 84229 | no substantial regression detected |
| many_functions/none | 10.499 | 10.602 | +0.57% | 60.92 / 60.52 | 193272 | no substantial regression detected |
| many_functions/mir-stack | 10.128 | 10.102 | -0.72% | 58.35 / 58.49 | 202830 | no substantial regression detected |
| many_functions/fast | 10.233 | 10.236 | -0.32% | 58.56 / 58.41 | 200169 | no substantial regression detected |
| many_functions/quality | 10.226 | 10.302 | -0.05% | 58.64 / 58.48 | 198890 | no substantial regression detected |
| symbol_table/none | 16.938 | 16.986 | +0.10% | 78.73 / 78.48 | 241439 | no substantial regression detected |
| symbol_table/mir-stack | 12.512 | 12.473 | -0.42% | 78.60 / 78.59 | 328803 | no substantial regression detected |
| symbol_table/fast | 12.486 | 12.448 | -0.15% | 78.54 / 78.54 | 329442 | no substantial regression detected |
| symbol_table/quality | 12.623 | 12.564 | -0.50% | 78.83 / 78.85 | 326410 | no substantial regression detected |
| control_flow/none | 32.568 | 32.557 | -0.02% | 112.65 / 112.70 | 102252 | no substantial regression detected |
| control_flow/mir-stack | 29.320 | 29.542 | +0.79% | 110.67 / 110.78 | 112685 | no substantial regression detected |
| control_flow/fast | 26.941 | 26.753 | -0.63% | 108.79 / 108.80 | 124436 | no substantial regression detected |
| control_flow/quality | 26.998 | 26.701 | -1.03% | 108.66 / 108.95 | 124676 | no substantial regression detected |
| backend_pressure/none | 72.042 | 73.632 | +2.25% | 193.19 / 197.15 | 260782 | no substantial regression detected |
| backend_pressure/mir-stack | 61.539 | 61.502 | -0.09% | 191.32 / 190.99 | 312216 | no substantial regression detected |
| backend_pressure/fast | 62.857 | 62.483 | -0.49% | 191.22 / 189.11 | 307318 | no substantial regression detected |
| backend_pressure/quality | 68.445 | 69.020 | +0.75% | 191.26 / 189.24 | 278210 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
