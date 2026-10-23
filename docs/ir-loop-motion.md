# SSA loop-invariant motion

`ir_optimize_licm.c` implements conservative loop-invariant motion for
[#139](https://github.com/buster14a/buster/issues/139). Include it after
`ir_optimize.c`, and call `ir_optimize_licm(function, output, scratch,
ir_licm_options(), result)`. It returns an `IrCheck` and a fresh verified
`IrFunction` in `IrLicmResult.function`. Input ownership, transactional arena
rollback and the explicit entry-phi refusal follow the
[shared optimization contract](ir-optimization.md).

The pass consumes [natural-loop analysis](dominators.md). It skips irreducible
loops and requires one reachable outside predecessor whose only successor
is the loop header. It does not synthesize preheaders or change CFG edges.
Nested loops are processed from smallest to largest. An instruction moved
out of an inner loop may subsequently move through the outer preheader.

Candidate operations are total, pure computations: integer wrapping
arithmetic and bit operations, comparisons, selects, exact conversions,
floating sign negation, constants, parameters and address calculations.
Division, remainder and shifts are excluded even when individual instances
might be provably safe. Loads, stores, calls, phis, `undef`, numeric floating
operations and floating comparisons are excluded. Taint follows uses from
every `undef`, so moving a calculation cannot fix a choice that was previously
unspecified on each iteration. There is no memory-alias assumption.

For one loop, a value is initially available if its current definition lies
outside the loop and dominates the preheader. A candidate becomes invariant
when all its operands are available. The vector path tests 16 candidates at
a time, with exact masked dword operand loads and masked availability gathers.
Only semantically present operands are gathered; no `NONE` index is accessed.
The caller-facing IR columns need no padding. Value spaces above `INT32_MAX`
use the scalar path because hardware dword gather indices are signed.

The independent scalar path queues candidates, then revisits their users
when a value becomes invariant. A vector sweep limit (64 by default) switches
to that worklist to complete the same fixed point. The output set is exact,
including dependencies that span partial batches. Neither path publishes
partially converged motion decisions.

The rewrite appends moved computations before each preheader terminator.
Original reverse postorder and in-block order topologically order all moved
non-phi dependencies. Ordinary surviving instructions retain their order,
and phis remain a prefix. Fresh value IDs remap direct operands, incoming phi
values and call arguments. Every variable side-table slice is rebuilt
independently, including valid overlapping input slices. CFG and dominance
are rebuilt for final verification. Unreachable blocks are compacted by the
shared rewriter. Instructions that can fault or have observable effects keep
their original relative order and execution condition.

`test-ir-licm [--seed N]` calls `run_ir_licm_tests(argc, argv)`, with the
command in `argv[0]`. Include its file after `ir_optimize_licm.c` and
`ir_optimize_test.c`. Fixtures check exact vector/worklist/fallback output,
the independent bounded interpreter, dependent arithmetic chains, nested
hoisting, missing dedicated preheaders, zero-iteration faulting division and
loads, undef dependence, no-loop functions, random arithmetic loops, every
guarded readiness tail and partial allocation rollback. Target-host cost,
register-pressure and complete generated-code measurements remain pending;
the current motion policy has no measured profitability model.

The implementation is original. LLVM's [loop terminology](
https://llvm.org/docs/LoopTerminology.html) was consulted for the preheader,
natural-loop and speculation-safety definitions. No implementation source
was imported.
