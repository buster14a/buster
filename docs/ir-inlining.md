# Bounded translation-unit inlining

`src/buster/compiler/ir_optimize_inline.c` implements the small-function
inlining slice of [#139](https://github.com/buster14a/buster/issues/139).
It consumes the shared [`IrFunction`](ir.md) representation, SSA's or the
lowering's memory form. The C driver's `--tier1` runs it on each body's
always_inline callees (#1282, [the Tier 1 guide](tier1.md)'s "Inlining");
inlining by size thresholds in the C driver, and its corpus and performance
acceptance, remain pending.

The first scope expanded single-block leaf bodies with no stack objects; #1282
takes a straight chain of blocks, stack objects, SIMD values and machine rows,
and calls whose by-value aggregates' ABI records are the callee's own ("SIMD
callees and by-value vectors", below). It
preserves arbitrary valid caller control flow, including loop phis and switch
tables. A callee is rewritten before its callers, so a small acyclic function
that originally called another small function can become an eligible leaf.
Functions in recursive components remain call targets.

## Producer and API

Include the file after `ir_optimize.c`, which supplies structural validation
and the shared CFG/use/dominator verification helpers. The public entry points
are `ir_inline_options()` and:

```c
IrCheck ir_optimize_inline(const IrInlineUnit* unit, Arena* output,
                          Arena* scratch, IrInlineOptions options,
                          IrInlineResult* result, uint32_t* error_function);
```

`IrInlineUnit` borrows these arrays:

| Field | Contract |
| --- | --- |
| `functions` | Contiguous `IrFunction[count]`, already in valid SSA form. |
| `symbols` | `uint32_t[count]`, strictly increasing, nonzero IDs below `symbol_count`. |
| `flags` | `uint8_t[count]`, explicit internal-linkage, noinline and variadic facts. |
| `symbol_count` | The same exclusive symbol bound used by every body. |

The metadata producer is responsible for marking a definition
`BUSTER_IR_INLINE_INTERNAL` only when its binding is fixed within the unit.
External-linkage definitions, unknown symbols, indirect calls, noinline
definitions and variadic definitions remain calls, and so do calls with ABI
records (#131's `IrCall.abi`: a call that passes or returns an aggregate by
value, or passes one the convention ignores) other than those #1282 inlines:
records of the call's own that match its callee's, argument by argument, with no
hidden pointer and no ignored argument. A row call (#141) is no call site. Unknown flag bits are errors. Every known direct call's variadic
status, and every other one's result type and fixed parameter types/count, must
agree with its table entry, even when expansion is disabled. A call with ABI
records has the convention's shape instead, which only its records give: its
callee takes a hidden pointer as parameter 0, or returns its result's bytes'
address to a void call, so its result and parameters' IR types aren't its
callee's. The existing verifier checks actual argument types and counts.

The table is a shared symbol namespace, so cloned `IR_SYMBOL_ADDR` operations
retain their symbol IDs. No symbol renumbering, ABI change or linker binding
decision occurs in this pass. Inputs and their side tables are immutable and
must provide exactly their declared readable lengths; there is no padding
requirement. Result and optional error-index objects must be valid writable
objects disjoint from each other and all borrowed input storage. Neither
object may intersect the output entry tail or any of scratch. Invalid
error-index storage is rejected before allocation and left untouched.

Output and scratch arenas must be disjoint. Input storage may occupy the
retained prefix of the output arena, but cannot intersect its unused tail or
any of scratch. Every borrowed column is checked against the entry tail
before allocating output. Success publishes a fresh result and restores the
scratch length. Failure restores both entry lengths and preserves `*result`.
The optional `error_function` reports the input function index when known,
or `UINT32_MAX`; on success it is `UINT32_MAX`. Underlying `IrCheck` errors
retain their instruction/detail diagnostic fields.

## Graph, eligibility and budgets

The pass builds direct-call edges from the original bodies, including calls
in unreachable blocks. Iterative Tarjan traversal uses explicit bounded DFS
and component stacks. Self edges and components containing more than one
function identify recursive callees. Component IDs are assigned in
callee-before-caller order; members within each component are processed in
input-table order. Recursive bodies can still receive expansions of eligible
acyclic callees, but a recursive body itself is never expanded into a caller.

The algorithm reference is Tarjan, *Depth-First Search and Linear Graph
Algorithms*, SIAM Journal on Computing 1(2), 146–160, 1972,
[doi:10.1137/0201010](https://doi.org/10.1137/0201010). The implementation and
tests are original. Tarjan traversal is linear in its graph; symbol lookup,
IR verification and body rewriting are additional work.

A candidate's rewritten body must be one straight line of blocks, each but the
last ending in a `br` to the next and the last in `IR_RET` (the `br`s are
dropped), with no labels, no incoming frame of a variadic definition and no
remaining calls but row calls, phis, dynamic stack operations or other
terminators. A typed return adds a `COPY`; a void
return disappears. Each `IR_PARAM` maps directly to the already evaluated
actual argument. All other
operations, including faulting arithmetic, memory accesses and volatile
annotations, remain in their original order. This scope does not splice
callee CFGs, clone stack-object lifetimes or expand recursive/variadic calls.

Each eligible call receives separate `uint32_t` cost and expansion-size
columns. Its structural cost is the number of cloned instructions, including
the result `COPY` and #1282's aggregate copies, plus all copied constant,
address, vector constant, machine row and slot rows. The default
maximum is 32 cost units. This is a provisional size rule, not a measurement
of instruction bytes, latency or profitability.

A masked 16-lane unsigned AVX-512 sweep compares cost against the threshold.
The scalar twin uses the same unsigned comparison. The last batch reads and
writes only active elements. Call-order admission then enforces:

- At most 256 additional instructions per function by default.
- At most 2²² instructions across the input plus admitted growth by default.
- At most 2²⁰ items in each bounded input/output IR column or side table.
- At most 2¹⁶ functions in the unit.

Each call charges `max(expanded_size - 1, 0)`. Shrinking calls do not refund
the budget. This conservative rule makes admission deterministic without
requiring another global selection pass. Options can lower the growth and
unit limits. An input past a representation limit is an error; reaching an
admission budget retains that call with an explicit decision. A call whose
callee's constant or address rows would carry the caller's table past 2²⁰
rows is also retained, as a growth decision, rather than failing the unit. Disabled mode
still validates and clones the unit, retaining every call.

## SIMD callees and by-value vectors (#1282)

The x86 intrinsics' wrappers are SIMD functions: vectors, masks, vector
constants and machine rows, stack objects for their by-value vectors, and a
preheader of their own. The pass verifies every function with the optimizer's
SIMD-taking preparation, and an expansion appends the callee's vector constants,
machine rows and slots to the caller's, rebasing `IR_VCONST`'s index,
`IrCall.row` and `IR_STACK_ADDR`'s and `IR_LIFETIME`'s slot. A cloned row call
gets a fresh descriptor, argument slice and fixed-type slice, as the caller's
kept calls do. A callee's slots have no lifetime markers of their own in the
caller unless they had them: they live throughout it.

A call with ABI records whose records are its callee's own (each argument's
bytes or type, its result's bytes, no hidden pointer, no ignored argument) is
expanded as the convention runs it: each aggregate argument's bytes are copied
into a fresh slot of the caller (address-taken, of its size), whose address the
callee's parameter becomes, since the callee owns its copy; and an aggregate
result's bytes are copied from the address the callee returns into the call's
destination. Where a vector register holds the aggregate whole and
`IrInlineOptions.vector_copies` allows its width (bit 0 16 bytes, bit 1 32, bit
2 64: the target's AVX-512F, and VL for the narrower), a copy is one vector load
and store; otherwise 8-, 4-, 2- and 1-byte integers, each piece past the first
through an `ADDRESS` of each side. Default options copy by integers. The
copies' instructions, addresses and slots are counted in the expansion's size,
cost and growth bounds, and the result is verified as any other.

The copies are memory: SROA with #1282's options ([the field splitting
guide](ir-scalar-replacement.md)) splits every slot whose accesses are whole
vectors into vector slots, which promotion then makes values, so an inlined
wrapper's arguments, parameter objects and result leave no memory access.
Integer copies of a vector the callee reads whole overlap its vector field and
stay memory, correctly. `test-ir-inline` runs a by-value unit on the IR's
interpreter before and after inlining (both copy kinds, both twins), after SROA
and after promotion, which leaves only the parameter's two loads and its store;
and checks a rows fixture's columns (the interpreter has no model of x86
records). The original aggregates fixture runs its expansions with stores and
loads compared by the memory they leave, since an inlined copy reads and writes
an aggregate's bytes otherwise than the interpreted call.

## Rewrite and consumer handoff

The rewrite computes every original value's destination before emitting
instructions. Thus a loop phi can refer to a later backedge definition after
an earlier call expands. Caller block IDs and predecessor identities remain
unchanged; contiguous block offsets reflect the new instruction counts.

The four byte columns and three operand columns retain their shared IR
meanings. Constant/address rows from each expanded body are appended with
rebased indices. Existing caller slots and signature rows are copied, and so
are its labels (its blocks don't change), its ABI records and its incoming
frame. Every remaining call occurrence receives a fresh descriptor, argument
slice and fixed-type slice, with its index into the ABI records, and an
aggregate result's destination (operand `c`) remapped like any value. Phi
slices remap value IDs while preserving predecessor IDs. Switch slices
preserve caller constant IDs and target block IDs.
Rebuilding each occurrence matters because valid source `extra` slices can
overlap while serving different roles.

`IrInlineResult.functions` has the same function order as the input. Consumers
can use it with the original symbol/flag arrays to construct the next unit
view. Rebuild CFG, use, dominance and later machine analyses from these new
bodies; input instruction IDs are invalid for rewritten bodies. The pass
verifies each output with the same reachable-SSA contract as its input and
preserves valid unreachable regions. Entry phis lack an initial-value
contract and are rejected by the shared optimizer preparation helper.

The result also retains an audit CSR indexed by original function:
`call_offsets`, `call_nodes`, `callees`, `costs`, `expanded_sizes` and
`decisions`. Call-node IDs belong to the original bodies. A missing callee
is `UINT32_MAX`; ineligible calls generally retain unknown cost
`UINT32_MAX`. Decisions are authoritative and distinguish disabled,
indirect, unknown symbol, external, noinline, variadic, recursive, ABI records
(`IR_INLINE_ABI`), unsupported body, cost threshold and growth budget cases.
`components` and `recursive` describe the original graph. Counts report
input/output instructions and selected original call sites.

`retained_bytes` is the exact output-arena length increase. `scratch_bytes`
is the greatest retained scratch length observed at this pass's phase
boundaries; it does not account for temporary peaks released inside shared
verification helpers or transient use of the output tail by those helpers.

The handoff is columnar and directly consumable by the existing IR passes.
The graph protocol, indexed operand remapping and growth admission are
currently scalar. Only the cost-threshold sweep has a separate vector path.
Target measurements and further work on column rebasing are required before
claiming the complete data-parallel or throughput acceptance of #139.

## Validation and remaining acceptance

`run_ir_optimize_inline_tests(argc, argv)` takes arguments after the command
name and currently accepts none. `./build/compiler test-ir-inline` runs it,
and the 9700X job's test step runs that command with the other optimizer tests.

Local optimized runs pass **92,932 checks**, including **6,160 independent
interpreter executions** before and after rewriting (59,741 checks, with
ASan/UBSan too, before the side-table growth case below).
The interpreter executes calls using an explicit frame stack, and a call with
ABI records by its records: an aggregate argument's bytes (which its callees
only read), and an aggregate result's bytes, which a stub writes where `c`
points, a callee of the unit returns the address of (`IrFunction.result_size`),
or takes `c` for, as its hidden pointer. Tests cover nested arithmetic through
I128, all typed identity returns through F80 and pointers, empty void calls,
caller diamonds/switches/loop phis, overlapping side-table roles, retained
calls, volatile effects and fault order. A caller
keeps three calls with ABI records (#783) between two expanded ones, which
renumber every later value: to a callee that returns 12 bytes in registers and
takes a 16-byte aggregate, to one that returns 24 bytes through a hidden
pointer and takes the first's result, and to a stub, which takes the second's.
Each destination must follow the renumbering, the records must stay the
caller's and the callees', and the unit must run as it did. Dropping the
destination's remap, either table's copy, or the rule that keeps these calls
apart from the signature check (which refused the whole unit as a type error)
each fails the test. A variadic call of the registers callee, which isn't
variadic, is refused at the call, as a call of another variadic status always is. Separate transitive-closure tests check 64 call graphs,
while guarded pages cover
empty and partial unsigned cost batches through 65 entries. Disabled mode,
exact thresholds, growth budgets (32,769 calls whose constant rows would
pass 2²⁰ among them), malformed metadata and many arena-failure
capacities exercise explicit decisions and transactional rollback. Scalar
and vector outputs must match in every retained body and audit column.

## Reach in `--tier1`

`--tier1` compiles each function alone, so it doesn't run this pass. In #1189's
measurement, clang -O2 makes 25.62 M of the 70.29 M calls that `--tier1`'s
speedtest1 makes into its own code, and 54.20 M of Lua 5.4.8's 91.79 M. The
calls into Tier 1's PLT stubs, the C library's, are left out (7.33 M and
14.21 M): clang's executables make them without stubs. speedtest1's include
1.37 M calls into the unit's atomic load and store helpers, which clang expands
in place; the other 68.92 M go to its functions.

The calls clang doesn't make, a callee's calls by Tier 1 less its calls by
clang, are 44.67 M and 37.60 M. Their callees' instructions in Tier 1 are
estimated at 1,121 M and 960 M: each callee's instructions times the share of
its calls clang doesn't make. A count difference alone doesn't show inlining.
Clang's inlining remarks (`-Rpass=inline`) do, from a rebuild that is byte for
byte the measured executable (the record's `scripts/inlining.sh`):
- **speedtest1:** 43.29 M of the 44.67 M calls go to callees it inlined at one
  call site or more, 1.37 M to the atomic helpers and 0.01 M to callees without
  a remark.
- **Lua:** all but 974 of the 37.60 M go to callees it inlined (`singlestep`,
  `luaS_newlstr` and the start-up code take the 974).

The match is by callee, not by call site.

The callees this scope takes (single-block leaves without stack objects) receive
9.56 M of the 68.92 M calls into speedtest1's functions (13.9%), and 1.86 M of
the 91.79 M into Lua's (2.0%), at 2 to 13 instructions a call. The atomic
helpers are left out of these, as no function of the IR. The hottest callees
clang inlines have several blocks: `index2value`, `luaV_mod`, `prepCallInfo`,
`yy_find_shift_action`, `sqlite3BtreeCursorHasMoved`. A unit-level stage before
the lanes pays only once the scope takes callees with control flow and stack
objects.

[The unit inlining design](tier1-unit-inlining.md) (#2541) is the design for
that scope: callees with control flow and calls of their own, expanded from
summaries built once a unit between the emission's halves, with what a
prototype of the block-level splice measured on the 9700X.

These are local correctness diagnostics. Full source-corpus coverage,
independent native before/after comparisons, translation-unit driver
integration, compile-time and generated-code measurements on the locked
9700X, and profitable default threshold selection remain pending. No target
measurement or end-to-end speedup is claimed by this component.
