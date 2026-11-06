# Type checking into SSA IR: stage 4

This page maps [#130](https://github.com/buster14a/buster/issues/130) to its
implemented components, acceptance commands and recorded evidence. The audit
of `d9442df0dde83b9701b6ff7487f8916641736a84` on November 6, 2026 found one
remaining component acceptance item: [#134](https://github.com/buster14a/buster/issues/134)
required native Tier 1 results to join the comparison of both SSA paths and
the all-memory interpreter. `test-ssa-source --tier1` supplies that comparison.
The introducing pull request records its tested revision and qualification;
the older corpus checks and historical measurements below are separate evidence.

## Components and consumers

The parser dependencies [#127](https://github.com/buster14a/buster/issues/127)
and [#128](https://github.com/buster14a/buster/issues/128) are completed. The
stage's components and their acceptance boundaries are:

| Component | Status | Production consumer and owning guide |
| --- | --- | --- |
| [#131: per-function IR](https://github.com/buster14a/buster/issues/131) | Completed | `ir_lower_typed` consumes the checked function's `Tier0Function`; verifiers, SSA, optimizers and Tier 1 consume its arena-backed columns. [IR](ir.md). |
| [#132: types](https://github.com/buster14a/buster/issues/132) | Completed | The checker uses interned types, conversions and record layout. [Types](commands/types.md). |
| [#133: checking and folding](https://github.com/buster14a/buster/issues/133) | Completed | The source reader and expression checker produce typed values and explicit conversions; function lowering supplies the executable typed stream. [Checker](c-checker.md). |
| [#134: local SSA](https://github.com/buster14a/buster/issues/134) | Strict native comparison added | `ssa_ir.c` promotes eligible locals in the shared IR; address-taken and otherwise ineligible locals retain memory storage. `test-ssa-source --tier1` compares both native paths with the interpreters. [SSA](ssa.md). |
| [#175: text IR](https://github.com/buster14a/buster/issues/175) | Completed | `ir_parse.c` reads the printer's format; source round trips and pass fixtures use it. [IR tools](ir-tools.md). |
| [#176: interpreter](https://github.com/buster14a/buster/issues/176) | Completed | Typed-stream and IR execution share the interpreter; source tests compare their output with independent expectations. [Interpreter commands](commands/ir-tools.md). |

Tier 0 consumes the typed function directly. Tier 1 lowers the same function
to the shared IR, constructs SSA, verifies it and passes it to optimization
and selection. The checker, IR adapter and SSA constructor each retain their
scalar implementations and their comparisons; a successful comparison does
not replace an independent C-language or execution oracle.

## Acceptance evidence

### Source semantics and identical typed results

`test-checker-corpus` compares acceptance and the first error's presumed file
and line with `clang -fsyntax-only` under the same source options. It also
compares the scalar and vector checkers' columns. Its semantic mutations
change declaration types, remove declarations or exchange operands; fixed
differences become regression fixtures. The
[checker corpus guide](commands/checker-corpus.md) owns the commands, and
[the recorded campaigns](c-checker.md#corpus-comparison-and-semantic-mutations)
identify their inputs, counts, fixed discrepancies and exclusions. A unit
that neither implementation preprocesses contributes no first-error-line
comparison.

The shared [correctness inventory](../tools/ci/correctness.tests) retains
frozen SQLite and musl mutation runs, SQLite's scalar/vector/deferred
comparison, and the repository-source comparison with its documented
sanitizer memory exclusions. The existence of these commands is the
regression contract; the historical campaign results are not a fresh run of
the current tree.

### Verified IR and SSA

`test-ir-source --dominators` requires both typed-to-IR paths to produce the
same columns, verifies them, and verifies both promotions with dominance.
`test-ssa-source` additionally uses `ssa_ir_promote_verified`, including the
core SSA verifier and a check for trivial phis, before execution. This
verified test path exercises the vector constructor past the production
scalar cutover wherever its memory bound permits.

The [October 30 target-host record][ssa-record], at compiler
`12bf987ae3514fe2dcdc4170e937590b8b62b949`, includes:

| Input | Functions | Acceptance recorded on the 9700X |
| --- | ---: | --- |
| All 220 pinned c-testsuite programs | 430 | Both SSA promotions verify; every program has its expected output in all-memory IR and both SSA forms. |
| Frozen `sqlite3.c` | 2,602 | Every selected function verifies through both promotions; both produce 6,097 phis in total. |
| Accepted musl units | 1,918 | Both promotions verify; 305 of the 1,355 input units are explicitly left out for named unsupported features. |
| 48 generated C programs | 288 | Both promotions verify and all three IR forms give Clang's output. |

These counts describe that recorded revision and its exact input options.
SQLite and musl's library sources have no `main` in this check: their rows
establish structural and SSA acceptance, not execution of a complete library
test suite. [The SSA guide](ssa.md#real-and-generated-c) describes the source
harness and its failure controls. The present `--typed` mode additionally
executes the retained typed stream; it was not part of this October 30 record.

### Programs run

[#145](https://github.com/buster14a/buster/issues/145#issuecomment-6416698757)
records all 220 single-exec programs passing on the 9700X at c-testsuite
`5c7275656d751de0e68b2d340a95b5681858ed07`. The
[c-testsuite harness](c-testsuite.md) checks native output, scalar/vector
determinism, function records and, with `--cross-check`, objects linked by
the system toolchain. Its separate `--tier1` run permits documented Tier 0
fallbacks; it does not by itself establish #134's native SSA comparison.

SQLite's library and `speedtest1` also run. The
[October 27 record][sqlite-record] built SQLite 3.53.2 with Tier 0's object
and direct-link paths, Clang `-O0` and tcc; every build produced the same
verification hash on the 9700X. The
[November 3 record][tier1-programs-record] adds Tier 1 and Clang `-O2` to the
runtime comparison, with the commands and scope described in
[the Tier 1 guide](tier1.md#where-the-gap-to-clang--o2-is-1189).
These executions satisfy the stage's requirement to run real SQLite code.
They do not decide the broader SQLite test-suite scope still tracked by
[#146](https://github.com/buster14a/buster/issues/146), including its open
choice of `testfixture` coverage.

### Strict native comparison

`test-ssa-source --typed --tier1` compares six executions of each runnable
source: the all-memory IR, vector SSA, scalar SSA, the retained typed stream,
and native Tier 1 code compiled by the default and scalar host paths. Each
native compilation uses `--tier1-strict --verify` and must report that every
function prepared for the comparison was compiled by Tier 1, with no fallback.
Excluded inputs fail this mode. Each program must finish normally with the
same exit status and captured output, including any independent expected
output from c-testsuite or Clang's generated-program oracle.

The correctness inventory enables this comparison for all 220 pinned
c-testsuite programs, 48 generated programs, 24 more generated programs with
local lifetime markers, and a 512-byte aggregate/variadic/indirect-call fixture
variant. The original fixture's 520-byte by-value aggregate continues through
all four interpreted forms; the native variant uses Tier 1's documented
[512-byte stack-aggregate boundary](tier1-selection.md#supported-slice).
Its self-test retains the interpreter failure controls and adds native
compile refusal, missing coverage, wrong output/status, failed launch and
parent-enforced timeout controls. Successful 127/255 exits remain valid.
Compilation preserves relative source/header paths; the runs use separate
directories and matching main arguments and environment. The
[SSA guide](ssa.md#comparing-tier-1s-compiled-programs) specifies the boundary.

This is a production-driver comparison: the two native builds construct their
own SSA through the respective production paths, including their documented
algorithmic cutovers. They do not emit the harness's already-promoted IR. The
independent forced-constructor and verifier checks remain in the interpreted
forms. Introducing-revision CI results belong in the linked #130/#134 pull
request; neither the command inventory nor an older record is a new test pass.

## Historical 9700X cycles per source token

The component benchmarks originally report cycles per body node, typed entry
or IR instruction. The following conversion uses each record's own parser
token count, including preprocessed headers:

`cycles/source-token = cycles/work-item * work-items/source-tokens`

The values are derived from already rounded reports, so the final digits are
approximate. These are historical measurements of the named compiler commits,
not measurements of the current tree or a claim that the frontend's 3 ms
checking goal is met.

### Checking expressions in function bodies

The [October 28 schedule record][checker-record] measures compiler
`fe35a8b5703202bb3d6e42bf0b70b2d787cf10a9`: 311,601 body nodes and 706,237
source tokens. Each process reports a median of 21 rotating passes; the table
uses the median of its three process reports
([1][checker-log-1], [2][checker-log-2], [3][checker-log-3]).

| Checker path at that commit | Cycles/body node | Approximate cycles/source token |
| --- | ---: | ---: |
| Scalar postorder | 246.746 | 108.868 |
| Vector postorder batches | 175.253 | 77.324 |
| Vector level order | 185.116 | 81.676 |

The timed work is the extracted body graph with warm type tables. Source
reading, type-table preparation, declarations outside those graphs and
executable typed-stream lowering are outside its timer. The denominator
normalizes that work to the input file; it does not turn it into complete
semantic-analysis time. The vector default subsequently changed to node order;
[the checker guide](c-checker.md#node-order-or-regions) records that later
decision separately.

### Lowering the typed stream and constructing SSA

The [October 30 SSA record][ssa-record] measures compiler
`12bf987ae3514fe2dcdc4170e937590b8b62b949`: 396,928 typed entries, 598,425
lowered IR instructions and 706,204 source tokens. Each of the six
`bench-N-head.log` files reports 21-pass medians. The table uses the median
of those six reported cycles-per-entry values
([1][ssa-log-1], [2][ssa-log-2], [3][ssa-log-3], [4][ssa-log-4],
[5][ssa-log-5], [6][ssa-log-6]).

| Operation | Cycles/typed entry | Approximate cycles/source token |
| --- | ---: | ---: |
| Lower to all-memory IR, default path | 255.655 | 143.693 |
| Lower to all-memory IR, scalar | 259.465 | 145.835 |
| Promote to SSA, default path | 783.220 | 440.216 |
| Promote to SSA, scalar | 781.700 | 439.361 |

Promotion includes input verification, CFG and eligibility analysis,
dominance, construction and cleanup, rebuilding the IR and output
verification. Its default path uses the documented scalar cutover for large
functions. These timings are not isolated phi-construction costs. The
[record's interpretation correction][ssa-correction] limits its comparative
performance claims; the table above reports absolute normalized costs and
claims no improvement over another build.

That legacy record's counter reader retained no enabled/running times, so its
reported cycles do not prove full counter coverage. Its process reports also
do not preserve the inner 21-pass samples. The conversion preserves those
limits; it neither repairs the historical measurement nor qualifies a later
compiler revision.

The checker and IR records use different revisions and preprocessing options,
including the checker's `--clang-features`, and have different token counts.
Do not substitute the 706,237-token denominator into the IR record, combine
these independently sampled medians into an end-to-end timing, or substitute
the earlier IR record's 705,670 tokens. Full compile attribution belongs to
[the compile-time guide](compile-time.md).

## Commands that preserve acceptance

Run these through the approved host's lock as [AGENTS.md](../AGENTS.md)
requires. The owning guides specify frozen inputs and preprocessor options;
the correctness inventory is the executable suite definition.

| Boundary | Commands and guide |
| --- | --- |
| Types and checking | `test-types`, `test-checker`, `test-checker-corpus`; [checker corpus](commands/checker-corpus.md). |
| IR and text | `test-ir`, `test-ir-lowering`, `test-ir-source --dominators`, `test-ir-parse`; [IR](commands/ir.md), [IR tools](commands/ir-tools.md). |
| SSA and execution | `test-ssa`, `test-ssa-ir`, `test-ssa-source --typed --tier1`, `test-ssa-source --typed --tier1 --generate 48 --clang clang`; [SSA](commands/ssa.md). |
| Execution failure controls | `test-ssa-source --typed --tier1 --self-test`; [SSA](commands/ssa.md). |
| Native source programs | `test-c-testsuite --cross-check`, `test-c-testsuite --tier1`; [c-testsuite](commands/c-testsuite.md). |
| Source measurements | `benchmark-checker-source`, `benchmark-ir-source`; [checker](commands/checker.md), [IR](commands/ir.md). |
| SQLite execution and whole compile | `measure-tier1-programs`, `measure-compile`; [Tier 1](commands/tier1.md), [compile time](commands/compile-time.md). |

The strict native comparison completes the missing consumer in the original
acceptance map. Closing #134 and #130 requires the introducing revision's
qualification to pass these commands; a historical interpreter-only pass or
an ordinary Tier 1 run that permits fallback cannot substitute for it.

[checker-record]: https://github.com/buster14a/buster-performance/blob/f7fd1b498aa19706204b1b8e82f486db480d526c/docs/measurements/checker-schedules-133-2026-10-28/README.md
[checker-log-1]: https://github.com/buster14a/buster-performance/blob/f7fd1b498aa19706204b1b8e82f486db480d526c/docs/measurements/checker-schedules-133-2026-10-28/logs/bench2-head2-1.log
[checker-log-2]: https://github.com/buster14a/buster-performance/blob/f7fd1b498aa19706204b1b8e82f486db480d526c/docs/measurements/checker-schedules-133-2026-10-28/logs/bench2-head2-2.log
[checker-log-3]: https://github.com/buster14a/buster-performance/blob/f7fd1b498aa19706204b1b8e82f486db480d526c/docs/measurements/checker-schedules-133-2026-10-28/logs/bench2-head2-3.log
[ssa-record]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/README.md
[ssa-log-1]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/logs/9700x/bench-1-head.log
[ssa-log-2]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/logs/9700x/bench-2-head.log
[ssa-log-3]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/logs/9700x/bench-3-head.log
[ssa-log-4]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/logs/9700x/bench-4-head.log
[ssa-log-5]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/logs/9700x/bench-5-head.log
[ssa-log-6]: https://github.com/buster14a/buster-performance/blob/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30/logs/9700x/bench-6-head.log
[ssa-correction]: https://github.com/buster14a/buster-performance/blob/224d16c36bc688bc40ddc2a6b30fe7f4f537ce95/docs/measurement-review-notes/pr149-timing-interpretation-2026-10-30.md
[sqlite-record]: https://github.com/buster14a/buster-performance/tree/c1bbfb2e56f58deb4972ff16d50221d7cd82f0fd/docs/measurements/speedtest1-tables-136-2026-10-27
[tier1-programs-record]: https://github.com/buster14a/buster-performance/tree/6495a438f6a3d3b0c8605e24d16f5da996910dad/docs/measurements/tier1-gap-1189-2026-11-03
