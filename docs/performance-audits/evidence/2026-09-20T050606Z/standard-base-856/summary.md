# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.971 | 18.942 | -0.79% | 52.75 / 52.78 | 264 | no substantial regression detected |
| large_function/fast | 55.340 | 55.317 | -0.01% | 149.19 / 149.19 | 148183 | no substantial regression detected |
| many_functions/fast | 40.863 | 41.057 | +0.68% | 125.12 / 125.14 | 399075 | no substantial regression detected |
| symbol_table/fast | 60.027 | 59.958 | -0.10% | 172.95 / 172.96 | 546601 | no substantial regression detected |
| control_flow/fast | 198.819 | 198.123 | -0.33% | 408.59 / 408.61 | 134386 | no substantial regression detected |
| backend_pressure/fast | 437.417 | 436.042 | -0.39% | 976.21 / 976.28 | 352264 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
