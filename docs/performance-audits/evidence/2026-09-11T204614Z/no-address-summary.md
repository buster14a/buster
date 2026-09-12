# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/fast | 18.887 | 18.685 | -2.21% | 23.38 / 23.48 | 268 | inconclusive |
| large_function/fast | 29.647 | 29.733 | +1.40% | 29.30 / 29.33 | 34608 | inconclusive |
| many_functions/fast | 26.212 | 26.670 | -0.30% | 28.95 / 29.01 | 76828 | inconclusive |
| symbol_table/fast | 29.509 | 30.224 | +0.33% | 30.44 / 30.52 | 135687 | inconclusive |
| control_flow/fast | 65.694 | 65.250 | +0.30% | 38.98 / 39.10 | 51020 | inconclusive |
| backend_pressure/fast | 154.906 | 151.350 | -0.85% | 55.81 / 55.84 | 126871 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **5**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
