# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/quality | 43.612 | 43.316 | -1.72% | 24.49 / 24.54 | 115 | diagnostic (guard disabled) |
| large_function/quality | 68.459 | 70.392 | +3.83% | 30.69 / 30.58 | 14619 | diagnostic (guard disabled) |
| many_functions/quality | 60.392 | 61.186 | -1.28% | 30.07 / 30.12 | 33489 | diagnostic (guard disabled) |
| symbol_table/quality | 69.632 | 68.506 | -4.36% | 31.66 / 31.66 | 59865 | diagnostic (guard disabled) |
| control_flow/quality | 148.679 | 151.832 | +1.23% | 40.92 / 40.96 | 21927 | diagnostic (guard disabled) |
| backend_pressure/quality | 363.870 | 359.724 | -0.72% | 57.20 / 57.14 | 53380 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
