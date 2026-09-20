# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 19.009 | 19.070 | +0.91% | 52.74 / 52.78 | 262 | no substantial regression detected |
| large_function/fast | 55.342 | 55.639 | +0.69% | 149.23 / 149.23 | 147324 | no substantial regression detected |
| many_functions/fast | 40.671 | 41.006 | +0.71% | 125.11 / 125.10 | 399575 | no substantial regression detected |
| symbol_table/fast | 59.784 | 60.119 | +0.55% | 172.95 / 170.95 | 545137 | no substantial regression detected |
| control_flow/fast | 198.850 | 198.848 | +0.04% | 408.56 / 408.55 | 133896 | no substantial regression detected |
| backend_pressure/fast | 438.000 | 435.811 | -0.49% | 976.24 / 978.24 | 352451 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
