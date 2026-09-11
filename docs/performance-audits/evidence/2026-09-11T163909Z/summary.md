# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 12.732 | 12.877 | +0.32% | 49.33 / 49.47 | 388 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 12.646 | 12.079 | -3.92% | 49.45 / 49.34 | 414 | diagnostic (guard disabled) |
| tiny_startup/fast | 12.884 | 12.838 | +1.49% | 49.43 / 49.40 | 389 | diagnostic (guard disabled) |
| tiny_startup/quality | 12.722 | 12.354 | -2.14% | 49.38 / 49.36 | 405 | diagnostic (guard disabled) |
| large_function/none | 14.144 | 14.451 | +1.09% | 51.52 / 51.34 | 9204 | diagnostic (guard disabled) |
| large_function/mir-stack | 13.070 | 12.820 | -3.01% | 51.42 / 51.38 | 10374 | diagnostic (guard disabled) |
| large_function/fast | 12.936 | 13.041 | +0.23% | 51.58 / 51.46 | 10198 | diagnostic (guard disabled) |
| large_function/quality | 12.806 | 13.372 | +2.77% | 51.44 / 51.49 | 9946 | diagnostic (guard disabled) |
| many_functions/none | 13.194 | 13.436 | +0.51% | 51.43 / 51.44 | 19130 | diagnostic (guard disabled) |
| many_functions/mir-stack | 13.732 | 13.333 | -2.33% | 51.39 / 51.39 | 19277 | diagnostic (guard disabled) |
| many_functions/fast | 13.145 | 13.027 | -1.14% | 51.44 / 51.41 | 19728 | diagnostic (guard disabled) |
| many_functions/quality | 13.413 | 13.580 | +1.32% | 51.42 / 51.49 | 18927 | diagnostic (guard disabled) |
| symbol_table/none | 14.763 | 14.832 | -0.09% | 53.37 / 53.38 | 34858 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 13.147 | 13.147 | +1.02% | 53.38 / 53.48 | 39326 | diagnostic (guard disabled) |
| symbol_table/fast | 12.797 | 13.555 | +3.98% | 53.46 / 53.40 | 38147 | diagnostic (guard disabled) |
| symbol_table/quality | 13.185 | 13.698 | +4.06% | 53.48 / 53.43 | 37745 | diagnostic (guard disabled) |
| control_flow/none | 18.681 | 18.562 | -0.87% | 61.46 / 61.50 | 22465 | diagnostic (guard disabled) |
| control_flow/mir-stack | 16.320 | 16.095 | -0.31% | 61.45 / 61.60 | 25908 | diagnostic (guard disabled) |
| control_flow/fast | 15.572 | 15.692 | +1.30% | 61.52 / 61.45 | 26574 | diagnostic (guard disabled) |
| control_flow/quality | 15.895 | 16.007 | -0.70% | 61.51 / 61.60 | 26051 | diagnostic (guard disabled) |
| backend_pressure/none | 27.245 | 27.208 | +0.08% | 77.48 / 77.47 | 88293 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 20.104 | 20.292 | +1.73% | 77.61 / 77.56 | 118373 | diagnostic (guard disabled) |
| backend_pressure/fast | 20.496 | 20.486 | +1.26% | 77.57 / 77.60 | 117248 | diagnostic (guard disabled) |
| backend_pressure/quality | 21.889 | 22.085 | +0.49% | 77.58 / 77.47 | 108760 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
