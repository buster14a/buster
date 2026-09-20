# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 19.345 | 19.355 | +0.03% | 52.76 / 52.82 | 258 | no substantial regression detected |
| large_function/fast | 60.500 | 57.949 | -4.46% | 147.19 / 145.31 | 141452 | no substantial regression detected |
| many_functions/fast | 41.167 | 41.352 | +0.56% | 125.11 / 127.18 | 396235 | no substantial regression detected |
| symbol_table/fast | 62.216 | 61.873 | -0.65% | 166.98 / 167.03 | 529684 | no substantial regression detected |
| control_flow/fast | 194.091 | 194.955 | +0.44% | 392.64 / 392.62 | 136570 | no substantial regression detected |
| backend_pressure/fast | 458.294 | 459.526 | +0.34% | 980.24 / 980.28 | 334262 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
