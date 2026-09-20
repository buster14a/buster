# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.926 | 19.068 | +0.69% | 52.76 / 52.80 | 262 | no substantial regression detected |
| large_function/fast | 55.395 | 55.539 | +0.29% | 149.19 / 149.23 | 147589 | no substantial regression detected |
| many_functions/fast | 40.716 | 40.849 | +0.16% | 125.14 / 125.09 | 401114 | no substantial regression detected |
| symbol_table/fast | 59.948 | 59.823 | -0.23% | 172.96 / 170.94 | 547831 | no substantial regression detected |
| control_flow/fast | 198.931 | 198.248 | -0.36% | 408.63 / 408.59 | 134302 | no substantial regression detected |
| backend_pressure/fast | 438.827 | 437.592 | -0.11% | 976.24 / 978.22 | 351016 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
