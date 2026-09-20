# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.939 | 18.872 | -0.45% | 52.77 / 52.80 | 265 | no substantial regression detected |
| large_function/fast | 55.351 | 55.208 | -0.25% | 149.20 / 149.19 | 148475 | no substantial regression detected |
| many_functions/fast | 40.882 | 41.041 | +0.22% | 125.12 / 125.10 | 399239 | no substantial regression detected |
| symbol_table/fast | 59.840 | 59.938 | +0.03% | 172.94 / 172.96 | 546778 | no substantial regression detected |
| control_flow/fast | 198.658 | 198.698 | +0.03% | 408.63 / 408.60 | 133998 | no substantial regression detected |
| backend_pressure/fast | 437.575 | 440.626 | +0.59% | 976.23 / 976.23 | 348599 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
