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
Division, remainder and scalar shifts are excluded even when individual instances
might be provably safe. Loads, stores, calls, phis, `undef`, numeric floating
operations and floating comparisons are excluded. Taint follows uses from
every `undef`, so moving a calculation cannot fix a choice that was previously
unspecified on each iteration. There is no memory-alias assumption.

SIMD functions are admitted (#1450), including vector constants, integer lane
arithmetic/comparisons, bitwise operations, lane selects, masks and exact-bit
scalar splats. Packed variable shifts are total for every count and may move;
scalar shifts keep their existing exclusion. Floating lane arithmetic and
comparisons remain in their original blocks, as do memory operations and
undef-tainted vector values. The shared rewriter preserves wide constants and
lane flags. A loop fixture checks the complete dependency chain on both paths,
its interpreted result, and the unmoved floating effects; an undef variant
checks that the chain stays in place.

For one loop, a value is initially available if its current definition lies
outside the loop and dominates the preheader. A candidate becomes invariant
when all its operands are available. The vector path tests 16 candidates at
a time, with exact masked dword operand loads and masked availability gathers.
Only semantically present operands are gathered; no `NONE` index is accessed.
The caller-facing IR columns need no padding. Value spaces above `INT32_MAX`
use the scalar path because hardware dword gather indices are signed.

The independent scalar path queues candidates, then revisits their users
when a value becomes invariant. Each vector sweep reports newly available
values, counting only 0-to-1 discoveries. Completion continues whole-function
sweeps only while at least one value per 16 visited instructions becomes
available, otherwise it preserves the sweeps' discoveries and completes
through the exact worklist to the same least fixed point. A zero-progress
sweep is already a fixed point, and an explicit zero sweep limit forces
worklist completion. The vector sweep limit (64 by default) therefore bounds
productive sweeps rather than paying the whole limit for a thin chain. The
output set is exact, including dependencies that span partial batches.
Neither path publishes partially converged motion decisions.

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
guarded readiness tail and partial allocation rollback. Whole-pass progress
fixtures cover thin, wide, mixed and phi-dependent chains at 255/256/257/4096
lengths, asserting identical output and motion with bounded sweeps, visits
and fallback. The current motion policy has no profitability model, and its
generated-code measurement says it needs one (below).

## In `--tier1`

`--tier1` doesn't run this pass (#1189). An experiment ran it after promotion,
before SCCP, DCE and CFG cleanup: `--tier1`'s pipeline with `ir_optimize_licm`
between `ssa_ir_promote` and `ir_optimize_verified`. It counted user-space
instructions by callgrind against the same head without it:

| | speedtest1 | Lua 5.4.8, five scripts |
| --- | ---: | ---: |
| without LICM | 3,869.8 M | 7,610.3 M |
| LICM | 5,067.0 M (+30.9%) | 12,130.9 M (+59.4%) |
| LICM in loops of at most 8 blocks | 4,253.9 M (+9.9%) | 7,928.9 M (+4.2%) |
| LICM in loops of at most 2 blocks | 3,930.2 M (+1.6%) | 7,744.8 M (+1.8%) |

On speedtest1 it moved 58,205 values out of 1,522 eligible loops in 799
functions. The moved values are live across their loops, where Tier 1's
allocation spills them:
- the records grew from 353,256 to 422,488;
- spill stores and reloads grew from 15,558 and 18,745 to 27,330 and 28,495;
- edge moves grew from 49,403 to 73,833.

`--tier1 -c` of speedtest1's unit took twice as long: 1,682 ms against 828 ms,
on one CPU of a cloud container, so as a ratio only. Outputs and
speedtest1's verification hash were unchanged throughout.

A motion that pays needs a model of the loop's register pressure. Values Tier 1
recomputes (constants, symbols' and frame objects' addresses) should stay where
their users are, so that selection can still fold a constant into an
immediate.

The implementation is original. LLVM's [loop terminology](
https://llvm.org/docs/LoopTerminology.html) was consulted for the preheader,
natural-loop and speculation-safety definitions. No implementation source
was imported.
