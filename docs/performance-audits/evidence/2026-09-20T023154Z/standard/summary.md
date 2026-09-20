# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 19.155 | 19.221 | +0.93% | 52.82 / 52.91 | 260 | no substantial regression detected |
| large_function/fast | 55.838 | 55.605 | -0.37% | 149.26 / 149.31 | 147414 | no substantial regression detected |
| many_functions/fast | 40.819 | 41.046 | +0.47% | 125.20 / 125.14 | 399186 | no substantial regression detected |
| symbol_table/fast | 60.593 | 60.277 | -0.64% | 170.99 / 170.99 | 543706 | no substantial regression detected |
| control_flow/fast | 198.842 | 198.980 | +0.05% | 408.66 / 408.64 | 133807 | no substantial regression detected |
| backend_pressure/fast | 437.461 | 440.886 | +0.99% | 978.21 / 978.31 | 348394 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
