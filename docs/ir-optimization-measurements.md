# Optimization stage cost harness

`ir_optimize_passes_benchmark.c` extends the separate SCCP/DCE/CFG harness with
complete public calls to value numbering, loop motion and private-field
splitting. Include it after `ir_optimize_benchmark.c` and the GVN, LICM and
SROA test files. The command entry is
`run_ir_optimize_passes_benchmark(argc, argv)`, with the command in `argv[0]`.

```
benchmark-ir-optimize-passes [--pass all|gvn|licm|sroa]
    [--instructions N] [--fields 1..4096] [--samples 1..21]
    [--only scalar|vector] [--after-scalar MS]
```

Each pass has three frozen shapes. GVN uses small and configurable arithmetic
DAGs containing duplicates, plus cyclic congruent phis. LICM uses a no-loop
function, nested loops, and a configurable invariant chain. SROA uses two
fields, conditional stores, and a configurable array of scalar fields.
Each shape has its own deterministic seed, so selecting one pass or changing
timing controls does not change its input. `--instructions` defaults to 4096
and controls the large DAG/chain; `--fields` defaults to 64 and supports the
future aggregate-size policy measurement.

Before timing, both paths run on the same input. LICM and SROA must agree
exactly, including scalar-slot metadata. GVN is checked with the independent
bounded interpreter and must remove at least as many instructions as the
scoped scalar expression table. The large SROA array also feeds both upstream
SSA promoters outside timing; the fixture's closed-form returned value is
checked after promotion. All public calls include input validation, CFG/use
and dominance construction, analysis, rewriting and final verification.
The SROA timing is the field-splitting public call; subsequent SSA promotion
is deliberately a separate stage and is not included in that number.

Each sample is calibrated to at least 2 ms. The default 21 samples alternate
which path runs first. `--only` measures one path, while `--after-scalar`
performs scalar stage work before each vector sample. The scalar label refers
to the scalar algorithm option; shared verification and other infrastructure
may themselves use vector instructions. Timing controls do not establish
hardware frequency isolation by themselves.

Output includes input/output instruction counts, removed instructions or
moved instructions or created fields, output allocation bytes, retained
analysis bytes, median/minimum/maximum nanoseconds, fenced TSC ticks and input
instructions per second. TSC ticks are not core cycles. Retained analysis
bytes exclude temporary verifier/dominator workspace and are not a peak
memory measurement. Slot compaction can leave reserved arena storage behind;
output allocation bytes include that retained storage.

Local sanitizer smoke checks exercise all nine cases and timing controls.
They are diagnostic correctness checks, not performance evidence. No target
measurements or speedup claims are recorded here. Acceptance still needs the
exclusive 9700X host, CPU 2, repeated controlled samples, pass-by-pass runtime
effects on `speedtest1` and the corpora, complete pass-ablation differential
testing, and a measured pass order and size/profitability policy.
