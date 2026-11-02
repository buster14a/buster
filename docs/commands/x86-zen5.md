# x86-zen5

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/x86_zen5.c` supplies the partial operand-sensitive Zen 5
  observation table and scalar/AVX-512 cost-column lookups. `test-x86-zen5`
  checks unknowns, guard-page tails and generated microbenchmark loops;
  `measure-x86-zen5` measures instruction cases on the approved 9700X;
  `benchmark-x86-zen5 [--only scalar|vector] [--after-scalar MS]` times lookups.
  Each build/test/measurement takes the shared host lock separately. Pin
  measurements to CPU 2. [The model guide](../../docs/x86-zen5.md) records what is
  measured and the coverage, pipe data and real consumers still pending for #181.
