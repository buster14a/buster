# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.965 | 18.972 | +1.03% | 23.37 / 23.38 | 264 | inconclusive |
| large_function/fast | 31.333 | 32.445 | +2.84% | 29.32 / 29.51 | 31721 | inconclusive |
| many_functions/fast | 27.293 | 27.725 | +1.76% | 28.95 / 29.16 | 73926 | inconclusive |
| symbol_table/fast | 32.738 | 32.025 | +1.81% | 30.39 / 30.52 | 128057 | inconclusive |
| control_flow/fast | 70.248 | 73.887 | +0.13% | 39.03 / 39.16 | 45056 | inconclusive |
| backend_pressure/fast | 157.036 | 161.276 | +1.60% | 55.83 / 56.34 | 119063 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **6**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
