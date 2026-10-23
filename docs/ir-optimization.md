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

SCCP tracks a state and two-word constant per instruction and an executable
byte per deduplicated CFG edge. It sweeps executable blocks in reverse
postorder, with 16-instruction transfer batches; the scalar solver queues
changed SSA users and instructions affected by newly executable edges. Integer
operations support all IR integer widths, including scalar i128 handling.
Copies, phis, selects, integer/pointer conversions, bitcasts and floating sign
negation preserve exact constant bits. Floating arithmetic and numeric
floating conversions remain overdefined: the IR does not yet carry a
floating-environment or rounding-mode contract that permits compile-time
evaluation. Unknown values remaining in an executable region are promoted to
overdefined and propagated before rewriting.

Dead-code removal classifies roots by a vector opcode-table lookup, skips
empty liveness batches, and propagates dependencies backwards. A separate
worklist implementation supplies the exact scalar result and completes a
bounded sweep. It skips dependencies of folded values and removed conditional
branches, and follows only executable phi edges.

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
enumerator, opcode tables, lattice or fold functions. A separate i128 fixture
checks a known cross-limb multiplication result. The interpreter is a test
oracle for these fixtures, not the complete interpreter tracked by #176.

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
`benchmark-ir-optimize-passes`, after the tests, pinned to one allowed CPU,
each also with `--only vector --after-scalar 200`; like the others, their
measurements are informational.

The output reports nanoseconds, TSC ticks (not core cycles), useful input
instructions per second, output size and retained analysis workspace. That
workspace count excludes temporary verifier and dominance allocations; it is
not a peak-memory measurement. No benchmark result or speed claim is recorded
yet. Local ASan/UBSan diagnostic checks passed 189,731 checks with zero
failures at the initial pass-group checkpoint. Entry-phi refusal regression
was added after that checkpoint; target-host results and final
revision validation must be recorded separately.

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

- Floating numeric evaluation needs an explicit environment contract.
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
