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

Each loop is walked over its own blocks, once, rather than the whole function
for each loop (#2781, #2979). In reverse postorder a value comes after its
operands (a phi's aside, and a phi doesn't move), so one forward availability
walk finds the loop's movable values; the unrestricted motion sends every one of
them to the preheader. On the largest functions of sqlite3.c, Lua and musl under
`+licm`, the whole-function sweeps this replaced took up to 1.6 times the
worklist's ticks, and the worklist itself scanned the whole function for each
loop.

`IrLicmOptions.vectors` (#2781) restricts that motion to vector- and mask-typed
values and the movable operands they need. Tier 1 enables it by default for a
function that has such values, unless `+licm` requests unrestricted motion. A
loop with no safe vector or mask value is skipped. The other loops take the
forward walk, then a backward operand-closure walk over their own blocks.
`IrLicmOptions.whole` retains the original whole-function algorithm, below, as
the exact test oracle of both policies' walks. If nothing moves, the result
borrows the input function and writes no output. `test-ir-licm` compares the
walks with it on SIMD, width-change, permute2 and nested-loop fixtures, checks
their interpreted behavior, and checks that scalar-only loops are skipped without
an output rewrite.
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

The whole-function oracle (`whole`): for one loop, a value is initially
available if its current definition lies outside the loop and dominates the
preheader. A candidate becomes invariant
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
`ir_optimize_test.c`. Fixtures check the walk's output against the oracle's
sweeps, worklist and forced worklist completion,
the independent bounded interpreter, dependent arithmetic chains, nested
hoisting, missing dedicated preheaders, zero-iteration faulting division and
loads, undef dependence, no-loop functions, random arithmetic loops, every
guarded readiness tail and partial allocation rollback. Whole-pass progress
fixtures cover thin, wide, mixed and phi-dependent chains at 255/256/257/4096
lengths, asserting identical output and motion with bounded sweeps, visits
and fallback for the oracle, and one visit of each loop instruction for the walk. The current motion policy has no profitability model, and its
generated-code measurement says it needs one (below).

## In `--tier1`

Before #2719, `--tier1` didn't run this pass on general-register values (#1189). An experiment ran it after promotion,
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

## The pressure policy (#2719)

`IrLicmOptions.pressure` is that model. `--tier1-passes=+hoist` runs it on
every function that has a loop and no vector or mask value (those take #2781's
`vectors` policy); it is off by default (below). A value it moves stays live
from the preheader through the whole loop, so it moves only what pays for that
register:

- **A loop that holds a call moves nothing.** Across a call a value needs one of
  the five callee-saved registers, and the values a loop already keeps across
  its calls hold them. `ir_licm_analyze` finds such loops with a bitset of the
  blocks that hold a call (the operations compared 64 at a time) against each
  loop's body, and walks none of their instructions.
- **A free value moves only as an operand.** A constant, a symbol's or a frame
  object's address, a cast that changes no bit a register holds (`inttoptr`,
  `ptrtoint`, `bitcast`, `copy`, `trunc`), and sums, differences, products,
  negations and addresses of the loop's free values alone cost nothing where
  they are used: selection folds them into an immediate or an address, or they
  are no instruction at all. One moves only where a moving value reads it (the
  backward pass of `ir_licm_loop_pressure`); otherwise it stays at its users. A
  `zext` or `sext` is costly: it is an instruction, and a loop that compares a
  byte against an invariant one widens it once outside.
- **A truth value stays.** A branch tests its comparison's flags; a hoisted
  `i1` takes a register and a `setcc` and is tested again in the loop.
- **A budget.** A loop moves at most `BUSTER_IR_LICM_REGISTERS` (12) costly
  general-register values, less the general-register values live through it:
  its header's phis and the costly values from outside that it reads, each
  counted once. The first ones in reverse postorder move. The count is of what
  the loop reads, not of what a move would free: a moved value whose operands
  were live through the loop only for it frees them, which the count doesn't
  credit.

A function without a loop (no edge to a block at or before its source,
`ir_function_cyclic`) skips the pass, and one where nothing moves is returned as
it was given, unwritten. The natural loops come with the preparation's
dominator tree (`IrOptimizeWork.dominator_outputs`), from one analysis, and a
function past the loop analysis' matrix limit is refused (`IR_ERROR_UNSUPPORTED`)
and goes on as it was. The rules are in `test-ir-optimize`'s text fixtures
(`licm-pressure`, `licm-pressure-call`, `licm-pressure-budget`) and
`test-ir-licm`'s budget on random invariant chains.

What each rule did, measured before the clean implementation by an
experiment's switches (#2719's record), in instructions by callgrind against
`--tier1` without the pass: Lua's five scripts and speedtest1, both built as
`measure-tier1-programs` builds them (Lua with a fixed string-hash seed), and
the record's 30 loop kernels (#2743's 25 and five with invariant arithmetic).
Every row but the second takes the free values above:

| Policy | Lua | speedtest1 | kernels |
| --- | ---: | ---: | ---: |
| every value (`+licm`) | +74.66% | +11.85% | −4.00% |
| constants and addresses never move; casts do | +60.88% | +2.70% | |
| free values as operands alone, no truth value | +0.10% | +0.94% | −10.89% |
| that, and the budget | −0.01% | +0.38% | −10.89% |
| free values as operands alone, no loop with a call | −0.00% | −0.17% | −10.89% |
| that, no truth value | −0.00% | −0.21% | −10.89% |
| that, and the budget (the policy) | −0.00% | −0.21% | −10.89% |

Lua's interpreter loop (`luaV_execute`, 17,239 instructions in three nested
loops of about 1,350 blocks, each with calls) is where every value's motion
cost: every value's motion made 15,845 moves there (a value out of each of the
three loops in turn), 12,331 of them constants and 1,977 pointer casts, and
`calls.lua` ran 4.2 times the instructions. The kernels that gain are those with invariant arithmetic: the
matrix multiply's row offsets (−26.6%), a 2-D row sum's (−32.6%), an affine
map's product (−22.2%), byte compares against a widened invariant (−11% to
−17%).

On the 9700X ([#2719's record](https://github.com/buster14a/buster-performance/tree/d6732216ac9005ff91305d6d96ca2c42409ce295/docs/measurements/tier1-pressure-licm-gvn-2719-2026-11-23),
`measure-tier1-programs --runs 11`, user cycles of each variant over `--tier1`
with `+hoist` in one executable; and `measure-compile --runs 11` of
`sqlite3.c`, `-c --tier1 --lanes 1`):

| | speedtest1 | Lua's five scripts | the kernels | `sqlite3.c`'s compile |
| --- | ---: | ---: | ---: | ---: |
| `+hoist`, cycles against without | 1.0032 | 1.0092 | 0.9384 | 1.0336 |
| `+hoist`, instructions against without | 0.9978 | 1.0002 | 0.8913 | 1.0396 |
| `+licm`, cycles against `+hoist` | 1.1059 | 1.7252 | | |

On speedtest1 and Lua the cycles move within code placement's noise (about 1%
between equal builds; Lua's scripts moved 2–3% each way at equal
instructions), and the instructions show why: those loops hold calls, or no
invariant work. The kernels gain 6.2% of their cycles, and the compile costs
3.4% more: the preparation's verification, use lists and dominator tree of
every function with a loop. #2719 asks for gains on speedtest1 and Lua before
a pass is on by default, as `+vectorize`, `+unroll` and `+storemotion` are
measured, so `hoist` stays off.

The implementation is original. LLVM's [loop terminology](
https://llvm.org/docs/LoopTerminology.html) was consulted for the preheader,
natural-loop and speculation-safety definitions. No implementation source
was imported.
