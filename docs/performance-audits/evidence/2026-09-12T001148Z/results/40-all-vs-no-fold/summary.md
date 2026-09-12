# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.954 | 7.079 | +2.51% | 46.43 / 46.35 | 706 | no substantial regression detected |
| tiny_startup/mir-stack | 6.953 | 6.965 | +0.33% | 46.32 / 46.28 | 718 | no substantial regression detected |
| tiny_startup/fast | 6.846 | 6.992 | +2.12% | 46.14 / 46.32 | 715 | no substantial regression detected |
| tiny_startup/quality | 6.944 | 6.926 | +0.45% | 46.64 / 46.63 | 722 | no substantial regression detected |
| large_function/none | 15.947 | 16.112 | +1.28% | 70.37 / 70.66 | 63865 | no substantial regression detected |
| large_function/mir-stack | 11.844 | 11.821 | -0.21% | 72.52 / 72.39 | 87051 | no substantial regression detected |
| large_function/fast | 11.997 | 11.929 | +0.15% | 72.50 / 72.51 | 86260 | no substantial regression detected |
| large_function/quality | 12.088 | 12.017 | -0.27% | 72.79 / 72.58 | 85625 | no substantial regression detected |
| many_functions/none | 10.317 | 10.355 | +0.35% | 60.65 / 60.39 | 197876 | no substantial regression detected |
| many_functions/mir-stack | 9.951 | 9.912 | -0.17% | 58.62 / 58.46 | 206715 | no substantial regression detected |
| many_functions/fast | 9.948 | 9.959 | -0.47% | 58.41 / 58.48 | 205748 | no substantial regression detected |
| many_functions/quality | 10.109 | 10.039 | -0.69% | 58.55 / 58.56 | 204111 | no substantial regression detected |
| symbol_table/none | 16.762 | 16.743 | +0.24% | 78.74 / 78.63 | 244945 | no substantial regression detected |
| symbol_table/mir-stack | 12.275 | 12.242 | -0.14% | 78.65 / 78.58 | 334981 | no substantial regression detected |
| symbol_table/fast | 12.293 | 12.294 | -0.01% | 78.59 / 78.71 | 333584 | no substantial regression detected |
| symbol_table/quality | 12.397 | 12.342 | -0.47% | 78.83 / 78.83 | 332276 | no substantial regression detected |
| control_flow/none | 32.099 | 32.146 | -0.04% | 112.67 / 112.65 | 103559 | no substantial regression detected |
| control_flow/mir-stack | 28.887 | 28.980 | +0.41% | 110.64 / 110.74 | 114870 | no substantial regression detected |
| control_flow/fast | 26.226 | 26.090 | -0.49% | 108.79 / 108.78 | 127598 | no substantial regression detected |
| control_flow/quality | 26.280 | 26.101 | -0.91% | 108.81 / 108.99 | 127543 | no substantial regression detected |
| backend_pressure/none | 70.989 | 72.485 | +2.01% | 193.21 / 197.21 | 264908 | no substantial regression detected |
| backend_pressure/mir-stack | 60.338 | 60.094 | -0.28% | 191.11 / 191.17 | 319531 | no substantial regression detected |
| backend_pressure/fast | 61.657 | 61.234 | -0.67% | 191.07 / 189.23 | 313583 | no substantial regression detected |
| backend_pressure/quality | 67.168 | 67.755 | +0.94% | 191.35 / 189.23 | 283402 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
