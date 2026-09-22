# Frontend discovery: normalized switch interval proof, and its materiality limit

Investigated source: `ec1897b900c694e57a10ee5ddaacd89c4cd7c57c`, tree `1e20ddfb534ba16a05a277136ff4f391a93dc28f`.
Repository instructions, frontend and semantic-validation guides, research lifecycle, and newest audit `2026-09-22T023624Z.md` read. Newest audit is a desktop observer control and provides no relevant current compiler hotspot evidence.

Source blob identities:

| File | Git blob |
|---|---|
| `src/buster/lib/compiler/frontend/c/c_parse.c` | `059ce8fc9e613515d9f985d5b506a0ec5bdff067` |
| `src/buster/lib/compiler/frontend/c/c_gen.c` | `60bf69ed970fc43d84fa3f7198aac5d1410b0eb9` |
| `src/buster/lib/compiler/frontend/c/c.h` | `80eeb4129249d893e4a5a5b179b710e88e061bc8` |

## Decision

Retain one frontend candidate: **normalize switch-case intervals on insertion and prove an incoming interval disjoint from the entire prior population through its extrema before considering pairwise comparisons**. This is a small scalar work-eliminating algorithm; wider SIMD is a competitor for its unresolved subset, not its premise.

The first independently useful experiment should touch only the semantic verifier's existing temporary endpoint arrays. A sorted disjointness certificate is a stronger asymptotic competitor for sufficiently large irregular populations. A persistent semantic-to-lowering plan is a higher-risk follow-up. Do not present any of these as a measured compiler speedup.

The static source census below makes this a bounded research opportunity rather than a plausible large self-host speedup today. Compile-time population and exclusive time remain unknown.

## Current computation and routes

Pinned anchors use `https://github.com/buster14a/buster/blob/ec1897b900c694e57a10ee5ddaacd89c4cd7c57c/` plus the paths/lines below.

- `c_parse.c:20058`: `c_parse_validate_one_switch`; it resolves and promotes the controlling C type, requires an integer size from 1 through 8 bytes, allocates `lows`/`highs` arrays with capacity based on switch token extent, walks source cases while excluding nested switches, evaluates lower/upper constants, truncates to controlling width, rejects reversed ranges, and compares every accepted interval against all earlier accepted intervals.
- `c_parse.c:20238–20256`: the exact insertion/overlap kernel. Inclusive overlap is `(low ^ sign_bit) <= (highs[p] ^ sign_bit) && (lows[p] ^ sign_bit) <= (high ^ sign_bit)`. A singleton is an interval. On overlap the semantic pass reports the current case token and still appends it; invalid nonconstant/reversed ranges are not appended.
- `c_parse.c:20264`: `c_parse_validate_switch_duplicates` scans every function token looking for switch and calls the above.
- `c_parse.c:21635–21770`: shared semantic constraints call the verifier for every function body.
- `driver.c:3571` uses `c_analyze_semantics_only` for syntax-only; `driver.c:3588` uses `c_analyze_with_options` for ordinary C compilation. The latter calls the same semantic validation before lowering. Native and direct nonnative routes therefore share this frontend work when their source reaches it. Pure preprocessing and assembly input do not.
- `c_gen.c:37329–37521` independently discovers cases and folds constants for lowering; `36889–36924` converts them to the promoted canonical controlling type; `36927–36950` repeats pairwise overlap checking and returns at the earliest offending current case. `33938` emits plain switches; `33999` handles range dispatch. Both are real consumers of the same underlying interval-disjointness question, but they are not currently one shared representation.
- `c_gen.c:33189`: `CIrSwitchCase` contains source ranges, two full constants, endpoints, CFG block, and flags. A SIMD benchmark using freely prepacked endpoints understates conversion cost for this lowering array.
- `c_parse.c:18221`: diagnostic selection replaces an existing diagnostic only for a **strictly lower** order token. Equal-token ties keep the earlier validator's result.

Let `n` be accepted case intervals in one switch, excluding default. Valid switches make `P=n(n-1)/2` prior-pair probes per verifier. This is a source operation count, not executed instructions. The current short-circuit matters: in ascending disjoint order, the first comparison is false and the second endpoint load/comparison may never execute. Logical endpoint read demand is between about `8P` and `16P` bytes depending on the compiled short-circuit path; it is not DRAM traffic and should be checked in assembly. Inputs are typically small and source order may make branches very predictable. These are the strongest reasons the incumbent can already be inexpensive.

## Small first transformation: online extrema proof

Normalize only after all existing constant/width/range checks. For target width `w`, let `M` be its unsigned width mask and `S` its signed-order bit, or zero for unsigned. Store `L=(low & M)^S`, `H=(high & M)^S`; both are `u64`, ordered by unsigned comparison. The current arrays are used only for overlap checking, so their representation can change without reconstructing values for another semantic consumer. Lowering's array is a separate follow-up.

Maintain `minimum_low` and `maximum_high` of inserted intervals. An interval with `H < minimum_low` or `L > maximum_high` cannot overlap any earlier interval. This is a sufficient certificate, not a necessary condition. An interval inside a gap of the enclosing range goes through the exact incumbent comparison. Update extrema and append the normalized pair whether the existing semantic diagnostic discovered overlap or not, preserving the current scan's control and error selection.

```c
/* called after existing validation; all pairs satisfy low <= high */
u64 low_ordered = (low & value_mask) ^ sign_bit;
u64 high_ordered = (high & value_mask) ^ sign_bit;
bool outside = value_count == 0 || high_ordered < minimum_low || low_ordered > maximum_high;
if (!outside) {
    for (u32 previous = 0; previous < value_count; ++previous) {
        if (low_ordered <= highs[previous] && lows[previous] <= high_ordered) {
            consider_existing_overlap_diagnostic(current_case_token);
            break;
        }
    }
}
/* First insertion initializes instead of consulting sentinel extrema. */
if (value_count == 0) {
    minimum_low = low_ordered;
    maximum_high = high_ordered;
} else {
    minimum_low = minimum_low < low_ordered ? minimum_low : low_ordered;
    maximum_high = maximum_high > high_ordered ? maximum_high : high_ordered;
}
lows[value_count] = low_ordered;
highs[value_count] = high_ordered;
++value_count;
```

This pseudocode deliberately leaves constant evaluation, diagnostics and append timing in place. It adds two scalar state words, no selection vector, no second pass, no sorting, no new allocation, and no full-lane staging. Ascending, descending, and alternating outward-extending interval sequences become O(n); shuffled/interior-gap sequences remain O(n²). The extrema recurrences limit independent case concurrency, so there is no reason to seek four wide arithmetic chains for this algorithm. Dependency shortening comes from deleting all prior-pair probes on successful certificates.

The first ablation is normalize-only. The second adds extrema. A third runs a vector prior-pair scan only for inconclusive cases. Do not attribute the combined gain to AVX-512. For small `n`, the guard/extrema update may cost more than a few predictable comparisons; test direct incumbent below a measured threshold rather than assume SIMD or the certificate always wins.

## Full-cost model and falsification

Let `P_f` be residual pair probes after the enclosing-range proof; `a` effective cost per incumbent pair in that workload/assembly, `h` per-record proof/update cost, and `d` setup/dispatch difference. A serial accounting approximation is profitable when `a(P-P_f) > h(n-1)+d`. This is an empirical break-even inequality, not a claim that each component executes serially; normalization, loads, compares and loop bookkeeping overlap. Build a resource bound as the maximum of relevant load bandwidth, compare/branch throughput, front-end demand and dependency chain bound. Required exact instruction numbers belong to the hardware agent's measured forms, not guessed four-wide division.

The optimistic lower bound for successful extrema sequences is one producer pass with endpoint normalization, extrema dependence, and existing stores; no prior endpoint arrays are read. Realistic costs include branch/conditional-move instructions, live extra scalar registers, unchanged token scanning and constant folding, and residual scans. Cache-line demand saved is bounded by the previous arrays' actual residency; repeated probes often hit L1. Proposed extra retained memory is zero for the first slice. Source distribution of signedness, order, width, range density and query size is not yet measured.

Falsifiers:

1. Current executed census finds almost no pairwise cost, making any guarded kernel irrelevant end to end.
2. Most source order places intervals inside the current enclosing range, so proof overhead is paid without suppressing probes.
3. Assembly already recognizes a stronger simplification, or extra extrema state causes harmful spills in the larger semantic function.
4. Full compile paired timing is indistinguishable from A/A despite a microbenchmark gain.
5. Any output/diagnostic mismatch, including earliest token and same-token priority.

Stop after a negative current-corpus result. A generated switch with thousands of cases can establish scaling but cannot manufacture an ordinary self-host claim.

## Strong competing algorithms

1. **Incumbent and normalize-only scalar.** Required baselines. An auto-vectorized baseline must be identified by disassembly.
2. **Online enclosing-range proof.** Best small implementation candidate; preserves order and requires no staging.
3. **Sorted Boolean certificate.** First try ascending/descending adjacent separation directly. If inconclusive, copy normalized pairs and sort by low, then test every adjacent pair for `high[i-1] < low[i]`. With sorted lows and individually valid intervals this certifies all-pairs disjointness; no prefix-maximum recurrence is needed. Any overlap makes the certificate fail. Replay the original source-order pair scan only on failure to recover exact diagnostics. Keep original arrays untouched. A direct iterative C sort avoids forbidden callback/recursive production machinery. General cost O(n log n), temporary pair traffic and sorting setup; invalid input still has quadratic cold replay unless improved separately. This is useful only above measured break-even. Uniform branchless SIMD triangular work has worse asymptotics.
4. **AVX2, 256-bit EVEX, 512-bit EVEX prior-pair comparisons.** Four or eight u64 lanes compare one broadcast interval against contiguous previous `L`/`H` arrays. Use exact unsigned comparisons or correct AVX2 sign bias. AND overlap predicates and reduce to any; no witness-lane extraction is necessary when the diagnostic names only the new interval. Mask lifetime is one comparison tile; OR independent tile masks and test once per modest unroll if delayed exit is cheap. Keep old source order between incoming records. Loading lows unconditionally may lose against incumbent short-circuit, especially on ascending disjoint input; charge it. For lowering, charge extracting endpoints from `CIrSwitchCase`.
5. **Interval tree / coordinate compression.** Strong asymptotic alternatives for enormous arbitrary order, but pointer traffic, balancing, sorting/build costs and tiny actual populations are a poor fit until the census changes. A min/max block hierarchy can reduce residual comparisons but adds build/update state; do not fold it into the first slice.

## Semantic and safety contract

- Intervals are inclusive. Equality at an endpoint is overlap. Do not replace comparison by `high+1` because `UINT64_MAX` is legal.
- Width/sign normalization follows promoted controlling type and existing truncation, not host signed arithmetic. Avoid shifts by 64; preserve the existing size check and mask formula.
- Defaults and invalid/reversed/nonconstant ranges never enter the normalized population. Nested switches own their own cases; a token-level unqualified collector would be wrong.
- Const evaluation and source diagnostics stay ordered, including GNU dialect refusal and unsupported nested-case checks. A certificate does not authorize speculative evaluation of invalid expressions.
- Empty/singleton cases initialize extrema explicitly. Empty arrays are not dereferenced. SIMD tails use in-bounds scalar cleanup or exact masked loads; masked-off lanes must not be represented by invalid C pointer arithmetic. No gather or masked arithmetic is needed.
- Original source arrays remain available for sorted failure replay, and indices refer to source tokens rather than compact positions. Do not modify ordinary case/CFG order when sorting a temporary proof copy.
- Separate arrays in the standalone experiment are non-overlapping by contract; production arena ownership must guarantee no alias with token/output state. Unaligned loads are valid; exact page-end tails need explicit tests.
- Current bitcode/diagnostic results are preserved. There is no generated-program optimization: switch dispatch order and immediate values remain unchanged.

## Speculative second stage: semantic switch plan

Current normal object path recomputes constants, normalization and all-pairs overlap after semantic validation. A C-owned immutable per-switch normalized plan could carry endpoint values, label tokens and a successful disjointness certificate into lowering. Its key must include token-stream identity/range, target and promoted controlling width/sign. It must not contain canonical IDs or become another frontend IR. Keep CFG/body ownership reconstruction in lowering initially.

**Crucial negative finding:** `CParseResult.analysis_complete` is also set by legacy `c_analyze_semantics(validate_lowering_constraints=false)`, not just semantic-only validation (`c_parse.c:22521–22543`). It is not a valid certificate for bypassing lowering's defensive checks. Direct `c_parse -> c_lower_to_ir` users and tests require fallback. Tests in `src/buster/tests/compiler/frontend/c/c_test.c:1711`, `11580–11791` explicitly exercise direct lowering and malformed/range cases. Use explicit proof presence, not a broad analysis bit.

Sharing constant results has more risk than sharing Boolean interval validity: parser `CParseConstant` and lowering `CIrConstantValue` have different type/conversion paths, and full-width source constants can truncate to admitted <=64-bit switch values. Agreement must be proved with signed promotion, high-bit immediates, enum values, `-1` versus wide positive constants, both direct SSA forms, and target data models. Defensive checks also detect lowering conversion defects. Start with the independent verifier improvement; adopt the plan only after current executed duplication is material and differential tests cover the stronger contract.

## Static source census (completed; not execution)

Artifacts are in `/workspace/scratch/9578d32a4819/`:

- `switch_static_census.py`: reusable lexical extractor.
- `switch_static_census.json`: compiler `.c` subset.
- `switch_static_lib_c.json`: whole production library `.c`, including compiler subset.
- `switch_static_lib_h.json`: production library headers including generated headers, separate from `.c`.
- `switch_static_entry_c.json`: non-library, non-test application/example `.c` sources; one switch is in `src/buster/apps/ide/ide.c:1219`.

Every JSON includes exact source-file SHA-256 identities, source locations, extracted case expressions and lexical anomalies. Reproduce with `python3 switch_static_census.py <pinned-repo> <output.json> compiler-c|lib-c|lib-h|entry-c`.

| Disjoint file population | Files | Explicit switches | Explicit cases | p50 / p90 / p99 / max cases | Sum n(n-1)/2 |
|---|---:|---:|---:|---|---:|
| Entire production library `.c` | 83 | 357 | 5,154 | 10 / 27 / 78 / 167 | 94,978 |
| Production library `.h`, including generated | 110 | 0 | 0 | — | 0 |
| Application/example `.c` outside lib/tests | 2 | 1 | 8 | 8 / 8 / 8 / 8 | 28 |
| Compiler `.c` subset, **do not add to lib total** | 52 | 306 | 4,269 | 10 / 27 / 51 / 156 | 60,444 |

No GNU ranges occur in the counted production labels; only 374 of the library's 5,154 labels are single numeric literals. Enum/macro-expression values and ordering are unresolved. Largest explicit library switches: `window/xcb.c:1991` 167 cases, `compiler/assembly/assembly.c:13339` 156, `target.c:1911` 147, `compiler/codegen/machine_aarch64.c:9055` 130. Some are platform-specific and not compiled together.

This is a lexical census excluding strings/comments/character literals, counting only explicit braced switch bodies and assigning nested cases to the innermost braced switch. It retains mutually exclusive preprocessor branches, omits macro-generated cases, omits unbraced switch cases, does not expand includes, and does not resolve numeric label values. Two unmatched lexical braces in `jit.c`/`link.c` are retained as anomaly records rather than silently claimed parsed. Embedded regression C source strings are excluded. Therefore no full-TU population, active-route case count, or dynamic pair count is asserted. Doubling 94,978 to estimate the two verifier routes is only a hypothetical source count and should not appear as measured work.

The modest census is evidence against promising a substantial self-host compiler gain before a preprocessed/executed census. It is not proof the path can never be hot on external generated interpreters such as SQLite; SQLite's existing lowerer comment explains nested labels but no current external case population was measured here.

## Novelty sweep and rejected frontend ideas

Read issue/PR bodies from the shared two novelty snapshots, including #587 (operator summaries, ordered call-predicate batches, type-pair frontiers), #248 (cold diagnostic allocation), #259 (constexpr universe clearing), and searched current repository documents. Live all-state searches included `switch overlap`, `case quadratic`, `switch certificate`, `in:title switch`, and PR `switch` (bounded top-30). They found correctness/other-layer work such as #263/#316 bitcode target rotation, #353 high-bit unsigned switch immediates, #924 FAST predecessor switch handling, and #296 parameter-edge indexing, with no switch interval-proof performance owner identified. Exact bounded searches cannot establish worldwide novelty or absence of differently named work. This is a new-to-Buster candidate, using standard interval bounding/sorted-separation mechanisms.

Rejected: generic semantic absence summaries. `c_parse_validate_assembly` currently scans all identifiers looking for three asm spellings and `c_parse_validate_labels` rescans whole bodies, suggesting a feature summary. However current `C_PARSE_DECLARATION_RANGE_KEYWORDS` (`c_parse.c:328`) already uses rare-feature range summaries to eliminate scans. `CTokenPositionIndex` (`c.h:1209`) already stores label candidates/attributes and matching delimiters, and its SIMD tile producer is in `c_parse.c:441`. Extending these to semantic validators is an incremental application of an established mechanism, not an original discovery for this packet. It may be useful ordinary engineering but fails the novelty goal.

Also rejected: promoted-member caching/flattening without new evidence, because current code explicitly documents prior per-query universe clearing elimination, local frontiers and compact initializer slot projections; numeric live-limb/decimal work overlaps excluded #561; type compatibility batching overlaps #587; parser token scan summaries overlap previous research. No quota was filled with these.

## Evidence and next gate

This subtask performed source inspection, bounded connected-GitHub novelty searches, and reproducible static census only. No Buster build, compiler correctness run, Zen 5 measurement, performance acceptance, publication, or production edit occurred here. A separate reproducer agent owns isolated equivalence/assembly work; do not attribute its results to this note until read.

Recommended first actual measurement: diagnostic-only current Buster counts at the semantic interval insertion (`n`, pair probes, enclosing-range successes, normalized order and signedness) over frozen self-host and representative external preprocessed inputs, accompanied by exclusive phase time. Then paired uninstrumented first-slice normalization+extrema versus incumbent, with smallest lengths emphasized and a fresh held-out run. A/A uncertainty bounds must come from benchpress; no whole-compiler percentage is estimated here. An eventual issue should be `status/needs-census` until incidence/materiality is known, even if the isolated mathematical kernel passes equivalence tests.
