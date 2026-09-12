# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 17.710 | 18.136 | +0.80% | 23.27 / 23.39 | 276 | diagnostic (guard disabled) |
| large_function/fast | 28.646 | 28.416 | -0.69% | 29.33 / 29.36 | 36213 | diagnostic (guard disabled) |
| many_functions/fast | 25.573 | 24.949 | -2.51% | 28.81 / 28.88 | 82127 | diagnostic (guard disabled) |
| symbol_table/fast | 29.821 | 30.371 | +0.54% | 30.33 / 30.39 | 135032 | diagnostic (guard disabled) |
| control_flow/fast | 62.600 | 62.719 | -0.67% | 38.75 / 38.79 | 53086 | diagnostic (guard disabled) |
| backend_pressure/fast | 153.856 | 158.850 | +0.87% | 55.28 / 55.24 | 120883 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
