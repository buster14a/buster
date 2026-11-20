# ir-optimization

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- The shared IR optimizers provide SCCP, dead-code/CFG cleanup, value numbering
  and conservative loop-invariant motion, private-field replacement and bounded
  translation-unit inlining, and #2755's loop unroller (below). `test-ir-optimize`, `test-ir-gvn`, `test-ir-licm`,
  `test-ir-sroa` and `test-ir-inline` verify transformations against independent
  semantic fixtures and the upstream SSA promoter (`test-ir-inline`'s #1282
  fixtures: SIMD callees' rows, vector constants and slots, and by-value vectors
  run on the IR's interpreter through inlining, SROA's `simd`/`flagged` options
  and promotion; #2541's generated units of callees with control flow, by the
  vector splice and the node-at-a-time one (a callee whose parameters don't
  begin it), through the same steps, and its per-callee limits and caller-only
  rewrite); `test-ir-optimize` also runs
  #175's hand-written `tests/ir/optimize/*.ir` before/after pairs in the text form
  through both parser paths, both twins, the canonical printer and the
  independent executor (`--print-text` prints them for review).
  #1729's join chains (arms of distinct constants, of the parameter's sums and
  of one constant, meeting at one phi) run through both twins against the
  executor, and chains of 4,096 arms bound the incoming values SCCP's phi
  transfers examine (`sccp_phi_incoming`) to four per arm on both paths, which
  the worklist path exceeded before the change.
  #1450's SIMD fixtures exercise constant lane folding against the lane model
  and interpreter, wide phi/bitcast propagation, exact-bit splats, GVN's complete
  vector payload keys and LICM's integer-lane motion on both paths. A floating
  lane folds exactly where the model gives the same bits under every rounding
  and under FTZ with DAZ (4,096 random lane pairs, and every lane fixture), the
  operation's row staying for its effects.
  `benchmark-ir-optimize`
  and `benchmark-ir-optimize-passes` include preparation and rewriting costs.
- Value numbering with memory (#2718, `ir_optimize_memory.c`, the optimizer's
  `BUSTER_IR_OPTIMIZE_MEMORY`): `test-ir-optimize` runs its six
  `tests/ir/optimize/memory-*.ir` pairs and `ir_optimize_memory_test.c`'s 400
  generated programs, whose interpreter's calls rewrite every escaped byte,
  folded into a frozen digest (the default seed; its scalar twin went in #2924),
  and an overflow fixture. `benchmark-ir-source --passes` times its `memory`
  row once. [Its guide](../../docs/ir-memory-value-numbering.md) has the rules,
  the vector phases and the measurements.
- #139's acceptance on real C: `test-ir-source --passes` runs every pass's
  vector and scalar versions on every function of c-testsuite's subset, the
  frozen `sqlite3.c` and musl in the correctness list, and
  `benchmark-ir-source --passes` times them on the 9700X ([the IR
  guide](ir.md)). The C driver's `--tier1-passes=LIST` turns each pass of
  `--tier1`'s pipeline on or off, for differential campaigns and run-time
  measurements, and `--tier1-report` counts what each did ([the Tier 1
  guide](../../docs/tier1.md)'s "The C driver's `--tier1`"). `test-ir-optimize`
  runs every fixture's pass masks with and without CFG cleanup's merges
  (`BUSTER_IR_OPTIMIZE_MERGE`), and `ir_optimize_test_merges` checks the merged
  blocks, the merged joins' phis and the dropped source regions.
  [The optimization guide](../../docs/ir-optimization.md) records supported inputs,
  the pass order and #139's measurements.

## Loop vectorization (#2743)

`test-ir-vectorize [--seed N] [--print NAME]` (`ir_vectorize_loops_test.c`) runs
the loop vectorizer's text fixtures, `tests/ir/vectorize/*.ir`: element-wise
operations of every lane width, fills, copies, sums with widening, a dot
product, the four other integer reductions, an if-converted conditional sum
with its unreachable stub, an `int` induction's clamp, byte arithmetic through
32-bit lanes, population counts, a stencil whose exit test is `i + 1 < n`, an
in-place `y[i] += k * x[i]`, an invariant start and index offset, a uniform load,
two loops and a nested one; and one fixture per refusal (a prefix sum, a
histogram, a strided store, the induction as data, a call, a volatile load, a
division, FNV-1a's recurrence, a float sum, a width no vector holds, more guards
than the limit, a reduction's partial value read, an early exit, a step of two).
Each runs by both parser paths and the pass at three targets of every operation
the IR has, at most 64, 32 and 16 bytes wide, its result and counts folded into
a frozen digest (#2924); the kinds and the refusing kinds' scan are checked
against their definitions; a fixture expects how many loops each width
vectorizes, or its refusal's reason. Each changed result, and its form after
the ordinary optimizer (whose final verification checks dominance), runs in the
shared interpreter against the input on one buffer, its pointers placed apart,
at one address, a byte, an element and a vector apart either way, at lengths
around the vectors' boundaries (signed bounds negative too): the same bytes,
value and status. Four mutants (`BUSTER_IR_VECTORIZE_MUTATE_*`: no overlap
test, an unrounded tail, a wrong reduction identity, lanes of the wrong width)
must each be rejected on some fixture. `--print NAME` prints a fixture's
64-byte result. `test-checker-driver`'s inlining unit compiles a C fixture of
loops by `--tier1 --tier1-passes=+vectorize --verify` at x86-64 to x86-64-v4 by
both paths, checks the loops each target vectorizes and that both paths give one
object, and runs it as clang -O0's build does. [The design and its
status](../tier1-loop-vectorization.md) has the rules and the measurements.

The optional `BUSTER_IR_OPTIMIZE_LAYOUT` pass (#1218) changes the rewrite's
block visitation order, using `ir_optimize_layout.c`'s fall-through chains.
Only the C driver's final Tier 1 optimization enables it by default; other
optimizer clients retain their existing order. The ordinary and retained-analysis
entry points both verify the resulting SSA and remapped CFG. See the
[Tier 1 command guide](tier1.md#block-placement-and-critical-edge-fall-through-1218)
for its ordering policy, regression coverage and measurement boundaries.

## Vector and mask values by default (#2781)

Tier 1's `vectors` pass item is on by default. In a function with vector or mask
values, it runs value numbering before promotion and LICM after if-conversion,
restricted to those values; `+gvn` and `+licm` still request the unrestricted
passes, and `-vectors` disables both restricted forms. The LICM implementation
walks each eligible loop's own blocks and skips loops with no safe vector or
mask value. `test-ir-gvn` and `test-ir-licm` cover both host paths, the
per-loop/whole-function LICM oracle, scalar neutrality and nested loops;
`test-checker-driver` checks the default and `-vectors` reports and native
output on a C vector fixture. See [the Tier 1 guide](../../docs/tier1.md).

## Loop unrolling (#2755)

`ir_optimize_unroll.c` is the unroller `--tier1-passes=+unroll` runs after
LICM and the loop vectorizer: a main loop of up to eight copies of each admitted counted innermost
loop's body, guarded by a bound the preheader computes, before the original
loop as its remainder. [The design](../../docs/tier1-loop-unrolling.md) has its
rules, budgets, refusals and the 9700X's record.

`./build/compiler test-ir-unroll [--seed N] [--shapes N]` builds loops in the
IR's text form (a builder that names values), runs each and folds its function,
outcomes and counts into a digest frozen for the default seed and shapes
(#2924), and checks the classes, their union and the operand remap against
their definitions; an unrolled one verifies with
dominance, passes Tier 1's optimizer and executes as the original did (return,
status and stores) on the shared executor, on parameters around zero and the
main loop's size and at each type's signed and unsigned limits. Every loop
form, predicate, operand order, branch sense, reduction kind and width, then
`--shapes` random ones (400 by default), each refusal, the budgets and every
short output arena are covered. `test-ir-optimize` runs its text pair
(`tests/ir/optimize/unroll-loops-*.ir`), and `test-ir-source --passes` the pass
on c-testsuite's subset, the frozen `sqlite3.c` and musl, where each unrolled
function must verify with dominance.

## Vectors assembled in memory (#1344)

clang's headers write `_mm512_set1_epi8`, `_mm512_setzero_si512`,
`_mm512_set_epi64` and their kin as vector compound literals, and the typed
stream initializes such a literal as an object: each element's store, then one
load of the whole vector. With the `simd` and `flagged` options, SROA's
`ir_sroa_assemble` turns that load back into a value before the fields are
made. It walks each reachable block's loads and stores of the slots that are
not refused and hold at most 64 bytes, and keeps for each slot the bytes the
block's stores wrote, which of them are constants', and whether every store
wrote the same scalar. A vector load whose every byte they wrote becomes, in
place:

- an `IR_VCONST` of the bytes, where each is a constant's (read through the
  truncations, extensions and copies the typed stream leaves between a literal
  and its store), appended after the function's own vector constants; or
- a splat of the scalar, where every store wrote the same one at offsets that
  are multiples of its size. A slot's repeated loads with no store between
  count as one scalar, since the typed stream reads a parameter again for each
  element.

The element stores then reach no load and go with their fields. The walk
assumes nothing across blocks: a slot that isn't refused has no access but its
loads and stores, and a dead store's bytes are read by no load, so within a
block it sees every write of the bytes it decides. A literal whose elements are
distinct scalars stays in memory. `IrSroaResult.assembled` counts the loads,
and the C driver's Tier 1 path takes SROA's result when it is nonzero, as when
a slot was split. `test-ir-inline` runs four fixtures in the typed stream's
shape through SROA and promotion on both twins and on the interpreter: a splat
of 64 bytes read from the parameter's object with a second 16-byte load, a
zeroed object's quadword constants, and two that must stay memory (the
scalar's object written between its loads, and one lane another scalar's).

On the compiler's own lexer by `--tier1`, `c_tokenize` went from 30.20 to
14.62 instructions per byte on a 9,076-byte file (clang -O2: 3.06), every
instruction counted; the record is
[buster-performance's](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-vector-lexer-breakdown-1344-2026-11-14).
What it leaves are true constants that Tier 1's constant rule still builds
below `rsp` (#2744), but for those whose quadwords are one value, which are a
`mov` and a `vpbroadcastq` at EVEX targets (13.46 instructions per byte).

## Verification work and retained analyses (#1379)

Every first validity boundary remains enabled. `ir_verify_cfg` checks shape,
rows, phi/switch metadata and same-block operand order, retaining the CFG it
built for the next consumer of that exact immutable revision. Optimizer
preparation consumes this CFG, constructs dominators and checks reachable
cross-block uses. It still permits dead regions that optimization will remove.

Final verification constructs one CFG and one dominator tree. After the
structural check, `ir_verify_dominance` validates the new tree view and every
operand's strict SSA dominance without repeating the phi/switch scan. The
ordinary final verifier rebuilds its analyses independently of the retained
optimizer-to-selector product and allocates no optimizer use lists, lattice,
liveness or work queues. Its failure path preserves the former priority of
reachable-use diagnostics over dead-region diagnostics.

The remaining boundaries in the ordinary C `--tier1` path are:

| Boundary | Row checks | CFG builds | Phi/switch scans | Operand-order/dominance scans |
| --- | ---: | ---: | ---: | ---: |
| Lowered output | 1 | 1 | 1 | 1 |
| Promotion preparation | 0 | 1 | 0 | 1 reachable-use scan |
| Promoted output | 1 | 1 | 1 | 1 |
| Optimizer preparation | 1 | 1 | 1 | 1 order scan and 1 reachable-use scan |
| Final retained verification | 1 | 1 | 1 | 1 order scan and 1 strict dominance scan |

Compared with `b7a95eed`, optimizer preparation builds one CFG instead of two;
final retained verification scans phi/switch metadata once instead of twice.
Ordinary optimizer-result verification, also used by GVN, LICM and SROA,
builds one CFG instead of three and performs no unused optimizer preparation.
The current inline-summary pipeline passes its verified lowering or expansion
result through the existing verified-input promotion API. Candidate transforms
can build additional CFGs; optional passes add their own validity boundaries.

There is no cross-mutation cache. New descriptors, in-place rewrites, shared
side-table edits and arena retirement still follow the existing revision and
ownership rules. Failed CFG validation publishes nothing and restores both
arena marks. Retained-final counters distinguish one structural `graph_checks`
from one `dominance_checks`; the latter is a separate operand walk. `--verify`
continues to enable the driver's allocation/code certificates and scheduler
cross-checks; these IR validity checks run with or without it.

`test-ir-optimize` compares retained and ordinary results with a fresh scalar
full verifier and independently rebuilt CFG/dominators. Its boundary fixtures
cover malformed rows, phi edges, switch and call payloads, invalid tree views,
diagnostic precedence, unreachable regions, allocation failures and unchanged
caller prefixes. The existing ownership, invalidation, generation exhaustion,
same-address reuse and scratch-poisoning tests remain. `test-ir` also rejects
invalid third-operand dominance for every ternary intrinsic kind, vector width
and verification-batch lane on both twins.

These are reductions in repeated work, not a measured compile-time claim.
The issue's few-percent verifier-share target requires current SQLite/Lua
profiles and paired A/A and A/B qualification on an approved main/scheduled
9700X campaign, with raw evidence in `buster-performance`.

## Bounded scalar if-conversion (#1382)

The C Tier 1 driver runs `ir_if_convert.c` after promotion, wide splitting,
integer truth simplification and truth threading, before optimizer
preparation. Diamonds and triangles whose arms are short chains of
single-predecessor blocks become `IR_SELECT`; the existing selector emits
`cmov`. Each arm's rows are numbered against the head's first, so the
lowering's repeated comparisons, zero extensions and reloads of an element the
head loaded take the head's values. Only the rows the join's phis need move to
the head, and they must be total or proven loads; the rest stay in an
unreachable stub and must be droppable. A join with other predecessors keeps
its phis and takes new selects in the head. Up to four rounds take nested
regions (a nested `?:`, `&&` and `||` values), with `ir_truth_simplify_values`
turning the truth values' selects into i1 `and`s and `or`s of them (#2677). A condition
that reads the value the join carries round a loop (a running max) keeps its
branch, and an accumulation's select sinks below its addition
(`s + (c ? x : 0)`). Each arm moves
at most four non-constant rows, each join takes at most four selects, and the
moved rows' cost plus two per select is at most eight. These are policy bounds,
not timings; [the design](../ir-optimization.md#scalar-if-conversion-1382) has
the rules.

`test-ir-optimize` runs `ir_if_convert_test.c`: complete SSA verification,
independent execution before/after cleanup, every result folded into a frozen
digest (#2924), the scans (conditional branches, barriers, keys' filter)
against their definitions on random columns, every guarded candidate-scan tail, multi-phi edge polarity, both triangle
orientations, sequential, nested and three-predecessor regions over rounds,
loop-carried values, shared switch/call side-table words, allocation rollback,
refusal of unsafe or expensive speculation, the reload and truth fixtures,
a running maximum kept as a branch (and converted with a constant bound), and
the loop's conditional sum sunk below its addition.
The reload fixtures reuse a reload, speculate it behind a store or a fence, and
refuse it behind a call, as a volatile access or at another offset. The truth
fixtures reduce `&&` and `||` to an i1 `and` and `or` and refuse a dead division.
`test-checker-driver` adds C-source semantic comparison against Clang, with
both scalar paths and rematerialization settings. Its decoded object checks
require `cmov` without conditional branches for max, the clamps (sequential
and nested) and conditional arithmetic, no select of a tested truth value (no
`cmov` whose flags a `test` wrote) and no conditional branch for the `&&`/`||`
values (#2677; their `cmov`s are #2676's fused selects of comparisons), and
`cmov` with only the loop's branch for the conditional sum and the `?:` sum. They also
require the branches to remain for loads, division, variable shifts, calls, a
reload after a call and the running max.

## Dominator analysis (#1352, #2924)

The AUTO transformation heuristic still chooses scalar SCCP/DCE/CFG kernels
for small or branchy functions. The dominator analysis they prepare and verify
with has one path, SEMI-NCA and the scalar loop bodies, whatever the heuristic
or the scalar control ([why it is scalar](../dominators.md#why-it-is-scalar)).
Other internal preparation callers retain their scalar argument's meaning.

The 64 MiB matrix budget bounds the frontier rows and the loop bodies; it is a
memory limit, not a measured crossover.

The verified-handoff fixtures check preparation under AUTO and the explicit
scalar control, empty graphs and the independently known diamond relation.
Every existing fixture still compares ordinary/verified optimized IR and all
dominance relations against a fresh rebuild.
