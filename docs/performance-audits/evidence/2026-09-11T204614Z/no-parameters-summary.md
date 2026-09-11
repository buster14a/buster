# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.346 | 19.010 | -0.62% | 23.38 / 23.40 | 263 | inconclusive |
| large_function/fast | 29.710 | 30.400 | +1.81% | 29.38 / 29.31 | 33860 | inconclusive |
| many_functions/fast | 26.496 | 26.939 | +0.62% | 29.15 / 29.04 | 76062 | inconclusive |
| symbol_table/fast | 29.998 | 30.439 | -0.29% | 30.38 / 30.42 | 134732 | inconclusive |
| control_flow/fast | 66.851 | 68.018 | +2.51% | 39.00 / 39.15 | 48943 | inconclusive |
| backend_pressure/fast | 157.729 | 156.403 | -0.76% | 55.96 / 55.85 | 122772 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **5**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
