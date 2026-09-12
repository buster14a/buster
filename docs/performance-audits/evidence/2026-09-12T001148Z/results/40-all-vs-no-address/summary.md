# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 6.981 | 7.030 | -0.41% | 46.56 / 46.45 | 711 | no substantial regression detected |
| tiny_startup/mir-stack | 6.895 | 7.035 | +1.75% | 46.32 / 46.22 | 711 | no substantial regression detected |
| tiny_startup/fast | 7.052 | 6.921 | +1.13% | 46.39 / 46.59 | 722 | no substantial regression detected |
| tiny_startup/quality | 7.046 | 7.060 | +0.90% | 46.64 / 46.50 | 708 | no substantial regression detected |
| large_function/none | 15.856 | 15.793 | -0.09% | 70.53 / 70.39 | 65157 | no substantial regression detected |
| large_function/mir-stack | 11.827 | 11.827 | -0.13% | 72.25 / 72.43 | 87005 | no substantial regression detected |
| large_function/fast | 11.939 | 11.838 | -0.53% | 72.56 / 72.47 | 86922 | no substantial regression detected |
| large_function/quality | 12.002 | 12.015 | +0.51% | 72.39 / 72.73 | 85645 | no substantial regression detected |
| many_functions/none | 10.294 | 10.273 | +0.05% | 60.59 / 60.52 | 199455 | no substantial regression detected |
| many_functions/mir-stack | 9.865 | 9.818 | -0.53% | 58.35 / 58.33 | 208692 | no substantial regression detected |
| many_functions/fast | 9.893 | 9.959 | +0.80% | 58.41 / 58.52 | 205752 | no substantial regression detected |
| many_functions/quality | 10.042 | 10.035 | +0.01% | 58.37 / 58.67 | 204178 | no substantial regression detected |
| symbol_table/none | 16.736 | 16.695 | -0.23% | 78.67 / 78.75 | 245649 | no substantial regression detected |
| symbol_table/mir-stack | 12.212 | 12.177 | -0.67% | 78.50 / 78.56 | 336794 | no substantial regression detected |
| symbol_table/fast | 12.208 | 12.168 | -0.28% | 78.93 / 78.75 | 337018 | no substantial regression detected |
| symbol_table/quality | 12.265 | 12.245 | +0.46% | 78.72 / 78.82 | 334899 | no substantial regression detected |
| control_flow/none | 31.977 | 31.955 | +0.14% | 112.85 / 112.72 | 104177 | no substantial regression detected |
| control_flow/mir-stack | 28.774 | 28.725 | -0.04% | 110.60 / 110.56 | 115890 | no substantial regression detected |
| control_flow/fast | 26.163 | 26.178 | -0.22% | 108.64 / 108.77 | 127168 | no substantial regression detected |
| control_flow/quality | 26.113 | 26.121 | -0.03% | 108.76 / 108.88 | 127444 | no substantial regression detected |
| backend_pressure/none | 70.792 | 70.719 | -0.03% | 193.08 / 193.24 | 271526 | no substantial regression detected |
| backend_pressure/mir-stack | 60.295 | 60.080 | -0.28% | 191.14 / 191.23 | 319607 | no substantial regression detected |
| backend_pressure/fast | 61.523 | 61.446 | -0.11% | 191.22 / 191.36 | 312500 | no substantial regression detected |
| backend_pressure/quality | 67.095 | 67.001 | -0.25% | 191.30 / 191.27 | 286592 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
