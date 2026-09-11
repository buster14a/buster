# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.297 | 19.949 | +4.34% | 23.28 / 23.30 | 251 | diagnostic (guard disabled) |
| large_function/fast | 29.936 | 29.007 | -2.00% | 29.36 / 29.48 | 35475 | diagnostic (guard disabled) |
| many_functions/fast | 25.390 | 25.523 | -0.57% | 28.80 / 28.95 | 80280 | diagnostic (guard disabled) |
| symbol_table/fast | 29.612 | 29.632 | -1.17% | 30.40 / 30.44 | 138400 | diagnostic (guard disabled) |
| control_flow/fast | 66.401 | 64.521 | -0.19% | 38.76 / 38.88 | 51595 | diagnostic (guard disabled) |
| backend_pressure/fast | 159.101 | 155.384 | -4.01% | 55.07 / 55.21 | 123578 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
