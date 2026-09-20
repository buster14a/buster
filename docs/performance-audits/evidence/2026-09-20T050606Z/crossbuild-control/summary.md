# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| large_function/fast | 55.672 | 55.680 | -0.12% | 149.20 / 149.18 | 147217 | diagnostic (guard disabled) |
| many_functions/fast | 40.936 | 41.099 | +0.46% | 125.11 / 125.09 | 398675 | diagnostic (guard disabled) |
| control_flow/fast | 197.029 | 198.213 | +0.69% | 408.67 / 408.59 | 134325 | diagnostic (guard disabled) |
| aggregate-abi/fast | 160.287 | 155.247 | -3.07% | 225.75 / 225.77 | 52774 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
