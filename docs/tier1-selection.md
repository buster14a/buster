# Tier 1 instruction selection

`tier1_select.c` implements a bounded integer consumer of the actual
[`IrFunction`](ir.md) representation for
[#141](https://github.com/buster14a/buster/issues/141). It plans before register
allocation and emits actual `X86Instruction` records after allocation. It does
not create another source or SSA IR.

The implementation is original project code. Architectural immediate and flag
rules come from the existing [x86 semantics](x86-semantics.md) module; the
[Intel instruction reference](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
was consulted to check comparison, immediate-extension and addressing rules.
No implementation, pattern tables or benchmark harness was copied from another
compiler.

## Supported slice

The current body emitter handles 32-bit and 64-bit integer values, pointers,
canonical `i1` comparisons, and bounded byte/word value transport. The supported
patterns are:

- Constants, register copies, truncation to 8/16/32 bits, zero extension from
  8/16/32 bits or canonical `i1`, and sign extension from 8/16/32 bits to a
  wider supported integer type. Pointer/integer conversions preserve 64 bits.
- 32/64-bit negation, complement, add, subtract, multiply, and/or/xor;
  immediate forms; shifts by a constant
  strictly below the operand width; multiplication by powers of two and by
  3, 5 or 9; add/shift and three-operand addition through `lea`.
- Explicit `IR_ADDRESS` base + index × scale + signed disp32, loads and stores,
  and folding those addresses into memory operands. Byte/word loads zero-extend
  into registers, while stores write exactly one/two bytes. Larger displacements
  return an unsupported result until materialization is implemented. An
  access's address is walked (`tier1_select_address`, #140) through the nodes
  only its pattern uses, at most 8 of them: the `inttoptr` and `ptrtoint` the
  typed stream's integers put around every address, addresses (one after
  another while the operand has room for their one index), constant indices
  and 64-bit additions of constants, which join the displacement while it fits
  32 bits. `s->z[2]` is then one `mov` from `[s + 24]` rather than three
  conversions, two `lea`s and the constants' moves; a pointer that is a value
  (a ptrtoint or an inttoptr of such a chain) is one `lea`. The index sheds the
  constants added to it (`tier1_select_index`): a 64-bit addition of a constant
  on either side, or a subtraction of one, joins the displacement scaled while
  it fits 32 bits, so that `p[i + 100]` of a 64-bit `i` is `[p + i * 4 + 400]`
  and `p[i - 100]` is `[p + i * 4 - 400]`; a subtraction from a constant stays
  a node of its own. A 32-bit `int` index
  keeps its addition, which comes before its sign extension: `(i + c) * s` isn't
  `i * s + c * s` when `i + c` overflows, which the IR doesn't rule out (and
  `-fwrapv` defines). A walk that reaches a frame object's address
  (`IR_STACK_ADDR`) ends there instead (#982): the load, store or address takes the frame's base register in its memory operand,
  with the object's offset plus the walked displacement, which the CFG consumer
  fills in at emission from the frame's layout, as it does the object's `lea`.
  The folded pattern reads no base value, so the object needs no `lea` of its
  own; realigned frames and base-register frames work through the same base and
  offset the `lea` takes. The object may live in the entry block past the
  walk's block: the producer materializes every slot address in the preheader,
  which dominates every use, and the address is pure with a function-global
  offset. Longer cross-block chains stay unfolded. A displacement the object's
  offset pushes past 32 bits refuses emission rather than truncating.
- C's truth values (`tier1_select_truth`, #140): the lowering gives `a < b` as
  `zext (icmp lt a, b)`, which `if`, `!`, `&&` and `||` compare with zero again,
  `icmp ne (zext x), 0` (`eq` for `!`), once for each operator the source
  nests. A branch on such a chain is its comparison's `cmp` and `jcc`, and its
  zero extension to an integer the comparison's `cmp`, `setcc` and `movzx`,
  negated for an odd number of `eq` links, where every link is consecutive and
  used only by the next, so that nothing the pattern doesn't compute runs
  between the comparison and its test. `if (a < b)` had been
  `cmp; setl; movzx; test; setne; movzx; test; jne`.
- Division and remainder by a power of two 2^k: unsigned, a shift right by k or
  a mask of k bits (a mask fitting an imm32); signed, for 1 ≤ k ≤ bits − 2,
  `TIER1_RULE_DIVIDE_POWER`'s sequence, rounded toward zero as C's: the bias
  2^k − 1 where the dividend is negative, from its sign in the scratch register
  (`mov`, `sar`, `shr`), added by a `lea`, then an arithmetic shift, or for the
  remainder a mask and the bias subtracted. A divisor of 1 is a copy (a zero
  remainder).
- Division and remainder by any other constant but those `div` and `idiv` keep
  (zero, and for a signed one −1, which raise C's undefined division by zero and
  `INT_MIN / -1` as the hardware does, and −2^k and the sign bit): a
  multiplication by the divisor's reciprocal and shifts
  (`TIER1_RULE_DIVIDE_RECIPROCAL`), after Granlund and Montgomery's "Division by
  Invariant Integers using Multiplication" (PLDI 1994), whose CHOOSE_MULTIPLIER
  gives the multiplier and its shift (`tier1_select_reciprocal`). For N-bit words:
  - unsigned, the high N bits of the dividend times the multiplier, shifted right.
    Where the multiplier needs N + 1 bits, an even divisor's dividend is shifted
    right by the divisor's zero bits first, which leaves its odd part's multiplier
    N bits, and an odd one's quotient is `(((n − t) >> 1) + t) >> (s − 1)`, t the
    high product of its low N bits;
  - signed, by the divisor's magnitude, the signed high product, plus the dividend
    where the multiplier is 2^(N−1) or more, shifted right arithmetically, plus its
    own sign bit, which makes the floor C's rounding toward zero; negated for a
    negative divisor;
  - a remainder, the dividend less the quotient times the divisor (its magnitude
    for a signed one, whose quotient then stays unnegated).

  The high product is `mul`'s or `imul`'s `rdx`, so the rule takes x86's fixed
  registers as `TIER1_RULE_DIVIDE` does: `tier1_select_fixed` writes its records
  (at most `BUSTER_TIER1_SELECT_FIXED_RECORDS`, 13: a remainder whose multiplier
  needs the add sequence, whose divisor is past an imm32 and whose dividend is in
  `rax` or `rdx`), reading the dividend before writing `rax` or `rdx`, through the
  scratch register where it is read after the multiplication, and the destination
  last; each consumer saves the other values that live past the node in `rax` and
  `rdx` around them (`tier1_emit_fixed`). Every other division stays
  `TIER1_RULE_DIVIDE`.
- A nonvolatile load whose only use is an arithmetic consumer in its block
  can become that consumer's memory operand (the right operand, or a
  commutative operation's left where its right isn't a load), its access moved
  to the consumer (#141). It moves past the nodes between them that neither write
  memory, call nor access a volatile object (arithmetic, constants, addresses,
  other nonvolatile loads) and past none that do: a store to any address, a
  call, a volatile load or store. A pass of its own after the plan's pass over
  the nodes keeps each one's last such barrier (`Tier1SelectForest.barriers`),
  a running maximum 16 nodes at a time (`tier1_select_barriers`, its scalar
  twin `tier1_select_barriers_scalar`), so the check is a comparison. Inside the
  plan's pass, its store made the operands' walk reload the function's columns
  (the build's `-fno-strict-aliasing`): 40 instructions more per node. The IR has no atomic access of its own: C's atomic operations
  reach it as calls of the unit's leaves. A volatile load remains a separate
  access; a store retains its original order and access width.
- Signed/unsigned integer comparisons, comparisons with zero through the shared
  flags-aware form chooser, comparison/branch fusion, simple selects using
  `cmov`, unconditional branches, returns and `IR_UNREACHABLE` as `ud2`.

`I8` and `I16` values occupy canonical zero-extended 32-bit registers and
four-byte spill slots. Their logical memory widths remain one/two bytes.
Signed extension interprets the source sign bit, then normalizes a narrow
destination again; for example, `I8(-1)` to `I16` has register bits `0xffff`.
Narrow arithmetic/comparisons and byte/word parameter, return and call ABI
forms remain unsupported. `i1` memory and signed-extension forms are also
declined explicitly.

Comparisons used by a branch alone do not produce `setcc`. Their comparison and
conditional jump are consecutive records. A comparison producing a value uses
`cmp`/`test`, `setcc`, then `movzx`; clearing the destination before comparison
would corrupt an aliased input, so the zero extension follows `setcc`.

The body uses only RAX, RCX, RDX, RSI, RDI and R8–R10. R11 is reserved for
operand-overlap repairs. Parameters must be an entry-block prefix, with up to
six integer/pointer parameters in the System V registers RDI, RSI, RDX, RCX,
R8 and R9. Their intervals start at function entry, because parameter nodes
emit no late copies. This emitter has no frame or callee-saved-register changes.

Calls, phis, switches, floating values, byte/word arithmetic, dynamic shifts,
division/remainder, stack/symbol addresses, spill fragments and rematerialized
fragments are explicit unsupported/allocation results in `tier1_select_emit`:
it has neither call preservation nor the edge moves a phi needs.
No semantic error silently falls back to another tier. The separate
`tier1_emit.c` consumer described below handles allocated spill fragments
without weakening that API's checks.

## Producer, plan and allocation handoff

`tier1_select_plan(function, dominance, output, scratch, vector, result,
error_node)` returns `Tier1SelectError`. Both the vector and scalar paths invoke
their corresponding IR verifier. A cross-block value use requires the supplied
dominator intervals; structural verification alone is insufficient.
`tier1_select_plan_verified`, with the same arguments, gives the same plan
without the verification: it takes a function its caller has verified with
those dominator intervals, as the C driver's `--tier1` takes `ir_optimize`'s
result, which `ir_optimize` verifies before it returns (`c_check_tier1.c`). On
the frozen `sqlite3.c`, the verification was a quarter of selection's cycles
(#141). The selector's tests plan their functions both ways, and the pipeline
test plans each optimized function as the driver does and checks it against the
verifying plan, field by field. This slice
requires value definitions to precede uses in physical instruction order, but
for a phi's operands, which may follow it (a loop's back edge). An integer phi
selects `TIER1_RULE_PHI` (#140): no records and no effective uses, since its
operands are uses on its block's incoming edges, which the CFG consumer's edge
moves read (`tier1_emit_cfg.c`, in the [pipeline guide](tier1-pipeline.md)).

The borrowed function remains immutable and alive until emission finishes.
Input, result and scratch storage must not overlap unused arena capacity;
output and scratch are separate arenas. A plan can precede other retained
objects in the same output arena. Failure restores output and scratch lengths
and leaves the result view unchanged. `error_node` identifies a node when
available. There is a bound of 2^20 input instructions, before multiplication
or allocation; counts and capacities cannot silently wrap.

`Tier1SelectPlan` retains:

| Field | Producer/consumer contract |
| --- | --- |
| `function` | Borrowed, original `IrFunction`, preserving node and block IDs |
| `patterns[n]` | Compact selected-rule metadata, consumed together by the emitter |
| `covered[n]` | Byte flag: a covered node emits no instruction and has no allocated definition |
| `effective_use_offsets[n+1]` | u32 CSR offsets by original node |
| `effective_use_values[effective_use_count]` | u32 value IDs actually read by that node's chosen pattern |
| `costs[n]` | u32 bottom-up subtree costs under the structural cost model |
| `estimated_cost` | Sum of intrinsic costs of uncovered nodes |
| `arena_bytes` | Exact retained arena charge, including alignment and reserved output capacity |

Every uncovered nonvoid node defines its original value ID. Effective reads
occur at machine position `2*i`, definitions at `2*i+1`; incoming parameters
start at zero. Duplicate source operands remain duplicate uses. Covered nodes
have empty use slices. The liveness adapter consumes these columns directly,
then the allocator returns locations for those same IDs.

**Folding extends lifetimes.** For example, folding a shifted index and address
into a later load makes the original base and index live until that load. It is
incorrect to allocate using the original IR's use list and fold afterwards.
The memory regression fixture checks that the effective uses are the base,
index and arithmetic source, with the folded load/address definitions absent.

## Matching and cost comparison

The planner counts original uses and forms single-use expression trees within
each block. Dependencies produce depth columns, then a count/prefix/fill pass
groups ready nodes by depth. All nodes in a depth are ready before the next
depth begins. Candidate generation checks the bounded C patterns and their
operand, type, immediate, memory-order and single-use constraints.

The vector path chooses the cheapest candidate for sixteen independent ready
nodes at a time, with unsigned comparisons over contiguous cost columns. The
final batch is masked. A reverse node pass then marks what the code computes:
the nodes with an effect (stores, calls, terminators, volatile loads), the
parameters, the phis and their operands, the nodes nothing uses (an unused load
or division the optimizer kept for the fault it may raise), and each value a
marked node's chosen pattern reads. Every other node is covered, its users'
patterns computing it themselves: a constant as an immediate, a load as a
memory operand, an address as a displacement, a truth value's chain as a
comparison, however deep their walks went (#140). A pattern's `fold_count` is
only maximal munch's rank. An iterative pass publishes the effective-use CSR.
There is no recursive tree walk or speculative input load.

The scalar twin uses a switch to rank maximal-munch patterns by coverage and
rule priority. It deliberately does not call the vector minimum-cost chooser.
For example, it can prefer a power-of-two multiply's shift while the costed
cover prefers the single-record immediate `imul`. Tests compare executed
results and require the vector cover's cost to be no higher. A division by a
register ranks below every rule for a constant divisor, which covers the constant
even where the constant has other uses and so doesn't count among the folds: both
paths take a constant divisor's rule wherever there is one, as the costed cover,
whose `TIER1_RULE_DIVIDE` costs 8 against the others' 1, 2, 5 and 7, always did.

The current cost is a **structural upper bound on record count**, including
possible copies and operand-overlap repairs. It is not a measured latency,
throughput, encoded-byte count, or guarantee about final runtime. Allocation
can remove copies from either path. The measured Zen 5 model is not complete
enough to replace this bound for every operand case. One rule departs from it: a
division by a constant's reciprocal costs 7, below `div`'s 8, though its records
(up to 13) may outnumber `div`'s, since `div`'s and `idiv`'s latency, which no
count of records sees, is the larger.

Candidate generation, depth construction, top-down covering and metadata
emission still use scalar loops. This is a working bounded bottom-up selector,
not the full startup-compiled pattern-state automaton proposed by #141. The
benchmark exposes the difference between a narrow dependency chain and a wide
ready frontier. No scalar-stage performance exemption is claimed before the
approved-host evidence and representative source workloads exist.

## Records and downstream consumers

Call `x86_encoder_initialize`, `x86_semantics_initialize` and
`x86_zen5_initialize` before selection/emission tests. Build the permitted-form
column with `x86_target_forms` or its scalar twin. A zero-feature
`X86_ARCH_BASELINE` target is independent of the compiler host's native ISA.

`tier1_select_emit(plan, value_registers, allowed_forms, output, result,
error_node)` consumes the allocator's direct byte register column. A value
without one stable supported register is rejected. The caller supplies an
allocation verified against the selected-use liveness; this API does not
reconstruct or independently solve allocation.

The immediates' forms are the shared form choice's (#221). Expansion leaves
each record with an immediate empty, with its request (the operation's row,
the value, the live flags and the destination); both emitters collect a
function's requests and choose their forms 32 at a time, one pass of
`x86_choose_forms`'s lanes, before checking the records; on the scalar path
(the plan's, `Tier1SelectPlan.vector`, for this emitter; the options' for
`tier1_emit_function`) each request's form is its scalar twin's, chosen as
the request comes, and none is held (#141). This emitter
checks them in order after the batch, so a node's invalid record or a form
the target refuses is reported before a later node's allocation failure, as
when each node's records were checked as it was selected. Every record either
emitter writes has a form `x86_semantics.c` covers, which the selector, emitter
and pipeline tests check.

`Tier1SelectCode` publishes:

- `records[count]`, the actual encoder records, validated for operand legality
  and generated-target eligibility;
- `cost_ids[count]`, each record's exact `x86_zen5_id`, with zero preserved as
  unknown, and an explicit `unknown_costs` count;
- `node_first[n+1]`, monotonically increasing record boundaries for original
  nodes, including empty ranges for covered nodes;
- ascending `branches[branch_count]` and corresponding `targets`, in record
  indices. Branches are rel8 with displacement zero, directly accepted by
  `branch_sizing_validate` and `size_branches`.

The output reserves at most six records per original node and two branches per
block. Its logical lengths are the returned counts, not the reserved capacity.
Every record a rule's expansion writes goes through `tier1_select_append`, which
checks it against the node's room (`BUSTER_TIER1_SELECT_RECORDS_PER_NODE`): a
sequence longer than the room writes nothing past it and marks the node's records
overflowed, which the three consumers refuse as an invalid function. The
signed power-of-two remainder's six records are the longest, the room's whole.
The tests take a node's sequence to its room and past it with no-ops before its
records (`Tier1SelectPattern.nops`, which the selector never sets):
`test-tier1-select` through the selector's emitter and `test-tier1-emit` through
both consumers, a conditional branch's sequence among them. At the room the
function emits and runs; one record past it, it fails at the node with its
arenas and result as they were.
Input columns and output records require no padding. Only the encoder's own
documented output buffer capacity/padding contract applies when encoding them.
All retained arrays belong to the caller's arena. Emission failure restores its
length and leaves the result view unchanged. Empty functions accept a null
register column and emit no records.

Any later scheduler must preserve node/branch metadata and compare/branch
adjacency, and run before branch sizing. This slice produces no symbolic
references, calls, frame metadata or unwind information.

## Fragment and frame consumer

`tier1_emit_function(plan, options, allowed_forms, output, scratch, result,
error_node)` connects the selected plan to actual native spill code. Its options
choose scalar/vector producer paths, integer-constant rematerialization,
a subset of the selector's caller-saved register mask, and the fixed-frame
KEEP_POINTER, NO_RED_ZONE and PROBE_STACK policies. The default mask is available
through `tier1_liveness_register_mask()`; an explicit subset can test pressure.

The supported control flow is a straight-line physical chain. Every block
before the last must unconditionally branch to the immediately following block.
The last returns, ends in `IR_UNREACHABLE`, or contains exactly one branch to
itself. Forward identity jumps emit no record. The empty terminal sink emits
a real self-jump targeting its block after the prologue; a preceding call is
not assumed to terminate. Terminal paths emit no return epilogue. This admits
the typed lowerer's parameter preheader and terminal suffix. Every other
function, with general joins, nonempty backedges and phis, goes through the CFG
consumer, `tier1_emit_cfg.c` (#140), which allocates over the CFG
(`tier1_cfg.c`) and moves values along its edges.

A division by a constant's reciprocal takes `rax` and `rdx` in either consumer,
through one helper (`tier1_emit_fixed`): `tier1_select_fixed`'s records, with the
other values that live past the node in those registers stored in call-save rows
before them and loaded after. The straight-line consumer gives the registers rows
(`tier1_emit_prepare`) and finds those values by their intervals' ends, as it
finds a call's; the CFG consumer by its intervals at the node's definition phase.
A division and a shift by a register keep their functions on the CFG consumer, as
before the helper (`tier1_emit_cfg_chain`).

Direct nonvariadic calls support up to six `I32`, `I64` or pointer arguments
and a void, `I32`, `I64` or pointer result. The call pattern keeps the symbol
ID in `immediate`; its three ordinary value holes are empty. Its complete
ordered argument list is the effective-use CSR slice, including duplicates.
Arguments are evaluated by their existing IR definitions exactly once. Calls
remain ordered barriers, and no load is folded across one.

The native consumer captures argument locations before definition-phase
evictions, saves surviving caller registers in dedicated fixed-frame rows,
resolves parallel argument moves using reserved R11, and emits the call. All
argument shuffles use full-width copies before narrowing `I32` destinations,
so mixed-width cycles preserve their original sources. The return value moves
from RAX before live registers are restored; `I32` returns use a 32-bit move
even when the destination is RAX. Existing allocation keeps the result's
destination distinct from the saved survivors. Call preservation counts are
reported separately as `call_stores` and `call_loads`.

Call-containing functions disable the red zone and use the frame planner's
System V call alignment, including zero-argument calls. The bounded register
save rows cannot share storage with ordinary spill homes. Leaf functions keep
their existing frame policy. Argument moves are a dependency-constrained
operation over at most six registers; the existing scalar/vector liveness,
allocation and frame producers still consume the complete function.

`Tier1EmitResult.references[reference_count]` contains ordinary
`LinkReference` entries: the call instruction index, displacement field,
`BUSTER_LINK_FIXUP_CALL`, the original symbol ID and zero source addend.
The corresponding records carry `BUSTER_X86_FORM_FIXUP`. After branch sizing
and encoding, publish them through `link_add_code_fixups` with the encoder's
instruction ends. The references retain instruction indices, not guessed byte
offsets. The older frameless `tier1_select_emit` API still rejects call plans.

Indirect and variadic calls, stack arguments, and floating-point and aggregate
arguments/results are outside this bounded path. Calls in general CFGs, loops
among them, go through the CFG consumer, which saves the registers of values
live past each call. Neither establishes acceptance of source functions.
Unsupported calls and allocation/target/capacity errors publish no partial
result. Correct execution of these component fixtures does not establish
atomic-builtin, full-source ABI or corpus acceptance.

The complete producer/consumer sequence is:

1. Build liveness from the plan's effective uses. Add one required register
   position at each definition and mark every effective source use required.
   Parameters still begin at position zero in their fixed incoming registers.
2. Build integer-constant recipes from actual selected definitions and obtain
   rematerialization marks. Prequery prospective spill-frame requirements, then
   exclude their reserved registers and R11 before allocation.
3. Allocate intervals and plan frame objects for exactly `allocation.spill_values`.
   Object widths are 4 or 8 bytes, with conservative whole-value lifetimes.
   Stack slots use `frame.base_register + frame.offset[spill_id]`.
4. Emit the frame prologue. At each machine phase, emit all register-to-spill
   stores before spill reloads or rematerialized copies reuse any register.
   Capture source registers at `2*i`, then perform definition-phase evictions
   at `2*i+1` before expanding the selected instruction. Destructive destination
   aliases use the existing selector's repairs.
5. Emit the frame epilogue before the final return. Validate every record and
   target form, and retain exact operand-case cost IDs. The returned branch
   columns include frame probe branches, ready for branch sizing and encoding.

Required definition positions mean initial definitions always have registers;
this consumer does not pretend an initially spilled definition was emitted.
After eviction a certified constant can be recomputed without a frame slot.
Every copy conservatively treats flags as live, so zero uses a move. Symbol,
TLS and stack-address recipes are not manufactured by this adapter.
Direct register-to-different-register transitions and odd-phase reloads are
explicit errors; the current required-use allocator does not produce them.

`Tier1EmitResult` retains the actual liveness input, required-location columns,
allocation fragments, frame, records and spill/reload/rematerialization counts.
All failure paths restore both arenas and preserve the result object. Transition
events use count/prefix/fill CSR over bounded machine positions. Their ordered
stores and copies are scalar emission work; the existing liveness, allocation,
frame and rematerialization producers retain their vector/scalar paths. No
compile-time speedup is claimed for adding this consumer.

## AVX-512's masks (#179)

The IR's masks, `m8` to `m64` (#732), are the opmask class's, which the CFG
consumer allocates (the [allocation guide](tier1-allocation.md)'s "The opmask
class"). The selector takes a function with masks and refuses one with vectors
(`tier1_select_has_vectors`: a vector type, constant or operation in its
instructions, parameters, calls or slots), which are #141's: a mask's operations
need no vector register. Its mask rules, each of the mask's width, 1 to 8 bytes:

| IR | Rule | Records |
| --- | --- | --- |
| `const` (and `undef`, any mask: zero) | `TIER1_RULE_MASK_CONSTANT` | `kxor k, k, k` for zero, `kxnor k, k, k` for every bit, else `mov r11d, imm32` (or `movabs r11`) and `kmov k, r11` |
| `copy` | `TIER1_RULE_MASK_COPY` | `kmov k, k`, none where allocation gave both one register |
| `and`, `or`, `xor` | `TIER1_RULE_MASK_LOGIC` | `kand`, `kor`, `kxor` `k, k, k` |
| `not` | `TIER1_RULE_MASK_NOT` | `knot k, k` |
| `load`, `store` | `TIER1_RULE_MASK_LOAD`, `_STORE`, and their `_ADDRESS` folds | `kmov k, m` and `kmov m, k`, the address folded as an integer access's, a frame object's among them |
| `phi` | `TIER1_RULE_PHI` | none: its operands are its edges' moves |

The opmask instructions of a width leave the bits past it zero, as the IR's masks
have them, so no record extends a mask. A node's register is its allocation's
location, class 2's `64 + k`, whose low three bits the expansion takes as k. A
mask's `select`, `vcmp` and `vselect` have no rule and are refused at their node:
`cmov` has no opmask form, and the comparisons and selection need vectors.

Only the CFG consumer takes the rules (`tier1_emit_cfg.c`): the straight-line
consumer sends any function with a mask there, and the uniform-register
`tier1_select_emit` refuses them. There a mask's value is class 2's, allowed k1
to k7, with k0 the class's scratch. Its spills, reloads, edge and split moves are
`kmov` of the mask's width (`tier1_select_mask_forms`): to and from its slot,
whose size is the mask's bytes, and between opmask registers; a cycle goes through
k0, and a slot-to-slot move through k0 too, in two moves, as `tier1_cfg.c` stages
them. No opmask register survives a call, so a call saves every mask live past it
in a frame row of its register's (`Tier1EmitPrepared.mask_save_mask`, the
registers some mask holds across some call) and reloads it after, by `kmov` of
the mask's width; a mask never takes a callee-saved general register. The rows
come after the general registers' call-save rows, then the general class's
temporary slot and the opmask class's, then the frame objects. The records' forms
are the target's to allow, as every record's: `kmovw` and the word forms need
AVX512F, the byte forms AVX512DQ, the doubleword and quadword ones AVX512BW, and
a target without them is refused at emission (`TIER1_EMIT_TARGET`). A width no
mask has gets no form at all (`X86_FORM_NONE`, which no target allows), so it is
refused the same way rather than taking another width's forms.

A mask load's or store's `kmov` is its node's only record, so its last, which the
CFG consumer's frame-object patch rewrites with the frame's base and the
object's offset (`stack_object`, #996). No mask is recomputed (#178): the
masks' rules are none of the recipes' (`tier1_emit_cfg_recipes`), which skip a
mask and any chain through one, and no mask value is marked for the allocator,
whose dead definitions read the marks. A mask recipe would need its own `kxor`,
`kxnor` or `kmov` forms. A division's fixed registers (#141's `rax` and `rdx`)
don't touch a mask, which keeps its opmask register across the sequence; their
saves take the general call-save rows, before the masks' own.

`test-tier1-masks [--seed N] [--print]` (`tier1_masks_test.c`) takes
hand-written IR of masks through this path: each fixture is a function of a
pointer to a 256-byte buffer, and of a count where it loops, that loads masks of
every width from the buffer's first half and stores masks into its second half
(the IR has no mask argument or result, and no conversion between a mask and an
integer). Each is parsed by both of `ir_parse.c`'s paths into the same function,
planned and emitted by both of Tier 1's paths for x86-64-v4, its allocation
accepted by `tier1_cfg_verify`, its records by the target's allow-list and
`x86_semantics.c`'s coverage, and encoded by both encoders into the same bytes.
On a host with AVX512F, BW and DQ, each runs in a child on 16 random buffers (a
quarter all zeros, a quarter all ones), whose bytes after the run, the ones no
store writes among them, must be the shared IR interpreter's. The fixtures:

- `and`, `or`, `xor`, `not` and copies at the four widths;
- constants: zero and every bit at each width, and others through r11;
- a loop whose two mask phis swap every iteration, a cycle through k0, beside a
  mask accumulated from the buffer by a loaded index;
- ten masks live at once, more than k1 to k7 hold, spilled and reloaded at their
  widths;
- masks live across two calls, and a mask phi across a call in a loop, of a helper
  that sets every opmask register (`kxnorq` into k0 to k7) before it returns its
  argument plus one: generated code beside the function, where the interpreter
  calls a C function that returns the same;
- masks of every width stored into a frame object and loaded back, each access's
  object folded into its `kmov`'s memory operand, the frame's base at the object's
  offset, as an integer access's is (`stack_object`): the test checks each such
  node's `kmov` there;
- masks live across a division by a register, one by a constant's reciprocal and
  a call, beside eight 64-bit constants live across all three, too wide for an
  immediate.

Each fixture runs with rematerialization (as the C driver's `--tier1` compiles)
and without, and in each no mask may have a recipe or be marked to be recomputed;
with it, some of the general values beside the masks must be recomputed, which
the division fixture's constants are.

And the refusals, on both paths: a vector at selection, a mask's `select` at
selection, and at emission the baseline target (no opmask form) and AVX512F alone
for the four widths (no `kandb`, `kmovd` or `kmovq`), where a function of word
masks alone is taken; and each width's `kmov` forms, none for a width no mask
has. `--print` prints each fixture's records by the vector path, as GNU as reads
them.

## Tests and benchmark

`test-tier1-select` checks both original-IR covers, real records and native
execution in a child process. Its independent arithmetic/IR oracles cover
constant boundaries, aliasing registers, shifts, signed/unsigned comparisons,
selects, addresses, exact memory access widths, volatile loads, intervening
stores and multiply-used loads. Failure tests distinguish invalid IR,
unsupported operations, missing allocation, target exclusion and arena
exhaustion. Empty functions and guarded, unpadded columns cover lengths around
16 and 32 lanes; wide frontiers and dependency chains cover distinct groupings.

`test-tier1-select --manifest` writes CSV with separate origins for actual
`plan` emission, direct `expansion` fixtures and test `wrapper` records. Every
record includes form, mnemonic, all memory/register/immediate fields, model ID
and each cost column. Summaries report unknown IDs and missing fields for each
origin separately; UINT16_MAX denotes a missing field. Summaries go to stderr.
This observed emitter inventory is generated independently of the model's case
table. Unknown IDs stay unknown. Its coverage
is only the bounded generated fixtures, not a claim of complete Tier 1 emitter
or corpus coverage for [#181](https://github.com/buster14a/buster/issues/181).
In particular, the plan-origin inventory currently lacks compare, select and
conditional-branch families covered by direct expansion tests. Expansion rows
do not establish coverage of those complete planner paths.

`test-tier1-emit` executes real pressure plans through liveness, allocation,
frame planning, fragment emission, branch sizing and encoding. Original
32/64-bit fixtures keep up to 600 values live with only three allocatable
registers. Both paths run against an independent integer oracle with stored
values and with rematerialized constants, fixed/red-zone/frame-pointer frames,
and stack probes. Independent allocation verification checks the produced
fragments. Failures cover target denial, scratch-register conflicts, capacity
and unsupported frame policies; a backedge through the entry now goes through
the CFG consumer, after an added entry block. Narrow-value fixtures
check canonical upper bits, exact byte/word accesses beside guard pages,
sign boundaries, real spills and rematerialization. Terminal fixtures check
explicit traps and empty self-loop sinks in bounded child processes, including
a returning helper whose sink must remain a loop. Empty input is accepted.
The separate pipeline tests cover upstream typed-stream lowering and SSA
promotion; neither test suite is the pending full C-program differential corpus.

Divisions by constants' reciprocals are tested at both levels. `test-tier1-select`
plans each of the four divisions at both widths by 52 divisors (small ones, C
programs' usual ones, the ends of 32 and 64 bits, negative ones, and those `div`,
`idiv` and the power rules keep), by both paths, which must pick the reciprocal
exactly where `tier1_select_reciprocal_divisor` does; runs each reciprocal's
records natively from four dividend registers (`rax` and `rdx` among them) into
five destinations (the dividend's own among them) on the dividend's edges, against
its interpreter; checks that the longest sequence fills the room exactly; and that
a divisor whose constant has two uses still takes its rule by both paths.
`test-tier1-emit` (`tier1_divide_test.c`) runs whole functions through
`tier1_emit_function` by both consumers: a straight-line one whose dividend and a
parameter that lives past both divisions arrive in `rdx`, and a loop's digit sum,
by both paths, with constants rematerialized and not, and with every register and
a few, against C's own division; both consumers must save values around the
sequences.

`benchmark-tier1-select [--steps N] [--functions N] [--only scalar|vector]
[--after-scalar MS]` freezes original synthetic IR with a deterministic seed.
Its chain, wide-frontier and live-pressure shapes report selection separately
from the complete backend path: plan, selected-use liveness, required register
positions, allocation, rematerialization recipes, frame planning, fragment and
frame emission, branch sizing and encoding. Scalar rows use the scalar encoder.
Both paths are
validated before timing. Samples alternate order, and report seven-sample
medians of nanoseconds and invariant TSC ticks per IR instruction. TSC ticks
are **not core cycles**. The output also reports actual record/code-byte counts,
unknown model IDs, spill slots, stores, reloads, rematerialized copies, peak
retained arena bytes and the maximum of liveness and transition scratch bounds.
The latter is not a whole-pipeline peak-memory measurement.

`benchmark-tier1-select-source [--only select|select-scalar] [--after-scalar MS]
[preprocessor options] file...` (`tier1_select_source_benchmark.c`, #141) times
selection on real C: every function `-c` compiles of the files, lowered, promoted
and optimized as the C driver's `--tier1` takes it, with its dominance, outside
the time; then `tier1_select_plan_verified` of each, as the driver plans it,
by both paths, in 21 passes that alternate which path runs first, as nanoseconds,
core cycles, instructions and the throttle (PMCx1A0's umask 0x08, #199) per IR
instruction and cycles per typed entry. A function a step before selection
refuses, or either path's selection refuses, is left out of both and counted;
any other failure fails the command, which then reports no time. `--only` times
one path alone, and `--after-scalar MS` runs the scalar path before each of the
vector path's passes. Of the frozen `sqlite3.c`'s 2,668 functions, both paths
select the 2,181 that the driver's `--tier1-report` gives Tier 1 (244,036 IR
instructions).

Run each build, test and measurement on the approved 9700X under its shared
`~/buster-fast-bench/exclusive.sh` lock, with owner/title/link metadata; pin each
benchmark to CPU 2. Use separate fresh processes for sustained `--only vector`
and `--only vector --after-scalar 200`, and compare both against scalar runs.
Local runs are diagnostic only. The current slice supplies the reproducible
measurement path; it does not establish a performance gain.

#141's batched choice and load fold, against main `0dd6fbc3` over six
placements of the benchmark's unit
([the record](https://github.com/buster14a/buster-performance/blob/6441e0df565d3c21ee4413848f75ce2a3927a7d3/docs/measurements/tier1-select-141-2026-10-29/README.md)),
each placement against each of main's as `docs/profiling.md` compares builds:
the plan's medians 1.5% to 2.5% lower on every shape and path, outside the
comparison's floor on four rows (the vector path's three shapes and `wide`'s
scalar twin); the whole pipeline's 0.3% to 2.1% lower, within the floor (one
placement of the head's executable misses its op cache). The fold's barriers
kept inside the plan's pass over the nodes had made the plan up to 10% slower,
37 to 42 instructions more per node, which their pass of their own doesn't
cost. The shapes hold no loads, so these are the costs, not the fold's gains.

## Remaining acceptance

Issue #141 remains open. The 9700X measurements its Done-when asks for are
recorded in [buster-performance](https://github.com/buster14a/buster-performance/tree/70a6b824ba7917e62968b0baabed847dd7d9fd0c/docs/measurements/tier1-reciprocal-141-2026-11-01), at #1001's
heads and at its merge with main, `cc3387f`:

- **Programs**, by the driver's `--tier1` (each function Tier 1's pipeline
  takes by Tier 1, the others by Tier 0), against Tier 0, clang -O0 and clang
  -O2: speedtest1 in 223.5 ms against 256.8, 291.0 and 131.8; Lua 5.4.8's
  interpreter, five scripts, in 286.1 ms against 337.6, 376.0 and 167.1;
  c-testsuite's one long program (00040) in 23.6 ms against 25.6, 33.2 and
  12.6. In user-space instructions, `--tier1` runs 4,413 M of speedtest1
  against Tier 0's 5,694 M and clang -O2's 2,010 M, and 8,749 M of Lua's
  scripts against 10,763 M and 4,281 M. Each program's hottest function
  (`sqlite3VdbeExec`, `luaV_execute`) is still Tier 0's.
- **Selection's cycles per IR instruction**, `benchmark-tier1-select-source` on
  the frozen `sqlite3.c`: 243 by the vector path and 253 by the scalar twin,
  since the plan no longer verifies the optimizer's verified result
  ([record](https://github.com/buster14a/buster-performance/tree/0aa27457a95dd2ee4e9b5ac26e67429c2490380b/docs/measurements/tier1-plan-verification-141-2026-11-01));
  with that verification they were 338 and 340.
- **#142**: 240 Csmith and YARPGen programs at `e0ae799c` and `cc3387f`, with
  `--tier1` and with `--tier1 --scalar`: 195 pass at both tiers, and the 45
  others fail in clang's own steps (links past the small code model, which
  Buster refuses too, compiles past the output limit, runs past 30 s), none in
  Buster's.

Full pattern-state generation, the remaining scalar/ABI/spill consumers, and
whole-C integration (the functions selection refuses whole: labels as values,
switches, aggregates by value and variadic definitions among them) are
outstanding.
