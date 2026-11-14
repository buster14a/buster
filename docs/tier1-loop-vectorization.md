# Tier 1 loop vectorization for array loops (#2743): design

Status: design only. The measurements are main
`c471f32f0136bbb21cfe083c9947f86346de2a0a` on the 9700X (Release, CPU 2,
under the lock), except where the desktop's preliminary Zen 4 result is named.
The 9700X's x86-64 runs cover four builds at four placements. Its completed
znver5 short run covers six builds as linked, including both explicit-vector
builds and clang's scalar comparisons; the other build configurations were
not run.
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
An exact scalar twin is the oracle for every data-parallel analysis and
rewrite.

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
[LICM](ir-loop-motion.md). The pass runs after if-conversion and LICM and before
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

## A data-parallel implementation with an exact scalar twin

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

The scalar twin independently walks the same rows and constructs the same
fresh IR. It is not merely the vector implementation with a width of one.
Both paths must produce byte-identical canonical IR for every accepted and
refused fixture; a legitimate difference in internal discovery order may not
change block, value or guard order. A host without the full AVX-512 feature
set uses the scalar path, and generated target objects must be identical
between host paths.

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
