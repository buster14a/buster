# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 6.542 | 6.485 | -1.36% | 46.37 / 46.29 | 771 | no substantial regression detected |
| large_function/fast | 11.258 | 11.083 | -1.36% | 72.45 / 72.35 | 92843 | no substantial regression detected |
| many_functions/fast | 9.362 | 9.292 | -1.14% | 58.49 / 58.56 | 220522 | no substantial regression detected |
| symbol_table/fast | 11.624 | 11.521 | -0.79% | 78.88 / 78.64 | 355974 | no substantial regression detected |
| control_flow/fast | 24.952 | 24.631 | -1.31% | 108.59 / 108.81 | 135157 | no substantial regression detected |
| backend_pressure/fast | 59.894 | 59.548 | -0.74% | 185.42 / 185.17 | 322460 | no substantial regression detected |
| self_host_stage1/fast | 889.522 | 887.389 | -0.27% | 1752.64 / 1752.87 | 422396 | no substantial regression detected |
| self_host_stage2/fast | 4104.062 | 4105.772 | +0.14% | 1762.01 / 1762.01 | 91088 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
