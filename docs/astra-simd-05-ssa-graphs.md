# SSA construction and graph maintenance: eliminate work before widening it

Research record: **astra-simd-05**, 2026-09-13.

**Disposition:** retain the current sparse direct-SSA constructor and canonical CFG. First experiment with stable value-compaction pass fusion and reuse of the expired liveness queue as an ordered alias frontier. Independently compare a genuine 16-way SIMD root-identity classifier using the existing vocabulary. Do not combine the alias-frontier and SIMD savings as though they were independent. Defer maintained reverse-use graphs, speculative early sealing, and vectorized replacement propagation until their complete costs beat these scalar baselines.

**Evidence status:** source analysis and review of previously published measurements. No compiler, prototype, test, census, self-host, benchmark, or PMU experiment was executed for this report, locally or remotely. Algorithms and crossover formulas below are proposals and source-level arguments, not measured speedups or completed acceptance.

## 1. Revision, scope, and ownership

Analyzed main: [`5865fa7add27908656aedf28a3c0e6a3de47dc04`](https://github.com/buster14a/buster/commit/5865fa7add27908656aedf28a3c0e6a3de47dc04), tree `950eae4171f20ece45f1092fbc311c950665ed5b`.

Before publication, main advanced to [`5217113aa33a74cd78524c29bd8addfc0e219ef0`](https://github.com/buster14a/buster/commit/5217113aa33a74cd78524c29bd8addfc0e219ef0), tree `a1a85005b5e6ebc48b5af6cfb66a9b31cc73ec8d`. The API comparison contains only the two removed Vulkan installer scripts and `.github/workflows/ci.yml`. The compiler sources and guides analyzed here are unchanged. The documentation branch starts at this newer revision; source permalinks deliberately retain the analyzed revision.

The large `c_gen.c` file was read through the connected GitHub Git-blob endpoint after the ordinary contents route returned no useful content. Its analyzed blob is `fc465d510bed497e91402fdcb4be3ec46310601c`. This is source inspection, not execution of the repository. Repository identity and reported write permissions were read through the authorized connection; publication success requires separate read-back verification.

### Existing work is not an unclaimed opportunity

| Existing work | Disposition for this report |
| --- | --- |
| [#34](https://github.com/buster14a/buster/issues/34), direct pruned frontend SSA | Existing baseline. Do not build another constructor. |
| [#447](https://github.com/buster14a/buster/issues/447), [#497](https://github.com/buster14a/buster/pull/497), [#548](https://github.com/buster14a/buster/pull/548) | #447 was closed/completed when read. The parameter-bearing-block optimization is present. Published diagnostic work is reusable evidence, not proof of a further speedup. Add a research comment without reopening or claiming its implementation branch. |
| [#38](https://github.com/buster14a/buster/issues/38), [#40](https://github.com/buster14a/buster/issues/40) | Published canonical CFG and bounded FAST work are existing baseline, not reasons to create duplicate graph infrastructure. |
| [#534](https://github.com/buster14a/buster/issues/534), [#546](https://github.com/buster14a/buster/issues/546) | Existing representation/optional-analysis discussions. This report supplies a bounded construction experiment, not a new semantic IR or architectural approval. |
| [#52](https://github.com/buster14a/buster/issues/52), [#525](https://github.com/buster14a/buster/issues/525), #447 | Preserve capacity, initialization, and slot-growth ownership. Do not turn a compaction experiment into their allocation refactor. |
| [#564](https://github.com/buster14a/buster/issues/564), open draft [#576](https://github.com/buster14a/buster/pull/576) | Active canonical-validation attribution. Read head `b564aae8ededa5f7a7866987b5dd30ca0f9cff05`; do not duplicate its instrumentation or change validation semantics. |
| [#574](https://github.com/buster14a/buster/issues/574) | Open report of word-SIMD fallback failures on AVX-512 F/BW hosts without VBMI/VBMI2. Treat exact feature-tier correctness as an explicit dependency, not as a repair owned by this document. Its reported diagnosis was not independently reproduced here. |
| [#36](https://github.com/buster14a/buster/issues/36), [#522](https://github.com/buster14a/buster/pull/522), [#579](https://github.com/buster14a/buster/pull/579) | Native retirement, dispatch, and wide-vector ABI work remain independent. Do not change modes or add a retirement blocker. |
| [#531](https://github.com/buster14a/buster/issues/531) | Function-body parallelism has separate ownership. Function-local-looking SSA does not establish a thread-safe lowering boundary. |

An open-PR inventory and targeted SSA searches found no open dedicated SSA-finalization implementation PR at the publication check. This is not evidence that every branch or concurrent session is unowned. Before implementation, recheck #447 comments and the existing `codex/issue447-ssa-scratch` work rather than taking over or replacing it.

### Source and instruction authority

The reviewed instruction sources are pinned [AGENTS.md](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/AGENTS.md), [frontend foundations](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/frontend/foundations.md), [machine](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/machine.md), [SIMD](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/simd.md), [parallelism](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/parallelism.md), and [benchmarking](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/benchmarking.md). This report does not replace those instructions.

## 2. What the evidence actually establishes

### 2.1 Historical native sample: where to investigate, not current timing

[#447's original capture](https://github.com/buster14a/buster/issues/447) describes one 800.395 ms Superluminal run on the Ryzen 7 9700X at `44a90fcbff44988624878aff9db3346a8bc6550b`, with a Clang 22.1.8 Release producer. It attributes 75.951 ms inclusive / 48.507 ms exclusive to `c_ir_ssa_finish`; `c_ir_ssa_current` contributes 25.539 ms inclusive beneath finish. Lowering is 334.064 ms of the 708.685 ms compiler lane.

That workload records 4,306 SSA functions, 37,269 eligible locals, 385,144 provisional parameters, and 357,747 removed parameters. These are historical populations before subsequent changes, not a current-main distribution. Removed parameters are not proof that all provisional construction can be avoided. Inclusive call-chain samples must not be added to their parent time.

### 2.2 Later diagnostic census: real work counts, incomplete distributions

The [#447 discussion](https://github.com/buster14a/buster/issues/447) records #548's diagnostic run [34728389279](https://github.com/buster14a/buster/actions/runs/34728389279), artifact `10308461549`, `direct-ssa-census-34728389279-1`, SHA-256 `d3b87b4adf5f65a8d8e54f83b14c5608158c1331a3ffd736b55ae2da9a494d65`. The report identifies hosted EPYC 9V74 and Clang 21.1.8, not the 9700X. This session reviewed the published record, not a new independent artifact download/replay.

| Published direct-SSA counter | Count |
| --- | ---: |
| Function starts | 4,763 |
| Slot growth / finish-time growth | 8,194 / 1,604 |
| Slot probes / finish-time probes | 3,149,719 / 2,827,243 |
| Rehash probes | 1,690,189 |
| Single-predecessor forward steps | 569,631 |
| Pending predecessor visits | 698,398 |
| Simplification passes | 9,408 |
| Simplification block / empty-block visits | 400,061 / 51,814 |
| Parameter / incoming visits | 887,310 / 1,762,671 |
| Slot clear bytes | 76,226,304 |
| Value clear / requested scratch bytes | 35,285,333 / 55,101,337 |
| Remap value / instruction rows | 5,055,222 / 1,298,229 |
| Remap operand slots | 1,256,613 |

These are cumulative calling-thread work/requested-allocation counters. They are neither wall time nor resident bytes. The current source records `SSA_REMAP_VALUE_ROWS` as `3 * count`; applying that definition to this historical census implies an aggregate 1,685,074 old value slots. That division is a derived work denominator, not a freshly measured value population.

The record reports passing frontend assertions but one failing assertion in the complete diagnostic suite, then owned by #550. The ordinary hosted comparison had 18 of 24 cases inconclusive and did not compare different SSA algorithms. It is not acceptable to describe that as an all-green optimization result.

### 2.3 Newest checked-in audit: bytes and Callgrind are separate currencies

The newest timestamped audit in the analyzed tree is [2026-09-12T224432Z.md](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/performance-audits/2026-09-12T224432Z.md). Its frozen workload is `5dab18af0208cfa7fa65673b6884d5c253fda85b`; its host is a four-vCPU Emerald Rapids KVM guest without PMU evidence. Valgrind 3.22 required an x86-64-v3 producer rather than the native EVEX path.

It reports 70,834,450 bytes of slot-growth filling over 8,172 calls and 27,618,725 bytes across SSA-finish scratch fills. The [follow-up slot-map comment](https://github.com/buster14a/buster/issues/447#issuecomment-5650137284) correctly motivates examining growth, but body-token/block/local counts are not a proven tight bound on demanded sparse keys. Allocating a blocks-times-locals table to avoid growth would violate the intended sparse design.

The audit's canonical-validation share, 8.6% of its Callgrind Ir total, is not current-main wall time, native retired instructions, or a demonstrated SIMD opportunity. Its already-landed token/initializer changes are not benefits available to this proposal. Replacing scratch clears with a zero-aware allocator does not automatically remove writes to a dirty reused scratch arena.

### 2.4 Missing evidence determines the current disposition

No inspected record supplies the necessary current per-function joint distribution of value count, alias fraction, replacement-chain depth, parameter fan-in, equality early-exit position, or affected-frontier density. Therefore this report supplies **crossover conditions rather than fabricated numerical dispatch thresholds**. The direct source finding is removable enumeration; whether it produces a substantial whole-compiler gain remains an experiment.

## 3. Current algorithm and data ownership

All following source anchors refer to the analyzed `c_gen.c`, not historical line numbers from an issue.

### 3.1 Variable lookup and provisional state

[Construction structures and lookup](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_gen.c#L4670-L5040) use `CIrDirectSsa`, `CIrSsaSlot`, `CIrSsaLocal`, `CIrSsaRead`, `CIrSsaEvent`, and `CIrSsaParameter`.

`CIrSsaSlot` has `u32 block_plus_one`, `u32 local`, and `IrValueId value`: a 12-byte sparse key/value row on the reviewed layout. Empty means `block_plus_one == 0`. `c_ir_ssa_slot` and `c_ir_ssa_grow_slots` implement growing open addressing. Existing-key queries already avoid growth. The source has not replaced this table with a dense map or removed its rehashing cost.

`c_ir_ssa_current` memoizes demanded `(block, local)` values. Single-predecessor forwarding is iterative, using `read_blocks`, `read_stamps`, and a stamp; join/cycle cases create pending parameters rather than recurse on the C stack. Growth can invalidate slot pointers, so retained identities must be IDs rather than pointers into the old table. Existing single-entry-definition shortcuts must remain revocable after later writes.

`c_ir_ssa_read` records provisional read aliases and source-sensitive events. `c_ir_ssa_read_place` can recover a place and, under its existing tail conditions, retract a no-longer-consumed read. Its sorted-ID lookup and instruction/block/event checks are not redundant graph maintenance: they protect C lvalue, source, and escape behavior.

### 3.2 Memory recovery precedes destructive simplification

[Initialization and recovery](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_gen.c#L5100-L5385) classify real place uses and conservative barriers. Elided exact local loads/stores are journaled; a remaining place use can imply escape, subobject access, or a memory-only operation. Assembly, indirect labels, dynamic stack operations, and relevant call barriers retain their current treatment.

`c_ir_ssa_classify_initialization` uses store right-hand sides and actual instruction operands as roots. The initialized-entry shortcut already skips work for proven owners. A reachable pending value without a reaching definition causes whole-owner memory recovery; it does not manufacture zero or an arbitrary undefined SSA value.

`c_ir_ssa_memory_roots` makes recovered loads independent definitions before later replacement decisions. `c_ir_ssa_restore_memory` replays rejected owners at their original event anchors. New instruction rows append to the canonical array while links express execution order; original source rows, instruction IDs, and extras remain associated correctly. The journal still has an active consumer until this decision finishes.

### 3.3 Sealing, predecessors, and incomplete parameters

[`c_ir_ssa_finish`](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_gen.c#L5370-L5585) obtains the complete CFG after the body and backedges are known. It counts unique predecessor incidences, prefix-sums offsets, and fills contiguous predecessor ranges in stable source-block order. Reachability is a separate fact with a separate traversal; identical-looking edge loops are not automatically duplicate proofs.

The pending queue can grow while predecessor values are queried. Forwarded single-predecessor and entry-definition cases avoid some incoming construction. Real joins retain `IrBlockParameter` and linked `IrIncoming` rows. These links are temporary mutable construction data; they are not a reason to add a second authoritative graph.

### 3.4 Trivial-phi elimination and liveness

[Final simplification and remapping](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_gen.c#L5550-L5890) already uses #497's stable parameter-bearing-block list. It preserves ascending block order and within-block parameter order, and drops empty blocks from later sweeps. It does **not** have a reverse-use notification graph.

For each retained parameter, incoming values are resolved through `c_ir_ssa_root`; its own value is ignored. One distinct non-self root makes the parameter trivial; two distinct roots end the test early. A self-only cycle is not arbitrarily replaced. A replacement can make an earlier parameter trivial on a subsequent sweep. Changing visitation order can change representative choices and later canonical IDs even when programs remain semantically equivalent.

`c_ir_ssa_root` already performs path compression. Introducing path compression is not a new proposal. Nor does the presence of compression alone justify applying a union-by-rank inverse-Ackermann bound to this order-constrained rewriting algorithm.

After trivial elimination, a value-indexed parameter table plus `work[count]` computes parameter liveness backward from actual instruction operands. Enqueue-once membership uses the existing `value_map` markers. This also prunes unused cyclic parameter groups. It is already a sparse worklist; a whole-function reverse def-use graph would add maintenance without replacing this consumer's needs.

Every required predecessor, including edges into parameter-free blocks, is then attached and blocks are sealed. The `work` queue has no remaining consumer before value remapping.

### 3.5 The concrete repeated work

At this point `count = N` is fixed and the selected replacement roots no longer change. The current function performs:

1. An ascending scan of all N old values assigning dense IDs to non-invalid `value_map` entries.
2. Another ascending N scan copying retained root `IrValue` rows into their dense positions.
3. Another ascending N scan calling `c_ir_ssa_root` and redirecting alias entries to the root's dense mapping.

It then remaps instructions, parameters/incomings, local-value state, local places, and label metadata. Some instructions share operand slices, so the code deliberately writes a **fresh dense operand pool**. Remapping shared old slices in place could apply the map twice. This report leaves that pool and all metadata consumers intact.

### 3.6 Published topology and verification are already shared

[Canonical CFG publication](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/canonical-cfg-publication.md) already gives `IrFunction.published_cfg` contiguous instruction spans, source-grouped edges, sorted predecessor slices, block parameters, and dense edge arguments. `ir_function_publish_cfg` transposes construction incoming columns once; `ir_function_cfg_edge` uses sorted predecessor slices. Native, Wasm, eBPF, and LLVM consumers share this representation. The native selectors' old private canonical-CFG reconstruction has already been removed.

Publication preserves block/value/local/symbol/label IDs while explicitly remapping instructions and source/extras. Detached construction allocations remain arena-resident until translation-unit release. `ir_function_invalidate_cfg` reconstructs mutable links before mutation, invalidates old builder pointers, and requires appropriate certificate revocation. Avoiding unnecessary reopen/republish is sensible only after finding an actual caller; this report does not claim a new instance was found.

[`ir_function_instruction_owners` and `ir_validate_module_ownership`](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/ir/ir.c#L4380-L4520) prove bounded instruction-chain ownership before other checks rely on it. A second visit rejects a cycle/shared chain; a tail mismatch or orphan is an error. One module-sized scratch capacity is reused. Removing this proof because semantic validation later also visits instructions would be incorrect.

The [validation-boundary contract](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/ir-validation-boundaries.md) explicitly distinguishes structural/type checks from dominance. Canonical validation and publication do not establish a complete canonical dominance proof. `machine_verify_function` supplies the ordinary immutable-machine-value dominance contract, including unreachable components. Neither verifier alone proves generated-program semantics or ABI behavior.

## 4. Best work-elimination proposal: stable compaction with an expired queue

### 4.1 Notation and boundary

N is the old value count at final remapping; A is the number of non-self replacement entries then; K is the number of entries marked retained before compaction. I is instruction count, O is instruction operand occurrences, B is blocks, P is retained/provisional parameters at the relevant subphase, H is incoming occurrences, and T is terminator-target occurrences. Use checked 64-bit arithmetic for byte counts and size sums; IDs retain existing 32-bit sentinel rules.

The proposal begins **after** memory recovery, forwarding, trivial elimination, and liveness, and **before** any value/operand/metadata remapping. It changes no phi decision, root selection, source operation, CFG edge, or public representation.

### 4.2 A1: fuse the first two scans

Pseudocode is a design, not a submitted C implementation:

```text
next = 0
for old in ascending [0, N):
    if value_map[old] != INVALID:
        destination = next
        next += 1
        value_map[old] = destination
        if replacements[old] == old:
            values[destination] = values[old]

perform the existing final replacement-map scan
perform all existing operand and metadata remapping
```

This changes `3N` source-level value enumerations into `2N`, with no additional scratch or projection. It preserves the baseline's exact condition for copying a row; do not silently assume every retained marker is a self root and simplify the condition further.

**In-place proof:** the dense destination assigned to `old` is at most `old`. An earlier copy therefore cannot overwrite a future old row. ID assignment is still the ascending rank among exactly the baseline's non-invalid entries. The map and value arrays are separate; copying a value cannot change future retention markers. Existing root, instruction-definition, type, source, and label fields move together as before.

A1 is the smallest independent handoff and the scalar baseline for everything below. It still requires execution-based equivalence and throughput acceptance; fewer loop iterations alone are not an accepted speedup.

### 4.3 A2: build an ordered alias frontier in that same scan

Reuse the expired liveness `u32 work[N]` as an alias-ID list. No new allocation and no extra discovery pass are required:

```text
next = 0
alias_count = 0
for old in ascending [0, N):
    parent = replacements[old]
    if value_map[old] != INVALID:
        destination = next
        next += 1
        value_map[old] = destination
        if parent == old:
            values[destination] = values[old]
    if parent != old:
        work[alias_count] = old
        alias_count += 1

for index in [0, alias_count):
    old = work[index]
    root = c_ir_ssa_root(replacements, old)
    value_map[old] = value_map[root]
```

This is `N + A` enumerated value IDs rather than `3N`. All aliases are included, including aliases of discarded values: preserving invalid mapping behavior is part of the failure contract. `A <= N`, so the existing queue capacity suffices. Do not reuse the initialization queue merely because it has the same element type; its conditional allocation and lifetime are different. The specific liveness `work` is the proven reuse candidate.

**Root invariant:** during this epoch the replacement relation is an acyclic forest with self roots, as required by the existing root resolver. Only path compression occurs; there are no new redirects, memory-root resets, or representative choices. A self root remains self, and a non-root cannot become self. Every root's dense mapping has been assigned before alias resolution begins, including roots whose old ID is greater than the alias's ID. Consequently, aliases can be visited in ascending old-ID order with exactly the original root choices.

The original third loop's calls for self roots have no effect. Omitting those calls while preserving the order of all non-root calls leaves path-compression behavior and resulting maps unchanged. If later code introduces a merge/reset between these phases, this proof is invalid and the optimization must be re-evaluated.

### 4.4 Complete costs and crossover

Let F be the actual work performed by the existing root resolver for the alias queries. The complete finalization slice, including unchanged operand/incoming/metadata remapping, costs `O(N + A + F + I + O + H + B + metadata)` for A2. A1 costs `O(N + F + I + O + H + B + metadata)` with different scan constants. Existing local-value arrays, when present, retain their actual scanned extent; they are not silently included in a smaller sparse bound.

Within this **fixed-forest, compression-only** epoch, an edge whose parent is not yet a root is compressed on its first traversal, and each query pays a constant terminal traversal. Thus F is bounded by `O(N + A)` across the epoch. This argument does not apply to earlier interleaved merges or establish a linear bound for the entire SSA constructor.

A2 has no new requested array capacity, but writes and reads approximately `8A` bytes of queue elements, before cache-line/write-allocation effects. It may touch queue pages that liveness did not touch. Reserved storage reuse therefore does not prove zero additional RSS or traffic. It also reads `replacements` for dead IDs which the original row-copy scan could skip.

A2 beats A1 only when the removed full-map enumeration/root-no-op cost exceeds this extra classification, queue traffic, and alias-loop overhead. An approximate selection inequality is:

```text
N * cost_of_A1_final_scan_control_and_root_test
    > N * additional_fused_classification_cost
      + A * (queue_store + queue_load + queue_iteration)
```

The common alias-resolution and canonical-output costs cancel only under identical root/query order. A high alias density, tiny functions, or cold queue pages can favor A1. Do not add a separate N-sized census pass simply to choose the supposedly faster path. Measure variants first, then select a bounded size/population rule supported by existing facts or a small explicitly charged sample.

### 4.5 Useful but separate lifetime experiment

`pending_by_value` and the later `parameter_by_value` serve different phases. A reviewed shared pointer workspace could reduce simultaneous/retained requested scratch by roughly one pointer per old value, but must preserve live pending access through simplification, representation typing, and clearing of stale entries. On a 64-bit host that is an `8N` requested-byte candidate, not an automatic `8N` RSS saving or removed clear. Keep it separate from A1/A2 so measurements can attribute effects. Sparse clearing needs a complete touched-entry inventory; no uninitialized reads are permitted.

## 5. Best genuine SIMD proposal: classify fixed replacement roots in place

### 5.1 What is vectorized

At the same final fixed-forest boundary, `replacements[old] == old` is an independent query over a contiguous `u32` array. Perform 16 such queries together, then run the existing ordered scalar resolver only for non-root lanes. This is genuine SIMD classification, **not** a claim to vectorize a dependent graph walk.

The reviewed [SIMD header](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/simd.h) already supplies `simd512_load`, `simd512_splat_word`, `simd512_or`, `simd512_equal_word`, and integer mask operations. It does not expose a gather operation. The selected design needs none and adds no IR opcode, builtin, graph representation, dependency, or portability framework.

### 5.2 Layout and pseudocode

Use an immutable 64-byte table `u32 lane_ids[16] = {0, ..., 15}` and the existing arrays. It needs no lazy global initialization. Macro arguments must be side-effect-free.

```text
lane_vector = simd512_load(lane_ids)
base = 0
while N - base >= 16:             // maintain base <= N; checked ID extent
    parents = simd512_load(replacements + base)
    ids = simd512_or(simd512_splat_word(base), lane_vector)
    aliases = (~simd512_equal_word(parents, ids)) & 0xffff
    while aliases != 0:
        lane = mask64_first_set(aliases)
        aliases = aliases & (aliases - 1)
        old = base + lane
        root = c_ir_ssa_root(replacements, old)
        value_map[old] = value_map[root]
    base += 16

for old in ascending [base, N):
    if replacements[old] != old:
        root = c_ir_ssa_root(replacements, old)
        value_map[old] = value_map[root]
```

`base` is a multiple of 16, so bitwise OR with lane IDs is exactly addition without requiring a missing word-add abstraction. Load only complete vectors and use a scalar tail. Do not assume a 16-bit word mask can be passed to a byte-masked load. Keep all ID additions and byte-address calculations within their validated extents; no overread, invalid gather lane, or vector-width padding contract is introduced.

### 5.3 Conflict, ordering, and termination proof

An earlier lane's path compression can change a later lane's parent pointer, but cannot change whether that later ID is a self root. Therefore the loaded vector's alias classification remains valid throughout the tile. Ascending tile order plus least-set-bit iteration preserves the old-ID order of all non-root queries. Each scalar query updates only its existing path and its own map entry; no vector scatter or cross-lane write arbitration is needed.

Each set-bit loop removes one bit. Each root walk terminates under the existing forest invariant. No SIMD lane independently decides a representative, and no change to C initialization policy is hidden in a mask. Use the improved scalar algorithm when the full vocabulary is unavailable. #574's F/BW-only report requires explicit feature-tier tests rather than assuming fallback correctness from successful compilation.

### 5.4 Four implementations to compare

| Variant | Mechanism | Setup and liabilities |
| --- | --- | --- |
| Improved scalar / sparse frontier | A1 and A2 | Strong baseline. A2 uses already allocated queue capacity but pays alias traffic. |
| Scalar-batched | Unroll 8/16 parent-ID comparisons, build a scalar bitmask, visit set bits in order | Measures batching/branch structure independently of SIMD. Inspect producer assembly so an auto-vectorized control is not mislabeled scalar. |
| AVX2 experimental comparator | Eight dword load/compare lanes, register-bitcast and movemask, low-eight-bit mask | Host experiment only; not a new production `Simd256` API. Scalar tail, exact same root routine/order. Include target-feature isolation costs and do not compare differently optimized whole compilers as though only width changed. |
| AVX-512 selected candidate | Existing word equality over 16 contiguous parents, low-16-bit mask, ordered scalar escapes | One 64-byte constant; no heap projection. Full-vector loads plus scalar tail. Dense alias populations still do scalar resolution and can lose. |

The AVX2 mechanism corresponds to integer comparison followed by `movemask` on a register bitcast; this is not floating-point conversion. The primary [Clang AVX header](https://clang.llvm.org/doxygen/avxintrin_8h.html) and [AVX-512 header](https://clang.llvm.org/doxygen/avx512fintrin_8h.html) define these operations. Production must continue to use the repository vocabulary and its tested self-host/host/fallback paths rather than embedding a new intrinsic layer in `c_gen.c`.

### 5.5 Crossover and non-additivity

For vector width W and alias count A, compare:

```text
SIMD setup + ceil(N/W) * tile_classification
           + A * set_bit_iteration + identical root work
```

against both A1's scalar scan and A2's fused discovery plus alias-queue traffic. AVX-512 wins only if vector classification and mask handling save more than setup, register pressure, and scalar-escape overhead. Cache residency and the joint per-function distribution matter more than the aggregate mean. No numerical threshold has been measured here.

**A1 and SIMD are independent:** A1 removes the separate retained-row copy scan; SIMD changes classification in the remaining root-map scan. Test baseline, A1, SIMD alone, and A1+SIMD.

**A2 and this SIMD proposal overlap:** both avoid root-no-op queries for self roots. A2 discovers aliases during the fused scan and removes a later dense scan; SIMD still scans the dense replacement array but avoids queue writes. Compare A1+A2 against A1+SIMD as alternatives. Do not sum their claimed savings. SIMD acceleration of A2's first pass would be a different mixed retention/compaction kernel and is not approved by this design.

## 6. Other graph strategies, with their full price

### 6.1 Ordered affected-parameter agenda

The remaining simplifier repeats visits to surviving parameters. A notification scheme may help, but it must reproduce the current Gauss-Seidel-like ordered decisions, not merely converge to some semantically equivalent result.

A concrete experimental projection assigns a temporary dense parameter ID in current block/parameter order. It may need `parameter_node[P]`, an old-value-to-parameter map, alive flags, and two ordered dirty sets. Reverse dependencies can use occurrence arrays `consumer[H]`, `next_occurrence[H]`, and root-indexed head/tail arrays. If contiguous incoming ranges are also required, add `incoming_offset[P+1]` and `incoming_value[H]`. The complete storage is `O(N + P + H)`, with every array, pointer width, clear, and retained builder allocation charged. A new `4N` ID map is not free merely because IDs are compact.

Build the projection once after forwarding/memory decisions. Normalize each incoming occurrence to its current root and append it to that root's subscriber bag. Seed every live parameter in stable order. When parameter root r redirects to s, notify the subscribers of r and concatenate that bag into s's bag. A raw list of uses of the original r is insufficient: subscribers must remain associated with the equivalence class after subsequent root merges. Preserve the semantic representative s even if an auxiliary storage implementation uses different ownership.

To reproduce current sweeps, processing parameter p schedules a newly dirty q greater than p in the current sweep and q at or before p in the next sweep. Two deduplicated bitsets with ordered set-bit traversal, or explicit ordered queues with equivalent epoch rules, supply this contract. Merely using FIFO order or scheduling by thread completion does not. Initial already-replaced/memory parameters and later removed parameters need consistent alive-state filtering.

A dirty flag is only a notification, not a cached proof. Recompute incoming equality at the parameter's ordered decision point. Stop on two distinct roots just as the scalar implementation does. Every successful merge removes a live parameter; with P initial parameters, there are at most P such merges. No self-only phi may invent its own replacement. Queue duplication is bounded, but a subscriber occurrence can migrate through many roots: the naive worst case is **O(PH)** notification work, in addition to root-query and agenda costs. Calling it a worklist does not make it linear.

Two bitsets use about `2 * ceil(P/64)` words, but repeatedly scanning all words can cost `O(sweeps * P/64)`. Charge a hierarchy or ordered heap if sparse huge-ID frontiers require one. A sparse queue avoids full word scans but pays duplicate suppression and ordering. Measure the actual fraction of affected parameters/occurrences; do not import graph-benchmark thresholds.

Admit this experiment only when measured eliminated parameter/incoming visits exceed projection construction, additional edge passes, notification traffic, invalidation, and any order-restoration cost. Use a fixed scratch/work budget with fallback at a verified phase boundary, not mid-mutation into a different schedule. If exact ordered equivalence cannot be demonstrated, it becomes a separate semantic/identity decision rather than an implementation of this handoff.

### 6.2 Batched incoming equality

For fixed representatives and one parameter, the equality summary is a small associative reduction: EMPTY, ONE(root), or MANY, excluding the parameter's own root. Two equal ONE states remain ONE; two distinct roots become MANY. This supports scalar batching and SIMD equality on an already contiguous normalized incoming array.

However, construction currently uses linked incoming columns, root resolution is not contiguous, and scalar testing often exits at the second distinct root. Building a normalized `u32[H]` projection costs an incoming walk, root resolution, stores, scratch, and later invalidation. A vector comparison after scalar gathering may only add work. High fan-in alone does not establish a win; count the observed early-exit position and root distribution.

A batch of decisions computed before earlier merges is stale. It must be revalidated at ordered commit, have sound root-change dependency invalidation, or remain read-only diagnostic work. Parallel lane merges, arbitrary min-ID representatives, and unbounded fixed-point rescans are not acceptable shortcuts. Retain the original scalar decision for small joins and sparse irregular cases.

### 6.3 Replacement-chain compression

The present resolver already compresses paths. Full dense pointer jumping scans even roots and short chains, potentially paying `O(N log D)` work for maximum depth D. A bounded masked gather kernel would require a supported gather vocabulary, checked lane addresses, divergence handling, and a policy for conflicting compression writes. The current header does not provide that operation, and vectorizing dependent loads is not automatically profitable.

Defer that experiment. A future research-only comparator could cap tile rounds and drain remaining lanes through the existing scalar resolver, but must include lost compression benefits, gather-cache behavior, scratch materialization, and all self-host/backend feature obligations. It must not smuggle a new production instruction or framework into this report's two bounded proposals.

### 6.4 Initialization, lookup, and def-use maintenance

For slot growth, compare capped reservations from observed per-function demand against the existing geometric table, including oversized clears and retained old allocations. Retain safe growth on underestimation. Token count is a predictor, not a replacement for the sparse-key contract. Never allocate a quadratic blocks-times-locals matrix merely to vectorize lookup.

For mark sets, dense bitsets reduce storage but introduce read-modify-write and indexing; sparse touched-ID lists pay list traffic; generation tables pay tag bandwidth and wraparound cleanup. The current liveness map already supplies visited membership, so allocating another bitmap for it is not automatically an improvement. The read-stamp design already exists in predecessor forwarding.

For use maintenance, distinguish instruction operands, parameter incoming occurrences, and source-recovery journal entries. They have different consumers and last uses. Build a reverse-use projection in bulk only for a measured consumer that needs reverse enumeration; counts or backward liveness alone do not justify maintaining it during every append and rewrite. All pass-owned projections die or are rebuilt on mutation and canonical compaction. No permanent def-use or GPU graph is proposed.

## 7. Graph shapes and a reproducible experiment contract

### 7.1 Refresh only the missing census

Extend existing `BUSTER_BENCH_ALLOCATIONS`/source-metrics infrastructure in a separately authorized diagnostic implementation, not a second telemetry framework. Reuse #548's counters and coordinate validation-specific data with #576. Instrumented producers must not be the timed compilers.

For each function, or bounded histograms with an identifiable tail sample, collect:

- N, B, unique edges, terminator target occurrences, eligible locals, demanded slot keys, peak table capacity, growth/probe counts, and reachable/disconnected classification.
- Provisional/surviving/removed P, total H, fan-in bins 0/1/2/3-8/9-16/17-64/65+, simplification sweeps, and equality's actual early-exit position.
- A/N at the fixed final-remap boundary, contiguous alias-run lengths, root-chain lengths before and after query-order compression, and repeated root-query counts.
- Maximum/current live frontier sizes, distinct affected parameters per merge/sweep, duplicate notifications, requested bytes, bytes explicitly cleared/written, and queue pages touched where measurable.

Do not infer these distributions from the aggregate counters above. Use current frozen self-host input and the existing harness's `tiny_startup`, `many_functions`, `large_function`, `control_flow`, and other admitted workloads. Reuse pinned real-source descriptors from #423 when they are available and accepted; do not silently download a moving dependency.

### 7.2 Required difficult populations

| Population | What the experiment must retain or expose |
| --- | --- |
| Many tiny functions and parameter-free functions | No per-function graph projection or vector setup dominates; preserve current no-SSA fast paths. |
| Long single-predecessor chains | Iterative forwarding, stamp wrap handling, bounded C stack, and existing memoization. |
| Loops and loop-carried joins | Pending backedges, exact incoming order, correct trivial-phi decisions and termination. |
| Irreducible control flow and computed labels | Multiple entries and exact label/provenance identity; no assumption that block order is dominance order. |
| Unreachable blocks and closed source SCCs | Preserve memory recovery, retained/self-only versus dead cyclic parameters, and machine verifier's disconnected-component contract. |
| High fan-in and duplicate targets | Unique CFG edges with stable source order; measure early disagreement versus all-equal incoming sets. |
| Large functions and sparse demanded locals | No B-by-local or P-by-P matrix; account for ID extent, caps, overflow refusal, and scratch high-water. |
| Late escapes, uninitialized copies, recovered places | Whole-owner replay at original source anchors, including RHS-only dependencies; no synthesized zero. |

Existing publication regressions already include degrees 0/1/2/17/4096 and parameter widths 0/1/2/32/33. Those are coverage shapes, not evidence that real workloads have those frequencies. Use the registered geometric direct-SSA work tests rather than substituting a graph microbenchmark for the compiler path.

### 7.3 Frozen artifacts and execution location

Freeze producer source SHA/tree, trusted Clang version and binary hash, generated headers/resource inputs, build configuration/flags, target feature set, corpus bytes/hashes, compiler argv, mode, artifact kind, thread count, and output-path/debug normalization policy. Compile baseline and candidate against the **same frozen input**, not their respective changing source trees.

Run diagnostics and correctness on explicitly authorized CI or remote execution. Dedicated acceptance uses the existing admitted 9700X mechanism, host qualification, and whole-job exclusion under #46/#422/#426/#437; a GitHub runner label, CPU affinity, or access to a server name is not proof of physical isolation. No remote service execution was invoked in this research session.

Follow [the native throughput harness](https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/tools/throughput/README.md). Its existing entry points include:

```sh
./build.sh bench_throughput self-test
./build.sh bench_throughput run --baseline /absolute/base/ide --candidate /absolute/candidate/ide --output build/ssa-comparison --baseline-id BASE_SHA --candidate-id CANDIDATE_SHA --mode all --require-identical-output
./build.sh bench_throughput compare --output build/ssa-comparison
```

These are handoff commands, **not commands run for this report**. Supply an admitted profile, paired sampling count, warmups, and uncertainty policy from the existing contract, rather than treating defaults or two smoke pairs as acceptance. Use a new output directory, no concurrent compilation on the measured host, no automatic baseline replacement, and retain failed/inconclusive observations. Hosted CI is correctness/noise-screening evidence, not a replacement for dedicated Zen 5 acceptance.

### 7.4 Stage and whole-compiler quantities

Separate final-remap time/work from all of `c_ir_ssa_finish`, all C-to-IR lowering, complete stage-1 compilation, and time to the final object/executable. Also record compiler CPU time, cycles/instructions where available, branch misses, cache misses, bytes written, minor faults, peak RSS, and generated artifact identity/size. An unavailable PMU field is unavailable, not zero.

Let f be the newly measured fraction of the complete compile spent in the changed slice, s its measured acceleration, and h added overhead as a fraction of baseline compile time. Then the conditional whole-compiler speedup is `1 / (1 - f + f/s + h)`. Neither historical finish samples nor the Callgrind validation share supplies f for these algorithms. Do not translate `3N -> N+A` directly into a percentage time saving.

Report one-lane/single-thread behavior and applicable existing multi-lane behavior separately. The kernels introduce no threads; any future function parallelism must use existing persistent lanes with private scratch and deterministic source-indexed publication after the shared-state boundary is proved. Do not multiply nested gangs or create a GPU offload path.

## 8. Correctness and acceptance gates

### 8.1 Exact algorithm oracles

A1/A2 and the SIMD classifier are representation/work-only changes. On a test-owned copy of the same pre-compaction state, compare the baseline and candidate old-to-new map, retained `IrValue` rows, final value count, instruction results and operand pools, parameter order/count/values, incoming order/values, local-value and local-place mappings, label metadata, and source/extras. Also compare resulting replacement arrays where the same query order is promised. Normalize nothing that encodes a semantic identity.

Use generated fixed-seed replacement forests with self roots, chains, stars, forward/backward IDs, shared suffixes, dead roots, mixed retention, and every tail length 0-15. Include N = 0/1/15/16/17 and boundaries around chosen dispatch sizes. The oracle must call the production helper/seam, not a copy of its new algorithm. Invalid forests are negative tests for the existing invariant boundary, not a request to add unchecked cycles to the hot resolver or weaken diagnostics.

A2 additionally checks queue capacity, the liveness queue's last use, discarded aliases, and root IDs greater than their users. SIMD additionally checks masks' high bits, non-overreading tails, all-self/all-alias/mixed tiles, and a chain where resolving an early lane compresses a later lane. A stale-snapshot test must fail an incorrect implementation which assumes all loaded parent values remain unchanged, while allowing the valid self/non-self classification invariant.

### 8.2 Compiler and verifier gates

Run registered `c_test_direct_ssa`, `ir_promotion_tests`, `ir_promotion_validation_tests`, `ir_cfg_publication_tests`, relevant SIMD tests, and the full applicable suite on the exact submitted implementation. Retain `tests/basic_c_frontend_ssa.c` and `basic_c_ir_validation_values.c`, including loops, irreducible joins, disconnected labels, late recovery, initialization, and provenance cases.

Keep `ir_validate_canonical_module`, required ownership checks, publication shape validation, and `machine_verify_function` at their existing boundaries. Exercise `-fverify-codegen` and `BUSTER_VERIFY_IR_TRANSFORMS=1` where applicable. Debug, test, and sanitizer checks must not be suppressed by turning the explicit transform switch off. Check exact error category, contextual IDs/source location, diagnostic order, and absence of valid-looking partial output on failure.

Test frontend SSA on and `-fno-frontend-ssa` independently of canonical/target promotion options. Cover the actual current NONE/MIR_STACK/FAST/QUALITY semantics; #522's proposed mode remapping must not be assumed landed. Rebase the mode inventory if cutover occurs. Cover supported native target/platform combinations and non-native Wasm/eBPF/LLVM consumers at their applicable execution, independent-validator, or object-only evidence levels. Do not call object generation native execution.

Require Debug/Release, unity/non-unity where applicable, supported bootstrap/producer compilers, sanitizer configurations, scalar fallback, AVX2 comparison, full AVX-512, and the F/BW-only tier implicated by #574. The production SIMD branch must remain correctly gated even before that separate issue is resolved. Retain Zen 4 evidence required by #447 for an AVX-512-specific change; do not assume a Zen 5 result transfers to all hosts.

### 8.3 Byte-identical self-hosting

For each candidate, run the ordinary and repeated self-host fixed-point gates and relevant mode/target matrix. Require byte-identical successive self-host generations under the existing deterministic build contract. Separately require baseline and candidate to emit identical artifacts for the same frozen inputs/options for these representation-only changes. A candidate's internal fixed point does not by itself prove baseline equivalence, and a self-built producer is not the trusted performance producer.

Check deterministic canonical IDs/source and diagnostic ordering under all supported lane counts. Preserve failed attempts and precise binary/source identities. An earlier PR's successful fixed point cannot validate a later integrated tree.

### 8.4 Performance decision

Retain a candidate only after trusted uninstrumented paired measurements show a useful stage/whole-compiler result with uncertainty and without unacceptable small-input, memory, code-size, or behavior regressions under the applicable agreed contract. The experiment must permit a negative decision: A1 only, A1+A2 only, A1+SIMD only, or no production change. No new numeric budget or retirement approval is created by this document.

## 9. Bounded implementation handoff

**Slice 1 — A1 compaction fusion:** reconcile current #447 ownership and source; change only the adjacent ID-assignment/root-copy loops and their truthful diagnostic work accounting. Add exact map/row/metadata oracles through existing tests. Preserve the third loop and all output consumers. Obtain exact-head correctness and paired performance evidence before adding a second mechanism.

**Slice 2 — A2 ordered alias frontier:** only after verifying the liveness queue's last use on the integrated head, reuse it for all non-self IDs during Slice 1's scan. Add boundary/invalid-map/page-touch census and equivalence controls. Compare against A1, not merely the original three-pass code. No slot-map redesign, early sealing, permanent reverse-use graph, or allocator refactor belongs here.

**Slice 3 — independent SIMD alternative:** compare A1+the existing-vocabulary 16-way classifier with A1+A2 and scalar-batched/AVX2 experimental controls. Preserve scalar escape order and tails, coordinate #574 feature-tier correctness, and retain dedicated Zen 5/required Zen 4 evidence. Do not ship both mechanisms with a heuristic until measured joint distributions justify the extra policy.

The ordered dirty-parameter agenda remains a **conditional later experiment**, not a fourth promised production slice. Admission requires a measured revisit/notification crossover and an exact-order proof. Slot reservation, validation attribution, function parallelism, native retirement, and architecture changes remain with their existing owners.

## 10. Primary algorithm research and applicability limits

[Braun et al., CC 2013, Simple and Efficient Construction of Static Single Assignment Form](https://compilers.cs.uni-saarland.de/papers/bbhlmz13cc.pdf), especially Algorithms 2-3 and sealed-block construction, supplies memoized backward reads, provisional phis, and affected-user simplification. The paper's recursive presentation must be made iterative here. Its treatment of undefined/self-only values is not authority to replace Buster's memory-recovery contract. Reducible minimality and extra irreducible-graph treatment do not authorize arbitrary SCC merging or changed representatives.

[Tarjan and van Leeuwen, 1984, Worst-Case Analysis of Set Union Algorithms](https://collaborate.princeton.edu/en/publications/worst-case-analysis-of-set-union-algorithms/) analyzes distinct path-compression variants. It motivates stating the actual algorithm and union policy rather than claiming every replacement table has the optimal disjoint-set bound. Section 4's narrower fixed-forest argument is an argument about this final phase, not a transfer of a general theorem to earlier SSA rewriting.

[Beamer, Asanovic, and Patterson, 2013, Direction-Optimizing Breadth-First Search](https://onlinelibrary.wiley.com/doi/abs/10.3233/SPR-130370) demonstrates why frontier/edge work can justify sparse/dense switching on particular graph distributions. Compiler CFGs and ordered destructive phi simplification are different populations and semantics. Its graph speedups and thresholds are not predictions for Buster; it motivates the measured-work comparison, not a dense adjacency matrix or a BFS replacement for SSA.

The [Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html) and the primary Clang headers linked in Section 5 supply operation semantics. They do not supply Buster crossover timings. External references were checked on 2026-09-13; live documentation is not revision-pinned like the Buster sources.

## 11. Publication and unrun-gate ledger

This Markdown is the single detailed research report. A timestamped audit entry links here; issue comments and the documentation PR should summarize and link rather than duplicate it. No new issue is needed for the overlapping #447 scope. Publishing the research is not approval to implement, merge, reopen, or close any issue.

Authorized access route: connected GitHub repository read, source/blob reads, live issue/PR reads, and new-branch documentation publication. Large-file contents retrieval was incomplete and the direct blob route succeeded. An alternate local CLI was unavailable and direct network retrieval failed at DNS/proxy resolution; neither was needed for successful connector access. No credentials were exposed or copied. File-path absence was checked before creation; no existing report or other session's branch is overwritten.

The actual created PR/comment URLs and read-back status belong in the publication response, after creation. This document deliberately does not claim an uncreated PR, completed CI run, or measured result.

| Gate | This research session |
| --- | --- |
| Pinned source, guide, issue/PR, and historical-evidence review | Performed through connected reads; limitations identified above. |
| Primary algorithm research | Performed; PDF algorithm pages inspected. |
| New compiler/algorithm implementation | Not performed; documentation only. |
| Local or remote compiler execution, census, tests, sanitizers | NOT RUN. |
| Self-host fixed points and mode/target matrix | NOT RUN for this report. |
| Dedicated timing, PMU, cache/branch measurements, graph-shape histograms | NOT RUN. No speedup or numerical crossover claimed. |
| New CI workflow or production-source changes | None authorized or proposed in this publication. |
| Audit generator and full repository documentation checks | NOT RUN under the no-local-execution contract; inspected timestamp/file conventions followed, with final submitted-document CI left unclaimed. |
| Merges, issue closures, force pushes, other-session edits | None. |

**Bottom line:** the strongest immediate proposal removes a complete value pass and can replace another with an already allocated, ordered alias frontier. The strongest bounded SIMD proposal classifies contiguous replacement identities without gathering graph nodes or changing roots. Their winner is a measured question; the canonical graph and semantic/identity contracts need not change to answer it.
