# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 18.010 | 18.318 | +1.06% | 21.79 / 21.81 | 273 | diagnostic (guard disabled) |
| tiny_startup/mir-stack | 17.800 | 18.209 | +1.02% | 21.88 / 21.72 | 275 | diagnostic (guard disabled) |
| tiny_startup/fast | 18.116 | 18.370 | +3.26% | 21.82 / 21.79 | 272 | diagnostic (guard disabled) |
| tiny_startup/quality | 17.226 | 17.878 | +1.68% | 21.55 / 21.55 | 280 | diagnostic (guard disabled) |
| large_function/none | 38.336 | 38.454 | -1.62% | 25.41 / 25.37 | 26761 | diagnostic (guard disabled) |
| large_function/mir-stack | 25.774 | 25.795 | +2.11% | 27.30 / 27.13 | 39892 | diagnostic (guard disabled) |
| large_function/fast | 25.427 | 25.548 | -0.68% | 27.09 / 27.13 | 40277 | diagnostic (guard disabled) |
| large_function/quality | 25.652 | 26.445 | +0.83% | 27.36 / 27.21 | 38911 | diagnostic (guard disabled) |
| many_functions/none | 27.363 | 27.377 | +1.36% | 28.22 / 28.17 | 74844 | diagnostic (guard disabled) |
| many_functions/mir-stack | 23.596 | 23.597 | +0.33% | 27.06 / 27.04 | 86834 | diagnostic (guard disabled) |
| many_functions/fast | 24.288 | 24.394 | +2.21% | 27.04 / 27.05 | 84003 | diagnostic (guard disabled) |
| many_functions/quality | 23.454 | 24.043 | +1.25% | 27.29 / 27.05 | 85223 | diagnostic (guard disabled) |
| symbol_table/none | 38.745 | 38.978 | -1.77% | 27.36 / 27.39 | 105216 | diagnostic (guard disabled) |
| symbol_table/mir-stack | 26.687 | 27.008 | +1.66% | 28.39 / 28.39 | 151844 | diagnostic (guard disabled) |
| symbol_table/fast | 26.234 | 26.660 | +1.12% | 27.89 / 28.27 | 153826 | diagnostic (guard disabled) |
| symbol_table/quality | 26.608 | 27.377 | +3.89% | 28.27 / 28.27 | 149798 | diagnostic (guard disabled) |
| control_flow/none | 84.111 | 83.017 | -0.42% | 40.10 / 40.19 | 40100 | diagnostic (guard disabled) |
| control_flow/mir-stack | 63.536 | 64.715 | +0.54% | 38.98 / 38.72 | 51441 | diagnostic (guard disabled) |
| control_flow/fast | 53.238 | 54.342 | +3.59% | 38.36 / 38.35 | 61260 | diagnostic (guard disabled) |
| control_flow/quality | 55.927 | 55.212 | +1.57% | 38.38 / 38.20 | 60295 | diagnostic (guard disabled) |
| backend_pressure/none | 164.250 | 168.618 | +4.52% | 61.39 / 61.46 | 113879 | diagnostic (guard disabled) |
| backend_pressure/mir-stack | 106.656 | 113.035 | +5.08% | 61.58 / 61.69 | 169877 | diagnostic (guard disabled) |
| backend_pressure/fast | 106.736 | 109.437 | +3.20% | 59.18 / 59.18 | 175464 | diagnostic (guard disabled) |
| backend_pressure/quality | 127.044 | 126.331 | -0.35% | 59.70 / 59.43 | 151999 | diagnostic (guard disabled) |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
