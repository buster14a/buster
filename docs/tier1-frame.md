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
The C driver's `--tier1` gives each block's objects its block's lifetime and
the CFG consumer passes them so ("Slot sharing between objects" below). The
frame module introduces no parallel IR.

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
a time, which the fixed-frame descriptor can't describe: its frame events
(#1056) put the CFA in r11, the loop's bound, from the subtraction that sets it
to the loop's exit, where rsp equals it ("Unwind tables" below). The C driver
asks for them under `-fstack-clash-protection` ("Stack-clash protection on
request" below); `test-emitted-code` proves and runs such frames in isolation.

The plan has bounded step arrays and no instruction positions. Spill
loads/stores belong to the instruction consumer.

### The frame pointer on request

`-fno-omit-frame-pointer` (the last of it and `-fomit-frame-pointer`) keeps a
frame pointer in every function, and with `-momit-leaf-frame-pointer` (the last
of it and `-mno-omit-leaf-frame-pointer`) in every function that calls, as
clang's driver reads them (`predefines_frame_pointer`); without the option
neither tier keeps one where its frame doesn't need one, at every `-O`, where
clang keeps it at `-O0`. The checked program carries the choice
(`CheckProgramOptions.frame_pointer`, from the predefines through
`check_request_program_options`) into each function's `Tier0Function.frame_pointer`:

- Tier 1's frame takes `KEEP_POINTER` (`Tier1EmitOptions.frame_flags`), and
  with the leaf rule `Tier1EmitOptions.omit_leaf_frame_pointer`, which drops it
  for a function without calls to functions, a machine row's none
  (`tier1_emit_frame_flags`, both consumers). A function that reads its frame
  address (`__builtin_frame_address(0)`, `__builtin_return_address(0)`, #1847)
  takes it whatever the options say.
- Tier 0's frame keeps one as a frame that moves rsp has, though its rsp stays
  put (`tier0_kept_pointer`, `tier0_frame_registers`): rbp no depth's register
  (`tier0_integer_depth_count`'s, and the u64 conversions' scratch registers by
  it), its places from rbp, its outgoing area at rsp. Its calls are its CALL
  entries, as its selection counts them.

Both publish the frame (`Tier0Code.frame_pointer`), so the unwind tables
describe rbp's CFA. `test-checker-driver`'s frame pointer fixture
(`c_check_frame_pointer_driver_test.c`) builds a chain of three functions (a
frame object, a variadic definition, a 64-byte-aligned local and a double live
across a call) between clang's main and a clang walker that follows rbp from
its own frame. It checks each return address is in the function it expects and
the chain ends at main's frame. That runs by Tier 0 and by `--tier1` with and
without rematerialization, by each path, with the leaf rule and without, and
each object starts each chain function with `push rbp; mov rbp, rsp`, the leaf
function under the leaf rule never. `test-tier0` runs a quarter of its random
programs with every frame's pointer and a quarter with the leaf rule's, against
the same oracle.

### Stack-clash protection on request

`-fstack-clash-protection` (the last of it and `-fno-stack-clash-protection`,
`predefines_stack_clash`; off by default, as clang's) makes every frame and
every dynamic allocation of both tiers touch each page below rsp before rsp
moves past it, so that none steps over a guard page. The checked program
carries it (`CheckProgramOptions.probe_stack`) into each function's
`Tier0Function.probe_stack`. A page is 4096 bytes; each touch is `or byte
[rsp], 0`, which never crosses a page and changes no byte. The probes move rsp
a page at a time rather than touch below it, since kernels before Linux 4.20
refuse an access more than 64 KiB below rsp where the main thread's stack must
grow.

A reservation under a page is touched only where the frame is realigned or the
body moves rsp (an allocation, a stack restore): a dynamic allocation must
start from a touched rsp, or its first touch could land a page below an
untouched reservation, past a guard page (LLVM's constraint). Other frames
under a page, with a frame pointer or not, leave less than a page untouched,
and the next call's return address is the next touch. Until #1450 a frame
pointer alone made both tiers touch, so with `-fno-omit-frame-pointer` every
call did a read-modify-write of `[rsp]`. That isn't the default.

The option's run-time cost is placement. #177 timed Tier 0's `closures.lua`
4.59% slower with it, in one layout per build. Tier 0's Lua probes in two
functions only: `luaV_execute`, whose 5,064-byte frame takes a page's loop and
a remainder's touch on every call, and `luaL_loadfilex`. A cycles profile
doesn't place the difference at the probe: `luaV_execute`'s share falls. Over
16 recorded link orders of fixed objects (the [profiling
guide](commands/profiling.md)'s protocol), the option's paired effect on
`closures.lua` is -1.02% at Tier 0 (95% interval -2.37% to +0.35%) and -1.37% at
Tier 1 (-3.75% to +1.07%). Layout alone spreads Tier 0's time over 14%, and in
the normal order the option is 6.2% faster (#1450's item 6, [the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-remaining-1450-2026-11-06),
buster-performance#303).

- **Tier 1's prologue** takes `PROBE_STACK` (`Tier1EmitOptions.frame_flags`):
  the planner's `PROBE_ALLOCATE` and `PROBE_ALIGN` above, with their frame
  events. `Tier1Frame.probe_stack` records it. Selection accepts object
  alignments through the planner's `1 << 30` bound, including those larger
  than a page (#1450).
- **Tier 1's dynamic allocations** (`TIER1_RULE_ALLOCA` and its immediate
  form) are the CFG consumer's `tier1_emit_cfg_probed_allocation`: the rule's
  records, but its subtraction from rsp, whose bytes a loop takes instead, in
  the rule's own register (an immediate moved there first). While more than a
  page is left, rsp moves a page and its byte is touched; then, unless nothing
  is left, rsp moves by the rest and its byte is touched. Its branches target
  its own records. No byte at or above the old rsp, the frame's own, is
  touched, so the frame events' proof (`emitted_verify_frames`) keeps the
  saved registers' slots.
- **Tier 1's dynamic outgoing call areas** (`tier1_emit_cfg_adjust`) touch
  their lowest byte for a sub-page area and use the frame emitter's page loop
  and final partial touch for a larger one (#1450), which selection doesn't
  make today: an area is the call's stack bytes, at most 2,560, rounded to the
  calls' alignment. `test-tier1-emit`'s native call-probe fixture runs the loop.
  Reserved r11 holds the
  bound, leaving all argument registers and an indirect callee intact; the
  dynamic frame's CFA stays in rbp. Records and branch columns reserve the
  loop's full capacity before emission, and release adds the area back without
  a probe.
- **Tier 0's prologue** probes a reservation of a page or more, and a realigned
  frame's or one whose body moves rsp (`Tier0Frame.moves`, published as
  `Tier0Code.frame_moves` for the unwind reader) whatever its size
  (`tier0_probes_reservation`), with Tier 1's records:
  `tier0_emit_probed_reservation`, r11 set to rsp less the pages, rsp moved a
  page and touched until it reaches r11, then the remainder's subtraction and
  touch; a realigned frame touches after its `and`, or past a page's alignment
  takes `tier1_frame_emit_probe_align`'s loop (`tier0_emit_probed_alignment`).
  r11 is free there: nothing arrives in it and no parameter has moved yet.
  The branches are the prologue's own (`tier0_put_local_branch`), counted by
  `tier0_prologue_branches` in both paths, the body's after them.
- **Tier 0's unwind tables:** `Tier0Code.frame_probe` publishes it, and the
  fixed reader (`unwind_tier0_frame_tails`) checks the probed records against
  Tier 0's own emitters' (a branch in either of the forms branch sizing leaves)
  and takes the reservation's last record that moves rsp as its transition.
  Without a frame pointer the loop's CFA is in r11, which no fixed shape says:
  the emission adds the two rows Tier 1's frame events have
  (`emitted_tier0_probe_rows`), r11's CFA from the bound's subtraction and
  rsp's at the loop's exit, and the rows resolve into a row program.
- **Tier 0's `ALLOCA`** (`tier0_emit_probed_allocation`) is Tier 1's loop in its
  count's register, which the entry's address then takes: 13 records (its
  rounding's two, the loop's ten, the address's one) and
  three branches. It takes no other register, since every other one may hold
  a deeper value (r11 is depth 8's) or a local's home. A deferred entry's
  branches are now counted and kept in entry order outside deep entries too,
  and the vector path sorts its columns in a probed function, as it does
  around deep entries.

Default output doesn't change: every probe is behind the flag.
`test-checker-driver`'s stack-clash fixture (`c_check_stack_clash_driver_test.c`)
runs a function on a stack of its own, a `PROT_NONE` page below its first, for
each of a three-page frame, `alloca`, variable-length array, 64-byte-aligned
frame and two-page-aligned frame. Each build prints what clang -O0's does:
"skipped" past the guard without the option, "probed" at the guard with it.
That holds by Tier 0 and by `--tier1` with and without rematerialization, by
each path, with a frame pointer and without, under `--verify`, every function
required to use Tier 1, including the two-page-aligned frame. Its probes fixture prints clang -O0's line through frames of
pages, variable-length arrays and allocas in loops, an allocation of none, of
a page and of a constant, recursion through two-page frames and a realigned
frame, plus a dynamic call after an allocation in a frame realigned to two
pages, whose outgoing area stays the call's own 256 bytes: rsp keeps the
calls' alignment, not the frame's (#1450), and selection's 2,560 stack bytes
make no area of a page. `test-tier1-emit`'s native call-probe fixture checks six integer and
eight vector argument registers, AL, an indirect-callee register, callee-save
registers and exact rsp restoration for zero, sub-page, page and multi-page
areas. Protected intermediate and final partial pages fault only with probes;
unprotected runs and the no-probe controls return normally. Exact-capacity and
short-column cases check the transactional emission boundary.
Each probed frame's function starts with the probes' loop; and the last
of the pair wins, byte for byte. `test-emitted-code` proves probed Tier 0
frames' events (every other round's programs, and frames of each size and
alignment with a frame pointer and without, the loop's without one a row
program) and refutes their mutants; `test-tier0` probes half its random
programs, `ALLOCA`'s loop run natively against its interpreter.

## Slot sharing between objects

Tier 1's frame lets objects that are never live at once share a slot (#177's
design: an address-taken local's lifetime is its scope, conservatively, since
its address can escape within it). The lifetimes are C's own: an automatic
object lives from its block's entry to the block's exit, however either
happens, wherever its declaration is in the block (C17 6.2.4p5).

- The checked program, asked by `--tier1` (`CheckProgramOptions.lifetimes`,
  `c_check_program_lifetime.c`), lists each block's local declarations by its
  closing brace (`CheckProgram.lifetime_first`). Its control stream gets a
  `CHECK_CONTROL_LIFETIME` start where the block's statements begin, and an end
  after them (after the block's cleanups) and before each break, continue or
  goto that leaves it (after the jump's cleanups). A goto back inside the block,
  before a declaration, leaves nothing: the object still lives, and a pointer to
  it may be read there. Clang's markers end such an object, which C doesn't. A
  return, a computed goto and the body's end need no end, since nothing in the
  block follows them, and an end missing on a path only makes a lifetime
  longer.
- A block that a jump can enter other than through its opening brace has no
  lifetimes; its objects live throughout the function, as clang leaves out the
  markers of an object a goto bypasses (`VarBypassDetector`). Such blocks are:
  - those between a goto's label (or an asm goto's) and the innermost block
    that holds the jump too. No emitted body reaches the asm goto part yet:
    the lowering refuses asm goto by name (`CheckProgramAsm.reason`), and the
    part stands ready for when it takes one;
  - those between a case or default label and its switch, the switch's own
    block among them, whose opening brace never runs;
  - every block around a label whose address is taken;
  - a statement expression's own block.
- The lowering turns each control into an event per local of the block
  (`Tier0Function.lifetime_entries` and `lifetime_locals`), which Tier 0
  doesn't read. `ir_lower` turns the events into `lifetime` markers before their
  entries ([the IR guide](ir.md)'s "Slot lifetimes"), and SSA promotion drops a
  promoted slot's markers.
- The CFG consumer (`tier1_emit_cfg_lifetimes`) finds where each object with
  markers is live. That forward problem (some path from a start reaches here
  without an end) is backward liveness on the reversed CFG, which `dataflow.c`'s
  AVX-512 solver, or its scalar twin with `--scalar`, solves. A block's last
  marker of an object is its use where it starts the object and its definition
  where it ends it. The object's interval for the coloring above is the hull of
  the positions where it is live, among the spill slots'. An object without
  markers keeps its whole function.

The positions are the blocks' layout, so a hull can cover positions where its
object is dead (a branch's other arm laid out inside), which only keeps slots
apart. An access through an object's address comes between a start and an end
on some path, so its positions are inside the hull. The analyses' passes are
walks up the parse tree from the declarations, jumps, cases and labels, each
bounded by its depth, and they run only for `--tier1`. Like the control stream
they feed, they are scalar.

`--tier1-no-lifetimes` compiles without lifetimes, every object a slot of its
own. On `test-checker-driver`'s lifetime fixture (`check_test_driver_tier1_lifetimes`),
which has the cases above, Tier 1's frames take 1,480 bytes with lifetimes
against 2,064 without, by both paths. Every build prints clang's output. That
was measured in a cloud container: frame sizes against `clang -O2` on the
corpora and the run time on the 9700X are still to be measured.

## Call-save rows and callee-saved registers: what wasn't done (#1625)

#1625 measured two refinements that #177's design deferred and #1450's item 8
left unattempted, on the 9700X, with main `cf38957a`, on speedtest1 and Lua
5.4.8 as `measure-tier1-programs` builds them by `--tier1`. Neither is worth
implementing now. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-frames-1625-2026-11-07)
(buster-performance#313) has the measurement patch, the scripts and the outputs.

### Shared call-save rows

A function that calls has an 8-byte row for each caller-saved general register
that some call saves, and the row lives over the whole function
(`Tier1EmitPrepared.call_save_mask`).

The rows take 23.2% of speedtest1's 146,874 frame bytes and 20.7% of Lua's
66,288. Colouring them as objects are coloured could shrink them, but not below
the most registers any one call saves. Under that bound:
- sharing among the rows themselves saves at most 1.9% and 2.0% of those bytes;
- sharing with the spill slots too saves at most 5.8% and 2.3%;
- that is about 3 bytes per frame, in frames already 0.89× clang -O2's.

### Shrink-wrapping

Tier 1 gives callee-saved registers only to values live across a call, and
pushes each in the prologue. A register used in the entry's straight run, up to
its first branch, must be saved there on every path. Only one first used after a
branch could be saved later.

IBS op samples of the executables show:
- callee-saved pushes and pops are 3.66% of speedtest1's executed ops;
- those of registers first used after a branch are 1.27% of speedtest1's, and 0%
  to 3.6% of a Lua script's.

That is the ceiling, assuming no path uses them. Each is a single op through the
stack engine.

clang -O2 shrink-wraps, yet its speedtest1 executes more callee-saved pushes and
pops than Tier 1's does (1,046 sampled against 813), and its cycle samples show
the same on four of the five scripts.

Placing saves per path would touch the frame protocol, the unwind tables and the
code certificate, for a bound of about 1% on speedtest1. If it is reconsidered,
`luaD_precall` comes first: its Lua-function path calls nothing, and all five of
its saved registers could move.

The owner decided not to pursue shrink-wrapping now, with profile-guided code
placement ([#1447's comment
6736401463](https://github.com/buster14a/buster/issues/1447#issuecomment-6736401463),
item 4).

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

Tier 1's code in the C driver is the emission's tier-neutral result
(`EmittedCode`, #1056, [the emission metadata guide](emission-metadata.md)),
whose frame events are its frame emitter's own: `tier1_frame_emit` appends,
with the records it writes, each rule change at the boundary after the
instruction that makes it (a push's CFA offset while the CFA is rsp's and its
slot, rbp's pointing, an allocation's CFA offset, a release's, a pop's and the
register restored, rbp's pop's CFA back to rsp + 8, a probe loop's CFA in r11).
`check_program_emit_code` resolves them through the final layout after branch
sizing, and writes the fixed-frame CFI where they spell a fixed frame (the
vector writer and its exact twin, the same bytes as when Tier 1's frame was
published as Tier 0's protocol, which `test-emitted-code` checks for every flag
of the planner) and a row program otherwise. `--verify` proves the events
against the records and their control flow (`emitted_verify_frames`), and where
`unwind_tier0`'s analyzer takes the function, its frame must be the same CFI.
`--tier1` therefore compiles with unwind tables by default, under the driver's
usual options ([the unwind guide](unwind.md)'s "The C driver's unwind tables"),
and with `-g` ([the Tier 1 guide](tier1.md)'s "Debug information").

Tier 1 puts an epilogue before every return's `ret` rather than jumping to one.
Both consumers record each return's `ret` (`Tier1EmitResult.exits`). Each
epilogue's events are bracketed: the body's rules remembered at its first record
and restored after its `ret`, as a tail call's teardown is (#147). Where the
code's last record is its last return's `ret`, the frame is closed, and the
emission drops that epilogue's bracket (`unwind_rows_canonical`); where it isn't
(a loop whose latch's jump comes last, a call that doesn't return, a trap), the
frame is open: its FDE has the prologue's transitions, the body's rules hold to
its end, and each of its returns is a bracketed teardown. A frameless function
has no events: its rules are the CIE's throughout. `check_tier1_compile` checks
the brackets against the recorded exits (each a `ret`, each restoring right
after one, in order). A disagreement is the emitter's own inconsistency: an
internal publication failure (`CHECK_TIER1_PUBLICATION`,
`BUSTER_CHECK_TIER1_FRAME_PUBLICATION`), for which the policy forbids the Tier 0
fallback, so the compile fails ([the outcomes guide](tier1-outcomes.md)). A
frame with probes is described too (its CFA in r11 through the loop), though
the C driver never asks for one. `--tier1-report` counts the shapes: `open_frames` (open frames with
transitions), `return_tails` (returns published as tails) and `red_zones`
(frames whose objects are in the red zone).

`test-check-debug --cross-check` (`c_check_unwind_test.c`) checks it on a
fixture compiled by `--tier1 --verify`, every function but `main` by Tier 1:
a frame of spill slots alone, one with pushes, two returns each after its own
epilogue, an open frame whose loop's latch comes last, a realigned frame (with
its frame pointer), the same open and with two returns, and a leaf whose spills
are in the red zone, whose shapes `--tier1-report`'s counts must show. Each
function's FDE, as `readelf -wF` interprets it, must have its shape: from the
entry's row, a row with the CFA rsp+8 at each return's `ret`, a last row with
another CFA exactly for the open frames (no epilogue's rows), the CFA rbp's in
some row exactly for the frame pointers, and the leaf's one row.
By each path and through GNU ld and lld, `backtrace()` walks through every frame
to `main` (and without unwind tables stops at the first), `--lanes 1` and 8 make
the vector path's bytes, and gdb's backtrace at every instruction of each
function, both returns and the loops' iterations among them, gives the function
and `main`. Tier 1 doesn't make the other
shapes the planner can: its allocator gives a leaf no callee-saved register (they
go only to values live across a call), so a red zone has no pushes above it
(`test-tier1-frame` and `test-tier1-frame-execution` check it). Since #141's
dynamic stack, a function with an allocation, a save or a restore asks for a
dynamic frame (`BUSTER_TIER1_FRAME_DYNAMIC_STACK`): its places are rbp's, or a
realigned one's r15's, and the CFG consumer takes each call's outgoing area from
rsp before the call's stores and gives it back after the call
(`dynamic_outgoing`, `tier1_emit_cfg_call_area`). `test-unwind` checks the writer's
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
including variadic definitions and over-aligned functions, frame-size
comparisons against `clang -O2` on the corpora (with and without the objects'
lifetimes, "Slot sharing between objects"), and controlled
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
The native companion emits records, branch columns and its frame events; the C
driver's emission resolves the events into the final-PC unwind tables ("Unwind
tables" above), and `-g`'s rows for Tier 1's code are at the function's
granularity ([the Tier 1 guide](tier1.md)'s "Debug information").

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
steps. The scalar colorer is the vector one's exact twin, each level's last
ended color in one ordered pass, so both paths give identical slots and offsets
(#2606), which `test-tier1-frame` requires besides its independent interference,
class slot count and frame size checks. Register/stack limits and all displacement bounds
are checked; partial-vector tails never create objects.

Disposition: retain explicit preallocation requirements and direct indexed
spills. `test-tier1-frame` includes independent layout/pressure checks, guarded
tails, failure rollback and input reorderings; `test-tier1-frame-execution`
executes actual ABI preservation, alignment, variadic saves and page probes.
Preparation/sort/color/layout benchmark rows are synthetic. [#177](https://github.com/buster14a/buster/issues/177)/[#140](https://github.com/buster14a/buster/issues/140)/[#133](https://github.com/buster14a/buster/issues/133)/[#88](https://github.com/buster14a/buster/issues/88)
retain actual interval production, full function emission, source/frame events
and whole allocation + frame + code/runtime/memory evidence. Do not call frame
metadata alone an unwind implementation.
