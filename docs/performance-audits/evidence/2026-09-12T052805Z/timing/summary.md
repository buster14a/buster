# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/quality | 17.303 | 18.536 | +6.81% | 25.49 / 25.58 | 270 | diagnostic (guard disabled) |
| large_function/quality | 26.191 | 26.160 | +1.56% | 31.63 / 31.84 | 39335 | diagnostic (guard disabled) |
| many_functions/quality | 24.018 | 24.978 | -0.45% | 31.22 / 30.99 | 82032 | diagnostic (guard disabled) |
| symbol_table/quality | 26.747 | 26.562 | +1.59% | 32.74 / 32.72 | 154417 | diagnostic (guard disabled) |
| control_flow/quality | 53.641 | 56.331 | +5.24% | 41.91 / 42.14 | 59115 | diagnostic (guard disabled) |
| backend_pressure/quality | 122.510 | 124.470 | +1.79% | 58.23 / 58.16 | 154300 | diagnostic (guard disabled) |
| self_host_stage1/fast | 2873.174 | 2904.122 | +0.66% | 988.50 / 988.36 | 129795 | diagnostic (guard disabled) |
| self_host_stage2/fast | 10131.314 | 10466.539 | +4.22% | 1000.42 / 1000.44 | 35923 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
