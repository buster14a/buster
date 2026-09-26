# Interaction 7c91 — predeclared, non-timing experiment

This file is recorded before compiling the generated families or observing their work counts. No timing experiment is authorized by this packet.

## Authority and source

Base commit: `ade6ac4b6ecb21f30b61b656439bac476c145e2f`.
Base tree: `4c5306221fdb22fccc929b55e333163742de17d0`.
LLVM writer: `src/buster/lib/compiler/llvm/bitcode.c`, blob `bfdcda8bcb96f7150a374c5c9260bda3e074a306`.

Single writer: this GPT-6 Astra Pro research session. Owned publication scope: this new investigation directory and `.github/workflows/research-interaction-7c91.yml`, on `research/interaction-cost-20260925-astra-7c91` only. No existing branch is taken over. Experimental overlays are applied only to disposable, exact-base hosted worktrees; production source/defaults are not committed or merged. There is no production implementation ownership claim.

Read current AGENTS.md, build/testing/benchmarking guidance, frontend foundations, LLVM_BITCODE.md, the newest pinned audit 2026-09-25T003323Z and relevant lookup-growth/macro audits. Recent #1335–#1338 are LLVM ABI/entity correctness work, not this type-order experiment; avoid their functions. All-state exact `llvm_bc_build_types` and bitcode/quadratic searches and an open bitcode PR search found no matching report. Search absence is bounded, not proof about unpublished work. Repeat before publication.

No callable subagent executor was available after tool/plugin discovery. Scouts and reproduction in this packet are serial work by one session, not independent-agent reviews.

## Scouts and controls

1. **Selected:** `llvm_bc_build_types` scans every canonical type on every dependency-resolution sweep. `c_lower_to_ir_with_options` publishes aggregate placeholders in parse-type order before filling fields. Root-first forward declarations followed by legal leaf-first definitions can therefore oppose the LLVM dependency order. Unrelated already-resolved types remain in every sweep.
2. `c_macro_parameter_index`: formal-parameter count times replacement-list identifiers at definition time. Repeated expansion no longer repeats this classification. Normal formal lists may be too small to justify indexing; #1091/#1107 and the macro-storage work are controls, not new findings.
3. `llvm_bc_add_type_record`: linear interning of type records, including full operand comparisons. This is a competing explanation for total cost in the selected LLVM route. Count it separately; do not claim eliminating dependency sweeps fixes interning too.
4. Debug local/scope matching and whole-function matching have existing reports (#790/#987). Do not relabel them.

## Primary valid C family

U unrelated named structs each contain one int. D chain structs each contain one child struct by value, with the leaf containing one int. All chain tags are first forward-declared; definitions are then written leaf-first, which is valid C. The primary order forward-declares root-first. A control reverses only the forward-declaration order. A function returns sizeof the root; an independent caller checks sizeof(int).

Source declarations, semantic field relations, and layout are O(U+D), not O(U*D). Each unrelated struct has the same LLVM storage shape as the leaf; therefore increasing U should not require additional distinct serialized type records. The chain has D genuine nested type records. No macros, includes, debug output, generated data arrays, or runtime loops are needed.

Compile each generated input through the same pathname with frozen arguments: C17, x86_64-linux, -O0, -g0, -fwrapv, -fno-strict-aliasing, -funsigned-char, -ffrontend-ssa, -fverify-codegen, -emit-llvm. Save every input before overwriting the pathname. Do not compare output filenames as if they were semantic differences.

Grid: U in {0,128,512}; D in {1,8,32,128}; both root-first and leaf-first forward-declaration orders. Reduction controls: U=0, D in {2,3,4}. This is 30 source cases, not an unbounded size search.

## Predictions, before observations

Let T be the canonical type count and S the actual successful sweep count. Exact old-loop identity: table_slot_visits=T*S. Each type is successfully emitted once. Attempted unresolved types are a separate count. Under the source-derived ordering hypothesis, T=H+U+D for a fixed H, S=D in the root-first family, and S=1 in the reordered control. Additional fixed type dependencies could shift those constants; record a mismatch rather than fitting it away.

Predicted root-first slot work: D*(H+U+D). The interaction residual relative to U=0,D=1 is U*(D-1), including 65,024 additional visits at U=512,D=128. Unresolved attempts should contain the chain triangular term D*(D+1)/2 but no U*D term. The reordered control should remove both repeated resolved-row visits and the triangular dependency retries without changing the graph or field count.

Competing explanations: frontend topologically renumbers the chain (falsifies C reach); interning rather than readiness dominates total compiler work; output size grows with U; different diagnostics/options select another path; memory/cache/allocator thresholds affect elapsed time. No elapsed time will be used to choose among these. Count table visits, unresolved attempts, dependency edges inspected, successful resolutions, emitted records, interning record/operand comparisons, bytes of artifacts, and prototype setup/allocation requests separately.

## Counterfactual, fixed in advance

A shrinking unresolved vector alone removes U*S but leaves triangular chain retries: reject it as an incomplete mechanism intervention.

Instead construct the exact LLVM dependency graph in temporary arena storage, visit each dependency a bounded number of times, and compute each node's old sweep rank:

`rank(v)=max(rank(d)+(d>=v))` over its dependencies d; roots have rank zero.

A nonrecursive topological queue computes ranks. A stable bucket order by (rank, original canonical ID) reproduces the original successful-emission order, including same-sweep forward progress. Emit each type once in that order using the original type emitter/interner. This is O(T+E) planning and O(T) invocations, including setup, not a faster repeated scan. Preserve opaque-pointer nondependencies, enum/atomic dependencies, duplicate edges, padding, field mappings, and canonical validation. Invalid/cyclic graph detection must retain fail-closed behavior; an explicit old-path fallback is acceptable for this experimental overlay to preserve existing diagnostics, but must be reported.

Compare baseline/instrumented-baseline/instrumented-counterfactual/uninstrumented-counterfactual .bc bytes; independent Clang consumes and executes the grid. Instrumentation is per emitter context, never shared mutable state. Report requested scratch bytes as requests, not RSS or bytes touched. No persistent telemetry framework.

## Reach and regressions

Use pinned cJSON 1.7.19 source hashes from the existing workload descriptor, existing LLVM regression fixtures and a repository source/header workload where supported. Record complete emission failures distinctly from reached type preparation. Representative reach does not establish typical dominance.

Run scoped LLVM tests and available self-host/full-suite checks on authorized hosted executors, recording actual results and limitations. No desktop execution, SSH, dedicated-runner lease, service/admission changes, generated-binding changes, or performance acceptance. Numerical speedup and peak-memory conclusions remain unavailable without the user-specified exclusively leased 9700X protocol.
