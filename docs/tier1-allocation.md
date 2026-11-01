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
at the same position. The common path, `tier1_allocate`'s, applies to a class
whose ranges all have its full register mask and whose maximum pressure fits.
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
event sort.

The rule alone gives values that end and restart at one position each other's
registers: ends deposit downward in input order and starts take upward in input
order. A CFG's per-block ranges do exactly that at a block's start, so
`hint_values` names, per interval, a value whose most recently assigned register
the interval takes where it is free; `tier1_cfg.c` hints each range a value
enters a block with to the value itself, each phi to an operand, and a value
a phi takes along a back edge (defined after the phi) to the phi, which has its
register by then, so that the back edge moves nothing for it. The common
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
independent reference over positions, and a trace whose two values the rule
alone swaps.

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

These are `benchmark-tier1-allocation`'s cycles per interval: medians of five
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
the end position is the conservative next-use estimate. A value without a
register gets a spill/rematerialization fragment. An eviction closes its
register prefix. With `use_required`, it reenters a register at its next
required use, potentially evicting another value, and can be split again.
The output capacity is bounded by `2*intervals + 2*uses`; the simple path needs
at most `2*intervals`. Dense spill ids are assigned in input-interval order.

This is not yet the complete Wimmer/Franz implementation: it lacks inactive
interval lookahead across holes, loop-aware split placement, vector spill
region selection/recoloring, and automatic machine-constraint interval
construction. Input holes are independent ranges, and the scalar twin is a
validity oracle, not a proof of optimum constrained coloring.

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

Liveness is `dataflow.c`'s solve over the values used outside their block, phi
operands among them. A phi operand is a use on its edge: it joins its
predecessor's use row unless that block defines it, and the predecessor's
live-out row after the solve. Each value then gets an interval per block it is
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
`--simd`, a function the SSA promoter refuses for its vectors or masks (as
`test-ir-source --simd` requires it to) is allocated as lowered, all its locals
in slots, which is SSA too: #732's `vcmp` results reach the opmask class from
real C, and the run fails unless some do. On `tests/ir/vector-i32.c` with
`-march=x86-64-v4`, its three functions' 3 masks allocate on both paths and pass
the checker. The correctness list runs it there, and without `--simd` on
c-testsuite's subset, the frozen `sqlite3.c` and musl's units, whose every
function allocates and passes the checker on both paths:

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
the reload it saves. The model has measured no copy's latency yet (its
`mov r32, imm32` and zeroing `xor` cases have throughputs alone, and it has no
case for a lea from rip or at a displacement), so every candidate is
recomputed; both consumers fill the columns, which follow the model as #181
measures more.

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
- a frame object's address, a lea from rsp at the offset the frame's layout
  gives the object, which `tier1_emit_cfg_frame` fills in after the layout:
  selection refuses the dynamic stack, so nothing moves rsp between the
  prologue and the epilogues;
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
evicts it, a write nothing read.

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

`tier1_allocate_expiry_test.c` adds literal traces, on both entry points and
every class, which run even with `--count 0`:
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
