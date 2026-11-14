# ir-optimization

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- The shared IR optimizers provide SCCP, dead-code/CFG cleanup, value numbering
  and conservative loop-invariant motion, private-field replacement and bounded
  translation-unit inlining. `test-ir-optimize`, `test-ir-gvn`, `test-ir-licm`,
  `test-ir-sroa` and `test-ir-inline` verify transformations against independent
  semantic fixtures and the upstream SSA promoter (`test-ir-inline`'s #1282
  fixtures: SIMD callees' rows, vector constants and slots, and by-value vectors
  run on the IR's interpreter through inlining, SROA's `simd`/`flagged` options
  and promotion; #2541's generated units of callees with control flow, by both
  splice twins, through the same steps, and its per-callee limits and caller-only
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
  [The optimization guide](../../docs/ir-optimization.md) records supported inputs
  and the source-corpus and target measurements still required for #139.

The optional `BUSTER_IR_OPTIMIZE_LAYOUT` pass (#1218) changes the rewrite's
block visitation order, using `ir_optimize_layout.c`'s fall-through chains.
Only the C driver's final Tier 1 optimization enables it by default; other
optimizer clients retain their existing order. The ordinary and retained-analysis
entry points both verify the resulting SSA and remapped CFG. See the
[Tier 1 command guide](tier1.md#block-placement-and-critical-edge-fall-through-1218)
for its ordering policy, regression coverage and measurement boundaries.

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
turning the truth values' selects into i1 selects between them. A condition
that reads the value the join carries round a loop (a running max) keeps its
branch, and an accumulation's select sinks below its addition
(`s + (c ? x : 0)`). Each arm moves
at most four non-constant rows, each join takes at most four selects, and the
moved rows' cost plus two per select is at most eight. These are policy bounds,
not timings; [the design](../ir-optimization.md#scalar-if-conversion-1382) has
the rules.

`test-ir-optimize` runs `ir_if_convert_test.c`: complete SSA verification,
independent execution before/after cleanup, exact scalar/vector output, every
guarded candidate-scan tail, multi-phi edge polarity, both triangle
orientations, sequential, nested and three-predecessor regions over rounds,
loop-carried values, shared switch/call side-table words, allocation rollback,
refusal of unsafe or expensive speculation, the reload and truth fixtures,
a running maximum kept as a branch (and converted with a constant bound), and
the loop's conditional sum sunk below its addition.
The reload fixtures reuse a reload, speculate it behind a store or a fence, and
refuse it behind a call, as a volatile access or at another offset. The truth
fixtures reduce `&&` and `||` to i1 selects and refuse a dead division.
`test-checker-driver` adds C-source semantic comparison against Clang, with
both scalar paths and rematerialization settings. Its decoded object checks
require `cmov` without conditional branches for max, the clamps (sequential
and nested), conditional arithmetic and the `&&`/`||` values, and `cmov` with
only the loop's branch for the conditional sum and the `?:` sum. They also
require the branches to remain for loads, division, variable shifts, calls, a
reload after a call and the running max.

## Dominator path policy (#1352)

The AUTO transformation heuristic still chooses scalar SCCP/DCE/CFG kernels
for small or branchy functions. It no longer forces their dominator analysis
onto the scalar solver: preparation and final verification use the caller's
explicit scalar control, otherwise the dominator analyzer's AUTO policy.
Explicit SCALAR and AUTO with `scalar=true` retain scalar analysis; explicit
VECTOR ignores that Boolean as before. Other internal preparation callers
retain their scalar argument's meaning.

The existing 64 MiB matrix budget and scalar fallback are unchanged. That budget
is a memory limit, not a measured crossover. This bounded change does not
replace the depth-dependent scalar solver or establish linear scaling beyond
the budget. It may increase temporary matrix storage for functions previously
selected by the transformation heuristic. Small-unit latency, branchy-function
scaling and all-core memory costs require approved-host qualification; no
measured improvement is claimed here.

The verified-handoff fixtures check preparation and retained-analysis path
selection, explicit overrides, empty graphs and the independently known diamond
relation. Every existing fixture still compares ordinary/verified optimized IR
and all dominance relations against a fresh scalar rebuild.
