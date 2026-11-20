# tier1

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- Tier 1 selection publishes effective uses for `tier1_liveness.c`; the native
  consumer connects required locations, allocation, frames and spill/remat
  records. The integer core is covered by bottom-up rewriting over state tables
  `tier1_select_initialize` compiles from a grammar of the candidates' patterns
  (`tier1_select_burs.c`, baked), the AVX-512 labeling taking 16 nodes of a depth
  level. Maximal munch remains the grammar's fallback and a separate test
  reference. `test-tier1-select` checks every fixture's plan against a frozen
  digest (#2924) and its records; `test-tier1-pipeline` checks the kernels'
  definitions and complete plans on every function, and
  `benchmark-tier1-select-source` checks the kernels before timing.
  `test-checker-driver` requires byte-identical Tier 1 scalar/vector objects for
  the #1395 regressions at the same target, with either rematerialization mode,
  including a parameter-free leaf with volatile loads. Both production
  allocators use the same regular-class level rule; the independent scan remains
  a test reference, alongside the exact allocation-output comparisons. Bounded
  direct scalar calls preserve live caller registers and publish symbolic CALL
  references. Byte/word memory and integer conversions
  use canonical 32-bit storage, as do the `i1`, `i8` and `i16` `and`, `or` and
  `xor` (#2677), whose whole registers `test-tier1-emit` probes natively, with a
  byte's or word's load at a guard page that a 32-bit memory operand would cross;
  terminal chains retain explicit traps or empty self-loops. Integer comparisons fold single-use loads at their original
  access width when the final consumer's memory-order proof permits it
  (#1218), including exact immediate comparisons through zero/sign extension;
  a word's, doubleword's or quadword's constant within a signed byte takes the
  sign-extended `imm8` form (#2600).
  `test-tier1-select` checks guard-page boundaries, predicates, every constant
  each side of the byte's, word's and doubleword's signed bounds and its form,
  address aliases and access-order refusals; `test-tier1-emit` checks frame-object comparisons,
  realignment and fused branches. Every other function, general CFGs with loops and phis, goes
  through `tier1_emit_cfg.c` (#140): `tier1_cfg.c` allocates it, its plain and
  callee-saved-preferring problems in one call (#2538, `tier1_emit_cfg_one_call`;
  `--verify` compares the order before, `tier1_emit_cfg_two_calls`), and its edges
  move values, before a jump, at a successor's start or in a split critical
  edge's stub. The preference's crossing test counts the calls before each
  position with a vector prefix sum, which `test-tier1-emit` checks against its
  definition. A straight line whose allocation can't place a value (an evicted
  parameter whose incoming register another operand holds) goes through the CFG
  consumer too. The 128-bit integers are split into 64-bit halves before the
  optimizer (`ir_wide.c`, #141): `test-tier1-wide [--seed N] [--programs N]
  [--print] [--file IR]` runs hand-written and random 128-bit IR, and Tier 0's
  random 128-bit programs, through the split by both paths, the optimizer and
  both of Tier 1's paths natively, against the IR's interpreter on the unsplit
  function and the programs' stack machine; Tier 0's random long double
  programs, whose values the CFG consumer keeps in frame objects and computes on
  the x87 stack (`TIER1_RULE_X87`, #141), natively against their stack machine;
  and vectors wider than the target's registers (`ir_vector_split.c`, #1624),
  hand-written IR through the split into 16- and 32-byte pieces by both paths,
  the interpreter and the optimizer, natively at x86-64's pieces, with its
  refusals and its scan's two paths (`tier1_wide_vectors_test.c`).
  The convention's rarer aggregate shapes are taken; those of vectors still
  refuse explicitly ([the Tier 1 guide](../../docs/tier1.md)'s list). AVX-512's
  masks (#179) are selected (`kmov`, `kand`, `kor`, `kxor`, `knot`, and loads,
  stores, constants and bitcasts to and from integers of every width) and
  allocated in the opmask class by the CFG consumer, which moves and spills them
  at their widths and saves them around calls; and vectors (#141), in the vector
  class at their widths, passed and returned by value as aggregates in a vector
  register whole; at a target without AVX512VL (x86-64-v3), by VEX forms in xmm0 to
  xmm15, machine rows' roles below 16 too (#1281). Run
  `test-tier1-select`, `test-tier1-liveness`, `test-tier1-emit`,
  `test-tier1-pipeline --cross-check` and `test-tier1-masks` (hand-written mask and vector IR
  natively against the IR's interpreter; Tier 0's mask programs, every `_k*`
  builtin's row and every comparison into a mask (#1623) through the IR, SSA and
  the optimizer to Tier 1's native code; comparisons ANDed with a mask, which
  selection folds into one compare under that writemask, #1847) for bounded typed-stream/native
  validation; `benchmark-tier1-pipeline [--clang PATH] [--repeat N]` times the
  pipeline's fixtures by Tier 1, Tier 0 and clang `-O0`/`-O2` (bounded kernels,
  not programs; under the 9700X's lock, CPU 2), and `benchmark-tier1-select-source
  [--only select] [-E's options] file...` selection's core cycles per IR
  instruction on real C's functions, as `--tier1` prepares them (#141). A division by a constant that isn't a power of two is a
  multiplication by its reciprocal (Granlund and Montgomery), in either consumer,
  which saves the values `mul`'s `rax` and `rdx` would clobber (the [selection
  guide](../../docs/tier1-selection.md)). `measure-mask-spills` times a mask's spill and
  reload, to memory against to a general register (`mask_spill_measure.c`, #179's
  measured choice; the 9700X job runs it, `test-mask-spills` checks its programs).
  `test-tier1-model` checks the emitted-case measurement reader;
  `measure-tier1-model --manifest PATH --verify` constructs its eligible
  experiments without reporting timings. See [the Tier 1 guide](../../docs/tier1.md)
  for supported inputs and the complete C-source acceptance still pending.
- `measure-tier1-programs --clang PATH [--tcc PATH] --sqlite DIR --speedtest DIR
  --lua DIR --scripts DIR [--runs N] [--warmups N] [--directory DIR] [--keep]
  [--uncounted] [--variant OPTION]... [--check-code-size[=lua|sqlite|all]] [--sizes-only]`
  (`tier1_programs_measure.c`, #138, #1189 and #1116) builds the
  programs Tier 1 is measured by against clang -O2: speedtest1 (DIR/sqlite3.c,
  DIR/speedtest1.c, #136's flags and options) and Lua 5.4.8's interpreter (its
  33 sources) running the five scripts of the `cc3387f` record. Each is built
  by this compiler's Tier 0 and `--tier1 -fno-asynchronous-unwind-tables`, clang
  -O0 and -O2, and tcc (left out without `--tcc`), as that record's `corpora.sh`
  builds them: speedtest1 as one translation unit but by clang, which compiles an
  object of each file and links them. It checks that speedtest1's `--verify`
  gives one hash and each script prints clang -O0's output, then runs every
  build of every program in `--runs` rounds (11) of a Williams design's order, as whole
  processes (`profile.c`'s), each with its wall time and its user cycles and
  instructions, every speedtest1 run from a fresh database: its files and
  SQLite's temporary directory removed before it, outside its time, as the
  record's runs were. It prints a tab-separated `tier1-programs-run` line per
  run (its phase, round, position, program, build, wall time, counters, status
  and whether it was counted), each pair's medians, then a table of
  speedtest1's and Lua's (the sum of the scripts' medians) by build, with
  `--tier1` against clang -O2 and -O0. Every run must exit 0 and be counted
  (`--uncounted`, for a host without the counters, skips the latter). The
  caller pins it; the 9700X job runs it under the `tier1_programs` gate (Tier
  1's input and pipeline: the checker's lowering, the emission's Tier 1 step,
  the IR's passes and Tier 1's own modules; and the measurement: its harness,
  `profile.c` and `compile_measure.c`), and on its daily and dispatched runs,
  with the scripts from a pinned buster-performance commit and the other inputs
  by their hashes. Against the record, its clang builds link with clang rather
  than `cc`, and each run's counters are its own rather than `perf stat`'s of
  one run per build. `correctness.tests` checks its dispatch on every run: an
  incomplete command prints its usage, and one without its inputs fails its
  first build. Each `--variant` (four at most) adds a build of `--tier1`'s
  with OPTION after its options, named `--tier1 OPTION`, checked and timed as
  every build is, with its medians against `--tier1`'s in the table (#172's
  `--variant --tier1-schedule=post`: [the scheduling guide](../tier1-scheduling.md)).
- The same command reports **function sizes** separately for SQLite's
  `sqlite3.c` and all **34 Lua sources**, including `luac.c` and its `PrintCode`.
  Each source is compiled to a relocatable object by Tier 1 and Clang `-O0`
  with the same corpus flags. Exact symbol names match within that source;
  the runtime benchmark's SQLite unity build and linked startup code do not
  enter this comparison. The versioned `--tier1-function-sizes` manifest
  identifies actual source bodies and their tier; its sizes are checked
  against ELF function symbols before use. Helpers, fallback bodies,
  unmatched functions and executable-section totals have separate counters.
  Every report requires complete Tier 1 accounting and nonempty matched
  evidence. `--check-code-size` (or `=all`) also requires all emitted Tier 1
  function bytes no greater than Clang's matched bytes **for each corpus and
  each requested Tier 1 variant**. `--check-code-size=lua` or `=sqlite` enforces
  only that corpus while retaining both reports and their `enforced` fields.
  Repeated selectors combine their corpora. An
  unmatched Tier 1 body still costs its full bytes and contributes no Clang
  bytes. This conservatively handles functions Clang inlines everywhere and
  omits; a missing counterpart can only make the bound harder to satisfy.
  One larger function can be offset by smaller functions in the same corpus.
  A missing report, failed compile or malformed object fails the command even
  for a corpus without an enforced byte bound; failure files are retained.
  `--sizes-only` runs only this deterministic comparison and requires only
  `--clang`, `--sqlite` and `--lua`, with no scripts, speedtest source,
  hardware counters or timed program runs. Its object compiles run sequentially
  and the Tier 1 compiler uses one lane:

  ```sh
  ./build/compiler measure-tier1-programs --sizes-only --check-code-size --clang clang --sqlite /path/to/sqlite --lua /path/to/lua/src
  ```

  The regular program measurement reports the same sizes before timing, and
  its existing 9700X CI step enforces both bounds. Hosted correctness uses
  the same gates, the existing compiler build and verified offline inputs, and
  enforces c-testsuite's with `test-c-testsuite --tier1 --check-code-size`
  (#2646). It also checks the size reader and
  command with original synthetic sources. PR and merge-queue hardware policy
  is unchanged. These deterministic size checks make no runtime-performance
  claim and require no 9700X run.
- `measure-tier1-rows --clang PATH [--base COMPILER] [--src DIR] [--lexer
  FILE]... [--march ARCH] [--rounds N] [--passes N] [--runs N] [--warmups N]
  [--directory DIR] [--keep] [--uncounted]` (`tier1_rows_measure.c`, #1283)
  times vector code that machine rows run, by this compiler's Tier 0 and
  `--tier1`, clang -O2 and, with `--base`, another compiler's `--tier1` (the
  pull request's base's): `test-checker-driver`'s machine rows' unit (its other
  unit's rounds, `--rounds` of them), a lexer's byte classes over a 1 MiB
  C-like text by byte vectors and compares into masks (`--passes` times), and,
  with `--lexer`, the compiler's own lexer (`--src`'s
  `buster/compiler/c_lexer_benchmark.c`, linked with a main by clang) running
  `benchmark-lexer` on those files. Each unit is built by the build's compiler
  and each main by clang -O2, at `-march` (native by default). Tier 1's builds'
  `--tier1-report` gives each unit's records, machine rows and the moves around
  them (`row_moves`; `none` from a compiler before #1283). Every program's output
  must be the same by every build (the lexer's corpus check and tokens, and its
  exit status, which says its vector lexer agreed with its scalar one), and the
  lexer's `c_tokenize` median is printed; then the runs, as
  `measure-tier1-programs` times its own (`tier1-rows-run` lines), each pair's
  medians, the counts' table and the medians' table with `--tier1` against Tier
  0 and the base. The 9700X job runs it under the `tier1_programs` gate, with a
  pull request's base built in a worktree; `correctness.tests` checks its
  dispatch on every run.


## Vector-only value numbering and LICM (#2781)

The C driver's default Tier 1 pipeline includes `--tier1-passes=vectors`.
Functions with vector or mask values run the congruence solver on those values
before promotion and loop-invariant motion on them after if-conversion. Their
scalar values are not substituted or hoisted merely because the pass is
enabled. Before promotion a local's reads are loads, which value numbering
never merges, so what the solver shares is equal vector constants (byte-class
tables, splats of constants) and the expressions over them; two `a + a` of a
local `a` stay apart, as they do with `+gvn`. `--tier1-passes=-vectors` restores the previous default pipeline;
explicit `+gvn` or `+licm` selects the corresponding unrestricted pass.
The report's `gvn_removed` and `licm_moved` include the restricted passes'
counts. Inline summaries use the same policy and retire the optional step
arenas after each summary. The IR guides describe the exact scalar/SIMD twins
and the per-loop LICM walk; the driver regression checks both host paths,
native output, and the counters with the policy on and off.

## Integer truth round trips (#1218)

The C driver's Tier 1 path runs `ir_truth_simplify` after wide-integer splitting
and before optimizer preparation. An equality test against an integer
comparison's true encoding reuses its predicate. Inequality uses its complement.
Against zero the choices are reversed. Bounded walks through copies and integer
extensions on either operand establish the encoding. For example, sign-extending
i1 to i8 and then zero-extending to i64 gives 255 for true. Reversing the
extension kinds gives 1. Truncations, other producers and mismatched constants
end the match. The pass changes only comparison operands and predicates in the
lane-owned IR, before use lists and verified-analysis revisions exist. The
optimizer then removes dead chains and verifies the resulting SSA. Its
64-byte opcode/type candidate scan uses masked tails. The `truth-encoding` IR
pair in `test-ir-optimize` checks both constant positions, mixed extensions
through i128 and the comparisons that must survive.
If-conversion (#1382) runs `ir_truth_simplify_values` on its own rounds'
columns, which also turns a select of truth values tested against zero into an
i1 select (`c && x` as `select c, x, c`), as
[the IR optimization guide](ir-optimization.md#bounded-scalar-if-conversion-1382)
says.

`tests/driver/tier1_truth.c` is the standalone native regression, also run by
`tools/ci/correctness.tests`. From the repository root with a built compiler:

```sh
clang -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tests/driver/tier1_truth.c -o build/test-driver-tier1-truth && ./build/test-driver-tier1-truth build/compiler "$(command -v clang)"
```

The supervisor compiles the subject with `--tier1-strict --verify`, by the
default and scalar host paths. Clang compiles a separate execution oracle.
All ten integer predicates and their negations, nested and shared truth
values, truncation, nonzero tests, volatile side effects and unordered floating
comparisons are checked. GNU `objdump` must find all 21 branch-only functions,
each with one `cmp`/`test` and no `setcc`; missing functions fail the test.
Failed commands and execution are fatal, and failure artifacts are retained.
CI builds the supervisor with its configured sanitizer flags as well.

This regression is not #1218's program-performance acceptance. The
speedtest1/Lua compare-and-branch counts and 9700X runtime evidence remain
necessary before closing the issue. No runtime improvement is inferred from
the peephole alone.

## Block placement and critical-edge fall-through (#1218)

The C driver's final Tier 1 optimization enables `BUSTER_IR_OPTIMIZE_LAYOUT`.
The shared optimizer's default options leave it off. `ir_optimize_layout.c`
orders surviving blocks into fall-through chains during the existing rewrite,
before selection, allocation and final SSA verification. Entry remains first.
Unconditional edges take priority over conditional ones, natural-loop back
edges over ordinary incoming edges, and existing adjacency breaks equal
priorities. A cycle loses one fall-through link, preferring to keep its loop
back edge. This can place a loop's body before its test while retaining the
entry jump to the test. No instruction crosses a control-flow edge.

The rewrite's existing maps rename values, phi inputs and predecessor IDs,
direct and switch targets, call arguments, and computed-goto labels. ABI
records, vector constants and machine rows retain their established ownership.
The emitter and certificates still consume contiguous blocks and monotonically
ordered records. When both conditional edges need move stubs, the emitter puts
the stub targeting the next physical block last, allowing its moves to fall
through without an additional jump.

`test-ir-optimize` exercises all 16 combinations of SCCP, dead-code removal,
CFG cleanup and layout on the existing semantic fixtures, including loops,
phis, switches, labels, calls and volatile effects. New fixtures cover reversed
chains of 0 through 65 blocks with forward SSA references and call argument
lists, irreducible control flow, moved aggregate calls and vector payloads.
The scalar/vector results must match, independently interpreted behavior must
agree, and every chain jump must target the following block. Retained-analysis
tests rebuild CFG and dominance after layout. `test-tier1-emit` additionally
checks both critical-edge stub orientations with actual native execution.

The ordering is a static heuristic, not a profile. Its compile-time cost and
program run time need locked 9700X measurements. The staged rank kernel follows
the optimizer's resolved scalar/vector path: its existing AUTO heuristic
selects scalar for the recorded corpus functions, while forced-vector tests
check the exact twin. That SCCP heuristic is not a measured layout crossover.
#1218's <=1.3x Clang `-O2`
dynamic compare/branch count remains a separate measured gate, using #1189's
unchanged classifier and the same speedtest1 and five Lua inputs. Raw evidence
belongs in `buster14a/buster-performance`; normal correctness tests do not
establish that gate.

`test-checker-driver`'s table-loop object oracle checks the one table-base LEA
and its exact PC32 relocation, the adjacent MOVSXD/ADD/indirect-JMP dispatch,
and a decoded CFG proof that every path to dispatch executes the table-base
LEA exactly once. A zero-trip guard may branch directly to the return before
the LEA. The proof resolves all four switch entries using their PC32 symbol,
addend and slot offset, checks all direct targets at instruction boundaries,
requires indirect targets strictly after the LEA and propagates entry/after-LEA
states through both direct and table edges. This covers placement where the
loop closes through backward table entries and dispatch follows the last
backward direct branch. The semantic Clang comparison and all four scalar/
rematerialization configurations remain checked. Controls reject repeated
LEAs, bypass to dispatch before the LEA, direct reentry, and indirect reentry
from the second table entry, whose slot offset is nonzero.
The checker-driver's own process needs its x86 control-effects tables for this
object proof. TSan's and MSan's PIE executables cannot bake those tables,
even though their compiler child processes initialize their own. The test
initializes the effects on demand and asserts jump, conditional-branch and
return classification before applying the table-base proof (#1829).

## Truth values, stored constants, identities and dispatch (#2554)

Four of #138's patterns in Tier 1's executed code (its record's `patterns.py`),
each removed where it is made:

- **Truth values across blocks** (`ir_thread.c`). `&&`, `||`, `!` and `?:` leave
  a block of phis, a short pure chain that tests them and a conditional branch.
  The C driver threads such a block after `ir_truth_simplify` and before
  if-conversion. A predecessor that jumps to it takes a copy of its chain and
  branch, the phis read as that predecessor's inputs. A predecessor with a
  conditional edge to it goes on that edge straight to the successor the chain
  chooses, where its inputs are constants (one jump where both of its edges then
  reach one block). The truth rewrite then runs again, so a copied
  `icmp ne (zext c), 0` is `c`, the branch's own comparison. The block's values
  must be read only by itself and by its successors' phis on its edges, but for
  one case (#2608): a phi read past the block, where one successor other than
  the entry has the block as its only predecessor and dominates every reader (an
  instruction's block, or a phi entry's predecessor). Lua's `tonumberns` is a
  `?:` whose join's phis are the truth value and the number it set, which the
  arithmetic reads after the test. That successor then starts with a phi that
  merges the number again from each predecessor that now reaches it (and from
  the block, where the block stays), and the readers read it: SSA repair over
  the successor's dominance subtree. The repair is a truth value's: the block's
  test reads none of the phis read past it, and is a constant on the edge of the
  first constant input of the first phi it reads. A data select's join is left
  to if-conversion: in `long y = x < lo ? lo : x; return y > hi ? hi : y;` no
  edge decides the test, and a clamp to a constant (`y > 255`) tests the value
  it clamps. Threading either made the driver's if-conversion test's `if_clamp`
  three conditional branches where main has two cmovs. A value of the chain
  still may not be read past the block, and a block that looks like a loop's
  header in block order (a predecessor past it, or a phi input at or past its
  start) isn't repaired, since threading it would rotate the loop; a header laid
  out after its body with no such input is threaded, which rotates the loop
  exactly. Whether a successor dominates a reader is a walk back over
  predecessors from the reader's block that stops at the successors the block
  alone enters: the reader is dominated by the one it meets where it neither
  meets both nor reaches the block itself (through its other successor), and a
  walk past 64 blocks refuses the repair. These tests are cheap and come first,
  and no dominator tree is built: #2608's first form built one in each round
  with a block to repair, 1,153 of them on sqlite3.c (160,053 blocks in all),
  where the walks now visit 1,294 blocks (320 on Lua's 33 sources). A successor
  that only the block enters and that has phis or a merge keeps a predecessor: a
  block whose predecessors would all be redirected to its other successor is
  left alone, its test the same constant on every edge (Lua's `checktab`), which
  SCCP folds; #2607's rule had left such a successor's phi empty. It is not the
  entry or a label's block, and its chain has at most four operations, at most
  sixteen predecessors take copies, and the rewrite runs up to four rounds
  (`a && b && c`). A next round runs where this one changed something and left a
  block to it: by the roles that keep a round's blocks apart, a block to repair
  whose readers no successor dominated yet, or a block of phis that took a copy
  and whose other operations are chain operations, which then has a candidate's
  shape (`tonumberns(a, n) && tonumberns(b, m)`: the `&&` join first, then both
  `?:` joins). A block no reachable predecessor reaches any more is left an
  `unreachable` alone. `test-ir-optimize` runs fifty-three IR text fixtures
  (`ir_thread_test.c`): `&&`, `||` with `!`, a loop's found flag, a truth value
  read past its test (left alone: the test reads it), a switch's kept edge with
  mapped successor phis, a collapse, `a && b && c`, a label's block and a
  self-loop; and #2608's `tonumberns`, `tonumberns && tonumberns` and
  `tonumberns && x < y` (two rounds each), readers through the successor's
  subtree and on an edge out of it, a chain value read past the test, readers
  where both successors' paths meet, a successor with a second predecessor, a
  loop's header (beside a `?:` join that is threaded), a kept edge, a merge in
  each successor, a reader on a loop's back edge in the subtree, an `&&` whose
  right side is a `?:` (two rounds), two repairs, one inside the other's
  subtree, whose reads are copied, merged and passed to a call, a vector a
  ternary intrinsic reads (verified, not executed), a repair the roles left to a
  second round, three blocks whose redirects would strand a successor's merge or
  phi, two whose redirects strand a successor that has neither (threaded) and
  two whose redirects leave a successor with a predecessor of its own, a merge
  reached by one of two redirects, one kept by the block's own edge, a data
  select's only phi tested and read past it behind a kept edge (left alone) and
  the same number beside a truth value (merged again and read by a select), a
  number read past two successors that reach each other (no walk), the entry as
  either successor (it has no phi to merge in, and it dominates every reader:
  the other successor merges the number, or the block is left alone), a block of
  70 phis, past a 64-byte mask, the driver's `if_clamp`, a clamp to constants, a
  truth value that no edge decides and one compared with a value from outside
  the block, a number read where both successors meet (each left alone), one
  read two blocks into the false successor's subtree, two repairs in one round
  whose second block's number is its first phi, a loop's header refused for its
  later back edge alone, another whose back edge is its 18th predecessor, a `?:`
  join refused for a phi input from a later block (the cheap test's false
  refusal), a header laid out after its body (threaded: the loop rotated), and a
  number read 63 and 64 blocks of jumps past the successor, the second also on
  an edge from it (the walk's bound: merged, then left alone twice). #2608's
  fixtures draw their random trials from a stream of their own, so that the
  programs generated after them stay the ones value numbering with memory's
  digest was frozen on. Each is verified as complete SSA, folded with the pass's
  work (the rounds run and the blocks the walks visited) into a frozen digest
  (#2924), and executed against its input after threading and after the truth
  rewrite and the optimizer. The candidate scan and both passes of the escape
  scan are checked against their definitions.
- **Stored constants** (`tier1_select.c`, `tier1_select_burs.c`). A store of a
  constant is `mov $imm, mem` at its width, an 8-byte store's where the constant
  fits a sign-extended imm32, rather than the constant's register. The candidates
  carry it as `compare_immediate` with no stored value. Maximal munch ranks it
  above the register's form, and the grammar has a store of `IMM` with its own
  action. A seq_cst store's exchange still takes a register.
- **Identities** (`BUSTER_IR_OPTIMIZE_IDENTITY`, Tier 1's alone). After SCCP, an
  integer operation that returns one of its operands is removed:
  - `x + 0`, `0 + x`, `x - 0`, `x | 0`, `x ^ 0` and a shift by zero (Lua's
    `>> 0` instruction decoding);
  - `x * 1`, `1 * x` and a division by one (`n * sizeof (char)`);
  - `x & -1`.

  Its users read the operand instead, and DCE keeps the operand rather than it
  (a shift is a root otherwise). `test-ir-optimize` runs every pass mask with
  and without it.
- **Dispatch** (`tier1_emit_cfg_dispatch`). A jump to a block whose first record
  is an indirect jump takes a copy of that record. That is GNU C's computed
  goto's dispatch block, so each handler ends in its own `jmp *%reg`, as LLVM's
  tail duplication gives. The copies leave the branch columns. The code
  certificate takes a copy as its arrival at the block
  (`tier1_cert_emitted_walk`), and the emitter counts them
  (`Tier1EmitResult.dispatch_copies`, which `test-tier1-pipeline` checks).

`tests/driver/tier1_patterns.c` is the end-to-end regression, run by
`tools/ci/correctness.tests` as the truth one is. It compiles each pattern's
functions by `--tier1-strict --verify` on both host paths and runs them
against Clang's oracle. objdump must find no setcc in the truth functions, six
immediate stores and no constant in a register in the store function, and no
shift by zero. In the computed goto, it must find at least four indirect jumps
and no jump to one. In #2592's fetch, `i = *pc++` at each handler's end, at
least three indirect jumps must follow the target's load with no register move
between them, and at least three increments of `pc` must be in place (`add
$0x4`). In #2768's `patterns_spilled`, a handler that calls in one arm as Lua's
`OP_GETTABUP` does, every indirect jump but the function's first must follow
the load of its target at once, with no reload of a loop value between them.
In #2745's objects that a 64-byte vector's value initializes whole (a struct's,
a union's, a compound literal's, and a union's other member read back), no
store of a zero immediate may be left; a volatile object and a union whose
designator moves to another member after the vector's keep their eight. In
#2922's selects of floating `==` and `!=` (one of each, a loop's sum of them
and a running last match), a `cmov` of the parity flag and no `setcc` must be
left, and in a conditional scale by `==` (`c = p[i] == t ? c * 3 : c + 1`),
whose accumulator recurs through the select's values by an operation, the truth
value's `setcc`s and no parity `cmov`. In #2784's `patterns_gsub`, Lua's `str_gsub` loop, no register may be
stored back into the memory the instruction before loaded it from:
#2784's `patterns_gsub`, Lua's `str_gsub` loop, no register may be stored back
into the memory the instruction before loaded it from. In #2843's conditional
sums, `s += p[i] > 0 ? p[i] : 1` over an `int`, an `unsigned` and an
`unsigned short` array, whose select reads the load's register for its
extension, each must have a cmov and no register moved into another. #2870's
`patterns_sector`, SQLite's `pagerWriteLargeSector` cut down, leaves out a
store into a slot two blocks after its reload, which the code's certificate
must prove from the run's entries:


```sh
clang -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tests/driver/tier1_patterns.c -o build/test-driver-tier1-patterns && ./build/test-driver-tier1-patterns build/compiler "$(command -v clang)"
```

The program measurements (speedtest1 and Lua's five scripts against
#138's record) are in buster-performance, as #2554 asks.

## Address-arithmetic selection regressions

`test-tier1-select` includes `tier1_address_arithmetic_test.c` (#1217) on
both the scalar and bottom-up paths. It checks every power-of-two multiply
in 32 and 64 bits (both operand orders and separate/coalesced destinations),
signed byte/word/dword loads at sign boundaries and a protected page end,
memory-order and shared-load counterexamples, and full-width pointer-integer
addition folded into a memory operand. An addition of a load, in both operand
orders and both widths, takes the load as its memory operand, and one with its
sum in the base's register loads into it rather than through the scratch
register; a load another addition also reads stays one load (#2602).
`test-tier1-emit`'s symbol fixtures do the same with a rip-relative symbol. `test-x86-target-selection` also
reaches these fixtures through the existing memory-plan tests.

The index walk also folds same-block, single-use I64 shifts and power-of-two
multiplies into the addressing mode's scale when the combined scale is at most
eight. It carries the current scale through constant additions/subtractions:
`(i + 3) << 2` contributes displacement 12, while `(i << 2) + 3` contributes 3.
Narrow arithmetic before an extension, shared index producers and larger scales
remain computed. Existing pattern kinds and intrinsic costs stay unchanged;
the BURS load/store/copy actions select their filled address operands without
new grammar tables. The regression checks both multiply operand orders, scales
of one through eight and a refused scale of sixteen, exact effective uses,
separately used constants, both pointer-address forms, and native memory and
returned addresses against the independent IR interpreter, including I32 wrap
before widening and high-bit I64 inputs. The C driver's host-reproducibility
fixture also exercises scaled loads and an intervening store against Clang,
with rematerialization enabled and disabled and byte-identical host-path objects.

Memory operands may also fold shared same-block pointer conversions,
full-width additions and address nodes. Each access publishes the original
base/index as its effective uses; the shared producer remains only when another
selected user needs its address value. A frame address may be shared in its
block or the entry preheader. A pure address node of an earlier block folds
too when every user takes it as an address (`tier1_select_sinkable`), so that
no node computes it; one that another user keeps stays computed. The walk
still stops at narrow arithmetic, additional indices and out-of-range
displacements. This folds address computation without moving, merging or
widening an access. `test-tier1-emit` executes shared and escaping
frame/offset addresses with rematerialization on and off, kept frame pointers
and realigned frames, and `tier1_address_sink_test.c` an entry block's address
stored and loaded in both arms of a branch, by both selection paths, with and
without a frame pointer: folded into each access where nothing else uses it,
and kept, its value the accesses' base, where the returned sum does.

A direct symbol's address folds into its memory operands rip-relative
(#2573, [the selection guide](../tier1-selection.md)). `test-tier1-emit`'s
`tier1_symbol_fold_test.c` plans a function of a direct, a GOT, a local-exec
and an initial-exec symbol by both paths with the access column: the direct
symbol's load, store, byte comparison and word comparison fused with a branch
fold it from another block, no node computes its address, and the others keep
their sequences; without the column every symbol folds and the consumer, given
it, refuses the plan. The code certifies and runs natively with its `PC32`,
GOT and thread-pointer references resolved by the test, the comparisons'
addends covering their immediates. Its repeated-access fixture (#2601) has the
lowering's three symbol addresses and index extensions for three indexed
accesses of one table in one block: every access reads the first of each, by
both paths, and the code has one `lea` and runs natively; with the index's
sign extensions, which the parameter's signed storage holds, every access
reads the parameter itself (#2843). `tier1_extension_test.c` loads a byte, a
word or a doubleword with two uses, a comparison and an extension to I64
that a select reads, in one block and across a branch, by both paths: the
select reads the load wherever its storage holds the extension, a word's
sign extension stays its own, and a load the extension alone reads is its
`movsx`'s operand; each runs natively against its oracle. `tests/driver/tier1_symbols.c` is the
standalone native regression, run by `tools/ci/correctness.tests`: every
width's loads, stores, comparisons with an immediate after the displacement
field and branches on them, the operations that take a memory operand,
floating accesses, a sequentially consistent store, a loop, comparisons with
a constant a signed byte holds (#2600), indexed (three accesses sharing one
`lea`, #2601) and address-of uses, and `sqlite3LogEstAdd`'s shape, whose two
extensions of one operand to I32 must stay apart, by both host paths with `--tier1-strict --verify`, linked by
the system linker against a Clang oracle; `objdump -dr` must find no `lea` of
a direct symbol but the indexed table's and the address's, each branch fused
with its comparison, and the GOT's and thread-locals' sequences, with no
`PC32` to a symbol the unit reaches indirectly. From the repository root with a
built compiler:

```sh
clang -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tests/driver/tier1_symbols.c -o build/test-driver-tier1-symbols && ./build/test-driver-tier1-symbols build/compiler "$(command -v clang)"
```

`tier1_address_coalesce_test.c` checks that a simple unscaled, undisplaced
base-plus-index uses `add` when allocation puts the result in either input
register. This removes the SIB byte while keeping one instruction. Other
destinations, scales, displacements and `rsp` bases retain `lea`. Folded frame
operands have their final base and offset before this decision. Both selector paths execute the
32-bit, 64-bit and pointer fixtures against the independent IR interpreter.
Flags are dead between IR nodes, as required by the existing emitter contract.
Its in-place cases run a base plus each class of displacement (one, imm8,
imm32, both signs) in the base's own register, an `add` (or `inc`, `sub`), and
in another register or with no displacement, a `lea`, and a signed
power-of-two quotient and remainder at both widths with the bias added in the
dividend's register or by a `lea` into another, all natively against C. Its
fixed-register cases run a shift by `cl` from every value, count and
destination register, and a division and remainder by a register from every
dividend, divisor and destination register, at both widths, natively against C,
and check that only the cases that need it go through the scratch register
(#2592).

`test-tier1-cfg` counts the block entries where a recomputed value's register
reads nothing before its recipe takes over (`tier1_cfg_entry_unread`), which
its edges leave unfilled, and fails if its generated cases reach none; the CFG
verifier and the certificate check every such entry the cases reach. It counts
too the ranges `tier1_cfg_carried_unread` proves, which carry their value only
to blocks that take it as their recipe (#2734), fails if the cases reach none,
and marks ranges the proof refused, each of which must fail the verifier. Half
its generated cases have fixed registers' sequences (#2767,
`Tier1CfgInput.sequences`), some with readers, operands and results, from a
generator of their own; it recomputes each range's avoided registers apart from
`tier1_cfg_crossed` and fails if the cases reach no range that avoids one or no
reader's range across its own sequence. `test-tier1-emit`'s
`tier1_sequences_test.c` compiles values that live past a division and a shift,
a shift's count that lives past its own shift, a divisor and a shifted value
that end at their sequences and a shift whose count ends at it; it checks that
nothing is saved around a sequence, no row is given for one and nothing moves
into the scratch register, and runs the function natively against C with every
register and with seven.

The immediate-multiply candidate and both corresponding BURS productions
have cost 3, versus 2 for a copy and shift and 1 for a 3/5/9 multiply's lea.
This is a latency-biased selection heuristic, not a measured runtime result
or an instruction-count bound. Signed loads retain their exact access width
in the selected pattern's `width`; its `source_width` is the 32- or 64-bit
result. Their existing single-use, same-block and memory-barrier checks
remain mandatory. I8-to-I16 sign extensions keep their canonicalization,
and narrow arithmetic is never widened by the address walk.

These local rules do not establish #1217's corpus-wide acceptance targets.
Address values that escape, register pressure and table-base placement still
need corpus evidence. Record speedtest1/Lua dynamic
counts and approved-host runtime results in `buster14a/buster-performance`,
not in this source repository; use `measure-tier1-programs` for the runtime
comparison described above.

For the instruction breakdown, retain the generated programs with `--keep`
and use the corrected [#1189 `gap.c`](https://github.com/buster14a/buster-performance/blob/e44d737737d8b811065931fd82a5c034a6e9c39c/docs/measurements/tier1-gap-1189-2026-11-03/tools/gap.c):
`GAP_INSTRUCTIONS=1 gap count DISASSEMBLY CALLGRIND OBJECT BASE` retains the
per-instruction rows needed for non-frame LEAs and power-of-two immediate
IMULs. Disassemble the same retained executable and derive its own Callgrind
object/base mapping; a historical binary's mapping does not qualify a fresh
one. These opcode counts need no origin-tagging compiler patch.
