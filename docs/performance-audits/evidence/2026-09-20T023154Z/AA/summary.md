# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 19.019 | 19.151 | +0.16% | 52.88 / 52.82 | 261 | no substantial regression detected |
| large_function/fast | 55.837 | 55.821 | +0.08% | 149.28 / 149.26 | 146845 | no substantial regression detected |
| many_functions/fast | 40.768 | 40.801 | +0.03% | 125.22 / 125.19 | 401586 | no substantial regression detected |
| symbol_table/fast | 60.334 | 60.342 | +0.10% | 170.97 / 170.98 | 543123 | no substantial regression detected |
| control_flow/fast | 198.350 | 198.435 | +0.10% | 408.67 / 408.69 | 134175 | no substantial regression detected |
| backend_pressure/fast | 436.336 | 438.475 | -0.05% | 978.25 / 978.30 | 350310 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
