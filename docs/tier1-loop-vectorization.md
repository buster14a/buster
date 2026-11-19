# Tier 1 loop vectorization for array loops (#2743): design

Status: the first implementation is `ir_vectorize_loops.c`, behind
`--tier1-passes=+vectorize` ("The first implementation" below, with its 9700X
results); the rest of this document is the design it follows. The ranking
measurements are main `c471f32f0136bbb21cfe083c9947f86346de2a0a` on the 9700X
(Release, CPU 2, under the lock), except where the desktop's preliminary Zen 4
result is named. The 9700X's x86-64 runs cover four builds at four placements.
Its completed znver5 short run covers six builds as linked, including both
explicit-vector builds and clang's scalar comparisons; the other build
configurations were not run.
The measurement sources, raw lines, counters, placements and tables are in
[the record](https://github.com/buster14a/buster-performance/tree/21eb224efc366eaa1ca1bb7a1426d95fcdc9fc2b/docs/measurements/tier1-array-loops-2743-2026-11-14)
(buster-performance#535). The issue is
[#2743](https://github.com/buster14a/buster/issues/2743).
[#2750](https://github.com/buster14a/buster/issues/2750)'s independent fix for
inlined by-value vectors is implemented in #2752; it is a prerequisite for
good vector code below AVX-512, not an implementation of this design.

**In one paragraph.** On the 9700X at `-march=x86-64`, `--tier1` takes 3.32
times clang -O2's time, geometrically averaged over 19 array loops that clang
vectorizes, and 1.08 times on six scalar controls; at `-march=znver5` those
ratios are 17.50 and 1.09. Taking clang's pieces apart puts 2.18 of the x86-64
gap on vectorization and 1.41 on unrolling, and 9.36 and 1.64 respectively at
znver5. Tier 1 is only 1.08 and 1.14 times clang with its vectorizer and
unroller disabled. On the completed znver5 explicit-vector probe, Tier 1 runs
12 vector loops 13.53 times faster than the same C loops, but still takes
1.29 times clang compiling the same vector source. The largest unowned lever
is therefore a loop vectorizer over the existing vector IR, with the measured
same-source back-end residual tracked separately rather than mistaken for the
main gap. This design recognizes countable innermost natural loops and affine
array accesses in parallel over IR rows, clones the accepted body into vector
lanes, guards it with exact trip count and run-time overlap checks, and leaves
the original loop as the scalar fallback. Integer reductions use the existing
reduction operations; strict floating-point reductions are never reassociated.
The data-parallel analyses are checked against their definitions, and every
fixture's result against a frozen digest and the interpreter (#2924 deleted
the exact scalar twin that was their oracle).

## The first implementation

`ir_vectorize_loops(function, options, output, scratch, result)` runs in
Tier 1's pipeline after if-conversion and LICM, before the optimizer, with
`--tier1-passes=+vectorize` (and `+all`), and gives `--tier1-report` its
`tier1-vectorize` line. It takes the IR as the passes before it leave it, not
the optimizer's cleaned form: the lowering's `sext (const)` chains are
evaluated as constants, `inttoptr`/`ptrtoint` pairs and copies are seen through,
rows no root needs are ignored, if-conversion's unreachable stubs stay where they
are, and the body may be a chain of blocks. Its own verification is therefore
structural; the optimizer that follows verifies dominance once it has removed
the stubs, and `test-ir-vectorize` runs that optimizer on every result.

**What it takes.** An innermost, reducible loop with one preheader whose only
successor is the header; a header of phis, its exit test and dead rows; a body
that leads back to the header through a chain of blocks, optionally containing
a min/max reduction diamond. The induction is an `i32` or
`i64` phi from an invariant start, `+ 1` on the back edge; the exit test is
`iv < n` or `iv + k < n` (unsigned or signed, either operand order and either
branch sense) against an invariant `n`. Every other header phi must be an
integer reduction: one wrapping `add`, `mul`, `and`, `or` or `xor` of the phi
and another value at `i32` or `i64`, or a signed or unsigned `i32` min/max.
The reduction's partial value must be private to its update. For min/max, the
vectorizer matches the two operands of an ordered comparison to the values
selected by a scalar `select` or the join of a one- or two-armed conditional.
An arm may reload the element already compared when its address and the
absence of an intervening write prove it reads the same value. Volatile
accesses and conditional stores refuse the loop.
Each needed row of the body is then, in order:

- **uniform:** no lane depends on the induction. It is cloned once, in the guard
  block where LICM would speculate it, else in the vector loop. A uniform load
  is admitted only in a loop without stores; a uniform floating operation never.
- **an index:** the induction, `sext`/`zext` of an `i32` induction (no wrap where
  the exit test bounds it), or 64-bit `add`, `sub`, `mul`/`shl` by a constant of
  an index and a uniform: affine modulo 2^64, as the address is.
- **a pointer:** an `address` of a uniform base and an index, or of a pointer and
  a uniform, and their integer conversions, advancing by a constant number of
  bytes per iteration.
- **a vector:** a load or store whose pointer advances by its access's bytes;
  element-wise integer `add` to `ashr` (not divisions) and floating `fadd` to
  `fdiv`, of vectors and uniforms (splatted); `icmp` into a mask; `select` on
  one; `zext`/`sext` of a mask (a `vselect` of 1 or -1 and 0); `zext`, `sext` and
  `trunc` between integer lanes (`extend`, `truncate`); `popcnt` of each lane
  (`lane_unary`).

Anything else refuses the loop, counted by reason: `shape` (unmatched control
flow in the body, no dedicated preheader, an outer loop), `induction`, `phi` (a recurrence
that isn't a reduction, a float sum), `operation` (a call, a division, a fence,
a volatile or atomic access, `fma`, `sitofp`), `address` (a strided or gathered
access), `use` (the induction as data, a reduction's partial value),
`dependence` and `width`.

**Dependences.** A load of the store's own address is in place, element by
element, and needs nothing. Two accesses of one base at other addresses (a
prefix sum's `a[i]` and `a[i - 1]`) refuse: a run-time test would find them
overlapping on every call. Every other pair of a store and another access gets
an exact test in the guard: lane 0's pointers at the start, `d = b - a` and
`a - b` modulo 2^64 against each range's `vector_n` elements' bytes, so that no
range's end can wrap; with any test, `vector_n` must also be below 2^56. More
than eight pairs refuse.

**The rewrite** appends three blocks per loop and leaves every original row in
place: the guard (G), reached from the preheader instead of the header; the
vector loop (VB); and its exit (VX), which reduces each accumulator (`reduce`,
then `lane_extract` of lane 0) and enters
the original header, whose phis take G's start or VX's results. Min/max
broadcasts the incoming accumulator into every lane in G and uses `lane_op`
in VB. The other reductions start at their identity and combine their scalar
result with the incoming value in VX. The original
loop is the remainder and the fallback at every target; the AVX-512 masked
remainder is not implemented. The vector loop's induction is lane 0's, the
original's type, stepping by the lanes to `start + vector_n` by the original's
predicate.

**Widths.** The lanes per iteration are the widest whose vectors are all 16 to
64 bytes and at which every row's operation is the target's.
`ir_vectorize_target.c` derives that from Tier 1's own forms and the target's
allow-list (`tier1_select_lane_form`, the compare and blend tables, the
conversions', reductions' and lane operations' forms), so the pass never emits a
row whose emission would send the function to Tier 0. Below AVX-512 that is the
element-wise operations and splats at 16 bytes (SSE2) or 32 (AVX2); masks,
conversions between lane widths, reductions and population counts are EVEX's.

**Data-parallel part.** Every row's admission kind is one two-register byte
lookup of its opcode per 64 rows, with the volatile and atomic flags folded in
(`ir_vectorize_kinds`), and each loop's blocks are scanned 64 rows at a time
for a refusing kind (`ir_vectorize_forbidden`). `test-ir-vectorize` checks both
against their definitions on random columns (#2924, which deleted their exact
scalar twins). The classification, which reads each row's operands' classes in
order, and the emission are the ordered part. A function without a phi on a
back edge in block order walks no blocks.

**Finding the loops.** The pass builds no CFG and no dominator tree: one
iterative depth-first walk over the blocks' terminators marks the reachable
blocks, counts each block's reachable predecessors and collects each back edge's
header once. The admitted shape needs nothing more: a header (not the entry)
with exactly two reachable predecessors, one of its branch's targets starting a
chain back to the header, and the other predecessor ending in a `br` to it.
An internal conditional must reconverge at a private two-predecessor join,
directly or through one single-entry block on each side.
That loop is natural, reducible and innermost by construction: the only way into
the chain is from the header, including at a matched join. On sqlite3.c the CFG
and the dominators were 84% of the pass's time
by in-process cycle counters in a local build (890 of 2,602 functions have a
candidate phi); with the walk, the measured kernels' objects are byte for byte
the same. The report's `loops` count is every back edge's header, outer loops
included (they refuse as `shape`). A function none of whose loops has the
admitted shape (651 of sqlite3.c's 890 candidates) allocates no per-row arrays
and computes no kinds, and the candidate test reads each block's leading phis
alone.

### Results

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/46134556e/docs/measurements/tier1-loop-vectorizer-2743-2026-11-14)), on the 25 kernels of #2743's record and
its harness, as linked / at each build's best of four placements:

| Geometric mean of time ratios | znver5: 19 loops | the 12 vectorized | x86-64: 19 loops | the 5 vectorized |
| --- | ---: | ---: | ---: | ---: |
| Tier 1 / clang -O2 | 16.75 / 16.41 | 19.32 / 19.24 | 3.44 / 3.39 | 6.50 / 5.92 |
| Tier 1 + vectorize / clang -O2 | 3.44 / 3.23 | 1.53 / 1.47 | 2.31 / 2.31 | 1.36 / 1.38 |
| Tier 1 / Tier 1 + vectorize | 4.86 / 5.09 | 12.60 / 13.12 | 1.49 / 1.47 | 4.78 / 4.30 |

The six controls stay at 1.09 (znver5) and 1.08 (x86-64), and nothing the pass
refuses changes: without the option the objects are main's, and both paths give
one object. At znver5 the element-wise loops reach clang (`add_i32` 0.99,
`scale_u8` 0.82, `blend_u8` 0.86, `fill_u32` 1.04 times its time); the
reductions stay 1.65 to 2.82 times behind, clang's loops keeping four
accumulators where the vector loop has one chain (#2755's unroller) and using
`vpmaddwd` for `dot_i16`. At x86-64 only `add_i32`, `saxpy_f32`, `fill_u32`,
`copy_u8` and `matmul_f32`'s inner loop are taken: masks, reductions and lane
conversions are EVEX forms in Tier 1. The refusals that remain have issues:
`max_i32` (#2805), the contracted `fma` and `sitofp` (#2806), the masked
remainder (#2807), and strided accesses and narrow-into-wide counts (#2808).
Compiling sqlite3.c with the option cost 2.30% more user cycles at znver5 and
1.51% at x86-64 in the first build, which built a CFG and dominators for each
candidate function; with the terminator walk and no row work in functions
without an admitted loop, 1.04% and 0.44%. Without unit inlining on both sides
the pass costs 7.9 M instructions (+0.17%) at either target; the rest is the
pass running on inlined bodies and, at znver5, Tier 1's later stages on the
64-byte loop that inlining copies into `sqlite3VdbeExec`, about 61 M
instructions more than the same loop at 16 bytes (#2831).

## What the measurements rank

The suite has 25 loops over 8 KB arrays, without `restrict`, intrinsics or
pragmas. Nineteen are loops clang -O2 vectorizes: integer and byte sums, a
dot product, maximum, conditional sum, count, population count, an
array-of-structs sum, element-wise arithmetic, saxpy, scaling and blending,
clamp, integer-to-float conversion, fill, copy, a stencil, RGB to gray and
matrix multiply. Six controls remain scalar in clang: a strict float dot
product, prefix sum, histogram, byte search, table CRC and FNV-1a. The harness
is always clang -O2, checks a digest of every result, and times the best of
seven calibrated samples. At placement 0 it also subtracts a one-repetition
`perf stat` run from the calibrated run, giving user cycles and instructions
for the loop itself.

### The cause split on the 9700X, x86-64

Each ratio is a geometric mean over the named kernels; a larger number is
slower. “Best placement” takes each kernel's fastest of four link placements.
The controls have no vectorization to recover, so their near-unity ratios are
also guards against attributing a general front-end or allocator gap to loop
vectorization.

| Ratio | 19 vectorized, as linked | 19 vectorized, best placement | 6 controls, as linked | 6 controls, best placement |
| --- | ---: | ---: | ---: | ---: |
| Tier 1 / clang -O2 | 3.32 | 3.39 | 1.08 | 1.08 |
| Tier 1 / clang -O2, no vectorizer or unroller | 1.08 | 1.10 | 1.03 | 1.03 |
| clang without vectorizer or unroller / clang without vectorizer | 1.41 | 1.42 | 1.05 | 1.05 |
| clang without vectorizer / clang -O2 | 2.18 | 2.17 | 1.00 | 1.00 |

Two examples make the instruction consequence concrete. `add_i32` retires
6.009 instructions and 1.079 cycles per element through Tier 1, against
1.392 and 0.329 through clang -O2. `sum_i32` retires 5.006 and 1.066, against
3.262 and 0.501. The strict `dot_f32` control is 1.949 cycles through Tier 1
and 1.941 through clang, so it must stay scalar without permission to
reassociate.

### The cause split on the 9700X, znver5

The shorter znver5 run used the same kernels and counters as linked (placement
0). All 150 timing runs and 150 counter runs completed with status 0, and all
six builds produced the same digests. Tier 1 compiled all 25 C kernels and all
85 functions in the explicit-vector unit with no fallback.

| Ratio | 19 vectorized loops | 6 scalar controls |
| --- | ---: | ---: |
| Tier 1 / clang -O2 | 17.50 | 1.09 |
| Tier 1 / clang -O2, no vectorizer or unroller | 1.14 | 1.03 |
| clang without vectorizer or unroller / clang without vectorizer | 1.64 | 1.05 |
| clang without vectorizer / clang -O2 | 9.36 | 1.01 |

The wider target makes the missing vectorizer more valuable, not less. It
also leaves a small scalar-loop difference (1.14 against clang without its
vectorizer and unroller) that must not be hidden by reporting only the 17.50.

The design consequently ranks the work as follows:

1. **Loop vectorization:** the 2.18 vectorization component at x86-64 and
   9.36 at znver5, and the only change that can turn the suite's scalar memory
   and arithmetic rows into the vector rows Tier 1 already emits.
2. **Loop unrolling:** the separate 1.41 and 1.64 components in clang's
   scalar loops. It should be designed and measured on the vector loops after
   step 1, not hidden inside their legality rules. #2755's pass
   ([loop unrolling](tier1-loop-unrolling.md)) unrolls today's scalar counted
   loops and takes a vector main loop the same way; it asks this pass to mark
   its scalar remainders, which it then refuses.
3. **Back-end residuals in explicit vector loops:** the 1.29 same-source gap
   at znver5, including constants and splats, rematerialized loop bounds,
   unfolded loads, extra moves around masked operations and the clamp/max
   shapes. The owned issues remain separate: #1344, #2676, #2718 and #2719.
   This design emits IR; it does not take over their instruction-selection or
   allocation work, and does not claim the whole 1.29 for those issues without
   a further decomposition.
4. **Code placement:** not yet a rule. The desktop showed large losses when a
   tiny loop crossed a 64-byte line. The 9700X does not reproduce that simple
   rule: most Tier 1 loops vary by only a few percent, while `saxpy_f32`,
   `matmul_f32`, `clamp_i32` and `blend_u8` have outliers not explained by the
   recorded crossing bit. No loop alignment change is justified until those
   placements are repeated and their cause is isolated.

### The back end carries a first vectorizer, with a measured residual

`src/kernels_hand.c` in the record writes 12 of the loops as a vectorizer
would: whole AVX-512 vectors, a masked final partial vector, and a run-time
overlap check before loops that store. It does not unroll. On the 9700X at
znver5, geometrically over those 12:

| Ratio | Geometric mean |
| --- | ---: |
| Tier 1 on the C loop / Tier 1 on explicit vector source | 13.53 |
| Tier 1 on vector source / clang -O2 on the C loop | 1.34 |
| Tier 1 on vector source / clang -O2 on the same vector source | 1.29 |

This is the acceptance measurement for the design's premise: the existing
back end carries explicit vector loops at 1.29 times clang on identical
source, so an IR loop vectorizer addresses the dominant missing piece without
being assumed to close the whole gap. Clamp (2.51), maximum (1.84) and byte
sum (1.68) have the larger same-source residuals; the record keeps every row.
The earlier desktop znver4 probe was directionally similar (13.25, 0.94 and
1.10 respectively), but remains preliminary and is not used for acceptance.

#2750 explains a second prerequisite. Before #2752, Tier 1 copied an inlined
intrinsic wrapper's by-value vectors as 8-byte integers below AVX-512. SROA
could not promote those slots, and each whole-vector load conflicted with the
integer stores. With the target's own 16- and 32-byte moves used instead, the
9700X A/B removes 57 to 71 times the cycles from the two intrinsic probes at
x86-64 through x86-64-v3. Plain C kernels do not exercise that bug, so it does
not explain their 3.32; it does mean a vectorizer's SSE2 and AVX2 output no
longer inherits an avoidable memory wall when wrappers are inlined.

## The IR the pass consumes

After promotion and if-conversion, a typical accepted loop has:

- a preheader whose only successor is the header;
- a header with an `i64` induction phi and zero or more reduction phis;
- an unsigned exit comparison against a loop-invariant trip count;
- a body whose addresses are an invariant base plus the induction value times
  an element-sized scale, with explicit `address.ptr`, `load` and `store`;
- one latch that increments the induction value and branches to the header.

This is the shape already produced by `test-ir-source` for the suite, including
its redundant conversion chains before cleanup. The pass consumes the cleaned
SSA, not C syntax. Natural loops, latches, parents and membership come from
[dominators](dominators.md); irreducible loops and loops without a suitable
outside predecessor are refused, as they are for
[LICM](ir-loop-motion.md). (The first implementation found that only its chain
shape needs neither: see "Finding the loops" above.) The pass runs after if-conversion and LICM and before
`ir_optimize_verified`, so ordinary cleanup sees both the vector and fallback
forms. `--tier1-passes=+vectorize` first exposes it as a measurement option;
making it a default is a later step with its own before/after evidence.

The vector representation already exists. Vector types are 16-, 32- and
64-byte containers, and each element operation names `i8`, `i16`, `i32`,
`i64`, `f32` or `f64` lanes in its flags. Integer lane arithmetic, floating
lane arithmetic, `vcmp`, `vselect`, splats, width changes, conversions,
`masked_load`/`masked_store`, `reduce` and `lane_extract` are verified IR with
interpreter semantics ([the SIMD guide](ir-simd.md)). No vector opcode is
invented for this design. Below AVX-512, comparisons and selects use vector
carriers and the target's VEX/SSE forms; masks are not fabricated where the
target has none.

## Legality: recognize a small class exactly

The first implementation accepts only innermost, reducible, countable loops.
A candidate must satisfy every rule; otherwise it remains unchanged and a
report counter names the refusal. There is no best-effort partial vector body.

### Induction and trip count

The induction phi starts at zero or another invariant constant, advances by a
positive constant, and has no non-canonical users that observe a different
update order. The initial form is the suite's `i64` zero-based induction with
an `icmp ult` exit. Other widths, signed bounds, multiple latches, early
breaks and unknown strides are later extensions, each with their own overflow
proofs.

Compute the vector trip count only in the preheader. All division, rounding,
range calculation and end-pointer arithmetic must be proved not to wrap for
the actual scalar trip count. The guard selects the original loop when the
count is zero or the calculation cannot be represented. Neither the main
vector loop nor its tail may touch an element the scalar loop would not touch.

### Memory and dependence

Each load or store must be `base + induction * scale + constant_offset`, with
an invariant base, an element-sized scale and a width that divides the chosen
vector width. In the first step, all memory rows in one loop use the same
element width and stride one. Stencils, negative strides, interleaved groups,
gathers, scatters and outer-loop vectorization are out of scope.

Read-only loops need no overlap check. For a loop that stores, an exact same
base and same per-iteration address is admitted for in-place element-wise
operations. Every other store-to-load base pair is either proved independent
by existing IR facts or guarded in the preheader. The guard compares the full
half-open byte ranges, including their overflow checks, and branches to the
scalar loop on overlap. A load may cross neither end of its proven object
without a guard, regardless of alignment.

A loop-carried scalar dependence is admitted only as a recognized reduction.
A loop-carried memory dependence is not: prefix sums, histograms and scatters
stay scalar. Volatile and ordered accesses, calls, atomics, irreducible
control flow and an address the analysis cannot classify all refuse.

### Operations and floating point

The initial whitelist is element-wise wrapping integer arithmetic, bitwise
operations, comparisons and selects, exact lane conversions, and `fadd`,
`fsub` and `fmul` in their original per-element order. Every operation must
have defined vector-lane semantics in the IR and a legal lowering at the
selected target and width. An operation available only at a narrower width
causes narrowing or refusal, never an accidental target-feature escalation.

Integer `add`, `mul`, bitwise and minimum/maximum reductions are admitted when
their phi and update form one associative wrapping or select operation and the
existing `reduce` operation covers the lane kind. Narrow reductions may be
widened only with a proof that this preserves C's promoted arithmetic and
final conversion. Float and double reductions are refused: the pass does not
reassociate, enable fast math, change rounding, or discard observable floating
exceptions. Element-wise floating vectorization keeps each element's operation
sequence and the IR's existing runtime-environment rules.

## The rewrite

For trip count `n` and vector lane count `w`, the preheader computes
`vector_n = n - n % w` with the guards above. The accepted loop becomes three
regions:

1. the preheader's guards and invariant vector constants;
2. a vector main loop over `[0, vector_n)`;
3. the untouched scalar loop for the remaining elements and for every guard
   failure.

Sharing the original scalar loop as both remainder and fallback avoids two
copies whose legality could diverge. Its entry phis receive either zero or the
vector loop's final scalar values. Reduction results are reduced only on the
vector exit edge, then joined with the scalar remainder's phi.

The vector body replaces each affine scalar address with its corresponding
vector address, splats invariant operands, and applies the same operation with
lane metadata. It does not load `w` scalar elements and pack them. Integer
reduction phis become vector accumulators with the operation's identity;
`reduce` and `lane_extract` run after the main loop. The first step uses a
scalar remainder at every target. A later step replaces it at AVX-512 targets
with one `masked_load`/operation/`masked_store` iteration whose mask is exactly
the low `n - vector_n` lanes. No tail is masked at SSE2 or AVX2, where the IR
has no architectural mask to give it.

Width selection starts from the target's legal operations and moves, not from
the widest register alone: 16 bytes at x86-64, 32 with AVX2, and 64 with
AVX-512F where the lane operations support them. The cost input is the same
kernel suite's 9700X cycles per element and the emitted instruction rows. A
narrower vector is selected when a wider form needs scalarization, excessive
lane extension or a masked operation the target lacks. Set-up and tail costs
are included; short-trip-count profitability is measured separately from the
8 KB steady-state suite before the pass becomes a default.

## A data-parallel implementation

Add `ir_vectorize_loops.c` beside the other optimization passes and follow the
shared immutable-input, fresh-output, verified-result and arena-rollback
contract in [IR optimization](ir-optimization.md). Analysis uses columns, not
an AST walk:

- Flatten each loop's instructions in RPO into row ranges. Opcode, type,
  flags, operands, block and loop membership remain parallel columns.
- Compare opcode and lane-metadata columns against the admission tables a
  vector width at a time. Masks identify candidate phis, affine addresses,
  memory rows, reductions and forbidden effects; compress collects only those
  rows for the smaller ordered checks.
- Recognize affine addresses and reduction cycles with bounded pointer
  chasing. A per-loop bitset marks rows participating in each candidate cycle,
  so a value with an unrecognized external use vetoes the whole candidate.
- Evaluate dependence pairs over compressed load/store rows. Base and offset
  IDs are compared in parallel; only distinct symbolic bases reach the ordered
  overlap-guard builder.
- Build the old-to-new value map with a prefix sum over accepted rows. Clone
  the vector body by lanes of rows, replacing opcodes' lane metadata and
  operands from that map. CFG creation, phi cycle resolution and publication
  of one loop at a time remain the small ordered part.

The design had a scalar twin that walked the same rows independently, and both
paths had to give byte-identical IR. #2924 deleted it: the scans are checked
against their definitions, and every fixture's result and counts at every
target against a digest frozen where the two agreed.

Report `vectorized_loops`, split by width and reduction kind, and refusal
counts by reason in `--tier1-report`. These counts make a silently narrowing
implementation visible in the driver fixtures instead of letting it pass as a
performance fluctuation.

## Verification and acceptance

### Semantics

- Add before/after text fixtures under `tests/ir/optimize/` for each admitted
  shape and each refusal: zero and partial trip counts, in-place operation,
  exact and partial overlap, guard arithmetic at address-space limits,
  induction overflow, unsupported strides, calls, volatile access, irreducible
  loops and strict float reductions.
- Run both implementations over every fixture and compare canonical output.
  Verify the result again after the ordinary optimizer.
- Interpret original and vectorized functions on identical memory for lengths
  around zero, one lane, two lanes and the tail boundary. Vary every base's
  relative placement, including exact and one-byte overlap, and compare all
  bytes, not only the returned value. Include NaNs, infinities, denormals and
  signed integer extrema.
- Compile and run driver fixtures at x86-64, x86-64-v2, x86-64-v3 and
  x86-64-v4, by both host paths. Compare with clang -O0 and with the scalar
  fallback. The fixture must check both that expected loops vectorize and that
  forbidden loops do not.
- Mutation tests remove or corrupt an overlap guard, tail bound, reduction
  identity and lane flag; the verifier or output oracle must reject every
  mutant. Tier 0 objects and builds with `-vectorize` remain byte-identical.

### Performance

Use the record's harness, not a new benchmark selected to favour the pass. On
the 9700X, publish every kernel's ns/element, cycles and instructions at all
four placements, all digests, compiler revision and object comparison. The
primary comparison is the 19-loop geometric mean against the recorded 3.32;
the six controls and strict `dot_f32` are regression guards. Also run the
explicit-vector probe, so remaining losses can be assigned to the vectorizer's
IR rather than guessed to be the back end.

Before enabling the pass by default, measure short trip counts and compile
cost on the compiler itself and `sqlite3.c`, including first-call and scalar
host paths. The acceptance claim must state both generated-program results and
compile-time cost. It must not redefine the expected kernel set after seeing
which loops improve.

## Staged delivery

1. **Analysis and fixtures:** natural-loop, induction, affine-access and
   refusal classification with no rewrite; both twins and report counters.
2. **Element-wise loops:** guarded vector main loop and shared scalar
   remainder at all four targets, then integer reductions covered directly by
   `reduce`.
3. **Coverage:** proven narrow reductions and conversions, followed by the
   AVX-512 masked remainder. Each addition keeps the same oracle and target
   matrix.
4. **Default and follow-ups:** enable only with the full 9700X result and
   compile-cost record. Design unrolling on the resulting vector loops
   separately (#2755). Keep #1344, #2676, #2718 and #2719 with their existing owners,
   and file placement or address-selection work only when the remaining
   znver5 evidence isolates it.

The intended result is not an “AVX-512 pass”. It is one legality and rewrite
implementation that emits the best legal width for each target, with the same
correctness oracle at x86-64 and x86-64-v4 alike.
