# tier1-cfg

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `tier1_cfg.c` allocates over a CFG (#140): per-block ranges from `dataflow.c`'s
  liveness with phi operands on their edges, hinted (`hint_values`) to keep one
  register, then each edge's parallel move, placed at a block's end or start or
  on the split edge, with a temporary slot for slot cycles. `tier1_cfg_verify`
  is its independent checker, which simulates each block and edge, and checks
  each piece of a range cut at a split where its split's moves leave it. Its
  classes, and `tier1_allocate.c`'s, are general, vector and AVX-512's opmask
  registers (#179: k1 to k7 for values, a class with k0 refused, k0 the class's
  scratch, every mask clobbered by a call). `test-tier1-cfg` runs fixtures and random SSA over random CFGs of the
  three classes with mutants; `test-tier1-cfg-source` adapts every function `-c`
  compiles of real C, lowered and promoted, through a machine model
  (`tier1_cfg_ir.c`) and checks both paths' allocations, and with `--simd`
  allocates #732's SIMD functions as lowered, their masks in the opmask class.
  The allocation guide's "Allocation over a CFG (#140)" and "The opmask class
  (#179)" have the contract and what is pending.
