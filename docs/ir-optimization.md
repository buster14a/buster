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
`BUSTER_IR_OPTIMIZE_CFG`; `scalar` selects the worklist twins. Tier 1 also
asks for `_LAYOUT` (#1218), `_IDENTITY` (#2554), `_MERGE`, CFG cleanup's
merges of single-predecessor chains (#139, `ir_optimize_merges`: "#139's
acceptance" below and [the Tier 1 guide](tier1.md)), and `_MEMORY`, value
numbering with memory (#2718, `ir_optimize_memory.c`: [its
guide](ir-memory-value-numbering.md)), which the default leaves off. A zero sweep
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
opcode mask. A function without one needs no CFG or output allocation.

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
(AVX-512 compares of the operation, type, flags and operand columns), latest
first, and each candidate is checked whole. A load takes
an earlier value only from a plain load of the same address and type after the
head's last write. A write here is a store, an atomic, a fence, a call, a stack
restore, a lifetime marker or an observable load; no arm row writes. The
barrier scan is one `vpermi2b` lookup of a 128-entry opcode class table per 64
rows.

Then only the rows the join's phis need, through their represented operands,
move to the head's end, before its branch. Each needed row must be total:
constants, copies, wrapping integer arithmetic, bitwise operations, integer
comparisons, selects, integer and pointer conversions, addresses, shifts by
an in-range constant, and floating negations (#2842, below). A plain load is speculated where the head made a plain
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
only new rows are the folded selects and the sunk accumulations' identity constants (below).

Two cost rules keep a branch or move a select:

- **A recurrence through the condition stays a branch.** The head's condition
  may read, within 16 rows, a phi that one of the join's phis flows into, as a
  running maximum's does. Its select would put the comparison on the loop's
  carried chain: on the 9700X, `cmp; setg; test; cmovne` was four cycles per
  element against a predicted branch's one, and #1382's running max ran 3.8
  times main's cycles. Tier 1's select of a comparison is `cmp; cmovg` since
  #2676, a chain of two cycles by their latencies, still twice the branch's.
  LLVM's x86 cmov conversion turns such a `cmov` back into a branch after
  selection, and clang -O2 keeps branches there.
- **An accumulation's select sinks below its operation.** An in-place phi may
  take `s op x` on one edge and `s` on the other: an addition, a subtraction
  from `s`, an or or a xor of integers, which nothing else reads. For these
  operations the arm row selects `x` or zero, and the phi becomes `s op select`.
  They need a zero already in the constant table. A one-arm AND of i8, i16,
  i32 or i64 instead selects `x` or all ones at that width. It admits a boolean
  parameter or a direct integer comparison and requires a region without
  reused or speculated arm loads. Copies and extensions in a comparison can
  hide a floating truth test, so this fold leaves them alone. A saved value in the head can feed the AND. A C arm
  reload can arrive without its volatile flag ([the IR guide](ir.md)), so the
  AND fold skips it. The pass reuses each required mask in the constant table
  or appends a missing width once to a private copy. In both orientations the
  accumulator waits only for the operation. A binding of the repurposed arm
  row becomes unavailable.

A converted head whose single-predecessor chain climbs to a conditional branch
may now be an outer region's arm (a nested `?:`, `||` inside `&&`). In that
case the pass runs again, for at most four rounds. Between rounds and after the
last, `ir_truth_simplify_values` takes the truth values the selects make: a
select of truth values tested against zero becomes an i1 operation in that
test's row. So `select c, (zext x), 0` tested nonzero is `and c, x` (`c && x`),
and `select c, 1, (zext y)` is `or c, y` (`c || y`); `select c, (zext x),
(zext y)` stays an i1 select. The same columns then fold `trunc (zext/sext x)`
into one cast of `x` (`ir_if_convert_fold_casts`, #2846). A `_Bool` result of
`&&` or `||` is `trunc i8` of `zext i32` of that i1, two arm rows where the
integer result is one, and the outer region would otherwise stay a branch. Tier 1 computes an i1 `and` and `or` as their
32-bit operations (#2677), as clang does. A truth value used as an integer
rather than tested, `select c, (zext x), 0` itself, is Tier 1's `and` of the
condition and the value too, or #2676's `cmp` and `cmovCC` where the condition
is a comparison only the select reads ([the selection guide](tier1-selection.md)).
A test of a floating comparison, which no integer comparison can take the place
of, becomes its `i1` copy, which Tier 1's selects and branches read through to
an ordered comparison's `ucomis` and `cmovCC` or `jcc` (#2791), and a select
of `==` or `!=` to `ucomis`, `cmovne` and `cmovp` where its values don't recur
through it (#2922). The
earlier truth pass, before threading, keeps its comparison rules alone.

The admissible values are `i1`, `i8`, `i16`, `i32`, `i64` and pointers, matching
the scalar select consumer, and since #2842 `f32` and `f64`, which Tier 1 selects
without a branch (`TIER1_RULE_FLOAT_BLEND`, [the selection
guide](tier1-selection.md)). A floating row moves only where it raises no
floating exception, so that a moved row sets no status flag C's code wouldn't
have (the optimizer keeps the floating operations as roots for the same
reason): of the total operations a copy, a select, a bitcast and a negation
(C's `-x` and IEC 60559's negate are quiet) make one, besides a constant and a
proven load. Floating arithmetic, comparisons and conversions have no cost:
they stay in their arm, which then refuses the region, so `x < 0 ? -x : x` and
`s += p[i] > 0 ? 3.0 : 1.0` convert, and `c ? a / b : 1.0` or the outer test of
`x < 0 ? 0 : x > 1 ? 1 : x` (its arm's comparison) stay branches. Each arm moves at most four non-constant rows, and
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
borrowing immutable tables (apart from an extended constant table) from the input until the ordinary optimizer
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
waits for ([#2676](https://github.com/buster14a/buster/issues/2676)).

The truth values were i1 selects then, where clang uses `and`/`or`. Since #2677
they are Tier 1's 32-bit `and` and `or`. Measured on the 9700X with the same
harness, 11 rounds, main `2ebfe68a` against #2796's `d42ea94b`, the driver's loop
being 300.6 Mcycles there
([record](https://github.com/buster14a/buster-performance/tree/35fa71ec1346673a404f56ae7e158e099016cdfe/docs/measurements/tier1-narrow-bitwise-2677-2026-11-14)):

| kernel | main | `d42ea94b` | clang -O2 -fno-vectorize |
| --- | ---: | ---: | ---: |
| `a < 128 && b < 128` | 320.0 | 318.7 | 334.1 |
| `(a < b) && (b < c \|\| c == 0)` | 383.9 | 363.1 | 365.5 |
| the same returning `int` | 374.1 | 362.9 | 381.7 |

sqlite3.c's `--tier1` object has 230 fewer `cmov`s (910) and 1,296 fewer bytes
of text. The compile takes 0.16% to 0.31% more user cycles. #2676, merged after
these runs, takes the `int` kernel's outer `&&`, whose comparison only its select
reads: `cmp` and `cmovge` instead of `setl` and `and`.

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
negation preserve exact constant bits. Floating arithmetic, comparisons and
numeric conversions fold only where the runtime floating environment can't
change their bits: SIMD lanes as below, scalar operations under a stricter rule
of their own (below). Unknown values remaining in an executable region are
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

### Scalar floating operations (#2720)

Scalar `fadd`, `fsub`, `fmul`, `fdiv` and `fcmp` of `f32` and `f64` fold as a
one-lane vector of the transfer above, so a result folds only where every
floating environment gives the same bits. A folded scalar row is a kind-2 root
(`ir_optimize_roots`): its constant discharges it and the row goes, so nothing
is left to raise a flag. A scalar operation therefore folds only where it raises
no exception flag at all: beyond the lanes' rule (an exact, normal result of
normal operands), no operand may be a NaN (a signaling one raises invalid, and
two quiet ones' payload would follow the operands' order in the emitted
instruction), the result may not be one (an invalid operation's default NaN),
and no division may be by zero. Those operations stay at run time, unfolded.

The numeric conversions fold under the same rule. `sitofp` and `uitofp` of an
8- to 64-bit integer fold where the format holds the value exactly; `fptosi`
and `fptoui` where the floating value is an integer the target holds, or a zero
of either sign; `fext` (`f32` to `f64`) and `ftrunc` (`f64` to `f32`) where the
operand is a zero, an infinity or a normal value that the target holds exactly
as a normal value. A NaN or a subnormal operand stays unfolded. `f80` and the
x87 are left alone.

`test-ir-optimize`'s oracle for these is the hardware: each random operation
runs by its own instruction (`addsd`, `comiss`, `cvttsd2si`, `vcvtusi2sd` and
the rest) in one asm statement that loads MXCSR, runs it, stores MXCSR and
restores it, under round to nearest, down, up and toward zero and under FTZ with
DAZ. An operation must fold exactly where all five give the same bits and raise
no flag and no operand is a NaN, to those bits, and the interpreter must give
the same result before and after: 22,000 random cases over the eleven
operations, each of which both folds and refuses.

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
branches, and follows only executable phi edges. Vector floating operations
stay roots, folded or not, so their dependencies stay live; a scalar one stays
a root until it folds, which it does only where it raises no flag (#2720).

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
`--tier1` runs, value numbering, loop-invariant motion, private-field
replacement and loop unrolling (#2755). `test-ir-optimize` parses each pair with both parser paths, runs
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

## #139's acceptance

The passes, and the guides that record each one:

- **SCCP, dead-code removal and CFG cleanup:** this guide.
- **CFG cleanup's merges (`BUSTER_IR_OPTIMIZE_MERGE`):** this guide, and [the
  Tier 1 guide](tier1.md)'s "The C driver's `--tier1`".
- **[Value numbering](ir-value-numbering.md):** congruence partition
  refinement, with an independent scoped scalar expression table; cyclic
  congruences are compared semantically.
- **[Value numbering with memory](ir-memory-value-numbering.md):** #2718's
  repeated loads and loads of stored values, forwarded along the dominator
  tree in the optimizer.
- **[Loop-invariant motion](ir-loop-motion.md):** total operations into existing
  dedicated preheaders of #174's loops.
- **[Private-field scalar replacement](ir-scalar-replacement.md):** nonescaping,
  disjoint constant-offset fields.
- **[Inlining](ir-inlining.md):** #2541's summaries and general splice.
- **[Loop vectorization](tier1-loop-vectorization.md):** #2743's measured
  design, not yet an implemented pass.
- **[Loop unrolling](tier1-loop-unrolling.md):** #2755's counted innermost
  loops, a main loop of copies before the original as its remainder, off by
  default (`+unroll`).
- **[Loop store motion](tier1-store-motion.md):** #3309's globals read from
  registers in the loops that update them, before promotion, off by default
  (`+storemotion`).

Each implemented pass has before/after fixtures, built in C and in the text form (#175), with
both twins and an independent executor. #139's last items close as follows.
The record is buster-performance's [`ssa-passes-139-2026-11-14`](https://github.com/buster14a/buster-performance/tree/c61db97a94c50c8f896ac5f4a58fb35bb44a23fe/docs/measurements/ssa-passes-139-2026-11-14).

### The pipeline and the order

`--tier1`'s order is: lowering; the unit's inlining from summaries (#2541);
SROA (`+sroa`); #866's solver (`+gvn`) or its vector/mask-only form
(`vectors`); loop store motion (`+storemotion`, #3309: [its
guide](tier1-store-motion.md)); promotion; the wide integers' halves and the wide vectors'
pieces; truth simplification; threading; if-conversion; the switches of
constants (`tables`, #3298); the value numbering of the promoted function
(`+numbering`, #2719); LICM (`+licm`), its vector/mask-only form or its
pressure policy (`hoist`, #2719); the loop vectorizer (`vectorize`, #2743: [its
guide](tier1-loop-vectorization.md)); the unroller (`+unroll`, #2755); and the optimizer,
which runs SCCP, the identities, value numbering with memory (`+memory`,
#2718), dead-code removal, CFG cleanup and its merges and layout, then the
final verification that selection reads. `--tier1-passes=` turns each pass on
or off ([the Tier 1 guide](tier1.md)). By default SCCP, dead-code removal, CFG
cleanup, the merges, the inlining, the switches of constants, the
vector/mask-only passes and the loop vectorizer run; unrestricted LICM and its pressure policy
(`+hoist`, #2719), SROA, the unroller, loop store motion, the solver (before
promotion or after, `+numbering`) and value numbering with memory don't, and
none repeats. The measurements below decided this, and
[#2718's](ir-memory-value-numbering.md#measurements) for value numbering with
memory.

### Switches of constants (#3298)

`ir_switch_table.c` takes a dense switch whose every arm only gives one value a
constant: `case 3: return 9;`, or `case 3: r = 9; break;` with the switch's end
or default among the arms, and with every other value the join merges the same
from all of them. The C lowering dispatches such a switch through a jump table
of its labels' offsets (a bounds check, a 32-bit load, an addition and an
indirect jump); the pass leaves the bounds check and the load where they are and
makes the dispatch `br J` (or `ret`) with the loaded entry as the value, and the
emission writes the constants into the table in place of the offsets
(`EmittedCode.value_tables`, `check_program_emit`'s tables), as LLVM's
SimplifyCFG switch-to-lookup-table does. An i8 or i16 value retypes the load to
the entry's low bytes; an i64 value takes the lowering's sign extension, so only
i64 constants that fit 32 signed bits convert. An arm with any row but a
constant, a join whose two values vary by arm, an arm that branches elsewhere, a
floating or pointer value and a table two dispatches load are refused. The arms,
left without the dispatch's edges, are the optimizer's dead blocks. A body with
jump tables takes its own pipeline rather than its inlining summary, which
skips the pass (no caller expands a body with labels).

On the issue's kernel (16 dense `return K` arms, 40 M calls with an LCG's
selector) the dispatch becomes `cmp; ja; mov (%rax,%rcx,4), %eax; ret`, clang
-O2's shape: on the 9700X, 269.30 ms became 36.94 ms (clang -O2: 29.75 ms) and
37.5 M branch misses 13 K. sqlite3.c, none of whose switches qualifies, compiles
by `--tier1 -c` in 604.61 ms with the pass and 604.08 ms without (11 runs
each). The record is buster-performance's
[`tier1-switch-value-tables-3298-2026-11-22`](https://github.com/buster14a/buster-performance/tree/ccf13245f4afac66b77f408e9fa8c72b4c68c7d3/docs/measurements/tier1-switch-value-tables-3298-2026-11-22).
`test-ir-optimize` runs `ir_switch_table_test.c`'s generated fixtures (returns
and phis of i8 to i64 from i32 and i64 selectors, negative lowest cases, holes,
shared arms, the join among the arms, and the refusals) on the independent
executor with the converted table's bytes, before and after the optimizer;
`test-checker-driver`'s value tables unit compiles a C fixture by `--tier1` and
`--tier1-passes=-tables` and runs it as clang -O0's build does, and checks which
functions keep an indirect jump.

### Agreement of the twins

- **The corpora.** `test-ir-source --passes` ([the IR guide](commands/ir.md))
  runs every pass by both versions on every function of c-testsuite's subset
  (430 functions), the frozen `sqlite3.c` (2,602) and musl (2,775, 1,355
  units). It covers SROA, the solver (exact against its complete-refinement
  oracle, semantic against its scalar twin), the optimizer's rows, LICM, and
  the inliner per caller. Every pass's versions agree on every function. On
  `sqlite3.c` the scalar solver removes the same 93,467 instructions as the
  partition. The correctness list runs it on all three.
- **Random IR.** `test-ir-metamorphic`'s combination row now includes the
  merges. A run of seed 139 over 2,000 cases (14,000 variants, 1,568,000
  checked executions through both twins of every pass) passed. The pass
  fixtures run every pass mask, with and without the merges, on both paths.

### #142's differential campaigns

Csmith `0cdc7103` and YARPGen `1adb290f` (#142's pins), seeds 5000 to 5039
of each, 80 programs per campaign, against clang -O0 and -O2 at both of
Buster's tiers, with Tier 1 as `--tier1 --tier1-passes=+all` and with each
pass turned off from there. Each campaign ran on a cloud host, a correctness
run and no timing:

| Tier 1 | Programs | Passed | Failed: clang -O0's link | Failed: clang -O0's 30 s bound |
| --- | ---: | ---: | ---: | ---: |
| `+all` | 80 | 66 | 8 | 6 |
| `+all,-sccp` | 80 | 66 | 8 | 6 |
| `+all,-dce` | 80 | 66 | 8 | 6 |
| `+all,-cfg` | 80 | 66 | 8 | 6 |
| `+all,-merge` | 80 | 66 | 8 | 6 |
| `+all,-gvn` | 80 | 66 | 8 | 6 |
| `+all,-licm` | 80 | 66 | 8 | 6 |
| `+all,-sroa` | 80 | 66 | 8 | 6 |
| `+all,-inline` | 80 | 66 | 8 | 6 |
| `+all` with `--scalar` | 80 | 66 | 8 | 6 |

A passed case means both of Buster's tiers and both clang modes compiled,
ran and printed the same output. The same 14 cases fail in every campaign,
each first at a step of clang -O0's:
- **8 YARPGen programs:** their arrays need more than the small code model
  reaches, so clang's link refuses them (`relocation truncated to fit`).
  Buster's link refuses 7 of them the same way. For the eighth, Buster links,
  and the cloud host's memory limit killed its executables at about 7 GB.
- **6 Csmith programs:** every build's run passes the 30-second bound, clang
  -O2's too.

No case shows a difference or a failure of Buster at either tier: 660
accepted comparisons of Tier 1's pipeline, 66 per configuration. The ledgers
are in the record. Each case's every mode is listed there.

### Compile time per pass, both versions

`benchmark-ir-source --passes` on the 9700X (CPU 2, under the lock) on the
frozen `sqlite3.c`: each pass's public call over all 2,602 functions (599,220
lowered IR instructions), medians of 21 rounds. Each call includes its
checks, analyses, rewrite and verification, which dominate the single-pass
rows.

| Pass | Vector, ms | Scalar, ms | Vector, ns an instruction | Scalar, ns an instruction | Scalar / vector |
| --- | ---: | ---: | ---: | ---: | ---: |
| SROA (Tier 1's options), on the lowered form | 46.3 | 63.3 | 77.2 | 105.7 | 1.37 |
| #866's solver, on the lowered form | 49.3 | 59.4 | 82.3 | 99.2 | 1.21 |
| SCCP alone | 42.9 | 46.7 | 71.6 | 77.9 | 1.09 |
| Dead-code removal alone | 38.2 | 47.0 | 63.8 | 78.4 | 1.23 |
| CFG cleanup alone | 30.9 | 40.4 | 51.6 | 67.4 | 1.30 |
| CFG cleanup and its merges | 33.1 | 41.6 | 55.3 | 69.5 | 1.26 |
| SCCP, dead-code removal and CFG cleanup | 50.4 | 53.1 | 84.2 | 88.6 | 1.05 |
| Tier 1's set (with the merges, layout, identities) | 54.2 | 56.9 | 90.5 | 95.0 | 1.05 |
| LICM | 55.8 | 58.3 | 93.1 | 97.2 | 1.04 |
| The inliner, 1,794 callers' units | 22.8 | 30.4 | 38.0 | 50.8 | 1.34 |

The optimizer's rows and LICM take the promoted form, 480,852 instructions.
The merges cost 2.2 ms over CFG cleanup alone on the vector path (1.3 ms on
the scalar), or 4 ns a lowered instruction. Their predecessor counts are one
scalar pass over the executable edges, a scatter of one count per edge, which
that bound includes; the chain heads come from the threading's pointer jumping,
with its vector and scalar twins.

The whole driver, `--tier1 --lanes 1 -c sqlite3.c`, 11 runs each by
`time-runs`, in user cycles:

| `--tier1-passes=` | User cycles, M | Against the default | Wall, ms |
| --- | ---: | ---: | ---: |
| (the default) | 2,459.1 | | 481.8 |
| `-sccp` | 2,500.5 | +1.7% | 470.3 |
| `-dce` | 2,582.3 | +5.0% | 484.9 |
| `-cfg` | 2,474.0 | +0.6% | 472.9 |
| `-merge` | 2,473.0 | +0.6% | 484.8 |
| `-inline` | 2,099.4 | −14.6% | 416.0 |
| `+gvn` | 3,247.3 | +32.0% | 604.5 |
| `+licm` | 3,023.3 | +22.9% | 565.5 |
| `+sroa` | 3,248.5 | +32.1% | 624.8 |
| `+all` | 4,177.3 | +69.9% | 773.1 |
| `-all` | 2,234.6 | −9.1% | 420.7 |

The default passes earn their compile time: turning SCCP, dead-code removal
or CFG cleanup off makes the rest of the pipeline do more. The merges save
0.6% of the compile's cycles: 8,003 of `sqlite3.c`'s blocks join their
predecessors, which leaves allocation and emission fewer blocks. Against main
`ca1ede6a`, whose objects head's `--tier1-passes=-merge` reproduces byte for
byte (speedtest1's unit and Lua's 33 units), head's `--tier1` takes 2,457.5 M
user cycles to main's 2,463.3 M (−0.23%), and Tier 0's compile is unchanged
(586.9 M to main's 588.1 M).

### Run time per pass

`measure-tier1-programs` on the 9700X (CPU 2, 11 rounds of a Williams design
per run) with each variant beside the default `--tier1`. Every build gives
speedtest1's verification hash and each Lua script's clang -O0 output. The
default's medians varied by about 0.3% on speedtest1 and 0.8% on Lua between
the five runs; earlier records place code-placement effects at about 1.5%.

Each variant against the default `--tier1` of the same run, in user cycles and
instructions (speedtest1's wall time is mostly its database's I/O):

| `--tier1-passes=` | speedtest1, cycles | speedtest1, instructions | Lua, cycles | Lua, instructions |
| --- | ---: | ---: | ---: | ---: |
| `-sccp` | +3.7% | +9.4% | +5.8% | +5.3% |
| `-dce` | +24.4% | +54.9% | +22.9% | +46.7% |
| `-cfg` | +0.0% | +0.0% | −1.0% | −0.0% |
| `-merge` | −0.6% | −0.1% | +2.0% | −0.0% |
| `-inline` | +4.4% | +3.0% | +1.5% | +0.6% |
| `+gvn` | −0.8% | +4.5% | +11.0% | +10.5% |
| `+licm` | +5.3% | +6.2% | +57.9% | +56.0% |
| `+sroa` | −1.6% | −0.4% | −0.6% | +0.1% |
| `+all` | +2.6% | +7.6% | +58.3% | +60.7% |
| `-all` | +34.2% | +66.7% | +26.9% | +52.5% |

The default `--tier1` took 1.30 to 1.32 times clang -O2's user cycles on
speedtest1 and 1.19 to 1.21 times on Lua. Head's `--tier1-passes=-merge`
writes main's objects, so the `-merge` row is head against main.

What the measurements decided:
- **SCCP, dead-code removal and the inlining** pay at run time, and turning
  them off makes the compile slower as well. All the default passes off cost
  34% of speedtest1's cycles and 27% of Lua's.
- **CFG cleanup's threading** is neutral at run time, since the layout already
  makes a jump to the next block a fall-through. It saves 0.6% of the compile.
- **The merges** stay on. They save 0.6% of the compile, take 0.35% from
  `sqlite3.c`'s Tier 1 text (1,100,264 to 1,096,408 bytes) and 439 call-save
  stores from its 15,424. Their run-time change, 0.1% fewer instructions, is
  inside code placement's noise either way.
- **#866's solver** stays off: Lua runs 11% more cycles. Its class
  representatives lengthen live ranges that the allocator then spills, as the
  value-numbering guide's earlier measurement found. It also costs 32% of the
  compile.
- **LICM** stays off: Lua runs 58% more cycles. Its moved values live across
  their loops, where they spill (#1189's breakdown).
- **SROA** stays off: at most 1.6% fewer cycles, inside the noise, with no
  fewer instructions, for 32% more compile. It splits the driver's one-slot
  scalars, which promotion takes anyway, and the aggregates that matter escape
  to calls until the inlining removes those calls. The inlining's expansions
  already run it where they copy aggregates.
- **Repetition:** no pass runs twice. The optimizer cleans up after the passes
  before it, the inlining's summaries are optimized once each, and nothing
  measured here would gain from a second round.

### What stays open

Each item #139's design or guide named that this close-out doesn't finish has
an issue of its own:
- **Value numbering with memory** (repeated loads, store-to-load forwarding):
  #2718 ([its guide](ir-memory-value-numbering.md)).
- **A policy under which LICM and the solver pay** (register pressure, values
  Tier 1 recomputes kept at their users): #2719.
- **Inlining callees with control flow, its classes and budgets:** #2541 and its
  steps. A variadic or unprototyped call is the call's decision, not the unit's
  error (#2721).

## Algorithm sources and provenance

The implementation and fixtures are original C under the project default
license. The algorithm references are Wegman and Zadeck,
[Constant Propagation with Conditional Branches](https://cs.brown.edu/research/pubs/techreports/reports/CS-91-22.html)
(1991), and Alpern, Wegman and Zadeck,
[Detecting Equality of Variables in Programs](https://doi.org/10.1145/73560.73561)
(1988). The papers supply algorithmic background; their code, figures and
fixtures are not incorporated.
