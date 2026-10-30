# Local SSA construction

`ssa.c` implements local promotion for [#134](https://github.com/buster14a/buster/issues/134).
It takes a completed CFG and a view of the local accesses in its instruction
columns. It returns read replacements and phis without prescribing the rest of
the IR. `ssa_build` is the vector path; `ssa_build_scalar` is its independent
predecessor-reading twin. Both are included in the existing compiler executable.
`ssa_ir.c` connects the core to #131's `IrFunction`, including the all-memory
functions produced by `ir_lower.c` from Tier 0's typed stream.
Both constructors use #174's shared `dominators_analyze` interface; the vector
constructor consumes its frontier rows and the IR adapter uses its subtree
intervals directly.

Both paths are checked on every function of the corpora and of random C
programs, and the programs run in each of their three forms by the shared IR's
interpreter ("Real and generated C" below); the comparison with Tier 1's
compiled code waits for Tier 1 (the end of this guide).

## Input and handoff

`SsaInput` contains block instruction ranges, successor CSR, instruction
kind/type/local/operand columns, and local type/flag columns. An instruction's
original ID is its position in these columns. Its kind describes only what
promotion needs:

| Kind | Contract |
| --- | --- |
| `VALUE` | An ordinary instruction, opaque to this pass. Its own ID names its result. |
| `READ` | Read `locals[i]`; the original instruction ID names the loaded value. |
| `WRITE` | Assign original value `operands[i]` to `locals[i]`; this instruction defines no value. |
| `ADDRESS` | Produce the address of `locals[i]`, excluding that local from promotion. |

All source instructions remain in the view, so original value IDs do not need
renumbering before construction. The pass checks that every WRITE's source
dominates it and has the local's type. The full IR verifier must check hidden
VALUE operands, terminators, memory semantics and every other IR rule.

The type column distinguishes scalar integer widths, floating types and pointers
from aggregates. Integer signedness and enum identity do not change promotion;
the producer maps them to the appropriate width. Volatile, atomic,
address-taken and aggregate locals stay in memory. A bounded 64-entry scan finds
every ADDRESS before construction, even if its flag was omitted or it occurs
after earlier reads and writes. Parameters are ordinary entry VALUEs followed
by WRITEs; an unwritten local starts with its own typed `undef` value.

Entry is block 0 and must have no incoming edge. A producer whose original entry
is a loop target inserts a new entry first. Blocks unreachable from entry are
marked for removal; their incoming edges do not contribute to reachable phis.
Duplicate successor edges are preserved. A phi has one operand per supplied
reachable incoming edge, ordered by source block and then source edge position.
Supplying a CFG whose successors are already deduplicated gives one operand per
predecessor block, as #131's proposed CFG requires.

`SsaResult` contains:

- Promotion and reachability byte columns.
- A replacement for each original instruction ID: identity for VALUE, ADDRESS
  and memory READ; `BUSTER_SSA_NONE` for WRITE; the reaching value for promoted
  READ. A promoted read in an unreachable block gets its local's undef.
- Phi block and local columns, operand offsets and a flat operand column.
- Predecessor offsets, predecessor blocks and original incoming edge IDs.

Original IDs occupy `[0, instruction_count)`. The next `local_count` IDs are
typed undefs; `phi_base + phi_index` names a retained phi. Undef is distinct from
`BUSTER_SSA_NONE` and is not a wildcard: `phi(undef, value)` must retain the
uninitialized incoming path.

The consumer rewrites **all** original operands through `replacements`, removes
promoted READ/WRITE operations, inserts phis at block entry and discards
unreachable blocks. A WRITE for an unpromoted local remains a store even though
its replacement is NONE: NONE describes its lack of a value, not permission to
delete it. Surviving phis are compacted after cleanup; no consumer should use a
provisional phi ID.

Every output column starts at a 64-byte boundary. Counts describe valid elements;
no readable tail padding is promised. Producers need no input padding or
alignment. Consumers use bounded or masked tail accesses. The output arena owns
the result until reset; scratch returns to its original length on every outcome.
On failure, both arena lengths and the caller's result object remain unchanged.
The two arenas must be distinct and disjoint from the input columns. Capacity
exhaustion and ID-space exhaustion return errors instead of truncating data.

## Construction

### Vector path

1. Classify promotion eligibility and discover addresses 64 entries at a time.
2. Build a complete predecessor CSR, preserving duplicate edges, and run the
   shared dominator analysis. Its vector solver uses aligned 512-bit
   intersections and change masks, then supplies immediate dominators,
   reachability, subtree intervals and dominance-frontier bitsets. Compact
   unreachable-source edges out of the SSA predecessor result afterward.
3. Collect each block's final writes and upward-exposed reads. Reuse
   `dataflow.c`'s AVX-512 liveness equations over dense promoted-local IDs.
4. For each local, expand iterated dominance frontiers with 512-bit ORs,
   intersect with live-on-entry blocks, and insert only the required phis.
5. Find nearest dominating definitions by pointer jumping. Up to 64 blocks use
   four in-register tables and permutes; larger functions use scalar indexed
   loads instead of hardware gathers. Entry queries start at the strict
   dominator so a later write in the same block cannot satisfy an earlier read.
6. Stable counting-sort READ/WRITE accesses by `(block, local)`. Within each
   segment, 16-wide prefix maxima find the most recent write, and a permute
   produces the reaching value for every read. Fill phi operands from each
   predecessor's exit value.

The scalar operations here include CFG walks, bitset construction, stable-sort
placement and indexed loads above the 64-block register-table limit. Their
costs need measurement on the 9700X before claiming that any particular
placement or batch size is optimal.

SSA reserves at most 16 MiB for the combined quadratic regions needed by the
shared analysis: its frontier output span, temporary dominator matrix and one
temporary reachable row. With `row_bytes` rounded to a 512-bit boundary, the
vector path requires `(2 * blocks + 1) * row_bytes <= 16 MiB`. Above this bound,
`ssa_build` uses the scalar constructor and requests only the shared scalar
dominator tree. It does not request frontier or loop matrices on that path.
This is a storage policy, **not a measured performance crossover**. The IR's
promotion (`ssa_ir_promote`) adds one (#787): its vector path's phi placement,
entry definitions and renaming each pass over every (block, slot) cell, where the
scalar constructor's work follows the instructions, so a function of more than
16,384 cells (`BUSTER_SSA_IR_VECTOR_CELLS`) takes the scalar constructor. On the
9700X, timed per function on the frozen `sqlite3.c` (`benchmark-ir-source
--functions`), the vector path was up to 9% faster from 2^5 to 2^14 cells, even
below them, and slower from 2^15, by 67% on `sqlite3VdbeExec`'s 763,840 cells.
Every bound from 8,192 to 32,768 cells gave the corpus's promotion time within
−0.28% to +0.26% of the 16,384-cell bound's; the power-of-two bounds 8,192 and
32,768 were within 0.14%. See the
[retained measurement record](https://github.com/buster14a/buster-performance/tree/11e845e033dc696b47969899e6837c1ed2e6218c/docs/measurements/ssa-vector-cutover-787-2026-10-29).

The shared analyzer requires distinct output and temporary arenas. SSA carves
two disjoint, bounded views from its existing scratch tail: a size-derived
span for retained analysis columns, followed by temporary storage. The views
own no mappings, and only the completed analysis output remains after the
call. This preserves the public SSA APIs without per-function memory mappings.
Linear analysis columns and alignment remain bounded by scratch capacity.
Current-definition tables are dense in `blocks × promoted locals`; the caller's
arena also bounds that remaining storage.

### Scalar path

The scalar constructor reads a variable's reaching definition lazily through
predecessors. It maintains separate entry, final-write and current-instruction
definitions. Before following predecessors it installs a provisional phi;
query frames live on an explicit stack. This handles loops without recursive
calls, including single-predecessor chains. All predecessors are already known:
the API accepts a filled, sealed function and does not expose incremental
unsealed-block construction.

### Shared cleanup

Both paths canonicalize read aliases, remove trivial phis and collapse redundant
phi cycles. An explicit Tarjan stack finds strongly connected phi components.
A component with one external value collapses to it; a component with several
external values revisits its inner subset through an explicit task stack. This
last step is needed for minimality in irreducible graphs. Cleanup never treats
an undef operand as interchangeable with a defined operand.

Only phis reachable from original promoted reads survive compaction. Every
exposed read and phi operand is resolved again after cleanup, including aliases
changed by an inner component.

## Shared IR adapter

`ssa_ir_promote` and `ssa_ir_promote_scalar` take an `IrFunction`, distinct
output and scratch arenas, and a separate output `IrFunction`. They return an `IrCheck`
and publish the result only on success. The input is immutable; scratch is
restored and output allocation is rolled back on failure.

The adapter uses the shared IR's deduplicated CFG and value-use descriptions.
A typed slot is eligible when every use of its STACK_ADDR definitions is the
address operand of a full-type, nonvolatile LOAD or STORE. Address escapes,
computed addresses, mismatched access types, volatile accesses and excluded
slot flags keep the entire slot in memory. Untyped storage stays in memory;
typed temporary slots can promote. Eligibility relies on the supplied IR
metadata; the source producer is responsible for marking volatility and
address-taking correctly.

Block 0 must have no incoming edge. The current typed-stream adapter already
provides a parameter/slot preheader, and sends source-entry backedges to the
following block. A different producer must provide the same precondition.

The result has compact reachable blocks and retained stack slots. Promoted
LOADs, STOREs and their unused STACK_ADDR definitions disappear; typed UNDEFs
and new phi prefixes represent reaching definitions. Existing phis are also
rewritten and lose operands from removed predecessor blocks. All direct value
operands, branch targets, call arguments, switch targets and phi pairs are
remapped. Variable-operand slices are rebuilt separately, so input slices may
share storage without a value rewrite corrupting a block ID or metadata.
Existing IR phis are preserved even if dead-edge removal makes them trivial;
the core's trivial/SCC cleanup applies to the newly constructed local phis.

The adapter checks reachable input uses against the shared dominator tree and
verifies the rebuilt function with both shared IR verifiers. It borrows the
analysis's `preorder` and `subtree_end` intervals for input checks and remaps
them to the compact output block IDs for verification.
Slot size and alignment are preserved for retained storage; final frame offsets
remain the backend's responsibility. The implementation was first tested against the
published IR in PR #322, commit
`7a6ed05745a6b522209005d2423103caedb6c99a`, without changing IR-owned files,
and since against its merge head `e1df89446d001be3d45e65cae739b491811e38e1`.
Promotion needs no refusal of Tier 0's variadic frame state of its own: both
`ir_lower_typed` paths refuse a function with `incoming_variadic` or a local
role other than ADDRESS_TAKEN, and the adapter first runs the full IR verifier,
which refuses any slot flag other than ADDRESS_TAKEN, VOLATILE and TEMPORARY.
Shared dominance comes from PR #327, commit
`8f75a585aeaddd58e614a64c0afc6a914e99bd15`, without changing its implementation.

## Verification and tests

`ssa_verify` validates promotion, reachability, exact predecessor multiplicity,
types, dominance and canonical IDs, and rejects trivial phis. Phi operands are
uses at their respective predecessor exits. It then solves independent symbolic
reaching-definition equations and replays local accesses. This catches wrong
same-type, dominating substitutions which a structural verifier alone accepts.

```sh
./build/compiler test-ssa
./build/compiler test-ssa --seed 7439 --count 150
./build/compiler test-ssa-ir
```

The test interpreter compares all-in-memory local execution with each SSA form
on the same bounded paths. Every dynamic execution of an opaque VALUE produces
a fresh token; definedness is tracked separately. Phi operands are read
simultaneously before phi results are written. Thus an accidentally reused value
from an earlier loop iteration, or sequential phi assignment, cannot pass merely
because a constant happened to match.

Fixtures cover reads before writes, multiple writes, undefined incoming arms,
loops, redundant irreducible phi cycles, duplicate edges, unreachable cycles
and incoming edges, scalar types, and memory exclusions. Generated CFGs include
diamonds, nested/backward edges, irreducible regions, random edges and unreachable
regions. Boundary cases exceed 512 blocks and exercise exactly 511, 512, 513 and
1,025 promoted locals. Guarded input tails cover the byte-mask and instruction
batch boundaries. Invalid inputs, damaged result columns and allocation failures
must fail with intact caller state. Deliberately swapped phi operands and wrong
dominating read values must fail both the verifier and the interpreter.

After shared-dominator integration, local qualification used Clang 23.1.1 on a
cloud AVX-512 AMD EPYC 9V74. The optimized and ASan/UBSan core suites each passed
3,471 checks with seed 7439 and 150 random programs. The added handoff regression
checks the 8,191/8,192-block memory boundary, successful construction and
tree-only verification above it with an 8 MiB scratch arena, nonzero arena
prefixes and input immutability. Clang's bundled sanitizer runtimes were used.
Leak checking was disabled in the sandbox; address and undefined-behavior
checks remained enabled. These checks are not target-performance acceptance.

`test-ssa-ir` compares generated Tier 0 execution, its all-memory shared IR,
and both promoted IR forms, including return bits and global memory, on
programs of values and on as many with frame objects and `COPY` between them
(#136's objects, lowered since #371), which stay in memory while the locals
around them are promoted. Its
test-only executor reuses the typed-IR arithmetic model and adds simultaneous
phi entry, switches and symbolic indirect calls. Execution coverage is limited
to that model's values of at most 64 bits; wider IR types still have structural
and promotion-contract tests, and require the general interpreter for full
execution acceptance.

The adapter suite passed 2,038 checks in optimized Clang 23 and ASan/UBSan
builds after shared-dominator integration, both with seed 7439/count 16 and
128 generated functions. Each run covers
92,844 input and 83,260 output instructions, 2,566 input and 868 retained slots,
and 586 constructed phis. These are coverage counts, not performance results.
Hand fixtures check escapes, volatile and mismatched accesses, typed temporaries,
untyped storage, typed undef, existing phis with dead predecessors, indirect
calls, all value/block operand forms, overlapping variable slices, hidden
non-dominating uses and transactional failures.

The combined compiler also built at `-O3` with Clang 23.1.1 on the cloud host.
Its integrated SSA and adapter commands passed 3,471 and 2,038 checks. The
shared dominator suite passed 9,345,990 checks with seed 7439/count 4.
The whole-program no-recursion gate checked 33 translation units,
16,859 functions and 92,557
calls with no unapproved cycle. The cloud-only compiler invocation added
`-mmwaitx` because the VM hides that feature; no production build flag changed
and lane waits were not executed there.

## Real and generated C

`test-ssa-source` (`ssa_source_test.c`) is #134's acceptance on C programs: the
functions `-c` compiles of each file, as `test-ir-source` prepares them
(`ir_source_prepare`), lowered with every local in memory and promoted by both
paths, and each program with a `main` run in each of its three forms by the
shared IR's interpreter (#176).

```sh
./build/compiler test-ssa-source $(sed -n 's|^\([0-9][0-9]*\)$|build/c-testsuite/tests/single-exec/\1.c|p' tests/c-testsuite/subset.txt)
./build/compiler test-ssa-source --generate 48 --clang clang
./build/compiler test-ssa-source [preprocessor options] sqlite3.c
```

### The checks

Each promotion goes through `ssa_ir_promote_verified`, which runs `ssa_verify` on
the core's view of the function and its result before the IR is built: every use
dominated by its definition, a phi operand per predecessor, no trivial phi, and
the independent reaching-definition equations. The core's result is the output's
phis and replacements one for one (`ssa_ir_fill`), so these are the output's
checks. There the vector path's constructor runs wherever its memory bound
admits, past the 16,384 cells where `ssa_ir_promote` takes the scalar
constructor (#787), so that it is checked on SQLite's largest functions too. The
rebuilt IR then passes both IR verifiers with dominance, as every promotion's
does, and `ssa_ir_phis` checks the IR itself: the lowering makes no phi, so a
trivial phi there (of one value but itself, however many times) would be the
adapter's.

### Running a program

A file with a `main` runs in its three forms at once, a child process each, in a
directory of its own (some of c-testsuite's programs write files), with standard
input from `/dev/null` and standard output and errors into a file, fully
buffered as a native program's are into a file. The three must end alike: the
same wait status, interpreter outcome and output. A run the interpreter couldn't
finish fails, however its forms agree: every form must exit normally, so a
matching timeout or other signal is not a pass. An interpreter resource or
step-limit result also fails. A program that calls a native `exit` may finish
normally without an interpreter record; those exits remain supported.
The empty record must still be readable; a missing, unreadable or partial
record fails. Child setup, output-flush or record-write failure terminates the
child with uncatchable `SIGKILL`, rather than aliasing a normal `exit(255)`.
Where `FILE.expected` exists
(c-testsuite's convention), the program must exit with status 0 having written
its bytes.

`test-ssa-source --self-test` checks this boundary on six tiny C fixtures,
through the real preparation, promotions and three child processes, without
expected-output sidecars: a normal nonzero return and a native `exit` must pass;
three identical wall-timeout signals and three interpreter step limits must
fail. The controls check every process status and retained interpreter record.
An admitted program that closes descriptors3–4095 invalidates the child's
record descriptor while leaving captured stdout/stderr open; the resulting
record-write failure must fail all three forms, with their empty records kept.
A separate native `exit(255)` must still pass without an interpreter record.
Unexpected failures retain their work directory; `--keep` retains every run.

The interpreter's program is the emission's link, which `ir_source_prepare`
keeps when asked: the static data published into it as the driver publishes it
(`check_data_publish`), its atoms' bytes copied out of the arenas the file's end
releases, and each symbol's size as the program declares it. The child
materializes it with `interpreter_link_materialize` from a copy whose code atoms
have no fixups: the materializer refuses those, and the interpreter runs the IR,
which carries its own references, never the code. Each function's symbol is the
interpreter's token for it (a copy of its `IrFunction`), and each label's is its
`labels` entry, in the child's arena beside the data, so that a jump table's
32-bit entries (`PC32`, as clang's `.long .LBB - .LJTI` are) reach their labels.
What the program refers to and doesn't define comes from the process, by name:
its functions as natives (`dlsym`, and `libgcc_s.so.1` for libgcc's), the
allocators, copies and fill with their bookkeeping, and its objects (`stdout`)
by the sizes it declares them with. `main` gets `argc` 1, `argv` and an empty
environment. Memory checking is on and undefined-byte checking off: a native
writes the memory a program gives it (`strcpy`'s) without telling the
interpreter which bytes it defined. A run is bounded by 2^32 steps (`--steps`)
and an alarm (`--timeout`, 600 s by default).

Three of the IR front's rules came out of these runs (the [tools
guide](ir-tools.md)'s "Execution contract"): it verifies each function once per
execution rather than at every call, which had been 45% of a recursive
program's time; it takes a flagged native's `i64` operands and result, which
the typed stream's pointers lower to, as pointers, as the typed front takes its
`U64`s; and since the lowered IR takes every argument of a variadic call as
named, such a call's named arguments are its callee's parameters, the rest
ignored by a callee without a variadic frame (a variadic function that never
calls `va_start`, and a function C calls without a prototype).

### Random programs

`--generate N` writes N programs (`ssa_source_generate.c`, from `--seed`'s on)
into the work directory, each built by clang -O0 (through the oracle cache) and
run for the output and status its forms must give. A program's functions mix
structured and unstructured control flow: if and else, while, do and for loops
nested to depth 7, break and continue at any depth (conditional or not),
switches whose cases fall through and whose case labels may sit inside their
bodies' loops and blocks (as Duff's device's do), gotos forward and backward to
labels anywhere in the function, into loops and switches and out of them, and
early returns. Their locals: integers of every width and signedness, `_Bool`,
`unsigned __int128`, float, double, long double and a pointer, which promotion
takes, and an address-taken integer, a volatile one, an array and a struct,
which stay in memory. Everything is defined behavior (the generator's header
has its rules), and every cycle of a function's control flow decrements its
fuel, which ends the function at zero.

### Results

Every function of the corpora passes both promotions' checks, and every program
gives its expected output in all three forms. On the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30)), c-testsuite's subset, `sqlite3.c`, musl and 48 generated programs
gave these counts again. Locally (a Zen 4 laptop, Clang 23.1.4), with the 9700X
job's options for `sqlite3.c` and musl:

| Input | Functions | Lowered instructions | Promoted | Phis (vector / scalar) | Programs run |
| --- | ---: | ---: | ---: | ---: | ---: |
| c-testsuite's subset | 430 | 22,142 | 18,719 | 140 / 140 | 220 of 220 |
| frozen `sqlite3.c` | 2,602 | 598,425 | 493,552 | 6,097 / 6,097 | none |
| musl 1.2.5 (1,050 units; 305 left out by name) | 1,918 | 247,429 | 203,490 | 2,777 / 2,777 | none |
| Lua 5.4.8 but `lua.c` and `luac.c` | 1,059 | 133,643 | 102,685 | 1,126 / 1,126 | none |
| 1,000 generated programs (seed 7) | 6,000 | 27,737,466 | 10,269,192 | 835,918 / 835,918 | 1,000 of 1,000 |

The paths' phis are the same on every function: both constructors' results go
through the same trivial-phi and redundant-cycle cleanup, which leaves minimal
SSA. The runs' steps show what promotion saves: c-testsuite's programs took
1,358,335,785 interpreter steps with every local in memory and 1,093,122,877 in
either SSA form (8 queens, `00040.c`, 1.35 billion and 1.09 billion of them),
the generated programs 519,202,382 and 536,015,938: their phis, at loop headers
and labels reached by many gotos, cost more steps than their loads and stores
did.

Each check was tried on a build with a deliberate defect, which the command
must reject (16 generated programs, and c-testsuite's subset without 8 queens):
phi operands written in reverse order by the adapter (`ssa_ir_fill`), which the
IR's dominance checks refused in 94 of the 96 generated functions and 46 of
c-testsuite's; a second write to a local in one block ignored by the scalar
constructor, whose later reads in the block then see the first write, which
dominates them, refused by `ssa_verify` ("read is not the current local value")
in 39 and 35 functions, and, with the verifier disabled as well, caught by the
runs, whose scalar form's output or status differed from the memory form's in
all 16 programs and in 27 of c-testsuite's; and an unsigned `>>` lowered as an
arithmetic shift (`ir_lower.c`), on which the three forms agree but 4 of the 16
programs print other than clang's build and 1 of c-testsuite's than its
`.expected`.

## Benchmark

### `sqlite3.c`'s functions

#134's measurement: `benchmark-ir-source` on the frozen `sqlite3.c`, with the 9700X
job's options, pinned to CPU 2 of the 9700X under the lock, against main
`caef4ea2` in six rounds that alternate which runs first ([the record](https://github.com/buster14a/buster-performance/tree/6a910d75bee362782f2edc77ab9066b48fd92b6c/docs/measurements/ssa-source-134-2026-10-30)).
These are medians of the rounds' 21-pass medians, over the 2,602 functions'
396,928 typed entries and 598,425 lowered IR instructions:

| Path | Cycles per IR instruction | ns per IR instruction | ns per typed entry | Instructions per typed entry | Phis |
| --- | ---: | ---: | ---: | ---: | ---: |
| `promote` (`ssa_ir_promote`) | 519.50 | 93.74 | 141.32 | 2,167.97 | 6,097 |
| `promote-scalar` | 518.50 | 93.55 | 141.04 | 2,236.80 | 6,097 |

A promotion is all of `ssa_ir_promote`: the input's verification, CFG and
classification, the shared dominance analysis, construction and cleanup, the
IR's rebuild and both IR verifiers on the output. The two paths make the same
phis. Against main, the record reports `promote` +0.16% and `promote-scalar`
−0.32%, with 0.003% more reported instructions and the promotion's added tests
of its verifier argument. The small timing changes remain unattributed:
unchanged-control timings do not establish a placement/noise bound for changed
promotion code. The [additive interpretation note](https://github.com/buster14a/buster-performance/blob/224d16c36bc688bc40ddc2a6b30fe7f4f537ce95/docs/measurement-review-notes/pr149-timing-interpretation-2026-10-30.md)
retains the original packet and records its counter-coverage and local-snapshot
limitations. After 200 ms of scalar work before each pass, `promote` took
141.16 ns per entry against main's 141.17.

### Synthetic CFGs

```sh
./build/compiler benchmark-ssa
./build/compiler benchmark-ssa --only scalar
./build/compiler benchmark-ssa --only vector
./build/compiler benchmark-ssa --only vector --after-scalar 200
```

The benchmark verifies both forms before timing, then reports 21 interleaved
sample medians and ranges: nanoseconds per input instruction, actual perf CPU
cycles when available, retained phis, operands and output bytes. Construction,
analysis and arena allocation/reset are timed; input generation, arena reservation
and verification are outside timing. Unavailable or multiplexed cycle counters
are reported as unavailable. `--only` isolates a path, and `--after-scalar MS`
conditions each vector sample with scalar work.

The inputs are explicitly synthetic: small diamonds, natural loops, an
irreducible CFG and a large random stress graph. The last can produce many more
phis than original instructions, so compare its printed output size alongside
input-normalized time. These are not SQLite functions or whole-compiler results.
The shared-IR adapter and its full-IR verification are outside this kernel
benchmark.

Run target measurements through the shared 9700X lock, pinned to one permitted
CPU, with the ownership variables from AGENTS.md. CI includes `test-ssa`,
`test-ssa-ir` and the synthetic benchmark in its existing 9700X job.
The workflow runs the normal interleaved benchmark; on the daily and
dispatched runs and on pull requests that change `ssa.c`, `ssa_program.c`,
`ssa_benchmark.c` or `dominators.c`, it then runs three separate processes per
mode: scalar alone, vector alone, and vector samples immediately after 200 ms
of scalar work. This follows the coding style's requirement to
measure both sustained execution and the transition from lighter work. Interpret
the isolated and conditioned modes separately from the interleaved medians.

The construction core passed the complete protected
[9700X workflow](https://github.com/buster14a/buster/actions/runs/39933567015)
on October 23, 2026, with Clang 23.1.1. The tested merge was
`ec87ae397768b599a3a8e4d3c40caeaafaf0640f` (SSA head `655ee3b`, main base
`635434f`). Its default SSA suite passed 2,847 checks. The kernel benchmark
reported these medians, with the same phi counts for both constructors:

| Synthetic input | Scalar ns/instruction | Vector ns/instruction | Scalar cycles/instruction | Vector cycles/instruction | Phis |
| --- | ---: | ---: | ---: | ---: | ---: |
| 32-block diamond | 16.628 | 12.325 | 91.912 | 68.214 | 31 |
| 256-block loop | 65.456 | 37.945 | 362.873 | 210.192 | 580 |
| 513-block irreducible | 52.367 | 41.117 | 290.171 | 227.879 | 3,420 |
| 1,025-block random stress | 860.625 | 736.558 | 4,768.951 | 4,081.660 | 197,087 |

The [retained measurement record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/ssa-2026-10-23/9700x-core.txt)
contains source hashes, build/run conditions, output sizes, sample ranges and
verbatim log excerpts. This historical target run covers four synthetic inputs
before the IR adapter and shared-dominator integration. Its timings apply to
the recorded core revision, not the revised analysis path. It establishes
neither SQLite throughput nor a general scalar/vector crossover.

The later published IR-adapter head `9c6ccfb` also passed the complete protected
[9700X workflow](https://github.com/buster14a/buster/actions/runs/39936586492).
That run tested merge `9f138c89690e9dbbd2ca2f039805d2ca12cab1a2` against IR base
`7a6ed05745a6b522209005d2423103caedb6c99a`: 2,847 core checks and 2,038 adapter
checks passed. The default benchmark and three independent processes each for
scalar, vector and vector after 200 ms of scalar work completed successfully.
The [adapter target record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/ssa-2026-10-23/9700x-ir-adapter.txt)
retains the complete SSA benchmark step, source hashes and qualification
excerpts. It precedes shared-dominator adoption; the revised analysis path
requires its own protected target result.

## What remains for #134

The comparison of Tier 1's compiled code with the three interpreted forms, once
Tier 1 compiles general CFGs and phis (#138, which depends on this issue): it
refuses them today, so no promoted function with control flow reaches native
code yet.

## Algorithm references

These papers supplied algorithms and terminology; no implementation or external
fixture source was copied.

- Braun, Buchwald, Hack, Leißa, Mallon and Zwinkau, *Simple and Efficient
  Construction of Static Single Assignment Form*, CC 2013,
  [DOI 10.1007/978-3-642-37051-9_6](https://doi.org/10.1007/978-3-642-37051-9_6),
  especially the completed-CFG construction and section 3.2's redundant-phi
  analysis ([paper](https://c9x.me/compile/bib/braun13cc.pdf)).
- Cytron, Ferrante, Rosen, Wegman and Zadeck, *Efficiently Computing Static
  Single Assignment Form and the Control Dependence Graph*, TOPLAS 1991,
  [DOI 10.1145/115372.115320](https://doi.org/10.1145/115372.115320).
- Cooper, Harvey and Kennedy, *A Simple, Fast Dominance Algorithm*, Rice
  University technical report, 2001: reverse-postorder immediate dominators.
