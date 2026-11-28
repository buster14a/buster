# Dominators, frontiers and natural loops

`src/buster/compiler/dominators.c` implements #174's entry-rooted CFG analysis.
It accepts the predecessor and successor CSR columns produced by #131's
`ir_build_cfg`, without copying them into another graph representation. The
analysis supplies dominance intervals for the IR verifier, frontier bitsets
for #134, and natural-loop membership for #139 and #140. These consumers must
explicitly integrate the interface; the existence of this module does not
establish their acceptance or source-corpus coverage.

The implementation and fixtures are original. The solver is SEMI-NCA,
as Georgiadis, Tarjan and Werneck describe it in
[Finding Dominators in Practice](https://jgaa.info/index.php/jgaa/article/view/paper119)
(JGAA 10(1), 2006): Lengauer and Tarjan's semidominators, from
[A Fast Algorithm for Finding Dominators in a Flowgraph](https://doi.org/10.1145/357062.357071)
(TOPLAS 1(1), 1979), followed by a nearest-common-ancestor step.
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

`dominators_options()` requests frontiers and loops, and limits simultaneously
live bitset storage to 64 MiB. Callers may request
only `BUSTER_DOMINATOR_FRONTIERS`, only `BUSTER_DOMINATOR_LOOPS`, both, or zero
for tree-only analysis. Tree-only SEMI-NCA storage is linear in blocks and
edges. The two quadratic output families are optional; a large sparse CFG
never silently allocates an unlimited frontier or loop-body matrix.

`max_matrix_bytes` is a memory limit, **not a measured performance threshold**.
It bounds the requested output matrices, the frontier rows and the loop bodies;
the analysis builds no other matrix. If they don't fit, the entire call fails.
Zero permits tree-only analysis and loop analysis with no natural loops. Arena
exhaustion is a distinct failure.

All output bitset rows contain `row_words` u64s, rounded up to a multiple of
eight, and start at a 64-byte boundary. Bits are original block IDs; inactive
tail bits are zero. The padding is allocated, readable and writable, separate
from the logical block count. No kernel reads past the row. Matrices are
disjoint; consumers must preserve their zero tails. Empty outputs have null
matrix pointers and zero rows. The layout lets consumers read whole rows with
512-bit loads; the analysis itself is scalar ("Why it is scalar" below).

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

An iterative DFS from entry produces RPO, DFS ancestry intervals, and each
preorder number's block and DFS parent. SEMI-NCA works in preorder numbers. It
computes semidominators in decreasing order: a predecessor numbered below the
block is a candidate itself, and one above contributes the least semidominator
on its path through the forest of already-processed vertices, which an
explicit stack compresses toward that forest's unprocessed root. Each immediate
dominator is then, in increasing order, the nearest common dominator-tree
ancestor of the DFS parent and the semidominator, found by walking up the
finished part of the tree from the parent. Compression bounds
the semidominators by O(E log B). The walk costs the depth between the parent
and the immediate dominator: a step or two on goto ladders, nested ifs and `?:`
chains, but O(B²) in total when a deep block has many DFS
children each also reachable from far above it, which Cooper–Harvey–Kennedy's
intersection walks too. Its scratch is two u32 columns from the DFS plus
12 bytes per reachable block.

Counting, prefix sums and filling produce the children CSR. An explicit tree
stack assigns intervals for constant-time dominance queries. Frontier
construction walks each reachable edge's source up to the target's immediate
dominator. For incoming edges to entry, it includes the root itself; this
also handles a single self-loop without assuming a join has two predecessors.
A walk stops at a block whose frontier already holds the target: an earlier
predecessor's walk passed there and marked the rest of the same chain. The
walks then cost the frontiers' total size plus the edges; without the stop, a
join of many predecessors under a deep chain, as a `?:` chain's, made them
O(E × depth). A goto ladder's frontiers are themselves quadratic in size.

Backedges are edges whose target dominates their source. All latches at one
header contribute to one natural loop.

Loops sharing a header are merged first; natural loops with different headers
are nested or disjoint, and an outer loop's header dominates its inner loops'.
`dominators_discover` therefore finds loops innermost first, by decreasing
dominator-tree preorder of their headers. Each walks back from its latches over
predecessor CSR and stops at its header. A block no loop has claimed becomes
this loop's own: its innermost loop, one bit in its body row. A block that a
loop found before claimed brings that loop's outermost discovered loop, by
union-find, and this loop absorbs it whole: it becomes that loop's parent, the
smallest strict containing natural loop; ORs that loop's body row into its own
with 512-bit ORs; adds its size and irreducible flag; and walks on from its
header. A walk leaves a natural loop only through its header, so each block is
claimed once and each header's predecessors are read at most twice: the walks
cost O(E α) and the bodies O(loops × row) vector ORs. Until #2953 a walk per
loop cost the bodies' total size, quadratic in nesting depth, and the parent
scan was quadratic in the number of loops. Depths follow the loop forest top
down. There is no recursion and no stack grows past the block count.

Each reachable edge to a DFS ancestor serves one of two purposes. If its
target dominates its source, it identifies a natural-loop header. Otherwise
the source witnesses irreducibility. Only graphs with such a witness need the
reverse DFS walk to assign SCCs. Every block in a witness’s SCC and every
natural loop touching it receives the conservative irreducibility flag.
This deliberately excludes some legal motion in reducible subloops of such
an SCC. It is a safe optimization boundary, not a minimal irreducible-cycle
decomposition. Unreachable SCCs and acyclic exits remain unflagged.

The public `output_bytes` and `scratch_peak_bytes` include alignment padding;
`matrix_peak_bytes` counts only bitset payload: the frontier rows and the loop
bodies.

### Why it is scalar

DFS/RPO, SEMI-NCA, the frontier walks, SCC traversal and the loops' union-find
walk are scalar; the loop bodies are combined by 512-bit ORs of whole rows
(above). Until #2924 the module also had an
AVX-512 path. Its bit-matrix solver started every reachable nonentry row at the
reachable set and swept in RPO until stable, intersecting predecessor rows with
`vpandq`; vector popcounts then made each block's largest strict dominator its
immediate one. Its loop-body sweep ORed reachable predecessor rows into a
frontier. Both lost to the scalar code at every measured size, and #2924
deleted them. The matrix is dense: each sweep costs B²/512 vector operations
and choosing the immediate dominators enumerates every set bit, O(B × depth),
where SEMI-NCA's semidominators cost O(E log B) and its tree walks are short.

Re-measured on main `aa09d1a04` before the deletion (`znver5` Release build,
CPU 2, the whole analysis per block, median of three processes):

- **sqlite3.c's 7,806 exported CFGs, tree only:** the matrix took 1.27–1.37×
  SEMI-NCA's time for 2–8 blocks, 1.35–1.94× for 8–128, 2.0× for 128–512, 2.7× for 512–1,024
  and 3.3× above, as for `sqlite3VdbeExec`'s 1,577 blocks; 1.44× over all of them
  (39.8 against 27.6 ns per block).
- **The same CFGs, every output:** SEMI-NCA with the loop worklist took 46.3 ns
  per block. The default before #2924, SEMI-NCA with the vector loop sweep
  wherever its matrices fit, took 48.6: 5% slower over all, 10–15% above 128
  blocks. The matrix with the sweep took 60.5.
- **Synthetic shapes, tree only:** the matrix took 1.6× on 16-block diamonds,
  2.7–6.8× on the 256- to 2,049-block shapes, and 31×, 56× and 90× on the
  8,192-block `ladder`, `nested-ifs` and `fan-in`, whose deep trees make its
  enumeration quadratic.

#1352's measurements, when SEMI-NCA replaced Cooper–Harvey–Kennedy, agree.
Cooper–Harvey–Kennedy was also faster than the matrix everywhere on sqlite3.c,
but quadratic in tree depth. A data-parallel solver that beats SEMI-NCA at every
size must be another design than a dense matrix; #2953 tracks it. Raw logs:
[the re-measurement](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/scalar-paths-removal-2924-2026-11-17/c-dominators-gate)
and [#1352's](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/dominators-semi-nca-1352-2026-11-07).

#2953 measured such a design: one sweep over RPO in which a block's dominators
among the 64 numbers above it are a u64 window, the AND of its forward
predecessors' windows shifted by their distances, whose lowest set bit is the
immediate dominator. Joins without a common dominator that close met by
skew-binary jump pointers in O(log depth), and irreducible regions the sweep
didn't settle took Cooper–Harvey–Kennedy passes. Its results equalled
SEMI-NCA's on every sqlite3.c CFG and generated graph. On the 9700X (main
`968a085f` against #2953's `6c67b3b8`, Release, CPU 2, the whole analysis per
block, median of three processes), tree only:

- **sqlite3.c's CFGs:** 1.00× SEMI-NCA's time for 2–4 blocks, 1.04× for 4–8,
  0.94–0.98× for 8–64, 1.01× for 64–128 and 1.10–1.19× above 128 blocks, as
  for `sqlite3VdbeExec`; 0.98× over all of them.
- **Synthetic shapes:** 1.10× on 16-block diamonds, 1.19× on the 513-block
  irreducible shape, 1.06×, 1.25× and 1.77× on the 8,192-block `fan-in`,
  `nested-ifs` and `ladder`, and 2.75× on the random 2,049-block `dispatch`,
  which its first sweep doesn't settle.

So SEMI-NCA stays. The sweep is on the `claude/2953-window-sweep` branch, and
#2953 has the measurements.

## Tests and benchmark

`test-dominators [--seed N] [--count N]` analyzes original chains, diamonds,
nested suffix loops, irreducible regions, random graphs, unreachable
components, and #1352's goto ladder, nested-if joins and a `?:` chain's single
join, whose dominator trees are half the graph deep. Fixed sizes include
empty/singleton graphs and boundaries at 63/64/65, 511/512/513 and 1,025
blocks. Every result is checked against the definitions below and the order
and tree columns' contract (`rpo` and its inverse, each block after its
immediate dominator, each block's children in ascending order), analyzed again
to the same result, and must keep its rows aligned and their tails zero. A
100,000-block chain requests only the tree and a zero matrix budget, checking
the explicit stacks and linear storage.

The independent dominance oracle deletes each candidate block and searches
for paths from entry. Frontier checks use that relation's definition. The loop
oracle searches forward to backedge latches with the header removed; it checks
bodies, strict-containment parents, depths and innermost loops. It costs
O(loops × blocks × edges), so it runs on the generated graphs of up to 127
blocks, the random ones and one of 513 blocks, whose rows take two vectors.
The rest get every other definition check. Deleting all dominance backedges
independently checks reducibility by topological removal.

On the default seed, the 36 generated graphs of 511 blocks and more must also
match frozen digests of their complete results. They were taken from main at
295417ae7, where SEMI-NCA and the bit-matrix solver gave equal results on every
generated graph and every one passed the whole definition, loop bodies
included. Another seed checks those graphs by the definitions alone.

Fixtures cover several latches, repeated/self edges, entry incoming edges,
an unreachable predecessor of reachable code, and natural self-loops inside a
two-entry SCC. They verify its conservative flags exclude exits and unreachable
cycles. #2924's mutation check added three paths no generated shape takes: two
6-block graphs on which SEMI-NCA's evaluation must climb through the vertex
numbered right after the one being processed, and carry a compressed link's
least semidominator; and a 3-block acyclic graph whose cross edge into a
just-finished DFS subtree the SCC pass must not flag. #2953's mutation check
skipped an absorbed loop's body OR or its parent, found outer loops first, and
took every predecessor of a header for a latch: each fails `test-dominators`.
CSR arrays end at
inaccessible pages. Malformed columns, mismatched CSR views, invalid IDs, arena
exhaustion at every allocation, and matrix budgets one byte short of the
frontiers alone, or of the frontiers and loop bodies, check rollback. These are
module checks, not corpus or frontend coverage. `test-ir-source --dominators`
analyzes the IR CFGs of real C sources twice each, and checks them against the
whole definition and the columns' contract.

`benchmark-dominators [--seed N] [--phase analysis|pipeline]` measures
seven fixed synthetic shapes, the last three the 8,192-block `ladder`,
`nested-ifs` and `fan-in`. It reports tree-only, tree-plus-loops and full
analysis. Analysis timing includes validation, all allocation, RPO, the
solver, requested derived data and ordinary dominance/loop/frontier consumers.
Pipeline timing additionally creates the input edge stream and both CSR views;
it includes preparation and handoff cost. Source parsing/lowering is absent.
`benchmark-dominators --cfg EXPORT [--form N] [--outputs tree|loops|all]`
times the analysis on the CFGs that `test-ir-source --dominators-cfg` exported:
by block-count bucket, the eight largest alone, and all of them together.

Each measurement takes 11 samples with a repetition count calibrated so a
warm batch takes at least 2 ms. Medians and ranges are nanoseconds per block and useful blocks per
second. Hardware cycles per block come from a nonmultiplexed `perf` counter;
unavailable or multiplexed samples are reported as unavailable, not zero.
The output also records blocks, edges, reachable blocks, loops, output bytes,
scratch peak and matrix peak. Repeat independent processes and inspect emitted
assembly when drawing a performance conclusion.

On the shared 9700X, acquire the lock **separately for every build, test and
benchmark**, and pin benchmark processes to CPU 2:

```sh
EXCLUSIVE_OWNER=codex-138-dominators EXCLUSIVE_TITLE='Tier 1 issue 138' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/174 ~/buster-fast-bench/exclusive.sh ./build/compiler test-dominators
EXCLUSIVE_OWNER=codex-138-dominators EXCLUSIVE_TITLE='Tier 1 issue 138' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/174 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-dominators --phase pipeline
```

Local sanitizer checks and benchmark smoke runs are diagnostic. The scalar
solvers rest on the measurements above; integration acceptance
in #134/#139/#140 remains outstanding. Postdominators
are not provided; #139 must request them if control dependence requires them.
