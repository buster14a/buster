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
  A late non-phi, non-recipe definition with no existing value or register hint
  follows its earlier body range's opening (`tier1_cfg_definition_hints`, #2553);
  the allocator's guarded recipe yielding can keep that register at the definition too.
  `tier1_cfg_verify` is its independent checker, which simulates each block and edge, and checks
  each piece of a range cut at a split where its split's moves leave it. Its
  classes, and `tier1_allocate.c`'s, are general, vector and AVX-512's opmask
  registers (#179: k1 to k7 for values, a class with k0 refused, k0 the class's
  scratch, every mask clobbered by a call). Edge moves read each live value's location at the predecessor's exit and
  the successor's entry from one forward sweep of that value's intervals (#1399), and emission resolves a position
  from the first interval overlapping its block when that column fits. Validation fills the position-to-block
  column that allocation and emission then read (to 2^20 positions), and checks each use against its definition's
  block start with one compare instead of a search per use and definition: on the 9700X, sqlite3.c's single-lane
  `--tier1` compile took 0.8% fewer cycles, and validation's lines fell from 0.78% of them to 0.18%, the most a vector
  pass over the uses could take off
  ([record](https://github.com/buster14a/buster-performance/tree/9e1b1d03ac4ceffdfaf513a9d04112699aeaaf44/docs/measurements/tier1-cfg-validation-1399-2026-11-13)).
  The verifier and the certificate keep the
  binary search and check both results against it; `test-tier1-cfg` compares the two validations over every
  placement of a definition and two uses in a small CFG, and runs fixtures and random SSA over random CFGs of the
  three classes with mutants (three generated cases in four share phi webs' slots, and a mutant gives two values whose
  slot fragments overlap one slot), and #2553's two: a join whose dominating critical edge moves nothing, and a phi web
  around a loop that keeps one slot; #2784's loop whose body precedes its header, whose path for a character moves
  nothing; #2834's loop whose latch lies between its body and its header, whose entry edge moves nothing; #2553's
  current slots (`tier1_cfg_current_slots`): a loop whose body reloads a value its header keeps in its slot, whose back
  edge stores nothing, refused by both checkers without the body's assertion and with one the entry doesn't give, and in
  the generated cases a slot asserted current that an edge doesn't bring (a mutant both refuse); every allocation's continuations (#2774) are checked against a twin that recomputes them from the intervals
  alone; `test-tier1-cfg-source` adapts every function `-c`
  compiles of real C, lowered and promoted, through a machine model
  (`tier1_cfg_ir.c`) and checks each allocation, and with `--simd`
  allocates #732's SIMD functions as lowered, their masks in the opmask class.
  The allocation guide's "Allocation over a CFG (#140)" and "The opmask class
  (#179)" have the contract and what is pending.

## Liveness storage (#1384)

The forward spill-slot analysis also groups up to 512 values. Its worklist
propagates losses of current slots through the blocks each group touches.
A 10,245-block fixture checks the exact sorted lists for 4,098 candidates,
including loop joins with spills on one or both incoming arms. It gives the
analysis 768 KiB of scratch and checks rollback on exhaustion.

Phi operands passed only by their defining block, with no ordinary use outside
it, are excluded from global liveness. Their intervals still reach that block's
exit, and the forward boundary sweep retains their edge-move sources. Remote
phi occurrences enter global liveness, including when the same value also has
local occurrences. Each global value's phi occurrences replace the fifth
`passed` matrix.

The global values are solved in groups of 512 dense ids
(`tier1_cfg_live_sets`). Each group's use and def rows are seeded and solved by
`dataflow_liveness`, one vector row per block. The group's phi operands are then
added to the out rows, and the rows are counted into, then filled into, each
block's ascending live-in and live-out lists. The four matrices therefore take
`blocks * 256` bytes however many values are global. A function of at most 512
global values makes the single solve it made before. Liveness is per value, so a
group's rows are exactly its columns of one solve over every value, and
allocations are unchanged. While counting, the group pass also records three
facts a vector at a time: whether each block's live-in values are live out of
the block before it (the continuation test), whether each live-in entry is live
out of its block, and whether each global value is live out of its home. That
way no reader searches the lists. The entry-state sizes, each value's block
list, the later-successor entry test, the boundary sweep and the edge moves read
the lists directly.

With more than one group, each group is solved again for the fill rather than
kept, and every group's dense solve sweeps every block, `groups * blocks` rows.

### Sparse groups

Where those rows would pass 65,536 (`BUSTER_TIER1_CFG_SPARSE_ROWS`), every group
is solved by `tier1_cfg_live_group_sparse` instead, and no dense rows are
allocated. A group gives a 256-byte row (def, in, out and pending, one 512-bit
vector each) only to its values' homes, their uses' blocks, the blocks passing
them to phis and the blocks they turn out to be live into, with a generation
stamp per block. Seeded as the dense rows are, a worklist sends each block's
newly live values to every predecessor as one OR, and queues a predecessor whose
in row grows. This is the least solution of the same equations, which the order
the blocks are taken in doesn't change, so the lists, the three facts and the
allocations are the dense solve's. Counting and filling read only the touched
rows. Its work follows the live pairs and the predecessors they cross. A group
whose values are live everywhere touches every block, and on 1,000 values live
across 1,000 blocks the worklist took 4% longer than the dense groups, which the
bound keeps there. No function of the frozen SQLite or musl sources passes the
bound, so they solve as before. `sparse_liveness` reports the form; for it
`liveness_bytes` is the most rows one group touched, 256 bytes each, and
`liveness_iterations` the worklist's block visits.

`test-tier1-cfg` allocates every fixture and generated case again with the
sparse groups forced (`tier1_cfg_allocate_sparse`), requiring every field
`tier1_cfg_differs` compares to match but the solves' own counters, and checks
every generated case's sparse lists against the per-value walk below. #1353's
function of scoped loops (`tier1_cfg_test_loops`: each loop's header has its
counter's and the sum's phis, its latch their next values) at 1, 255 and 1,200
loops compares both forms' lists with the walk and runs as whole cases; at
3,000 and 6,000 loops, past the bound, it must be sparse, match the walk, and
visit blocks and touch rows at most 2.2 times as much for twice the loops.
`test-tier1-cfg-source` allocates every function again with the sparse groups
forced and fails on any difference. Breaking the sparse solve (no phi-edge OUT,
a dropped predecessor, no continuation narrowing, a wrong group base, no
phi-edge USE) fails these checks.

`test-tier1-cfg` checks the local/global partition, duplicate local occurrences,
remote occurrences and uses, self-phis, undefined operands, sibling edges,
spills, rematerialization and fixed clobbers in all three register classes.
Small fixtures run the allocator, independent verifier, certificate and
mutation checks. The dispatch-entry fixture (#2782) checks that a handler a
`goto` and a dispatch's multiway branch both enter starts its phi in the
dispatch's register for the phi's operand. The late-definition fixture (#2553)
keeps a body's register when an unused recipe occupies it at the definition,
and needs no repair on their edge. Protected-page rows check the definition
hint's empty inputs, vector tails and divergent interval searches. Existing
operand, tie, interval and final call-register hints retain priority, including
call preferences propagated through tied results. Different legal register masks
at the body and definition remain hard constraints in all three classes.
Wide joins of 8, 65, 513 and 8,193 predecessors must prepare
within 8 MiB of scratch with zero or one global value, and every local operand
must keep exactly its home-tail interval. A ladder of 255, 511, 512, 1,100 and
20,000 rungs uses each rung's value up to three rungs later, passes it to the
next rung's phi from both predecessors, and loops on every fifth rung. That
gives 256 to 20,001 global values, around and across the 512-value group
boundary. Each must prepare within 32 MiB of scratch with one group's matrices,
where the old ones took 410 MB at 20,000 rungs. Its live lists must equal a
per-value walk from each use back to the definition. Every generated case's
lists are checked against the same walk, and the ladders up to 1,100 rungs run
as whole cases through the checker and the certificate. These are capacity and
semantic checks, not timing or RSS measurements.

`tier1_cfg_edge_ordinals` maps each successor edge to the source's ordinal in
the target's original predecessor row in `O(blocks + edges)`. Allocation keeps
this column with the preparation whenever phis exist. Tests compare it against
the original rows, including shuffled rows, self edges and empty graphs, and
check rollback at each allocation failure. The mapping removes a repeated
predecessor search at large joins without changing phi operand associations.

`cross_block_values` counts genuinely nonlocal values, `edge_local_values`
counts excluded phi operands, `liveness_bytes` counts one group's four matrices
(or, sparse, its most touched rows), `live_entries` counts the entries of the
live-in and live-out lists, and `liveness_iterations` counts the sweeps of every
group's first solve (or, sparse, the worklist's block visits).

The measurements are from a cloud VM with AVX-512 (Release, `--tier1
--tier1-strict -c`), not the 9700X, so they are indicative only. They use `{ int
t = g(a + i); if (a > i) h(t); }` repeated N times. Each statement adds two
blocks and one global value, so main `b7d0b5ed`'s matrices were `blocks ×
values`:

| N | main peak RSS | main wall | grouped peak RSS | grouped wall |
| ---: | ---: | ---: | ---: | ---: |
| 8,000 | 222 MB | 214 ms | 166 MB | 175 ms |
| 16,000 | 551 MB | 591 ms | 315 MB | 399 ms |
| 32,000 | 1,570 MB | 1,547 ms | 612 MB | 972 ms |
| 64,000 | 5,091 MB | 5,642 ms | 1,207 MB | 2,482 ms |

Peak RSS now grows 1.9 to 2.0 times per doubling of N. Wall time still grows 2.3
to 2.6 times: 64,000 statements make 125 groups over 128,001 blocks, and about half
of 30 stack samples fell in `dataflow_liveness` or the group scan. Every object
was byte-identical to main's, and so was `main.c` compiled with `--tier1`. The
issue's `||` chain needs no group: 8,000 operands peak at 51 MB on both.

With the sparse groups, on another cloud VM with AVX-512 (Release, so again
indicative only), the same statements against main `47440912`:

| N | main wall | sparse wall | peak RSS, both |
| ---: | ---: | ---: | ---: |
| 8,000 | 206 ms | 197 ms | 186 MB |
| 16,000 | 460 ms | 408 ms | 334 MB |
| 32,000 | 1,143 ms | 868 ms | 631 MB |
| 64,000 | 2,835 ms | 1,627 ms | 1,226 MB |

Wall time now grows 1.9 to 2.1 times per doubling. `benchmark-tier1-cfg-source`
times the allocation alone: on #1353's scoped loops (one function of N `for`
loops, each with its own `i`) main took 25, 84 and 306 ms at 4,000, 8,000 and
16,000 loops, the sparse groups 12, 29 and 59 ms; on its `MAX` statement
expressions, 32, 115 and 394 ms against 17, 38 and 80 ms. Below the bound both
are the dense solve, within noise. Every object was byte-identical to main's,
`sqlite3.c`'s too.

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
step, and refuses an input with fixed ranges.
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
calls (`tier1_cfg_test_two_orders`): a random preferred problem
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
the whole Tier 1 pipeline has other costs. On the 9700X, main `b7a95eed`'s
`tier0` stage under `--tier1 --tier1-strict` takes 5.2, 10.4 and 19.3 ms for
the issue's 4,000-, 8,000- and 16,000-case switch and label table (about
1.3 µs per case; 1.85x to 1.99x per doubling, against the issue's 2.9x to
3.1x). The generator, recipe and raw results are in
[buster-performance's #1356 record](https://github.com/buster14a/buster-performance/tree/8640815ecb5a3b0f9483d88e530cd78540d55dfa/docs/measurements/tier1-cfg-1356/results-2026-11-13-claude).

