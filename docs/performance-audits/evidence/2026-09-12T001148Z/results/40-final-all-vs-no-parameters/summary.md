# Compiler throughput comparison

Same-host paired A/B; two fixed rounds; all samples retained. Wall-time gate: **15% and 2 ms**. Peak-RSS gate: **20% and 16 MiB**. Each gate must pass its one-sided exact sign test in **both** rounds (nominal family-wise alpha 1%, Bonferroni across cases and gated metrics; independent pairs assumed).

| Workload / mode | Base wall (ms) | Candidate wall (ms) | Paired change | Base / candidate peak MiB | Candidate lines/s | Decision |
|---|---:|---:|---:|---:|---:|---|
| tiny_startup/none | 7.001 | 7.147 | +0.04% | 46.47 / 46.27 | 700 | no substantial regression detected |
| tiny_startup/mir-stack | 6.954 | 7.169 | +2.15% | 46.24 / 46.35 | 697 | no substantial regression detected |
| tiny_startup/fast | 7.063 | 7.038 | -1.16% | 46.28 / 46.30 | 710 | no substantial regression detected |
| tiny_startup/quality | 6.934 | 7.114 | +2.11% | 46.43 / 46.45 | 703 | no substantial regression detected |
| large_function/none | 16.125 | 16.145 | -0.09% | 70.70 / 70.54 | 63737 | no substantial regression detected |
| large_function/mir-stack | 12.061 | 12.025 | -0.22% | 72.46 / 72.49 | 85573 | no substantial regression detected |
| large_function/fast | 12.111 | 12.191 | +0.13% | 72.41 / 72.52 | 84407 | no substantial regression detected |
| large_function/quality | 12.174 | 12.236 | +0.10% | 72.52 / 72.65 | 84095 | no substantial regression detected |
| many_functions/none | 10.454 | 10.355 | +0.36% | 60.63 / 60.51 | 197877 | no substantial regression detected |
| many_functions/mir-stack | 10.088 | 10.017 | -0.23% | 58.55 / 58.58 | 204555 | no substantial regression detected |
| many_functions/fast | 10.176 | 10.110 | -0.89% | 58.52 / 58.47 | 202674 | no substantial regression detected |
| many_functions/quality | 10.173 | 10.113 | -0.20% | 58.72 / 58.75 | 202618 | no substantial regression detected |
| symbol_table/none | 16.906 | 16.904 | +0.15% | 78.59 / 78.70 | 242601 | no substantial regression detected |
| symbol_table/mir-stack | 12.418 | 12.371 | +0.04% | 78.49 / 78.60 | 331509 | no substantial regression detected |
| symbol_table/fast | 12.420 | 12.404 | -0.28% | 78.69 / 78.72 | 330626 | no substantial regression detected |
| symbol_table/quality | 12.484 | 12.485 | +0.18% | 78.90 / 78.75 | 328484 | no substantial regression detected |
| control_flow/none | 32.433 | 32.506 | +0.05% | 112.71 / 112.73 | 102410 | no substantial regression detected |
| control_flow/mir-stack | 29.562 | 29.589 | -0.21% | 110.85 / 110.80 | 112508 | no substantial regression detected |
| control_flow/fast | 26.890 | 26.828 | -0.33% | 108.65 / 108.65 | 124088 | no substantial regression detected |
| control_flow/quality | 26.842 | 26.831 | +0.01% | 109.00 / 108.76 | 124072 | no substantial regression detected |
| backend_pressure/none | 72.125 | 72.181 | -0.03% | 193.45 / 193.09 | 266027 | no substantial regression detected |
| backend_pressure/mir-stack | 61.680 | 61.665 | -0.00% | 191.19 / 191.11 | 311394 | no substantial regression detected |
| backend_pressure/fast | 62.881 | 62.864 | -0.01% | 191.02 / 191.21 | 305454 | no substantial regression detected |
| backend_pressure/quality | 68.636 | 68.505 | -0.10% | 191.18 / 191.28 | 280300 | no substantial regression detected |

Confirmed regressions: **0**. Inconclusive cases: **0**. An inconclusive result is a warning, not evidence of equivalence; it is not silently rerun until green.

OS fault and context-switch medians in `summary.json` are diagnostic only and never affect either guard. Hardware-counter and instrumented-allocation replays are separate from these uninstrumented timing trials. See `telemetry.csv`, `commands.jsonl`, and `capabilities.jsonl`. Missing observations are `NA`/`null`, not zero. Generated workloads count source definitions; self-host stages report actual lexed lines and leave function throughput unavailable.
