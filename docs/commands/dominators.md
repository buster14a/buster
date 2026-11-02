# dominators

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `dominators.c` analyzes borrowed CFG columns into dominance intervals,
  optional frontier bitsets and natural loops. `test-dominators` checks both
  paths against independent graph definitions; `benchmark-dominators` includes
  analysis and synthetic producer/consumer measurements. [The dominator
  guide](../../docs/dominators.md) records memory limits and pending corpus evidence.
  Take the shared 9700X lock per build/test/measurement and pin timing to CPU 2.
