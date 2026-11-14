# Tier 1 SSA optimization

`src/buster/compiler/ir_optimize.c` owns the transformations in
[#139](https://github.com/buster14a/buster/issues/139). It consumes the
`IrFunction` supplied by [#131](https://github.com/buster14a/buster/issues/131).
SSA construction remains [#134](https://github.com/buster14a/buster/issues/134).

## Public transformation

Include `ir_optimize.c` after `ir.c` and `dominators.c`.
`ir_optimize(function, output_arena, scratch_arena, options, result)` returns
an `IrCheck`. `ir_optimize_options()` selects SCCP, dead-code removal and CFG
cleanup, a 64-sweep limit, and the vector path. The `passes` bits are
`BUSTER_IR_OPTIMIZE_SCCP`, `BUSTER_IR_OPTIMIZE_DCE` and
`BUSTER_IR_OPTIMIZE_CFG`; `scalar` selects the worklist twins. A zero sweep
limit immediately uses worklist completion. It never publishes a partially
converged optimistic analysis.

The input is immutable and structurally verified. Its reachable uses must
satisfy SSA dominance; valid unreachable regions are permitted. Entry-block
phis are forbidden by the shared IR contract and return `IR_ERROR_PHI` from
the shared verifier before transformation: initial function entry has no
predecessor value. Removing their unreachable predecessors must not produce a
zero-input phi or invent an `undef`. Typed-stream IR lowering creates an entry
preheader, so its ordinary output avoids this case.
The output is
a fresh `IrFunction` in `result.function`. The output and scratch arenas must
be distinct, disjoint and own none of the input columns. Failure preserves
the output arena length and result; scratch is restored on both outcomes.
Derived CFG, uses and dominance belong to scratch and do not survive the call.
The final output passes the IR verifier with freshly rebuilt dominator-tree
intervals. The tests also invoke each pass separately and all eight pass-bit
combinations, verifying each resulting function.

`ir_optimize_verified` in `ir_optimize_verified.c` (#1055) is the Tier 1
handoff: it runs the same preparation, SCCP, DCE and rewrite kernels, then
retains the final CFG and dominators in a caller-owned analysis arena for
`tier1_select_plan_verified`. Its invalidation and lifetime contract is in
[the analysis-handoff guide](ir-analysis-handoff.md). `ir_optimize` remains the
independent rebuilding control.

### Scalar if-conversion (#1382)

The C driver's `ir_if_convert.c` runs after SSA promotion, wide-value splitting,
integer truth simplification and truth threading, before the ordinary optimizer
prepares any analysis. It finds conditional terminators with a bounded 64-byte
opcode mask and an exact scalar twin. A function without one needs no CFG or
output allocation.

A region is a reachable head ending in a conditional branch whose two sides
reach one join. Each side is the join itself (a triangle) or an arm: a chain of
at most four blocks, each with the previous block (or the head) as its only
reachable predecessor and an unconditional branch to the next. A region's
blocks overlap no other region's in a round, except that a join may head
another region, so sequential clamps convert in one round.

The C lowering's arms hold rows that no phi needs: the comparisons truth
simplification copied, their zero extensions and zeros. They also hold rows the
head already computed, such as a conditional sum's reloaded element and its
address. So each arm row is first numbered against the earlier arm rows and the
head's rows. A row whose operation, type, flags, fields and represented
operands equal an earlier row's takes that row's value, comparing an address's
scale and offset by content. The head's rows are filtered 16 at a time
(AVX-512 compares of the operation, type, flags and operand columns, with a
scalar twin), latest first, and each candidate is checked whole. A load takes
an earlier value only from a plain load of the same address and type after the
head's last write. A write here is a store, an atomic, a fence, a call, a stack
restore, a lifetime marker or an observable load; no arm row writes. The
barrier scan is one `vpermi2b` lookup of a 128-entry opcode class table per 64
rows, with a scalar twin.

Then only the rows the join's phis need, through their represented operands,
move to the head's end, before its branch. Each needed row must be total:
constants, copies, wrapping integer arithmetic, bitwise operations, integer
comparisons, selects, integer and pointer conversions, addresses, and shifts by
an in-range constant. A plain load is speculated where the head made a plain
load or store of the same address, of at least its size, after its last
release. A call, a stack restore or a lifetime marker counts as a release,
since it could unmap or reuse the memory. Nothing else proves an address.
Rows no phi needs stay in their arm, which becomes an unreachable stub ending
in `unreachable` for SCCP/DCE/CFG cleanup to remove. A row left there must be a
reused one or one that could have moved, so the stub drops no effect and no
fault: a dead division by a variable, a dead unproven load, a call or a store
refuses the region.

A join reached only by the region's two edges turns each phi into a select of
the head's condition in place. A join with other predecessors keeps its phis.
Each phi takes a new select row in the head for the region's two entries, which
become one entry from the head. That is the shape of LLVM SimplifyCFG's
`FoldTwoEntryPHINode` and `SpeculativelyExecuteBB`, for the C lowering's
nested `?:`. The head then branches to the join. Rows are permuted, and the
only new rows are the folded selects and the sunk accumulations' zeros (below).

Two cost rules keep a branch or move a select:

- **A recurrence through the condition stays a branch.** The head's condition
  may read, within 16 rows, a phi that one of the join's phis flows into, as a
  running maximum's does. Its select would put the comparison on the loop's
  carried chain: on the 9700X, `cmp; setg; test; cmovne` is four cycles per
  element against a predicted branch's one, and #1382's running max ran 3.8
  times main's cycles. LLVM's x86 cmov conversion turns such a `cmov` back into
  a branch after selection, and clang -O2 keeps branches there.
- **An accumulation's select sinks below its operation.** An in-place phi may
  take `s op x` on one edge and `s` on the other: an addition, a subtraction
  from `s`, an or or a xor of integers, which nothing else reads. The
  operation's row then selects `x` or zero instead, and the phi becomes
  `s op select`, as InstCombine folds a select of a binary operation. A
  conditional sum's accumulator then waits for one addition per element, not
  for the `cmov` as well, and a predictable condition costs no more than its
  branch did. The zero is a new constant row in the head that names a zero the
  constant table already has; a function whose table has none keeps the select
  outside. A variable binding of the operation's row, whose value changed,
  becomes unavailable.

A converted head whose single-predecessor chain climbs to a conditional branch
may now be an outer region's arm (a nested `?:`, `||` inside `&&`). In that
case the pass runs again, for at most four rounds. Between rounds and after the
last, `ir_truth_simplify_values` takes the truth values the selects make: a
select of truth values tested against zero becomes an i1 select in that test's
row. So `select c, (zext x), 0` tested nonzero is `select c, x, c` (`c && x`),
and `select c, 1, (zext y)` is `select c, c, y` (`c || y`). Tier 1 selects i1
selects as it does wider ones. Its integer rules take no i1 `and`/`or`, so the
truth values stay selects (#1382's follow-up). The earlier truth pass, before
threading, keeps its comparison rules alone.

The admissible values are `i1`, `i8`, `i16`, `i32`, `i64` and pointers, matching
the scalar select consumer. Each arm moves at most four non-constant rows, and
its blocks hold at most 32 rows in all, needed or not. Each join takes at most
four selects. The moved rows' cost plus two per select is at most eight: a
constant costs nothing, an integer multiply three, a select two, and other
admitted rows (a speculated load among them) one. These conservative bounds
limit speculation and added live values; they are not measured target
latencies. The IR has no branch-probability metadata (`__builtin_expect`
currently lowers to its value), so a predictable branch converts unless the
recurrence rule keeps it. Functions containing indirect-branch labels remain
unchanged.

`ir_if_convert(function, output, scratch, scalar, result)` requires verified
SSA and distinct output/scratch arenas; scratch must own none of the input.
The output arena may already contain the input. A changed result owns fresh
row columns, block offsets, extra words and call records for each round,
borrowing immutable tables from the input until the ordinary optimizer
materializes its output. Both lifetimes must cover that consumption. Shared or
overlapping `extra` slices are never remapped in place: original switch/label
words stay intact, and surviving phi/call occurrences get appended value
slices. With no accepted region the result borrows the input and allocates no
output. Failure preserves the result and output mark; scratch is restored on
every outcome. The result counts the regions, selects, folded joins, reused
rows, reused or speculated loads, sunk accumulations, the candidate regions
each round left for a recurrence, and the rounds.

The existing optimizer command covers the pass with independent execution,
complete verification, exact twins, hostile speculation cases, guarded tails,
side-table aliasing and allocation failures. Its fixtures cover nested and
three-predecessor regions, the reload's reuse, its speculation behind a store
or a fence, its refusal behind a call, a volatile reload or another offset, and
the `&&`/`||` truth values with their dead and repeated rows. The C driver
tests also check native results against Clang and decode actual `cmov`/branch
shapes. These include #1382's nested clamp, `&&` and `||` values, and loops
whose only conditional branch is the loop's. A running max and a reload after
a call keep their branches.

Measured effect (#2674, the 9700X, CPU 2): #1382's kernels compiled by main
`b7a95eed`'s Tier 1 and by #2674's `fab53452`, called from a clang -O2 driver in
another object, median `cycles:u` of five rounds
([record](https://github.com/buster14a/buster-performance/tree/2c957f4be1b74bdaba6d81d4f2b92e93222ffbe5/docs/measurements/tier1-if-convert-1382-2026-11-13)).
The scalar kernels take 50 M calls on xorshift64's high bits, about 310 Mcycles
of which is the driver's loop. The loops take 80 passes over 2^20 such bytes.

| kernel | main | `fab53452` | clang -O2 -fno-vectorize |
| --- | ---: | ---: | ---: |
| `x < 0 ? 0 : x > 255 ? 255 : x` | 1,146.8 | 341.3 | 332.5 |
| `a < 128 && b < 128` | 1,131.6 | 344.3 | 335.9 |
| `(a < b) && (b < c \|\| c == 0)` | 1,321.8 | 371.3 | 349.9 |
| `if (a[i] > 128) s += a[i]` | 1,327.1 | 162.1 | 93.3 |
| `s += a[i] > 128 ? a[i] : -a[i]` | 1,349.5 | 203.5 | 150.6 |
| `m = a[i] > m ? a[i] : m` | 90.4 | 90.4 | 72.0 |

Each converted kernel's 25 to 42 M branch misses fell to a few thousand. #2674's
first commit had put the running max's comparison on its loop's carried chain
(342.1 Mcycles, 3.78 times main), which the recurrence rule removed. The sums'
remaining gap is clang's unrolling and the `setcc; test` that each `cmov`
waits for ([#2676](https://github.com/buster14a/buster/issues/2676)). The
truth values are i1 selects, where clang uses `and`/`or`
([#2677](https://github.com/buster14a/buster/issues/2677)).

The pass costs about 1.2% of sqlite3.c's `--tier1 -c` user cycles. That is
2,163.6 against 2,138.4 Mcycles over 11 runs, with 0.63% more instructions. Its
object has 0.17% more text, 678 `cmov`s against 422, and 16,266 conditional
jumps against 16,503. The time spreads over the rounds' analysis and rewrite
without a hot spot. Storing the per-row maps plus one instead of setting them
up each round measured 0.25% slower, and was dropped.

The lookup of a phi's entry from a block stops at the first one. Without the
early exit, clang vectorized the scan into AVX-512 masked gathers, which
MemorySanitizer reports as reading uninitialized lanes.

SCCP tracks a state and two-word constant per instruction and an executable
byte per deduplicated CFG edge. It sweeps executable blocks in reverse
postorder, with 16-instruction transfer batches; the scalar solver queues
changed SSA users and instructions affected by newly executable edges, phis in a
queue of their own (below). Integer
operations support all IR integer widths, including scalar i128 handling.
Copies, phis, selects, integer/pointer conversions, bitcasts and floating sign
negation preserve exact constant bits. Scalar floating arithmetic, floating
comparisons and numeric floating conversions remain overdefined because their
results can depend on the runtime floating environment; SIMD floating lanes fold
where they can't (below). Unknown values remaining in an executable region are
promoted to overdefined and propagated before rewriting.

A join of n arms (a long `?:` chain's, nested ifs') is queued again by each
incoming edge and value, and each visit of its phi meets the incoming values
on executable edges: n visits of n each were 55–66% of a `--tier1` compile of
such a chain at 16,000 arms (#1729). The lattice only falls, so a visit of an
overdefined value skips its transfer, and a phi's meet stops at its first
overdefined incoming value. The worklist also moves a constant phi it dequeues
while other instructions are queued (one queued again by a new edge into its
block or a changed incoming value) to a queue of its own, and visits it only
once the shared queue is empty, so that the edges and values reaching a join in
one wave share a visit: a join whose arms all give one constant, which stays
constant, is visited twice rather than once per arm. An unknown phi (its first
visit) and an overdefined one are visited in turn, as before; the queue is
reserved, unzeroed, by the first phi moved.
`IrOptimizeResult.sccp_phi_incoming` counts the incoming values the phi
transfers examined. The order of visits doesn't change the fixed point, which
the vector sweeps' exact agreement with the worklist checks.

### Constant SIMD lanes (#1450)

`ir_optimize_simd.c` supplies the wide transfer. Its first vector visit allocates
an instruction-to-vector index and one 64-byte value per vector-result row in
scratch. Scalar-only functions allocate neither, and retain their existing
two-word lattice. Masks still use that scalar lattice. The sparse values are
canonical: bytes above the vector width stay zero. Equality compares the whole
value, including phi meets and scalar selects; a changed loop-carried value
moves from constant to overdefined. Allocation failure aborts publication and
restores the public call's arenas.

SCCP folds 8-, 16-, 32- and 64-bit wrapping lane arithmetic, total packed shifts,
bitwise operations, integer comparisons, lane selects, raw-bit
scalar splats, copies, phis, scalar selects and 128-bit scalar/vector bitcasts.
Integer lanes feed the existing bounded 16-item fold batches and their scalar
twin; oversized packed shifts produce zero or the sign fill, rather than
using scalar shift refusal. Tests compare both paths against
`ir_simd_model.c` and execute complete before/after functions through the
shared interpreter.

Floating lanes fold only where every floating environment gives the same bits.
The IR carries no `FENV_ACCESS` state, and the runtime MXCSR chooses the
rounding, may flush a subnormal result to zero (FTZ) and may read a subnormal
operand as zero (DAZ). So `fadd`, `fsub`, `fmul` and `fdiv` fold a lane whose
result is exact, so that every rounding agrees, with no subnormal operand and a
result that isn't subnormal. A NaN operand decides its lane whatever the controls:
x86's rule gives the first operand's quieted where both are NaNs, and an invalid
operation gives the default NaN. An exact cancellation (`x - x`, `+0 + -0`) stays
unfolded: it is -0 when rounding down. A floating `vcmp` folds C's predicates
unless a lane without a NaN has a subnormal operand. The whole vector folds only
where every lane does. Folding never removes the operation: its row stays, with
its operands live, ahead of the constant its users receive, for its flags and
traps.

Neither path reads or writes the compiler's own MXCSR. The scalar twin
(`ir_optimize_simd_float_lane`) is integer arithmetic on significands and
exponents: a sum aligned in 128 bits, a product of significands, a quotient's
divisibility of odd parts, each refused past the format's precision or range.
The vector path rounds every lane down and up by AVX-512's embedded rounding,
with exceptions suppressed, and folds the lanes where the two agree. A zero that
no operand makes exact is refused, since the host's own FTZ would flush a tiny
result alike in both directions. Comparisons are integer operations on ordered
keys on both paths, because Clang drops a comparison's suppressed exceptions.
`test-ir-optimize` checks both paths against a rule taken from the model alone:
a lane folds exactly where `ir_simd_model.c` gives the default environment's bits
when rounding down, up and toward zero and under FTZ with DAZ, and has no
subnormal operand without a NaN. It runs the lane fixtures over
every vector width, plus 4,096 random lane pairs mostly near 1.0, with the
compiler's MXCSR set to an unusual value that must come back unchanged. A
one-off run of 409,600 pairs also passed. Tier 1's existing native MXCSR fixture
(`test-tier1-masks`) still sees round-upward, FTZ and DAZ results after SCCP.

The fold boundary receives complete `IrSimdValue` cells with all 64 bytes
readable, independently of logical vector width. No function column needs
padding; per-lane batches use bounded masks. Inputs are immutable during a
transfer and output is staged before publication. The sparse map and payloads
belong to scratch and do not survive the pass; folded `vconst` payloads append
to the fresh output's vector-constant table. These changes make no throughput
claim; target-host measurement remains required.

Dead-code removal classifies roots by a vector opcode-table lookup, skips
empty liveness batches, and propagates dependencies backwards. A separate
worklist implementation supplies the exact scalar result and completes a
bounded sweep. It skips dependencies of folded values and removed conditional
branches, and follows only executable phi edges. Floating operations stay
roots, folded or not, so their dependencies stay live.

CFG cleanup compacts unreachable blocks, collapses single-successor
conditionals, and threads transparent jumps. Pointer jumping and propagated
minimum IDs canonicalize an empty jump cycle to its smallest member, which
retains a self-loop and therefore nontermination. Up to 64 target IDs are
looked up by register permutations; larger graphs use bounded scalar indexed
loads. The scalar twin walks each functional-graph component with an explicit
path. Phi destinations stop threading, preserving predecessor identity.
Merging nonempty one-predecessor blocks is still pending.

The rewrite preserves surviving block order and ordinary instruction order.
Unfolded phis remain at the block start; folded phis become constants after
that prefix. Value and block IDs are remapped with phi pairs, switch targets
and call arguments. Calls and extra words are rebuilt compactly. Constants,
slots, address metadata and call-type metadata retain their original indices;
new folded constants append to the constant table. Unused entries in those
tables are not yet compacted. Output columns have the IR's exact logical
lengths and no readable tail padding.

## Integer transfer functions

`ir_optimize_fold_one` evaluates an integer operation on unsigned bit patterns
of 1 through 64 bits. Addition, subtraction, multiplication and left shift
produce the low result bits. Signed comparison uses a sign-bit bias. Signed
division uses magnitudes, which avoids signed host overflow. Division by zero,
signed minimum divided by minus one, and a shift at least as wide as its
operand are refused. Shift counts retain their own width; a wide count is not
truncated to the shifted value's width. Refusal leaves the output untouched. Refusing to fold an
operation does not authorize its removal or its motion to a different block.

`ir_optimize_fold_vector` handles at most 16 independent ready transfers with
two eight-lane AVX-512 groups. Arithmetic, bitwise operations, shifts and
comparisons run in vector lanes. Division and remainder use the exact scalar
transfer. The scalar batch entry point is `ir_optimize_fold_scalar`.

The producer supplies contiguous operation and width columns of `uint32_t`
and left/right/result columns of `uint64_t`. The operation column is a transfer
classification internal to this module, not a second IR opcode catalogue.
`count` is the logical length and cannot exceed 16. The caller owns all storage;
the columns remain valid for the call and result storage cannot overlap any
input. No alignment or padding is required. Only active lanes below `count`
are read. Only bits returned in the folded mask authorize writes to the result
column. Zero length permits null pointers. An invalid count returns an empty
mask without accessing columns. Partial batches flush at every block boundary
or dependency barrier; a semantic dependency must never wait for a full batch.

The independent test arithmetic uses 128-bit signed C calculations, checks
fault refusal separately, and places every column against inaccessible guard
pages. It checks all tail lengths from zero to 16, inactive stores, invalid
widths and counts, signed extremes, and random mixtures of operations.

## Transformation safety

The analysis lattice has unknown, constant and overdefined states. Unknown
means that the analysis has not learned a value; an IR `undef` must be
overdefined. A phi meets only incoming values on executable predecessor
edges. A branch on an overdefined value exposes every successor. A sweep
limit must finish the fixed point with the worklist solver before any result
is used for a rewrite.

Stores, calls, volatile operations, terminators and potentially faulting
operations remain liveness roots unless the pass has a separate proof that
the particular operation is safe to remove. In particular, a load with no
users is not automatically removable. Control-flow compaction must rebuild
phi predecessor pairs, including the case where several original edges reach
one successor. Instruction identifiers are remapped together with every side
table that contains a value identifier.

## Tests and measurements

`test-ir-optimize [--seed N]` checks independent wide arithmetic, guarded
columns, deterministic functional-graph cycle resolution, exact scalar/vector
rewrites, forced fallback, invalid SSA and allocation rollback. Its independent
bounded interpreter checks returns, termination or faults, calls, stores and
volatile load order for constructed diamonds, loops, switches, jump chains
and cycles, effects, mixed-width shifts and random arithmetic DAGs. It reads
opcode meanings directly and does not consume the optimizer's CFG, use
enumerator, opcode tables, lattice or fold functions. It takes `br_indirect` to
the label whose symbol's address it holds (label `k`'s symbol is 64 + `k`, the
fixtures' producer's), and runs a call with ABI records by them: an aggregate
argument's bytes, and an aggregate result's bytes, written where `c` points.
A separate i128 fixture checks a known cross-limb multiplication result. The
interpreter is a test oracle for these fixtures, not the complete interpreter
tracked by #176.

#131's labels and ABI records have fixtures of their own (#783): a label whose
block, a jump alone, is threaded moves to the jump's target; labels keep their
blocks beside a removed block that no label or edge reaches; a `br_indirect`
that a constant branch leaves dead goes with its labels' blocks, whose labels
then have none; and an aggregate call's destination follows dead-code
removal's renumbering, its records the input's. Each runs through every pass
mask on both paths, and with every pass the labels' blocks and the destination
are checked. Leaving a threaded label at its old block, a dead label at its
old block, the labels unrenumbered, or a call's destination unremapped each
fails the test.

`benchmark-ir-optimize [--instructions N] [--samples 1..21]
[--only scalar|vector] [--pass all|sccp|dce|cfg] [--after-scalar MS]` measures
complete public calls on frozen small/large arithmetic DAGs, a phi diamond,
a loop and a jump chain. It includes input validation, CFG/use/dominator
construction, analysis, allocation, compaction and final SSA verification.
It checks exact output agreement before timing. Repetitions are calibrated
once per path to target 2 ms; later samples can be shorter as execution
conditions change. Default samples alternate which path runs first. `--only`
isolates one path, and `--after-scalar` runs lighter scalar work before every
vector sample. The benchmark shares the original test fixture builders and
is included after `ir_optimize_test.c`. The default 9700X job runs it, and
`benchmark-ir-optimize-passes`, after the tests, pinned to one allowed CPU;
on the daily and dispatched runs and on pull requests that change `ir.c`,
`ir_validate.c` or `ir_optimize*.c`, each also with `--only vector
--after-scalar 200`. Like the others, their measurements are informational.

The output reports nanoseconds, TSC ticks (not core cycles), useful input
instructions per second, output size and retained analysis workspace. That
workspace count excludes temporary verifier and dominance allocations; it is
not a peak-memory measurement. For real source-derived IR, `benchmark-ir-source`
prepares the files as `test-ir-source` does and now runs both GVN paths once on
every function before timing `gvn` and `gvn-scalar` with the same medians. Its
#866 results are in [the value-numbering guide](ir-value-numbering.md)'s
performance acceptance. No benchmark result or speed claim is recorded
yet. Local ASan/UBSan diagnostic checks passed 189,731 checks with zero
failures at the initial pass-group checkpoint. Entry-phi refusal regression
was added after that checkpoint; target-host results and final
revision validation must be recorded separately.

### Pass fixtures in the text form

[#175](https://github.com/buster14a/buster/issues/175)'s last acceptance item is
the first tests of this issue written in the IR's text form, and
`tests/ir/optimize/*.ir` are those fixtures: hand-written before/after pairs for
SCCP alone, DCE alone, CFG cleanup alone, the combined set the C driver's
`--tier1` runs, value numbering, loop-invariant motion and private-field
replacement. `test-ir-optimize` parses each pair with both parser paths, runs
the pass with both optimizer twins, requires the vector result's canonical
print to be the after file's bytes, reparses it to the after file's function,
and executes both sides on eighteen inputs through the same independent
interpreter the constructed fixtures use. Value numbering's scalar twin is the
available-expression oracle, so it may remove fewer instructions than the
vector partition; every other pair must give both twins the same function.
SROA's slot renumbering means its stores legitimately move in the executor's
address-keyed trace, so that pair compares values and statuses alone.
`test-ir-optimize --print-text` prints the vector results for a reviewed
regeneration of the pairs. The [text tools guide](ir-tools.md#pass-fixtures-in-the-text-form)
has the fixture-by-fixture record and the exact contracts.

## Remaining issue scope and acceptance

The SCCP/dead-code/CFG group and the following separate slices are implemented
against the shared IR, with local verifier and independent execution checks:

- [Value numbering](ir-value-numbering.md) uses congruence partition
  refinement, dominance-qualified substitution and an independent scoped
  scalar expression table. Cyclic congruences are compared semantically.
- [Loop-invariant motion](ir-loop-motion.md) consumes #174's loops and
  dominance, with exact vector/worklist output and conservative total-operation
  motion into existing dedicated preheaders.
- [Private-field scalar replacement](ir-scalar-replacement.md) splits
  nonescaping, disjoint constant-offset fields. Tests compose its output with
  both upstream #134 promotion paths, including conditional stores and loops.
- [Bounded inlining](ir-inlining.md) processes acyclic call-graph components
  from callees to callers, expands small single-block leaf bodies, and preserves
  recursive calls and explicit growth limits. Vector cost admission has a
  scalar twin; rewriting preserves caller control flow and ordered effects.
- The [additional pass-cost harness](ir-optimization-measurements.md) measures
  complete GVN, LICM and field-splitting calls on frozen inputs. Its local
  sanitizer smoke checks provide no target performance claim.

These slices do not complete #139. The following work remains tracked:

- Scalar floating numeric evaluation needs an explicit environment contract.
- CFG cleanup still needs nonempty single-predecessor merging and metadata
  compaction where measurements justify its cost.
- Inlining still needs general callee CFG and stack-object cloning, measured
  profitability, complete translation-unit driver integration and vector
  operand rebasing beyond the current vector cost-admission sweep.
- The aggregate-size limit, motion profitability, pass order and repetitions
  require measurement. Conservative operation and alias exclusions are
  documented by each implemented stage.
- Complete pipeline integration, corpus and randomized-IR qualification,
  all-pass and individual-pass differential tests, and 9700X compile-time
  and emitted-program runtime acceptance remain pending.

Each transformation requires before/after verifier checks and independent
execution comparisons. Every function of the source corpora and randomized
IR must exercise the sweep and scalar paths; #142 must compare passes both on
and individually off. Those checks must use the complete source-lowering
and compiler-consumer pipeline. Local diagnostic tests do not satisfy 9700X
acceptance.

The required measurements are full pass time, including operand preparation,
analysis, remapping, allocation and verifier costs where enabled, plus emitted
program time for `speedtest1` and the corpora. They must report useful items,
bytes and peak scratch, compare both implementations in separately locked
9700X runs on CPU 2, and include sustained vector work and work after a lighter
phase. A kernel-only result cannot select the final pass order or discharge
the compiler performance criteria.

## Algorithm sources and provenance

The implementation and fixtures are original C under the project default
license. The algorithm references are Wegman and Zadeck,
[Constant Propagation with Conditional Branches](https://cs.brown.edu/research/pubs/techreports/reports/CS-91-22.html)
(1991), and Alpern, Wegman and Zadeck,
[Detecting Equality of Variables in Programs](https://doi.org/10.1145/73560.73561)
(1988). The papers supply algorithmic background; their code, figures and
fixtures are not incorporated.
