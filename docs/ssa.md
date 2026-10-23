# Local SSA construction

`ssa.c` implements local promotion for [#134](https://github.com/buster14a/buster/issues/134).
It takes a completed CFG and a view of the local accesses in its instruction
columns. It returns read replacements and phis without prescribing the rest of
the IR. `ssa_build` is the vector path; `ssa_build_scalar` is its independent
predecessor-reading twin. Both are included in the existing compiler executable.

This implements the SSA construction kernel. It does **not** yet complete
#134's source-corpus, interpreter or Tier 1 acceptance: the integration work at
the end of this guide remains necessary.

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
2. Build reachable predecessors and reverse postorder with explicit stacks.
   Dominator sets use aligned 512-bit intersections and change masks. Extract
   immediate dominators, then build dominance-frontier bitsets.
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

The dense dominator/frontier matrix has a 16 MiB per-matrix budget. Above it,
`ssa_build` uses the scalar constructor and scalar immediate dominators. This
is a bound on quadratic storage, **not a measured performance crossover**.
The shared dominator analysis from #174 should replace this private setup when
its interface is available. Current-definition tables are dense in
`blocks × promoted locals`; the caller's arena bounds that remaining storage.

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

## Verification and tests

`ssa_verify` validates promotion, reachability, exact predecessor multiplicity,
types, dominance and canonical IDs, and rejects trivial phis. Phi operands are
uses at their respective predecessor exits. It then solves independent symbolic
reaching-definition equations and replays local accesses. This catches wrong
same-type, dominating substitutions which a structural verifier alone accepts.

```sh
./build/compiler test-ssa
./build/compiler test-ssa --seed 7439 --count 150
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

Local qualification used Clang 19 on a cloud AVX-512 AMD EPYC 9V74. The final
optimized and ASan/UBSan suites each passed 3,447 checks with seed 7439 and 150
random programs. Clang compiled and instrumented the sanitizer build; GCC's
installed sanitizer runtimes supplied the link because Clang's archives were
absent. Leak checking was disabled in the sandbox; address and undefined-behavior
checks remained enabled. These checks are not target-performance acceptance.

## Benchmark

```sh
./build/compiler benchmark-ssa
./build/compiler benchmark-ssa --only scalar
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

Run target measurements through the shared 9700X lock, pinned to one permitted
CPU, with the ownership variables from AGENTS.md. CI includes `test-ssa` and
the synthetic benchmark in its existing 9700X job. No Zen 5 performance result
or vector/scalar crossover is claimed by this change.

## Integration still required for #134

- Map #131's IR local-slot loads/stores and address escapes into the input view;
  rebuild the general IR with replacements and `(predecessor, value)` phi pairs,
  and run its scalar/vector verifier after promotion. Stack-slot size/alignment
  and actual frame offsets remain the IR/backend's responsibility.
- Connect #133's complete typed-stream lowering, including volatility, aggregate
  and address-taking metadata, so actual source functions reach this pass.
- Use #174's shared dominance/frontier analysis instead of private preparation.
- Compare full IR execution through #176 and Tier 1 generated code through #138.
- Run the source corpora and random C control flow, then measure cycles per IR
  instruction and phi counts on SQLite's functions under the locked 9700X.

The local-event interpreter proves the promotion contract on its inputs; it is
not a replacement for these whole-IR and source-program checks. Keep #134 open
until those requirements are demonstrated.

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
