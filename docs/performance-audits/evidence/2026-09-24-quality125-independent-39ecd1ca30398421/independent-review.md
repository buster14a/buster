# #125: independent candidate-region validation

**Disposition: useful independent validation of the active owner's implementation; not a new production change, PR, merge recommendation or performance verdict.** No repository branch was modified by this review. The parent issue remains open.

## Exact scope and identities

This session found a live ownership claim for precisely the requested slice: [#125 comment 5813893082](https://github.com/buster14a/buster/issues/125#issuecomment-5813893082). Rather than create a competing allocator implementation, it analyzed the owner's fresh hosted capture and independently exercised the existing region-ordering helpers.

| Identity | Exact value |
| --- | --- |
| Inspected main/base | `5d1c314a8ee3c5f9fb713f6afb85ab1e795f44a0` |
| Base tree | `b1f7f60e49d79b1452349493c3f6cbc2a4acd93d` |
| Baseline QUALITY source blob | `2ce4bfdfb73d07cd9a127812700108403b770de8` |
| Owner's experiment base | `2e942e80a87666409cf29d3e24a68d322b9e71fd` |
| Owner's prototype inspected | `033a0b884a6d45b4489a26f0af948e1b319fd067` |
| Prototype helper source blob | `d0598b6e8b310ece77e17f6bbeda9eafea988da1` |
| Owner's subsequently published implementation | `6644f7a9dd9c9b360091c5c9d7a6eda75183f293` |
| Published QUALITY source blob | `ea8da150b9ac7b82dc2f8554833c3226a2150f4a` |
| Hosted diagnostic run / attempt | `36000528004 / 1` |
| Diagnostic workflow commit | `748d985a30cbe651d142637461c56560cad3b170` |
| Diagnostic artifact ID | `10807458255` |
| Original artifact SHA-256 | `085ce2e62f963059d6f6dafdedc999ba914aff5ee26c04e23026b6c62ab1bc04` |

The original artifact's digest was recomputed after download and matches GitHub metadata. It is preserved unmodified in `raw/`. The diagnostic overlay identifies the same baseline allocator blob as inspected main. The four intervening base-to-main commits change documentation/service policy, not compiler source. The original diagnostic workflow, not this session, built and ran the Clang diagnostic compiler on a standard Ubuntu hosted runner. Its archived compiler digest is `146c8454052824bf35fbfc4fa6a49ee64a0296e9deaf703217f619f1f76eeb0f`.

The published implementation's scanner/comparator/heap function bodies were read back and match the helpers exercised here. **This does not mean the complete implementation commit was built or tested in this session.** The independent translation unit has its own minimal type/counter adapters, not the Buster build or arena implementation. Its exact file/binary identities are in `enumeration-binaries.sha256` and the package manifest.

Prior work is retained: #381 is the landed census, #462 the exact-u64 arithmetic repair, #496 the assigned-value scratch optimization, and #586 research rather than accepted implementation evidence. The historical closure microbenchmark and blocked pin-budget experiment are not reused as performance results. No #36 retirement, deployment, policy, generated-binding or spill-economics work was performed.

## Fresh workload populations

`analyze_populations.py` reads every table, row, selected-region and query record. It rejects duplicate/missing/out-of-range rows, duplicate region IDs, nonpositive traffic, unclosed query scopes and inconsistent counts. It independently joins table occupancy and all logged selections/queries to the corresponding aggregate metrics. Every captured selected-region sequence is the appropriate prefix of `(traffic descending, original region ID ascending)`.

| Capture | Region tables | Candidate rows in those tables | Dense cells | Nonzero cells | Density | Scanner calls | Selected regions | Cells scanned |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Frozen Buster unity, direct SSA | 4,083 | 88,817 | 947,201 | 99,703 | 10.526% | 43,263 | 24,273 | 603,265 |
| Same source, memory form | 4,083 | 88,472 | 947,597 | 99,300 | 10.479% | 42,190 | 23,712 | 600,854 |
| Existing register-pressure fixture | 3 | 4 | 4 | 4 | 100% | 0 | 0 | 0 |

Both unity captures have at most 136 regions, 1,150 candidates and 67 nonzero regions in one candidate row. All recorded baseline edit streams are point-ordered. That observation does not authorize dropping the constructor's runtime fallback for unordered relevant edits.

The distribution matters more than the aggregate density. In direct SSA, 82,592 of 88,817 rows have exactly one nonzero region; 16,510 of 19,169 queried rows are singleton rows. The maximum *queried* row has 44 nonzero regions. The existing pressure fixture exercises neither repeated region enumeration nor a sparse constructor, so its success cannot establish this slice's coverage.

The old source bound survives: repeated selection scans L cells per query, up to K+1 queries for a row with K positive cells, including exhaustion. Most observed queried rows exhaust, but K is usually one rather than L. Therefore the O(CL²) bound is not itself a description of typical observed behavior or evidence of a dominant compiler bottleneck.

### A proved storage bound, not an inferred time threshold

Let C be admitted candidates, L regions, P nonzero candidate-region cells, E all baseline edits, and U baseline spill/reload edits belonging to admitted candidates. The prototype's early gate is:

```text
L > 1 and 16U + 8C + 20 < 8CL.
```

This prices its 16-byte traffic/region pairs, C+1 offsets, C construction cursors and conservative alignment allowance. Actual P is at most U, so allocated typed storage fits the conservative estimate. Arena backing/RSS and complete allocator scratch are separate quantities.

**The capture does not include U.** It includes P and E, giving P <= U <= E. The reader therefore classifies bounds, not fictional exact dispatch:

| Direct-SSA classification | Tables | Dense cells | Scanner-cell visits |
| --- | ---: | ---: | ---: |
| Sparse gate guaranteed from E, provided point order holds | 51 | 637,889 | 447,391 |
| Dense fallback guaranteed from P or L=1 | 3,444 | 80,717 | 36,468 |
| Exact U required | 588 | 228,595 | 119,406 |

Those 51 tables account for **67.34% of dense cells and 74.16% of current scanner-cell visits**. They contain 16,144 actual pairs and 12,920 candidate rows. Their modeled typed-storage upper bound is 362,684 bytes, versus 5,103,112 dense payload bytes. This is a 92.89% modeled reduction **for that table subset only**, not a measured allocation/RSS or compiler-speed result. The corresponding memory-form subset has the same 51-table concentration.

The gate does not guarantee faster construction. In particular, U can be small while E is large; the sparse constructor traverses the entire edit stream twice. This must be priced in complete allocator and compiler A/B runs, including no-query cases. The missing U prevents exact identification of all dispatched tables but does not invalidate the guaranteed 51-table subset.

## Executed independent correctness tests

`region_enumeration_test.c` contains the unchanged baseline scanner and the owner's unchanged comparator/heap bodies, plus independent sparse-scan and elementary insertion-order oracles. It replays all **177,293 captured rows** and compares **all 47,985 recorded region selections**. It then exhausts and partially consumes each heap and rebuilds it from its permuted, dirty contents without restoring the original array.

An additional **800 deterministic generated rows** cover zero/singleton/tiny cases; dense and sparse populations; shuffled physical row layout; heavy ties; costs at and above UINT32_MAX and near the legal maximum per-cell traffic; and full rejection/exhaustion and early-stop/restart behavior. These are helper-domain tests, not claims that every synthetic aggregate is a realizable complete machine function. A deliberate mutable-ranking counterexample confirms why a stored ordering cannot be generalized to changing traffic.

**All three configurations passed 5,258,803 checks each:**

| Helper build | Result |
| --- | --- |
| Clang 17, C11, O2, strict warnings/errors | PASS |
| GCC 14.2, C11, O2, strict warnings/errors | PASS |
| Clang 17, O1, AddressSanitizer + UndefinedBehaviorSanitizer, leak detection enabled, fail-fast | PASS |

The complete result streams are identical. The decision fingerprint is `7ce2e861804f31c0`; it is a test fingerprint, not a cryptographic provenance proof. Full file digests are retained separately. Tests ran in the disposable assistant container, not the user's desktop. No compiler-throughput timing was collected there.

A further 26 deterministic counted-work cases check heap growth through 4,096 entries. Each construction satisfies at most K recorded sift-loop iterations; full popping satisfies at most K floor(log2 K) iterations. At L=K=4096, the old full enumeration visits 16,781,312 dense cells; the heap records 3,687 construction and 38,995 pop sift iterations. At L=4096,K=4, dense enumeration visits 20,480 cells, sparse rescan visits 20 entries, and the heap records 2 construction plus 2 pop iterations. These are unlike primitive operations, **not speed ratios**.

For the actual recorded queries across both unity forms, forcing each ordering alternative over the captured positive rows gives:

| Counted operation model | Count |
| --- | ---: |
| Existing dense scanner cell visits | 1,204,119 |
| Simpler sparse rescan entry visits | 152,366 |
| Lazy heap build entries presented | 48,615 |
| Heap construction + pop sift iterations | 16,521 |
| Lazy elementary insertion-sort comparisons | 21,749 |

This deliberately compares ordering alternatives only; it excludes CSR construction/cleanup and does not simulate the unknown complete dispatch set. These counts cannot select heap versus simpler sparse scan versus sorted row on timing grounds. The singleton-heavy distribution makes the simpler alternative an important control, not a straw man.

## Conditional exact-policy argument

1. **Same incidence and exact arithmetic.** The constructor observes the unchanged baseline SPILL/RELOAD edits, maps subjects through the original pre-heap candidate slots, finds the same inclusive merged region, and adds the same positive instruction weights using the retained u64 policy. Within an ordered stream, region IDs are monotone, so counting and materializing one cell per candidate/region is exact. Unordered relevant edits restore the temporary mark and use the original dense constructor; they are not silently reordered or discarded.
2. **Same rank, not cached eligibility.** Each sparse tuple moves its original region ID together with its traffic. The comparator is exactly descending traffic, ascending ID. It does not use heap position as ID or replace the candidate heap's separate strict-greater tie policy. Ranking data is built before pin attempts and never updated by them.
3. **Rejections remain ordered operations.** Region legality, inclusive budget checks, capacity breaks, register preference, foreclosure, overlap and profitability checks stay at their original sites. Cached prefix construction is rank-independent. A rejected region does not change rank or consume an assignment. Capacity `break` must not be converted to `continue`, and prefiltering eligibility is not equivalent.
4. **Eligibility may change between candidates.** Earlier accepted pins alter budgets, conflicts and plan capacity. The next candidate still performs those checks against current state. For a given candidate, the region walk ends at the first assignment, so there is no later selection following that candidate's assignment mutation. Induction over unchanged candidate heap pops and region order therefore gives the same assignment sequence, conditional on correct construction and unchanged surrounding logic.
5. **Retries and fallback remain unchanged.** Current source only permits splitting in attempt 0. Later degradation restores candidate/pin state without regional enumeration. The heap additionally retains popped tuples in its inactive suffix, so rebuilding a partially consumed row preserves its original tuple multiset. This stronger helper restart property passed here; whole-allocator retry equivalence did not run here.
6. **Ordered edits are a separate acceptance obligation.** Equal ordered assignments should give equal split-entry/store append order and the same stable row-only store ordering before FAST consumes the plan. That is the correctness argument, not an executed complete-placement/artifact result. Dirty arena reuse, target switching, returned-placement ownership and ordered initialized edit fields still require the registered full allocator regressions.

A logged query stop with no final empty selection is not automatically a successful assignment: a plan-capacity break has that shape too. The trace reader distinguishes exhaustion from other stops and does not label every truncated prefix as acceptance.

## Construction, query, update, reset and storage costs

Let q_c be actual calls for candidate c and k_c its nonzero region count. Common prepass, liveness, admission, candidate heap, budget scans and FAST placement costs remain separate.

| Representation | Construction | Region query work | Storage for traffic representation |
| --- | --- | --- | --- |
| Current dense table + scanner | O(CL + E + U log(L+1)) | O(sum q_c L), worst O(CL²) | 8CL bytes |
| Sparse rows + simple scanner control | O(E + L + C + P), using the same ordered two-pass construction | O(sum q_c k_c) | 16P + 8C + 4 plus alignment |
| Existing sparse prototype + lazy heap | Same sparse construction, plus O(k_c) only when a row is first queried | O(sum [k_c + q_c log(k_c+1)]) | Same arrays, in-place heap, no row backup |
| Sparse rows + elementary insertion order control | Same sparse construction, plus O(k_c²) for queried rows | O(1) per query after sorting | Same arrays, in-place ordering |

Construction updates cost O(1) each after monotone region traversal; each of two passes advances the region cursor at most L times. Rejections pop without traffic updates. New function construction initializes C metadata entries and each materialized pair; no stale pair is read before initialization. An arbitrary repeated helper enumeration rebuilds in O(k_c); production's later attempts do not enumerate regions. The representation adds no per-entry destructor. Existing arena teardown/backing-memory behavior was not measured here and is not described as free.

For accepted sparse tables, P <= min(CL,U), and the gate keeps the conservative typed-storage bound below dense payload. C remains capped at 4096. A rejected order check costs an extra O(C+E+L) bounded prepass before rollback and the old construction. **The hybrid's overall worst-case selection remains O(CL²): dense or unordered tables deliberately keep the old scanner.** Only the admitted sparse branch has the improved representation-local bound; this is not a global worst-case elimination claim.

## Gates, limitations and publication

| Gate | Status in this session |
| --- | --- |
| Capture ZIP digest and all census joins | PASS |
| All captured region-selection prefixes against three alternatives | PASS |
| Extracted C heap/scanner generated, dirty-heap and counted-growth tests | PASS, three builds |
| Existing owner's sparse construction / actual arena regression | NOT RUN |
| Full candidate heap decisions, placements, initialized ordered edits, encoded bytes | NOT RUN |
| Complete registered Buster, target/mode, sanitizer, self-host CI on implementation SHA | NOT RUN |
| Matched trusted-Clang whole-allocator/compiler timings including setup and cleanup | NOT RUN |
| Authorized benchpress 9700X, PMU, peak RSS, measured timing crossover | NOT RUN |
| Local repository clone | FAIL: `Could not resolve host: github.com` |
| New production branch, issue comment, PR, merge or issue closure | NOT PERFORMED |

The existing hosted capture is diagnostic only. It archives output hashes, not the emitted objects or a baseline/candidate pair; artifact identity cannot be recomputed from absent objects. It is not whole-compiler equivalence evidence. The earlier hosted screening run `35999334636` remains failed; this packet uses only the separately successful `36000528004` capture and does not reclassify the earlier attempt.

GitHub reads and artifact retrieval succeeded. The exposed connector actions in this session are read-only, and the local Git network attempt failed. No write/dispatch execution was available here. The owner published implementation `6644f7a9...` while this review ran; that branch was left untouched and no second implementation was produced. No PR was found for that head at the scoped final search; this is a time-bounded observation, not an exhaustive repository inventory.

**Next acceptance decision:** compare the owner's complete implementation with untouched current baseline and the simpler sparse-row scan control using the existing infrastructure. Retain tiny/dense/no-query and large-E/small-U controls, exact assignment/placement/ordered-edit/output comparisons, full cleanup and retained memory. Choose a timing crossover only from those matched results. The present evidence supports continued evaluation of the selective sparse representation; it neither proves a speedup nor supplies an evidence-backed rejection of it.

## Reproduction

From the unpacked packet, run `./reproduce.sh`. It verifies the original ZIP hash, regenerates the joined replay, compiles all three independent helper builds, runs them with strict sanitizer settings and compares their complete output. Python, Clang and GCC are required. It creates only a temporary directory and does not modify a repository or contact a runner.

Primary immutable source: [baseline QUALITY](https://github.com/buster14a/buster/blob/5d1c314a8ee3c5f9fb713f6afb85ab1e795f44a0/src/buster/lib/compiler/codegen/register_allocator_quality.c), [owner's published implementation](https://github.com/buster14a/buster/blob/6644f7a9dd9c9b360091c5c9d7a6eda75183f293/src/buster/lib/compiler/codegen/register_allocator_quality.c), [registered prototype fixtures](https://github.com/buster14a/buster/blob/6644f7a9dd9c9b360091c5c9d7a6eda75183f293/src/buster/tests/compiler/codegen/quality_regions_test_internal.h), and [hosted capture](https://github.com/buster14a/buster/actions/runs/36000528004).
