# Tier 1 frame planning

`src/buster/compiler/tier1_frame.c` implements the planning part of
[#177](https://github.com/buster14a/buster/issues/177): interval stack-slot
sharing, fixed areas, stack alignment, register preservation and explicit
prologue/epilogue steps. It emits no machine instructions or unwind tables
itself, but its steps are Tier 0's frame protocol, which the C driver's unwind
tables describe ("Unwind tables" below). The scalar and vector paths produce
valid frames with identical class counts, storage sizes and prologue plans; the
physical color assigned to an individual object can differ.

## Producer and consumer contract

`tier1_frame_requirements(request, result)` is the query to run **before
register allocation**. Supply the local objects, sizes and alignments of every
value that might spill, outgoing calls and frame options. Exclude the returned
`reserved_registers` from both the allocator's class masks and its per-interval
allowed masks. RSP is always unavailable to allocation independently of this
query. The frame query reserves RBP for dynamic stacks, requested frame
pointers or alignment beyond 16 bytes; dynamic stacks with such realignment
also reserve R15 as a stable base (Tier 0's realigned base too, which the unwind
tables' reader checks). It does not silently repair an allocation.

After allocation, `tier1_frame_plan` and `tier1_frame_plan_scalar` take the
actual memory objects. They reject conflicting allocated registers and check
the final frame extent, which can grow after spilling. An underestimated
pre-allocation requirement therefore requires an explicit allocation retry.

`Tier1FrameSlots` borrows four `uint32_t` columns: `start`, `end`, `size` and
`alignment`, each with `column_count` elements. With no `indices` column, the
first `count` rows are objects. Otherwise `indices[count]` selects distinct
rows; duplicates and out-of-range indices are rejected. No input padding or
vector alignment is required. Each object is a nonempty half-open interval
`[start, end)` and has positive size and power-of-two alignment. Endpoints can
use the entire unsigned 32-bit range; the planner never computes `end + 1`.
Zero objects permit null pointers or inaccessible zero-length columns.

The allocation output `Tier1Allocation.spill_values[spill_count]` is the
compact index list. When the producer retains **value-indexed** conservative
lifetimes and type sizes, it connects directly:

```c
Tier1FrameSlots slots = {
    .start = value_starts,
    .end = value_ends,
    .size = value_sizes,
    .alignment = value_alignments,
    .indices = allocation.spill_values,
    .count = allocation.spill_count,
    .column_count = value_count,
};
```

These are not the allocator's fragment-indexed `starts` and `ends`. A value
with several intervals needs their conservative lifetime union: minimum start
through maximum end, including any intervening holes. The interval producer
should retain these bounds while constructing intervals. An address-taken
local supplies its conservative scope lifetime, including every possible
escaped use; a local or aggregate producer can use the same column contract.
The current C checker and source-to-SSA path do not yet produce this complete
frame request. The frame module introduces no parallel IR.

`AbiCall[]`, produced by `abi_assign` or its scalar twin in
`calling_convention.c`, supplies each call's outgoing byte count and required
alignment. The planner keeps their maxima and rejects malformed totals.
`used_callee_saved` is the union of callee-saved general registers actually
used by allocation. All SysV vector registers are caller-saved. Flags request
variadic saves, dynamic stack storage, a frame pointer, red-zone exclusion and
stack-clash protection.

For input item `i`, `Tier1Frame.slot[i]` is the physical slot ID and
`offset[i]` its signed byte displacement from `base_register`. A spilled
value's lookup is therefore `offset[allocation.spill_ids[value]]` with the
compact view above. `slot_count`, `class_count`, `object_bytes`,
`payload_bytes`, `outgoing_bytes`, `stack_adjust_bytes` and
`maximum_stack_bytes` describe the storage and stack movement. The last
includes the pushes, the worst realignment gap and the pointer push, or the
pushes and the red-zone extent when no stack adjustment is needed.
`saved_registers` and `save_offset[16]` describe the pushed callee-saved
registers, each slot's displacement from RBP in a frame with a frame pointer and
from the post-prologue RSP otherwise; RBP's mandatory pointer push is
represented by its own prologue/epilogue steps.

Output columns belong to the caller's output `Arena`. Temporary columns belong
to a distinct scratch `Arena`. Newly allocated output/scratch storage must not
overlap the inputs or result; the arena regions must be disjoint. Inputs can
live in the retained prefix of the output arena. A failed plan preserves the result and both arena
lengths; successful planning also restores scratch. Object counts above
`INT32_MAX / 2` fail before reading columns. Individual rounded sizes, signed
displacements and the worst-case frame extent must fit `INT32_MAX`. Capacity,
invalid input, extent overflow and register conflicts have distinct status
values. There is no truncation or partial output publication.

The batching unit is a whole function. Arbitrarily splitting one function's
lifetimes would lose cross-batch interference and is not legal. Within the
function the kernels use masked blocks of eight or sixteen items; partial
blocks are flushed immediately. Reordering input items only changes their
index mapping and permitted color choices, not the frame extent.

## Interval coloring

Objects are grouped by alignment and size rounded up to that alignment. A
stable radix sort orders their start and end events by class and position,
with ends first at a tie. The vector path then:

1. Scans `+1` at starts and `-1` at ends to obtain live pressure.
2. Finds the nearest preceding event with equal or greater pressure using
   simultaneous pointer-jumping rounds. At a start, the event immediately
   after that predecessor is the free-slot-producing end it matches.
3. Makes a fresh color only for a new class pressure maximum. Other starts
   inherit their matched interval's color; pointer jumping resolves those
   inheritance chains.
4. Forms the class byte offsets and physical-slot bases with prefix sums.

This is the bracket-matching formulation in #177, expressed in live-pressure
coordinates. A class uses exactly its maximum simultaneous live count. The
scalar twin independently scans starts and takes the first class color whose
previous interval has ended. It reaches the same per-class lower bound, so
it never produces a smaller frame than the vector path. Neither path claims
globally optimal packing across different storage classes.

Classes are laid out by descending alignment. Each class stride is a multiple
of that alignment, so a prefix sum after the first class's aligned start
preserves all later class alignments. The stable radix scatter and the small
per-function policy decisions are scalar in both paths. Indexed row loads
and pointer-jump neighbor loads use bounded scalar staging rather than Zen 5's
expensive wide gathers. Their costs remain in the measurements; a measured
9700X justification or further optimization remains part of qualification.

## Frame and prologue rules

The prologue is Tier 0's frame protocol, the one `unwind_tier0_frame_tails`
reads and `unwind.c` describes: with a frame pointer `push rbp; mov rbp, rsp`
first; then each callee-saved register the allocation uses pushed, in RBX, RBP,
R12, R13, R14, R15 order (ascending encoder numbers); then one subtraction; then
a realignment's `and rsp, -alignment` and, for a dynamic realigned frame, its
base's `mov r15, rsp`; then the variadic saves. The epilogue releases the
subtraction (`add rsp`) or, with a frame pointer, resets rsp from rbp past
whatever the body allocated or realigned (`lea rsp, [rbp - 8n]` below the `n`
pushes, `mov rsp, rbp` without any), then pops the registers in reverse, and
rbp last. Pushes take a byte or two each where stores into the frame took four
or five, as clang's prologues do.

The subtraction's region holds outgoing arguments at its bottom, then colored
objects, then the aligned 176-byte SysV variadic register-save area when
requested. Entry RSP is 8 modulo 16 and every push takes 8, so rsp after the
pushes is 0 or 8 modulo 16; the subtraction includes that residue so that rsp
after it is 16-aligned, and an odd number of pushes can align a frame without
one (a caller's frame of three pushes and no locals subtracts nothing). Calls
can require alignment beyond the usual 16 bytes, which realigns the frame like
an over-aligned object. Dynamic frames keep the maximum outgoing requirements
but set `dynamic_outgoing`: the consumer must allocate and align outgoing
arguments at each call's current RSP, separately from fixed local storage.

The leaf red zone is allowed only without calls, dynamic stack allocation,
realignment or a required frame pointer, and when the complete payload fits
128 bytes below rsp after the pushes, whose residue the planner includes when
placing sixteen-byte objects. With no pushes, a 128-byte object aligned to
eight fits; the same size aligned to sixteen needs 136 bytes of downward extent
and does not. `NO_RED_ZONE` disables it explicitly.

Fixed frames address objects through their post-prologue RSP. Ordinary dynamic
frames use RBP with negative displacements below the pushes. A realigned frame
subtracts and then aligns, so that its objects lie in the subtraction's bytes
above the aligned rsp; a dynamic one then copies rsp into R15, one of its
pushes, as its stable aligned base, which its pop restores after the reset.
A fixed realigned frame only needs RBP for restoration and uses RSP to address
objects.

The variadic prologue plans all six general argument-register saves and eight
sixteen-byte vector saves; the latter are conditional on entry AL being
nonzero. The emitter must preserve that entry AL value until these
conditional saves are decided. Variadic stores explicitly name RSP as their
base. Tier 1's CFG consumer asks for this area for a variadic definition with a
register save area (#789). The IR's save-area slot is the area itself, at
`variadic_offset`. Its overflow area is addressed from the incoming frame:
16 bytes past RBP with a frame pointer, and otherwise past the reservation,
the pushed callee-saved registers and the return address from RSP.

With `PROBE_STACK`, allocations of at least 4096 bytes use a
`PROBE_ALLOCATE` step. A realigning prologue probes its subtraction whatever its
size and uses `PROBE_ALIGN` after it, so that the alignment gap's probes start
from a touched address: otherwise the subtraction and the gap together could
cross an unprobed page. These abstract steps require the emitter to touch each
descending page-sized interval **and the final destination**. The recorded
amount is the subtraction size or target alignment; the probe stride is
`BUSTER_TIER1_FRAME_PAGE_BYTES`. Dynamic-body allocations require their own
protected lowering and are not prologue steps. A probe's loop moves rsp a page at
a time, which the unwind tables' protocol doesn't describe: the C driver, which
asks for no probes, refuses such a frame rather than describe it wrongly.

The plan has bounded step arrays and no instruction positions. Spill
loads/stores belong to the instruction consumer.

## Native frame companion

[`tier1_frame_emit.c`](../src/buster/compiler/tier1_frame_emit.c) consumes the
same `Tier1Frame` plan after instruction allocation. Its entry point is
`tier1_frame_emit(const Tier1Frame*, bool epilogue, Tier1EmitBuffer*)`, returning
`TIER1_FRAME_EMIT_OK`, `TIER1_FRAME_EMIT_INVALID`, or
`TIER1_FRAME_EMIT_CAPACITY`. The shared buffer has `records`, `count`,
`capacity`, `branches`, `targets`, `branch_count`, and `branch_capacity` fields.
Instruction records use the existing `X86Instruction` representation. Both
branch columns use absolute record indices, including when a frame sequence
is appended after an existing prefix. The complete function then goes through
the existing branch-sizing and encoding stages.

The caller appends the prologue once and the epilogue at each return. Appends
are transactional: a malformed step or insufficient capacity leaves all
destination bytes and counts unchanged. Conservative bounds per call are
`BUSTER_TIER1_FRAME_EMIT_RECORDS` (384) and
`BUSTER_TIER1_FRAME_EMIT_BRANCHES` (64), independent of frame byte size.
The helper expands the bounded control protocol directly; it adds no IR,
allocation columns, or retained per-object representation.

Reserve R11 (`BUSTER_TIER1_FRAME_EMIT_SCRATCH`) before register allocation,
in addition to the planner's requirements mask. Probe loops use only R11,
RSP, and flags; they preserve the six incoming general argument registers,
XMM arguments, and the original entry AL. Consecutive vector-save steps
share one test of AL and one conditional branch around their `movaps` stores.
All emitted forms belong to baseline x86-64. Epilogues preserve return-value
registers. Without probes, the records are the protocol in Tier 0's own forms,
which the unwind tables' reader matches: `push` and `pop`, the frame pointer's
and the base's `mov` in their `RM64_R64` form, `sub`, `add` and `and` with a
sign-extended imm8 where the amount fits, and the reset's `lea` from rbp.

Protected allocation descends by 4096 bytes with a byte read/write at each
step, then touches any final partial step. Protected realignment also touches
the final aligned address. Large alignment gaps use a bounded loop rather
than a record per page; the loop never steps past its final target. Ordinary
alignment or allocation without `PROBE_STACK` emits the corresponding single
arithmetic instruction. The helper does not lower dynamic body allocations or
dynamic outgoing call areas; the instruction consumer must reserve, align,
and, when required, probe those at the call or allocation site.

The companion publishes records and branch targets; the C driver's emission
finds the transitions' PCs in them after branch sizing ("Unwind tables" below).
Full ABI cross-linking and corpus requirements remain open as listed below.

## Unwind tables

Tier 1's code in the C driver publishes its frame to the emission as Tier 0's
code does (`check_tier1_frame` in `c_check_tier1.c`, into `Tier0Code`'s
`frame_saved`, `frame_size`, `frame_pointer`, `frame_alignment_log2` and
`frame_base`), so that `check_program_emit_code` writes its unwind tables with
`unwind_tier0_frame_tails` and `unwind_write`, the vector writer and its exact
twin, as it writes Tier 0's, and `--verify`'s analyzer (`unwind_tier0`) checks
the frames it takes. `--tier1` therefore compiles with unwind tables by default,
under the driver's usual options ([the unwind guide](unwind.md)'s "The C
driver's unwind tables"), and no longer needs `-fno-asynchronous-unwind-tables`.

Tier 1 puts an epilogue before every return's `ret` rather than jumping to one.
Both consumers record each return's `ret` (`Tier1EmitResult.exits`). Every
return but the code's last record is published as one of the code's tails
(`Tier0Code.tail_records`), the way a tail call's teardown is (#147): its CFI
remembers the body's rules before the teardown, follows the release and pops,
and restores them after the `ret`. Where the code's last record isn't its last
return's `ret` (a loop whose latch's jump comes last, a call that doesn't
return, a trap), the frame is open (`Tier0Code.frame_open`,
`UnwindFunction.open`): its FDE has the prologue's transitions alone, the body's
rules hold to its end, and each of its returns is a tail. A frameless function
publishes no tails, since its rules are the CIE's throughout; it is published
open where its code doesn't end in a `ret` (the reader otherwise wants a closed
frameless function to end in a `ret` or a `ud2`), and its FDE is the closed one
either way. A frame with probes isn't the protocol, and the C driver refuses it
(`TIER1_EMIT_FRAME`), never asking for one. So does a frame whose exits aren't
all `ret`s, or whose last record is a `ret` though the frame is published open:
`check_tier1_frame` checks both, so that a disagreement with the emission's
reader would send the function to Tier 0 rather than fail the compile.
`--tier1-report` counts the shapes: `open_frames` (open frames with
transitions), `return_tails` (returns published as tails) and `red_zones`
(frames whose objects are in the red zone).

`test-check-debug --cross-check` (`c_check_unwind_test.c`) checks it on a
fixture compiled by `--tier1 --verify`, every function but `main` by Tier 1:
a frame of spill slots alone, one with pushes, two returns each after its own
epilogue, an open frame whose loop's latch comes last, a realigned frame, and a
leaf whose spills are in the red zone, whose shapes `--tier1-report`'s counts
must show.
By each path and through GNU ld and lld, `backtrace()` walks through every frame
to `main` (and without unwind tables stops at the first), and gdb's backtrace at
every instruction of each function, both returns and the loop's iterations
among them, gives the function and `main`. Tier 1 doesn't make the other
shapes the planner can: its allocator gives a leaf no callee-saved register (they
go only to values live across a call), so a red zone has no pushes above it, and
its selection refuses the dynamic stack's operations, so no frame of Tier 1's is
addressed from rbp (`test-tier1-frame` and `test-tier1-frame-execution` check
those). `test-unwind` checks the writer's
open descriptions against its independent CFI interpreter and
`unwind_tier0_frame_tails` on early returns and open frames of every save mask,
with and without a frame pointer, with its refusals. `test-c-testsuite --tier1`
compiles c-testsuite's subset with the tables and `--verify`.

## Checking and measurement

`test-tier1-frame [--seed N] [--count N]` checks both paths against independent
pairwise byte-overlap and alignment rules and an interval-pressure lower
bound. Cases cover simultaneous starts/ends, nesting, chained reuse, sparse
positions near `UINT32_MAX`, many classes, legal index permutations, every
frame-policy combination, variadic saves, call maxima and all callee saves.
A sparse abstract stack executes the prologue and epilogue, including dynamic
RSP movement and multiple entry residues for realignment. Protected pages
check unpadded input tails, inaccessible empty inputs and exact-capacity output.
Invalid indices, duplicates, arithmetic limits, arena exhaustion and conflicting
reserved registers check refusal and rollback.

The abstract stack also checks that the steps follow the unwind tables'
protocol (the order of the pushes, subtraction, realignment and base, and the
epilogue's release or reset and its pops in reverse) and that each push writes
the slot `save_offset` publishes. With push-based saves, local diagnostics in a
cloud container passed 2,277,543 checks with seed 177 and eight rounds and
8,802,059 with seed 177177 and 32 rounds (the earlier store-based planner's
were 2,273,931 and an ASan/UBSan 8,793,497). These are local correctness
diagnostics, not 9700X or emitted-code acceptance evidence. A local benchmark smoke exercised all four
phases and both paths at 65 objects; assembly inspection confirmed bounded
masked vector loads/stores and the pressure-scan vector additions.

`test-tier1-frame-execution` executes the native companion in an isolated
child on controlled stacks. It covers all 32 frame-policy combinations,
small and page-spanning objects, alignment through 8192 bytes, two entry
residues, deliberate changes to every allocated callee-save register, dynamic
RSP movement, AL values 0/1/8, and all 176 variadic save-area bytes. Its
generated caller independently checks callee-save sentinels and restoration
of its original stack. Real nested calls check fixed and dynamic outgoing
areas, 16-/64-byte call alignment, stack arguments, and preservation of locals.

Guard-page tests compare a probing function with an otherwise identical
unprotected control: only the probing function faults on an intermediate
allocation page, a 65536-byte alignment gap, or the final partial allocation
step. Additional cases check malformed steps, exact output capacities,
unchanged destination bytes on refusal, and the maximum 384-record/64-branch
append. Local optimized and ASan/UBSan runs each passed 14,619 checks, and the
push-based emitter's optimized run passed the same 14,619. These are native
helper correctness diagnostics; they do not substitute for the
source-to-program ABI/corpus qualification or controlled 9700X measurements.

`benchmark-tier1-frame [--only scalar|vector] [--phase prepare|color|layout|total]
[--count N] [--seed N] [--after-scalar MS]` uses original generated lifetime
columns frozen before timing. The default leaf, calling, variadic and dynamic
realigned cases contain 16, 256, 4096 and 32768 objects. The indexed cases
include the compact-index validation and sorting cost. It reports 21-sample
medians in nanoseconds per object and function, useful objects per second,
CPU cycles where available, borrowed input bytes, planner scratch bytes and
retained output bytes. The harness keeps a separate prepared copy for isolated
phase measurements, which is benchmark storage rather than production planner
retention. Full planning retains eight bytes per object plus the fixed plan;
preparation needs approximately 116 scratch bytes per object.

Run every build, test and benchmark separately through the shared 9700X lock:

```sh
EXCLUSIVE_OWNER=codex-138-frame EXCLUSIVE_TITLE='Tier 1 issue 138' \
EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/177 \
~/buster-fast-bench/exclusive.sh taskset -c 2 \
./build/compiler benchmark-tier1-frame --only vector --phase total
```

Repeat frozen inputs in separate processes, alternate scalar/vector order and
also measure `--after-scalar 100` through `300`. The total row includes
validation, copies/staging, sorting, scratch setup, coloring, layout and all
output stores; isolated rows cannot establish a total improvement. Synthetic
generation precedes timing. Source-level interval construction, allocation,
native emission and generated-program execution are outside this benchmark.

No 9700X timing or speedup is claimed here, nor for the push-based saves:
they shorten every prologue and epilogue that saves registers, but their run
time on `speedtest1` hasn't been measured. Issue #177 remains open for
[#142](https://github.com/buster14a/buster/issues/142) generated-code checking,
[#137](https://github.com/buster14a/buster/issues/137) Tier 1 cross-linking
including variadic definitions (which the CFG consumer still refuses) and
over-aligned functions, address-taken locals' scope lifetimes (each frame object
is live throughout its function today, so objects never share a slot),
frame-size comparisons against `clang -O2` on the corpora, and controlled
`speedtest1`/corpus runtime evidence for slot sharing and frame-pointer
omission. Prologue-plan simulation and bounded native-helper execution do not
satisfy those full-program requirements.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Sources:
[planner](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/tier1_frame.c#L4)
and [native record consumer](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/tier1_frame_emit.c#L4).
Both exist; the full source-local/allocator producer remains an integration.

The real dependency is **potential objects/calls → frame requirements →
allocator register reservations → actual spills/callee-save usage → frame
plan → native prologue/epilogue records**. `tier1_frame_requirements` therefore
precedes allocation, although final `tier1_frame_plan` follows it. A late
change needing more reserved registers requires an explicit allocation retry.
The native companion emits records and branch columns; the C driver's emission
derives the final-PC unwind tables from them ("Unwind tables" above), while `-g`'s
source rows for Tier 1's code are still missing.

The planner borrows u32 start/end/size/alignment columns and an optional compact
index list with separate selected count and column count. Retaining value-wide
conservative lifetime bounds at the interval producer avoids gathering/copying
fragment rows into a second table. Duplicate indices are invalid. Intervals are
half-open and nonempty; escaping address-taken locals require a safe scope
lifetime. Natural alignment and exact logical extents suffice, with no padding.
Arenas and new output/scratch are disjoint from live inputs; retained output
prefixes may hold inputs. Failure preserves result and arena lengths.

One function is the semantic batch: independent chunks would miss cross-chunk
interference. Internal class/event sorting puts ends before starts at ties,
then color/layout phases produce object-to-slot/offset columns and explicit
steps. Scalar/vector colors may differ but must preserve interference, class
slot counts and frame sizes. Register/stack limits and all displacement bounds
are checked; partial-vector tails never create objects.

Disposition: retain explicit preallocation requirements and direct indexed
spills. `test-tier1-frame` includes independent layout/pressure checks, guarded
tails, failure rollback and input reorderings; `test-tier1-frame-execution`
executes actual ABI preservation, alignment, variadic saves and page probes.
Preparation/sort/color/layout benchmark rows are synthetic. [#177](https://github.com/buster14a/buster/issues/177)/[#140](https://github.com/buster14a/buster/issues/140)/[#133](https://github.com/buster14a/buster/issues/133)/[#88](https://github.com/buster14a/buster/issues/88)
retain actual interval production, full function emission, source/frame events
and whole allocation + frame + code/runtime/memory evidence. Do not call frame
metadata alone an unwind implementation.
