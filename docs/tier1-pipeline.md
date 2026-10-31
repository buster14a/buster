# Typed-stream to native Tier 1 integration

`tier1_pipeline_test.c` checks the existing producer and consumers together:

```text
tier0_emit / tier0_program_functions (including abi_assign)
  → ir_lower_typed / ir_lower_typed_scalar
  → ssa_ir_promote / ssa_ir_promote_scalar
  → ir_optimize (SCCP, DCE and CFG cleanup)
  → rebuilt CFG and dominance; both IR verifiers
  → tier1_select_plan
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

The source is original project test code. It appends the established typed-stream
columns through `tier0_emit` and obtains the real System V ABI view through
`tier0_program_functions`. It does not manufacture an alternate frontend or
substitute handwritten SSA for the producer. Every stage owns a separate arena;
derived CFG and dominance columns are rebuilt after optimization. The selected
plan's effective uses feed the production liveness consumer.

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

Each supported function takes three `uint64_t` parameters and returns a
`uint64_t`. Fifteen original fixtures cover:

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

A successful complete run reports 1,920 native results, including twelve
emitted call sites across the scalar/vector and rematerialization combinations,
and 28 functions through the CFG consumer, whose edge moves and stubs it counts:
each one's allocation must pass `tier1_cfg_verify` as well. A cross-check
additionally reports 960 Clang results. Original typed symbols
resolve to helpers compiled through the same complete pipeline; relocation
metadata, signed rel32 range and encoder instruction ends are checked before
execution. The check count also includes stage validation,
allocation/frame assertions and failure contracts; it is not a corpus size.

Two focused harness regressions run even without Clang. One deliberately changes
a valid typed shift after freezing the oracle and requires both source-mutation
detection and a semantic difference from the frozen result. The other hangs a
command, its child and a detached grandchild; it requires a timeout, reaping of
all three processes and proof that both descendant PIDs no longer exist.

## Run time

`benchmark-tier1-pipeline [--clang PATH] [--repeat N] [--seed N] [--dump
DIRECTORY]` compiles each supported fixture four ways: by Tier 1 (the vector
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

An additional valid typed fixture, floating-point arithmetic, passes through
lowering, promotion and optimization on both paths and must return
`TIER1_SELECT_ERROR_UNSUPPORTED`. Its two refusals must preserve the failing
stage's result and restore its output and scratch arena lengths. It is never
executed as native code.

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
Indirect/variadic/wide-ABI calls, floating point, dynamic/variadic frames, dynamic shifts, division,
undefined values and unsupported conversions remain outside this integration
subset. See [selection](tier1-selection.md), [liveness](tier1-liveness.md),
[allocation](tier1-allocation.md), and [frames](tier1-frame.md) for their contracts.

These are bounded integration fixtures, not source-corpus qualification. The
source driver does not yet provide complete typed function streams for arbitrary
C programs. No c-testsuite, SQLite, `speedtest1`, or other source corpus is compiled
through this path here. The tests do not establish general C correctness,
performance acceptance, target-host evidence, or issue closure. See the producer
boundaries in [Tier 0](tier0.md), [IR](ir.md), and [SSA](ssa.md).
