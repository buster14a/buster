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
  `tier1_emit_cfg_recipes`). `test-tier1-allocation` includes native execution
  and guarded tails; `benchmark-tier1-allocation` measures full preparation and
  allocation; `test-checker-driver`'s Tier 1 fixture runs every recipe through
  `--tier1` against clang. [The allocation guide](../../docs/tier1-allocation.md)
  records producer constraints and the corpora's spills with and without
  rematerialization. Use the shared 9700X lock per build/test/measurement and
  CPU 2 for timing.
