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

## Register preferences (#1216)

The allocator and CFG adapter accept optional soft `preferred_registers` masks.
They select free registers from that set first and use other allowed registers
when it is full. `test-tier1-allocation` checks preferences, hints, required uses
and fixed restrictions through both production paths and the independent scan.
The production CFG emitter uses them to prefer callee-saved registers across
calls without restricting loop-carried values to that small register set.
`test-tier1-emit` covers an eight-value loop and hostile caller clobbers with and
without a frame pointer. Dynamic edge-traffic and native runtime acceptance
remain the workload measurements specified by #1216.
