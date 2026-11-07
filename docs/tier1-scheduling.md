# Tier 1 scheduling

[#172](https://github.com/buster14a/buster/issues/172) asked whether scheduling
Tier 1's code pays on Zen 5, whose out-of-order window is large: the run time
it saves against the compile time it costs, before allocation and after it,
measured on speedtest1 and the corpora's programs. Both variants are now in
the C driver as `--tier1-schedule=post` and `--tier1-schedule=pre`, opt-in
measurement options, and [the 9700X's measurement](#the-measurement-on-the-9700x)
below is the answer.

@@DECISION@@

Scheduling is machine-dependent: the order that pays depends on the target's
latencies, ports, decoding and fusion, and on how much of a block its
out-of-order window already overlaps. Both variants here are Zen 5's. The
postallocation one weighs records by `x86_zen5.c`, the 9700X's own
observations, and the preallocation one by coarse classes that fit current x86
cores alike; neither reads `-mtune`, and every number below is the 9700X's. A
core with a smaller window, or an in-order one, would need its own model and
its own measurement before this conclusion applied to it: the tuning target is
an option of its own, as the generated code's instruction set is (#180).

## The driver's variants

`--tier1-schedule=none|post|pre` takes `--tier1`; `none` is the default.
`--tier1-report` counts what the variant did, `schedule_blocks` and
`schedule_moved`: the blocks (or segments) scheduled and the records (or IR
instructions) that changed places. `--verify` checks every scheduled block
against its scalar reference, which rebuilds the dependencies from the original
code: a refusal is the compile's internal failure, never a Tier 0 fallback.

- **post** (`c_check_tier1_schedule.c`): Tier 1's published code of each
  function, cut into blocks at the code's start and end, each branch record and
  the record after it, each branch's target, each label's first record, each
  frame event's boundary, and the body's start and the epilogue's (with or
  without `-g`, so that `-g` doesn't change the code). Each block's records are
  reordered by `tier1_schedule_block` before branch sizing and encoding; the
  symbolic references follow their records and are sorted by them again, as
  the encoder's fixup ends pair them in order. Nothing moves across a cut, so
  the branch columns, labels, frame events and source events keep their
  records, and every block keeps its size: a function's layout is the
  unscheduled one's but for the order inside its blocks. A block of more than
  256 records is scheduled in pieces of 256, nothing moving across a piece's
  end; that keeps more order than the block needs and loses none.
- **pre** (`tier1_schedule_ir.c`): each block's IR instructions after
  `ir_truth_simplify` and before the optimizer's final verification, which then
  covers the new order, so that selection, allocation and emission take it.

## Postallocation: contract

Initialize the encoder, semantics and Zen 5 tables before worker threads.
`Tier1ScheduleInput` supplies `instructions`, an optional byte column `pinned`,
and `count`. These are physical-register records after allocation, validated
for ordinary 64-bit encoding by the emitter. They are the entire block, or a
piece of it nothing moves across; labels or source anchors within it require
the corresponding caller pin. The API does not infer block boundaries,
calling-convention clobbers or volatile information.

`tier1_schedule_block(input, scratch, order, capacity, vector, statistics)`
returns `TIER1_SCHEDULE_OK`, `TIER1_SCHEDULE_INVALID_ARGUMENT` or
`TIER1_SCHEDULE_CAPACITY`. Its scratch is one caller-owned, reusable
`Tier1ScheduleGraph`; no allocations occur inside it. At the current bound of
256 records, the scratch is 35,072 bytes. The output is
`order[new_position] = old_position`, with one `uint16_t` per record. The caller
must permute every parallel record column with it, including references, then
calculate branch displacements and relocations.

All slices, scratch and optional statistics are disjoint. Only `count`
records/pin bytes and `capacity` output elements are accessible. Their natural C
alignment is sufficient; no readable tail padding is required. Empty input
permits null pointers. Invalid arguments, insufficient capacity and blocks
above 256 records are rejected before input access and preserve output and
statistics. The caller retains the original order on rejection.

The stage consumes a complete block at a time. Its eight-record dependency
comparison and 16-height reduction widths are kernel widths, not semantic
batch boundaries. All final loads are masked by actual records or successor
bits. Inactive bits are zero, including the padding in fixed-stride graph rows.
The result and optional graph/statistics belong to the caller and remain valid
until the scratch is reused; the input is immutable throughout.

## Postallocation: dependencies and priorities

The graph retains every original-order RAW, WAR and WAW dependency over the
semantics module's GPR, vector-register, flag and memory resources. Partial
register writes, zero idioms, implicit registers and addressing-register uses
come from those shared operand-resolved effects, rather than a scheduler copy
of instruction semantics. An unknown semantic form reads and writes everything.
Two refinements (`tier1_schedule_adjust`, `tier1_schedule_live_flags`):

- **Memory:** every access both reads and writes memory, so that no two
  accesses change places: there is no alias analysis, and a volatile access
  stays where it was among the others. A load or a store moves among the
  records that don't access memory.
- **Flags:** a record that may leave the block reads every flag, and every flag
  is live at the block's end. A writer whose flags are all overwritten before
  any record reads them is dead; two dead writers needn't keep their order,
  while every live writer, reader and other resource does. A shift by `cl`
  reads the flags it may keep (the semantics module's), so it never kills them.

The following records are full ordering barriers (`tier1_schedule_is_pinned`):
a caller's pin; an uncovered form or a decorated one (an opmask, a rounding
override, a segment); a possible arithmetic fault or trap; anything that may
leave the block or writes MXCSR; `stmxcsr`, which reads the floating-point
status flags every floating operation may set; a fence or an atomic access;
`endbr64`; a system form; and a compare or test right before a conditional
branch, together with the branch, so that the pair stays adjacent and
fusable. Floating operations move: their status flags are sticky, so their
order among themselves leaves MXCSR the same, and every reader of the flags
(a call, `stmxcsr`) is a barrier. There is no alias speculation, movement
across calls, division speculation, FP reassociation or false-dependency
rewrite.

A register's or a flag's RAW edge carries the writer's weight into the path of
its readers: a movable record's priority is its weight plus the largest
priority among those successors, and a barrier's is zero (a priority, not a
latency). The weight is the measured GPR latency of the record's operand case
where `x86_zen5.c` has one, and otherwise its class's (`tier1_schedule_weight`):
the measured rows' classes for the integer forms (an ALU operation 1 cycle, a
multiplication 3, a scaled `lea` 2, an L1 load 4, the load taking a move's place
and adding to any other form's), and for the floating forms, which the model
hasn't measured yet (#181), weights of their usual order (an addition,
multiplication, compare or conversion 3, a fused multiply-add 4, a division 13,
a square root 15). Weights order a block's ready records; they are not cycle
predictions. The model's coverage is still counted as before: an unmeasured case
is `unknown_cases`, and `missing_columns` the columns it lacks.

The vector implementation obtains effects in eight-record batches and model
latencies by the existing 32-ID in-register lookup. It compares each record's
resource masks against eight prior records together, producing dependency
bitsets. Reverse ready frontiers assign dependency levels; contiguous masked
height loads reduce the already completed successor levels. The scalar twin
uses scalar effects and costs, pairwise hazards and a backwards height pass.
The flags' liveness is one backward pass over the block, shared by both. Both
select a ready instruction of greatest priority and resolve ties by its
original position; this selection loop is shared and scalar.

`tier1_schedule_verify(input, order)` independently reconstructs scalar
effects from the original records. It first checks that the order is a complete
permutation, then that every register and memory dependency (any two accesses
dependent), barrier and fusion pair keeps its order, and, for each flag, that
every reader of it sees the same writer as in the original order and that the
block's last writer of it is the same. It does not trust the graph, the
liveness or the heights produced by either scheduler.

## Preallocation

Tier 1 has no machine IR before allocation: no selected instructions with
virtual registers. The selector's plan (`Tier1SelectPlan`) is a pattern per IR
node, a root and the nodes it folds; liveness, allocation and emission walk the
plan in the IR's order and write the records with physical registers. The only
order before allocation is the IR's, so the preallocation variant is a Tier 1
pass on the IR (`tier1_schedule_ir.c`), target-specific for all that: its
weights are x86 classes and its one adjacency rule is the x86 selector's load
fold. It runs before selection, which then makes its trees on the new order.
Those trees are the scheduling units a machine IR would have: the selector folds
a single-use child of the same block into its root wherever the child is
(`tier1_select_tree_child`), so the order changes no fold but two that need
adjacency, a load's (glued below) and a zero extension's of the compare right
before it (not glued, so a few of those may become `setcc` and `movzx`). What a
machine-IR scheduler would have that this one hasn't is the selected forms'
costs as weights; permuting the plan after selection, IR and patterns together,
would give it them, and so would a machine IR of virtual registers, which Tier
1's pipeline doesn't have.

`tier1_schedule_ir_function` reorders each block's instructions in place, on the
columns the driver owns at that point, as `ir_truth_simplify` does. A block's
instructions between its barriers form a segment, list-scheduled on its own
with a binary heap: the ready instruction of greatest priority first, ties by
original position. A barrier keeps its place and nothing moves across it: a
phi, a parameter, a terminator, a call, a stack allocation, save or restore, a
lifetime marker, a fence, an atomic access, an intrinsic and a volatile or
atomic load. In a segment an instruction follows its operands, and the loads,
stores and divisions keep their order among themselves. Priority is the longest
path to the segment's end with each instruction weighed by its class (a load 4,
a multiplication, floating operation or conversion 3, a division 13, a constant
or a slot's or symbol's address 0, any other 1).

Tier 1's selection folds a load into its consumer when it is the last memory
access before the consumer (`tier1_select_load_folds`). A load that does so in
the original order stays glued to its consumer: one node of the segment's
graph, placed right before the consumer, its weight added to the consumer's.
Every other value's place is the scheduler's, and with it the register pressure
the allocation sees: that trade is what this variant measures.

Blocks keep their instruction counts, so the block offsets, the extra column,
the side tables and the labels are unchanged; the instruction columns are
permuted within blocks and every value operand (a, b and c by `ir_value_mask`,
phi incomings and call arguments) renamed. With `--verify`, the driver copies
the columns first and `tier1_schedule_ir_verify` checks the result from the copy
alone: a permutation within each block, every barrier in its place, each
instruction at its new place with its operands renamed, every operand defined
in the block before its use (a phi's excepted), and the loads, stores and
divisions in their order. The optimizer's final SSA verification then runs on
the scheduled function as on any other.

The pass is scalar. Its graph is the operands' (at most three edges an
instruction in a segment, every instruction with more being a barrier) and the
memory order's chain, built in one pass, and its selection a heap: a vector
twin of a sparse, per-segment graph has nothing to compare many items at once
against, and the compile-time measurement below bounds what the pass costs.

## Tests

`test-tier1-schedule [--seed N]` checks both graph matrices, priority edges,
resolved effects, weights, heights, levels, order and coverage. It covers
handwritten RAW/WAR/WAW cases, partial registers, explicit pins, faulting
instructions, fences, atomics, `stmxcsr`, loads and stores among register
operations and among themselves, floating operations, dead and live flag
writers, flags readers (`setcc`, `cmov`) and calls, unknown immediates, known
L1 loads, compare/branch adjacency, malformed permutations, rejected capacities
and null/empty slices. Every length from zero through 256 ends at guarded input,
pin and output boundaries. Random mixed and movable-register blocks exercise
many legal topological orders through both paths and the verifier. The shared
64-record integer-loop fixture also compares both encoders' bytes and executes
the original and reordered programs against an independent reference for 64
inputs through each scheduling path.

`test-checker-driver`'s scheduling fixture compiles a program of loops, loads,
stores, multiplications and flags by both paths with each variant under
`--verify`, runs it against clang's build, and requires code to have moved under
both variants and none without the option; the postallocation variant's record
count must be the unscheduled build's. c-testsuite's 220 programs pass with
either variant under `--verify` (`test-c-testsuite --tier1 --compiler` with a
wrapper adding the option), every function by Tier 1.

`benchmark-tier1-schedule [--blocks N] [--records N] [--only
encode|scalar|vector] [--after-scalar MS]` freezes generated machine-record
blocks, verifies both schedules and encodings, and reports 11-pass median
nanoseconds and available core cycles per record: scheduling plus the record
copy and encoding, beside encoding alone, and a synthetic generated loop in
both orders. It is a stage boundary, not a compiler benchmark.

## The measurement on the 9700X

@@MEASUREMENT@@

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The implemented scheduler](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/tier1_schedule.c#L4)
consumes physical-register `X86Instruction` records and caller pins. Effects
from `x86_instructions_effects` and operand-sensitive Zen 5 cost rows feed
hazard edges and weighted priorities; `tier1_schedule_block` produces a u16
`order[new] = old` permutation. Since #172's measurement the C driver applies
it (`--tier1-schedule=post`, `c_check_tier1_schedule.c`): blocks cut at every
branch, target, label, frame and source event, references carried through the
mapping and sorted again **before** branch sizing and encoding, and
`tier1_schedule_verify` as the independent scalar consumer under `--verify`.
A block above the 256-record bound is scheduled in pieces nothing moves
across, which keeps every order across the cut and assumes nothing at a piece's
ends (every flag live at its end, nothing about its entry). Internal
eight-record comparisons and height reductions are not scheduling independence
boundaries. The preallocation variant is a separate edge (`tier1_schedule_ir.c`), on
IR before selection, not this API moved earlier.
