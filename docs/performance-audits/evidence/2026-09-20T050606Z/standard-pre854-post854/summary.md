# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 19.056 | 18.952 | -0.25% | 52.76 / 52.78 | 264 | no substantial regression detected |
| large_function/fast | 55.751 | 55.471 | -0.61% | 149.19 / 149.18 | 147771 | no substantial regression detected |
| many_functions/fast | 40.857 | 40.934 | +0.14% | 125.12 / 125.11 | 400280 | no substantial regression detected |
| symbol_table/fast | 60.305 | 59.865 | -0.91% | 170.99 / 170.97 | 547449 | no substantial regression detected |
| control_flow/fast | 198.105 | 197.977 | -0.08% | 408.64 / 408.60 | 134485 | no substantial regression detected |
| backend_pressure/fast | 435.707 | 437.277 | +0.41% | 978.27 / 978.23 | 351269 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
