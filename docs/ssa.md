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

The construction core and IR adapter have local semantic tests. #134's complete
source-corpus, general interpreter and Tier 1 acceptance still requires the
integration work at the end of this guide.

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
This is a storage policy, **not a measured performance crossover**.

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
and both promoted IR forms, including return bits and global memory. Its
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

## Benchmark

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
The workflow runs the normal interleaved benchmark; on pushes to main,
dispatched runs and pull requests that change `ssa.c`, `ssa_program.c`,
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

The [retained measurement record](measurements/ssa-2026-10-23/9700x-core.txt)
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
The [adapter target record](measurements/ssa-2026-10-23/9700x-ir-adapter.txt)
retains the complete SSA benchmark step, source hashes and qualification
excerpts. It precedes shared-dominator adoption; the revised analysis path
requires its own protected target result.

## Integration still required for #134

- Connect #133's complete typed-stream lowering, including volatility, aggregate
  and address-taking metadata, so actual source functions reach this pass.
- Compare full IR execution through #176 and Tier 1 generated code through #138.
- Run the source corpora and random C control flow, then measure cycles per IR
  instruction and phi counts on SQLite's functions under the locked 9700X.

The local-event and typed-IR interpreters check their supported inputs. They do
not provide the complete source-program and downstream-code acceptance above.
Keep #134 open until those requirements are demonstrated.

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
