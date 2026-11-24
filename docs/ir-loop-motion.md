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
them to the preheader. A value of the loop is movable when it is a candidate
(below) and each of its operands is either a movable value of the loop or a
value outside it whose block dominates the preheader.

The walk goes a value at a time, and it is LICM's one implementation (#2924).
It replaced a whole-function algorithm that found each loop's movable values
by vector sweeps to the fixed point of their readiness, 16 instructions at a
time with gathers, with a use-worklist twin. On the 9700X under
`--tier1-passes=+all` on sqlite3.c, Lua and musl, the sweeps took 1.07 to 3.00
times the walk's ticks and the worklist 1.35 to 1.85, in every size bin; the
objects were byte-identical
([record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/scalar-paths-removal-2924-2026-11-17/c-licm-2979)).
The whole-function algorithm then stayed as the walk's test oracle until #2924
deleted it, after `test-ir-licm`'s digest had been frozen where the two agreed.

`IrLicmOptions.vectors` (#2781) restricts that motion to vector- and mask-typed
values and the movable operands they need. Tier 1 enables it by default for a
function that has such values, unless `+licm` requests unrestricted motion. A
loop with no safe vector or mask value is skipped, and so is a loop that holds
a call (#2743): every vector and mask register is caller-saved, so a value
moved out of such a loop is stored and reloaded around each of its calls.
Before that rule, with the loop vectorizer on by default, unit inlining copied
a vectorized loop into `sqlite3VdbeExec`, and its two splats moved out of the
interpreter's loop: at znver5 the function grew from 87 KB to 131 KB, with
1,788 more stores around calls (#2831); with it, 90 KB, and nothing moves
there. The other loops take the
forward walk, then a backward operand-closure walk over their own blocks.
If nothing moves, the result borrows the input function and writes no output.
`test-ir-licm` checks the walk's motion on SIMD, width-change, permute2 and
nested-loop fixtures against the interpreter, and checks that scalar-only loops
are skipped without an output rewrite; on nested loops with a call in the outer
loop and in the inner one, it checks where the vector ends up (the inner
preheader, and its own block) and the loops passed over for their calls.
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
lane flags. A loop fixture checks the complete dependency chain, its
interpreted result, and the unmoved floating effects; an undef variant checks
that the chain stays in place.

The candidates are classified 16 instructions at a time (`ir_licm_classify`):
the operations' kinds and free kinds come from 128-entry tables by byte
permutes, with masked loads of the operation, type, flag and discriminator
columns and masked stores of the results, so no column needs padding.

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
`ir_optimize_test.c`. Fixtures check the walk's motion counts against the
independent bounded interpreter: dependent arithmetic chains, nested
hoisting, missing dedicated preheaders, zero-iteration faulting division and
loads, undef dependence, no-loop functions, random arithmetic loops and
partial allocation rollback. Long-loop fixtures cover independent values, a
chain, a mixed chain and a phi-dependent one at 255/256/257/4096 lengths, with
one visit of each loop instruction. The classification's columns are checked
at every tail against an inaccessible page. Every result of the walk, by all
three policies, folds into an FNV-1a digest that is frozen for the default
seed; it was recorded where the walk and the whole-function oracle agreed.
`test-ir-source --passes` gives LICM's result on every function of its
corpora back to LICM, which must move nothing: each loop's one walk reaches
the fixed point. The current motion policy has no profitability model, and its
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

`IrLicmOptions.pressure` is that model. Tier 1 runs it by default (`hoist`;
`--tier1-passes=-hoist` turns it off) on every function without a vector or
mask value (those take #2781's `vectors` policy) where its screen finds a loop
that may hold no call (below). A value it moves stays live from the preheader
through the whole loop, so it moves only what pays for that register:

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

Before any analysis, Tier 1 asks `ir_licm_pressure_screen` whether the
function may have a loop without a call: an edge to a block at or before its
source that joins two blocks without one. Every cycle has an edge back in the
blocks' order, and a loop without a call has a cycle of blocks without one, so
a function without such an edge (no loop, or a call in every loop) moves
nothing and skips the pass. The blocks that hold a call come from the
operations 64 at a time; the terminators of those that hold none are read one
by one, as `ir_build_cfg` reads them, since a gather of them would cost more on
Zen 5 than the loads. A function where nothing moves is returned as it was
given, unwritten. The natural loops come with the preparation's dominator tree
(`IrOptimizeWork.dominator_outputs`), from one analysis, and a function past
the loop analysis' matrix limit is refused (`IR_ERROR_UNSUPPORTED`) and goes on
as it was. The rules are in `test-ir-optimize`'s text fixtures
(`licm-pressure`, `licm-pressure-call`, `licm-pressure-budget`),
`test-ir-licm`'s budget on random invariant chains and its screen fixtures (a
call in the loop's latch, a call in its header, and a loop without a call whose
back edge goes forward in the blocks' order).

### What it costs to compile

In #2719's first measurement the pass cost 3.4% of `sqlite3.c`'s `--tier1`
compile. A profile of it on the 9700X (perf's cycles, against the same build
without the pass) put about half of that in verifying the promoted function
again: the verifier's shape, row and graph checks and the operands' dominance
check, though promotion verified it and the passes since (threading,
if-conversion) take it as verified. The rest was the dominator tree and
its natural loops, the use lists and the motion's own analysis. Tier 1 now
runs the pass with `IrLicmOptions.validated`: its preparation builds the CFG
with `ir_cfg_validated`, as if-conversion and threading build theirs, and
leaves out the dominance check (`IrOptimizeWork.validated`). The optimizer
after it verifies whatever reaches it, and the pass's own result is verified
where it rewrites the function, as before. Under the policy the use lists only
serve to taint what an undef reaches, so a verified function without an undef
builds none (`IrOptimizeWork.without_uses`). The screen then leaves out the
functions whose every loop holds a call. Each function's output is unchanged:
`sqlite3.c`'s and Lua's 34 sources' `--tier1` objects are byte for byte those
of main's `--tier1-passes=+hoist`, and `test-ir-optimize` runs every LICM and
value-numbering fixture with and without the verified input's preparation.

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
every function with a loop. #2719 asked for gains on speedtest1 and Lua before
a pass is on by default, as `+unroll` and `+storemotion` are measured, so
`hoist` stayed off then.

### On by default

No policy of the motion can reach that bar: speedtest1's and Lua's hot loops
hold calls or no invariant work. The owner set the bar for `hoist` on
2026-11-23 instead: a gain on the loop kernels, no change to speedtest1's and
Lua's run time beyond code placement's noise, and a compile cost that the pass
pays only where it may act ("What it costs to compile" above). With that cut,
Tier 1 runs it by default. On the 9700X (each run under the lock on one CPU;
the logs are in `~/buster-fast-bench/2719-claude-default/` there), against
`--tier1-passes=-hoist` of the same compiler:

| | User cycles | Instructions |
| --- | ---: | ---: |
| the 30 loop kernels (`time-runs --runs 11`) | 0.9358 | 0.8913 |
| speedtest1 (`measure-tier1-programs --runs 11`) | 0.9983 | 0.9978 |
| Lua's five scripts, one link order (the same run) | 1.0164 | 1.0006 |
| Lua's five scripts, 8 link orders (paired mean) | 0.9977 | |
| `sqlite3.c`'s `-c --tier1 --lanes 1` (`measure-compile --runs 11`) | 1.0188 | 1.0158 |

Lua's one executable ran 1.6% more cycles at equal instructions, though
`hoist` leaves its interpreter loop (`lvm.c`) as it was: it changes `ldo`,
`lstring`, `ltm` and `lstrlib`, whose sizes move the code after them. So, as
[the profiling guide](commands/profiling.md#comparing-generated-tier-1-code-over-layouts-1450)
says, both builds' objects were linked in the same 8 orders (Lua's Makefile
order and seven shuffles of it by Python's `random.Random(2719)`, fixed before
any timing), every executable's output checked against clang -O0's, and each
script timed over all 16 placements: the five scripts' cycles came to −0.23%
(a 95% interval of −0.98% to +0.52% over the paired layouts), each script's
interval spanning zero but `arithmetic.lua`'s, +0.25% (+0.03% to +0.48%), whose
code is unchanged. Before the cut, main's `+hoist` cost the same compile 3.2%
to 3.4% of its cycles and 4.0% of its instructions; with it, Tier 1's default
compile of `sqlite3.c` takes 1.6% to 1.8% more cycles and 1.4% more
instructions than main's without the pass. A differential campaign of 20 Csmith
and 20 YARPGen programs (seed 2719, both tiers, in a cloud container) passed
all 30 that clang -O0 built and ran in time, with `hoist` on and again with
`+numbering`.

The implementation is original. LLVM's [loop terminology](
https://llvm.org/docs/LoopTerminology.html) was consulted for the preheader,
natural-loop and speculation-safety definitions. No implementation source
was imported.
