# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 19.052 | 19.133 | -0.09% | 52.78 / 52.80 | 261 | no substantial regression detected |
| large_function/fast | 55.909 | 56.047 | +0.14% | 149.24 / 149.25 | 146253 | no substantial regression detected |
| many_functions/fast | 40.908 | 40.933 | -0.01% | 125.13 / 125.14 | 400289 | no substantial regression detected |
| symbol_table/fast | 60.442 | 60.154 | -0.46% | 170.99 / 170.97 | 544815 | no substantial regression detected |
| control_flow/fast | 198.489 | 198.473 | +0.03% | 408.71 / 408.64 | 134149 | no substantial regression detected |
| backend_pressure/fast | 436.396 | 441.831 | +0.93% | 978.33 / 978.28 | 347649 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
