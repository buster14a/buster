# dominators

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `dominators.c` analyzes borrowed CFG columns into dominance intervals,
  optional frontier bitsets and natural loops; its tree solver sweeps reverse
  postorder with 64-bit dominator windows ([what is
  data-parallel](../../docs/dominators.md#what-is-data-parallel)). `test-dominators` checks
  it against independent graph definitions and, on the default seed, frozen
  digests of its largest graphs; `benchmark-dominators` includes analysis and
  synthetic producer/consumer measurements. [The dominator
  guide](../../docs/dominators.md) records memory limits and pending corpus evidence.
  Take the shared 9700X lock per build/test/measurement and pin timing to CPU 2.
