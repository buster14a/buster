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
  return an unsupported result until materialization is implemented.
- A nonvolatile load whose only use is an arithmetic consumer in its block
  can become that consumer's memory operand (the right operand of a
  subtraction, either of a commutative operation), its access moved to the
  consumer (#141). It moves past the nodes between them that neither write
  memory, call nor access a volatile object (arithmetic, constants, addresses,
  other nonvolatile loads) and past none that do: a store to any address, a
  call, a volatile load or store. The plan's pass over the nodes keeps each
  one's last such barrier (`Tier1SelectForest.barriers`), so the check is a
  comparison. The IR has no atomic access of its own: C's atomic operations
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
fragments are explicit unsupported/allocation results in `tier1_select_emit`.
No semantic error silently falls back to another tier. The separate
`tier1_emit.c` consumer described below handles allocated spill fragments
without weakening that API's checks.

## Producer, plan and allocation handoff

`tier1_select_plan(function, dominance, output, scratch, vector, result,
error_node)` returns `Tier1SelectError`. Both the vector and scalar paths invoke
their corresponding IR verifier. A cross-block value use requires the supplied
dominator intervals; structural verification alone is insufficient. This slice
requires value definitions to precede uses in physical instruction order and
does not accept phis.

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
final batch is masked. A reverse node pass selects the cover and suppresses
only the exact nodes consumed by its rules; an iterative pass publishes the
effective-use CSR. There is no recursive tree walk or speculative input load.

The scalar twin uses a switch to rank maximal-munch patterns by coverage and
rule priority. It deliberately does not call the vector minimum-cost chooser.
For example, it can prefer a power-of-two multiply's shift while the costed
cover prefers the single-record immediate `imul`. Tests compare executed
results and require the vector cover's cost to be no higher.

The current cost is a **structural upper bound on record count**, including
possible copies and operand-overlap repairs. It is not a measured latency,
throughput, encoded-byte count, or guarantee about final runtime. Allocation
can remove copies from either path. The measured Zen 5 model is not complete
enough to replace this bound for every operand case.

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
`x86_choose_forms`'s lanes, or by its scalar twin on the scalar path (the
plan's, `Tier1SelectPlan.vector`, for this emitter; the options' for
`tier1_emit_function`), before checking the records (#141). This emitter
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
the typed lowerer's parameter preheader and terminal suffix while general
joins, nonempty backedges and phi moves still need an edge-location consumer.

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

Indirect and variadic calls, stack arguments, floating-point and aggregate
arguments/results, and general terminal-call CFGs are outside this bounded path.
The supported empty terminal sink does not establish acceptance of source
functions with conditional branches or unreachable continuation blocks.
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
fragments. Failures cover target denial, scratch-register conflicts, capacity,
unsupported frame policies and general CFG backedges. Narrow-value fixtures
check canonical upper bits, exact byte/word accesses beside guard pages,
sign boundaries, real spills and rematerialization. Terminal fixtures check
explicit traps and empty self-loop sinks in bounded child processes, including
a returning helper whose sink must remain a loop. Empty input is accepted.
The separate pipeline tests cover upstream typed-stream lowering and SSA
promotion; neither test suite is the pending full C-program differential corpus.

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

Run each build, test and measurement on the approved 9700X under its shared
`~/buster-fast-bench/exclusive.sh` lock, with owner/title/link metadata; pin each
benchmark to CPU 2. Use separate fresh processes for sustained `--only vector`
and `--only vector --after-scalar 200`, and compare both against scalar runs.
Local runs are diagnostic only. The current slice supplies the reproducible
measurement path; it does not establish a performance gain.

## Remaining acceptance

Issue #141 remains open. Full pattern-state generation, reciprocal-division
selection, the remaining scalar/ABI/spill consumers, and whole-C integration
are outstanding. The native fixtures are real generated code but are not
`c-testsuite`, Csmith/YARPGen, the full corpora, SQLite or `speedtest1`. The
required 9700X comparisons against Tier 0 and Clang `-O2`, runtime and size,
selection core cycles per IR instruction, and complete #142 campaigns still
need the completed upstream source path and those recorded workloads.
