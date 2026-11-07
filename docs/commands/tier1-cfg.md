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
  scratch, every mask clobbered by a call). Edge moves read each live value's location at the predecessor's exit and
  the successor's entry from one forward sweep of that value's intervals (#1399), and emission resolves a position
  from the first interval overlapping its block when that column fits. The verifier and the certificate keep the
  binary search and check both results against it. `test-tier1-cfg` runs fixtures and random SSA over random CFGs of the
  three classes with mutants; `test-tier1-cfg-source` adapts every function `-c`
  compiles of real C, lowered and promoted, through a machine model
  (`tier1_cfg_ir.c`) and checks both paths' allocations, and with `--simd`
  allocates #732's SIMD functions as lowered, their masks in the opmask class.
  The allocation guide's "Allocation over a CFG (#140)" and "The opmask class
  (#179)" have the contract and what is pending.

## Dispatch interval boundaries (#1216)

Adjacent blocks share intervals only when they form a straight-line CFG chain:
the earlier block has one successor and the later block has that block as its
only predecessor. The existing phi, liveness and fixed-range boundary checks
still apply. Branches, joins and loop headers get independent intervals, so
register pressure in a sibling handler cannot become their incoming spill
state merely through layout order. Edge moves carry values between the ranges.

`test-tier1-cfg` covers dispatch handlers with local pressure and clobbers,
joins, back edges and retained straight-line merging, through both allocator
paths and the existing verifier and certificate oracles. These fixtures check
allocation behavior; the issue's dynamic speedtest1/five-script edge-traffic
target and 9700X runtime requirement still need workload measurements.

## Edge validation and emission indexing (#1356)

`tier1_cfg_input_valid` checks both complete offset arrays before any edge-array
access, transposes successor edges into the predecessor ranges, and consumes
each source exactly once per target. Both rows may be unsorted; duplicates in
either view and mismatched views are rejected. This is O(blocks + edges), with
two block-sized and one edge-sized temporary uint32_t columns; validation
restores scratch on success and failure. The caller's predecessor ordering and
its phi operand association remain unchanged.

`tier1_cfg_single_predecessor_edges` takes a validated, immutable input and
writes a disjoint `block_count`-entry column owned by the emitter's scratch.
Each entry is the sole incoming edge's successor-CSR index, or `NONE` for a
block with zero or several predecessors. One edge sweep builds it, and
`tier1_emit_cfg_records` reads it when placing successor-entry moves. The
column survives until record emission finishes and is then reclaimed with the
emitter's scratch; allocation failure keeps the existing emission failure path.

`test-tier1-cfg` includes unordered rows, duplicate/missing edges, invalid
indices and intermediate offsets, self edges, empty edge arrays, and a
16,384-way dispatch and join. Existing allocation fixtures cover phi moves,
critical edges, loops and all register classes through both allocation paths.
These source changes remove the three degree-squared searches from #1356;
the whole Tier 1 pipeline has other costs. Locked 9700X scaling measurements
on the issue's switch and label-table inputs remain necessary before claiming
its wall-time acceptance or a measured speedup.

