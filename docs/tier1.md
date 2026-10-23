# Tier 1 backend

Tier 1 is tracked by [#138](https://github.com/buster14a/buster/issues/138).
Its acceptance includes generated-program correctness, compilation cost and
run time on the approved Ryzen 7 9700X. A standalone analysis or allocator test
does not establish that the C compiler can compile and run a program at Tier 1.

## Integration order

The shared IR belongs to [#131](https://github.com/buster14a/buster/issues/131)
and local SSA construction to
[#134](https://github.com/buster14a/buster/issues/134). The backend consumes
those representations rather than creating another general-purpose IR.

1. Lower the checked function into the shared IR, build its compact CFG and
   verify it.
2. Compute dominators and dominance frontiers, promote eligible locals, and
   verify dominance and phi operands.
3. Run the enabled optimization passes. A pass that changes the CFG invalidates
   the CFG, dominance and loop analyses; verification follows each pass.
4. Plan instruction selection and publish the selected instructions' effective
   value uses. Folding an address or load can extend its operands' lifetimes,
   so allocation must use this plan's uses rather than the original IR alone.
5. Build liveness and intervals, allocate registers, resolve edge moves and
   plan spill slots and the frame. A rematerialized fragment carries its recipe
   instead of a spill slot.
6. Emit instruction records and symbolic references, preserving the plan's
   value, block and instruction identities. Optional scheduling must preserve
   register, flag, memory, fault and control dependencies.
7. Size branches, encode the records and publish them through the existing
   object/link representation. Generated ISA eligibility is independent of the
   compiler host's native ISA.

Every handoff needs exact logical lengths, explicit capacity or arena ownership,
and a defined failure result. Unsupported input must be reported before code is
published. An experimental pass may be disabled explicitly; a semantic error
must not silently select another compiler or another tier.

## Required evidence

The component issues retain their individual acceptance criteria:

| Component | Issue | Required consumer or program evidence |
| --- | --- | --- |
| Optimization passes | [#139](https://github.com/buster14a/buster/issues/139) | Verified IR and semantic agreement with each pass enabled and disabled; compilation cost and generated-program benefit |
| Register allocation | [#140](https://github.com/buster14a/buster/issues/140) | Valid register assignments, constraints and edge moves; spill counts, scalar fallback share and program results |
| Instruction selection | [#141](https://github.com/buster14a/buster/issues/141) | Target-valid records, executed results, instruction counts and compilation cost |
| Differential testing | [#142](https://github.com/buster14a/buster/issues/142) | Recorded real Csmith and YARPGen programs compiled by Buster at both tiers and Clang at both optimization levels |
| Scheduling | [#172](https://github.com/buster14a/buster/issues/172) | Dependency validity and measured program benefit relative to compilation cost; retain only if it pays |
| Dominators and loops | [#174](https://github.com/buster14a/buster/issues/174) | Agreement with independent definitions and actual SSA, optimizer and allocator consumers |
| Frame layout | [#177](https://github.com/buster14a/buster/issues/177) | Valid slot sharing, call alignment, variadic/over-aligned ABI cross-linking, unwind integration and program results |
| Rematerialization | [#178](https://github.com/buster14a/buster/issues/178) | Flags-safe recipes at actual uses, spill/frame reduction and generated-program correctness and cost |
| Zen 5 costs | [#181](https://github.com/buster14a/buster/issues/181) | Independent emitter coverage and real consumers; unknown measurements remain unknown |

The parent requires per-program results for `speedtest1` and the corpora, with
Buster Tier 1 and Tier 0, Clang `-O2` and `-O0`, and `tcc`. Record the exact
sources, compiler revisions and flags, generated-output checks, compilation
times and run times. Distinguish generated-code quality from host compiler
throughput. Kernel-only measurements and harness self-tests are labeled as
such and do not enter the completed-program count.

Every build, test, benchmark and profile on the shared 9700X takes
`~/buster-fast-bench/exclusive.sh` separately, with the owner, title and issue
link set. Timings are pinned to CPU 2. Repeat frozen-input comparisons in
alternating order across fresh processes; measure sustained vector execution
and execution following lighter work. CI retains priority between runs.

## Current integration boundary

The implementation now consumes the shared IR and the upstream local-SSA
adapter. The optimization slices cover SCCP/dead-code/CFG cleanup, value
numbering, conservative loop motion, private aggregate-field splitting and
bounded translation-unit inlining.
Their [optimization guide](ir-optimization.md) records unsupported forms and
the remaining transformation and measurement scope.

The [selection](tier1-selection.md), [selected-use liveness](tier1-liveness.md),
[allocation](tier1-allocation.md) and [frame](tier1-frame.md) modules are
connected by a native function consumer. Its fragment path supports 32/64-bit
scalar values in straight-line block chains, with actual spill stores,
reloads, constant rematerialization and stack probes. General CFG fragment
reconciliation, phis, indirect/variadic/wide-ABI calls and floating-point emission remain outside this
bounded consumer. The selector's uniform-register API has its own broader
branch subset and explicitly refuses unsupported allocations.

Ordinary direct calls accept up to six register-passed `I32`, `I64` or pointer
arguments and a void or matching scalar result. The native consumer preserves
live caller registers, resolves argument cycles, aligns the stack and publishes
ordinary symbolic CALL references. Independent hostile helpers test clobbers,
return widths, memory effects and frame safety. Full builtin/source acceptance
requires integrating the source owner's exact helper and checked-call path.

`test-tier1-pipeline --cross-check` starts with six original typed-stream
fixtures and runs ABI assignment, lowering, SSA promotion, optimization,
selection, liveness, allocation, frame emission, branch sizing and encoding.
It freezes interpreter expectations before compilation and also compares
Clang-built C formulas. A complete cross-check expects 768 native results,
384 Clang results, eight emitted direct calls and eight explicit unsupported
refusals. These are bounded integration fixtures, described in
the [pipeline guide](tier1-pipeline.md), not a source-corpus campaign.

The [emitted-case harness](tier1-model-measurements.md) accepts only complete
selector manifests, retains exact operand distinctions and refuses experiments
it cannot represent faithfully. Its current inventory has 566 eligible cases;
verification is construction/execution evidence, not a processor measurement.
This frozen inventory does not include the new native-call consumer records.
No unknown model column is filled by this integration.

At the start of the October 23, 2026 takeover, main
`a41ae0432ad30fbd1466ea41c1f595e385b5945a` contained the Tier 0 typed-stream
backend, calling-convention module, dataflow solvers and partial measured Zen 5
model. The general IR and SSA were being implemented by their own active
owners. The source-to-typed-function path was incomplete. The ordinary C driver
did not yet compile C to machine code at either tier.

Consequently no full Tier 1 corpus, SQLite run-time, real-generator differential
campaign or Tier 1 ABI acceptance is established by this document. Component
tests and subsequent integration evidence must identify their actual producer,
consumer and revision before this boundary can be advanced.
