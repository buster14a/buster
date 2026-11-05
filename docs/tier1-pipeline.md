# Typed-stream to native Tier 1 integration

`tier1_pipeline_test.c` checks the existing producer and consumers together:

```text
tier0_emit / tier0_program_functions (including abi_assign)
  → ir_lower_typed / ir_lower_typed_scalar
  → ssa_ir_promote / ssa_ir_promote_scalar
  → ir_optimize (SCCP, DCE and CFG cleanup)
  → rebuilt CFG and dominance; both IR verifiers
  → tier1_select_plan_verified (as the C driver plans ir_optimize's verified
    result), checked against tier1_select_plan's plan
  → tier1_emit_function
      → a straight-line chain: effective-use liveness, required input/definition
        locations and fragment allocation
      → any other CFG (tier1_emit_cfg.c): tier1_cfg.c's allocation over the CFG,
        with edge moves
      → actual spill frame, saves/restores and fragment transitions
  → branch sizing
  → baseline x86-64 record validation
  → vector and scalar encoders
  → native execution
```

The source is original project test code. The original scalar fixtures append
the established typed-stream columns through `tier0_emit` and obtain the real
System V ABI view through `tier0_program_functions`. The boundary fixtures below
append the same typed columns and provide explicit ABI and label descriptors,
frozen before either backend reads them. Neither manufactures an alternate
frontend or substitutes handwritten SSA for lowering. Every stage owns a separate
arena; derived CFG and dominance columns are rebuilt after optimization. The
selected plan's effective uses feed the production liveness consumer.

## Running the checks

The driver command is:

```sh
./build/compiler test-tier1-pipeline
./build/compiler test-tier1-pipeline --seed 123 --cross-check
./build/compiler test-tier1-pipeline --clang /path/to/clang
```

`--clang` also enables the cross-check. The default seed is `0xb138134177`.
The seed changes input bits; fixture shapes and constants stay fixed. The command
runs one job at a time. Clang temporary sources and executables go into a private
directory under `TMPDIR` (or `/tmp` if unset); successful runs remove them and
failed runs print the retained directory. Native execution runs in a child with
a time limit; native faults become test failures. Each external command has a
private supervisor with a monotonic deadline, an isolated process group and Linux
subreaper ownership. Completion or timeout kills residual descendants and reaps
them before the result is accepted, including descendants that detach with
`setsid`. Cleanup has a separate two-second bound and incomplete cleanup fails.

The integration command must be included after `tier0_generate.c`, `tier0_test.c`,
`ir_lower.c`, `ssa_ir.c`, `ir_optimize.c`, `tier1_emit.c` and their dependencies.
`run_tier1_pipeline_tests` takes the arguments after the command name. It makes no
changes to compiler options or production routing.

## Supported fixtures and independent oracles

Each original scalar fixture's entry takes three `uint64_t` parameters and
returns a `uint64_t`. Seventeen fixtures cover:

| Fixture | Handoff exercised |
| --- | --- |
| Local complement plus a shifted parameter | Parameter initialization, local writes and promotion, folded shift input lifetimes |
| Twelve separately defined nonconstant terms | Long live ranges after promotion, real spill stores, frame slots and reloads |
| Sixteen separately defined wide constants | Ordinary constant spills and rematerialization after eviction |
| `uint64_t`/`uint32_t` conversions and local arithmetic | Narrowing, unsigned wraparound, zero extension and shared local uses |
| Direct call to a constant-returning local helper | Zero-argument calls, ordinary symbolic references and caller stack alignment |
| Three-argument local helper with values live across the call | Argument permutation, preservation across CALL and helper/entry compilation through the complete pipeline |
| The byte-swap-16 typed recipe followed by a widened result | Wide mask/shift expansion, `I16` truncation and canonical zero extension |
| Signed byte, word and dword locals | Local promotion and signed widening, including `I8` through `I16` to `I64` |
| An early return: `a ? b : c` by two returns | A conditional branch, two epilogues, the CFG consumer |
| `do { b += a; a >>= 1; } while (a)`, a loop back to the original entry | Lowering's preheader, loop-carried parameters, a back edge's moves |
| `a ? 17 : 29` through a local | A join's phi of two constants |
| A loop whose carried values swap | Phis whose back-edge moves can form a cycle |
| Eight values and a counter around a loop | Pressure past five registers across a back edge: spill slots meeting edge moves |
| A call of the helper in a loop | Caller-save rows around a call inside a loop, the result carried by a phi |
| `x = a; if (b & 1) x ^= c; return x + (b >> 1)` | A critical edge into a phi's join, through a stub |
| `(uint64_t)((double)a * 0.75 + 1.0)` | Floating values (#141): an unsigned 64-bit integer to a double and back, past 2^63 both ways, `F64` arithmetic in the vector class, the CFG consumer |
| `a`, with the stack's position saved and restored around it | The dynamic stack (#141): a frame pointer, and the saved position in a register, the CFG consumer |

Every fixture runs through scalar and vector paths, with rematerialization both
disabled and enabled. A five-register pool contains the three required System V
parameter registers. The rematerializing configuration also keeps a frame
pointer and disables the red zone. The tests require actual spill/reload counts
for pressure cases, actual rematerialization for constant pressure, a valid
allocation, one compact frame row per spilled value and dedicated call-save rows.

Thirty-two inputs combine zero, one, 8/16/32-bit sign boundaries, high-bit values,
`UINT64_MAX` and deterministic random values from the existing Tier 0 input
generator. Shifts are within width and arithmetic is unsigned, so the C oracle
has no signed-overflow or shift undefined behavior.

Expected bits come from the existing `tier0_test_interpret`, which reads the
original typed stream directly **before any lowering, promotion or optimization**.
The results are frozen once per fixture, and a byte-exact snapshot of every
original typed column and descriptor is checked after compilation on both paths.
The interpreter shares neither the IR promotion/optimizer nor the selected
machine plan. Both encoders must produce identical bytes before native execution.
The optional cross-check compiles separately written C
formulas with Clang at `-O0` and `-O2`, using `-march=x86-64`, and checks every
fixture/input pair against the typed interpreter. It does not translate selected
instructions or optimized IR back into C.

A successful complete run reports 2,176 native results from these seventeen
fixtures, including twelve emitted call sites across the scalar/vector and
rematerialization combinations, and 36 functions through the CFG consumer,
whose edge moves and stubs it counts: each one's allocation must pass
`tier1_cfg_verify` as well. A cross-check additionally reports 1,088 Clang
results. Original typed symbols resolve to helpers compiled through the same
complete pipeline; relocation metadata, signed rel32 range and encoder instruction
ends are checked before execution. The check count also includes stage validation,
allocation/frame assertions and failure contracts; it is not a corpus size.

Two focused harness regressions run even without Clang. One deliberately changes
a valid typed shift after freezing the oracle and requires both source-mutation
detection and a semantic difference from the frozen result. The other hangs a
command, its child and a detached grandchild; it requires a timeout, reaping of
all three processes and proof that both descendant PIDs no longer exist.

## Native Tier 0 boundary comparisons (#789)

`tier1_pipeline_boundary_test.c`, included by the pipeline command, adds five
fixtures. Both backends compile exactly the same frozen `Tier0Function` columns
and ABI descriptors. Tier 1 must lower, promote, optimize, verify, select,
allocate and emit successfully; a refusal is a failure, never a Tier 0 fallback.

| Fixture | Boundary exercised |
| --- | --- |
| Runtime-selected label address carried through a local and phi | Exactly one indirect dispatch; both label symbols relocated to their emitted records |
| A two-word aggregate passed by value, mutated by its callee and returned in two registers | Argument, definition and result ABI records; independent callee storage; result-object capture and a value live across the call |
| A variadic definition called with only general-register and stack arguments | `%al=0`, saved general arguments and the incoming overflow area's address |
| A variadic definition with eight register doubles and a ninth on the stack, mixed with integer arguments | `%al=8`, all eight vector save slots and mixed overflow values |
| A variadic definition with seven named integer arguments | The overflow anchor starts after the named argument already on the stack |

The thirty-two inputs and both scalar/vector paths are reused, with
rematerialization off/on; the latter also keeps a frame pointer and disables the
red zone. These fixtures use the full available general-register mask rather
than the original pressure fixtures' five-register pool. Their native results
must agree with both Tier 0 and independent unsigned C formulas. The variadic
call sites are typed host-C calls into each emitted callee, not another typed
stream being tested as the caller. This tests the incoming frame contract
without claiming coverage of every variadic call-lowering shape.

The loader validates branch layout, baseline target forms, ordered symbolic
references, both encoders' bytes and field ends, label bounds and rel32 ranges
before making code executable. Each comparison runs in a child with a five-second
alarm; a fault, timeout, missing compile or result mismatch fails the command.
The suite requires 640 native differential pairs (1,280 native results), reported
separately from the original fixtures' counters. `--cross-check` still adds Clang's
`-O0`/`-O2` comparison for the original seventeen fixtures; it does not turn the
boundary suite into an external Clang cross-check or a performance benchmark.

## Run time

`benchmark-tier1-pipeline [--clang PATH] [--repeat N] [--seed N] [--dump
DIRECTORY]` compiles each original scalar fixture four ways: by Tier 1 (the vector
path with rematerialization, every caller-saved register and the red zone), by
Tier 0 (`tier0_test.c`'s builder, from the same typed stream), and by clang at
`-O0` and `-O2` (`-march=x86-64`) from the fixtures' C formulas, as shared
objects loaded in place. Every compile's results on the 32 inputs must be the
typed interpreter's before anything is timed. Then 21 passes call each
compile's function on every input `--repeat` (2,000) times, the compiles'
order rotating by pass, and the report gives each one's median in ns per call
and their geometric means. `--dump` writes each fixture's Tier 1 and Tier 0
code for `objdump -D -b binary -m i386:x86-64`. These are bounded kernels, not
programs: a call's cost is a large part of the shortest ones'.

On the 9700X (CPU 2, medians of five processes), the geometric means were 2.18
ns per call for Tier 1, 2.25 for Tier 0, 3.55 for clang `-O0` and 2.08 for clang
`-O2`. The loops: `do { b += a; a >>= 1; } while (a)` took 9.62 ns by Tier 1,
11.41 by Tier 0 and 9.56 by clang `-O2`; the swap loop 2.68, 2.68 and 1.99; eight
values around a loop past the registers 5.05, 4.44 and 3.72; a call in a loop
4.71, 4.80 and 4.26. One process each of the swap loop ran 42% (Tier 1) and 50%
(Tier 0) slower than the other four; the medians take all five.

## Unsupported cases and limits

No fixture here is intended to be refused: the floating-point fixture that had to
return `TIER1_SELECT_ERROR_UNSUPPORTED` is the floating fixture above since Tier
1 takes floating values (#141). Refusals, with their rollback of the result and
the arenas, are `test-tier1-select`'s (unsupported operations, an over-aligned
slot's address, the straight-line emitter's calls) and the C driver's
(`test-check-lanes --tier1-outcomes`, whose mixed fixture's long double Tier 1
refuses, as [the outcomes guide](tier1-outcomes.md) says).

The straight-line consumer takes one block or a physical straight-line chain
whose intermediate terminators branch to the next block and whose final block
returns, ends in explicit unreachable, or contains only a branch to itself. The
terminal consumer tests exercise these last two forms separately; they preserve
a real self-loop even when a preceding helper unexpectedly returns. Every other
function goes through the CFG consumer (`tier1_emit_cfg.c`, #140): its plan
becomes `tier1_cfg.c`'s problem (each node's effective uses, its definition's
required register, phis at their blocks' starts, parameters at entry), and its
records take each edge's moves before the predecessor's jump, at the
successor's start, or in a stub after the branch for a split critical edge,
whose jumps then go to the stub. Jumps to the next block are left out, and a
branch whose true successor follows is inverted. Where the IR's entry has
predecessors, an empty entry block goes before it. The terminal tests' nonempty
self-loops, conditional returns and phis go through it now, keeping their calls
and jumps back, with no trap in their place.
The boundary suite covers indirect jumps, one by-value aggregate ABI shape and
variadic definitions including stack arguments. The fixtures do not cover
indirect or variadic call lowering, every aggregate classification, variable
shifts, division or undefined values; the broader native coverage remains in
`test-tier1-emit` and `test-checker-driver`'s `--tier1` fixtures. What Tier 1 still
refuses is [the Tier 1 guide](tier1.md)'s list. See [selection](tier1-selection.md),
[liveness](tier1-liveness.md), [allocation](tier1-allocation.md), and
[frames](tier1-frame.md) for their contracts.

These are bounded integration fixtures, not source-corpus qualification. The
source driver does not yet provide complete typed function streams for arbitrary
C programs. No c-testsuite, SQLite, `speedtest1`, or other source corpus is compiled
through this path here. The tests do not establish general C correctness,
performance acceptance or target-host evidence. See the producer boundaries in
[Tier 0](tier0.md), [IR](ir.md), and [SSA](ssa.md).
