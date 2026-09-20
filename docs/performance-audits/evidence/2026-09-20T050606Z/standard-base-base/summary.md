# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.824 | 18.871 | -0.66% | 52.77 / 52.78 | 265 | no substantial regression detected |
| large_function/fast | 55.425 | 55.295 | -0.13% | 149.23 / 149.19 | 148242 | no substantial regression detected |
| many_functions/fast | 40.726 | 40.782 | -0.09% | 125.13 / 125.12 | 401771 | no substantial regression detected |
| symbol_table/fast | 60.047 | 59.981 | -0.19% | 172.95 / 172.96 | 546386 | no substantial regression detected |
| control_flow/fast | 198.589 | 198.659 | -0.01% | 408.59 / 408.63 | 134024 | no substantial regression detected |
| backend_pressure/fast | 438.115 | 441.138 | +0.21% | 976.21 / 976.24 | 348198 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
