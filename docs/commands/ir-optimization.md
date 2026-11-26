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
  through the parser, the pass (GVN's fixtures also through its scalar twin,
  which must remove no more), the canonical printer and the independent
  executor (`--print-text` prints them for review).
  #1729's join chains (arms of distinct constants, of the parameter's sums and
  of one constant, meeting at one phi) run through the optimizer against the
  executor, and chains of 4,096 arms bound the incoming values SCCP's phi
  transfers examine (`sccp_phi_incoming`) to four per arm, which the worklist
  exceeded before the change.
  #1450's SIMD fixtures exercise constant lane folding against the lane model
  and interpreter, wide phi/bitcast propagation, exact-bit splats, GVN's complete
  vector payload keys and LICM's integer-lane motion. A floating
  lane folds exactly where the model gives the same bits under every rounding
  and under FTZ with DAZ (4,096 random lane pairs, and every lane fixture), the
  operation's row staying for its effects.
  `benchmark-ir-optimize`
  and `benchmark-ir-optimize-passes` include preparation and rewriting costs.
- Value numbering with memory (#2718, `ir_optimize_memory.c`, the optimizer's
  `BUSTER_IR_OPTIMIZE_MEMORY`): `test-ir-optimize` runs its
  `tests/ir/optimize/memory-*.ir` pairs and `ir_optimize_memory_test.c`'s 400
  generated programs, whose interpreter's calls rewrite every escaped byte,
  folded into a frozen digest (the default seed; its scalar twin went in #2924),
  and an overflow fixture. `benchmark-ir-source --passes` times its `memory`
  row once. [Its guide](../../docs/ir-memory-value-numbering.md) has the rules,
  the vector phases and the measurements.
- #139's acceptance on real C: `test-ir-source --passes` runs every pass
  (GVN also by its scalar twin) on every function of c-testsuite's subset, the
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

## Switches of constants (#3298)

`ir_switch_table.c` turns a jump-table switch whose arms only give a constant to
one value (returned, or one phi's) into a load of the jump table's values,
which the emission writes in place of the labels' offsets; Tier 1's `tables`
pass item, on by default (`--tier1-passes=-tables` turns it off), and
`--tier1-report`'s `value_tables` counts them. `test-ir-optimize` runs
`ir_switch_table_test.c`: generated text fixtures of the C lowering's dispatch
(returns and phis of i8 to i64, i32 and i64 selectors, negative lowest cases,
holes, the join among the arms) converted, verified, and run on the
independent executor with the table's bytes at every selector around the table
and the type's extremes, before and after the optimizer, against the value the
fixture's description gives; and the refusals (a store in an arm, an i64 past
32 bits, an arm's variable, a retyped load read twice, an arm branching
elsewhere, two dispatches of one table), which must borrow the input
unchanged. `test-checker-driver`'s value tables unit runs a C fixture's
`--tier1` builds against clang -O0's. [The design](../ir-optimization.md#switches-of-constants-3298).

## Loop vectorization (#2743)

`--tier1` runs the loop vectorizer by default (`--tier1-passes=-vectorize`
turns it off). `test-ir-vectorize [--seed N] [--print NAME]` (`ir_vectorize_loops_test.c`) runs
the loop vectorizer's text fixtures, `tests/ir/vectorize/*.ir`: element-wise
operations of every lane width, fills, copies, sums with widening, a dot
product, the four other integer reductions, an if-converted conditional sum
with its unreachable stub, an `int` induction's clamp, byte arithmetic through
32-bit lanes, population counts, a stencil whose exit test is `i + 1 < n`, an
in-place `y[i] += k * x[i]`, an `fma`, an `sitofp` of `i32` to `f32`, an invariant start and index offset, a uniform load,
a sum of a uniform (no vector load or store), two loops and a nested one; and one fixture per refusal (a prefix sum, a
histogram, a strided store, the induction as data, a call, a volatile load, a
division, FNV-1a's recurrence, a float sum, bytes summed into a 64-bit total (a width no vector holds), more guards
than the limit, a reduction's partial value read, an early exit, a step of two). #2808's fixtures, past the frozen
digest's: counts of comparisons (`count-u8`, `count-i32`), strided loads (`aos-sum`, `rgb-gray` through 32-bit
indices, `strided-pairs-i16`, `stride8-u8`, and `stride8-i64`, whose last element ends the interpreter's buffer, so a
load past it faults) and a 32-bit index that wraps within a vector (`narrow-wrap`, whose input and result must fault
alike).
Each runs by the parser and the pass at six targets of every operation
the IR has: at most 64, 32 and 16 bytes wide with masked moves, 64 bytes without
them or masks from a general register, 64 bytes with them but no select of 32-
or 64-bit lanes, and 64 bytes with them but no mask of 8 lanes or fewer from a
general register (an AVX512F target without DQ's `kmovb`) (#2807). Its
result and counts are folded into a frozen digest (#2924), and the kinds and the
refusing kinds' scan are checked against their definitions. A fixture expects how
many loops each width vectorizes, or its refusal's reason, and how many of them
have a masked tail: every integer loop's where the target has masked moves (a
reduction's only where it has the selects too, and a loop of 8-byte lanes only
where it has the byte mask), and none of a floating loop's. Each changed result, and its form after
the ordinary optimizer (whose final verification checks dominance), runs in the
shared interpreter against the input on one buffer, its pointers placed apart,
at one address, a byte, an element and a vector apart either way, at lengths
around the vectors' boundaries (signed bounds negative too): the same bytes,
value and status. Seven mutants (`BUSTER_IR_VECTORIZE_MUTATE_*`: no overlap
test, an unrounded tail, a wrong reduction identity, lanes of the wrong width, a
masked tail's mask of one lane more, no 32-bit index wrap test, a strided load's
wrong lane) must each be rejected on some fixture. `--print NAME` prints a fixture's
64-byte result. `test-checker-driver`'s inlining unit compiles a C fixture of
loops by `--tier1 --tier1-passes=+vectorize --verify` at x86-64 to x86-64-v4 and znver5 by
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
mask value, and loops that hold a call (#2743: every vector register is
caller-saved, so a value moved out of one is stored and reloaded around its
calls). `test-ir-gvn` covers both host paths, and `test-ir-licm` the walk's
frozen digest (its whole-function oracle went in #2924), scalar neutrality,
nested loops and calls;
`test-checker-driver` checks the default and `-vectors` reports and native
output on a C vector fixture. See [the Tier 1 guide](../../docs/tier1.md).

## Pressure-aware LICM and value numbering (#2719)

`IrLicmOptions.pressure` (Tier 1's `hoist` item, on by default, in functions
without a vector or mask value where `ir_licm_pressure_screen` finds a loop that
may hold no call) moves only what pays for the register it holds through the
loop: nothing out of a loop that holds a call, free values (constants,
addresses, casts of no bits) only as operands of a costly value that moves, no
truth value, and at most a budget of costly general-register values.
`IrGvnOptions.pressure` (`+numbering`, off by default, on the promoted
function) never merges a free value and merges a truth value only within its
block. Tier 1 runs both with `validated` (`IrOptimizeWork.validated`): their
input comes verified from the passes before them, so their preparation builds
its CFG without verifying it again, and LICM's under the policy builds no use
lists for a function without an undef (`IrOptimizeWork.without_uses`).
`test-ir-optimize`'s `licm-pressure`, `licm-pressure-call`,
`licm-pressure-budget` and `gvn-pressure` pairs check the rules, and it runs
every LICM and value-numbering pair with and without the verified input's
preparation, which must give the same function; `test-ir-licm`'s budget on
random invariant chains, its screen fixtures and the undef taint under the
policy check the rest. On real C, `test-ir-source --passes`' `hoist` and
`numbering` rows run each as Tier 1 does against the verifying preparation (the
same function; where the screen passes a function over, the verifying LICM
moves nothing), and `benchmark-ir-source --passes` times each once, as Tier 1
runs it. `test-checker-driver`'s passes fixture checks that the default
hoists, that `-hoist` doesn't, and that `+numbering` reaches its pass.
The rules and their measurements are in [the loop motion guide](../../docs/ir-loop-motion.md#the-pressure-policy-2719)
and [the value numbering guide](../../docs/ir-value-numbering.md#the-pressure-policy-2719).

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

## Loop store motion (#3309, #3346)

`ir_store_motion.c` is the pass `--tier1-passes=+storemotion` runs on the
lowered function, before promotion (off by default): a loop's loads of one of the unit's own
globals read a private slot that the preheader fills, and promotion makes the
slot a register's value. Where every read of memory in the loop is a load the
pass places, the loop's stores of the global write the slot alone and each exit
writes it back once (#3346), through the global's address where its bits
changed and a dummy slot's where they didn't; otherwise each store stays and
writes the slot too. [The design](../tier1-store-motion.md) has its rules, the
symbols it trusts and the 9700X's records.

`./build/compiler test-ir-store-motion [--seed N] [--print NAME]`
(`ir_store_motion_test.c`) runs its text fixtures,
`tests/ir/store-motion/*.ir`: two globals whose second statement reads the
first's store, a conditional update, nested loops (the outer one takes the
keys, or calls and leaves them to the inner one), a record's fields through
the lowering's address chains, every scalar type, a load through a pointer,
two exits, a loop tested at its end, more keys than the limit, the lowered
form with its locals in slots, a key the loop never reads, stores to other
symbols, and loads that overlap a key; and one fixture per refusal (a call, a
volatile access, a fence, a store through a pointer, an indexed store, an
overlapping store, a key past its object, no preheader, an irreducible region,
a type no register holds, a symbol that isn't listed, more keys than the
analysis follows, an address past the walk's steps). #3346's add exits by a
break, a return and a store after the loop, an exit block other paths reach
(split edges), two loops in a row (one's write-back before the other's load), a
double's and a float's keys, and refusals of sinking for a switch's exit to a
shared block and for a stored-only key beside a load through a pointer. Each is read by both
parser paths and expects its moved loops and keys, the loads and stores given a
slot, and its outcome or refusal; results and counts fold into a frozen
digest. A changed result must verify, promote and pass the optimizer, and runs
in the shared interpreter as it is, promoted and optimized against the input,
on equal memory, a pointer parameter at a key's own bytes among its places: the
same bytes, value and status. A quarter of the trials bind the symbols
read-only, so a store the source didn't make, the value it held included,
faults. Nine faults (`BUSTER_IR_STORE_MOTION_MUTATE_*`; #3346's a key sunk past
a load it can't place, an exit with no write-back and a write-back to a key its
loop didn't change) must each be rejected on some fixture. The symbols' filter and the
classification are checked against their definitions on random columns at
every length, and the entry point with arenas of every short size. `--print
NAME` prints a fixture's result.

`tests/driver/tier1_store_motion.c` is the C driver's test: 26 functions of
loops over globals compiled strictly at Tier 1 with the pass and without it on both host
paths, each program's lines clang -O0's (#3346's: a loop left by a break, a
return and a goto, the issue's `double` kernel, an `mprotect`ed page a loop that
stores nothing must not write, a `volatile sig_atomic_t` flag a signal handler
sets, and an address that escapes to a pointer the loop reads); the report's
counts; the issue's kernel's references to its globals in `objdump`'s text (six with the pass, all outside the loop); a
program whose loop's write-backs come before a callee's `longjmp`, built
without `--tier1-strict` (Tier 1 doesn't admit the function that calls
`setjmp`); and 29 declarations whose objects the emission's list must take or
leave:

```sh
clang -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tests/driver/tier1_store_motion.c -o build/test-driver-tier1-store-motion && ./build/test-driver-tier1-store-motion build/compiler "$(command -v clang)"
```

Each write-back is a load of the slot and of the key, a compare of their bits,
the dummy slot's address, a select and a store: the key is read again at the
exit rather than the preheader's value held across the loop, which cost a spill
an iteration in a loop that needs its registers.

`test-ir-source --passes` runs the pass on every function of its files with
every symbol listed, and `benchmark-ir-source --passes --only store-motion`
times it ([the IR guide](ir.md)).

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

## Register-class aggregates in registers (#3311)

The typed stream copies a by-value aggregate's bytes by eightbytes (a
parameter's from its `param ptr` into its object, a register-class result's
out of the object its `ret` returns), while the source reads and writes the
fields, an `int`'s four bytes at a time. SROA refused such a slot (its
accesses overlap) and a result's slot escaped through its `ret`, so a struct
passed and returned in registers went through three stack copies, and its
`int` fields' narrow stores were read back as eightbytes, failing store
forwarding. Tier 1 now takes them in three steps, all in
`ir_optimize_sroa.c`:

- **Widening** (`IrSroaOptions.widen`, `ir_sroa_widen`): a slot of at most 64
  bytes, not refused but for its overlaps or a `ret` (offset 0), all of whose
  accesses are scalars (integers, `f32`, `f64`, `ptr`) inside one aligned
  eightbyte, has each access made one of its eightbyte's `i64` (a narrower
  load shifted and truncated, a narrower store or'ed into the eightbyte with
  its bits cleared). A reachable `ret` of it, where the result is one or two
  general or xmm eightbytes of at most 16 bytes, loads its eightbytes and
  returns them through a new slot of its own. The split then makes each
  eightbyte a field that promotion makes a value. The insertion reuses the
  unroller's remapping; a function with debug information is left as it is.
  `test-ir-optimize`'s `sroa-widen` pair runs it through the interpreter.
- **Scalars** (`ir_sroa_scalarize`), for a function's own code alone, never
  an inline summary its callers expand: a parameter in one or two eightbyte
  registers whose `ptr` only block 0's loads of a part read becomes a scalar
  parameter a part (`i64` in its general register, `f64` in its xmm one), and
  a result of one eightbyte in `rax` or `xmm0` that every `ret` returns from a
  slot one `i64` store filled becomes that value. A function left with no
  aggregate whose parameters are where their types put them drops its records,
  and with them its parameters' frame rows.
- **The optimizer's identities** take what the eightbytes leave: an `and` of a
  zero extension and a mask that keeps its bits, a bitcast of a bitcast and a
  truncation of an extension back to the first value's type, and an `undef`
  as zero where it is an operand: an `or`, `xor` or `add` of it is the other
  operand, and SCCP folds an `and` of it to zero, as LLVM does. The `and`
  isn't the `undef` itself (#3424): its other operand clears bits, and an
  `undef` that reaches a phi has no definition, so the phi's register held
  whatever it held, a struct field left zero among it.

Tier 1 runs the widening SROA on every function with its own ABI records
(`check_tier1_compile`, and `check_tier1_inline_expand` where the expansion
runs), and a two-eightbyte result is loaded straight from its slot into `rax`
and `rdx` (or `xmm0` and `xmm1`) rather than staged in the red zone (#3299's
`tier1_select_part_direct`).
`tests/ir-source/register-aggregates.c` checks the shapes (mixed widths,
floats, padding, nesting, arrays, 9 to 16 bytes, fields left unread, an
escaping parameter, several returns, unions, and call sites in a loop) by
both strict native paths against clang's output, and the code of `{int, int}`
and `{long, long}`.

At call sites (`ir_sroa_scalarize_calls`, the function's own code too, before
SROA): a non-variadic call's argument in one or two eightbyte registers is
passed as its parts, each loaded from the argument's ptr before the call at its
own width (an i32 or i64, an f32 or f64: nothing past the object's end; a part
of another size keeps the argument as its ptr), with records of no size in the
parts' registers, and a result of one part (1, 2, 4
or 8 bytes in `rax`, 4 or 8 in `xmm0`) comes back as the call's value, which a
store after it puts where `into` pointed. The objects the call passed and
filled are then private, so the widening SROA and promotion keep them in
registers. A result of two eightbytes still comes back through its `into`
slot. `tests/ir-source/register-aggregates-page-end.c` passes objects that end
at an inaccessible page (native only).

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
(`s + (c ? x : 0)`). If both arms compute the same integer addition,
subtraction, OR or XOR on a common operand, the join can compute
`s op (c ? x : y)`. Both results must be private to the phi, and subtraction
requires `s` on the left in both arms. Each arm moves
at most four non-constant rows, each join takes at most four selects, and the
moved rows' cost plus two per select is at most eight. Since #2842 a join's
`f32` and `f64` phis convert too, their rows constants, copies, selects,
bitcasts, negations and proven loads, none of which raises a floating
exception. Integer-to-floating conversions move when their entire source range
fits the target's precision: through `i16` for `f32`, through `i32` for `f64`,
or a wider input with a qualifying zero/sign extension of those widths. A sign
extension only proves a signed conversion's range; looking through a zero
extension changes the proof to unsigned. Unproven conversions, floating
arithmetic and comparisons keep their arm's branch. These are policy bounds, not timings; [the design](../ir-optimization.md#scalar-if-conversion-1382) has
the rules.

`test-ir-optimize` runs `ir_if_convert_test.c`: complete SSA verification,
independent execution before/after cleanup, randomized results folded into a frozen
digest (#2924), the scans (conditional branches, barriers, keys' filter)
against their definitions on random columns, every guarded candidate-scan tail, multi-phi edge polarity, both triangle
orientations, sequential, nested and three-predecessor regions over rounds,
loop-carried values, shared switch/call side-table words, allocation rollback,
refusal of unsafe or expensive speculation, the reload and truth fixtures,
#2842's floating phis (`f32` and `f64` copies and negations converted, an
addition, a subtraction, a multiplication, a division and a dead comparison
refused, a floating load speculated behind the head's integer access of its
bytes and refused past them or volatile, drawing their trials from a stream of
their own), exact integer-to-floating conversions through both triangle
orientations and a diamond (including extension chains and unsigned
reinterpretations that must keep their branch),
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
check that a strong `__builtin_expect` hint keeps its conditional jump and a
balanced or dynamic expectation still permits a conditional move. They
require the branches to remain for loads, division, variable shifts, calls, a
reload after a call and the running max. #2842's floating selects (the issue's
kernels, the six double comparisons, `!(a < b)` and `!(a == b)`, the float
comparisons `<`, `!(a > b)` and `!=`, a mixed width, a triangle, sequential and
nested clamps, a magnitude, a reused load and a nested choice) are compiled at
x86-64, x86-64-v3 and x86-64-v4 and run against clang -O0 on NaNs, signed
zeros, infinities and both conditions; their decoded code has its blends
(`kmovw` and `vpblendmq` under k1 at x86-64-v4 for an integer condition, at
either width, `cmps*` and `pand` otherwise) and no branch but a loop's, the
nested clamp's outer test and those of an unproven load and a division.

## Dominator analysis (#1352, #2924)

The SCCP/DCE/CFG passes are scalar worklists since #2924 (#860's AUTO bound
and the vector passes went with it). The dominator analysis they prepare and
verify with has one path, SEMI-NCA and the scalar loop bodies
([why it is scalar](../dominators.md#why-it-is-scalar)).

The 64 MiB matrix budget bounds the frontier rows and the loop bodies; it is a
memory limit, not a measured crossover.

The verified-handoff fixtures check preparation, empty graphs and the
independently known diamond relation.
Every existing fixture still compares ordinary/verified optimized IR and all
dominance relations against a fresh rebuild.
