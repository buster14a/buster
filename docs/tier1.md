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

## Vector and opmask handoff

[#732](https://github.com/buster14a/buster/issues/732) supplies verified
`v4i32`/`v8i32`/`v16i32` values, distinct `m8`/`m16`/`m32`/`m64` masks,
canonical text, scalar execution oracles and the bounded #717 VECTOR/X86
producer adapter. [Its contract](ir-simd.md) defines comparisons, merging/zeroing
selection, exact-width memory effects, target eligibility and ABI refusals.
The shared interpreter keeps wide payloads only for vector-producing values.

[#141](https://github.com/buster14a/buster/issues/141) must select these values
using the generated target, retain memory effects and publish vector/mask
register constraints. [#179](https://github.com/buster14a/buster/issues/179)
allocates the distinct opmask class: `tier1_allocate.c` and `tier1_cfg.c` take
it as their third class, k1 to k7 for values and k0 the class's scratch, calls
clobbering all of them, and `tier1_cfg_verify` checks it; `tier1_cfg_ir.c`
models the IR's masks with it, and `test-tier1-cfg-source --simd` allocates
real C's `vcmp` masks ([the allocation guide](tier1-allocation.md)'s "The
opmask class"). The selector takes masks' operations, and the CFG consumer
spills and reloads a mask at its width, moves it along CFG edges with `kmov` and
saves it around calls ([the selection guide](tier1-selection.md)'s "AVX-512's
masks"): `test-tier1-masks` runs hand-written IR of masks natively against the
shared IR's interpreter. Neither consumer may reinterpret a mask as an ordinary
GPR value or infer target permission from the compiler host. SSA promotion and
SCCP/DCE/CFG take a function whose only SIMD values are masks, as the typed
stream's mask class and the `_k*` builtins' rows lower (#179, [the IR SIMD
guide](ir-simd.md)'s "Masks from the typed stream"), and refuse one with vectors;
GVN, LICM, SROA and the inliner still refuse every SIMD function, and selection
refuses vectors; this handoff does not claim native Tier 1 vector
execution, full vector ABI support or a throughput gain.

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
| Frame layout | [#177](https://github.com/buster14a/buster/issues/177) | Valid slot sharing, call alignment, variadic/over-aligned ABI cross-linking and program results; unwind integration is done (the driver's unwind tables) |
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

## The C driver's `--tier1`

`./build/compiler --tier1 [options] file.c...`
compiles each function the pipeline takes by Tier 1 and every other function by
Tier 0, as without the option, into one System V program
(`c_check_tier1.c`, #138's piece for #140). Each function's typed stream is
lowered to the shared IR, promoted to SSA, optimized (SCCP, dead code and CFG
cleanup), its CFG and dominators analyzed, selected and emitted, the CFG
consumer allocating it over its CFG (`tier1_cfg.c`); with `--scalar`, every step
by its scalar twin. Its records, branch columns and references take Tier 0's
place in the emission (`check_program_emit_tier1`), on the emission's lanes,
each lane with arenas of its own for the pipeline. `--tier1-report` prints, on
standard error after the command, how many functions each tier compiled and the
step, and for the selector the IR operation, that refused each of the others,
and `--tier1-refusals` a line for each function Tier 0 compiled, its name and
that step (`tier1-refused	NAME	selection	call`). The report also sums Tier
1's code: its records, its spills' stores and reloads, the saves and restores
around its calls and fixed registers' sequences, its recomputed values and the
definitions of them it left out, its edges' moves, its spill slots, its
frames' bytes, and its unwind tables' shapes: open frames, returns published as
tails and frames in the red zone (`open_frames`, `return_tails`, `red_zones`). `--tier1-no-rematerialize` compiles Tier 1's functions spilling
what they would recompute (#178's comparison: [the allocation
guide](tier1-allocation.md)'s "Rematerialization"):

```text
tier1	functions=2709	tier1=2207	...	selection=499	...	selection.call=369	...
```

`--tier1-gvn` runs #866's congruence solver on the lowered IR, before
promotion, so that promotion's phi placement decides what its merged values
need. It is a measurement option, off by default, and the measurement says to
keep it off: its class representatives lengthen live ranges that Tier 1's
allocation pays for in moves and spills.

The figures are from two hosts, `BUSTER_DEBUG_INFO=0`, Clang 23, one pinned
CPU: a Ryzen 9 7940HS (Zen 4) and the approved Ryzen 7 9700X (Zen 5) under its
shared lock. The source is a local `sqlite3.c` of 2,604 functions (2,155 of them
Tier 1's), not the frozen file [the value-numbering guide](ir-value-numbering.md)
measures at 2,602, so the counts aren't comparable across records. On the
7940HS the object's text grows from 1,283,627 to 1,439,515 bytes (+12.1%) and
five compiles' median wall time from 525 to 698 ms in one pair of runs and 751
to 910 ms in another (+33.0% and +21.2%; that host is shared, so its time is a
range). On the 9700X, at the code head `fe3f3c79` — this head with `main` merged
again, whose wider selection takes 2,171 of the 2,604 functions rather than
2,155 — the text
grows from 1,440,195 to 1,613,155 bytes (+12.0%) and the median of four clean
compiles from 363 ms to 487 ms (+34.2%), the first run's figure discarded
because it includes the wait for the CI job that held the lock. The two hosts'
byte totals differ because their trees are different commits; the relative
change agrees (12.0% against 12.1%), which is what the guide claims.

On c-testsuite's 430 functions Tier 1 still compiles the same
350, with the objects 1.2% smaller (347,912 to 343,624 bytes) but 13.9% more
instructions (9,136 to 10,402), 25 spill stores and 25 reloads where it had
none, and 146 edge moves where it had 113; no program fails to compile either
way. Running it after `ir_optimize` instead is worse still (text +15.8%,
instructions +40%, 2.5 times the spill traffic), and the pipeline's cleanup
after it does not recover the regression (205,229 against 205,269
instructions, 0.02%), so the cost is the merge rather than dead values. Tier 1
therefore doesn't route C source through GVN until a variant accounts for
allocation cost. [The measurement record](https://github.com/buster14a/buster-performance/tree/126886e46a23f095709bffb36c3c57aaafac35b3/docs/measurements/gvn-866-tier1-2026-11-01)
has the commands, the host and the raw logs; [the value-numbering
guide](ir-value-numbering.md) has the rest of the figures.

Tier 1's functions have unwind tables as Tier 0's do, by default and under the
same options ([the unwind guide](unwind.md)'s "The C driver's unwind tables"):
their frames are Tier 0's frame protocol (callee-saved registers pushed, one
subtraction, with a frame pointer `push rbp; mov rbp, rsp` first and a
realignment after the subtraction), which Tier 1's code publishes to the
emission (`check_tier1_frame`), its early returns' epilogues as teardowns the
tables remember and restore around, and a function whose code doesn't end in its
last epilogue (a loop's latch last) as an open frame ([the frame
guide](tier1-frame.md)'s "Unwind tables", #177). The driver refuses what Tier
1's code can't carry yet: `-g`'s rows. Several source files compile as without
the option, each unit's report added to the command's.

Beyond the straight-line subset, the CFG consumer takes frame objects'
addresses (each object a frame slot live throughout, its `lea` re-based on the
frame's layout), symbols' addresses (`lea` from `rip`, or a load of the GOT slot
where the emission's access column says so; thread-locals are Tier 0's),
parameters anywhere in the entry block and narrow ones (zero-extended into
their canonical storage at the entry), variadic calls of integer arguments (`al`
zero), calls through a pointer (the callee moved into `r10` with the
arguments), narrow call results and returns, and division, remainder and shifts
by a register (`tier1_emit_cfg_fixed`: the divisor or the shifted value through
the scratch register, x86's `rax` and `rdx` or `cl` taken after the operands are
read, and any other value live past the node there saved around it in a
call-save row). A division or remainder by a constant that isn't a power of two
is a multiplication by its reciprocal and shifts (#141, Granlund and Montgomery's:
the [selection guide](tier1-selection.md)), whose `mul` or `imul` takes `rax` and
`rdx` the same way, in either consumer. A parameter's register binds its definition's interval alone
(`Tier1CfgInput.definition_registers`); where that leaves a use in the interval
no register, the allocation runs again with every parameter free, and the entry's
moves bring each from its incoming register (`tier1_emit_cfg_entry`). Freeing a
parameter live across a call there too, in a callee-saved register from the
entry on, cut the saves around speedtest1's calls but spilled more and ran
slower, so a bound parameter stays bound. Values live across a call take the
callee-saved registers (`rbx`, `r12` to `r15`), which the prologue saves once,
rather than caller-saved ones that every call saves and restores around itself
(`tier1_emit_cfg_callee`: from the first allocation's intervals, each value whose
interval covers a call is allocated again with the callee-saved registers alone,
every other with the caller-saved ones; the first allocation stands where the
second fails). On speedtest1 that halved the saves around calls (41,464 to 20,710
records) and the spills' stores and reloads (15,028 to 7,550). A constant, a
symbol's address or a frame object's address, or a copy of one, is recomputed
where the allocator takes it out of a register, rather than stored and reloaded
(#178), which took a third of those stores and reloads again, with no change in
speedtest1's run time. Labels and indirect jumps (#789) go through the CFG
consumer too: an indirect jump's edge moves at each label's block, or in a stub
after the jump, and each label's symbol where the jump enters it. So do variadic
definitions (#789): the frame's register save area, which the prologue fills
from the argument registers, and the overflow area past the incoming stack
arguments. Still refused: floating point, narrow call arguments (the IR doesn't
keep their signedness, which the callee's extension needs), stack arguments,
switches and aggregates by value.

`test-c-testsuite --tier1` runs c-testsuite's programs so, and
`test-differential --tier1-arg --tier1` #142's campaigns.

Measured on the 9700X ([step 5's
record](https://github.com/buster14a/buster-performance/tree/1d3f174/docs/measurements/tier1-cfg-allocation-140-2026-10-31/step5)
and [step
6's](https://github.com/buster14a/buster-performance/tree/ca1c5a8/docs/measurements/tier1-cfg-allocation-140-2026-10-31/step6)):
speedtest1, with 2,208 of its 2,709 functions by Tier 1, gives Tier 0's
verification hash by both paths and runs in 226.9 ms, against Tier 0's 257.1
ms, clang -O0's 293.6 ms and clang -O2's 131.4 ms. Before the selection's walks
of C's truth values, addresses and divisions by powers of two (#141's items in
`tier1_select.c`) it ran in 285.7 ms. By sampled cycles, the functions Tier 1
compiles take 26% fewer as its code than as Tier 0's (41% fewer than before the
walks). What still costs: a narrow value's zero extension is a `movzx`, though
its register is zero-extended already; `&&`, `||` and `?:` results whose depth
slot carries values of several types stay in memory (single-type ones promote
since #983); a frame
object's address is a `lea` rather than part of its users' memory operands; the
pipeline has no loop-invariant motion; and `sqlite3VdbeExec`, the hottest
function, is Tier 0's (its switch). 80 of #142's programs, 40 per generator, gave
66 passes at both tiers and 14 failures, all in clang's own steps, and 40 more by
the scalar path alone 36 passes and 4 such failures. The default path's compile
time is unchanged, and `--tier1 -c` of `sqlite3.c` takes 371 ms (387 ms before
the walks, which leave 40% fewer records to emit); the checker's unit, which
compiles Tier 1's modules for the driver, takes 40.0 s to build against 36.6 to
36.9 s without them.

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
scalar values in straight-line block chains, plus byte/word memory and integer
conversions with canonical 32-bit storage. It emits actual spill stores,
reloads, constant rematerialization and stack probes. A chain may end in a
return, explicit unreachable, or an empty self-loop sink retaining its actual
branch. Every other CFG, with loops and phis, goes through its CFG path
(`tier1_emit_cfg.c`, #140): [`tier1_cfg.c`](tier1-allocation.md) allocates the
selected uses over the CFG, and the records carry each edge's moves, critical
edges' in stubs. Indirect/variadic/wide-ABI calls and floating-point emission
remain outside this bounded consumer. The selector's uniform-register API has
its own broader branch subset and explicitly refuses unsupported allocations
and phis.

Ordinary direct calls accept up to six register-passed `I32`, `I64` or pointer
arguments and a void or matching scalar result. The native consumer preserves
live caller registers, resolves argument cycles, aligns the stack and publishes
ordinary symbolic CALL references. Independent hostile helpers test clobbers,
return widths, memory effects and frame safety. Full builtin/source acceptance
requires integrating the source owner's exact helper and checked-call path.

`test-tier1-pipeline --cross-check` starts with eight original typed-stream
fixtures and runs ABI assignment, lowering, SSA promotion, optimization,
selection, liveness, allocation, frame emission, branch sizing and encoding.
It freezes interpreter expectations before compilation and also compares
Clang-built C formulas. A complete cross-check expects 1,920 native results,
960 Clang results, twelve emitted direct calls, 28 functions through the CFG
path and two explicit unsupported refusals. These are bounded integration fixtures, described in
the [pipeline guide](tier1-pipeline.md), not a source-corpus campaign.
Source byte loops now have a CFG/phi consumer, but no source corpus is compiled
through it yet. Source floating-point helper
adapters additionally need addressable frame locals, stack addresses, FP value
transport/conversions and their ABI. Raw integer-bit helper-call acceptance
does not establish those producer paths.
The frozen source overflow fixture also retains an address-taken byte local;
narrow memory/conversion support alone does not provide its `IR_STACK_ADDR`
frame object.

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
