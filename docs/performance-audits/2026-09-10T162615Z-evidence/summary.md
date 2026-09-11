# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 14.174 | 14.200 | +1.67% | 23.67 / 23.63 | 352 | inconclusive |
| tiny_startup/mir-stack | 14.146 | 14.302 | +0.52% | 23.61 / 23.42 | 350 | inconclusive |
| tiny_startup/fast | 14.263 | 14.042 | -1.84% | 23.72 / 23.58 | 356 | inconclusive |
| tiny_startup/quality | 14.314 | 14.190 | +2.42% | 23.69 / 23.59 | 352 | inconclusive |
| large_function/none | 29.968 | 30.346 | +1.67% | 27.64 / 27.56 | 33909 | inconclusive |
| large_function/mir-stack | 22.579 | 22.804 | +0.46% | 29.77 / 29.64 | 45125 | no substantial regression detected |
| large_function/fast | 23.243 | 23.076 | -0.56% | 29.73 / 29.60 | 44591 | no substantial regression detected |
| large_function/quality | 23.120 | 23.226 | -0.07% | 29.99 / 29.85 | 44305 | inconclusive |
| many_functions/none | 22.068 | 21.833 | -1.31% | 30.32 / 30.27 | 93848 | no substantial regression detected |
| many_functions/mir-stack | 20.416 | 20.678 | +0.47% | 29.26 / 29.13 | 99094 | inconclusive |
| many_functions/fast | 20.472 | 20.739 | -0.45% | 29.22 / 29.08 | 98800 | no substantial regression detected |
| many_functions/quality | 20.556 | 20.474 | +0.84% | 29.25 / 29.14 | 100078 | inconclusive |
| symbol_table/none | 32.236 | 31.686 | +0.68% | 30.06 / 29.95 | 129427 | inconclusive |
| symbol_table/mir-stack | 24.344 | 24.138 | -0.97% | 30.86 / 30.80 | 169898 | inconclusive |
| symbol_table/fast | 23.670 | 23.782 | +0.06% | 30.72 / 30.58 | 172444 | inconclusive |
| symbol_table/quality | 24.183 | 24.186 | +0.55% | 30.84 / 30.73 | 169561 | inconclusive |
| control_flow/none | 61.810 | 61.566 | -0.75% | 41.13 / 41.04 | 54072 | inconclusive |
| control_flow/mir-stack | 55.585 | 55.470 | -0.77% | 39.82 / 39.69 | 60014 | inconclusive |
| control_flow/fast | 49.113 | 49.131 | -0.25% | 39.19 / 38.98 | 67757 | no substantial regression detected |
| control_flow/quality | 48.767 | 49.316 | +1.16% | 39.16 / 39.12 | 67503 | no substantial regression detected |
| backend_pressure/none | 134.577 | 136.736 | +1.86% | 57.58 / 57.46 | 140436 | inconclusive |
| backend_pressure/mir-stack | 111.025 | 117.011 | +3.42% | 57.65 / 57.52 | 164104 | inconclusive |
| backend_pressure/fast | 112.848 | 115.014 | -0.87% | 55.64 / 55.52 | 166954 | inconclusive |
| backend_pressure/quality | 124.733 | 126.335 | +0.49% | 55.88 / 55.71 | 151993 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **18**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
