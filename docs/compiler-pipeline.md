# Dense SSA compiler pipeline contract

This document is the normative architecture and migration contract for
[issue #31](https://github.com/buster14a/buster/issues/31). It describes the
pipeline the compiler is converging on, the invariants that are already
binding, the temporary exceptions that remain, and the order and evidence
required to remove those exceptions.

The target is deliberately small:

```text
C source
  -> C frontend
  -> dense, certified block-argument SSA canonical IR
       |-> Wasm64 / eBPF / LLVM-bitcode and other canonical consumers
       `-> target selection and legalization
           -> legal target-specific SSA machine IR
           -> scheduling and allocation side data
           -> streaming encoding
           -> object writer and linker
```

The migration changes ownership and semantic truth. It does not replace the
proven row formats, add a second permanent canonical IR, or turn FAST mode into
a general optimizer.

## Fixed representation contracts

These are architecture constraints rather than implementation suggestions.

- `IrInstruction` remains the authoritative canonical instruction row. On a
  64-bit build its compile-time size check remains 64 bytes. New analyses use
  compact side arrays instead of widening the row.
- `MachineInstruction` remains the 24-byte target instruction row. Static
  opcode, operand-role, register-constraint, memory, scheduling and emission
  facts belong in `MachineOpcodeInfo` or generated target metadata, not in
  every instruction.
- Canonical values, blocks, machine references, virtual registers, stack
  slots, symbols and side-table entries are integer IDs. A published function
  is traversable without a pointer graph.
- A mutable builder may use chunked append storage. Publication materializes
  each authoritative stream once into flat function-owned storage. The builder
  is construction state, never a third long-lived IR.
- Block parameters and edge arguments are the join representation at both IR
  levels. There is no PHI instruction. Argument `i` on an incoming edge maps
  to parameter `i` of the destination block.
- Edge copies remain parallel through allocation. The allocator or copy
  resolver, not instruction selection, resolves cycles and target two-address
  constraints.
- Liveness segments, use lists, schedules, allocation locations, edits,
  spill/reload plans, address facts and ABI decompositions are side data with
  explicit ownership and lifetime. They do not mutate the semantic row into a
  phase-specific object.
- IDs are stable after publication. A compaction or rewrite that changes them
  emits one explicit old-to-new map and updates every dependent side table in
  the same operation.

## Stage ownership

### 1. C frontend and canonical construction

The C frontend owns language semantics, diagnostics, source provenance and the
initial canonical control-flow graph. It does not own target registers,
calling-convention decomposition, encodings or native-only legalization.

Ordinary canonical values are single-definition. Eligible local state is
represented by canonical SSA values and block parameters. Storage remains
explicit for address-taken objects, volatile or atomic objects, variable-length
objects, identity-observable aggregates, inline-assembly memory operands and
other cases whose C semantics require memory.

A frontend failure publishes no apparently valid function. A successful
frontend result retains every source range and symbol relation required by
later diagnostics, debug information, relocations, label addresses and
computed goto.

### 2. Canonical publication and certification

Publication is the boundary between mutable construction and shared compiler
IR. It validates once and publishes dense immutable slices for:

- instruction rows and pooled variable operands;
- values and their defining instructions or block parameters;
- blocks and their contiguous instruction ranges;
- predecessor and successor edges;
- ordered block parameters and edge arguments;
- source/debug provenance and relocation ownership.

Certification means those structural and semantic invariants were checked by
a trusted producer. It is not permission for a consumer to reinterpret the
IR, maintain a divergent private CFG, or skip validation after mutating the
function.

### 3. Compact FAST canonical pipeline

The shared FAST pipeline is bounded to transformations that remove obvious
work without expensive global analysis:

```text
finalize CFG
promote eligible locals or accept frontend-built SSA
fold constants, copies, identities and redundant casts
normalize address expressions
remove unused pure instructions
remove trivial block parameters
certify
```

The default implementation is linear scans, forward rewrite maps, use counts
and compact scratch arrays. A CSR-style use index is built only for a selected
pass that repays its construction. Each pass is independently measurable and
independently switchable for differential testing. Improving an IR proxy while making total
compile time worse is not a FAST improvement.

### 4. Selection and legalization

Selection consumes certified canonical IR and produces legal machine SSA for
one target. Target selectors may own ABI sequence construction, exact
instruction choice and complex target lowering, but shared canonical facts
have one producer and are not reconstructed independently by every target.

Every supported canonical operation has exactly one authoritative selection
owner. Declarative and handwritten selection may coexist only across an
explicitly disjoint boundary; a pattern miss and the path that handles it are
observable. Exact-target intrinsics either select their exact semantics or
produce a deliberate diagnostic/fallback reason. They are never silently
scalarized as if they were portable vector operations.

### 5. Machine SSA, scheduling and allocation

Every ordinary virtual register has exactly one definition. A block parameter
is defined at block entry. Every use is dominated by its definition, including
uses carried as edge-copy sources. A target two-address instruction is still
represented as a machine-SSA definition with a tied source constraint; it is
not represented by mutating the source virtual register.

Register classes model every independently allocatable target bank. At minimum
this distinguishes general, vector, predicate/mask and flags resources where a
target exposes them. Reserved encodings such as x86 `k0` are target semantics,
not allocatable values accidentally withheld by an ad-hoc fixed-register path.

The scheduler consumes explicit dependencies, opcode resource metadata and
conservative memory facts. Unknown provenance, calls, inline assembly,
volatile operations and atomics remain ordered. A relaxed memory edge requires
a proven alias-class distinction; absence of proof means unknown, not disjoint.

FAST and QUALITY use the same machine semantics. FAST prioritizes bounded
linear work and compact state. QUALITY may construct more side data and run a
pressure scheduler, but it remains deterministic and cannot rely on facts that
the machine verifier does not establish.

### 6. Encoding

Encoding consumes legal allocated machine IR plus sorted edits and relocation
facts. It does not perform semantic legalization or repair malformed machine
IR. A failed exact-form lookup, placement or encoding is reported at that
stage and is observable while whole-function fallback still exists.

The object writer owns format layout and relocation serialization. It does not
rederive language, SSA or target-selection semantics from instruction bytes.

## Canonical consumers and the no-silent-regression rule

Canonical IR is a shared contract, not a native-backend input format. Any
change to canonical opcodes, types, CFG topology, block parameters, address
semantics, ABI queries or publication lifetime must account for every active
consumer:

| Change class | Required consumers and evidence |
| --- | --- |
| Canonical opcode/type semantics | Native selectors, Wasm64, eBPF and LLVM-bitcode lowering either implement the semantic class or reject it explicitly. |
| CFG, value IDs or block arguments | All canonical traversals use the published interface; validator and negative tests cover malformed topology and remapping. |
| ABI decomposition | Native call lowering and every non-native consumer request only their target's side-table result; mixed-target tests prove no cross-contamination. |
| Vector or predicate semantics | Cross-target tests distinguish legalizable operations, exact intrinsics and language-visible integer masks. |
| Address/provenance facts | x86-64 and AArch64 share the canonical fact; TLS, symbols, relocations, computed labels, volatile/atomic boundaries and overflow retain their exact behavior. |
| Purity or dead-code classification | The classification is shared and tested against calls, traps, volatile/atomic accesses, inline assembly and observable memory. |

A consumer that cannot support a new semantic class must fail deliberately. A
successful build with silently changed semantics is never an acceptable
fallback.

## Direct-native fallback contract

The direct canonical native emitter is a migration oracle, not a second target
architecture. Until it is retired, every attempt to use a non-`NONE` allocator
that falls back for a function remains measurable through
`CodegenStatistics`:

- `fallback_function_count` counts affected functions;
- `fallback_opcode_counts` records selection refusals by canonical opcode;
- `fallback_verify_count`, `fallback_placement_count` and
  `fallback_encode_count` identify later-stage failures;
- verbose compiler output publishes `CODEGEN_FALLBACK` and
  `CODEGEN_FALLBACK_STAGES` records beside target and allocator information.

A test intended to prove machine-path coverage asserts the fallback count and,
where relevant, its stage/reason. Merely running the generated program is not
coverage evidence because the direct emitter can produce the same answer.
Unexpected fallback is a regression even when output bytes still execute
correctly.

The direct path remains frozen except for correctness fixes needed to keep it a
trustworthy differential oracle. It can be removed from production only after
[issue #35](https://github.com/buster14a/buster/issues/35) establishes stable
reason codes and a curated zero-unapproved-fallback corpus, and
[issue #36](https://github.com/buster14a/buster/issues/36) records clean
differential results and removes fallback in reviewable stages.

## Current migration exceptions

The following are known exceptions to the destination contract. They are
bounded debt, not precedent for new code.

| Exception on the current path | Removal owner |
| --- | --- |
| Target-local promotion may redefine one machine virtual register and `definition_point` records only part of that history. SSA-only consumers must not infer a complete dominance contract from it. | [#32](https://github.com/buster14a/buster/issues/32) |
| Eligible C locals can still enter canonical IR as `LOCAL`/`LOAD`/`STORE`, with overlapping promotion in native selectors. | [#33](https://github.com/buster14a/buster/issues/33), then [#34](https://github.com/buster14a/buster/issues/34) |
| Native compilation may abandon machine selection, verification, placement or encoding and emit the whole function directly from canonical IR. | [#35](https://github.com/buster14a/buster/issues/35), then [#36](https://github.com/buster14a/buster/issues/36) |
| Canonical instruction rows are dense, but complete CFG topology is not yet published through one immutable dense interface. | [#38](https://github.com/buster14a/buster/issues/38) |
| Interned types retain more ABI-specific state than one active compilation needs. | [#39](https://github.com/buster14a/buster/issues/39) |
| The shared FAST canonical pass order and per-pass budgets are not yet an enforced pipeline contract. | [#40](https://github.com/buster14a/buster/issues/40) |
| Memory scheduling is deliberately conservative and cannot yet relax dependencies by proven alias class. | [#41](https://github.com/buster14a/buster/issues/41); first preserve complete memory classification in [#126](https://github.com/buster14a/buster/issues/126) / [PR #127](https://github.com/buster14a/buster/pull/127) |
| Declarative and handwritten instruction selection do not yet have a final, disjoint ownership boundary. | [#42](https://github.com/buster14a/buster/issues/42) |
| Portable vector operations, exact target intrinsics and internal predicates need an explicit semantic split. | [#43](https://github.com/buster14a/buster/issues/43) |
| Target selectors still repeat canonical address-expression discovery. | [#44](https://github.com/buster14a/buster/issues/44) |
| Some metadata and `MachineAddress` abstractions have no audited authoritative producer/consumer/lifetime. | [#45](https://github.com/buster14a/buster/issues/45) |
| x86 AVX-512 masks are not yet allocated as a complete independent `k1`-`k7` bank with `k0` semantics enforced. | [#37](https://github.com/buster14a/buster/issues/37), coordinated with [#43](https://github.com/buster14a/buster/issues/43) |

No new producer may expand one of these exceptions without naming the owning
issue and adding a regression test that keeps the boundary visible.

## Work order, ownership and evidence

The coordinating owner for #31 and every listed child is `@davidgmbb` until an
issue is explicitly reassigned. An assignee owns the design decision, the
correctness oracle, the measurement record and cleanup of superseded paths; it
does not imply that the assignee must author every commit.

Hard dependencies are deliberately few. “Coordinate” means interfaces must be
reviewed together, but neither issue is blocked unless a hard dependency is
listed.

| Issue | Hard dependency / coordination | Correctness gate | Required measurement |
| --- | --- | --- | --- |
| [#32 — single-definition MIR](https://github.com/buster14a/buster/issues/32) | Foundation | Negative verifier cases plus branches, loops, backedges, reassignment and values live across calls on x86-64/AArch64 | Selection/verification time, spills and generated code against the current path |
| [#33 — canonical local promotion](https://github.com/buster14a/buster/issues/33) | Foundation | Diamonds, loops, irreducible supported CFGs, escape, volatile/atomic, asm and uninitialized reads; promoted/unpromoted differential | Canonical row counts, frontend/middle time, peak memory and native/non-native output |
| [#34 — frontend-built pruned SSA](https://github.com/buster14a/buster/issues/34) | After #33 | Sealed/unsealed blocks, backedges, trivial parameters, fallback ownership and source/debug provenance; compare against #33 | Frontend-built path versus standalone promotion in total compile time and peak memory |
| [#35 — MIR coverage telemetry](https://github.com/buster14a/buster/issues/35) | After #32 | Stable reasons at selection/verify/place/encode; curated corpus rejects unapproved fallback on x86-64/AArch64 | Coverage by target, tier, reason and corpus plus total compile time |
| [#36 — retire direct native backend](https://github.com/buster14a/buster/issues/36) | After #32 and #35 | Clean MIR/direct differential corpus before each removal stage; production has no silent fallback | Compile time, peak memory, object/code size and runtime performance against the frozen oracle |
| [#37 — AVX-512 mask bank](https://github.com/buster14a/buster/issues/37) | Coordinate semantic boundary with #43 | Concurrent predicates, `k0`, spill/reload, calls, parallel copies and explicit integer-mask boundaries | `KMOV` count, register pressure/spills, compiler time and kernel runtime |
| [#38 — dense canonical CFG](https://github.com/buster14a/buster/issues/38) | Foundation; coordinate publication with #40/#44 | Block/edge/parameter integrity, stable remap, source/debug, labels/computed goto, relocation and fallthrough | Published bytes/function, finalization cost, peak memory and backend traversal time |
| [#39 — active-target ABI side tables](https://github.com/buster14a/buster/issues/39) | Foundation | Mixed target/ABI contexts plus scalar/aggregate call and return matrices for native and non-native consumers | Type-pool bytes, peak memory, ABI computation time and memoization hit rate |
| [#40 — compact FAST canonical pipeline](https://github.com/buster14a/buster/issues/40) | After #33; coordinate with #38 | Per-pass differential, shared purity tests, trivial parameters and certification | Per-pass and total time, peak memory and output quality with each pass independently disabled |
| [#41 — proven memory alias classes](https://github.com/buster14a/buster/issues/41) | After #32 and #33; preserve #126/#127 first | Calls, volatile/atomic, asm, overlap, unknown memory and disjoint spill/local slots | Dependency/schedule quality on memory-heavy kernels and FAST/QUALITY compile time |
| [#42 — authoritative instruction selection](https://github.com/buster14a/buster/issues/42) | Foundation; coordinate retained metadata with #45 | Generated coverage validation, observable misses and exactly one owner per bounded selection subset | Fact-building/selection time, generated-code delta and deleted maintenance surface |
| [#43 — vectors, exact intrinsics and predicates](https://github.com/buster14a/buster/issues/43) | Foundation; coordinate x86 mask lowering with #37 | Cross-target generic legalization, exact lowering/refusal and integer-mask/predicate conversions | Canonical/MIR row counts, conversion traffic, fallback/diagnostic counts, compile time and target instruction quality |
| [#44 — shared address facts](https://github.com/buster14a/buster/issues/44) | Foundation; coordinate publication with #38 and alias users with #41 | Globals, TLS, fields, scaled indices, offsets, relocations, labels, volatile/atomic and overflow | Fact-build cost versus eliminated selector chasing, selector time and folded-address quality |
| [#45 — metadata and `MachineAddress` audit](https://github.com/buster14a/buster/issues/45) | Foundation; coordinate selection ownership with #42 | Producer/consumer/lifetime/invalidation inventory and assertions against stale derived data | Retained metadata bytes, build cost, consumer savings and deleted branches/fields |

### Execution waves

Work can proceed in parallel inside a wave; a later wave begins for an issue
only after its own hard dependencies are complete.

1. **Semantic foundations and inventories:** #32, #33, #37, #38, #39, #42,
   #43, #44 and #45. Preserve conservative scheduling classification in #126 /
   PR #127 while #41 remains blocked.
2. **Shared consumers and observability:** #34, #35, #40 and #41 according to
   their hard dependencies.
3. **Retirement:** #36, after the machine verifier and fallback corpus prove the
   machine path is authoritative.

## Change gates

Every child PR records the base commit, corpus, target, allocator mode, command,
result and before/after measurement. A count without a date and revision is not
an architectural fact.

The minimum local gates are selected by change class:

- `ide test` for focused unit, verifier, selector, allocator, encoder and driver
  coverage;
- `./build.sh test_mode_matrix --config Release` for allocator/target/object
  format width;
- `./build.sh test_self_host --config Release` for deep native fixed-point
  coverage;
- `test_all` plus the relevant Wasm64, eBPF and LLVM-bitcode assertions when a
  shared canonical contract changes;
- differential execution or byte/relocation inspection against the direct
  emitter and external compilers where they are valid oracles.

Tests that cannot execute a cross-target image still inspect the strongest
available structural property. A missing execution avenue is reported, not
silently counted as a pass.

## Completion rule

Issue #31 is the architecture and coordination parent. It is complete when:

1. this contract is in the repository and contradicting code changes update it;
2. #32 through #45 have explicit owners;
3. the dependency order and coordination edges above are the queue of record;
4. every child has a correctness oracle and compile-time/code-quality
   measurement contract;
5. fallback observability and non-native consumer duties remain mandatory
   through the migration.

Closing #31 does **not** claim that #32 through #45 are implemented. Each child
remains the independently reviewable implementation and closure unit for its
exception. The destination architecture is reached when those child issues,
not the coordination parent, are closed with their recorded evidence.
