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
masks"): `test-tier1-masks` runs hand-written IR of masks, and of vectors (#141),
natively against the
shared IR's interpreter. Neither consumer may reinterpret a mask as an ordinary
GPR value or infer target permission from the compiler host. SSA promotion and
SCCP/DCE/CFG take a function whose only SIMD values are masks, as the typed
stream's mask class and the `_k*` builtins' rows lower (#179, [the IR SIMD
guide](ir-simd.md)'s "Masks from the typed stream"), and one with vectors
(#141), folding no vector; GVN and LICM still refuse every SIMD function. The
inliner and SROA take them (#1282): "Inlining", below. Selection takes vectors (#141, the selection guide's
"Vectors"), and claims no throughput gain for them.

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

## Optimization flags and backend selection (#1132)

**Tier 0 is the default regardless of `-O`.** The conventional Clang-compatible
`-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Oz`, `-Og`, `-Ofast` and `-O` options
currently configure preprocessing state (including `__OPTIMIZE__`,
`__OPTIMIZE_SIZE__` and `__NO_INLINE__`), **not** backend optimization passes.
In particular, `buster -O2 -c file.c` uses Tier 0, not the Tier 1 pipeline
benchmarked against Clang `-O2`. Macro-dependent source/header paths can
nonetheless produce different code between `-O0` and `-O2`.

Use `buster --tier1 -O2 -c file.c` explicitly to request Tier 1; `--tier1`
wins even with `-O0`, and a function unsupported by Tier 1 takes its
documented Tier 0 fallback unless `--tier1-strict` forbids that fallback.
`--tier1 -g` retains surviving source statements and proved value locations
("Debug information" below, #1114). The driver deliberately does not map
standard `-O` flags to Tier 1 until #138's correctness and compile-/run-time
gates and #1116's code-quality gates qualify it for ordinary Release builds.
The driver regression in `test-checker-driver` pins this selection policy;
changing the mapping requires updating that test and re-evaluating these gates.

## The C driver's `--tier1`

`./build/compiler --tier1 [options] file.c...`
compiles each function the pipeline takes by Tier 1 and every other function by
Tier 0, as without the option, into one System V program
(`c_check_tier1.c`, #138's piece for #140). Each function's typed stream is
lowered to the shared IR, promoted to SSA, optimized (SCCP, dead code and CFG
cleanup), whose final verification retains the CFG and dominators for selection
(#1055), selected and emitted, the CFG consumer allocating it over its CFG
(`tier1_cfg.c`); with `--scalar`, every step
by its scalar twin. Its records, branch columns and references take Tier 0's
place in the emission (`check_program_emit_tier1`), on the emission's lanes,
each lane with arenas of its own for the pipeline. `--tier1-report` prints, on
standard error after the command, how many functions each tier compiled and the
step, and for the selector the IR operation, that refused each of the others,
and `--tier1-refusals` a line for each function Tier 0 compiled, with its name,
stage, category, cause, original error and instruction/detail, refused
operation and fallback disposition. `--tier1-strict` requires `--tier1`: an
unsupported body that ordinary `--tier1` compiles with Tier 0 then fails
qualification, while resource and internal failures fail either mode. [Tier 1
failure outcomes](tier1-outcomes.md) has the policy, fields and rollback
contract. The report also sums Tier 1's code: its records, its spills' stores
and reloads, the saves and restores
around its calls and fixed registers' sequences, its recomputed values and the
definitions of them it left out, its edges' moves, its spill slots, its
frames' bytes, its unwind tables' shapes: open frames, returns published as
tails and frames in the red zone (`open_frames`, `return_tails`, `red_zones`),
and its machine rows and the moves around them into their roles' registers and
out of them (`machine_rows`, `row_moves`, #1283).
Its `requested`, `admitted`, `compiled`, `unsupported`, `resource_failures`,
`resource_retries`, `internal_failures` and `fallbacks` fields count the
selected, admitted and compiled bodies and the disposition of the rest.
`--tier1-no-rematerialize` compiles Tier 1's functions spilling
what they would recompute (#178's comparison: [the allocation
guide](tier1-allocation.md)'s "Rematerialization"), and `--tier1-no-lifetimes`
without their objects' lifetimes, every object a slot of its own (#177's
comparison: [the frame guide](tier1-frame.md)'s "Slot sharing between
objects"):

```text
tier1	functions=2709	tier1=2207	...	selection=499	...	selection.call=369	...
```

`--tier1-gvn` runs #866's congruence solver on the lowered IR, before
promotion, so that promotion's phi placement decides what its merged values
need. It is a measurement option, off by default, and the measurement says to
keep it off: its class representatives lengthen live ranges that Tier 1's
allocation pays for in moves and spills.

`--tier1-schedule=post` and `--tier1-schedule=pre` schedule Tier 1's code block
by block, after allocation on its machine records or before selection on its IR
(#172). They are measurement options too, off by default; [the scheduling
guide](tier1-scheduling.md) has their contracts and the 9700X's measurement of
what they save and cost.

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
allocation cost. #866's acyclic numbering later cut what the option adds to
`-c` of the frozen `sqlite3.c` from 127.1 to 74.1 ms on the 9700X, with
byte-identical objects, so the reason to keep it off stands. [The measurement record](https://github.com/buster14a/buster-performance/tree/126886e46a23f095709bffb36c3c57aaafac35b3/docs/measurements/gvn-866-tier1-2026-11-01)
has the commands, the host and the raw logs; [the value-numbering
guide](ir-value-numbering.md) has the rest of the figures.

Tier 1's functions have unwind tables as Tier 0's do, by default and under the
same options ([the unwind guide](unwind.md)'s "The C driver's unwind tables").
Tier 1's code is the emission's tier-neutral result (`EmittedCode`, #1056: [the
emission metadata guide](emission-metadata.md)), with its frame emitter's own
frame events (`tier1_frame_emit`: each push, pointing, allocation, release and
pop at the record that makes it, its early returns' epilogues bracketed by the
body's rules remembered and restored, a function whose code doesn't end in its
last epilogue an open frame), which the emission resolves after branch sizing
into the fixed-frame CFI where the frame is one and a row program otherwise ([the
frame guide](tier1-frame.md)'s "Unwind tables", #177). Several source files
compile as without the option, each unit's report added to the command's.

### Debug information

`--tier1 -g` carries original typed-entry and local identities through the IR,
optimization, emission and the final scheduler permutation (#1114). Its DWARF
has surviving statement rows and call-site rows, qualified source types,
parameters, locals and lexical scopes. Optimization may remove statements or
move their instructions; generated records and inline callee instances do not
acquire invented source identities. The unwind tables use their independent
frame events as before.

Value locations require concrete machine proof. Whole retained objects use a
verified frame base and their actual physical storage; shared rows need an
explicit single-block lifetime, complete initialization and overwrite bounds.
Normal scalar parameters and locals can use exact literal bytes or SSA values
established by the full CFG emitted-code certificate. Assignments in an entry
source block with no incoming backedge use ordered events; other assignments
require sparse source windows witnessed
by real reads or stores and surviving exact block identities. Existing semantic
phis can supply join and loop values from a register proved on every incoming
edge. Memory-only phi entries and unwitnessed paths remain unavailable. Fresh
block entries and repeated definitions discard older iteration copies before
publishing current values. The straight-line emitter also certifies
bounded integer kernels without calls, spills, rematerialization or
transitions, checking actual operand ownership and exact kernel bytes after
the allocation certificate. Decoded copies, compiler-row spills/reloads,
writes, call clobbers and CFG intersections determine where those values
remain available. At suspended calls, only preserved carriers are published.
Unsupported source bindings, split values and inline locals stay named and
typed where appropriate but have no guessed value. Debug metadata changes no
instructions or allocation decisions. The [debug guide](debug.md#optimized-tier-1-values)
records the exact admission rules and gaps.

`test-check-debug --cross-check` checks native and object-linked programs with
`llvm-dwarfdump --verify` and GDB. Its Tier 1 fixtures read retained objects,
shadowed names, loop values, snapshots and reassigned call results at explicit
stops, including a computed straight-line local, both branch arms and their
join, three scalar loop iterations, and the caller's frame while a callee is
running. Default, scalar,
pre/post scheduling, frame-pointer and no-rematerialization variants compare
values with native output and compare allocated bytes with a compile without
`-g`; lane variants require deterministic complete files. Direct unit fixtures
also check carrier joins/backedges, shared-row boundaries, final byte-range
publication and resource rollback.

Beyond the straight-line subset, the CFG consumer takes frame objects'
addresses (each object a frame slot live throughout, its `lea` re-based on the
frame's layout), symbols' addresses (`lea` from `rip`, or a load of the GOT slot
where the emission's access column says so; a thread-local's, as Tier 0's,
the thread pointer `fs:[0]` plus its offset: a TPOFF32 immediate for the
image's own, or the offset from its GOT slot for one that may be outside it,
which `test-checker-driver`'s thread-locals' fixture runs on five threads
against clang -O0's build),
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
second fails). A phi and its operands are one web there (#1216): where any of
them crosses a call, all take the callee-saved registers, so that a loop's back
edge can keep the phi's register rather than move between the two sets. On speedtest1 that halved the saves around calls (41,464 to 20,710
records) and the spills' stores and reloads (15,028 to 7,550). A constant, a
symbol's address or a frame object's address, or a copy of one, is recomputed
where the allocator takes it out of a register, rather than stored and reloaded
(#178), which took a third of those stores and reloads again, with no change in
speedtest1's run time. Labels and indirect jumps (#789) go through the CFG
consumer too: an indirect jump's edge moves at each label's block, or in a stub
after the jump, and each label's symbol where the jump enters it. So do variadic
definitions (#789): the frame's register save area, which the prologue fills
from the argument registers, and the overflow area past the incoming stack
arguments. A narrow call argument reaches Tier 1 as the `i32` the lowering
extends it to by its class, and a narrow parameter as an `i32` it truncates (#141,
the [IR guide](ir.md)'s "Typed-stream lowering"). Arguments and parameters past
the six registers (#141) go through the CFG consumer too: a call stores each
argument in its slot of the outgoing area, from wherever its value is, and the
entry loads each parameter from its incoming slot. Floating values (#141), `F32`
and `F64`, go through the CFG consumer too, in the vector class's registers by
SSE2's scalar forms: their arithmetic, comparisons, conversions, loads, stores,
phis, parameters, arguments and results ([the selection guide](tier1-selection.md)'s
"Floating values" and [the allocation guide](tier1-allocation.md)'s "The vector
class's floating values"). So do by-value aggregates' ABI records (#789): a call
passes each aggregate's bytes where its record says, in registers or in its
stack slots, and stores an aggregate result into its object. Where a call's
stack slots need rsp aligned past 16 bytes (a 32- or 64-byte vector's, an
over-aligned aggregate's), the frame realigns itself to the most any call needs,
and a dynamic frame rounds its allocations and calls' areas to it (#1204's
review); a function stores
each aggregate parameter's registers into a frame object at its entry, and
returns an aggregate's parts or copies it through the hidden pointer (the
selection guide's "By-value aggregates"). So does the dynamic stack (#141):
variable-length arrays' and `alloca`'s allocations, and the stack's position
saved and restored around their scopes, in a frame with a frame pointer whose
places are rbp's (or a realigned frame's r15's), each call's outgoing area taken
from rsp around it (the selection guide's "The dynamic stack"). So do the
128-bit integers (#141), as pairs of 64-bit halves ("The 128-bit integers",
below), and long double (#141), in frame objects of their own, computed on the
x87 stack (the selection guide's "Long double"), with aggregates returned in
the x87's registers, and the convention's rarer aggregate shapes (#141): a
part in a stack slot, a high or low eightbyte alone, an aggregate in a vector
register whole, and an aggregate it ignores. So do the IR's vectors (#141, the
selection guide's "Vectors"), within a function and passed or returned by value,
which the lowering makes aggregates in a vector register whole. Calls whose
register parts exceed the red zone's 14 eightbyte staging slots reserve one
frame object for the largest call (#1450), addressed through the stable frame
base even in a dynamic frame. Four or more 32-byte vector arguments therefore
work on both v3 and v4 policies; the checker driver's overflow-staging fixture
cross-links them with clang under `--verify`. A function that calls a function
that returns twice is Tier 0's (#1847): `__builtin_setjmp`'s callers since
#148, and since #1847 the callers of setjmp's library kin (the catalogue's
entries with clang's `j` attribute: `setjmp`, `_setjmp`, `__sigsetjmp`,
`sigsetjmp`, `savectx`, `vfork`, `getcontext` and `_setjmpex`) and of any
function a declaration gives `returns_twice` (`Tier0Function.calls_returns_twice`),
and the callers through a pointer whose function's first parameter is a
`jmp_buf`'s or a `sigjmp_buf`'s element (glibc's `struct __jmp_buf_tag *`), as
setjmp's is (`check_lower_indirect_twice`). C invokes `setjmp` by its name alone
(C17 7.13.1.1), and neither clang nor LLVM marks a call through a pointer
`returns_twice`; clang's -O0 code survives one only because it gives every value
its own stack home. A call through a pointer of any other type is an ordinary
call.
Tier 1 shares call-save rows and spill slots among values whose ranges don't
meet on the CFG, which has no edge from a `longjmp` back to the call, so a
second return would read another value's bytes; LLVM's StackSlotColoring stays
out of such a function for the same reason. Their `longjmp` restores the
callee-saved registers, and C's contexts for `setjmp` hold no value of Tier 0's
stacks across its call, so Tier 0's code needs nothing more. Such a caller's IR
lowers as any other's, so that the IR's corpus checks still take every function:
Tier 1's admission (`check_tier1_compile`) is what refuses it, `--tier1-refusals`
naming it at the lowering stage with `cause=admission error=2`, and neither
inliner takes it as a callee, as LLVM's `isInlineViable` doesn't.
`test-checker-driver`'s returns-twice unit runs each kind, the pointer's too,
against clang -O0's build. Still refused: a
vector result LLVM demotes to a hidden pointer, naked functions, whose bodies
are assembly Tier 0 copies as written, an address whose offset alone is past 32
bits. Asm statements placed in their callers are Tier 1's as asm bodies (#1839,
[the selection guide](tier1-selection.md)'s "Asm bodies"), but a template that
returns from its function, whose function is Tier 0's. The IR's switches and selects of floating
values are refused too, but the C driver's lowering makes neither: a `switch`
comes as compares or a jump table, which Tier 1 takes, and `?:` as branches.
Vectors of elements other than 32-bit integers (floats, doubles, 8-, 16- and
64-bit integers, which `__m512i`'s intrinsics are) are the IR's operations of
their lanes where C's operators make them (#141, [the SIMD IR guide](ir-simd.md)'s
"Lanes"), and the x86 builtins' other rows of vectors are the IR's machine rows
([the IR guide](ir.md)'s "Machine rows"),
which Tier 1 places as Tier 0 does ([the selection guide](tier1-selection.md)'s
"Machine rows"); a vector of 4 or 8 bytes (an SSE carrier, #1280: the operand of
`_mm256_cvtepu8_epi32`, MMX's `__m64`) is an f32 or f64 there. A 32- or 64-byte
vector at a target whose registers are narrower (x86-64's and x86-64-v2's 16 bytes,
an AVX target's 32) is split into the target's widest vectors before the optimizer
(#1624, "The vectors wider than the target's registers" below); an SSE carrier at
a target without AVX (#775) still stops before Tier 1, at the lowering to the IR;
at a target without AVX a 16-byte vector is Tier 1's by
legacy SSE's forms (#141, [the selection guide](tier1-selection.md)'s "At targets
without AVX"). At a
target with AVX but without AVX512VL, such as x86-64-v3, Tier 1 takes 16- and
32-byte vectors by VEX forms in xmm0 to xmm15, and places machine rows' roles
below 16 (#1281, [the selection guide](tier1-selection.md)'s "At targets without
AVX-512"). By `--tier1 -march=native`, main.c compiles every function by Tier 1
but its naked one. The scalar ones, BMI2's
`pdep`, `pext` and `bzhi` and BMI1's `bextr`, are the IR's `intrinsic` (#141,
[the SIMD IR guide](ir-simd.md)'s "Scalar x86 builtins"), each one VEX
instruction of selection's `TIER1_RULE_INTRINSIC`, and so are the C bit builtins'
rows (#1374: `tzcnt`, `bsf`, `lzcnt`, `bsr`, `popcnt` and `bswap`) and the signed
overflow builtins' (#1385: `saddo`, `ssubo` and `smulo`, the operation and `seto`).
The masked moves' selects of the x86 builtins (every masked operation's merge)
are the IR's `vselect` and their undefined vectors its zero vector (#1840, [the
SIMD IR guide](ir-simd.md)'s "Selects and undefined vectors"), which Tier 1
selects as `vpblendm` and `vpxor`.
The x86 builtins' casts between vector widths, lane inserts and masked lane
extracts are the IR's `extract`, `widen` and `insert` intrinsics (#1840, [the
SIMD IR guide](ir-simd.md)'s "Width changes"), each one EVEX instruction of
selection's `BUSTER_TIER1_VECTOR_WIDTH_CHANGE`; their variable permutes, byte
shuffles, aligns and shuffles of lanes are its `permute`, `shuffle`, `align` and
`shuffle_lanes` (#1840, its "Permutes"), each one EVEX instruction of
`BUSTER_TIER1_VECTOR_PERMUTE`.
Their integer lanes' extensions and truncations, `__builtin_convertvector`'s and
the `vpmov*` builtins', are its `extend` and `truncate` (#1840, its
"Conversions"), each one EVEX instruction of `BUSTER_TIER1_VECTOR_CONVERT`.
A vector's shift by one scalar count, the x86 builtins' shifts by an int, is
its `shift` (#1840, its "Shifts by a scalar count"), `BUSTER_TIER1_VECTOR_SHIFT`:
the immediate form where the count is a constant, else `vmovd` of the count into
the vector scratch register and the form by that xmm count. A select of all ones
and zeros, a C comparison's expansion and a mask to a vector, is `vpmovm2b` to
`vpmovm2q`, and a signed comparison less than a zero vector is `vpmovb2m` to
`vpmovq2m` (#1840, its "Masks to vectors and back"). The integer lanes' minima, maxima and
saturating sums and differences are its `lane_op`, one EVEX instruction of
`BUSTER_TIER1_VECTOR_LANE_OP` (#1840, its "Lane operations").
The permutes of two tables are its `permute2`, whose third operand is in the
extra array (#1840, its "Permutes of two tables"), `BUSTER_TIER1_VECTOR_PERMUTE2`:
`vpermt2*` into the first table's register, `vpermi2*` into the indices', or a
move of the first table first.
The bitwise functions of three vectors are its `ternlog` (#1840, its "Bitwise
functions of three vectors"), `BUSTER_TIER1_VECTOR_TERNLOG`: `vpternlogd` with
the operand the destination's register holds first, its table permuted.
The compresses and expands are its `compress` and `expand` (#1840, its
"Compresses and expands"), `BUSTER_TIER1_VECTOR_COMPRESS`: the form merged under
the mask into the destination, which takes the passthrough first.
The AES rounds are its `aes` (#1840, its "AES rounds"), `BUSTER_TIER1_VECTOR_AES`:
`vaesenc` or `vaesenclast`, EVEX, or VEX where the target has no VL.

### Inlining (#1282)

Between the lowering and SSA promotion, `--tier1` expands a body's calls of the
unit's always_inline functions of internal linkage, the x86 intrinsics'
wrappers above all (`c_check_tier1_inline.c`), and splits the aggregates' slots
of a body with vectors or with expansions into fields that promotion makes
values. The checker records each function's always_inline and noinline
attributes (`CheckProgram.object_inline`); a function with both, or neither, is
no callee. The emission then lowers every part before any part's code
(`CheckEmitRun.tier1_inline`: the lanes meet between the halves, a growing
team's code waits for every lowering, a board's share runs two epochs), so that
a body's lane reads any callee's typed stream. Each lane prepares a callee once
an emission, in a cache arena of its own: lowered at its own target, promoted
and optimized, which leaves a wrapper a preheader and its body, one straight
line. A callee whose target has a feature its caller's lacks stays a call.

The unit of the caller and the callees its calls reach (up to 64) goes through
`ir_optimize_inline` with no cost threshold, as clang inlines an always_inline
function whatever its size, copies of 64-, 32- and 16-byte vectors taking the
target's widths ([the inlining guide](ir-inlining.md)'s "SIMD callees and
by-value vectors"). SROA, with its `simd` and `flagged` options ([the field
splitting guide](ir-scalar-replacement.md)'s "#1282's options"), then makes the
by-value vectors' slots typed vector slots, which promotion makes values: a
wrapper's arguments, its own parameter objects and its result's bytes all leave
memory. Tier 1's own entry and return of a function that takes or returns a
vector by value, which its frame emitter stores into and reloads from frame
objects, are unchanged. The typed stream's lowering copies an aggregate
parameter that a vector register holds whole by one vector load and store
rather than eight `i64`s (`ir_typed_vector_whole`), in a function with vectors
and at a target with AVX-512, so that its object is one field.

Any failure of the inlining or of SROA keeps the body as it was lowered: inlining
is an optimization, never a refusal. `--tier1-report`'s `inlined` sums the
calls expanded into the bodies Tier 1 compiled; `--tier1-no-inline` keeps every
call, for comparison. `test-ir-inline` runs the inliner's SIMD and by-value
fixtures, the latter on the IR's interpreter before and after inlining, SROA and
promotion; `test-checker-driver`'s inlining fixture builds a program of
intrinsics, a struct helper and a noinline function by each path, with and
without inlining, against clang -O0's.

### The vectors wider than the target's registers

At a target whose widest vector register is narrower than a function's vectors
(x86-64's and x86-64-v2's xmm for 32- and 64-byte ones, AVX's ymm for 64-byte
ones), the driver's lowering takes them (`ir_lower_typed_target_pieces`, #1624)
and `ir_vector_split.c` splits each into the target's widest vectors
(`ir_typed_vector_piece`) after SSA promotion, beside the 128-bit integers'
halves (the driver's `wide` stage), so that the optimizer, selection, both
allocations and their certificates see vectors the target has registers for. The
typed stream carries such a vector through its transports alone: Tier 0 refuses
a row wider than its target's vectors and computes C's operators on them in
pieces, so a wide vector's IR is constants, loads and stores (of objects,
through addresses, and of the expression stack's slots, which promotion makes
phis), copies and phis. Each becomes its pieces' instructions in its place: a
constant's bytes, an access per piece at its offset (the access's alignment, at
most a piece's), a phi per piece. Any other instruction of a wide vector (an
operation, a comparison, a select, a call's argument or result, a row's operand)
or an atomic access refuses the function, which Tier 0 then compiles, as before.
A vector passed by value is an object already; one returned in registers comes
back split across xmm0 and xmm1, xmm0 to xmm3 or ymm0 and ymm1 (#1193), which
selection, both consumers and the code's certificate now take as up to four
result parts (`tier1_select_result_parts`). At an AVX-512 target a 128-byte
vector comes back in zmm0 and zmm1, two 64-byte parts, and a 256-byte one in
zmm0 to zmm3 (#1847). The function's own return loads each such part, a whole
vector register's, straight from the result's address
(`tier1_select_part_direct`), which is in a general register no load
overwrites, and the code's certificate takes each load as reading the result's
bytes through the address and defining the part in its register. Any other
part (a record's in rax and rdx or xmm0 and xmm1, whose bytes may be fewer
than its load's) is staged in the red zone's eightbyte slots first
(`tier1_select_result_staged`, which the consumer and the certificate check
too); two of them take at most two slots. `test-checker-driver`'s wide results' unit returns a
128-byte vector at x86-64-v3 and x86-64-v4 against clang -O0, and its 256-byte
results' unit one of 256 bytes (`make64`, called by `use64`) at x86-64-v4;
below AVX-512 LLVM returns that one through a hidden pointer clang doesn't
count (`DEMOTED`), which Tier 1 refuses.

Measured by `--tier1 --tier1-refusals --verify` of the compiler's own units,
whose scalar host's emulations of AVX2's and AVX-512's intrinsics
(`host_scalar_vectors.h`) take and return these vectors: at main `cf38957a`,
`main_unit_frontend.c` refused 249 functions at x86-64 and x86-64-v2 and 244 at
x86-64-v3, every one for its wide vectors, and now refuses none, its 2,177
functions' code certified at x86-64 (1,928 at main, uncertified 59 either way);
`main_unit_backend.c` refused 233 at x86-64 and now refuses 25, none for a wide
vector (the same 25 it refuses at znver5).

Those 25, attributed at main `cb608c1f` by `--tier1 --tier1-refusals --verify
-c` at x86-64, x86-64-v3 and znver5 (#1847):
- 16 were past the 16 arguments a call passed and a function took
  (`BUSTER_TIER1_SELECT_MAX_ARGUMENTS`): 14 tests' calls of `printf` and of
  their code through pointers with 17 to 25 arguments, and `_mm256_set_epi8`'s
  and `_mm512_set_epi8`'s 32 and 64 parameters (at x86-64, the scalar host's
  `host_mm512_set_epi8` and `tier0_shift_back`'s call of it). The limit is 64
  now, and Tier 1 takes them all.
- 5 are naked functions, whose bodies are assembly Tier 0 copies as written.
- 4 called `__builtin_frame_address(0)` (`tier1_float_callee`,
  `tier1_float_callee_variadic`, `tier1_stack_check_alignment` and
  `cbuiltin_misc_lower_test_frames`), whose typed-stream entry the lowering to
  the IR refused. It is now a `stack_save frame` (#1847): the function keeps a
  frame pointer (`KEEP_POINTER`) and copies rbp, and the code's certificate
  takes it. At main `d43c37e5` the unit refuses only the 5 naked functions at
  x86-64, x86-64-v3 and znver5, and `test-checker-driver`'s frame address unit
  runs against clang -O0. `test-tier1-wide` takes hand-written
wide vector IR through the split at both piece widths and natively at x86-64's,
and `test-checker-driver`'s wide vectors' unit runs against clang -O0 at
x86-64, x86-64-v2 and x86-64-v3 by both paths.

### The 128-bit integers

`ir_wide.c` (#141) splits a function's `I128` values into pairs of `I64`
halves, low and high, between SSA promotion and the optimizer (the driver's
`wide` stage), so that the optimizer's SCCP, dead code and CFG cleanup,
selection, both allocations and their certificates see 64-bit values alone.
Each `I128` instruction becomes its halves' instructions in its place, so that
blocks keep their numbers and a block's phis stay first:

- a constant, an undef, a copy, a select, `and`, `or`, `xor` and `not`: each
  half's own;
- an addition: the low halves' sum, and the high halves' plus its carry, the low
  sum's unsigned compare below an operand; a subtraction likewise with its
  borrow; a negation: each half negated, the high one less 1 where the low one
  isn't zero;
- a product, modulo 2^128 whatever the signedness: the low halves' product and
  its high half, the IR's `intrinsic` `mulhu` (the high 64 bits of an `I64`s' unsigned
  product, [the IR guide](ir.md)'s), plus each cross product of a low half by a
  high one not known to be zero. A product of two zero extensions, C's
  `(unsigned __int128)a * b`, is `imul` and `mul` alone: selection's
  `TIER1_RULE_MULTIPLY_HIGH`, one operand in `rax`, `mul` of the other, its
  `rdx` the result, which both consumers save other values around as they do
  a division by a constant's reciprocal;
- a shift by a constant (through the count's extensions, truncations and
  copies, as the typed stream gives `r >> 64`): shifts of the halves and an
  `or`, a half alone past 64 bits, or at 64 the other half itself, for no
  instruction; by a variable count, both halves shifted by its low 6 bits, the
  bits crossing between them shifted by 1 and then by the complement (so that
  no count reaches 64), and selects on its bit 6;
- a comparison: equality from the halves' `xor`s, `or`ed; an order from the high
  halves' (strict, of the predicate's signedness) where they differ, else the
  low halves' (unsigned);
- an extension: the value as the low half (a 64-bit one itself) and zero or its
  sign as the high half; a truncation, the low half (to 64 bits the half
  itself); a pointer's conversions, through the low half;
- a load and a store: two 8-byte accesses, the high half's address 8 bytes on;
  a phi: one of each half with the same predecessors.

Division and remainder, and conversions to and from floating values, reach the
IR as libgcc's calls (the checker's lowering), whose 128-bit arguments and
results are by-value aggregates; an `I128` in a signature, or an operation the
split doesn't take, refuses the body as unsupported, and Tier 0 compiles it.
Every function `--tier1` compiles is scanned for an `I128` instruction, 64 type
bytes a compare on the vector path and one at a time on its scalar twin, and a
function without one is the promoted function itself. The rewrite is scalar:
two passes of one body, counting then writing, on the functions with 128-bit
values alone (two of `sqlite3.c`'s 2,668, none of Lua's), whose cost is below
measurement on the compile (the record below). Each phi's and call's remapped
list is appended to the `extra` table, so phis that share a predecessor list,
as the verifier lets them, can take the output past what a `u32` counts where
the source isn't: the counting pass counts in 64 bits and refuses such an
output as a limit before it allocates. With the split, `--tier1
--tier1-strict` compiles every function of `sqlite3.c` and speedtest1's, whose
`--verify` hash is unchanged.

The halves showed a constraint of the straight-line consumer: its allocation
keeps each interval in the registers it allows and moves no value between
registers, so a parameter bound to its incoming register, once evicted, can
only reload there, which fails where another operand of the same node holds
that register. Such a function now goes through the CFG consumer, whose
allocation moves values (`tier1_emit_function`), rather than stopping the
build.

A 128-bit local is still a frame object (the checker gives `__int128`'s locals
Tier 0's memory, initialized through its address), so promotion leaves its
loads and stores, which the split makes 8-byte accesses: `sqlite3Multiply128`'s
`r` goes through its frame slot. Promoting such a local is a front-end change
(the checker's initialization and the IR lowering's slot types) not made here.

`test-tier1-wide` runs hand-written 128-bit IR (every operation the split takes,
`sqlite3Multiply128`'s product, a shift by an extended constant, phis around a
loop, unaligned, volatile and indexed accesses), 120 random straight-line
programs and 300 of Tier 0's random 128-bit typed programs (frame objects,
`?:`'s blocks, conversions both ways) through the split by both paths, which
must make the same function, both verifiers, the optimizer and both of Tier 1's
paths with their certificates, natively, against the IR's interpreter on the
unsplit function and the programs' stack machine; the split's refusals and its
scan's twins; overlapping `extra` slices, and `I128` phis that share one
predecessor list past the `u32` limit; and 300 of Tier 0's random long double programs (calls of C
functions of long doubles, every conversion, NaNs, frame objects, `?:`'s blocks)
through the optimizer and both of Tier 1's paths natively against their stack
machine (#141). `test-checker-driver`'s wide values' fixture
(`c_check_tier1_driver_test.c`) passes long doubles, aggregates returned in the
x87's registers (a struct of a long double, a `_Complex long double`) and the
memory class's, to and from clang -O2's code and through a variadic
definition, beside 128-bit integers within its bodies, every function Tier 1's
by both paths, against clang -O0's build; and its rarer shapes' fixture passes
aggregates with a high or a low eightbyte alone, in a register and in a stack
slot, `SPLIT` ones and ignored ones, so.

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
slot carried values of several types stayed in memory (single-type ones promoted
since #983, and each depth and type has a slot of its own since #1189); a frame
object's address is a `lea` rather than part of its users' memory operands; the
pipeline has no loop-invariant motion; and `sqlite3VdbeExec`, the hottest
function, was Tier 0's then (its switch). 80 of #142's programs, 40 per generator, gave
66 passes at both tiers and 14 failures, all in clang's own steps, and 40 more by
the scalar path alone 36 passes and 4 such failures. The default path's compile
time is unchanged, and `--tier1 -c` of `sqlite3.c` takes 371 ms (387 ms before
the walks, which leave 40% fewer records to emit); the checker's unit, which
compiles Tier 1's modules for the driver, takes 40.0 s to build against 36.6 to
36.9 s without them.

## Where the gap to clang -O2 is (#1189)

#1189's breakdown ([its comment](https://github.com/buster14a/buster/issues/1189#issuecomment-6601742145);
[the record](https://github.com/buster14a/buster-performance/tree/6495a438f6a3d3b0c8605e24d16f5da996910dad/docs/measurements/tier1-gap-1189-2026-11-03)
has the tool, the experiment patches and the counts)
counts every user-space instruction of speedtest1 and of Lua 5.4.8's five
scripts. It uses callgrind with `--dump-instr=yes`, which is exact per
instruction and agrees with the 9700X's `perf stat` on c-testsuite 00040 to the
instruction. An experiment build, not on main, tags each Tier 1 record with its
origin: spill, edge spill, call save, edge move, call sequence, frame,
recomputed value, CFG branch or node. Its executables are main's byte for byte.

At main `96b6cbcd`, the program's own code runs:

| | `--tier1` | clang -O2 | Gap |
| --- | ---: | ---: | ---: |
| speedtest1 | 3,744 M | 1,888 M | 1,856 M |
| Lua 5.4.8, five scripts | 7,228 M | 3,768 M | 3,460 M |

The gap by cause, as shares of each program's gap:

| Cause | speedtest1 | Lua | Issue |
| --- | ---: | ---: | --- |
| stack loads and stores | 23.4% | 27.4% | #1216 (edge spills and reloads), #1116 (call saves) |
| register moves | 18.8% | 12.9% | #1216 (edge moves), #1116 (call sequences' 32-bit self-moves) |
| address arithmetic | 14.8% | 18.2% | #1217 |
| compares and branches | 14.8% | 17.8% | #1218 |
| other loads and stores | 11.2% | 7.8% | #139 (memory value numbering), #1218 (load folding) |
| zero and sign extensions | 9.0% | 12.5% | #1219 |
| calls, returns and frames | 7.4% | 1.5% | #139 (inlining), #177 (frames) |

The interpreters' loops hold most of it: `sqlite3VdbeExec` 21% of speedtest1's
gap, and `luaV_execute` 64% of Lua's.

None of the existing passes that `--tier1` doesn't run measures as a gain:
- GVN: the measurement above (`--tier1-gvn`).
- LICM: [the loop-motion guide](ir-loop-motion.md)'s "In `--tier1`".
- SROA: [the scalar-replacement guide](ir-scalar-replacement.md)'s "In `--tier1`".
- The inliner: [the inlining guide](ir-inlining.md)'s "Reach in `--tier1`".

Two further directions are not pursued now, by the owner's decision on #1447
([comment 6736401463](https://github.com/buster14a/buster/issues/1447#issuecomment-6736401463),
item 4, closing #1450's last open items):
- profile-guided code placement: blocks keep the static fall-through order of
  `ir_optimize_layout.c` ([the command guide](commands/tier1.md)'s "Block
  placement and critical-edge fall-through");
- shrink-wrapping, which #1625 measured as a bound of about 1% of speedtest1's
  executed ops ([the frame guide](tier1-frame.md)'s "Shrink-wrapping").

What did pay is a slot per depth and type for the typed stream's values that
cross blocks ([the IR guide](ir.md)'s adapter): 1.2% of speedtest1's
instructions and 1.3% of Lua's. On instructions, `--tier1` is now below clang
-O0 on both programs: 3,870 M against 3,908 M, and 7,610 M against 8,253 M.

On the 9700X, `measure-tier1-programs` ([the command guide](commands/tier1.md))
ran in #1230's Benchpress job at `eeeafb6d`, with clang 23.1.4
([the record's `9700x/`](https://github.com/buster14a/buster-performance/tree/6495a438f6a3d3b0c8605e24d16f5da996910dad/docs/measurements/tier1-gap-1189-2026-11-03),
every run of 11 rounds). The medians, Lua's being the sum of its five scripts':

| Program | Tier 0 | `--tier1` | clang -O0 | clang -O2 | tcc | `--tier1` / -O2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| speedtest1, ms | 258.2 | 207.0 | 271.0 | 129.3 | 269.7 | 1.60 |
| speedtest1, user cycles, M | 1,336 | 1,051 | 1,397 | 626 | 1,398 | 1.68 |
| Lua's scripts, ms | 335.2 | 260.8 | 358.7 | 167.4 | 393.6 | 1.56 |
| Lua's scripts, user cycles, M | 1,839 | 1,424 | 1,957 | 910 | 2,157 | 1.57 |

The `cc3387f` record had `--tier1` at 1.70× -O2's time on speedtest1 and 1.71×
on Lua, before #1117 put `sqlite3VdbeExec` and `luaV_execute` into Tier 1 and
before the depth slots above. The 9700X job runs the measurement on its daily
and dispatched runs, and on pull requests that change Tier 1's input or
pipeline.

#1216 keeps dispatch loops' loop-carried values in registers and their phi
webs in one slot ([the allocation guide](tier1-allocation.md)). From its merge
base `6a021550e`, the tagged edge spills, reloads and moves fell from 469 M to
226 M on speedtest1 and from 1,013 M to 477 M on Lua (51.9% and 52.9%). On the
9700X, user cycles fell from 974 M to 928 M and from 1,322 M to 1,219 M
(100 ln(head / base) of -4.91 and -8.25), head faster at every layout
([the record](https://github.com/buster14a/buster-performance/tree/c8cc523034dd4035161800a4d0940e287a8975dc/docs/measurements/tier1-loop-carried-1216-2026-11-09)).

#2553 keeps a value's place along the dominator tree, so that it changes
register between blocks only where the blocks on its way moved it
([the allocation guide](tier1-allocation.md)). From its merge base `afcb185e`,
the tagged edge traffic fell from 225 M to 211 M on speedtest1 and from 543 M
to 356 M on Lua, and user cycles on the 9700X by 0.73 and 1.02
(100 ln(head / base)), head slower at 1 of 8 shifts and 7 of 33 link orders
([the record](https://github.com/buster14a/buster-performance/tree/ddb5213da1fce7ac9325afbf6364a797bbc63b5b/docs/measurements/tier1-dominator-places-2553-2026-11-12)).

At main `c5f0efb2` (2026-11-12) the same measurement gives 1.42 and 1.39 times
clang -O2's time, and gaps of 1,160 M and 1,780 M instructions; a ladder of
LLVM's own pieces ranks what is left by time, and the compiler itself is
measured as a third program. [The unit inlining design](tier1-unit-inlining.md)
(#2541) has those tables, the ranking with each cause's issue, the design of
the first-ranked lever that no issue owns (inlining the unit's small functions
whatever their control flow, with a prototype's measurements), and the
inventory of what stands between Tier 1 and `-O1` and above.

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
arguments (64 through the CFG consumer, #1847, those past the sixth on the stack:
#141) and a void or matching scalar result. The native consumer preserves
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
