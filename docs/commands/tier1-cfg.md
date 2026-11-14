# tier1-cfg

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `tier1_cfg.c` allocates over a CFG (#140): per-block ranges from `dataflow.c`'s
  liveness with phi operands on their edges, hinted (`hint_values`, and
  `hint_intervals` for a dispatch loop's handlers' entries, #1216) to keep one
  register, then each edge's parallel move, placed at a block's end or start or
  on the split edge, with a temporary slot for slot cycles. Ranges and phis take
  their values' places at the exit of a dominating block or chosen predecessor
  (`tier1_cfg_anchors`, exit hints, #2553). With `slot_widths`,
  a phi web's spilled values share one slot where none overlaps another, so an
  edge moves nothing for an operand already in its phi's slot (#1216).
  `tier1_cfg_verify` is its independent checker, which simulates each block and edge, and checks
  each piece of a range cut at a split where its split's moves leave it. Its
  classes, and `tier1_allocate.c`'s, are general, vector and AVX-512's opmask
  registers (#179: k1 to k7 for values, a class with k0 refused, k0 the class's
  scratch, every mask clobbered by a call). Edge moves read each live value's location at the predecessor's exit and
  the successor's entry from one forward sweep of that value's intervals (#1399), and emission resolves a position
  from the first interval overlapping its block when that column fits. The verifier and the certificate keep the
  binary search and check both results against it. `test-tier1-cfg` runs fixtures and random SSA over random CFGs of the
  three classes with mutants (three generated cases in four share phi webs' slots, and a mutant gives two values whose
  slot fragments overlap one slot), and #2553's two: a join whose dominating critical edge moves nothing, and a phi web
  around a loop that keeps one slot; `test-tier1-cfg-source` adapts every function `-c`
  compiles of real C, lowered and promoted, through a machine model
  (`tier1_cfg_ir.c`) and checks both paths' allocations, and with `--simd`
  allocates #732's SIMD functions as lowered, their masks in the opmask class.
  The allocation guide's "Allocation over a CFG (#140)" and "The opmask class
  (#179)" have the contract and what is pending.

## Edge-local phi storage (#1384)

Phi operands passed only by their defining block, with no ordinary use outside
it, are excluded from global liveness. Their intervals still reach that block's
exit, and the forward boundary sweep retains their edge-move sources. Remote
phi occurrences enter global liveness, including when the same value also has
local occurrences. The four dense matrices retain the existing scalar/vector
solver; two phi scans replace the fifth `passed` matrix.

`test-tier1-cfg` checks the local/global partition, duplicate local occurrences,
remote occurrences and uses, self-phis, undefined operands, sibling edges,
spills, rematerialization and fixed clobbers in all three register classes.
Small fixtures run the allocator, independent verifier, certificate and
mutation checks. Wide joins of 8, 65, 513 and 8,193 predecessors must prepare
within 8 MiB of scratch with zero or one global value, and every local operand
must keep exactly its home-tail interval. These are capacity and semantic
checks, not timing or RSS measurements.

`tier1_cfg_edge_ordinals` maps each successor edge to the source's ordinal in
the target's original predecessor row in `O(blocks + edges)`. Allocation keeps
this column with the preparation whenever phis exist. Tests compare it against
the original rows, including shuffled rows, self edges and empty graphs, and
check rollback at each allocation failure. The mapping removes a repeated
predecessor search at large joins without changing phi operand associations.

`cross_block_values` counts genuinely nonlocal values, `edge_local_values`
counts excluded phi operands, and `liveness_bytes` counts the four dense
matrices only. The general nonlocal case remains dense; see the allocation
guide for its storage formula and scope.

## One allocation for two problems (#2538)

`tier1_cfg_allocate` is three functions in a row: `tier1_cfg_prepare`
(validation to the intervals, its working state left on the scratch arena in a
`Tier1CfgPrepared`), `tier1_cfg_assign` (one `tier1_allocate` over those
intervals for a problem's registers and column) and `tier1_cfg_finish` (lookup
columns, slot sharing, the splits' and edges' moves); `tier1_cfg_release` gives
the scratch back. Without fixed ranges the intervals serve every problem over
the same values: `tier1_cfg_column` writes another problem's column (each
value's run its allowed set, its definition's interval its bound registers),
after `tier1_cfg_input_valid`'s two checks on a value's registers, 16 values a
step with a scalar twin, and refuses an input with fixed ranges.
`tier1_cfg_order_prepare` and `tier1_cfg_order_run` are the CFG consumer's
order: the intervals, then the preferred problem (`Tier1CfgPreference`: its
registers and each value's set, to which `tier1_cfg_preferred` adds a bound
definition's registers) assigned with the plain problem's verdict inside its
call (`tier1_cfg_assign_verdict`, `tier1_allocate_verdict`), the definitions
freed and the preferred problem assigned again where the verdict is a
constraint, and the standing allocation finished once; the plain problem is
assigned whole only where the preferred allocation isn't taken. Without the
preference's `verdict`, or where none was taken, the plain problem is assigned
whole first. `tier1_cfg_differs` compares two allocations field by field.

`tier1_allocate_verdict` takes another problem's verdict over the same
intervals inside one assignment: per class, nothing where that problem would
color the class; else its scan over the events at positions below the last end
of an interval whose registers are fewer than its class's, or over every event
where the class has fewer registers than the most required uses at one
position. Then the scans' columns go back for the assignment's own.

`test-tier1-cfg` runs each case as two problems, in one call (with the verdict
inside, and with the plain problem first) and in two whole `tier1_cfg_allocate`
calls, on both paths (`tier1_cfg_test_two_orders`): a random preferred problem
(the general class widened, each value's set its own, the new registers or
both, and in a case of four one required value given no register), with the
definitions unbound, bound at random and bound in clashing pairs, two values
required at one position given one register, which drives the constraint
path; with fixed ranges another column must be refused, and the case runs
without them. The verdicts, the problems left and the standing allocations
must agree, and the checker must accept it. `test-tier1-cfg-source` does the
same for every function of real C with the verdict inside, unbound and in
clashing pairs, a required use added at each definition so that one can be
bound. Mutants that drop the column's bound registers, the constraint retry,
the verdict's prefix or its reset of the hints fail them.

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

