# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.142 | 18.392 | -0.63% | 23.40 / 23.42 | 272 | no substantial regression detected |
| large_function/fast | 29.557 | 29.407 | +1.01% | 29.35 / 29.45 | 34995 | inconclusive |
| many_functions/fast | 26.547 | 25.668 | -0.13% | 28.94 / 28.93 | 79828 | inconclusive |
| symbol_table/fast | 29.842 | 29.971 | +1.53% | 30.37 / 30.52 | 136834 | inconclusive |
| control_flow/fast | 65.849 | 62.821 | -4.09% | 39.03 / 38.99 | 52992 | inconclusive |
| backend_pressure/fast | 155.168 | 151.092 | -4.45% | 55.77 / 55.30 | 127088 | inconclusive |

Confirmed regressions: **0**. Inconclusive cases: **5**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
