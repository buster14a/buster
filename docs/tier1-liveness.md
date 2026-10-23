# Tier 1 selected-use liveness

`tier1_liveness.c` connects the real Tier 1 selector to the existing
`dataflow.c` solvers and `tier1_allocate.c`. It supplies the IR/selection
integration slice of [#143](https://github.com/buster14a/buster/issues/143):
actual selected uses, dense values that live across CFG edges, and retained
physical-order intervals for allocation. Source-to-IR corpus compilation and
its 9700X performance evidence remain separate acceptance work.

## The stage boundary

Call `tier1_liveness_build(plan, output, scratch, vector, result)` after a
successful `tier1_select_plan`. The `Tier1SelectPlan` and its `IrFunction` are
immutable for the call. The selector has already checked dominance for any
cross-block operand. The adapter validates the IR shape and selected-use
ranges again, but does not invent a dominance proof or repair an invalid plan.
Recompute the plan and liveness after any IR, CFG or pattern change.

The producer's effective-use CSR is essential. For example, if a shift at node
3 folds into an ADD at node 5, the shift's input is used at node 5. The original
IR use list would end its lifetime too early. `covered[node]` suppresses the
covered node's register definition, and its own effective-use slice is empty.
Every surviving nonvoid node has one interval, including an unused definition
that the current emitter still writes. The current selector subset requires
definitions before uses in node order, and the adapter verifies that rule.

The result's `allocation` member is directly a `Tier1AllocationInput`. It owns
starts, ends, value IDs, allowed-register masks, classes and sorted use CSR in
the output arena. `value_count` is the original instruction count; covered and
void nodes have no interval. The allocator can therefore publish its existing
`value_registers[value]` column directly to `tier1_select_emit`. No temporary
bitset matrix or reconstructed operand list is required by that consumer.

`output` and `scratch` are distinct, nonoverlapping arenas. Their unused
capacity must not overlap the input columns or result view. Retained input may
precede the new output in the same arena; the tests cover this append-only
case. Failure restores both arena lengths and preserves the result. Success
retains output only and always restores scratch. Input arrays have exactly
their documented logical lengths; no padding or vector alignment is assumed.
The internal dense columns and bitsets belong to scratch and cannot be used
after return.

Errors distinguish invalid arguments/plan, unsupported instructions, size or
matrix limits, and allocation failure. Instruction/value counts are bounded by
the allocator's `2^20` limit, uses by `2^24`, positions by `2^24`, and all four
simultaneously live dataflow matrices together by 64 MiB. Bounds are checked
before multiplication or matrix allocation. Nothing silently truncates, spills
behind the caller's back, or selects another compiler tier on error.

## Machine positions and parameters

Uses occur at position `2*node`, definitions at `2*node+1`, with half-open
intervals. A dying operand can end at the definition position and share a
register with the result; the selector's destructive-operation handling and
reserved R11 make that reuse legal. The input register pool is obtained from
the selector's own allowed-register predicate: caller-saved GPRs excluding R11,
RSP and callee-saved registers. The current adapter emits GPR intervals only.
Narrow `I8`/`I16` definitions use canonical 32-bit registers and four-byte
spill storage; their one/two-byte memory accesses remain a selector property.
This does not admit narrow parameter/call ABI forms or narrow arithmetic.

Parameters begin at position zero in their fixed SysV registers RDI, RSI, RDX,
RCX, R8 and R9. The emitter emits no parameter move, so beginning a parameter
at its later IR definition position would allow an earlier instruction to
clobber it. Likewise a parameter is not a dataflow kill when control jumps
back to entry: it must preserve the original incoming value. A used parameter
enters the dense set when entry has a predecessor, even when all its uses are
in that same block. The self-loop test checks that its interval reaches the
backedge, beyond its last local use. Other emitted definitions retain the
ordinary kill rule.

PHIs, indirect/variadic calls and floating-point values remain unsupported by
this bounded selector/adapter combination. Direct nonvariadic calls accept up
to six `I32`, `I64` or pointer arguments and a void or matching scalar result.
Their ordered effective-use slices preserve duplicate arguments. The native
consumer handles ABI moves and preserves surviving caller registers around
each call; the liveness adapter does not ban those registers for whole
intervals. There are no fabricated phi copies or edge moves.
If allocation produces a nonuniform or
spilled value location, the current leaf emitter declines it explicitly. This
does not claim that the general allocator lacks fragment support.

The native consumer accepts forward block chains ending in a return, explicit
unreachable, or a final block containing only a branch to itself. The empty
terminal sink has no value locations to reconcile. The adapter's conservative
CFG intervals alone do not authorize other machine-code joins or backedges:
those require explicit predecessor/successor location reconciliation and
edge-specific phi uses.

## Dense rows and conservative envelopes

The adapter builds the CFG from the actual IR and marks values whose selected
uses cross a block boundary, plus the incoming-parameter re-entry case above.
Only those values receive dense IDs. Local values use their definition and
last selected use directly and never occupy a bitset column. Dense IDs retain
ascending original value order. The vector path constructs them in batches of
16 by masks, population counts and compress stores; the scalar twin has the
same IDs and ordering.

Use/def construction resolves dense IDs from the selected-use stream, then
batches up to 16 IDs within each block. AVX-512 conflict detection groups lanes
that address the same 32-bit word; a masked OR reduction combines their bits
before one scalar word store. No hardware scatter occurs, and duplicate uses
cannot lose updates. The scalar twin sets one 64-bit word bit at a time. Tail
loads are masked; inactive IDs are `BUSTER_IR_NONE`. Batches flush at every
block boundary, because grouping across blocks would change meaning.

The existing `dataflow_postorder` and scalar/vector liveness routines solve
the dense rows. The implementation does not alter those established solvers
or their alignment policy. Each row starts on a 64-byte boundary, is rounded
to a full 512-bit width and has zero padding. The vector and scalar paths
produce identical iteration counts and interval results in the tests.

The final handoff expands each value's interval to include block entry when it
is live-in and block end when it is live-out. Original machine-node order is
preserved. Taking one envelope over those extents conservatively covers loop
backedges, layout gaps, nested loops and irreducible cycles without assuming
that their bodies are contiguous. It can increase register pressure compared
with holes, loop-contiguous layout or edge splitting. That is an explicit
quality limitation, not an assumption that the lifetime ends at its final
textual use.

`cross_block_values`, `iterations`, `matrix_bytes`, `retained_bytes` and
`scratch_bytes` make the representation cost visible. `scratch_bytes` is a
bounded storage estimate including the postorder visit stack and alignment
allowance, not an RSS reading. CFG, dense maps, construction buffers and all
four matrices are discarded. Only intervals and exact selected-use CSR survive
for allocation. Dense grouping, transpose construction, validation and envelope
publication belong in any measurement of this producer-to-consumer path.

## Validation and remaining evidence

`test-tier1-liveness [--seed N]` constructs actual `IrFunction` values,
dominators and selector plans. It checks both liveness paths against an
independent per-value graph search for a use reachable before a definition,
including the no-definition parameter rule. The oracle independently derives
each interval's entry/exit extent and checks every retained use position.

Coverage includes folded-use lifetime extension, fixed incoming parameters,
entry self-loops, generated cyclic CFGs, local-only zero-matrix functions,
15/16/17, 31/32/33, 63/64/65 and 511/512/513 dense-value boundaries, duplicate
word updates, guarded input tails, malformed use offsets, a use of a covered
value, allocation failure and arena rollback. Real scalar/vector allocation
and emission consume the resulting intervals. A folded arithmetic function is
encoded by both encoders and executed against an independent C expression for
64 input pairs in each path. These are original generated IR fixtures; they
are not preprocessed source-corpus functions.

The focused Clang and sanitizer runs are development diagnostics. Final
acceptance requires the complete retained revision's locked 9700X tests and
measurements through selection, liveness, allocation and emission, as well as
corpus functions once the source producer supplies them. In particular,
existing isolated Dataflow benchmark results do not establish the cost of
these new construction and handoff stages. Issue #143 remains open for its
real corpus requirements.

The adapter and its fixtures are original project C code. No external
liveness or allocator implementation has been copied.
