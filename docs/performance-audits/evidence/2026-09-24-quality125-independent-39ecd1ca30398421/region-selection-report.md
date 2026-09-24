# #125: exact next-region ordering — executed prototype, production decision deferred

24 September 2026.

## Result and limits

**No production change is justified by the evidence obtained here.** This is not a finding that region indexing cannot improve QUALITY. Two concrete adoption hypotheses failed a cheap selector-only screen: constructing a complete compact heap on the second request can substantially regress two-request dense rows, while caching tile winners on the first request regresses first-only and empty rows. Both representations greatly reduce work when a large row is drained, but that does not establish their value on current compiler populations.

Implemented and executed: the current scalar selector, a compact-list control, a lazy complete region-ID heap, a tiled-winner heap, deterministic exact-order tests, sanitizer tests, generated operation-budget checks and separate uninstrumented timing. Not implemented or executed: integration into QUALITY, registered Buster regressions, current-workload census, complete allocator and compiler comparison, or production publication. Keep #125 open.

The connected GitHub interface in this session supplied reads and artifact downloads, not write/create/dispatch actions. Local Git access failed DNS resolution. Plugin discovery did not supply a usable alternative repository writer or admitted execution route. Existing CI artifacts were retrieved rather than treating historical green checks as validation of a new candidate. These are session capability limits, not reasons to relax acceptance.

No user's desktop work, retirement work, policy change, service deployment, generated-code-quality experiment, candidate-admission change, spill economics change, or duplicate census implementation was performed.

## Exact identities and source basis

The actual inspected base was `2e942e80a87666409cf29d3e24a68d322b9e71fd`, tree `f976bfd7cdb8830a8a15d959ef36f0152f1b767d`. The closing main read was `5d1c314a8ee3c5f9fb713f6afb85ab1e795f44a0`, tree `b1f7f60e49d79b1452349493c3f6cbc2a4acd93d`. The QUALITY implementation has the same Git blob, `2ce4bfdfb73d07cd9a127812700108403b770de8`, at both reads. This proves identity of that file, not identity or validation of the two complete repository trees.

`identities.json` and `raw/build-sha256.txt` bind the separately tested prototype. There is **no repository candidate SHA**. The scalar control matches the pinned production helper after removing the diagnostic increment and normalizing whitespace; the original helper is retained in `pinned-scalar-helper.c.txt`.

Reviewed material includes pinned AGENTS, relevant machine/benchmarking guidance, the returned #125 discussion, the landed census and weighted/touched-scratch continuations (#381/#462/#496), the #586 research report, the blocked P06 bitmap experiment, and the matched-build audit of 20 September. Overlap searches were bounded, not exhaustive changed-path clearance or a writer handoff. No branch was claimed or modified.

The earlier closure experiment remains negative/inconclusive evidence at its historical scope; it was not run, republished as a win, or used for acceptance here. The census is already landed. Exact u64 traffic and touched pin resets are also already present. The 20 September audit's build-path/layout confound is a further reason not to promote a helper timing into a compiler conclusion.

Source ledger, all paths in `buster14a/buster` at the inspected base unless stated otherwise:

```
AGENTS.md
src/buster/lib/compiler/codegen/register_allocator_quality.c
src/buster/lib/compiler/codegen/register_allocator_quality_internal.h
docs/agents/machine.md
docs/agents/benchmarking.md
docs/quality-scratch-census.md
docs/astra-simd-08-register-allocation.md
docs/performance-audits/2026-09-12T174302Z.md
docs/performance-audits/2026-09-20T050606Z.md
.github/workflows/compiler-throughput.yml
```

## What remains in the current implementation

For C admitted candidates and L merged regions, construction still reserves and clears `C * L` u64 traffic cells: **8CL bytes**, not the older 4CL figure. Baseline spill/reload edits map through the original candidate slot and a region lookup; traffic additions preserve their exact weighted arithmetic. Candidate heap rearrangement does not redefine this slot map.

`machine_quality_region_next` still reads the complete row on every query, including exhaustion. A candidate reaching K positive regions can incur `(K + 1) * L` scanned cells if all regions are rejected. Summed over split candidates, the familiar O(CL²) upper bound remains. This is a source bound, not an observed current compiler population or latency.

There are three packing attempts, but **only attempt 0 allows splitting** at this base. A reusable order across multiple split-enabled retries is therefore not a current requirement to invent. Candidate heap restore/pop order, marginal early rejection, whole-span probes, split legality/budget/register/cost probes and later fallback remain part of the unchanged consumer contract.

Current QUALITY receives sorted merged spans from the shared prepass. It no longer owns the older local loop-sort scratch array. An index cannot claim that array as free storage. The tested tiled prototype uses separate u64 workspace and must be charged for it. Also, the current source increments both `raw_loop_spans` and `merged_regions` from the merged prepass count; those names alone must not be mistaken for independent raw-versus-merged populations. This experiment did not modify that census.

## Population evidence actually available

The existing main bootstrap artifact `10805548502`, run `35994136733`, was downloaded and hashed. Its 34 `.metrics` files contain no `quality_census` keys. The retained throughput artifact `10804539000`, run `35993381092`, belongs to the earlier PR head `de1b561fae7d9428ff9a34162e89bcd1b9f5f8e0`; its 2,016 `.metrics` files also contain no such keys. Artifact identities and this inspection are in `raw/artifact-inspection.json`.

Missing keys are unavailable, not zero. Existing successful CI and bootstrap runs are not candidate tests. The current distribution of C, L, K, query count before acceptance, and per-candidate/table density was **not measured**. No sparse/dense production representation or dispatch threshold is selected without it. The generated populations below challenge the algorithms; they are not a replacement for that census.

## Hypotheses and controls

The primary experimental change is **replace repeated full-row next-region scans with an exact region ordering/index**, leaving dense traffic construction untouched. This does not combine pin-budget indexing, local-vreg remapping, closure, admission or splitting-profitability changes.

The first falsifiable hypothesis was: keep the first scalar query, then compact remaining positive IDs and heapify only when a second region is requested; saved repeated scans should repay construction. The cheaper control keeps the same compact IDs but repeatedly removes the maximum by linear search. The original scalar selector is the baseline.

The second hypothesis was: cache one winner per 16-region tile during the first traversal and maintain a heap of tile winners only when another query is requested. This reduces full-heap setup and index storage. The tile width 16 is an experimental constant, not a measured production threshold or a SIMD dispatch choice. Rows of length at most 16 call the scalar helper; that fallback still has wrapper overhead in this standalone implementation.

## Exact ordering argument

For each positive-traffic region r define its rank by descending `traffic[r]`, then ascending global region ID r. This is a strict total order. Zero-traffic regions are absent. No subtraction or narrowing is used to compare u64 traffic; equality retains the ascending-ID tie.

The scalar helper returns the greatest key strictly following the previously returned key. A compact heap containing exactly that remaining suffix, using the same total order, returns the identical sequence by induction. The compact-list control uses the same set and maximum relation without heapification.

For tiled winners, initially each nonempty tile caches its greatest key and the first returned region is the greatest of these tile maxima. At the next request, heapification makes the previously returned global winner the root. Only that winner's tile can expose a new maximum: every other tile already has its remaining maximum cached, and that key follows the old global winner. Rescan the winning tile strictly after its old winner, replace or remove its root entry, and sift down. Induction preserves the global scalar sequence through exhaustion.

The caches do **not** incorporate eligibility. Every positive region, including an illegal or unprofitable one, remains in the same enumeration. The caller must continue checking legality, live-past/exit rules, capacities, changing pin depth, foreclosure, register overlaps, excluded boundary rows, and exact split cost in their original order. No rejected earlier region is revisited merely because eligibility later changes.

The ranking precondition is essential: traffic must not change while an index is live. In the inspected implementation traffic is constructed from the baseline edits before packing and thereafter only read by regional search. Pin planning changes eligibility, not those ranking values. Tests deliberately mutate a hidden member's traffic after cache construction and require the stale cache to disagree with the scalar helper; rebuilding restores agreement. These are negative controls proving the test would detect an invalid immutability assumption, not support for live rank updates.

Candidate heap ties are a separate policy: the existing strict-greater interval heap is not replaced by the region comparator. Candidate admission/prefix, original slot mapping, register preference, stopping at the first accepted region and retry sequencing must remain unchanged. A fresh candidate must initialize fresh iteration state. Retry/fallback discards this state, and no pointer may escape scratch into a returned placement.

If those consumer and lifetime invariants are maintained, identical region sequences imply the same decisions and pin updates, which in turn should preserve FAST placement and the stable ordered-edit stream. That last implication is a **conditional integration argument** here: no full allocator, placement, ordered-edit or emitted-artifact comparison was executed. In particular, stable split-store ordering at equal rows must not be replaced by an unstable sort.

## Construction, query, update, reset and storage costs

Let K be the positive cells in a reached candidate row, q its requested queries including a possible exhaustion request, T=16, and P the nonempty tiles (P ≤ min(K, ceil(L/T))). Costs below exclude the rest of the allocator, which remains unchanged.

| Representation | Construction / activation | Subsequent work | Scratch capacity in this prototype |
|---|---|---|---|
| Current dense scalar | None beyond existing traffic table | O(L) per query; O(L(K+1)) for full rejection | No selector index |
| Lazy compact list | First scalar query; on second request O(L) collect remaining IDs | O(K) max removal; O(K²) full drain | 4L bytes, reusable across candidate rows |
| Lazy complete heap | First scalar query; on second request O(L + K) collection/Floyd heapification | O(log(K+1)) per nonempty removal | 4L bytes, reusable across candidate rows |
| Tiled winners | First query O(L) plus P winner stores/comparisons; on second request O(P) Floyd heapification | O(T + log(P+1)) per refresh/removal | 8 ceil(L/T) bytes plus iterator state |

Tiled full rejection costs O(L + P + K(T + log(P+1))), replacing a quadratic drain with O(L log L) for fixed T. With no positive entries it scans L once and exhausts. For L≤T the current scalar algorithm is used. Stopping after the first answer still pays the tiled construction, which is an important measured loss below.

All variants retain the dense 8CL-byte traffic table. Its clearing is Θ(CL); baseline edit traversal and binary region lookup add an O(M log(L+1)) upper bound for M memory edits, plus the unchanged row-weight/region-metadata construction. Each identified traffic-cell accumulation is O(1). This experiment does not reduce those construction costs or prove the dense representation appropriate.

There are no supported within-index traffic updates. Arbitrary rank mutations require a rebuild; merely updating the changed tile is not a valid general way to preserve a suffix defined by a possibly changed previous rank. Normal eligibility rejection consumes a query but does not change the rank inputs.

Reset is O(1) iterator metadata followed by the applicable rebuild; the list/heap writes only its initialized active prefix. Dirty inactive capacity is never read and need not be cleared. The tiled index overwrites nonempty winners before heap use. Worst-case physical capacity and arena allocation/free costs must still be paid; active-prefix operation counts are not a peak-RSS claim. A production u32 tiled index could halve this prototype's index bytes, but that is not the measured implementation and no saving is claimed for it.

Under sequential candidate consumption one capacity can be reused; retaining one heap per candidate is unnecessary. Reusing or borrowing any existing prepass array requires separate lifetime proof. No such reuse was implemented or assumed.

## Executed validation

The generator covers all ternary traffic rows of length 0 through 8, 2,048 deterministic mixed rows, and 28 boundary sizes with six patterns through 4,096 regions. It includes empty, singleton, dense, sparse, ascending, descending, all-tied, repeated-u32-boundary, <2^44 weighted-boundary and u64-limit comparator cases. The extra-large comparator values are robustness tests, not claims about reachable allocator traffic.

An independent insertion-sort oracle checks exact IDs, not only checksums. All suffix starts are checked for the small compact-list/full-heap cases; every full tiled sequence is checked. Early-stop walks, poisoned/reused state, redzones, shrinking sizes, repeated exhaustion, no traffic modification, changing eligibility and deliberately stale ranking are included. Rejection-heavy behavior is modeled by draining all positive regions; it is not a run of the real rejection gates.

| Gate | Result |
|---|---|
| Instrumented standalone selector/oracle and generated work budgets | PASS — 12,057 populations; 3,973,748 assertions; zero failures |
| Same selector tests, instrumentation compiled out | PASS — 3,973,423 assertions; zero failures |
| Standalone AddressSanitizer/UndefinedBehaviorSanitizer with leak detection enabled | PASS — 3,973,748 assertions; zero failures |
| Current C/L/K/query and density census using instrumented Buster | NOT RUN |
| Registered Buster regressions, full mode/target/diagnostic coverage | NOT RUN; no tests registered |
| Actual candidate decisions, placements, ordered edits and emitted bytes | NOT RUN |
| Complete QUALITY construction, selection, cleanup and retained/peak memory | NOT RUN |
| Matched trusted Clang-built Buster compiler throughput | NOT RUN |
| Hosted ordinary required CI, repeated self-host, full sanitizer suites | NOT RUN for a candidate |
| Authorized benchpress 9700X / hardware-specific measurements | NOT RUN |
| Repository writer and workflow-dispatch action | UNAVAILABLE in this session |

The geometric diagnostic asserts, among other properties, the old exact dense scan count and conservative heap/tile comparison bounds. At L=K=4,096, with 4,097 requested queries, it records:

| Strategy | Dense scan cells | Construction/refresh cells | Indexed key comparisons | Index writes |
|---|---:|---:|---:|---:|
| Scalar | 16,781,312 | 0 | 0 | 0 |
| Compact list | 4,096 | 4,096 | 8,382,465 | 5,460 |
| Complete heap | 4,096 | 4,096 | 88,770 | 96,235 |
| Tiled winners | 0 | 69,632 | 12,013 | 8,465 |

These are instrumented logical events, not equivalent costs, hardware loads, retired instructions, cache traffic or time. Traffic dereferences inside indexed comparisons are not included in the first two columns. A low value in one column is not a speedup proof.

## Selector-only screening and observed crossovers

The host reports a shared KVM AMD EPYC 9V74 environment; the build used Clang 17.0.0. The final timing executable is uninstrumented and contains all four variants; it is not a pair of matched Buster compilers. The core and driver were compiled separately without LTO. Each case uses a fixed deterministic row and consumes an observable checksum. The oracle check and warmups occur outside the timer.

The first three-variant screen fixed 31 cases and collected 1,116 samples. Its second-query heap regressions motivated the tiled variant. Its sources have been exactly recovered by hashes and the rebuilt timing executable is byte-identical to the original measured executable. That reconstruction did not create new timing observations.

The final four-variant screen separately fixed the same 31 shapes, 12 observations per strategy/case in balanced four-way orders, for **1,488 samples**. No results are pooled across the two source versions. The plan, all commands, binary/source hashes, raw elapsed times, checksums, medians and ranges are retained.

Only warm, preconstructed selector rows are timed. Index construction and consumption/reset are inside; traffic population construction, workspace allocation/free, the rest of QUALITY, process startup, compiler work, actual rejection checks and artifact emission are outside. This is explicitly not the repository's throughput acceptance harness and must not replace it.

The following are ratios of median selector time to the scalar control in the final screen; lower is faster. They are descriptive observations, not confidence-bound acceptance results.

| L | K | Requested queries | Scalar median ns | Compact list / scalar | Complete heap / scalar | Tiled / scalar |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 2 | 5.0 | 1.299 | 1.596 | 1.487 |
| 16 | 16 | 17 | 199.1 | 0.624 | 0.539 | 1.282 |
| 64 | 64 | 1 | 38.9 | 1.004 | 0.957 | 1.362 |
| 64 | 64 | 2 | 108.5 | 1.132 | 1.591 | 0.673 |
| 256 | 256 | 1 | 155.7 | 0.989 | 0.969 | 1.298 |
| 256 | 256 | 2 | 419.8 | 1.271 | 1.636 | 0.567 |
| 4096 | 0 | 1 | 1758.6 | 1.028 | 1.002 | 1.694 |
| 4096 | 4096 | 1 | 2483.8 | 0.931 | 0.932 | 1.241 |
| 4096 | 4096 | 2 | 6092.3 | 1.398 | 1.978 | 0.551 |
| 4096 | 4 | 5 | 6465.8 | 0.622 | 0.442 | 0.436 |
| 4096 | 64 | 65 | 92835.0 | 0.062 | 0.038 | 0.045 |
| 4096 | 4096 | 4097 | 13933097.0 | 0.532 | 0.018 | 0.026 |

On these generated dense rows of size 64 and above, the tiled variant crosses from a first-only loss to a two-query benefit. That is an observed local crossover for this implementation, CPU and traffic pattern, not a production threshold. q is an outcome of allocation; the first call cannot know whether it will be the last. The complete heap has the opposite activation problem: it preserves the first scalar query but can lose badly just when the second request triggers construction. The simpler compact list is an important control: small setup can win some sparse/short cases without a complete heap, but it keeps quadratic draining work.

The full-drain gains do not justify claiming that real compiler work approaches the source bound. Conversely, the first-only losses do not reject indexing for all workloads. Real per-row query/occupancy distributions and complete construction/cleanup costs remain the missing decision inputs.

Tiny scalar fallback is also not a zero-overhead guarantee: the standalone wrapper still regresses several tiny cases. Preserving the old outer path or adopting a different lazy activation might address that, but neither is integrated or measured here. No automatic threshold, crossover model or sparse-traffic production replacement is accepted.

## Required boundary before a production PR

The next valid implementation decision must use the current existing QUALITY census on frozen real compiler inputs and registered adversarial cases; the downloaded metrics do not provide it. If indexing is supported, integrate only this region-selection slice, preserve original heap slots and all consumer control flow, register exact decision/placement/edit/artifact and dirty-scratch tests, and use the existing full allocator replay and compiler throughput infrastructure. Charge the index workspace, traffic table, initialization, all actual retries and cleanup. Use matched trusted Clang-built Buster compilers and ordinary-task policy; do not import #36's retirement contract or create a prerequisite there.

Until those gates can run, this bundle is an executed research handoff with concrete negative controls and raw evidence, **not merge-ready code, a published PR, completion of #125, or an accepted evidence-backed rejection of every representation change**.
