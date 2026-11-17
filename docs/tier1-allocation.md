# Tier 1 interval allocation and rematerialization

`tier1_allocate.c` implements the interval assignment and move-resolution part
of [#140](https://github.com/buster14a/buster/issues/140).
`tier1_rematerialize.c` implements the eligibility column and flags-safe copies
for [#178](https://github.com/buster14a/buster/issues/178). These are original
implementations. Wimmer and Franz's
[Linear Scan Register Allocation on SSA Form](https://doi.org/10.1145/1772954.1772979)
is the algorithmic reference; no implementation or external fixture corpus is
imported.

The allocation interface is a view of machine lifetimes. It neither defines
another IR nor changes the concurrently owned IR/SSA implementations. The
selector must publish its effective uses **after** folding: folding an address
or shift into a later instruction extends the original operands' lifetimes.
Allocating the unfused uses and then folding is unsound.

## Producer and consumer

`Tier1AllocationInput` borrows contiguous columns for `count` input intervals:

| Column | Width | Meaning |
| --- | --- | --- |
| `starts`, `ends` | u32 | Half-open machine lifetime `[start,end)` |
| `values` | u32 | Stable value id, below `value_count` |
| `classes` | u8 | GPR=0, vector=1, opmask=2 |
| `allowed_registers` | u32 | Allowed physical-register bit mask |
| `flags` | optional u8 | `REQUIRED` pins the whole interval in a register |
| `hints` | optional u8 | Preferred allowed register, or `UINT8_MAX` |
| `rematerializable` | optional u8 | Certified 0/1 eligibility from selection |
| `use_offsets` | optional u32 | `count+1` CSR offsets into use columns |
| `use_positions` | optional u32 | Nondecreasing positions inside each range |
| `use_required` | optional u8 | 1 requires a register at that use; 0 permits memory |
| `clobbers` | optional u32 | Nondecreasing positions where `clobber_registers[class]` lose their values (#2574) |
| `avoided_registers` | optional u32 | Registers each interval takes only where no other it may take is free (#2767) |

`registers[3]` contains the allocatable physical masks. GPR bits are 0–15;
vector bits are 0–31; opmask bits are 1–7, k1 to k7 (#179,
`BUSTER_TIER1_ALLOCATE_MASK_REGISTERS`): EVEX encodes "no mask" with k0, so no
mask value is ever allocated there, and validation refuses a class, an interval
or a hint that would put one in k0. A fixed range of the class may name all
eight (`BUSTER_TIER1_ALLOCATE_MASK_FILE`), as a call clobbers k0 too. The caller
restricts the vector and opmask masks to the generated-code target,
independently of the host's AVX-512 capability: a target without AVX-512 has
no opmask registers. The caller excludes RSP, reserved scratch registers, and
frame bases selected by the frame planner. In particular, it applies the frame
planner's RBP/R13 reservation before allocation. Masks of zero are valid when
memory operands are permitted.

A class without intervals is skipped: one pass over the intervals' classes
finds the classes present, and an absent class's passes over the intervals and
the events, and the scan the common path would have left it, are never run, so
a function's empty vector and opmask classes cost nothing past that pass. The
output is the same as the scan's of an empty class, which assigns nothing.

The selector/liveness adapter uses input position `2*i` and definition position
`2*i+1` for node `i`. Ending a source interval at the output phase permits a
destination to reuse a dying source register. Positions are simultaneous
phases: all required uses at one position are pinned before allocation makes
any eviction decision. Too many simultaneous required operands return
`TIER1_ALLOCATION_CONSTRAINT`, not a memory operand masquerading as a register.

Distinct input intervals of one value may describe lifetime holes or machine
constraint splits. They must not overlap or change register class. Assignment
can change between such intervals; the producer must supply the required
control-flow transitions. Extending ranges over loops and choosing a
loop-contiguous order are the liveness/block-order producer's responsibilities.
The allocator does not infer either from numeric instruction ids.

`Tier1FixedIntervals` supplies half-open reservations/clobbers with physical
masks and classes. For example, a call phase reserves its caller-saved masks;
values crossing it can then use only unreserved registers or memory. An
instruction's fixed operand uses a one-bit `allowed_registers` mask, with
`REQUIRED` when needed. A fixed reservation describes an *other* use/clobber;
the producer does not reserve that same register against the operand itself.
The current implementation removes intersecting reservations for the whole
input interval. Producers may split around constraints to recover registers.
It does not yet search for optimal split positions around fixed ranges.

Fixed filtering reuses the position-sorted end events and the dead event buckets.
It sweeps fixed starts strictly before each value interval's end and keeps the
maximum fixed end for each of the two classes' 32 physical registers. Two
16-register vector comparisons identify the reservations whose maximum end is
strictly greater than the value interval's start. This preserves half-open
boundaries and removes the same union of masks from the entire interval,
including unsorted, nested and duplicate fixed ranges and multibit clobbers.

Preparation costs O(positions + fixed ranges + events), including required-use
events, with four temporary bytes per fixed range and a 256-byte register table.
The existing sixteen-interval vector cross-product handles pair counts no larger
than `positions + 16*(intervals + fixed ranges)`. This keeps tiny functions and
small fixed sets on the simple path while bounding its work linearly as well.
The scalar allocator keeps its independent cross-product reference. Empty fixed
sets allocate no additional scratch.

The current native caller receives its input from `tier1_liveness_build`, whose
allocation initializer leaves `fixed` empty. `tier1_emit_prepare` copies that
input and adds allowed masks and required uses; calls save and restore live
caller registers separately. Thus this change addresses the allocator API's
fixed-range scaling; it does not establish a speedup in the current native
pipeline or the default Tier 0 compiler.

`tier1_allocate` and `tier1_allocate_scalar` return an explicit status and an
arena-owned `Tier1Allocation`. Each input interval owns the compact range
`fragment_offsets[i] .. fragment_offsets[i+1]` in the output's start/end,
location-kind and register columns. Fragments cover the input lifetime
exactly. A fragment is REGISTER, SPILL, or REMATERIALIZE. Nonregister fragments
have register `UINT8_MAX`.

`value_registers[value]` is a convenient selector view: it is a physical
register only when the value's fragments all stay in one register without
splitting; otherwise it is `UINT8_MAX`. The initial leaf selector rejects
values without that uniform view. It does not silently ignore spill plans.
`tier1_allocation_location` queries the precise location for a machine position.

Actual spill values have dense `spill_ids[value]` and the inverse compact
`spill_values[spill_count]`. Values requiring no slot have id `UINT32_MAX`.
(Over a CFG, a phi web's values may share a slot: "Allocation over a CFG".)
`Tier1FrameSlots.indices` can borrow `spill_values` alongside the producer's
value-indexed size, alignment and conservative lifetime columns. Rematerialized
values allocate no spill slot unless another interval of the same value really
spills. Output statistics count vector and scalar intervals, transitions,
maximum pressure, retained arena bytes and peak temporary bytes.
`vector_intervals` counts the intervals of classes admitted to the
regular-class path below; that path's matching is scalar code, so the count
measures admission, not SIMD lanes or instructions.

## Assignment

Both paths validate and count-sort position events. End events precede starts
at the same position. Both production entry points use the same assignment
policy (#1395), including on a baseline host: the common path applies to a
class whose ranges all have its full register mask and whose maximum pressure fits.
It walks the class's events in order, with one register per pressure level:
- an end at level d deposits its interval's register there;
- the next start at level d takes it, the most recent end that no other start
  has taken;
- a start at a level no end has reached takes a fresh register.

A start's partner started earlier, so its register is already final when the
two are matched, and the one pass assigns every register. This uses at most
maximum pressure registers, including arbitrary input order and noncontiguous
physical masks. `test-tier1-allocation` compares these registers with an
independent implementation of the same rule, which walks positions without the
event sort. Scalar production keeps its scalar fixed-range filtering kernels,
but it no longer selects a different allocation policy. The former scalar
policy remains `tier1_allocate_scan_reference`, a test oracle that sends every
class through the independent scan. Its valid assignments can differ from the
production paths' identical assignments.

The rule alone gives values that end and restart at one position each other's
registers: ends deposit downward in input order and starts take upward in input
order. A CFG's per-block ranges do exactly that at a block's start, so
`hint_values` names, per interval, a value whose most recently assigned register
the interval takes where it is free; `tier1_cfg.c` hints each range a value
enters a block with to the value itself, each phi to an operand, and each phi
operand nothing else hints to the phi, which along a back edge has its register
by then, so that the edge moves nothing for it. A hinted interval whose own
value already had a register, in an interval earlier in the order, takes that
one first: a loop's header laid out after its body keeps the register the body
gave its phi rather than its operand's (#1216).

`hint_intervals`, optional with `hint_values` (ignored without it), names per
interval an earlier-starting interval of its class whose register at its start,
its opening register, the interval takes before either of those (#1216);
validation refuses one of another class, starting at the same position or
later, or past `count`. The openings follow `value_count + 1` per-value bytes in
the same scratch array, written as each interval starts on every path, and an
interval that started in memory leaves the memory hint, which an interval
without uses follows as it would its value's. `tier1_cfg.c` gives a block that no
earlier block reaches (a dispatch loop's handler, its join laid out after the
handlers) the entry convention of its sibling, the first block in layout that
its predecessors reach; and the join's phis their places at the entry of its
first successor, which is laid out before it. Each handler then enters with the
loop's values where the first one has them, and the join's edges move nothing
for a value every handler keeps. Before, a handler's entry ranges took registers
by the values each handler had last placed in them, which differ per handler, so
that each join edge rotated the loop's values through registers. The common
path takes the hinted register from the level above that holds it, which then
holds the register the rule would have taken, or as a fresh one, which makes a
register taken from the current level fresh again. Every level from the current
one up still holds a free register or none, and every level below none, so an
end never overwrites a deposit and a start never finds no register while the
class's pressure fits; with hints it may use more of the class's registers than
maximum pressure. The scan honors the static `hints` first, then `hint_values`.
Without `hint_values` every output is unchanged, and so is the code: the
allocator is one body compiled twice, an input without hints running a copy with
no hint code and one with them a noinline copy that validates them first. On
the 9700X, `benchmark-tier1-allocation`'s cases (which have no hints) then run
within 2% of the allocator before hints, but tiny-fit's vector path at +2.9%, 30
cycles per call for the dispatch; with the hint code in the plain loops they had
run 4% to 15% slower. `tier1_allocate_hints_test.c` checks the rule against an
independent reference over positions, with and without interval hints, a trace
whose two values the rule alone swaps, and one whose range takes an earlier
range's opening register over its own value's last.

With `BUSTER_TIER1_ALLOCATE_HINT_EXIT` in its entry, `hint_intervals` names
instead an interval that ends by the interval's start, whose register at its
end, its closing register, the interval takes first (#2553); validation also
refuses one that ends after the interval starts. The closings follow the
openings in the same scratch array, written as each interval ends on every path:
the register it still holds, or the memory hint where an eviction left it in its
slot. `tier1_cfg.c` names so a value's range at the exit of a block that
dominates the one the interval enters, and a phi's operand's at a predecessor's
exit ("Places along dominators" below). The hint test's reference takes the
closings too, its random inputs name a third of their hinted ranges' earlier
ranges by their exits where those end first, and a literal trace, whose interval
the scan moves from r0 through its slot to r1, gives the range named by its exit
r1 and by its opening r0 on every path.

That matching stays scalar, for a measured reason. It used to resolve the
partners' registers afterwards: pointer-jumping rounds over every interval,
after an AVX-512 prefix sum that recounted the pressure the class loop already
counts. Both recomputed what the sequential matching knew. Removing them (#140)
left every output the same and made the whole call faster on the 9700X:

| Workload | Intervals | Vector path, before | after | Scalar path, before | after |
| --- | ---: | ---: | ---: | ---: | ---: |
| tiny-fit | 16 | 103 | 65 | 187 | 75 |
| small-fit | 128 | 96 | 54 | 174 | 63 |
| large-fit | 2,048 | 114 | 53 | 172 | 61 |
| large-pressure | 16,384 | 232 | 118 | 231 | 118 |

These historical results predate #1395's common production assignment policy;
the scalar column used the scan on every class. They are
`benchmark-tier1-allocation`'s cycles per interval: medians of five
processes, pinned to CPU 2, before and after the change, with the same ratios
after 100 ms of scalar work. The scalar column also has the scan's end-event
expiry below, and large-pressure, which is overfull, takes the scan on both
paths.

The [stopgap record](https://github.com/buster14a/buster-performance/tree/35f63a2eafc3e5471a8818b19cf3c216a89655d4/docs/measurements/tier1-allocation-stopgap-140-2026-10-30) has the logs, the differential against
the previous code and the method. A data-parallel formulation of the matching
itself is #140's candidate vector path: heights by byte prefix sums, partners
by a window-maximum descent, registers by in-register pointer jumping over
64-lane chunks. Its first prototype assigned about 7.2 cycles per instruction
on one unconstrained block, tied with a scalar lowest-free loop on short
temporaries and ahead of it on chains
([review record](https://github.com/buster14a/buster-performance/tree/d3fb9c012d666cb13082f4e359d4d25928633768/docs/measurements/tier1-allocation-140-review-2026-10-30)).

The hot functions copy the input's column pointers into a local struct, since
under `-fno-strict-aliasing` each store into the arena would otherwise make
clang reload them. They pass the original `input` to their helpers, so that the
copy's address doesn't escape: an escaped copy could alias the stores too, and
the counting sort's placement loop then reloaded its column from the stack.

Constrained or overfull classes defer to a scalar scan bounded by their
physical-register count. An interval frees its register at its end event,
unless an eviction gave that register to another interval (the scan's
`active[r] == interval` check). It honors available-register hints, then evicts an
unpinned active interval whose next use is farther away. Without use columns,
the end position is the conservative next-use estimate. With `use_required`
(#1216), the victim is a rematerializable interval first, then the farthest
next use, then, at equal distance, the value with fewer uses over all its
intervals. An interval with no use of its own, starting where it is not
required, keeps an unclaimed free register if it can be rematerialized, but
never evicts another value or takes a continuing value's claimed register,
even through a physical-register hint. This preserves hoisted definitions
through low-pressure blocks, including a jump table's once-per-entry base
(#1217), while leaving them cheap eviction victims under pressure. It rests
without one where its hint names no register, and a recipe evicted before any
use read it gives up its register from its interval's start ("Recipes evicted
before a read" below). Nor does a
recipe keep a register from a value without one that starts, or reloads at a
required use, where its value hint names that register: where no use requires
the recipe there, it leaves the register as an eviction does, recomputed at its
next required use ("Recipes give way to hinted values" below). If its
value was last in its slot and has no recipe, it stays there. So does a range
with uses that continues its value from the slot, until its next required use,
unless its value's first register is free at its start ("Ranges that wait in
their slots" below). A CFG's edges
move a value without a register where a successor wants it in one. The
intervals starting at one position claim the registers they are hinted to,
first come in input order, before any takes one; an interval whose hint is
another's claim, or that has none, takes a free register no other claims there,
or a claimed one only where it is required. Taking registers in input order
alone gave a value another's wanted register, pushed that one into a third, and
so on along a block's entry. A value without a
register gets a spill/rematerialization fragment. An eviction closes its
register prefix. With `use_required`, it reenters a register at its next
required use, potentially evicting another value, and can be split again.
The output capacity is bounded by `2*intervals + 2*uses`; the simple path needs
at most `2*intervals`. Dense spill ids are assigned in input-interval order.

With `use_required`, `clobbers` (#2574) are positions where the registers
`clobber_registers[class]` lose their values, as a call's definition phase
loses the caller-saved ones. At each, the scan takes every recomputable
interval holding one of them out of it, as an eviction does: a REMATERIALIZE
fragment from that position, no store, and a register again at its next
required use. A value required there keeps its register, and so does a value
without a recipe: its consumer saves it around the clobber. A clobber's event
follows the ends at its position and precedes the starts, so an interval
starting there keeps its register. A class where a clobber falls inside a
recomputable interval takes the scan, the only path that evicts, and an input
without recipes is allocated as it would be without its clobbers. Each entry
into a register is evicted at most once, so the output's bound stands.
`tier1_allocate_clobbers_test.c` checks each case's trace on the three entry
points, the refusals (clobbers without required uses, out of order, past the
positions, or over a register outside the class), and random inputs against
the verifier, the occupancy oracle and two independent properties: no
recomputable value holds a clobbered register across a clobber but where a use
requires it there, and clobbers change no allocation of values without recipes.

This is not yet the complete Wimmer/Franz implementation: it lacks inactive
interval lookahead across holes, loop-aware split placement, vector spill
region selection/recoloring, and automatic machine-constraint interval
construction. Input holes are independent ranges, and the independent scan
reference is a validity oracle, not a proof of optimum constrained coloring.

## Transitions and parallel moves

A register-to-spill boundary requires a store before the register is reused.
A spill-to-register boundary requires a reload; a rematerialization-to-register
boundary requires a fresh copy of its certified definition. An initial spill
requires producing the definition in an appropriate temporary and storing it.
At a simultaneous transition phase, stores precede reloads/copies that reuse
their source registers. Required-use fragments make these transitions explicit;
the allocator does not emit machine code for them.

`tier1_resolve_moves` sequentializes a same-class, same-width parallel assignment.
The locations are opaque caller ids and must not alias. Destinations must be
unique. It emits assignments whose destination is no longer an outstanding
source, and breaks a cycle by saving one old destination in a caller-reserved
scratch location. Identity moves disappear. The scratch must not appear in
the input. Registers and stack slots may use disjoint id ranges, but a machine
consumer still needs a legal lowering for a memory-to-memory copy. CFG edge
placement and critical-edge splitting are not done by this routine.

The deterministic policy is unchanged: emit the lowest input index whose
destination has no remaining source users; when none is ready, save the lowest
pending destination to scratch. Up to eight moves use the independent scan
implementation, `tier1_resolve_moves_scalar`. The only current production caller
is `tier1_emit_call`, bounded by the six SysV integer argument registers; the
general API keeps its full 2^20-move limit and opaque location domain.

Larger sets use `tier1_resolve_moves_indexed`. Eight-wide masked initialization
validates scratch exclusion, marks identities and constructs packed
`(destination, input index)` keys. A nonrecursive merge sort detects duplicate
destinations; binary searches map each source to its destination owner. Source
use counts and a minimum heap replace repeated pending-move scans. Each emitted
move updates only its source owner's count, and each move enters the ready
heap once. A monotonically advancing cursor finds the first pending move when
breaking cycles. Sorting, lookup and heap work are O(N log N), with O(N) storage,
including adversarial opaque ids and fan-out.

When the ready heap is empty, every remaining destination has at least one
source user. There are at most as many source edges as remaining moves, so
every remaining destination has exactly one user: the residual graph is a set
of disjoint cycles. Saving one destination makes its move ready; its sole
remaining consumer reads scratch instead. That cycle drains before another
save can reuse scratch. The algorithm needs neither repeated source rewriting
nor a scan for that consumer.

Both paths retain at most 16*N bytes for output columns. The indexed path uses
28*N temporary bytes (two u64 key arrays and three u32 dependency/count/heap
arrays), plus alignment; the small scan uses 5*N temporary bytes. Inputs remain
immutable, errors restore the entry arena length and preserve the result, and
success discards all temporary arrays. Sorting requires temporary capacity, so
capacity exhaustion can precede duplicate-destination diagnosis.

### Resolver measurements (#608)

On the approved locked Ryzen 7 9700X, Clang 23.1.1 with the normal optimized
flags, three independent processes pinned to CPU 2 measured the complete
resolver call and arena reset at source `e2dcb4b63c2611946be26a424a4618f6b584cfb8`.
The 1024-move adversarial chain took 116.487–117.449 ms with the original scan
and 13.309–13.314 microseconds with dispatch. Median retired instructions fell
from 3,842,936,080 to 420,805. Across 32–1024 moves, doubling N increased the
original chain's instruction work by 7.39–7.96 times, and dispatch's by
2.13–2.15 times. Reversed chains, independent moves, cycles and fan-out were
also measured at every requested size and retain the indexed path's bounded
work. These are resolver measurements, separate from whole-compiler throughput.

At 1024 moves the indexed call peaks at 44 KiB of arena storage versus 21 KiB
for the scan; both retain 16 KiB. The small cutoff preserves the current
caller's original algorithm and smaller arena budget. Its tuning remains an
opportunity: forced indexing takes 43.811–44.878 ns on the six-move synthetic
chain, while dispatch takes 106.910–117.510 ns; the original scan wins on the
one- and two-move chains. The actual caller's shuffle distribution remains
unmeasured, so these data establish neither an optimal cutoff nor a corpus
speedup. The native/Clang pipeline, allocation, liveness, selection, call-graph
and resolver ASan/UBSan qualification all passed on the locked host.

The [pinned report, harnesses, raw counters/timings, assembly and identities](https://github.com/buster14a/buster-performance/tree/24e978f162396700cfc1848a4d77804e2e7f065e/docs/measurements/tier1-moves-608-2026-10-27)
record the shapes, complete process ranges, memory costs and validation scope.

## Allocation over a CFG (#140)

`tier1_cfg.c` allocates a function's machine CFG: blocks in layout order, each
a range of positions, with both CSR views of the edges, and SSA values. A phi
is defined at its block's start by the moves on the block's incoming edges,
with one operand per predecessor, in the predecessor view's order. Block 0 is
the entry, with no predecessors and no phis. Every other value is defined once,
before its uses in its block. A use in another block must be dominated by the
definition: a value live into the entry or into its own block is refused as
invalid. A use of a value other than a phi at its own definition position is the
definition's: a required one asks that the definition write a register, as an
instruction's destination does. The checker checks it after the definition, by
the location it gives the value.

Liveness is `dataflow.c`'s solve over values with an ordinary use outside their
home block or a phi occurrence passed from another block (#1384). A phi
operand passed only from its home, with no remote ordinary use, needs no dense
bit: a byte flag extends its home interval through the exit. Every occurrence
is checked; a single remote occurrence makes the value global. The existing
home-definition CSR supplies local exit locations to the forward boundary
sweep, and value-indexed exit stamps let phi moves read either kind of source.
This includes self-phis, fixed-range splits and all three register classes.

A global phi operand joins its predecessor's use row unless that block defines
it, then joins its predecessor's live-out row after the solve. Two operand
scans supply these facts directly, replacing the fifth `passed` matrix. The
four remaining matrices cost `4 * blocks * 64 * ceil(global_values / 512)`
bytes, zero when no global value remains. `cross_block_values`,
`edge_local_values` and `liveness_bytes` report that partition and matrix cost;
the last excludes CFG columns and other allocator state. Short-circuit chains
whose global set stays bounded therefore have linear matrix storage. This does
not impose a sparse fallback or memory ceiling on genuinely nonlocal values.

The successor-edge-to-predecessor-ordinal column is built once in
`O(blocks + edges)` and retained with the preparation. Its transpose is mapped
through each target's original predecessor order, preserving phi associations
even with shuffled rows. Edge moves read it directly rather than searching a
large join's predecessor row once per incoming edge.

A phi operand may be undefined (`BUSTER_TIER1_CFG_NONE`, #141): the IR's `undef`,
which the CFG consumer passes as no value. Any value is the phi's along that edge,
so the operand is no use, keeps nothing live, hints nothing, and the edge moves
nothing for it; `tier1_cfg_verify` checks nothing of the phi's location on that
edge. Without it, the one `undef` per local that promotion puts in the entry
block was live from there to every join that read it: around a loop, everywhere.
Promotion makes such operands where a block's lifetime start kills a local
([the SSA guide](ssa.md)'s adapter), and Lua's `luaV_execute` had hundreds of
them.

An edge can carry copies the allocation doesn't see: a long double phi's
(#141), whose values live in frame objects, which the CFG consumer copies on the
x87 stack (`tier1_emit_cfg_x87_phis`). Its optional per-edge column
(`Tier1CfgInput.edge_copies`) places such an edge as one with moves, at its
predecessor's end, its successor's start or a split's, though it has none of the
allocation's; `tier1_cfg_verify` and the certificate's shape check take that
placement. Each value then gets an interval per block it is
defined in or live into, from its definition or the block's start to its last
use there, or to the block's end where it stays live. A value's blocks come
block by block from the live-in rows, counted and then filled, so building the
intervals costs the live pairs rather than values times blocks.

A block continues its layout predecessor's intervals when it has no phis and
every value live into it is live out of that predecessor. Then every value of
its entry state continues, and a fragment transition at its first position runs
inside the block, on every path into it. Without that rule a phi moved in on an
edge could take a register that a continuing value gives up at the block's
start, in a transition that runs only after the edge's moves. Intervals are
numbered by value, then position: each interval's uses are a slice of its
value's, and the allocator borrows the input's use columns.

After `tier1_allocate`, or its scalar twin, assigns the intervals, each edge
moves its successor's entry state, the live-in values and the phis, from their
locations at the predecessor's last position to their entry locations
(`tier1_cfg_entry_location`: the location at the block's start, or for a
continuing interval the location just before it). `tier1_resolve_moves` orders
each class's moves. A cycle is broken with the class's scratch register or,
when one of the class's moves on the edge goes slot to slot, with the class's
temporary slot; a slot-to-slot move goes through the scratch register, which
then holds nothing from one move to the next. Recomputing a rematerializable
value comes last. The moves go at the end of a predecessor with one successor
(after its last instruction, so a jump there must read no allocated value), at
the start of a successor with one predecessor, and otherwise on the split edge.

A value's range is also cut around each fixed range of its class that blocks
every register the value may take: a call's clobbers block a double's. The
allocator takes a fixed range's registers from every interval that overlaps it,
so without the cut a double live across a call would have no register anywhere
in its block, and a use that needs one would be refused. The piece over the
fixed range goes to memory, and the pieces around it keep their registers,
hinted back to the one the value had. Where pieces meet inside a block, a
split's moves take each value from its location before the position to its new
piece's, resolved as an edge's moves are; they run after the position's stores
and before its reloads. A block whose start a fixed range covers doesn't
continue its predecessor's ranges.

Locations are `class * 32 + register` for registers,
`BUSTER_TIER1_CFG_SPILL + spill id` for slots and
`BUSTER_TIER1_CFG_SPILL + spill_count + class` for the temporary slots, which
`temp_slots` marks for the frame to reserve. A move whose source is
`BUSTER_TIER1_CFG_REMATERIALIZE` recomputes its value into its destination.
`BUSTER_TIER1_CFG_SPILL` is 32 per class, 96 with the opmask class.

With `slot_widths`, which Tier 1's emission gives as each value's slot bytes, a
phi web's spilled values share one slot (`tier1_cfg_share_slots`, #1216), after
the allocation and before the edges' moves. A phi and each operand of its class
and width, both spilled somewhere, join where no value already sharing either
one's slot is live at another's definition: in SSA, two ranges overlap exactly
there. An edge then moves nothing for an operand already in its phi's slot,
where a dispatch loop's values that rest in memory went slot to slot through
the scratch register on every handler's back edge. A slot keeps its first value
in `spill_values`, every value its id in `spill_ids`, and the frame sizes the
slot's row by the hull of all its values' intervals. At most 32 values share a
slot, since each join checks every pair of the two slots' values.
`tier1_cfg_verify` refuses a store over another value its slot still holds;
`tier1_allocation_verify`, which takes a slot to hold one value, checks the CFG
tests' allocations with a slot per spilled value.

### Places along dominators (#2553)

A block's ranges used to take, by `hint_values`, the register their value had
most recently in layout, and a phi its operand's at the predecessor nearest
before it. A block laid out after a sibling whose code moved a value took the
moved register wherever control came from, and a phi often took the cold path's:
each of Lua's handlers tests `trap` and calls `luaG_traceexec` where it is set,
and the join after that call took the call's result's register, so that the hot
path moved `trap` on a split edge and the handler's tail rotated three values
through r11 into the dispatch join's places. #138's measurement put edge moves,
spills and reloads at 7.4% and 10.0% of speedtest1's and Lua's executed
instructions.

`tier1_cfg_anchors` takes the CFG's dominator tree (`dominators.c`'s SEMI-NCA,
one analysis per function) and gives each block:

- a chosen predecessor: one laid out before it that it dominates (a latch of a
  loop whose header is laid out after its body, whose back edge runs every
  iteration: the nearest of them in layout), else its immediate dominator where
  that is a predecessor laid out before it, else its nearest predecessor before
  it in layout;
- an anchor: such a latch, else its immediate dominator where that is laid out
  before it, else none: the entry, an unreachable block, and a dispatch loop's
  handlers, whose dominator is the join laid out after them, which keep #1216's
  conventions.

A value live into a block is live out of each of its predecessors, and of its
immediate dominator unless the block defines it, so the values a block takes
from one block's exit are there at once, each in its own place. The ranges then
take, by exit hints:

- a range a value enters a block with, the value's place at the anchor's exit: a
  value keeps its register from its definition through the blocks it dominates,
  and an edge moves what the blocks on its way changed;
- in a block laid out after its first successor (a dispatch loop's join), that
  successor's entry convention, for its values as #1216 gave it for its phis;
- a phi, its operand's place at its chosen predecessor's exit, so that the edge
  from a dominating predecessor, a critical one where the other path rejoins,
  moves nothing;
- a value that a phi takes and nothing else hints, where that phi has no range
  before the value's definition: along the phis that take the phi in turn
  (`tier1_cfg_operand_targets`, up to eight), the first with a range before it,
  whose first range's opening register the definition takes. A dispatch loop's
  join's phi has its first handler's, the register every handler takes it in;
- a two-address instruction's result that nothing else hints (#2685), its first
  operand's place at the end of that operand's range, where the range ends at
  the definition ("Two-address results" below).

Slot sharing joins phi-to-phi pairs before the others (`tier1_cfg_share_slots`):
in `sqlite3VdbeExec`, whose latch's join is laid out before its header, an
operand spilled where it entered the loop joined its phi's slot first and kept
the loop's two phis apart, so that every handler's edge to the latch copied
slot to slot.

From its merge base `afcb185e`, the tagged edge moves, stores and reloads fell
from 225.1 M to 210.6 M on speedtest1 and from 542.7 M to 355.5 M on Lua, where
the moves through r11 halved (191 M to 96 M). On the 9700X, user cycles fell by
0.73 on speedtest1 over 8 shifts and by 1.02 on Lua over 33 link orders
(100 ln(head / base); [the record](https://github.com/buster14a/buster-performance/tree/ddb5213da1fce7ac9325afbf6364a797bbc63b5b/docs/measurements/tier1-dominator-places-2553-2026-11-12)).
Not everywhere: `sqlite3VdbeExec`'s edge traffic rose by 2.1 M though it runs
fewer instructions, and `yy_reduce` keeps its parameter `yypParser` in `rdi`,
its incoming register, into its switch's join, which calls
`yy_find_reduce_action`: every reduction saves and restores it (4.9 M more).
The preferred problem lets a parameter keep its incoming register on every one
of its intervals; keeping one that crosses a call in callee-saved registers past
its definition's interval cost far more (28.5 M instructions on speedtest1,
269 M on Lua). The dominator analysis costs about 0.6% of sqlite3.c's
`--tier1 -c` cycles, the whole change 1.0%.

### The opmask class (#179)

AVX-512's opmask registers are the third class, k0 to k7. Nothing moves a value
between classes, so a mask is never reinterpreted as a general register's
value. A mask value takes one of k1 to k7: k0 can't be a write mask, and both
allocators refuse a class with k0 among its registers (`tier1_cfg_verify` too,
whose input check is theirs, and it rejects a mask fragment in k0 as outside its
value's registers). The producers here (`tier1_cfg_ir.c`, the tests) give the
class k1 to k7 and make k0 its scratch register: the class needs a register
outside its own for cycles and slot-to-slot moves, which `kmov` makes through
k0 as `mov` does through the general class's scratch. A mask's
slot and the class's temporary slot hold the mask at its width, which a
consumer moves, spills and reloads with the `kmov` of that width. The System V
convention passes nothing in the opmask registers and preserves none of them, so
a call clobbers all of them: `tier1_cfg_ir.c` gives each call a fixed range of
the class over k1 to k7, which cuts every mask live across it around the call,
stored before it and reloaded after. Every use of a mask in the model needs a
register: the opmask instructions have no memory operand but `kmov`'s, and a
write mask is a register. No mask is rematerializable: its class has no recipe
(a general register's copy can't recompute it), so the CFG consumer marks none
(`Tier1CfgInput.rematerializable`), and none is recomputed or left out as a dead
definition.

Whether a spilled mask goes to memory or to a free general register is #179's
measured choice. `measure-mask-spills [--passes N] [--iterations N]
[--any-cpu]` (`mask_spill_measure.c`) times generated loops of 84 steps, each a
`kadd` of one to a mask after spilling and reloading it: by `kmov` of the
mask's width to its stack slot and back, to a general register and back, or not
at all (the control). It runs every width, one chain (a round trip's latency)
and seven independent ones (throughput, as masks saved around a call are), each
with and without four chains of `vpaddd` beside them, since `kmov k, r` takes a
vector pipe. It reports core cycles per step, the median of interleaved passes,
and requires the approved 9700X unless `--any-cpu`. `test-mask-spills` checks
every program: both encoders' bytes, its loop's instructions, and each mask's
final value against the chains' arithmetic where the host runs AVX-512. The
9700X job runs the measurement where its sources change.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/f43798bc303e26152c60c3f7acaabebe6ecded67/docs/measurements/mask-spills-179-2026-11-01),
from #997's CI job at `f96a98c8`), every width costs the same:

- A mask's round trip adds 12 cycles through memory and 10 through a general
  register to the `kadd`'s 1.
- Seven independent chains pay 1.21 and 1.08 cycles per spill and reload,
  beyond the `kadd`s' 0.5 per step.
- Four chains of `vpaddd` beside the masks change neither cost.

So a general register is the cheaper place, by 2 cycles of latency and about
11% of throughput. Tier 1 keeps its masks' spills in their slots all the same:

- The general register has to be free for as long as the mask is spilled. The
  loops gave it one for nothing.
- Across a call it would have to be callee-saved, since every opmask register is
  caller-saved. The prologue would then save it, and it would compete with the
  general values for the callee-saved registers.
- More masks live than k1 to k7 hold is rare, and the measured saving is small.

Spilling to a general register is left to a user whose masks show pressure.

VP2INTERSECT's pair of masks, the first even, is no constraint the allocator
takes: the IR has no operation that writes one, and Tier 0's X86 rows keep it
inside a row (docs/tier0.md's "Vectors"). A form that needs one would come
with a selector that makes it, as #141's vector selection.

`tier1_cfg_verify` is the independent oracle. It computes liveness by walking
each use back to its definition, simulates each block's locations from its
entry state as the emitter runs the fragments (stores, then reloads and
recomputations, at each transition; fixed ranges clobber their registers), and
checks that every use finds its value and that no write lands on a value the
allocation still keeps in that location; a split's moves run between the
position's stores and its reloads. Once they have run, each piece of a range cut
at that position must find its value where the piece's first fragment keeps it,
whether or not a use reads the piece: a piece without uses (a mask's past a
call, often) would otherwise leave a move to the wrong register unseen, which
#997's review found. For each edge it applies the moves in order
to the predecessor's final state: each must read the value it says it carries
and write a location of that value's class, and the successor's entry state
must then hold every live-in value and each phi's operand.

`test-tier1-cfg [--seed N] [--count N]` runs eleven fixtures (a diamond, a loop
phi, a phi swap, a critical edge, one register, a swap without general
registers, whose slot cycle needs the temporary slot, a double live across a
call, which a fixed range splits, and the opmask class's: a phi swap with k1 and
k2 alone, whose cycle goes through k0, a swap without opmask registers, through
the class's temporary slot and k0, two masks across a call that clobbers every
mask, four moves at its splits, and eight masks live at once in k1 to k7, one of
them spilled) and `--count` random SSA programs (2,000 by default) over CFGs
with loops, self-loops, irreducible regions, narrowed masks, required uses,
fixed ranges and rematerializable values of the three classes, a quarter of the
values masks in one to seven of k1 to k7 and half the opmask class's fixed
ranges a call's, over all of them, every use dominated by its definition
(`dominators.c`). The masks must be moved, spilled and split. Both entry points' results must pass the
checker and, up to 600 intervals, the interval verifier, and the two must agree
on which cases they refuse. On every fixture and every eighth program, mutants
must fail the checker: a register fragment given the register of another
value's fragment that overlaps it, an edge's or a split's move run before a
move whose source it overwrites, a split's move to a register sent to another
of its class's registers, and a mask's register fragment moved to k0. A mask
class with k0 among its registers must be refused by both paths. Over seeds 1
to 20 and the default, every mutant fails the checker.

### The vector class's floating values (#141)

Tier 1's CFG consumer gives `F32` and `F64` values the second class, the vector
registers: xmm0 to xmm14, with xmm15 the class's scratch for cycles and
slot-to-slot moves (`BUSTER_TIER1_EMIT_VECTOR_REGISTERS`, or the
`Tier1EmitOptions.vector_register_mask` of them, which the tests narrow to force
spills). A value's slot is its width, which `movss` or `movsd` spills and reloads,
and a move between two registers is a `movaps`, which copies the whole register
rather than merging into its destination as the scalar moves do. The class's
temporary slot holds a value at its width too. The System V convention preserves
no xmm register, so a call saves each floating value live past it in its
register's row before the call and reloads it after, by `movsd`, as it saves the
masks (`Tier1EmitPrepared.vector_save_mask`: rows after the opmask registers'),
rather than through fixed ranges. No floating value is rematerializable: a
constant's recipe would need its bits in a general register first, which the
recipes don't make.

The IR's vectors (#141), `v4i32`, `v8i32` and `v16i32`, are the class's values
too, at their widths, 16, 32 and 64 bytes: a vector's slot is its bytes, aligned
to 16 at most, which `vmovdqu32` of its width spills and reloads, and a move
between two registers is a `vmovdqa64` of its ymm or zmm register (a 16-byte
one's a `movaps`). In a function with vectors, the class's call-save rows and
its temporary slot take the widest vector's bytes: the saves around a call move
a register's whole widest vector (`Tier1EmitPrepared.vector_bytes`), and the
temporary slot fits any value a cycle stages there; without one they stay 8 bytes
and `movsd`'s. No vector is rematerializable either.

A floating parameter arrives in xmm0 to xmm7 by its place among the function's
floating parameters (`tier1_select_parameter_place`), its definition bound to
that register (`Tier1CfgInput.definition_registers`) as a general parameter's
is to its own, and the entry's moves bring a free one where its allocation has
it, one parallel move per class. A parameter past xmm7 is in the incoming area's
next slot, which the entry loads with `movss` or `movsd`.

### On real code

`tier1_cfg_ir.c` turns a promoted function of the shared IR into an allocation
problem, until the selector makes general CFGs' (#141): two positions per
instruction (three where it clobbers registers between its uses and its
result), a call's caller-saved general registers and every vector and opmask
register, division's rax and rdx and a variable shift's rcx as fixed ranges,
constants and addresses rematerializable, and an instruction's first operand, a
store's value and every use of a mask needing registers. Masks (`m8` to `m64`)
are the opmask class's, in k1 to k7 with k0 the scratch. A function with a value
of a type the model has no class for (long double, 128-bit integers) is left out
and counted.

`test-tier1-cfg-source [--simd] [--print NAME] [options] file...` prepares every
function `-c` compiles as `test-ir-source` does, lowers and promotes it, adapts
it, allocates it by both paths and checks both with `tier1_cfg_verify`; any
refusal but the model's, a rejection or paths that disagree fail it. With
`--simd`, a function with vectors, which the SSA promoter takes (#141), and one
whose only SIMD values are masks (#179) are promoted and allocated as any
other: #732's `vcmp` results reach the opmask class from real C, and the run
fails unless some do. On
`tests/ir/vector-i32.c` with `-march=x86-64-v4`, its three functions' 3 masks
allocate on both paths and pass the checker, and on `tests/ir/mask-builtins.c`
its four functions of the `_k*` builtins, promoted, allocate their 76 masks on
both paths and pass it. The correctness list runs it on both, and without
`--simd` on c-testsuite's subset, the frozen `sqlite3.c` and musl's units, whose
every function allocates and passes the checker on both paths:

| Corpus | Functions | Left out | IR instructions | Intervals | Left to the scan | Spilled values | Stores | Reloads | Moves | Split edges |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| c-testsuite's subset | 430 | 8 | 16,274 | 13,390 | 40.5% | 0 | 0 | 0 | 187 | 34 |
| `sqlite3.c` | 2,602 | 2 | 493,470 | 541,061 | 93.4% | 4,654 | 20,270 | 23,882 | 70,982 | 2,626 |
| musl | 2,883 | 223 | 228,216 | 228,195 | 88.1% | 1,995 | 5,980 | 7,277 | 22,004 | 982 |

These are the vector path's; the scalar path spills the same values with the
same stores and reloads, and its moves are within 0.4%. The scan takes most of
real code's intervals: a call's fixed ranges make every class that has one
irregular, which the coloring path doesn't take.

`benchmark-tier1-cfg-source [--only VARIANT] [--after-scalar MS] [options]
file...` times both paths' allocation of the same functions per IR instruction
and per typed entry, and the interval allocator's share (`tier1_allocate` on
the CFG's own intervals), with 21-pass medians. On the 9700X (CPU 2), on
`sqlite3.c`'s 2,600 functions, the vector path took 815 cycles per IR
instruction (1,014 per typed entry), 245 of them the interval allocator's; the
scalar path 1,462 (1,817), 888 of them its. Tier 0's whole compile of the same
functions (`benchmark-backend-source`) took 65.7 cycles per typed entry by its
vector path and 177 by its scalar path: Tier 0 has no allocation to time apart,
a value's register being its stack depth.

This first form keeps liveness's rows, interval construction, edge resolution
and the checker scalar around `dataflow.c`'s vector solve and the vector
allocator; at 570 of the vector path's 815 cycles per instruction, they are
the next to get vector paths. Each block's interval of a spilled value is stored
again where it is evicted, though its slot still holds it: storing a spilled
value once, at its definition, is the next change for the code.

Tier 1's machine consumer emits these allocations natively for every function
that isn't a straight-line chain (`tier1_emit_cfg.c`, in the [pipeline
guide](tier1-pipeline.md)): each edge's moves before the predecessor's jump, at
the successor's start, or in a stub for a split critical edge. The plan's
definitions are its uses there, required, since an instruction writes its
destination's register.

### Values live across calls (#1116)

A call saves each value that lives past it in a caller-saved register: the
value goes to its register's call-save row, a frame row of that register's for
the whole function. Only a register some call, or some fixed register's
sequence (a division's `rax` and `rdx`, a shift's `cl`), saves has a row (#177).
Before allocation the frame's reservations count a column for every
caller-saved register of a function that calls (`call_save_columns`). After it,
each consumer keeps the rows of those registers whose fragments cover a call's
or such a sequence's definition phase, other than their own value's definition
and a shift's count, which its shift reads in `cl` and leaves there
(`tier1_emit_fragment_saved`: `tier1_emit_saves` and `tier1_emit_cfg_saves`, the
latter beside the opmask and xmm registers' rows). Values live across a sequence
avoid its registers ("Values live across fixed sequences" below), so few take one. Every caller-saved register
used to have a row in every function that called, used or not: in `sqlite3GetInt32`,
64 of its 104 bytes. The tests check that the rows are exactly the registers the
records store to (`tier1_emit_test_general_rows_used`). On the 9700X, Tier 1's
frames went from 1.49× clang -O2's to 0.89× on `sqlite3.c`, and from 1.45× to
0.89× on Lua, against Tier 0's 1.27× and 1.21× ([the
record](https://github.com/buster14a/buster-performance/pull/260)).

Where the allocation puts it in a callee-saved register,
the prologue saves that register once instead (`tier1_emit_cfg_preference`). `rbp`
is the sixth callee-saved register where the frame keeps no frame pointer in it.
The frame's reservations, made before allocation, say so (`tier1_frame.c`'s
`reserved_registers`).

The preference is a second problem over the same values: the values that cross
a call allowed the callee-saved registers alone, the others the caller-saved
ones. Without a fixed range a value's intervals don't depend on the registers
it may take, so the CFG consumer allocates both problems in one call (#2538,
`tier1_emit_cfg_one_call`). `tier1_cfg_prepare` builds the intervals once
(validation, homes, hints, liveness, block lists, intervals). The preference
reads them: an interval crosses a call where the calls before its end and before
its start differ, two loads of a prefix count of the calls (the binary search
it replaced stays in the reference order). The preferred problem's column
(`tier1_cfg_column`) is assigned with the plain problem's verdict inside the
same call (`tier1_allocate_verdict`), from the plain problem's scans over the
events before the last bound definition ends; where that verdict is a
constraint, every parameter is freed and the preferred problem assigned again.
`tier1_cfg_finish` makes the lookup columns, slot sharing and edge moves once,
for the allocation that stands, the preferred one where `tier1_cfg_taken` takes
it; the plain problem is assigned whole only where it isn't
(`tier1_cfg_order_run`). The order before, two whole `tier1_cfg_allocate` calls
with the preference between them, stays as `tier1_emit_cfg_two_calls`: the path
for a problem with fixed ranges, and the reference that `--verify` runs after
the one-call order on every function, comparing the allocations field by field
(`tier1_cfg_differs`), the problems each left and the values each moved. What
the two calls cost, the design and what the one call measured are in
[tier1-one-allocation.md](tier1-one-allocation.md).

### Recomputable values read on every iteration (#2574)

Lua's `vmdispatch` is `goto *disptab[GET_OPCODE(i)]`, at the end of every
handler. Tier 1 computed `disptab`'s address in each handler, `lea
disptab(%rip)` before the dispatch's load, where clang -O2 keeps it in `r10`
and computes it again only after calls. Three changes keep it in a register:

- **One value.** IR lowering defines each direct symbol address a function
  takes at least twice once (`ir_typed_plan_hoisted`), as #1740 does for jump
  tables' bases: before the outermost loop that reads it where it can keep a
  register there (#2618), or where every path reads it (#2593's
  `ir_typed_plan_places`, the [IR guide](ir.md)'s preheader). `disptab`'s definition is before the dispatch loop.
- **Hot recipes.** In the preference, a recomputable value that crosses a
  call is *hot* where it is read on every iteration of the outermost loops it
  is live across (`tier1_emit_cfg_hot_recipes`, over `dominators.c`'s loops):
  a loop whose header it is live into but not its parent's, every path from
  the header back to it passing a block that reads the value. A hot value may
  take the callee-saved registers and `r8` to `r10`, the caller-saved
  registers no call takes before its fifth argument and no instruction takes
  implicitly. Every other value crossing a call keeps the callee-saved ones
  alone, as before.
- **Clobbers.** Where the function has a hot value, the preferred problem
  gets the calls' definition phases as clobbers of the caller-saved registers
  (`Tier1CfgInput.clobbers`, per problem: the plain one has none). A hot
  value in `r8` to `r10` leaves it at a call, as an eviction, and its next
  required use recomputes it; the call saves nothing for it.

This is the frequency weighting LLVM's spill weights make from block
frequencies, at the grain the scan's decisions need: a value read once per
iteration weighs as much as the loop's hottest one, and a frame object's
address read in a few of a loop's handlers doesn't. Hoisting alone had made
the table one long-lived value that the callee-saved registers' loop-carried
values evicted first, whose reload at each dispatch then evicted one of them:
`luaV_execute`'s instructions rose (the issue's experiment). A value read on
every turn of an inner loop but live across its parent isn't hot: its
registers are chosen for its whole range. Taking any loop's iterations instead
gave caller-saved registers to `sqlite3VdbeExec`'s frame objects' addresses,
which the edges after most opcodes' calls then recomputed: speedtest1 ran
38.4 M more instructions, Lua the same. Confining hot values to `r8` to `r10`,
without the callee-saved registers, gained Lua 5 M and lost speedtest1 6 M.

Callgrind's counts at `245e9969` against main `5f75ea52` and clang -O2
23.1.6, with #1189's `gap.c` over the same inputs as #1217's record (Lua's
five scripts, speedtest1's `--size 5` mix):

| | main | head | clang -O2 |
| --- | ---: | ---: | ---: |
| Lua, `luaV_execute`'s `disptab` `lea`s | 55.1 M | 18.6 M | 7.6 M |
| Lua, `rip`-relative `lea`s | 68.8 M | 32.3 M | 16.6 M |
| Lua, non-frame `lea`s | 216.7 M | 180.2 M | 148.8 M |
| Lua, whole processes | 5,963.7 M | 5,927.1 M | 4,207.3 M |
| speedtest1, `rip`-relative `lea`s | 59.1 M | 50.7 M | 34.3 M |
| speedtest1, whole process | 3,249.8 M | 3,246.0 M | 2,028.9 M |

`disptab` now stays in `r8` through the dispatch tails: `mov
(%r8,%rax,8),%rax`. Each of its remaining recomputations follows a call
(`luaV_mod`, `luaV_idiv`, `luaH_getshortstr`, the calls into a Lua function
that reach `startfunc`) or the function's entry, as clang's do; clang inlines
`luaV_mod` and `luaV_idiv`. Lua's other functions run the same instructions,
within the string hash's seed's noise (#1217's record).
On speedtest1, memsys5's functions gain from the hoist (each keeps `mem5`'s
address in a register over several reads) and a few lose: a hoisted address
computed at the entry on a path that never reads it, as
`yy_find_shift_action`'s two tables on its early return, or one that takes a
callee-saved register its prologue then saves. #2593 places the one
definition by its takes' dominators and loops instead (the [IR guide](ir.md)'s
preheader has the rule and the others measured): speedtest1 runs 8.1 M fewer
instructions than with every definition at the entry, and `luaV_execute` the
same.

After #2573's rip-relative symbols (main `9750b3f0`, against this change merged
at `f8aeb145`), Lua's scripts run 5,954.3 M and 5,912.9 M instructions and
`disptab`'s `lea`s are as above, but speedtest1 runs 3,233.3 M and 3,235.7 M:
#2573 already reads `mem5` through `rip`-relative operands, so memsys5's gains
are #2573's, and the losses above remain. Counting only the takes inside the
typed stream's loops (backward jumps' ranges) left those losers hoisted and
cost Lua 7.5 M.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/pull/476)),
by #1217's method, against both bases, the instructions fall but the time
doesn't move measurably:
- **Lua.** Its instructions fall 0.6% at each of six link orders. Its time is
  0.9992 (95% interval 0.9875 to 1.0110) against `5f75ea52` and 1.0061
  (0.9926 to 1.0198) against `9750b3f0`. The `lea` taken out of each dispatch
  is a one-cycle operation that the dispatch's load and indirect jump don't
  wait on once it has issued.
- **speedtest1.** Its time is 1.8% higher at the layout the driver's link
  gives against `9750b3f0`, with 0.08% more instructions. Six layouts (pads of
  0 to 3,088 bytes before the unit's code) give 0.9881 to 1.0169 in cycles,
  with a mean of 1.0020 (0.9918 to 1.0121): that +1.8% is layout, not the
  change's code.
- **Compile time.** `sqlite3.c`'s `--tier1 -c` takes 1.0% longer, from 50 M
  more instructions. By callgrind on a baseline build, they go to:
  - the hot-recipe test's loop analysis (`dominators_analyze`, 12.5 M) and its
    walks (8.6 M);
  - the allocator's longer intervals and clobbered scans (12.5 M);
  - the hoist's plan (3.3 M).

  The emitter doesn't receive the selector's dominator tree. Passing it
  would remove the first of these.

A parameter keeps its incoming register over its definition's interval, so a
parameter live across a run of calls in its entry block, as in Lua's
`luac.c:PrintCode` and its runs of `printf`, stays in a caller-saved register.
Saving it call by call was most of such functions' `call_stores` and
`call_loads`. Both consumers now save less often within straight-line code: a
chain, or a block of a CFG (`Tier1EmitSaves`).

- **Loads wait until a read.** A call doesn't load its saved registers back.
  It owes those loads until a node reads the register, or does anything that
  would move or replace its value: its transitions at either phase, its
  definition, or a fixed registers' sequence. The straight-line code's last
  node, which jumps, also takes them all back (`tier1_emit_owed`). At a return
  or a trap the owed values are dead, so they are dropped. A value read by none
  of the calls stays in its row across the run.
- **A row isn't stored again while it holds the value.** For each general
  register, the code keeps the owner its row was stored or kept for. A call
  stores a register only where its owner is now another value. A fixed
  registers' sequence, which stores those rows, forgets them. The rows are one
  vector of the 16 general registers' lanes, as the owners' first 16 locations
  are, so a call's kept registers are one compare and its rows one blend
  (`tier1_emit_kept`, `tier1_emit_saved`), each with a scalar twin that
  `test-tier1-emit` checks it against on random rows. A row stays its owner's
  while that SSA value's bits don't change, and they change only where the
  value is defined again, an iteration later.
- **Every CFG block starts with no rows.** A row that a block's one
  predecessor stored would still hold its value where that predecessor comes
  first in layout. But the code's certificate (#939,
  `tier1_certificate_emitted.c`) proves each block from its entry facts, which
  hold each value in one place, the allocation's, so it can't know the row's.
  The first call of a block stores again what it saves. On Lua 5.4.8, carrying
  rows into such blocks stored 3,163 values where this stores 4,253, in 255,363
  bytes of `.text` against 260,821.

`test-tier1-emit`'s call fixtures check this natively, against hostile helpers
that poison every caller-saved register (`tier1_call_test.c`):

- **A run of calls in one block**, where the second call stores none of the
  values live past both, and **two blocks**: the second call stores them again
  in a block whose one predecessor comes first, at a join and past a later
  predecessor.
- **Values live past a call** take the callee-saved registers the frame
  leaves. Six take all six, `rbp` among them, without a frame pointer. With
  one, five take exactly `rbx` and `r12` to `r15`, the frame pushing them and
  `rbp` as its pointer. (Six would split one there, which discards the
  preference whole, so they only run.) In
  `test-check-debug --cross-check`'s Tier 1 fixture, `six` is such a function,
  with a branch and a join that keep it on the CFG consumer, since the
  straight-line consumer takes no callee-saved register. Its FDE must save
  `rbp` in a frame without a frame pointer, `backtrace()` walks through it to
  `main`, and gdb steps every instruction of it.
- **Every fixture's records**, and every caller of the pipeline test's, show
  each call-save store loaded back before the code branches, returns or traps
  (`tier1_emit_test_saves_loaded`). That covers a general register's row, a
  floating value's xmm row (#141) and a mask's opmask row (#179), and the
  float, stack-argument and mask tests check it too.

Measured with #1116's 32-bit moves, after #1117, at `6e1e6121` against main `b70deb47`
([the record](https://github.com/buster14a/buster-performance/tree/b3b04fb39b07a267e9d5e0aba0346b739148a7fb/docs/measurements/tier1-code-quality-1116-2026-11-03)):

- **Lua 5.4.8 by `--tier1 -c`**, all 1,104 functions Tier 1's: `call_stores` go from 5,956 to 3,165, `call_loads` from
  5,956 to 5,207, and `.text` from 289,580 to 260,789 bytes, against clang -O0's 273,461.
- **The frozen `sqlite3.c`**, 2,666 of 2,668 functions Tier 1's: `call_stores` go from 28,027 to 15,079.
- **speedtest1 by `--tier1`, on the 9700X:** 3.2% fewer instructions and 1.3% less time. Lua's interpreter runs 3.0%
  and 3.8% fewer instructions on its tables and strings scripts, and as many on the other three.
- **Compile time:** `--tier1 -c` of `sqlite3.c`, at `3b195d57` against main `703ae9f5`, each built at six placements of
  the backend's code and timed over them and fresh copies, takes 0.10% more instructions and 0.33% more user cycles
  (main against itself: −0.05%). Before the definition's interval was looked up once and the rows kept in a vector,
  it was 0.17% and 0.50%. Throwaway builds price the instructions: the scan for owed loads, run twice, adds 3.09 M
  (0.053%), and `rbp` out of the callee-saved registers takes 3.72 M away, with 5,046 more records and 3,165 more
  spills in `sqlite3.c`. A bound at each call, skipping the scan before the first node that can need a load back,
  would spend a good part of the scan's cost on its own searches, so the scan stays.

An owed register's owner never changes while the load is owed: whatever would
change it takes the value back first. So the emitter's view of where each value
is stays the allocation's, which `tier1_cfg_verify` checks unchanged. The cost is a scan of each node's uses and transitions while some
load is owed, scalar work bounded by the node's own uses.

### Recipes give way to hinted values (#2618)

A block's ranges take the registers their values' hints name: a loop's values
keep theirs from block to block, and the edges move nothing for them. A
recomputable value, a constant or a frame object's or symbol's address, often
holds such a register when a range starts: it took the register while it was
free, in a block where the hinted value wasn't live, and nothing evicts a value
from a free register's competitor. The range then took another register, and
the edges into its block and out of it moved the value there and back. Now a
value without a recipe whose hint names a register a recipe holds, and no use
requires the recipe there, takes it, unless another range starting at that
position claimed it: the recipe leaves it as an eviction does, a
REMATERIALIZE fragment from that position, and takes a register again at its
next required use (`tier1_allocation_yields`). Each start or reload still makes
at most two transitions, its own and its victim's, so the output's bound stands.

By callgrind at main `26f0bb0e` (#1189's `gap.c`, #1217's inputs), Lua's
`luaV_execute` runs 2,365.8 M instructions instead of 2,415.0 M, 45.6 M fewer
moves and 3.6 M fewer frame addresses' `lea`s, and speedtest1 1.0 M fewer
(`sqlite3VdbeRecordUnpack` 0.8 M). On the 9700X, with #2618's placement test
([the record](https://github.com/buster14a/buster-performance/tree/afc37fed2abcb7a99fca93fb64a7af7d667d2575/docs/measurements/tier1-hoist-allocator-2618-2026-11-13)), Lua's instructions fall 0.91% at each of six link
orders and its time is 0.9979 (95% interval 0.9902 to 1.0057). Its arithmetic
script runs 3% to 6% slower at five of the orders and 3% faster at the sixth.
That is no branch mispredictions, decoder ops or load stalls the change adds,
and the same build varies as much between orders. Letting the values claim such registers among
a position's starts as well changed no executable. `tier1_allocate_hints_test.c`
has the rule's two traces on all three entry points: a recipe that leaves its
register to the value its hint names, and one a use requires, which keeps it.

#2618 also measured giving way in other ways, which lost:

| Variant | speedtest1 | `luaV_execute` |
| --- | ---: | ---: |
| this rule | −1.0 M | −49.2 M |
| a required recipe's victim by its next use, not the lowest register | 0 | |
| symbol addresses crossing a call allowed `r8` to `r10` too, calls evicting them, as hot ones | +32.1 M | |
| the same, in `r8` to `r10` alone | +30.6 M | |
| every recipe crossing a call so | +44.5 M | +274 M |
| a recipe crossing a call allowed the caller-saved registers where no callee-saved one is free | +159 M | |
| callee-saved registers only on the intervals that cross a call | +109 M | |
| an unrequired recipe kept out of callee-saved registers no range has held yet | 0 | |

In `sqlite3VdbeExec`, the symbol addresses that cross calls were recomputed
after each opcode's calls in the caller-saved registers (+32.6 M `lea`s), its
frame objects' addresses too where those were allowed them (+92.3 M), and with
callee-saved registers only where an interval crosses a call its symbol
addresses moved between the two sets at its blocks' edges (+77.2 M `lea`s,
+26.0 M moves). A recipe's definition is a required use, so the last variant
reached none of them: `jsonTranslateBlobToText`'s `sqlite3CtypeMap` still takes
a fresh callee-saved register at its switch, read only in cases' loops that
rarely run, and its prologue saves one more register on every call (0.89 M more
than without the hoist).

### Increments after their last reader (#2592)

Lua's `vmfetch` ends every handler with `i = *(pc++)`. The lowering defines
`pc + 4` before the load that reads `pc`, so `pc` was still live where the
increment was defined, and the increment took another register: `lea
0x4(%r14),%rbp` and then `mov (%r14),%r14d`, the loaded `i` in `pc`'s register,
which the load freed. The dispatch join's phis have `pc` in `r14` and `i` in
`rbp`, so every dispatch swapped the two through the scratch register: `mov
%r14,%r11; mov %rbp,%r14; mov %r11,%rbp`. clang loads first and adds in place:
`mov (%r14),%ecx; add $4,%r14`.

The operand sink (`tier1_schedule_ir_sink`, in `tier1_schedule_ir.c`) gives the
allocation that order. It runs on every function, on the IR before the optimizer's
final verification, as `--tier1-schedule=pre` does, and moves an expression to
right after the last instruction of its block's segment that reads its operand
`a`. The expression is pure, total and cheap integer and pointer arithmetic,
constants and conversions included, and its root is a phi's incoming value.
`a` is the only operand it takes from outside it, besides constants. Nothing
reads the root before its new place, nor the expression's other values but the
instruction of it that each feeds. The expression's result is then defined
where `a` has died, and takes the register its phi's hint names, which is
`a`'s. Moving it can't raise register pressure:
over the distance it moves, the root was live and now `a` is, if `a` wasn't
already. The last reader isn't one of the operations that move, so it keeps its
place. It is no truth value either, so a branch or extension still finds its
comparison right before it, and a load still folds into its consumer, since
nothing moved is a memory access. `--verify` checks the order with the
preallocation schedule's reference (`tier1_schedule_ir_verify`).

The phi must be `a`'s own: `a` is that phi, a value computed from it by such
operations, or a join of its values (and, since #2782, a join of such joins, or
a value the phi itself joins: "A handler a goto enters" below). A Lua handler
that may skip a word joins
`pc`'s paths before its fetch. A list walk shows why the rule is needed:
`pp = &p->next; p = p->next` makes two values from `p`, and the load is `p`'s
own next value, which wants `p`'s register. Moving `pp`'s increment past the load
gave that register to the increment, and the loop's back edge swapped the two:
speedtest1's `vdbeSorterMerge` ran 2.82 M more instructions in the change's
first form, which moved any expression of one value.

By callgrind at main `ca1ede6a`, with #1189's `gap.c` and #1217's inputs (Lua's
five scripts with a fixed string-hash seed, and speedtest1's `--size 5` mix):

| | main | this | clang -O2 |
| --- | ---: | ---: | ---: |
| `luaV_execute`'s instructions | 2,365.8 M | 2,250.8 M | |
| `luaV_execute`'s register moves | 438.8 M | 324.6 M | 322.9 M |
| of them through `r11` | 81.2 M | 9.7 M | |
| Lua's five scripts, whole processes | 5,236.6 M | 5,121.1 M | 4,207.3 M |
| speedtest1, whole process | 2,873.3 M | 2,869.0 M | 2,028.9 M |

clang's column is #2574's. Of the moves through `r11` that are left, 7.9 M are
one edge of `OP_CALL`'s path into `startfunc`, which swaps `rbx` and `r15`, and
0.8 M are `OP_NEWTABLE`'s shift by `cl`, which isn't an edge's (both gone since:
"Recipes no block reads, settled phi places, and the fixed rules' scratch"
below). 16 of
`luaV_execute`'s 78 indirect jumps still follow a swap, down from 59, and they
take 1.0 M of the moves. Some of these handlers increment `pc` inside the handler,
as `OP_LOADKX` does, so the handler leaves `pc` and `i` in each other's
registers. Others reload `trap` after a call into a register other than the
join's, as `OP_POW` does. A loop's backward jump moves `pc` by a variable
amount, which isn't of the phi's family either. No other function of Lua runs
more instructions. In speedtest1, two functions run more, `instrFunc` 0.17 M and
`allocateBtreePage` 0.16 M, where moves change which counters spill. Nine run
fewer, `sqlite3VdbeExec` 2.18 M of them. The pass moves 76 expressions in
`lvm.c` and 62 in `sqlite3.c`, whose records fall from 285,832 to 285,492 and
spill stores from 8,429 to 8,327. `tests/driver/tier1_patterns.c` checks the
fetch natively: each handler's indirect jump follows the target's load, with no
move between them.

The pass takes no use counts. Its candidates are the phis' incoming values, and
a scan of each candidate's window of at most 64 instructions finds the readers
it needs. A function with nothing to move allocates no per-instruction column
but the scans' bits, and one with something permutes only the span from its
lowest moved instruction to its last anchor. The pass's first form, which #2705
merged, counted every value's uses over every operand and permuted the whole
function. On the 9700X, against the compiler before the pass:

| `--tier1 -c`, user counts | first form | without use counts |
| --- | ---: | ---: |
| `sqlite3.c`'s instructions | +1.07% | +0.18% |
| `sqlite3.c`'s cycles | +1.57% | +0.29% |
| `lvm.c`'s instructions | +0.96% | +0.63% |
| `lvm.c`'s cycles | +0.79% | +0.35% |

On `sqlite3.c` the pass looks at about 11,000 candidates, scans 2,000 windows
and renames the operands of 48,500 of the 561,500 instructions it sees; how the
rest of the difference splits between it and the later stages, whose input it
changes, isn't measured. On `lvm.c` the moved expressions span almost all of
`luaV_execute`, so limiting the permutation saves nothing there.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-sink-2592-2026-11-14)), with the
pass as #2705 merged it (its `luaV_execute` is the same), the instructions fall
but the time doesn't measurably. Lua's five scripts run 2.20%
fewer instructions at each of 33 link orders, and their user cycles change by
-0.10% (95% interval -0.64% to +0.44%). Zen 5 eliminates register moves at
rename, and the dispatch's critical path, the load of `i`, its opcode and the
table's load, never included them. By script the cycles don't follow the
instructions: `strings` runs 1.6% faster with 0.17% fewer instructions,
`closures` 1.2% slower with 1.1% fewer and `arithmetic` 0.5% faster with 5.7%
fewer. Branch mispredictions are the same in both, and their changes from order
to order don't follow the cycles'; every order shares `luaV_execute`'s own
layout, which the change moves, and nothing here separates that layout from the
removed moves. speedtest1 runs 0.34%
fewer user cycles over 8 placements (-0.71% to +0.04%).

### Two-address results (#2685)

A two-address instruction writes its first operand's register: `sub`, `and`,
`imul` with a register or memory operand, `neg`, a copy, a select's `cmov`.
Its records copy that operand into the destination first where the
allocation gave the result another register, and the result took the lowest
free register, the operand's only by chance. #2618's experiment of leaving
rip-foldable takes out of the hoist found it: without the hoisted address in
`rax`, `memsys5Unlink`'s second `imul` took `rdx` rather than its dying `edi`,
and copied `edi` into it on every call.

`tier1_select_ties` names, per node, the operands whose register its result
may take: the first operand of each rule whose records copy it into the
destination, a commutative operation's second too (its records swap the two
where the destination is the second's), and a select's arms. Each consumer
hints the result's definition to one of them whose range ends at the
definition, where nothing else hints it:

- the CFG consumer's problem by `Tier1CfgInput.ties`, two values per value,
  which `tier1_cfg_ties` makes an exit hint to the operand's range
  (`BUSTER_TIER1_ALLOCATE_HINT_EXIT`): its register as it ended, wherever the
  scan moved it;
- the straight-line consumer's by the allocator's value hints, which its input
  had none of (`tier1_emit_prepare`): the hinted allocator then takes it.

An operand live past the definition still holds its register there, so only a
dying one can be taken; an addition of two registers is a three-operand `lea`
and ties nothing. A value a return reads takes no tie
(`tier1_select_returned`): the return wants it in `rax`, where the lowest free
register usually is, and `memsys5Size`'s product, whose two operands end at its
`imul`, took the first one's `rcx` and was then copied into `eax` (0.48 M more
on speedtest1). `tier1_ties_test.c` checks both consumers' registers and runs
their code.

By callgrind (#1189's `gap.c`, #1217's inputs) at #2688's head `01a2d0b3`,
speedtest1 runs 7.80 M fewer instructions (2,991.51 M to 2,983.71 M), the
straight-line consumer's 0.43 M of it, and Lua's five scripts 21.85 M fewer
(5,300.87 M to 5,279.02 M): `sqlite3AddInt64` −1.31 M, `index2value` −9.14 M,
`luaV_execute` −3.87 M. Without the returned-value rule, `memsys5Size` lost
0.48 M; with it, no function loses more than 23 K.

### Recipes evicted before a read (#2618)

A recipe often enters a block in a register its range's hint names and loses
it to the block's first values before any use reads it. `tier1_cfg_entry_unread`
already enters such a value as its recipe, so that the edges into the block
move nothing for it; but the scan still noted the register as the range's
opening, and a range that takes the block's entry convention took it again.
`hexFunc`'s loop is laid out body first: the header, after it, takes the body's
convention (#2553), so its range of `hexdigits`, which reads nothing there,
took the register the body's range had opened in before its first value
evicted it, and the back edge moved the table into it on every iteration.

- **Evicted before a read.** A recipe evicted where it has held its register
  since its range's start and no use has read it there is recomputed from that
  start instead (`tier1_allocation_evict`), and its opening becomes the memory
  hint. An eviction by a required value and by a hinted value without a recipe
  does so; a call's clobber doesn't: a dispatch loop's handler that calls
  before its first read would leave the table without a register at every
  handler's entry, as each takes the first one's convention (`luaV_execute`
  ran 93.5 M more instructions that way).
- **Resting.** A recipe's range with no use, starting where nothing requires
  it, takes no free register where its hint is another range's opening that
  took none: the convention of a block it takes, as `hexFunc`'s header takes
  its body's. It took one, and the edges into its block then moved or
  recomputed the value into it, though the block never read it. A range
  hinted to a closing that a call left without a register keeps taking one
  (below).
- **Definitions no edge reads.** A definition whose register fragment reaches
  its block's end is left out where no edge leaving the block reads it: each
  successor the value is live into enters with its recipe, and no phi takes it
  along the edge (`tier1_emit_cfg_dead_definition`, "Rematerialization"
  below). `hexFunc` computed `hexdigits` before its loop on every call for a
  header that rests. `tier1_dropped_test.c`'s `r` is that loop.

By callgrind (#1189's `gap.c`, #1217's inputs) at #2688's head `01a2d0b3`, with
#2685's ties before it, the two rules took speedtest1 from 2,983.71 M to
2,969.60 M instructions and Lua's five scripts from 5,279.02 M to 5,151.74 M,
the resting rule 12.54 M and 127.28 M of it alone: `luaV_execute` −125.8 M,
68.0 M fewer register moves and 48.2 M fewer frame `lea`s, and
`sqlite3VdbeExec` −7.19 M. The variants measured on the way:

| Variant | speedtest1 | Lua's scripts |
| --- | ---: | ---: |
| evicted before a read, by a required value | −1.99 M | −0.24 M |
| the same, and by a hinted value | −2.27 M | −0.31 M |
| the same, and by a call's clobber | −1.53 M | +92.9 M |
| resting alone, on openings and closings | −12.54 M | −127.28 M |
| both, with the opening's memory hint | −14.11 M | −127.28 M |

Main's #2592 (the operand sink, "Increments after their last reader" above)
removed much of the same traffic in `luaV_execute` since. On main `98186d5e`,
resting on closings too gained speedtest1 8.90 M but cost Lua's scripts 7.64 M:
`luaV_execute`'s dispatch join, whose ranges then rested where a handler's call
had taken `disptab`'s register, recomputed it on every dispatch (14.3 M more
`lea`s, and 45 M more moves). Resting on openings alone gains speedtest1 1.73 M
and Lua 0.34 M there, and keeps `hexFunc`'s header without a register.

`tier1_allocate_hints_test.c`'s traces check the three eviction sites and the
resting range, and its quiet-range trace now gives its register up.

With #2685's ties, the IR guide's folded takes (#2618), the eviction before a
read, resting on openings and the definitions no edge reads, by callgrind at
main `98186d5e` (after #2592) against it, speedtest1 runs 2,865.62 M
instructions instead of 2,878.38 M (−0.44%) and Lua's five scripts 5,100.92 M
instead of 5,122.80 M (−0.43%; each script −0.14% to −0.88%): `luaV_execute`
−7.04 M, `index2value` −9.14 M, and no Lua function more. speedtest1's largest
losses are `allocateBtreePage` +0.22 M and `sqlite3BtreeIndexMoveto` +0.09 M,
against `sqlite3VdbeExec` −3.02 M, `sqlite3AddInt64` −0.96 M and
`insertCellFast` −0.96 M. Before #2592, at main `389d25b9` and with resting on
closings too, the change took speedtest1 from 2,871.10 M to 2,851.20 M (−0.69%)
and Lua's scripts from 5,235.80 M to 5,080.78 M (−2.96%; `luaV_execute`
−139.3 M); #2592 took most of `luaV_execute`'s part on its own.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/4cbed5edd1ca723aabb3c523bc024b59e78c2440/docs/measurements/tier1-ties-unread-2618-2685-2026-11-14),
#2593's method, a hold of the lock per round), main `389d25b9` against the
change's first form (`282d1157`: resting on closings too, without the
definitions no edge reads, before #2592): Lua's five scripts take 0.9875 of
the time (95% interval 0.9599 to 1.0160) over six link orders, with 2.75% fewer
instructions at each, faster at five of them and 3.3% slower at one; speedtest1
takes 0.9941 (0.9907 to 0.9974) over six layouts, its cycles lower at all six.
That head's `--tier1 -c` of `sqlite3.c` took 1.24% more cycles for 0.22% more
instructions: the ties and the returned values' marks read each node's
selection pattern again after the problem's first pass had (+10.2 M cycles in
`tier1_emit_cfg_problem`), and every straight-line function took the hinted
allocator (+4.0 M). The first pass now records them, the values' pass maps
them to values, and the straight-line consumer hints only where a tie exists,
with the same code out: over six placements of the backend unit and two fresh
copies of each, the compile takes 0.60% to 0.73% more cycles than main's in
three rounds, against 0.98% to 1.07% before; main against itself, within 0.07%.
The ties' work stays scalar: one test of a node's rule and two loads of its
pattern, in a pass that reads the pattern anyway.

After #2592, on the 9700X ([the record's section 6](https://github.com/buster14a/buster-performance/tree/01844589ee71fe62c8eea4e0cfd3c87b201a3de6/docs/measurements/tier1-ties-unread-2618-2685-2026-11-14),
the same method), main `98186d5e` against the change as merged (`c6900890`'s
code): Lua's five scripts take 1.0022 of the time (95% interval 0.9912 to
1.0133) over six link orders and speedtest1 1.0003 (0.9934 to 1.0073) over
six layouts, for 0.44% and 0.45% fewer instructions; `sqlite3.c`'s
`--tier1 -c` takes 0.69% more cycles for 0.42% more instructions. What the
change gains after #2592 is those instructions; the time doesn't resolve it.
Lua's `arithmetic` script moves with the link order (0.973 to 1.079): at order
1, linked after pads that move all the code by whole 64-byte lines, the change's
build mispredicts 330 K more branches and runs 7.9% to 9.0% more cycles at one
phase of address bit 6, and 0.5% to 0.7% more at the other, where main's is
the same at both. The extra mispredictions fall in `luaV_mod`, whose code the
change leaves the same, and in `luaV_execute`'s dispatch: the predictor's
aliasing at the change's branch addresses, not instructions it adds.

### Recipes that wait (#2618, #2735)

On main `56b5864`, against the same compiler with no hoist at all, the hoisted
addresses (the IR guide's #2618) still cost `jsonLookupStep` 295 K and
`speedtest1_run` 141 K by callgrind, through the registers their recipes held.
Two rules of the scan take them back, with the IR guide's argument takes:

- **A closing a call emptied.** A range that ends without a register notes
  memory as its closing, and a range its hint names there followed it. Where a
  call's clobber took the register (#2574's preferred problem), that would make
  `luaV_execute`'s dispatch join leave `disptab` in memory once a handler's call
  had evicted it (#2735). The clobber site marks the closing
  (`BUSTER_TIER1_ALLOCATE_CLOBBERED`), any other eviction clears the mark, and
  `tier1_allocation_note_closing` then notes no hint: the next range follows
  its value's last register.
- **Waiting for a read.** A recipe whose hint is memory takes no register at
  its start, read or not; a required use gives it one there.
  `jsonLookupStep`'s `sqlite3CtypeMap`, defined before its loops and read past
  one of them, took the lowest free register at that loop's latch, `rbp`,
  which the loop's frame address followed: every iteration parked the address
  in `r8`, recomputed the table into `rbp` and moved the address back (three
  instructions, 98 K times). An unused one rests at a closing that held no
  register too, #2735's first ask, which `speedtest1_run`'s loop join needs
  for its counter to keep `r14`.

By callgrind on main `45cd9ae` (#1189's `gap.c`, #1217's inputs), the change
takes speedtest1 from 2,626.66 M instructions to 2,625.62 M (−0.04%) and Lua's
five scripts from 4,773.9 M to 4,768.9 M (−0.10%); with no hoist at all it runs
2,633.84 M and 4,812.5 M. Main's #2734 ("Recipes no block reads, settled phi
places" below) had taken most of what these rules gained before it: on main
`56b5864` they took speedtest1 0.13% and Lua's scripts 1.44%. Each rule's part,
turning it off:

| Without | speedtest1 | Lua's five scripts |
| --- | ---: | ---: |
| (all) | 2,625.62 M | 4,768.9 M |
| the clobber's closing | 2,625.34 M | 4,821.2 M |
| waiting | 2,626.65 M | 4,771.4 M |
| waiting for unused recipes | 2,625.92 M | 4,769.3 M |
| the argument takes | 2,625.63 M | 4,768.9 M |

The clobber's closing gains Lua 52.3 M (`disptab`'s dispatch join) and costs
speedtest1 0.28 M; waiting gains `jsonLookupStep` 294 K and `str_gsub` 3.4 M, and costs
`sqlite3VdbeExec` 2.44 M: at its unused recipes' resting, the registers they
left took other values, and a handler's block swaps two loaded values through
`r11` on the way to its join. Without waiting for unused recipes,
`sqlite3VdbeExec` runs as on main, but `speedtest1_run` keeps 70 K against no
hoist and speedtest1 loses 0.30 M in all. `tier1_allocate_hints_test.c`'s
traces check the clobber's closing and the wait on the three paths.

A third rule, measured on main `56b5864` and left out, rested an unused recipe
at the entry of a join of three or more predecessors: each edge into the join
recomputed it, where its reads past the join, on fewer paths, would. It took
`sqlite3VdbeExec`'s dispatch join's frame address out of `r15` (−11 M there),
but needed #2574's hot test over every recipe and cost `sqlite3Parser` 1.2 M;
#2734's proof that no block reads a register now enters such recipes as recipes
after the allocation, and on `45cd9ae` the rule gains speedtest1 0.58 M and
costs Lua 0.2 M.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/cc05fb63e9bf0c495940bd6918d18e03a077cc31/docs/measurements/tier1-hoist-finish-2618-2026-11-14),
#2593's method, a hold of the lock per round), main `45cd9ae` against the
change: speedtest1 takes 0.9954 of the cycles (95% interval 0.9922 to 0.9986)
over six layouts, for 0.04% fewer instructions; Lua's five scripts take 1.0002
of the time (0.9926 to 1.0079) over six link orders for 0.10% fewer
instructions, each script's cycles moving with the link order for the same
instructions (`closures` 0.981 to 1.035). `sqlite3.c`'s `--tier1 -c` runs 0.05%
more instructions.

### Recipes no block reads, settled phi places, and the fixed rules' scratch (#2734, #2592)

What #2592's operand sink left of `luaV_execute`'s moves through `r11` was
mostly one edge. Lua's `OP_CALL` enters `startfunc` with the callee's `ci`:

```
lea 0x68(%rsp),%rbx; lea 0x68(%rsp),%rbp; lea 0x58(%rsp),%r12
lea 0x58(%rsp),%r13; lea 0x58(%rsp),%r14; lea disptab(%rip),%r8
mov 0xc0(%rdi),%ebx            # trap = L->hookmask
mov %r12,%r14
mov %rbx,%r11; mov %r15,%rbx; mov %r11,%r15
```

Two things went wrong there:

- **Frame addresses that nothing read.** The five frame addresses
  are recipes whose ranges have no use in `startfunc` or in
  `returning` after it. Each block took a free register for them
  and lost it to `ci`'s state before any read. `tier1_cfg_entry_unread`
  enters such a range as its recipe only where it is evicted in its
  own block. Here the ranges carried the value to the next block in
  their registers, so every edge into the chain recomputed it.
- **`ci` and `trap` crossing.** `startfunc`'s `ci` phi followed its
  operand from the chosen predecessor, the new `ci` that
  `luaD_precall` returned into `r15`. `trap`'s load then took `rbx`,
  the lowest free register, and the edge into `returning`, whose
  phis hold `ci` in `rbx` and `trap` in `r15`, swapped them.

**Ranges carried to recipes** (`tier1_cfg_carried_unread`). The proof
runs after the assignment. It covers a recomputed value's range
that spans whole blocks (from one's start to the end of the last
that continues it in layout), has one register fragment, no use,
and whose value is no phi's operand. That range enters as its
recipe where every successor takes the value as its recipe: a
recipe's fragment, `tier1_cfg_entry_unread`'s case, or another
range this proof holds. A worklist starts from every such range.
It unmarks each that has a successor reading its register, and
puts its value's marked ranges in the predecessors back on the
list. The result is the greatest such set, so a loop that carries
a recipe it never reads is proven too.
`tier1_cfg_interval_entry` gives these ranges the recipe's
location, and the edges into them move nothing. Nothing changes
in the assignment, so the change can only remove edge moves. The
CFG verifier's walk checks it independently: an edge that read
such a register would find nothing there.

**A phi's settled place** (`Tier1AllocationInput.alternate_intervals`).
A block with one successor runs its edge to it every time it runs,
at least as often as all its incoming edges together. A phi there
that a phi of the successor takes now takes first the register that
phi's first range opened with, the successor's convention. It does
so only where that phi still holds the register last and the
register is free at the block's start. Otherwise the operand's exit
register stays the hint (#2553). Two looser forms each lost
elsewhere:

- following the successor phi's latest register gave Lua's handlers
  swaps of their own, where that phi had moved;
- following its first range without the check spilled phis in
  `sqlite3VdbeExec` (13.5 M more stack loads and as many stores),
  where that phi had moved to memory.

The edge now reads `lea disptab(%rip),%r8; mov %r15,%rbx; mov
0xc0(%rdi),%r15d`: 4 instructions with its jump, down from 13.

Three fixed sequences used the scratch register on their own.
Each is a register choice or a record's order, not an edge:

- **Address ties.** An address from a register, with no rip-relative
  part and no frame object, ties its base (`tier1_select_ties`, #2685):
  `lea d(%r),%r` costs what a `lea` into another register does.
  `OP_LFALSESKIP`'s `pc + 1` took another register than `pc`'s, and
  its fetch then swapped `pc` and `i`. It now takes `pc`'s, as `add
  $4,%r14`.
- **Shift by `cl`.** A shift by a register shifts in its destination.
  It goes through the scratch register only where the destination
  is `rcx`, or holds the count while the value is in `rcx`.
  `OP_NEWTABLE`'s `1 << (b - 1)` and Lua's `<<` and `>>` went
  through it every time.
- **Division by a register.** A division by a register divides from
  the divisor's own register. It goes through the scratch register
  only where the divisor is in `rax` or `rdx`, which the dividend's
  move and the extension write. #2541's unit inlining put Lua's
  integer `%` and `//` into `luaV_execute`, where the divisor's move
  ran 4.01 M times.

By callgrind (#1189's `gap.c`, #1217's inputs, Lua's hash seed fixed)
at main `21e0d999`:

| | main | this |
| --- | ---: | ---: |
| `luaV_execute`'s instructions | 2,311.8 M | 2,225.5 M |
| its register moves | 344.9 M | 322.2 M |
| of them through `r11` | 17.7 M | 5.0 M |
| its frame addresses' `lea`s | 58.3 M | 0.2 M |
| Lua's five scripts, whole processes | 4,878.4 M | 4,774.1 M (−2.14%) |
| speedtest1, whole process | 2,657.7 M | 2,626.7 M (−1.17%) |

- **By script.** Every script runs fewer instructions: `calls` −3.87%,
  `arithmetic` −3.15%, `closures` −1.96%, `tables` −1.94%, `strings`
  −0.65%.
- **By function.** No Lua function runs more than 50 K more. Besides
  `luaV_execute`, `luaH_getshortstr` runs −9.11 M and `match` −4.06 M.
  speedtest1's `sqlite3VdbeExec` runs −22.23 M. Its only function
  that runs over 50 K more is `resolveP2Values`, +0.31 M; 30 run
  over 50 K fewer.
- **Each rule's share.** Taking each one out of the change at its
  last commit before the division (speedtest1 2,627.26 M, Lua
  4,778.8 M):

| Without | speedtest1 | Lua's scripts |
| --- | ---: | ---: |
| the carried ranges' proof | +28.02 M | +79.2 M |
| the phis' settled places | −0.11 M | +15.2 M |
| the address ties | +0.06 M | +0.3 M |
| the shifts in place | +2.68 M | +12.6 M |

The division's own share is −0.61 M on speedtest1 and −4.7 M on Lua.

`luaV_execute`'s moves through `r11` that are left aren't edges'
swaps:

- **3.49 M: a divisor in `rax`.** The value is live past the
  division, so the records save and restore it besides moving it.
  Only the allocation avoiding `rax` and `rdx` for a divisor would
  remove them, as fixed ranges keep values out of a call's clobbers.
  #2767 does ("Values live across fixed sequences" below).
- **1.48 M: `OP_GETTABUP`'s fetch.** It swaps `pc` and `i` (as main
  does): the handler spills `pc` around its call, and reloads it into
  another register than the dispatch's.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-carried-recipes-2734-2026-11-14),
#2553's method, a hold of the lock per round), main `21e0d999` against this
change, the time follows the instructions, unlike #2592's earlier steps,
whose removed moves Zen 5 eliminates at rename:

| Program | Metric | change / main | 95% interval |
| --- | --- | ---: | ---: |
| Lua's five scripts, 33 link orders | user cycles | −1.40% | −1.75% to −1.04% |
| | user instructions | −2.08% | −2.10% to −2.06% |
| speedtest1, 8 layouts | user cycles | −0.60% | −0.92% to −0.27% |
| | user instructions | −1.20% | |

- **Lua.** It is faster at 29 of the 33 orders. By script, `arithmetic`
  runs −3.07% and `calls` −2.25%. `closures` and `tables` are within their
  intervals of zero.
- **speedtest1.** It is faster at all eight layouts.
- **Static reports.** Lua's objects move 2,530 values at edges instead of
  3,056 (`lvm.o` 881 instead of 1,196), and speedtest1's 35,590 instead of
  39,235. Spills don't change.
- **Compile cost.** `sqlite3.c`'s `--tier1 -c` takes 0.66% more cycles
  (0.61% more time) for 0.33% more instructions, and `lvm.c`'s 0.61% more
  cycles at the same time.

Tests:

- `test-tier1-cfg` counts the ranges the proof takes in its random
  cases and fails if they reach none. It marks ranges the proof
  refused, and every one fails the verifier.
- `tier1_allocate_hints_test.c` traces an alternate taken, and one
  whose phi moved or whose register another holds. Its random inputs
  give some ranges alternates, which its independent reference of
  the coloring rule models.
- `tier1_ties_test.c` checks an address's register.
- `test-tier1-select` runs every value, count and destination
  register's shift, and every dividend, divisor and destination
  register's division and remainder, natively against C.

### Values live across fixed sequences (#2767)

The CFG consumer's fixed sequences (`tier1_select_fixed`) write registers of
their own: a division by a register, a division by a constant's reciprocal and a
high multiplication write `rax` and `rdx`, and a shift by `cl` writes `rcx`.
Their consumer saves every other value live past one in those registers around
its records, in the register's call-save row (`tier1_emit_cfg_fixed`). Nothing
kept values out of them, and the scan takes the lowest free register, `rax`
first. Lua's integer `//` (`luaV_idiv`, which #2541 inlined into
`luaV_execute`) kept its divisor in `rax`, live past the division:

```
mov %rax,(%rsp); mov %rax,%r11; mov %r13,%rax; cqto; idiv %r11
mov %rax,%rcx; mov (%rsp),%rax
```

`luaH_getshortstr` kept a string's hash in `rcx` across `1 << lsizenode`. Such
saves ran 7.32 M times on Lua's scripts and 5.83 M on speedtest1, each a store
and a load. A divisor in `rax` or `rdx` that ends at its division, and a shift's
destination in `rcx`, also went through the scratch register.

**Sequences** (`Tier1CfgInput.sequences`). The consumer names each sequence by
its definition phase, the registers it writes that values may take, and three
values:

- its reader (`tier1_select_fixed_reader`): a shift's count, which the shift
  reads in `cl` and leaves there;
- its operand (`tier1_select_fixed_operand`): a division's divisor, and a
  shift's other operand, which its destination takes where it dies;
- its result (`tier1_select_fixed_result`): a shift's destination, which shifts
  in the scratch register where it is `rcx`.

**The registers a range avoids** (`tier1_cfg_crossed`).

- A general range lives across a sequence where the sequence is after its start
  and before its end. The sequence's own value starts there and its dying
  operands end there, so neither avoids its registers by living across it: a
  quotient may still take `rax`, a dividend end in it.
- A range of a sequence's reader doesn't avoid that sequence's registers.
- A sequence's operand and result avoid its registers.
- A whole phi web avoids the union, in all its ranges.
- Each position's count of the sequences before it and a prefix count per
  register over the sequences find a range's in two loads, so a range over a
  long run of sequences costs no more.

**Avoided, not refused** (`Tier1AllocationInput.avoided_registers`). The scan
takes a register an interval avoids only where no other register it may take is
free. It doesn't do so for the interval's hint, and it evicts no value to keep
out of one. The common path's hinted copy, which the CFG's problems take, does
the same with its own swap: a start whose level's register it avoids takes,
among the free registers it doesn't avoid, its hint, else the lowest, from a
free level above or the never-taken registers, as it takes a hint. Without
hints, a class with an interval that avoids a register it may take goes to the
scan. A function whose ranges cross a sequence so keeps the allocation it had
but for those swaps. Sending every such class to the scan instead gave Lua the
same objects and speedtest1 61 more instructions, and compiled `sqlite3.c` in
the same time on the 9700X.

**Readers.** A shift whose count is in `rcx` writes nothing of `rcx` but its
destination. Its consumer no longer saves the count around it, and the frame
gives it no row (`tier1_emit_save_positions`' readers). `memsys5Free`'s loop had
saved `iLogsize` around its `sar %cl` on every iteration.

Lua's division now reads:

```
mov (%r14,%rcx,1),%rcx   # the divisor, loaded where it divides from
...
mov %r13,%rax; cqto; idiv %rcx
```

and `luaH_getshortstr`'s mask is `mov $0x1,%r8d; movzbl 0xb(%rdi),%ecx; mov
%r8,%rdi; shl %cl,%edi`.

**Measured.** By callgrind (#1189's `gap.c`, #1217's inputs, Lua's hash seed
fixed), #2766's head `3652581` against this rule:

| | base | this |
| --- | ---: | ---: |
| Lua's five scripts, whole processes | 4,774.6 M | 4,752.8 M (−0.46%) |
| `luaV_execute` | 2,225.5 M | 2,215.0 M |
| its moves through `r11` | 5.0 M | 1.5 M |
| saves around Lua's sequences | 7.32 M | 0 |
| speedtest1, whole process | 2,626.71 M | 2,613.07 M (−0.52%) |
| saves around speedtest1's sequences | 5.83 M | 0.44 M |
| divisors moved through `r11` in speedtest1 | 1.79 M | 0 |

- **By function.** On Lua, `luaV_execute` −10.47 M, `luaH_getshortstr`
  −5.78 M, `mainpositionTV` −2.42 M, `luaH_free` −1.20 M, `luaH_resize`
  −0.80 M, `luaH_newkey` −0.80 M; none runs more. On speedtest1,
  `sqlite3BtreeIndexMoveto` runs +0.97 M: its hot loop has every register busy,
  and a division that speedtest1 never runs moved its `pRes` out of `rdx`, which
  reshuffled the loop's registers.
- **What's left.** speedtest1's saves are of parameters whose first range is
  bound to its incoming register (`pcache1Fetch`'s `createFlag` in `rdx`).
  `luaV_execute`'s moves through `r11` are #2768's `OP_GETTABUP` fetch.

**What lost** (against the base, before the operand and result rule):

| Form | speedtest1 | Lua's scripts |
| --- | ---: | ---: |
| ranges refused the registers (allowed sets), no readers | −2.85 M | −6.9 M |
| the same, with readers | −5.65 M | −6.9 M |
| ranges avoid the registers, each on its own | −4.89 M | −6.9 M |
| ranges avoid them, but a hint to one wins | −3.38 M | 0.0 M |
| each value's ranges avoid them together | −10.71 M | −10.4 M |
| each phi web's ranges avoid them together | −11.13 M | −11.2 M |
| the same, with the operand and result (this rule) | −13.64 M | −21.8 M |

- **Refusing the registers** spilled `sqlite3BtreeIndexMoveto`'s `pRes`. Its
  range in the block of that never-run division found every other register
  held, and the slot then followed it into its hot loop: +3.84 M.
- **Each range on its own** moved Lua's divisor out of `rax` on the edge into
  its division's block. A range in a cold block also took a free register that
  its value's later ranges then followed by their hints, which the loop's
  values were already using.
- **A hint first** kept Lua's divisor in `rax`: its range enters the division's
  block hinted there.
- **Without readers** (the first row), `memsys5Free` ran +1.13 M: `iLogsize`
  moved out of `rcx` in its loop, where it is the shift's count, and back on its
  edges.
- **Without the result,** Lua ran 6.3 M more: `luaH_getshortstr`'s count dies at
  the shift, and its destination took the `rcx` it left.

**Compile cost.** On the 9700X (`measure-compile`, CPU 2, `--tier1 --lanes 1 -c
sqlite3.c`, 21 runs a revision in a Williams order against base), the first form
of the change took +0.80% wall time and +0.70% instructions: two halvings per
range, two passes over every node, and the scan. Finding the sequences by a
count per position and filling them in one pass over the functions that have
any took that to +0.39% and +0.45%. The rule as it stands, with the operands
and results, takes +0.44% wall time (618.7 against 616.0 ms) and +0.43%
instructions; Lua's `lvm.c` compiles in the same time. In callgrind's count of
the scalar host build, the same compile runs +0.25% (`tier1_cfg_prepare`'s
sequences, the consumer's pass and the allocator's avoidance).

**Run time on the 9700X** (#2553's method: Lua's objects in 33 link orders,
speedtest1 after 8 pads, 5 and 7 rounds, each a hold of the lock, `perf stat` on
CPU 2; 1,762 runs, none failed). Lua's five scripts run −0.44% instructions and
−0.36% cycles, an interval of −0.79% to +0.08%; `arithmetic`, whose loops run
`//` and `%`, takes −1.51% cycles (−1.94% to −1.08%) and is faster at 30 of the
33 orders. speedtest1 runs −0.53% instructions in the same time: +0.09% cycles,
−0.66% to +0.83%. Statically, speedtest1's edge moves go from 35,590 to 36,035,
where values move out of the registers their webs avoid; Lua's don't change.

### Ranges that wait in their slots (#2768)

A range a value enters a block with takes its place at the anchor's exit
(#2553). Where the value is in its slot there, the range took a register at the
block's start all the same, the lowest free one, and the edges into the block
reloaded the value into it. The blocks the value then reaches take that
register from the anchor's exit in turn, so the register picked at the start
decided where the value meets its other paths.

Lua's `OP_GETTABUP` calls `luaH_getshortstr` in one arm of `luaV_fastget` and
spills `pc` around it. At the join of the two arms, `rbx`, where the dispatch
keeps `pc`, still held `upval`, which the other arm's `luaV_finishget` reads, so
`pc`'s range took `r13`, the lowest free callee-saved register and the one the
dispatch takes `i` in. The fetch then loaded `i` into `rbx`, added 4 to `pc` in
`r13`, and swapped the two through `r11` before the indirect jump: 0.74 M times
on Lua's five scripts, and 1.48 M of the moves through `r11` left after #2592.

- **Waiting.** A range hinted to its own value, which a block's entry ranges and
  a split's pieces are and a phi's definition isn't, whose value was in its slot
  where its hint says, waits there until its next required use, as an unused
  one rests. A use that isn't required reads the slot.
- **The first register.** The scan with required uses notes each value's first
  register. Where it is free at the range's start, the range takes it there;
  where it isn't, the range waits. A reload of a value whose hint names its slot
  takes that register where it is free, rather than the lowest free one.

`pc` now waits in its slot past the join, and the hot edge into the fetch
reloads it into `rbx`, which `upval` has left by then. The fetch adds 4 to `pc`
in `rbx` and swaps nothing through `r11`. `cl`, which the dispatch keeps in its
slot, now keeps `r13` through the handler, so `i` takes `trap`'s `rbp`, which
the scan evicts as the value with the fewest uses, and moves to `r13` at the
jump, where `trap` is reloaded. The handler's path through the call runs 29
instructions from the call's return to the indirect jump instead of 33.

Taking a free register at the start where the first one is busy is what keeps
a value spilled before a loop in a register around it: waiting there too, at a
loop's header, reloaded `nCol` on every iteration of `sqlite3ColumnIndex`'s
outer loop (+5.82 M). A phi's range is no continuation of its value: one that
waited needed a slot of its own, and where slot sharing can't join it to its
operands' (`sqlite3VdbeExec`'s opcode loop, whose join has 95 of them), every
edge copied slot to slot through `r11` (+11.7 M). By callgrind (#1189's
`gap.c`, #1217's inputs, Lua's hash seed fixed) at main `5f3c7d48`:

| Variant | speedtest1 | Lua's five scripts |
| --- | ---: | ---: |
| main | 2,657.78 M | 4,879.4 M |
| the first register for reloads alone | 2,656.99 M | 4,879.5 M |
| waiting alone, every range | 2,661.32 M | 4,852.6 M |
| both, every range | 2,657.79 M | 4,851.6 M |
| both, but where the first register is free | 2,652.65 M | 4,851.5 M |
| the same, for a range of its own value only (this) | 2,633.68 M (−0.91%) | 4,851.7 M (−0.57%) |
| this without the free first register | 2,638.79 M | 4,851.9 M |

- **By function.** `luaV_execute` runs 2,287.4 M instructions instead of
  2,311.8 M, with 339.6 M register moves instead of 344.9 M and 16.4 M through
  `r11` instead of 17.7 M; `rehash` runs −2.37 M, `str_gsub` −0.46 M and
  `str_format` −0.36 M, and no Lua function more. speedtest1's
  `balance_nonroot` runs −8.23 M, `sqlite3VdbeExec` −6.08 M and
  `sqlite3_str_vappendf` −1.22 M; its losses are `sqlite3WhereEnd` +0.42 M and
  `speedtest1_run` +0.14 M, where the reloads and the values they displace
  moved.
- **Statically** (`--tier1-report`), speedtest1's unit has 397,490 records
  instead of 403,045, 30,715 edge moves instead of 39,235, and 11,225 spill
  stores and 11,466 spill loads instead of 12,645 and 13,915; its call saves
  rise from 19,436 stores and 24,766 loads to 19,545 and 24,902. `lvm.c` has
  9,840 records instead of 9,994 and 992 edge moves instead of 1,196.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/e1a21c966bf6728ef5f50326d60dd7419c932cc1/docs/measurements/tier1-waiting-ranges-2768-2026-11-14),
#2592's method), main `5f3c7d48` against the change: speedtest1 runs 0.39% fewer
user cycles over 8 layouts (95% interval −0.77% to −0.01%, slower at 2 of
them) for 0.93% fewer instructions, and Lua's five scripts −0.04% (−0.52% to
+0.44%) over 33 link orders for 0.56% fewer instructions at each: Zen 5
eliminates register moves at rename, and `arithmetic` and `closures` move with
the link order by up to 10% either way. `sqlite3.c`'s `--tier1 -c` takes 0.21%
more cycles for 0.29% more instructions, and `lvm.c`'s the same cycles: the
scan's checks and its reloads at uses cost about what the fewer records and
edge moves save later.

With #2766 ("Recipes no block reads, settled phi places, and the fixed rules'
scratch" above), which merged first, by the same callgrind method ([the
record's section 4](https://github.com/buster14a/buster-performance/tree/296a5e536c79297214c215dbb9402d88b86094fc/docs/measurements/tier1-waiting-ranges-2768-2026-11-14)),
its head `c3c9fb69` against it merged with this change: Lua's five scripts run
4,756.0 M instructions instead of 4,778.2 M (−0.46%), `luaV_execute` 2,206.3 M
instead of 2,225.5 M with 3.6 M moves through `r11` instead of 5.0 M (#2767's
divisor in `rax` is 3.49 M of them), and speedtest1 2,603.53 M instead of
2,626.65 M (−0.88%). No Lua function runs more, and speedtest1's only loss over
0.1 M is `sqlite3WhereEnd` (+0.42 M).

Every Lua build printed clang -O0's output and every speedtest1 gave its
verification hash. `tier1_allocate_hints_test.c`'s traces check a range that
waits and reloads into its value's first register, one that takes that
register at its start where it is free, a range hinted to another value and
one required at its start, which take a free register there; a build without
each part of the rule fails its trace. `tests/driver/tier1_patterns.c`'s
`patterns_spilled` is a handler shaped like `OP_GETTABUP`: on main its join
reloads two of the loop's values into registers others hold, and its indirect
jump follows their reloads, where now every handler's follows its table's load.

### A handler a goto enters (#2782, #2592)

#2766, #2767 and #2768 left `luaV_execute` one site that moved values through
`r11`: the generic `for` loop. `OP_TFORCALL` is entered by the dispatch and by
`OP_TFORPREP`'s `pc += Bx; i = *(pc++); goto l_tforcall`. `OP_TFORLOOP`
(`l_tforloop`, after `OP_TFORCALL`'s own `i = *(pc++)`) then jumps back to the
dispatch.

- **The phis' registers.** `OP_TFORCALL`'s block has phis, for `pc` and `i`.
  Their hint was their operand's register at the exit of the chosen
  predecessor (#2553), the predecessor laid out before the block:
  `OP_TFORPREP`'s, not the dispatch's, which is laid out after. The handler
  then ran in `OP_TFORPREP`'s registers. `OP_TFORLOOP`'s jump rotated `pc`, `i`
  and `trap` back through `r11` on every iteration (with #2768's waiting
  ranges), and `OP_TFORPREP`'s edge rotated them once per loop.
- **The fetches.** #2592's operand sink didn't move either fetch's `pc + 1`
  past its load, because neither `pc` was of the target phi's family:
  - `l_tforloop`'s `pc` joins `OP_TFORCALL`'s `pc + 1` with the dispatch's
    `pc`;
  - `OP_TFORCALL`'s own `pc` joins the dispatch's `pc` with `OP_TFORPREP`'s
    jump;
  - the dispatch's join has more incoming values than the 32 the rule
    reads.

Two changes:

- **The dispatch's convention for the phis.** In `tier1_cfg_phi_exits`, a block
  that an earlier block and a later multiway branch (three or more successors)
  both enter gives each phi the register its operand along the branch has at
  the branch's first successor's entry. That is the register every handler
  takes it in (the `siblings` convention), so the dispatch's edge moves nothing
  and the `goto`'s edge moves once per loop. The block's other values keep
  their hints: giving them the convention too spilled `trap` around
  `OP_TFORCALL`'s call, three stack stores and three loads per iteration.
- **The sink's family** (`tier1_sink_ir_family`). `a` is now also of the
  target phi's family in three more cases:
  - it comes from a join of joins of the phi's values (`OP_TFORLOOP`'s
    `vmfetch`);
  - the phi joins `a`'s origin along another edge than the root's (`l_tforloop`
    and `OP_TFORCALL`'s `pc`);
  - the phi and `a`'s phi share a source that joins at least three values, a
    dispatch's join (`OP_TFORCALL`'s fetch).

  Two loops' phis sharing an initial value share no register, so a shared
  source must be such a join. With any phi as the source, the rule moved code
  for no gain.

`OP_TFORCALL`'s and `OP_TFORLOOP`'s fetches now add in place. `luaV_execute`'s
jump after `OP_TFORLOOP` moves nothing.

**By callgrind** (#1189's `gap.c`, #1217's inputs, Lua's hash seed fixed):

| | main `6415e30` | this | main with #2773 | that and this |
| --- | ---: | ---: | ---: | ---: |
| `luaV_execute`'s instructions | 2,214.90 M | 2,214.44 M | 2,195.67 M | 2,195.27 M |
| its moves through `r11` | 1.48 M | 1.48 M | 132,002 | 4 |
| speedtest1 | 2,610.83 M | 2,610.12 M | 2,590.34 M | 2,589.57 M |

- **On main.** `OP_TFORCALL` ran no rotation without #2773; its 1.48 M are
  `OP_GETTABUP`'s, which #2773 removes. The change saves `luaV_execute` 0.46 M
  register moves there.
- **Lua's totals** move by `luaH_getshortstr`'s count, whose code is the same in
  every build: −5.40 M on main and +1.71 M with #2773. Its hash chains follow
  the addresses Lua hashes pointer keys by, which move with the binary, so the
  totals include that effect.
- **By function.** Without it, no Lua function runs more instructions.
  speedtest1 runs 0.71 M and 0.76 M fewer: `resolveP2Values` −0.43 M,
  `cellSizePtrTableLeaf` −0.14 M, `sqlite3_str_vappendf` −0.10 M to −0.15 M.
- **The 4 left** are one swap's two moves, run twice: `OP_CLOSE` puts
  `luaF_close`'s constant arguments 0 and 1 in each other's registers. Such
  swaps just before a call are #2823; across Lua they are 5.80 M moves through
  `r11` at 283 sites, almost all outside `luaV_execute`.

**On the 9700X** (#2553's method; main against this, without #2773, whose 132,002
moves Zen 5 eliminates at rename, too few to time): Lua's five scripts over 33
link orders take −0.09% cycles (−0.32% to +0.15%), speedtest1 over 8 layouts
−0.17% (−0.52% to +0.19%). Both are flat, as 0.46 M moves and 0.71 M
instructions are. `--tier1 --lanes 1 -c` of `sqlite3.c` takes +0.02% cycles and
+0.07% instructions, `lvm.c` −0.08% cycles. The record is buster-performance's
`docs/measurements/tier1-dispatch-entry-2782-2026-11-15`.

**Tests.**

- **`test-tier1-cfg`'s dispatch-entry fixture.** A dispatch branches to three
  handlers. One computes a value from `pc` while `pc` lives on, then jumps to
  another, whose phi joins it with `pc`. The phi must start in `pc`'s register
  at the first handler's entry, on both paths.
- **`test-ir-optimize`'s sink fixtures:**
  - `rejoin`, `nested` and `shared` move one expression each;
  - `loop` (the same shape around a loop's two-value header) moves none.
- **Mutations.** Each rule turned off fails its fixture.

### Victims that continue in their slots (#2774)

With #2768's waiting ranges, Lua's fetch after `OP_GETTABUP`'s call held all six
callee-saved registers when `i = *pc` was defined: `pc` in `rbx`, `trap`'s phi
in `rbp`, `ci`, `cl` and `k` in `r12` to `r14`, and `base`'s phi in `r15`. No
candidate had a use left before its range's end, so the scan with required uses
took the one with the fewest uses over all its intervals, `trap`'s phi, which
has none. The edge into the dispatch then stored `cl`, whose place there is its
slot, moved `i` into `cl`'s `r13`, and reloaded `trap`: three instructions more
than evicting `cl`, 0.74 M times on Lua's five scripts. Ten other fetches did
the same, 5.28 M times in all.

Two parts:

- **An interval evicted where it starts opens in its slot.** It never held the
  register it took there, so its opening, which the ranges that name it follow,
  is its slot too (`tier1_allocation_evict`). The first handler's entry range
  for `cl` took `r13` and lost it at once to `i`; the dispatch's join, whose
  ranges take that handler's openings (#1216), took `r13` for `cl` in turn, for
  `i` to evict it again, and every edge into it stored `cl` from `r13`.
- **A victim that continues in its slot goes first.** `tier1_cfg.c` names, for
  each range live out of its block, the range whose opening is the value's
  place at the entry of the block's first successor in layout
  (`Tier1AllocationInput.continuation_intervals`): the successor's entry range
  where it comes no later than the block, else the range that entry takes its
  opening from, as the dispatch's join, laid out after the handlers, takes the
  first handler's. Each block holds that block, or none, so a range checks one
  word. A range that goes on through merged blocks keeps what the last of its
  blocks that gives one gives: the edges out of the blocks before, a loop's
  back edge among them, store it there too. A victim with no use left whose
  continuation opened in its slot only moves the store its edge makes, so the
  scan evicts it before any other, a recipe included.

The fetch now loads `i` into `r13` and adds 4 to `pc` in `rbx`, and the edge
moves nothing; no fetch stores, moves and reloads `trap` any more.
`luaV_execute` has 7,481 instructions instead of 7,599. The one
path that runs more moves is the first fetch after `returning`, where `cl`,
`k` and `base` arrive in each other's registers: it stored `trap`, `cl` and
reloaded `trap` around four moves (7 instructions); it now stores `cl` and
rotates four registers through `r11` (6), 6.37 M times.

By callgrind (#1189's `gap.c`, #1217's inputs, Lua's hash seed fixed), main
`ee74e8e4` with #2773 merged against each variant (buster-performance's record
has the switches that reproduce them):

| Variant | speedtest1 | Lua's five scripts |
| --- | ---: | ---: |
| the base | 2,603.58 M | 4,756.0 M |
| a stored victim as the farthest next use alone | 2,600.30 M | 4,756.9 M |
| openings of intervals evicted at their start alone | 2,588.28 M | 4,701.2 M |
| both | 2,583.93 M | 4,686.2 M |
| both, a stored victim before a recipe | 2,581.23 M | 4,685.8 M |
| the same, only where the block has one successor | 2,583.84 M | 4,685.8 M |
| the same, only for a range that held its register since its start | 2,584.07 M | 4,686.5 M |
| **this** | **2,581.25 M (−0.86%)** | **4,685.8 M (−1.48%)** |
| this, with a range's continuation from its last block alone | 2,587.25 M | 4,684.9 M |

- **The continuation alone** finds little: the dispatch's join took the first
  handler's register for `cl`, so its continuation was a register.
- **A range's last block alone**, where it goes on through merged blocks and
  that one gives no continuation, gave `sqlite3VdbeExec` 5.3 M more and
  `str_gsub` 0.9 M less. Requiring one successor, or an unbroken register (no
  reload since the range's start, after which an edge leaves its store out),
  cost speedtest1 2.6 M and 2.8 M.
- **By function.** `luaV_execute` runs 2,135.5 M instructions instead of
  2,206.3 M, with 303.8 M register moves instead of 317.0 M (23.4 M through
  `r11` instead of 3.6 M, the rotation above); `str_gsub` +0.58 M, and no other
  Lua function changes by 0.05 M. speedtest1's `sqlite3VdbeExec` runs −11.88 M,
  `sqlite3RunParser` −5.02 M, `sqlite3ColumnIndex` −2.16 M and
  `sqlite3FindTable` −0.90 M; its losses are `lookupName` +0.57 M,
  `sqlite3Update` +0.12 M and `instrFunc` +0.11 M.
- **Statically** (`--tier1-report`), speedtest1's unit has 390,798 records
  instead of 393,120, 25,287 edge moves instead of 27,296, 10,526 spill stores
  and 10,630 spill loads instead of 11,207 and 11,478, and 19,473 and 24,812
  call saves' stores and loads instead of 19,545 and 24,902. `lvm.c` has 9,375
  records instead of 9,493, 610 edge moves instead of 715, and 214 and 194
  spill stores and loads instead of 261 and 236; the compiler's own `main.c`
  375,737 records instead of 376,561 and 26,971 edge moves instead of 27,878.
- **The compile.** By callgrind in the container, `lvm.c`'s `--tier1 -c` runs
  0.15 M more instructions (948.41 M to 948.56 M): the producer +0.21 M and the
  allocator +0.21 M, less what the fewer records and moves save. Checking every
  live-out range's successor had cost the producer +1.08 M; most blocks give
  none.

At main `ae3778a2`, which merged #2773 after these variants, the change does
the same: Lua's five scripts run 4,662.4 M instructions instead of 4,732.8 M
(−1.49%) and speedtest1 2,563.32 M instead of 2,590.34 M (−1.04%).

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/dc331fbfbf9fded8e3d691d6f17243d31d0858a9/docs/measurements/tier1-continuations-2774-2026-11-14),
#2592's method), the variants' base against the change: speedtest1 runs 0.74%
fewer user cycles over 8 layouts (95% interval −1.28% to −0.20%, slower at
none) for 0.88% fewer instructions. Lua's five scripts run +0.05% (−0.24% to
+0.34%) over 33 link orders for 1.49% fewer instructions: `calls` −0.93% and
`closures` −1.26%, `strings` +1.08% and `arithmetic` +1.98%. `arithmetic`'s
cycles move with the link order from −1.4% to +8.1%, and `strings`, whose
instructions don't change, leaves more dispatch slots empty at the front end;
Zen 5 eliminates the fewer register moves at rename. Against main `ae3778a2`,
`sqlite3.c`'s `--tier1 -c` takes 0.19% more cycles for 0.27% more
instructions, and `lvm.c`'s 0.23% fewer cycles.

`tier1_allocate_hints_test.c`'s traces check a definition that evicts a value
continuing in its slot though another is lighter, and one that doesn't without
a continuation, with one that opened in a register, or with a use left; and a
range that follows an interval evicted at its start, which waits in its slot,
and one evicted after its start, which takes a free register at its own. A
build without each condition fails its trace. Its random inputs take
continuations with required uses, on both paths and the reference scan, against
the verifier and the occupancy oracle. `test-tier1-cfg` checks every
allocation's continuations against a twin that recomputes them from the
intervals alone.

### Loop bodies before their headers, and slots already current (#2784)

With #2768's waiting, Lua's `str_gsub` ran 73.0 M instructions on the five
scripts instead of 69.9 M. Its loop swapped `r12` and `r13` through `r11` twice
on its path for a plain character, 0.34 M times each, and at its head reloaded
two values from their slots and stored them straight back, 0.57 M times each.
Turning only the waiting off restored the earlier code, but the waiting decided
none of it directly: it changed which register was free where the loop's body
chose its phis' places.

The loop's header follows its body in layout. Its phis for `src` and for the
value passed through with it take their operands' registers at the exit of the
latch before it, which take them from the join before the body, where the loop
may end: `src`'s from the matches' path, laid out before that join. The body's
first block has no anchor, since its predecessors all follow it, and no
sibling, being their first successor: its ranges of the two phis took their
values' last registers or the lowest free ones. Before #2768 a third value held
`r13` at that block's start and the body's places agreed with the header's by
chance. Waiting left `r13` free there, the two phis' ranges took each other's
header registers, and each iteration swapped them back.

- **Body hints.** A range laid out before its value's definition that nothing
  hints takes the definition's hint, followed back along the hint intervals,
  at most 8 of them as #2553's operand targets are, to the first range of
  another value that ends before it (an exit) or starts before it (an
  opening): the register the header's phi takes from its latch
  (`tier1_cfg_body_hints`). A chain back to the value's own earlier range is
  left out: it named the value's first opening, where the range's own last
  register serves as well, and following it cost `balance_nonroot` 4.70 M,
  `sqlite3_str_vappendf` 1.20 M and `lookupName` 0.52 M.
- **No waiting in another value's slot.** A range waits in its slot (#2768)
  only where its hint names a range of its own value: another value's range in
  memory says nothing of this one's slot.
- **Slots already current.** An eviction into a value's slot leaves out its
  store where the slot already holds the value: a transition stored it there
  or reloaded it from there earlier in the same block, or in the run of blocks
  before it, each the only predecessor of the next (`tier1_emit_phase`'s slot
  stamps, in the CFG consumer). An SSA value never changes, and slots are
  shared only by values never live at once. A range that waits and is evicted
  again after its reload otherwise stored the value back where it had just read
  it: `str_gsub`'s pattern and `max_s` at its loop's head.

By callgrind (#1189's `gap.c`, #1217's inputs, Lua's hash seed fixed) at main
`20cd8e54`:

| Variant | speedtest1 | Lua's five scripts |
| --- | ---: | ---: |
| main | 2,590.34 M | 4,732.8 M |
| body hints, back to any earlier range, the waiting unchanged | 2,583.23 M | 4,712.1 M |
| body hints, back to another value's range, waiting in its own value's slot only | 2,578.59 M | 4,712.1 M |
| slot stamps alone | 2,587.92 M | 4,731.7 M |
| both (this) | 2,576.17 M (−0.55%) | 4,711.0 M (−0.46%) |

- **By function.** `str_gsub` runs 69.4 M, with no move through `r11` and no
  store back on its loop's paths, and `luaD_precall` 230.8 M instead of
  249.1 M; `luaV_execute` runs as before (2,195.7 M). speedtest1's
  `sqlite3VdbeExec` runs −13.00 M and `balance_nonroot` −0.52 M; its one loss
  over 0.05 M is `wherePathSolver` (+0.07 M).
- **Statically** (`--tier1-report`), speedtest1's unit has 388,992 records
  instead of 389,379, 26,618 edge moves instead of 26,717, and 10,932 spill
  stores instead of 11,179 (11,756 loads instead of 11,712). `lvm.c` compiles
  as before; `lstrlib.c` has 472 edge moves instead of 485.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/adeb8b4a5/docs/measurements/tier1-body-hints-2784-2026-11-15),
#2768's method), main `b5099aa7` against the change: Lua's five scripts run
0.37% fewer user cycles over 33 link orders (95% interval −0.64% to −0.10%,
slower at 11) for 0.46% fewer instructions at each. speedtest1 runs 0.41% more
(−0.03% to +0.85%, slower at 7 of 8 shifts) for 0.56% fewer instructions. The
slot stamps alone cost it 0.66% (+0.32% to +0.99%) for 0.10% fewer
instructions, and the body hints alone 0.45% (−0.17% to +1.07%) for 0.47%
fewer. Sampled by function, the stamps' cycles move by up to 15% either way in
functions whose code they leave as it is (`pcache1FetchStage2`,
`releaseMemArray`, `memsys5Size`): the shifts move the whole unit but keep the
distances between its functions, which a change of size moves for every
function after it, and the run can't separate that layout from the change's
own effect. `sqlite3.c`'s `--tier1 -c` takes 0.18% more cycles, `lvm.c`'s
0.08% fewer.

A loop whose chain ends in a range with no hint of its own keeps its body's
places to chance: `tests/driver/tier1_patterns.c`'s `patterns_gsub`, whose
latch, holding its join's ranges, is laid out between the body and the header,
still swaps `n` and `lastmatch` on its path for a character. Hinting a phi's
range to an operand's exit before it fixes that and gains 7.7 M on Lua, but
loses 13.3 M in `sqlite3VdbeExec`: #2834.

`tier1_cfg_test.c`'s fixture is `str_gsub`'s loop in small: its character path
moves nothing, where without the body hints the edge into the body moved the
header's phi. `tier1_allocate_hints_test.c`'s fifth slot trace is a range of
its own value hinted to another value's range in that value's slot, which now
takes a free register at its start. `tests/driver/tier1_patterns.c`'s
`patterns_gsub` stores nothing back where the instruction before loaded it, as
main's does once.


### Call arguments in their registers (#2646)

A value whose last use is a call's register argument takes, where nothing else
names a register for it, the register the call reads it in. Before, it took the
lowest free register and the call's parallel move put it in place:
`mov $0x81,%eax ... mov %rax,%rdx` for a constant third argument, `lea
str(%rip),%rax; ...; mov %rax,%rdi` for a string's address, a load into `ecx`
moved to `esi`. Tier 1's objects of `sqlite3.c` held 27,479 register moves into
an argument register within a few instructions of a call, 82,048 bytes, where
clang -O0 loads each argument into its register; they were most of the 40,142
register-to-register moves against clang's 6,813, and the reason `sqlite3.c`
and c-testsuite were above #2646's bound of clang -O0's bytes.

- **Where it comes from.** The CFG consumer gives each use the register it reads
  its value in (`Tier1CfgInput.use_registers`): a register argument's, by its
  place (`tier1_select_parameter_registers`, or xmm n for a floating one), an
  indirect callee's `r10` or `rax`, `UINT8_MAX` for any other use
  (`tier1_emit_cfg_problem`). `tier1_cfg_prepare` gives an interval the register
  of its last use where that use ends it (`Tier1AllocationInput.register_hints`):
  a value that lives past the call doesn't take it, as the call would clobber it
  there; one that lives across an earlier call takes callee-saved registers
  alone in the preferred problem, where the hint isn't allowed and is ignored.
  The straight-line consumer (`tier1_emit_prepare`), whose allocation has an
  interval a value, gives the same register to each interval that ends at a
  call reading it in a register, the first argument's for a value passed
  twice; it then allocates with hints where before only a tie made it.
- **Its rank.** `tier1_allocation_value_hint_registered` returns it only where no value
  hint names a register (a named interval's opening or closing, the value's own
  place, its hint value's): a range that enters a block keeps its value's
  register, a phi its operand's, a tie its operand's, and the edges and ties
  move nothing for it. Both the coloring path and the scan read it there, at a
  start and at a reload.
- **It evicts nothing.** A value hint takes its register from a recipe no use
  requires there (#2618); a register hint alone doesn't (the hint functions' `registered`):
  a call's argument would push the loop's addresses and constants out of the
  argument registers they rest in. Where the register is taken, the value takes
  another, as before.

What stays: a function whose parameters live across its calls in their
incoming registers, saved around each call, has no argument register free for
its calls' other arguments (`sqlite3DeleteFrom`'s five, saved and reloaded at
every call); an argument whose register an incoming parameter still holds takes
another and moves (#2823's `v5`, now one move where it took a swap's three); a
two-address result tied to its operand's register goes where the operand was;
and an indirect callee's `r10` isn't among the registers its range may take.
21,323 moves into argument registers remain in `sqlite3.c`'s object.

| Objects' Tier 1 function bytes (`--tier1-no-inline`), against clang -O0 | main `d876a5e0` | this | clang -O0 |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 904,862 | 884,975 | 913,339 |
| Lua's 34 sources | 205,026 | 196,906 | 248,990 |
| c-testsuite's 220 programs | 48,365 | 45,227 | 47,293 |
| `sqlite3.c`'s register-to-register moves | 40,142 | 33,335 | 6,813 |

(The x86-64 host path's objects and the AVX-512 host path's are the same bytes.)

**Tests.** `tier1_cfg_test.c`'s use-registers fixture allocates, by both paths
and on both the coloring path and the scan, a call's argument in the register
it is read in where the lowest free one is another; a second argument whose
register a recipe holds, which it doesn't take; a value read by the call that
lives on, which takes no hint; and a range that enters a block in its value's
register though a call there reads it in another. `tier1_call_test.c` checks
that the CFG consumer names each register argument's register at its call, and
that the straight-line consumer hints it to each argument's interval that ends
there, both of them over the call fixtures. A mutation of each rule fails its
check.

## Rematerialization

`Tier1RematerializationInput` is a borrowed column view of selection facts,
not an alternate value representation. The scalar and vector mark producers
accept only:

- Integer constants of width 4 or 8, with width-4 payloads normalized to u32.
- Symbol addresses explicitly certified stable for the function: computed from
  rip, or with `BUSTER_TIER1_REMAT_GOT` read from the symbol's GOT slot, which a
  consumer certifies where it reaches the symbol so (the loader writes the slot
  before the program runs, and nothing changes it after). TLS recipes and other
  loads are not treated as addresses.
- Final signed-i32 frame displacements from a frame base explicitly certified
  stable at every copy position. A changing RSP is not stable merely because
  the original operation names a stack slot.

An operation-table lookup classifies 64 candidate bytes at a time. Sparse
payload checks confirm width, stability, displacement range and optional
comparable measured costs. Unknown costs prefer recomputation. Known
rematerialization costs greater than the reload cost suppress the mark.
`tier1_rematerialization_model_costs` fills those columns from the Zen 5 model
(`x86_zen5.c`, #181), classifying 64 operations at a time as the marks do, its
scalar twin a plain loop: each copy's GPR latency as `tier1_rematerialize`
emits it, against the model's L1-hit load (`mov r64, [base]`), the cheapest any
reload of a slot can be, so that a copy no costlier than it is no costlier than
the reload it saves. With #181's rows the reload is 4 cycles (an L1 load's
address chain), a `lea` from RIP or a frame base 1, a GOT slot's load 4, and a
constant's move and a zeroing `xor` have no latency (they read nothing: the
column is absent, so unknown); no copy costs more than its reload, and every
candidate is still recomputed. Both consumers fill the columns, which follow
the model's rows.

`tier1_rematerialize` emits one baseline x86 record. Zero uses `xor r32,r32`
only when the caller reports no live flags; otherwise it uses `mov r32,0`.
Other constants use a u32 move, sign-extended imm32 move, or imm64 move as
needed. Stack addresses use LEA from the final stable base. Each symbolic copy
(a LEA from rip, or a GOT slot's load) has its own encoder fixup mark plus
returned symbol and addend; the consumer must publish a PC32 fixup for a LEA,
and a LOAD_GOT one for a GOT slot's load (`Tier1Rematerialized.got`), for every
copy through `link_add_code_fixups`. This module does not silently drop fixups
or reuse the original copy's offset.

### In Tier 1's consumers (#178)

Both consumers emit copies through `tier1_emit_copy`, which publishes a
symbol's reference with its record. No flag is live where a copy goes, between
two nodes' records or on an edge (`BUSTER_TIER1_EMIT_BOUNDARY_FLAGS`):
selection keeps each flag's producer and its readers in one node's records (a
compare and its setcc, conditional jump or cmov), as its constants' immediate
requests already assume, so a recomputed zero is a zeroing xor. Before #178
both consumers took every flag as live, and moved zeros in. The straight-line
consumer recomputes constants alone.

The CFG consumer (`tier1_emit_cfg_recipes`), which compiles the C driver's
`--tier1` functions, recomputes every value whose definition no store can
change:

- a constant;
- a symbol's address, from rip, or from its GOT slot where the emission's
  access column reaches it so;
- a frame object's address, a lea from the frame's base at the offset the
  frame's layout gives the object, which `tier1_emit_cfg_frame` fills in after
  the layout: the base is rsp where nothing moves rsp between the prologue and
  the epilogues, and rbp (or a realigned frame's r15) where the dynamic stack
  moves it (#141: an `alloca`'s `sub rsp`, a call's outgoing area taken from rsp
  around it), so the address never depends on where rsp is;
- a copy of one of them: the C driver's typed streams carry pointers as 64-bit
  integers, so every address the front end keeps in a variable reaches its uses
  through a `ptrtoint` and an `inttoptr`, the selector's `TIER1_RULE_COPY`, with
  its operand's bits (chains of up to `BUSTER_TIER1_EMIT_CFG_RECIPE_DEPTH`);
- a symbol's or a frame object's address plus a constant (a member's, a
  constant index's: `TIER1_RULE_ADDRESS` without an index), one lea at the sum,
  a symbol's the addend of its reference. A GOT slot's address takes none.

A symbol's copy is one more reference, whose room the records count from the
allocation's fragments and the edges' moves before they are emitted. A
recomputed value's definition whose first register fragment no use reads
before a recomputation takes over, or that only definitions left out read, is
left out (`tier1_emit_cfg_dead_definition`, decided from the last node back):
the C front end's address chains define each link right before the allocator
evicts it, a write nothing read. A fragment that reaches the block's end is one
too where no edge leaving the block reads its register (#2618): every
successor the value is live into enters with its recipe, and no phi there takes
the value along the edge. Otherwise an edge reads it: a move from the
register, or none where the successor keeps the value (or the phi) in it.

A recomputed value's definition needs no register at all (#2618): its own use
isn't required, so where values read sooner hold every register the allocator
gives it none, the cheapest choice, and the recipe's fragment starts at the
definition, which is left out too. While that use was required, the
assignment found no register for it and failed, and the second, with every
definition freed, spilled a value around it (`tier1_dropped_test.c`'s `g`). In
a `sqlite3VdbeExec` handler run 288,561 times on speedtest1, two frame
objects' addresses no longer take registers, and a value in `r14` no longer
spills and reloads around them: four instructions fewer a run. By callgrind at main
`b7a95eed` (#1189's `gap.c`, #1217's inputs), speedtest1 runs 1.50 M fewer
instructions: `sqlite3VdbeExec` −0.71 M (0.29 M fewer `lea`s, 0.39 M fewer
moves), `sqlite3Atoi64` −0.23 M, `sqlite3_str_vappendf` −0.21 M,
`pageFreeArray` −0.13 M; `instrFunc` +0.17 M, its loop's spills moved from one
value to another. Lua's scripts run the same instructions. On the 9700X, with
#2618's split of hoisted addresses (`docs/ir.md`), speedtest1's user cycles
are 0.9976 over six layouts (95% interval 0.9895 to 1.0058). The emitter accepts
a definition whose first fragment is its recipe's for a recomputed value alone,
and records no register for it.

The same holds where control enters a block (`tier1_cfg_entry_unread`, #1217):
a recomputed value whose register fragment at the block's start serves no use
before its next fragment, the recipe's, takes over, before the block's last
position, enters as its recipe. Its edges then move nothing for it, where they
recomputed it into a register the block overwrote before any read: on the
edges into Lua's `luaV_execute` dispatch tails and `sqlite3VdbeExec`'s, mostly
a frame object's address (the measurements are #1217's record, linked from
[the Tier 1 guide](commands/tier1.md)). Only a block's start
counts: a split's piece starts inside its block, and the verifier checks that
each piece holds its value whether or not a use reads it. Neither a use nor an
edge leaving the block can read the register, since the fragment ends inside
it; the verifier, which simulates every block from these entry locations, and
the certificate take the same entry location.

Recomputing a value live across a call instead of saving its register around
it wouldn't pay: in speedtest1's scalar-path build every one of the 9,887
values the CFG consumer saved around calls had no recipe; those with one live
across a call are recomputed after it or in callee-saved registers there.

`--tier1-no-rematerialize` compiles without any of it; `--tier1-report` counts
the recomputations, the definitions left out, the spill slots and the frames'
bytes (`Tier1Frame.maximum_stack_bytes`, what a frame takes below the return
address).

### Measured (#178)

On the 9700X, the vector path's `--tier1 -c` of each corpus, its units'
reports summed, at `4851f99d` with and without rematerialization and at main
`a28f29f`, which recomputed constants alone (and reports neither slots nor
frames); speedtest1 is the program (`speedtest1-unity.c`), as the run times'
build compiles it ([the record](https://github.com/buster14a/buster-performance/tree/a17ba43dcf56dbbd24faddf752eab77efdb5c997/docs/measurements/tier1-rematerialization-178-2026-10-31)):

| Corpus | Build | Stores | Reloads | Slots | Frame bytes | Recomputed | Left out |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| c-testsuite's subset (350 functions by Tier 1) | none | 22 | 22 | 12 | 16,812 | 0 | 0 |
| | main | 22 | 22 | | | 0 | |
| | #178 | 0 | 0 | 0 | 16,716 | 22 | 11 |
| `sqlite3.c` (2,153) | none | 3,583 | 4,246 | 1,318 | 185,290 | 0 | 0 |
| | main | 3,410 | 4,042 | | | 203 | |
| | #178 | 2,367 | 2,681 | 918 | 182,706 | 1,564 | 140 |
| musl (2,023) | none | 2,127 | 2,590 | 784 | 235,720 | 0 | 0 |
| | main | 2,021 | 2,451 | | | 136 | |
| | #178 | 1,375 | 1,567 | 527 | 233,992 | 1,020 | 85 |
| speedtest1 (2,210) | none | 3,620 | 4,245 | 1,324 | 192,218 | 0 | 0 |
| | main | 3,432 | 4,028 | | | 216 | |
| | #178 | 2,377 | 2,674 | 921 | 189,618 | 1,570 | 143 |

Against none, rematerialization takes a third of the stores, 37% to 40% of
the reloads and 30% to 33% of the spill slots on the large corpora, and every
spill on c-testsuite's; the frames shrink by 0.6% to 1.4% only, their bytes
mostly the call-save rows, frame objects and alignment. The scalar path's
counts are within 1% of these (its allocator's choices differ), and the same
on the 9700X as locally.

speedtest1 (its usual `--size 5` mix, pinned to CPU 2, 11 rounds in an order
that rotates by round, each run one hold of the lock, every build the same
`--verify` hash) runs alike with it and without:

| Build | Median | Range | Instructions | Cycles |
| --- | ---: | --- | ---: | ---: |
| Tier 0 | 256.4 ms | 255.6–256.9 | 5,694.2M | 1,331.8M |
| Tier 1, main | 228.1 ms | 227.7–229.7 | 4,589.3M | 1,169.1M |
| Tier 1, none | 227.8 ms | 227.2–229.1 | 4,588.3M | 1,167.7M |
| Tier 1, #178 | 228.4 ms | 227.3–229.6 | 4,581.0M | 1,168.9M |
| Tier 1, #178, scalar path | 227.9 ms | 227.5–228.8 | 4,594.6M | 1,164.6M |
| Tier 1, none, scalar path | 228.8 ms | 228.2–229.3 | 4,600.7M | 1,166.1M |

It retires 7.3 million fewer instructions (0.16%) in the same cycles: the
spills it removes aren't on speedtest1's hot paths, whose hottest function,
`sqlite3VdbeExec`, is Tier 0's (its switch). Of c-testsuite's subset, one
program's code differs with it and without (`00216`, the one with the spills),
and runs in 287 us either way.

#142's campaigns, 40 programs per generator (seeds 17800 to 17839), Tier 1's
build rematerializing: 61 ran at clang `-O0` and `-O2` and at both tiers and
printed the same, and 2 more at both tiers where clang's warnings passed the
harness's capture; the other 17 failed in clang's own steps (12 YARPGen
programs whose arrays overflow clang `-O0`'s link, 5 Csmith ones that ran past
the limit at every build). No case's builds disagreed.

At `ca6f9096`, after main's merge, main's #989 puts constants added to an
index into displacements and #991 makes a canonical narrow value's zero
extension a copy, so there is less to recompute and fewer spills without it.
Rematerialization still takes 25% of `sqlite3.c`'s stores (3,296 to 2,473),
28% of its reloads (3,902 to 2,804) and 22% of its slots (1,092 to 856); 29%,
34% and 28% of musl's (2,052 to 1,447, 2,456 to 1,614, 649 to 469); and every
spill of c-testsuite's. speedtest1 runs in 222.3 ms with it and 221.9 ms without
(5 rounds, the ranges overlapping), with the same hash.

## Memory and failure contract

Columns need no padding or alignment beyond their element types. Logical
counts authorize exactly those elements; all vector tails use masked loads
and stores. Inputs are immutable and disjoint from the destination arena's
unused capacity. The result and its live allocations may precede a later call
in the same arena. Scratch is returned on success; errors restore the entry
arena length and preserve the result object. Limits are 2^20 intervals/values
and 2^24 positions/uses; larger requests fail before reading the columns.
Rematerialization recipes have the same 2^20-value limit, checked by both
marking paths and the individual copy builder before reading any column.
The mark output has `count` writable bytes and must be disjoint from every
borrowed recipe column; the individual copy result must also be disjoint.
Empty intervals are invalid; an empty function is valid. Optional absent use
columns require zero use count. Capacity failure is distinct from invalid
input and impossible register constraints.

Both production paths allocate a temporary column of the class's end/start
events: eight bytes per input interval, plus alignment. Scalar production now
needs that column too (#1395); the independent scan reference does not. This
raises the scalar path's temporary capacity requirement without changing the
retained output layout. Fixed-range filtering can still need different scratch
on the two hosts. No new throughput measurement is claimed for this policy
change.

## Validation and measurements

`test-tier1-allocation [--seed N] [--count N]` checks arbitrary input order,
the three classes including vector register 31 and k1 to k7 (a mask allowed k0 refused, a call's range over all
eight taken), pressure boundaries, required
operands, fixed clobbers, hints, deterministic output, repeated reload/resplit,
compact spill identity, malformed/overflow inputs and arena rollback. One
oracle compares interval fragments pairwise; another expands them into a
per-position register occupancy map. A move oracle executes sequential output
against an independent snapshot of the parallel assignment. Guard-page tests
end every input column at an inaccessible page through 65 elements.
Fixed filtering also compares exact masks with an independent per-position
bitmap, compares complete allocations with explicitly prefiltered inputs, and
exercises indexed capacity rollback after an existing arena prefix. The 8,192
nonoverlapping fixed/value interval fixture checks every resulting register
without invoking the deliberately quadratic allocation verifier.

Generated cases and hinted ranges compare both production paths' complete
fragment, register, spill and pressure results. Both also pass the interval,
occupancy and allocation-certificate checks, as does the independent scan
reference. The position-walking level-rule oracles run on baseline hosts too.
The literal hint trace distinguishes the policies: without value hints both
production paths swap the two adjacent ranges' registers, while the independent
scan keeps them; with hints all three keep them. `test-checker-driver` adds a
parameter-free leaf with volatile loads to the #1395 whole-object comparison,
so unrestricted allocation is covered independently of loop phi ordering.

`tier1_allocate_expiry_test.c` adds literal traces, on both production entry
points, the scan reference and every class, which run even with `--count 0`:
- an evicted interval's old end while its successor still holds the register;
- a reload of the same value into another register before its original end,
  which must free that current register;
- simultaneous and duplicate required uses, and two distinct values that
  cannot share one register;
- a capacity layout.

Their expected fragments, logical fields and values are written out, not
replayed. A byte-by-byte capacity sweep of each entry point's own boundary
requires the ample output on success, and the prior arena prefix and result
object on failure. The first three traces each reject a mutant: a register
freed without `tier1_allocate_scan`'s ownership guard, a stale register freed at
the end, and eviction that ignores the other operands pinned at its position. In
`tier1_allocate_scan_uses` that guard cannot fail, since an eviction sets the
victim's register to `UINT8_MAX` and a reload records the new one.

The move-specific fixtures (`tier1_moves_test.c`, #608) exercise the chain,
reversed chain, independent moves, long and disjoint cycles, fan-out and
identities at 0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 64, 128, 256, 512 and
1024 moves, with sparse ids including UINT32_MAX. A 2^20-move chain checks the
full accepted limit. Snapshot execution checks meaning, the independent scan
checks exact ordering on small sets, and the scalar/vector indexed paths agree
exactly. Read-only guarded columns cover every tail through 33 elements; a
byte-by-byte capacity sweep checks rollback, prior arena contents and retained
output length. Scratch collisions, duplicate destinations, excessive counts
and absent columns fail transactionally.

`test-checker-driver`'s Tier 1 fixture (`c_check_tier1_driver_test.c`) keeps
every recipe's kind live across a loop under pressure: statics' addresses, a
static array's member's, extern variables' through their GOT slots, frame
objects' and a member's of one, and constants of each encoding, a zero among
them. It compiles the fixture by `--tier1` with and without
rematerialization by both paths. Each executable, and each `-c` object linked
by `cc`, must print clang's build's output; the report must have fewer spill
stores, reloads, slots and frame bytes with it, and count recomputations and
definitions left out only with it. In each object's code, decoded by
`x86_64_disassembler.c`, every zeroing xor must leave no flag live (a backward
fixpoint over `x86_semantics.c`'s effects, following the decoded branches),
every instruction that addresses from rip must have a relocation of its own
at its displacement (a PC32 for a lea, a GOT one for a load), and the loop
must recompute addresses with rematerialization and none without.

Original native straight-line reductions execute the assigned register/spill
plans through the existing encoder with rematerialization enabled and disabled,
using both allocation paths. Results must match an independent integer sum.
They exercise actual spill stores, memory reads, repeated register-required
reload/resplit and recomputed constants. Additional native fixtures cover the
constant encoding boundaries, live flags, stable stack addresses and a resolved
PC-relative symbol copy. Repeated reload/resplit also has a location-state simulation.
These bounded tests are not the #142 C-program differential corpus.

`benchmark-tier1-allocation [--only vector|scalar] [--after-scalar MS]` times
complete interval validation/preparation, assignment, output publication and
arena reset. It reports nanoseconds and available hardware cycles per interval,
sample ranges, fallback intervals, spill slots and peak arena bytes. Both plans
are verified before timing. Input generation and source/IR liveness are outside
this benchmark. Default samples alternate variant order; isolated and
scalar-conditioned modes expose different host power states. Run repeated
processes through the per-run 9700X lock, pinned to CPU 2, before retaining a
performance claim. No performance acceptance is asserted by adding the tool.

The [#609 fixed-filtering record](https://github.com/buster14a/buster-performance/tree/11051131ad5c4ae57c65d170fb38c6f5f3154796/docs/measurements/tier1-fixed-609-2026-10-27)
compares compiler code `9a652c02` with `92c025e7` under the shared 9700X lock.
At 8,192 sparse value/fixed intervals, filtering takes 6.954 ms versus 57.849 us
(120.21x), and complete allocation 7.131 ms versus 263.745 us (27.04x).
It covers dense/call/mixed constraints, no-fixed/tiny controls, scalar conditioning,
exact output checks and native/guard/rollback qualification. Indexed scratch adds
4*F bytes; all tiny samples, including slower ones, are retained in the record.
These are allocator API measurements; the current native producer supplies F=0.

#178's evidence is above ("Measured (#178)"): the corpora's spills and frames
with and without rematerialization, speedtest1's run times, and #142's
campaigns. #140's own remaining work is in its issue.
