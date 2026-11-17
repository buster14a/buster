# tier1-allocation

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `tier1_allocate.c` and `tier1_rematerialize.c` supply bounded interval
  allocation, required-use reload splitting, parallel moves and baseline-x86
  rematerialization: constants, symbols' addresses (from rip, or their GOT
  slots) and frame objects', the recipes' costs from the Zen 5 model where it
  has them. The CFG consumer recomputes them through the copies the typed
  stream puts around addresses, and leaves out definitions nothing reads (#178,
  `tier1_emit_cfg_recipes`). `test-tier1-allocation` includes native execution,
  guarded tails, and constrained hint traces for unused recipes, simultaneous
  register claims and stored-value continuations. `benchmark-tier1-allocation`
  measures full preparation and allocation; `test-checker-driver`'s Tier 1
  fixture runs every recipe through `--tier1` against clang.
  [The allocation guide](../../docs/tier1-allocation.md)
  records producer constraints and the corpora's spills with and without
  rematerialization. Its clobbers (#2574), where a call evicts a recomputable
  value from a caller-saved register, have their traces, refusals and random
  properties in `tier1_allocate_clobbers_test.c`, and a recipe that gives its
  register to a value its hint names (#2618), one that waits for its read
  (#2618, #2735), a range that waits in its value's slot for its next required
  use (#2768) but not where its hint names another value's range (#2784), and a
  victim that continues in its slot and a range following an interval evicted
  where it starts (#2774: `continuation_intervals`, which the random inputs take
  with required uses too) and a range of its own value named to another value's
  recipe, which takes the recipe's register only (#2834) have their traces in
  `tier1_allocate_hints_test.c`, as does a range's alternate (#2734:
  `alternate_intervals`, taken where its interval's value still holds the
  register and it is free), which the random inputs and their reference model
  too; avoided registers (#2767: `avoided_registers`, a range live across a fixed
  register's sequence) have their traces on the scan and the common path and their
  random properties in `tier1_allocate_avoided_test.c`; `test-tier1-emit` checks
  which values the CFG consumer finds read on every iteration of their loops
  (`tier1_hot_recipes_test.c`). Use the shared 9700X lock per build/test/measurement and
  CPU 2 for timing.
