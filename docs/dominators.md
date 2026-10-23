# Dominators, frontiers and natural loops

`src/buster/compiler/dominators.c` implements #174's entry-rooted CFG analysis.
It accepts the predecessor and successor CSR columns produced by #131's
`ir_build_cfg`, without copying them into another graph representation. The
analysis supplies dominance intervals for the IR verifier, frontier bitsets
for #134, and natural-loop membership for #139 and #140. These consumers must
explicitly integrate the interface; the existence of this module does not
establish their acceptance or source-corpus coverage.

The implementation and fixtures are original. The scalar algorithm follows
the algorithm described by Cooper, Harvey and Kennedy in
[A Simple, Fast Dominance Algorithm](https://www.cs.tufts.edu/comp/150FP/archive/keith-cooper/dom14.pdf).
The loop definitions agree with the published
[Cornell compiler notes](https://www.cs.cornell.edu/courses/cs412/2008sp/lectures/lec29.pdf)
and [LLVM's loop terminology](https://llvm.org/docs/LoopTerminology.html).
No implementation, external fixture or generated source table is imported.

## Producer and consumer contract

Include `dominators.c` after `lane.c`. `DominatorCfg` borrows the complete
function's two CSR views:

| Field | Meaning and logical extent |
| --- | --- |
| `block_count` | Number of compact u32 block IDs, `[0, block_count)` |
| `edge_count` | Number of edges in each CSR edge column |
| `entry` | Root block; `BUSTER_DOMINATOR_NONE` for an empty CFG |
| `successor_offsets`, `predecessor_offsets` | `block_count + 1` u32 offsets; first zero, last `edge_count` |
| `successors`, `predecessors` | `edge_count` u32 block IDs |

Offset columns are monotone. Unsorted edge lists and duplicate edges are
accepted, but both CSR views must describe the same multiset, including
multiplicity. The linear validation pass constructs one temporary transpose
and checks that agreement. #131 deduplicates targets, so its phi predecessor
semantics remain unchanged. Both columns use 4-byte element alignment and
stride, with no readable padding and no vector-alignment requirement. Empty
graphs have no edges and may supply null column pointers; nonempty offset
columns are mandatory, and zero-edge columns may be null.

The producer finishes the whole CFG before calling the analysis. Its block
IDs stay stable, arrays remain immutable throughout the call, and no incomplete
batch is retained across a dependency. The borrowed input can be discarded
after the call: the output contains no input pointers. The caller owns the
input, output arena and scratch arena; the two arenas must be distinct and
nonoverlapping, and input/result objects must not overlap unused arena space.

```c
DominatorOptions options = dominators_options();
DominatorStatus status = dominators_analyze(&function_arena, &scratch, &cfg, options, &analysis);
```

The call returns `DOMINATOR_OK`, `DOMINATOR_INVALID`,
`DOMINATOR_NO_MEMORY`, or `DOMINATOR_MATRIX_LIMIT`. Invalid columns, unknown
options and an invalid entry are errors. Counts/indices are u32; products and
allocation extents are u64, checked by the arena allocator. With u32 blocks,
the simultaneously live matrices fit in u64 arithmetic even before the byte
budget is enforced. Failure leaves the result object and the output arena's
length unchanged. Scratch is restored on every outcome. Output lives until
the caller releases or rewinds its arena. Recompute after any CFG change;
there is no incremental maintenance or stale-result detection.

`dominators_options()` requests frontiers and loops, selects `DOMINATOR_AUTO`,
and limits simultaneously live bitset storage to 64 MiB. Callers may request
only `BUSTER_DOMINATOR_FRONTIERS`, only `BUSTER_DOMINATOR_LOOPS`, both, or zero
for tree-only analysis. Tree-only CHK storage is linear in blocks and edges.
The two quadratic output families are optional; a large sparse CFG never
silently allocates an unlimited frontier or loop-body matrix.

`max_matrix_bytes` is a memory limit, **not a measured performance threshold**.
`AUTO` selects CHK when the dominator matrix plus its reachable row exceed the
budget, and selects CSR loop worklists when retained output plus temporary
predecessor rows would exceed it. Explicit `VECTOR` returns a matrix-limit
error instead of either fallback. `SCALAR` never constructs the dominator or
predecessor matrices. Requested output matrices must fit in every mode;
otherwise the entire call fails. Zero permits tree-only scalar analysis and
loop analysis with no natural loops. Arena exhaustion is a distinct failure.

All output bitset rows contain `row_words` u64s, rounded up to a multiple of
eight, and start at a 64-byte boundary. Bits are original block IDs; inactive
tail bits are zero. The padding is allocated, readable and writable, separate
from the logical block count. No kernel reads past the row. Matrices are
disjoint; consumers must preserve their zero tails. Empty outputs have null
matrix pointers and zero rows. The host executes AVX-512 instructions directly,
without runtime ISA dispatch; this does not change generated-program ISA.

## Results

Only blocks reachable from `entry` participate. Incoming edges from unreachable
blocks have no effect on reachable dominance or natural loops. The entry's
immediate dominator is itself, including when it has incoming reachable edges.

| Output | Consumer contract |
| --- | --- |
| `idom` | One u32 per block; entry is self, unreachable is `BUSTER_DOMINATOR_NONE` |
| `rpo`, `rpo_index` | Reachable RPO in the first `reachable_count` entries; unused entries and unreachable inverse entries are `NONE` |
| `tree_offsets`, `tree_children` | Children CSR; logical child count `reachable_count - 1`, sorted by block ID within each parent |
| `preorder`, `subtree_end` | Half-open subtree intervals; reachable blocks have nonempty intervals, unreachable blocks have `[0, 0)` |
| `frontiers` | Optional block-major bitset rows |
| `loop_header`, `loop_parent`, `loop_size` | Optional loop columns; IDs follow ascending header ID; root loops have parent `NONE` |
| `loop_bodies` | One optional bitset row per loop, including its header |
| `loop_depth`, `block_loop` | Optional per-block nesting count and innermost loop; nonmembers have zero and `NONE` |
| `irreducible`, `loop_irreducible` | Optional byte flags; see the conservative SCC rule below |

`tree_children` reserves `block_count` elements; only the logical count above
is initialized and readable as output. For an empty CFG all output columns
are null and all counts are zero. No dominance query is true for an unreachable
block, even against itself. `dominators_dominates`,
`dominators_frontier_contains` and `dominators_loop_contains` reject
out-of-range IDs and query those representations directly. `outputs` records
which optional families were requested; a missing family is not evidence
that its mathematical sets are empty.

The interval columns can be borrowed directly as #131's `IrDominance.enter`
and `leave`; its unreachable contract is also `[0, 0)`. No transpose or
per-consumer interval copy is needed. Phi uses occur at the corresponding
predecessor's end, as #131's verifier specifies. An instruction before another
in the same block still needs the verifier's instruction-order check.

For loop consumers, a backedge's source is a latch. A header's predecessors
inside its body are its latches; predecessors outside the body are its entries.
This module does not create preheaders or prove that moving an instruction is
safe. LICM must require a suitable unique outside predecessor, dominance,
instruction semantics and noninterference, and must reject
`loop_irreducible[loop] != 0`. Loop block ordering can start from RPO, use
membership/parents to group reducible loops, and retain RPO in flagged regions.

## Algorithms and bounded storage

An iterative DFS from entry produces RPO and DFS ancestry intervals. Both
solvers run in that order until stable. CHK intersects processed predecessor
idom chains by their RPO indices. The vector solver starts reachable nonentry
rows at the reachable universe, intersects reachable predecessor rows with
`vpandq`, inserts the block's bit, and tests changes with vector masks. Entry
stays a singleton, and unreachable rows stay zero. One vector covers up to
512 blocks; larger rows use as many eight-word chunks as allocated. Vector
popcounts determine each set's size; the strict dominator with the largest
set is the immediate dominator. `iterations` includes the final stable sweep.

Counting, prefix sums and filling produce the children CSR. An explicit tree
stack assigns intervals for constant-time dominance queries. Frontier
construction walks each reachable edge's source up to the target's immediate
dominator. For incoming edges to entry, it includes the root itself; this
also handles a single self-loop without assuming a join has two predecessors.

Backedges are edges whose target dominates their source. All latches at one
header contribute to one natural loop. The scalar path walks predecessor CSR
with a mark-before-push worklist, stopping at the header. The vector path
constructs reachable predecessor rows once, expands a frontier by ORing those
rows, and masks visited nodes before the next expansion. It uses two extra
rows, clears every padding bit, and shares the memory budget with retained
frontiers and loop bodies. Both return identical bodies.

The smallest strict containing natural loop is the parent. Loops sharing a
header are merged first; natural loops with different headers are nested or
disjoint. Membership counts give per-block depth. There is no recursion and
no stack grows past the block count. The loop-parent scan is quadratic in the
number of headers; the measurements must include that cost.

A reverse DFS walk assigns SCCs. A DFS retreating edge whose target does not
dominate its source proves irreducibility; the module conservatively flags
every block of that edge's full SCC, and every natural loop touching it.
This deliberately excludes some legal motion in reducible subloops of such
an SCC. It is a safe optimization boundary, not a minimal irreducible-cycle
decomposition. Unreachable SCCs and acyclic exits remain unflagged.

The public `output_bytes` and `scratch_peak_bytes` include alignment padding;
`matrix_peak_bytes` counts only bitset payload. DFS/RPO, CHK chain intersection,
frontier walks, SCC traversal and sparse bit enumeration currently use scalar
control flow. Their useful-work cost must be measured together with the vector
parts; this implementation makes no claim that those choices are optimal on
the 9700X or that a synthetic crossover establishes a production threshold.

## Tests and benchmark

`test-dominators [--seed N] [--count N]` compares complete scalar/vector
results on original chains, diamonds, nested suffix loops, irreducible regions,
random graphs and unreachable components. Fixed sizes include empty/singleton
graphs and boundaries at 63/64/65, 511/512/513 and 1,025 blocks. Random small
graphs get full definition checks. A 100,000-block chain requests only the
tree and a zero matrix budget, checking the explicit stacks and linear fallback.

The independent dominance oracle deletes each candidate block and searches
for paths from entry. Frontier checks use that relation's definition. The loop
oracle searches forward to backedge latches with the header removed; it checks
bodies, strict-containment parents, depths and innermost loops. Deleting all
dominance backedges independently checks reducibility by topological removal.
Fixtures cover several latches, repeated/self edges, entry incoming edges,
an unreachable predecessor of reachable code, and natural self-loops inside a
two-entry SCC. They verify its conservative flags exclude exits and unreachable
cycles. CSR arrays and the vector solver's matrices end at inaccessible pages;
poisoned matrix tails must be cleared. Malformed columns, mismatched CSR views,
invalid IDs, arena exhaustion and independent matrix-budget failures check
rollback. These are module checks, not corpus or frontend coverage.

`benchmark-dominators [--seed N] [--only scalar|vector]
[--phase analysis|pipeline] [--after-scalar MS]` checks equality, then measures
four fixed synthetic shapes. It reports tree-only, tree-plus-loops and full
analysis. Analysis timing includes validation, all allocation, RPO, the chosen
solver, requested derived data and ordinary dominance/loop/frontier consumers.
Pipeline timing additionally creates the input edge stream and both CSR views;
it includes preparation and handoff cost. Source parsing/lowering is absent.

Each comparison uses 11 alternating-order samples with a shared repetition
count, calibrated so the faster selected path's warm batch takes at least
2 ms. Medians and ranges are nanoseconds per block and useful blocks per
second. Hardware cycles per block come from a nonmultiplexed `perf` counter;
unavailable or multiplexed samples are reported as unavailable, not zero.
The output also records blocks, edges, reachable blocks, loops, sweeps, output
bytes, scratch peak and matrix peak. `--only` isolates a path;
`--after-scalar 100` through `300` conditions each vector sample with complete
scalar handoffs before timing it. Repeat independent processes and inspect
emitted assembly when drawing a performance conclusion.

On the shared 9700X, acquire the lock **separately for every build, test and
benchmark**, and pin benchmark processes to CPU 2:

```sh
EXCLUSIVE_OWNER=codex-138-dominators EXCLUSIVE_TITLE='Tier 1 issue 138' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/174 ~/buster-fast-bench/exclusive.sh ./build/compiler test-dominators
EXCLUSIVE_OWNER=codex-138-dominators EXCLUSIVE_TITLE='Tier 1 issue 138' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/174 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-dominators --only vector --phase pipeline
```

Local sanitizer checks and benchmark smoke runs are diagnostic. Source-CFG
corpus tests, controlled repeated 9700X comparisons on real functions
(especially `sqlite3VdbeExec`), a measured scalar-selection threshold, and
integration acceptance in #134/#139/#140 remain outstanding. Postdominators
are not provided; #139 must request them if control dependence requires them.
