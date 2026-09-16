# SIMD lookup and type-query research: preserve identity, batch independent reads

## Status, decision, and provenance

Research date: 2026-09-13. This is a source-grounded design and experiment contract, **not a new performance audit, an implementation, or a measured speedup**. It changes no compiler source, test registration, CI configuration, dependency, or canonical representation.

Analyzed main commit: `5865fa7add27908656aedf28a3c0e6a3de47dc04`.
Analyzed tree: `950eae4171f20ece45f1092fbc311c950665ed5b`.
At the publication recheck, main was `5217113aa33a74cd78524c29bd8addfc0e219ef0`, tree `a1a85005b5e6ebc48b5af6cfb66a9b31cc73ec8d`. The compare contained only the two Vulkan-installer removals and `.github/workflows/ci.yml`; none of the analyzed compiler files or guides changed. The report and its source links deliberately retain the original frozen SHA.

**Decision:** the highest-value bounded batching experiment identified by this inspection is the read side of `c_symbols_intern_tokens`, retaining `c_symbol_intern` as the ordered insertion authority. Start with several independent scalar queries against a frozen table, then commit misses in the existing token order. Do not replace the table, add a global type cache, or make an AVX-512 gather implementation the prerequisite.

This selection reflects an available independent-query boundary, stable identities, and a small integration surface. It is **not a measured ranking of current compiler time**. Current-main interning time, hit rate, probe distribution, and within-window duplication remain unmeasured. Collection is the first implementation gate; rejection before adding a kernel is a valid outcome.

The governing sources were [AGENTS](../AGENTS.md), [SIMD guidance](agents/simd.md), the [frontend index](agents/frontend.md), [frontend foundations](agents/frontend/foundations.md), [layout guidance](agents/frontend/layout.md), [benchmarking](agents/benchmarking.md), [forge workflow](agents/workflow.md), [the native throughput harness](../tools/throughput/README.md), and the newest timestamped audit found at the analyzed revision, [2026-09-12T224432Z](performance-audits/2026-09-12T224432Z.md). This document belongs among design notes, not among timestamped reports claiming newly collected measurements. Historical audits and their index are unchanged.

### Evidence ledger

| Evidence class | What is established | What is not established |
| --- | --- | --- |
| Current source inspection | Existing layouts, callers, growth policy, identity rules, fast paths, and remaining scalar probe loop, at the SHA above | Query frequencies, cache misses, CPU cycles, or an end-to-end speedup |
| Retained measurements | The two historical audits summarized below, with their own sources, producers, hosts, and workloads | Current-main measurements or physical Zen 5 acceptance |
| Derived/modelled | Byte counts from specified layouts, lane counts, conditional cost equations, and the ordered-commit proof | Actual hardware gather latency, achieved memory-level parallelism, or workload distributions |
| Unavailable/unrun | New query census, prototype timings, PMU measurements, sanitizers, self-hosting, and platform tests | None of these gates is represented as passed |

No compiler, benchmark, or generated program was executed locally. No remote or CI experiment was dispatched. Obtaining the missing interning census requires a separately authorized instrumentation implementation; this research-only change does not install one. Existing CI is not changed to manufacture measurement access.

## 1. What the compiler already knows

The following are revision-pinned source references, not historical line-number guesses:

| Source | Inspected symbols / contract |
| --- | --- |
| [c.h: token representation][token-source] | `CToken`, `CTokenShape`, `C_TOKEN_LENGTH_OVERSIZED`; the token already carries `symbol`, with zero as the synthesized/test-token fallback |
| [c_internal.h: symbol table][symbol-table] | `CSymbolTable`, dense `builtin_kinds`, `class_bits`, `word_bits`, and stable well-known IDs |
| [c_source.c: projected key and interner][intern-source] | `CSymbolSlot`, `CSymbolKey`, `c_symbol_key`, `c_symbol_slot_hash`, `c_symbol_middle_equal`, `c_symbol_intern` |
| [c_source.c: seed order and run caller][intern-run] | `c_symbol_table_create`, `c_symbols_intern_tokens`, existing 64-token shape filtering |
| [c_source.c: a builtin setup caller][builtin-caller] | Existing calls to `c_symbols_intern_tokens` before macro-definition consumption |
| [c_source.c: directive consumers][directive-source] | `c_macro_find_token` at conditional directives; macro state and source-frame ordering are observable |
| [c_parse.c: bindings and visibility][binding-source] | `c_parse_scope_add_entity`, `c_parse_lookup_entity_symbol`, `c_parse_lookup_entity_chain`, `c_parse_lookup_entity_in_scope`, `c_parse_lookup_entity_at_symbol` |
| [c_parse.c: tag index and type append][tag-source] | `c_parse_aggregate_lookup_slot`, `c_parse_aggregate_lookup_grow`, `c_parse_aggregate_lookup_insert`, `c_parse_add_type`, array-bound inference |
| [c_parse.c: compatibility, first half][compat-a] and [second half][compat-b] | `CTypePair`, `c_parse_types_compatible`, parameter adjustments, bound evaluation and aggregate identity handling |
| [c_parse.c: target layout helpers][layout-helpers] and [layout cache/solver][layout-source] | `c_parse_builtin_type_layout`, `c_atomic_promoted_layout`, `CParseLayoutContext`, committed cache lookup and pending-type snapshot |
| [simd.h][simd-source] | Existing SIMD feature tiers, masks, byte/word operations, self-hosted builtins and scalar fallbacks |

### Interning is already a compact projected-key lookup

A `CSymbolSlot` contains three `u64` words: `low`, `high`, and `length_and_id`. The last packs `(length << 32) | id`; zero is empty. `CSymbolKey` itself contains only the two key words; length is supplied separately. The first and last eight bytes cover every byte of a spelling of at most sixteen bytes. Longer spellings must additionally pass `c_symbol_middle_equal` over the middle. The bounded short-key reads and overlapping middle-tail reads must remain bounded; spelling buffers do not promise arbitrary padding. [Source][intern-source]

`c_symbol_slot_hash` mixes the two projected words and length with integer multiplication and folding. It is not the separate `c_macro_name_hash` used by other tables. An important unfavorable distribution is therefore names with identical length, first eight bytes, and last eight bytes but different middles: they have identical projected hashes, not merely identical small fingerprints. A fingerprint derived from that same hash cannot distinguish them.

The table uses linear probing without tombstones. Hits do not grow either array. A unique insertion doubles the names array at its existing threshold; the probe array doubles when the next symbol would exceed half occupancy. Rehashing preserves IDs. The new ID is `count + 1`, and the stored name is the first occurrence's `String8`. These are stronger requirements than simply returning equal spellings from a different container. [Source][intern-source]

The initial probe capacity is 16,384 slots; at 24 bytes per slot this is **384 KiB of slot storage**, a layout calculation rather than RSS. Initial name capacity is 4,096 entries: **64 KiB assuming the inspected 64-bit `String8` pointer/length layout**. The three predefined-ID fact arrays occupy 1 KiB at capacity 256. Include array growth and retained arena allocations in any footprint comparison, not only live symbols. [Creation and table fields][intern-run]

`c_symbols_intern_tokens` already selects identifiers with a masked 64-byte shape load and comparison. It then drains the identifier mask in ascending token order and calls the scalar interner once per selected token. A missing shape sidecar selects the row-based reference loop. Widening the classification scan again would not vectorize the dependent probes that remain. Comments describing historical identifier frequency or short-name frequency are not a current measured census. [Source][intern-run]

### Several proposed lookup optimizations are already fast paths

The parser's current-scope binding array answers eligible `c_parse_lookup_entity_symbol` requests with a single indexed load. Chains remain the authority outside that scope and for visibility-sensitive queries. `c_parse_lookup_entity_at_symbol` also checks the declaration's token position; `(symbol)` alone is not its semantic key. Dense builtin/class/word facts and `CToken.symbol` similarly avoid repeated spelling queries. Batching these direct loads by replacing them with a hash table would add work. [Bindings][binding-source] [Symbol facts][symbol-table]

Type handling is not one universal canonicalization table. `c_parse_add_type` reserves and appends a `CType`, then updates the aggregate-tag index. Compatible types need not share a frontend or canonical type ID. The aggregate index already grows geometrically at half occupancy, with separate handling for multiple scoped tags, stale/reused IDs, qualified aliases, and allocation refusal. Do not reimplement the former fixed-capacity saturation repair. [Type append/index][tag-source]

`c_parse_type_layout` already checks a machine-local committed cache when its token-stream identity matches, then returns stored size/alignment for a resolved requested ID. Its slow path snapshots pending IDs because solving can reenter parsing and grow the type table. It distinguishes provisional results from committed facts. A proposed cache must account for this existing work before claiming to remove repeated whole-table computation. [Layout solver][layout-source]

## 2. Query census: known structure, missing distributions, exact collection contract

This is a **source census plus an unrun empirical-census specification**. No current observed query count, hit/miss percentage, percentile, duplicate rate, locality distribution, or mutation frequency is supplied where evidence is absent.

| Family | Key / value and existing access | Mutation / ordering boundary | Empirical evidence still needed |
| --- | --- | --- | --- |
| Identifier interning | Exact spelling bytes; projected first/last eight bytes plus length; stable `u32` ID; 24-byte open-addressed slots | Append-only names and IDs; growth only on new insertion; sequential call order matters | Length histogram, snapshot hits/misses, unique inserts, probes, projection collisions, middle bytes read, growth and duplication per existing run/window |
| Ordinary entity lookup | Symbol or spelling, requested scope; entity ID; binding array or chains | Bind, shadow, unwind, enclosing-scope insertion, speculative rollback | Direct-load eligibility, chain visits, scope hops, hit/miss by caller, same-key requests within an unchanged binding state |
| Position-sensitive lookup | Above plus token visibility position and query kind | Declaration visibility and scope mutation | Repeated exact semantic keys, not just repeated names |
| Macro lookup | Interned ID where available; current definition/disabled state | Define/undefine, nested expansion state, synthesized respelling, source-frame progression | Interned-vs-fallback requests and repeated facts within unchanged macro state |
| Aggregate-tag index | Tag spelling, kind; candidate identity plus ambiguity/staleness handling | Scope changes, completion, rollback, ID reuse, index growth | Actual tags/population, probes, ambiguity and stale fallbacks, rehash visits; existing synthetic counters are reusable |
| Type compatibility | Ordered type pair plus qualifier-adjustment mode and all semantic context read by the comparison | Bound evaluation can reenter semantic work; type completion and rollback | Pair visits vs distinct pairs, repeated subgraphs, scratch bytes, bound-evaluation work and side effects |
| Layout | Requested type, target/layout policy and its dependency state; size/alignment and provenance of the answer | Incomplete types, inferred bounds, attributes, provisional dependencies, rollback | Direct/builtin/committed hits, misses, pending visits, copied bytes, repeated unresolved requests and causes |
| Fixed metadata / classifications | Existing stable IDs and dense facts | Initialization or explicitly documented invalidation | Remaining spelling fallbacks and repeated derivation despite an available upstream fact |

### Counting definitions for the selected family

Use the existing diagnostic-build and source-metrics conventions, with counters compiled out of ordinary producers. Do not time an instrumented binary. For each caller/run, record `Q` accepted identifier requests, `U` unique insertions, table capacity/count before and after, and the following:

- Length buckets: 0, 1-3, 4-7, 8, 9-15, 16, 17-31, 32-63, 64-255, 256-1023, and larger admitted identifiers; retain exact maximum. Zero is a diagnostic control, not an assumed lexical identifier.
- Probe histogram for hit, miss and rehash separately: 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, and larger; record sums and maxima. A probe is one examined slot including the terminating empty slot. A rehash visit is not a source query.
- Reject counts at occupancy, length, low-word, high-word, and exact-middle checks; count requested comparison bytes separately from measured cache-line traffic.
- For B = 4, 8, 16 inside the actual 64-token windows, record filled lanes, distinct exact keys, keys already present at batch entry, duplicate-new keys, and unique first destination indices. `Q-U` is not the snapshot hit count: repeated new keys inside a batch were all absent from its initial snapshot.
- Record the distance to prior identical query within a run, bounded reuse-distance buckets for symbol IDs, distinct slot/cache-line indices per batch, and consecutive probe-cluster lengths. Instrumentation addresses describe logical locality; they do not establish L1/L2/DRAM misses.
- Record every growth, slots scanned/reinserted, explicit zero/copy bytes, total allocated and retained capacities, peak live scratch, and time spent outside the lookup helper in the ordinary producer.

Counter-only builds must preserve the original lookup result, ID sequence, spelling references and outputs. Prefer aggregate histograms, not an unlimited log of every token. A bounded deterministic diagnostic sample may retain exact query ordinals for independent replay; count overflow or sampling omissions must be explicit. Existing `-fsource-metrics` integration and `BUSTER_BENCH_ALLOCATIONS` are extension points, not proof that these new counters already exist. Coordinate with the active metrics work in [PR #576](https://github.com/buster14a/buster/pull/576), rather than racing its schema edits.

### Retained measurements that can and cannot guide selection

The [newest audit](performance-audits/2026-09-12T224432Z.md) compiled frozen source `5dab18af0208cfa7fa65673b6884d5c253fda85b` under a shared four-vCPU Emerald Rapids KVM guest. Its Clang 18.1.3 `x86-64-v3` Callgrind producer could not measure the native AVX-512 kernels. It reports 3,364,471 preprocessed tokens and 13,749,246,043 baseline Callgrind Ir. Its 8.6% canonical-validator share is neither current-main wall time nor an interning cost. The audit's memory-fill reduction and repeated RSS measurements concern different changes; no fraction of those gains belongs to this design.

The [tag-growth audit](performance-audits/2026-09-12T132557Z.md) has useful production-path work counters. At 8,193 synthetic tags its cumulative candidate totals were 69,007 probes, 16,384 rehash-slot visits and zero fallback type visits. At 16,385 they were 179,172, 49,152 and zero. These cumulative workload counts are not per-query distributions. Its 12,000-tag frontend timing fell from 2435.259 to 201.671 ms on a shared host; this demonstrates the scale of removing saturation scans, not the benefit of SIMD lookup or a general physical-Zen-5 result.

The [September 12 throughput-priority synthesis](compiler-throughput-priorities-2026-09-12.md) also distinguishes older native sampling from accepted A/B results. None of these records supplies the missing interning share. Measure that share before spending effort on vector instructions.

## 3. Primary designs and what transfers

[Abseil's Swiss-table design notes][swiss] describe a dense byte of state/fingerprint metadata per slot, SIMD candidate filtering, and exact equality for survivors. Empty and deleted states have different stopping behavior. Transfer the separation of cheap candidate rejection from exact identity, not the C++ container or its capacity policy. Buster's append-only interner has no deletion requirement and already has a strong short-key projection.

[Folly's F14 design][f14] describes chunk-level filtering and overflow tracking, with storage variants including an indexed dense-value representation. Its implementation also exposes prehash/prefetch patterns for independent requests. Transfer the question of independent queries in flight and the accounting of metadata versus payload locality. Chunk capacity, overflow machinery, allocation, and ownership cannot simply be copied into Buster's table.

Böther, Benson, Klimovic and Rabl's [*Analyzing Vectorized Hash Tables Across CPU Architectures*, PVLDB 16(11), 2023][vldb] compares vectorized linear probing, fingerprinting, and bucket-based designs across architectures. Its fixed-size experiments and its Figure 1 make the important separation between SIMD over adjacent slots and several queries proceeding independently. They do not establish the best width, growth behavior, insertion order, or compiler benefit here. Use the alternatives as experimental controls, not imported speedup numbers.

[Intel's AVX-512 description][intel-avx] and [conflict-detection discussion][intel-conflict] distinguish masked vector operations from detection of earlier equal destination indices. Conflict detection can help partition a set of lane operations; it does not prove exact semantic key equality, resolve later probe collisions, perform growth, or select Buster's first-occurrence ID. The first design below deliberately requires no scatter or conflict-detection instruction.

## 4. Implementation-ready first experiment: frozen probes, ordered commit

### Integration boundary and unchanged storage

Modify only the private intern-run implementation in a later implementation PR. Preserve `CToken`, `CSymbolTable`, `CSymbolSlot`, the public frontend API, table creation, macro execution order and all downstream consumers. Do not batch across calls to `c_symbols_intern_tokens`, source-frame changes, or any semantic mutation callback. Keep isolated calls to `c_symbol_intern` immediate.

Within each existing 64-token shape window, take up to B selected identifier indices in ascending order. Start the experiment with B = 4 and 8; include B = 16 as a control. Flush the last partial group in that window immediately. Do not add a whole-stream queue or an extra whole-token pass. Keep the no-sidecar reference and a low-population immediate path for comparison.

Suggested private scratch, with B fixed in each experimental build:

```c
#define C_SYMBOL_BATCH_MAX 8 /* separately evaluate 4, 8, 16 */
typedef struct CSymbolQueryBatch
{
    u64 token_index[C_SYMBOL_BATCH_MAX];
    u64 low[C_SYMBOL_BATCH_MAX];
    u64 high[C_SYMBOL_BATCH_MAX];
    u64 length[C_SYMBOL_BATCH_MAX];
    u32 hash[C_SYMBOL_BATCH_MAX];
    u32 slot[C_SYMBOL_BATCH_MAX];
    u32 result[C_SYMBOL_BATCH_MAX];
    u32 count;
    u32 active;
} CSymbolQueryBatch;
```

This specifies 44 bytes per lane plus eight bytes of bookkeeping: 360 bytes at B=8 or 712 at B=16 before any additional implementation-specific spills. These are layout calculations, not measured stack usage. Only populated lanes are initialized; no table-sized clear or arena allocation is introduced. Keep the `u64` token index because the existing run count/index is `u64`. The source spelling is recovered from the unchanged token and spelling base when needed.

### C-like reference algorithm

The following is design pseudocode using the current key/hash/equality helpers. `indices` denotes a bounded list extracted from the existing identifier mask. The future production implementation should obey current local linkage/style rules and retain one control-flow exit. The obvious loop is a correctness baseline, not a claim that a host compiler will automatically expose all useful memory parallelism.

```c
void c_symbols_intern_batch(CSymbolTable* table,
                           char8 const* spelling_base, CToken* tokens,
                           u64 const* indices, u32 count)
{
    CSymbolQueryBatch q; /* initialize only [0, count) */
    q.count = count;
    q.active = (1u << count) - 1u; /* count <= 16 */
    CSymbolSlot const* snapshot = table->slots;
    u32 mask = table->slot_capacity - 1u;

    for (u32 i = 0; i < count; i += 1)
    {
        String8 name = c_token_spelling(spelling_base, tokens[indices[i]]);
        CSymbolKey key = c_symbol_key(name);
        q.token_index[i] = indices[i];
        q.low[i] = key.low;
        q.high[i] = key.high;
        q.length[i] = name.length;
        q.hash[i] = c_symbol_slot_hash(key, name.length);
        q.slot[i] = q.hash[i] & mask;
        q.result[i] = 0;
    }

    /* No insertion, growth, callback, or token publication in this loop. */
    while (q.active)
    {
        for (u32 i = 0; i < count; i += 1)
        {
            u32 bit = 1u << i;
            if (q.active & bit)
            {
                CSymbolSlot const* entry = snapshot + q.slot[i];
                u64 packed = entry->length_and_id;
                bool done = !packed;
                if (packed && entry->low == q.low[i] &&
                    entry->high == q.high[i] &&
                    (packed & UINT64_C(0xffffffff00000000)) ==
                        (q.length[i] << 32))
                {
                    u32 id = (u32)packed;
                    String8 name = c_token_spelling(
                        spelling_base, tokens[q.token_index[i]]);
                    if (q.length[i] <= 16 ||
                        c_symbol_middle_equal(table->names[id], name))
                    {
                        q.result[i] = id;
                        done = true;
                    }
                }
                if (done)
                {
                    q.active &= ~bit;
                }
                else
                {
                    q.slot[i] = (q.slot[i] + 1u) & mask;
                }
            }
        }
    }

    /* Existing interner owns every write, allocation and stable new ID. */
    for (u32 i = 0; i < count; i += 1)
    {
        u32 id = q.result[i];
        if (!id)
        {
            String8 name = c_token_spelling(
                spelling_base, tokens[q.token_index[i]]);
            id = c_symbol_intern(table, name);
        }
        tokens[q.token_index[i]].symbol = id;
    }
}
```

There is always an empty slot under the existing admitted half-full invariant, so each frozen probe terminates. All spellings remain alive, and names/table pointers cannot move during the read phase. Only IDs, never borrowed slot pointers or presumed insertion positions, cross into the commit phase. Mask generation must retain the existing zero-mask and 64-token tail rules; do not evaluate `ctz(0)` or shift by an integer's width.

An improved scalar-batched implementation should explicitly stage the first occupancy loads for several lanes before resolving their dependencies, then examine survivors. Compare fixed-lane unrolling with the compact active-loop reference: active-mask maintenance and branch scheduling can cost more than the saved stalls. Preserve the same observable algorithm in both forms.

### Duplicates, colliding destinations, growth, and deterministic winners

A snapshot miss is only a provisional absence fact. At commit, **always re-probe the current table** through the existing interner. Earlier misses may have installed the key or occupied the proposed destination, and may have grown either array. Never scatter a vector of snapshot-empty destinations.

For duplicate keys absent at batch entry, the earliest token's commit allocates the ID and retains its name reference. Every later duplicate resolves that ID. Different keys with the same hash/destination advance through the ordinary scalar collision rules. No arbitration mask, hash equality, lane scheduling decision, or sorting order allocates IDs.

Do not reserve for all B requests in advance. That would overestimate unique insertions, change growth timing and memory behavior, and make high-duplication batches pay for nonexistent names. Existing insertion order and thresholds also preserve the rehash traversal/order. On successful admitted input, induction over the original token order shows the same returned IDs, name references, table count, and table contents as scalar execution: read hits are append-only identities; a read miss is re-executed after exactly the same earlier writes.

The reference intentionally pays a second probe and key/hash preparation for snapshot misses. A later private `c_symbol_intern_prepared` factor may reuse the prepared key/hash while preserving the complete existing insertion body and recomputing its slot with the current capacity. That factor is optional and must be measured independently. Its contract is not a reservation token for an old empty slot.

A direct concurrent insertion implementation is rejected for this slice. Full conflict-free parallel insertion would additionally require exact-key leader election, distinct destination arbitration after every probe, capacity/growth barriers, and a deterministic prefix assignment over unique new leaders. Merely detecting equal initial indices solves none of the later dependencies. Serial commit is the bounded solution here.

### Optional exact deduplication, not required for correctness

Use census results to decide whether deduplication pays. At B=8 an exhaustive earlier-lane search has at most 28 pair candidates; B=16 has 120. Filter by length and projected key, then perform exact middle equality where required. A follower records the earliest exact leader's ordinal and receives its ID only after that leader commits. The number of candidate comparisons is not a count of byte comparisons.

Deduplication can save frozen probes for both repeated hits and repeated new names; it cannot merge same-hash/different-middle strings. Retain an off variant. Do not install a permanent translation-unit cache duplicating the interner just to deduplicate eight queries.

## 5. Five forms, SIMD direction, gathers and footprint

| Form | Work and integration | Reason to prefer / reject |
| --- | --- | --- |
| Current | Existing shape filter, one projected-key linear probe at a time; serial insertion | Strong baseline; no packing or miss replay |
| Improved scalar | Same table; compare occupancy/length before unnecessary payload work where profitable; reuse already established symbol facts; optionally factor prepared key/hash | Can win on tiny/cache-hot tables without batching; every reordering needs ordinary-binary measurement |
| Scalar-batched | Frozen probes for B independent keys; ordered commit through existing interner | First candidate; portable, no new SIMD vocabulary; loses when packing, active masks and replay exceed overlapped stalls |
| AVX2 | Experimental four-qword query lanes, or 16/32-byte contiguous fingerprint filtering; serial exact checks/commit | Compare with scalar-batched, not only baseline; current header is not a generic 256-bit lookup layer |
| AVX-512 | Experimental eight-qword query lanes, or 16/32/64-byte fingerprint filtering through suitable feature tiers | More lanes do not imply better end-to-end latency; gathers, tails, masks, frequency, text size and self-host support are gates |

### One query over adjacent slots versus independent queries

For adjacent-slot vectorization, a 24-byte AoS entry is awkward: one 64-byte load does not hold eight independent complete projected keys. A dense control array makes adjacent rejection contiguous, but costs construction and maintenance. A scalar table with short probes can reach its answer before that metadata machinery pays back.

For independent-query vectorization over the unchanged AoS table, the simple complete-row form needs three qword gathers per probing round: low, high and packed length/ID. AVX2 has four qword lanes per 256-bit vector; AVX-512 has eight per 512-bit vector. The gather instruction count does not count underlying cache accesses. Empty slots need only the packed word in the scalar baseline, so blindly gathering every field can increase traffic. Long-name survivors additionally depend on the name array and middle bytes.

The existing hash contains 64-bit products; do not assume a one-instruction AVX2 qword multiply. Preparing hashes scalarly is a legitimate control. An AVX-512 qword arithmetic variant must declare the required additional feature/builtin support rather than inheriting it from byte-vector availability. Use wide address arithmetic for `24 * slot`; truncating this product to `u32` is not justified by a `u32` slot index.

Model the useful number of outstanding misses as bounded by `min(independent demanded cache lines, available miss-handling resources, instruction-window capacity)`, not by SIMD bit width. Eight queries may still hit one line; eight different table rows may demand more than eight lines. For a 64-byte-line model and uniformly distributed eight-byte-aligned starts of complete 24-byte rows, two of eight row-start residues straddle a line: 1.25 lines per fully fetched cold row before sharing. This is a geometric model, not observed traffic, and lazy scalar rejection can read less.

Let `d` be dependent miss stages, `L` the relevant miss latency and `M` achieved memory-level parallelism. A latency component resembling `d * L / M` per query is only a ceiling-oriented model; bandwidth, issue cost, cache hits and active-lane divergence add other limits. Measure cycles/query, batch lane utilization, distinct lines, ordinary retired instructions, LLC misses where available, and full compiler time. No gather latency or Zen 5 speedup is assumed in this report.

### Dense fingerprints: a separate, conditional experiment

A minimally invasive overlay would retain the slots and add a control byte per slot: 0 empty, `0x80 | fingerprint7` occupied. Its latest-array footprint is `25*C + padding` rather than `24*C` bytes, an approximately 4.17% slot-storage increase, before retained growth arrays. A G-byte wrap-safe load can use a cloned prefix of G-1 controls with matching serial updates, or two bounded loads. A padding contract must be implemented and tested; do not read beyond an unpadded allocation.

For this linear-probe overlay, start at the query's normal slot, compare fingerprints in contiguous windows, and restrict exact candidates to the part preceding the first empty control. If a window contains no empty entry and no exact match, advance by G with wraparound. Check exact projected fields and long-name middle bytes for every fingerprint survivor. The serial interner must maintain controls on every insertion and rebuild them on growth. This is a distinct experiment, not part of the initial unchanged-layout kernel.

With independent uniform fingerprints, inspecting n occupied nonmatching entries gives an expected `n/128` accidental 7-bit candidates. That is a conditional expectation, not a measured collision rate or a guarantee about Buster's hash. Fingerprint bits correlated with bucket selection or identical projected hashes violate that simplification. The common-prefix-and-suffix long-name control must remain in the benchmark.

A more invasive dense-key variant has `C` control bytes, `C` four-byte IDs, and `D` allocated 24-byte projected-key rows indexed by stable ID: `5*C + 24*D` bytes, excluding unchanged names and spelling storage. At `D=C/2` this is `17*C`, but it adds an indirection and initialization/growth work. Use allocated D, not live N, and count all retained arrays. It is not automatically smaller on tiny populations or during growth. Do not implement this container replacement without evidence that unchanged-layout batching loses specifically to payload footprint.

### Exact string equality and upstream facts

For long-name survivors, compare the already bounded middle directly. A scalar-overlapped-word implementation is the baseline; compare chunked AVX2/AVX-512 equality and several independent exact comparisons in flight. Stop after a mismatch, mask the final fragment, and never copy arbitrary long names into a packing buffer just to fill lanes. Include late mismatch, equal long strings, and very short middle fragments. The bytes copied to prepare a batch count against the gain.

The first optimization for a caller that already has `CToken.symbol`, a resolved binding, a predefined class, or a committed layout is **not to hash again**. Keep symbol-zero and synthesized-token fallbacks. Respelling a token invalidates its old symbol; macro substitution, paste and source-map ownership must not acquire a stale identity. Reuse a fact only with its real semantic lifetime, not because two requests share a string.

### Buffering and latency

The selected boundary already holds the lexed tokens, so there is no wait for future input to fill a batch. There is still extraction, key preparation, head-of-line blocking on a long probe, and delayed first-result publication until the bounded group completes. Measure that cost and dispatch small groups immediately when it loses.

For a hypothetical arrival stream at rate lambda, waiting for B requests would add approximately `(B-1)/lambda` to the first request and `(B-1)/(2*lambda)` averaged across a regular-arrival batch. Those are explanatory models, not Buster timings. Such a deferred semantic-query queue is not proposed: parser control flow often needs the answer before it can produce the next request. No batch may cross define/undefine, scope binding, type mutation, or rollback merely to improve occupancy.

## 6. Types and layouts: semantic keys before caching or batching

Canonical identity, C compatibility, and equal object layout are different relations. Two types can share size/alignment without being compatible, or be compatible while retaining distinct IDs/provenance. Hash equality can only filter candidates; it never replaces the operation's exact semantic equality.

| Query / type family | Required semantic key or dependency | Reuse boundary |
| --- | --- | --- |
| Builtin layout | Type kind plus selected target/data-layout policy, including long/long-double, signed-char and atomic rules when relevant | Immutable for that fixed target/policy; prefer the existing direct helper |
| Qualified/pointer/vector type construction | Operation kind, exact child identity, qualifiers, vector shape, type-level attributes and any ABI-relevant context the constructor consumes | Only after dependencies are stable; equal pointer size is not equal pointer type |
| Function compatibility/construction | Return type, ordered parameter types, prototype/variadic state, parameter-adjustment mode, qualifier policy, calling-convention/attributes where consumed | Preserve current adjustment and diagnostic behavior; compatibility is not automatic ID coalescing |
| Arrays | Element identity, bound category, constant/inferred count or unresolved expression identity with its scope/environment; star/VLA/flexible/incomplete status | Inferred bounds, completion, constant evaluation, and rollback invalidate affected facts; do not freeze VLA runtime values |
| Tagged or anonymous aggregates/enums | Declaration identity and scope/namespace/kind, qualification/alias identity, completeness, ordered members and layout-affecting attributes; enum underlying-type state | A tag spelling alone is not a universal identity; ID reuse after rollback is an invalidation event |
| Object and bit-field layout | Type identity, target ABI, packing, type/declaration alignment distinction, ordered member widths, bit-field access span and policy | A cached type size does not certify object alignment, access width, ABI register classification or diagnostics |
| Compatibility work item | Ordered left/right IDs plus `ignore_qualifiers` and the effective query context; exact pair equality | Prefer bounded query-local work reuse; do not silently extend lifetime over mutation/evaluation |
| Published canonical metadata | Canonical ID and owning immutable program/type state plus target/options consumed by the query | No frontend IDs/pointers escape; mutators must invalidate derived results according to existing canonical contracts |

`c_parse_type_layout`'s existing token-pointer cache association is part of its current machine lifetime, not a sufficient key for an invented global cache. Reuse of incomplete/provisional answers is particularly dangerous. A failed or unresolved layout query is not a permanent negative fact. Array-bound inference in `c_parse_infer_file_array_bounds` and declaration attribute processing visibly mutate inputs. Reentrant alignment/bound evaluation can create types during a query. [Layout solver][layout-source] [Bound mutation][tag-source] [Declaration processing][compat-a]

For any later layout-reuse change, first inventory **every** mutation and rollback path affecting the selected fact, including transitive dependencies. An immutable-phase cache needs a real freeze boundary; otherwise a diagnostic prototype can invalidate the whole bounded cache on each relevant mutation/rollback and measure the remaining opportunity. Incrementing an epoch without hooking all mutators is not a correctness design. Do not add a full dependency graph or permanent type IR to recover a small cache hit.

### The closed type-DAG issue needs source/status reconciliation, not a duplicate issue

[Issue #241](https://github.com/buster14a/buster/issues/241) is closed as completed, but the inspected `c_parse_types_compatible` body still allocates a pair stack and schedules return/parameter/element pairs without a visited-pair set. That is a source observation, **not a fresh reproduction** of its historical exponential timing. Its existing discussion explicitly asks for a pinned residual-premise check and forbids a process-global mutable cache. Keep any follow-up in that work rather than opening a duplicate.

For a pure repeated-DAG comparison, a query-local exact set keyed by `(left_id, right_id, ignore_qualifiers)` can avoid expanding identical dependencies repeatedly. However, the current array comparison can evaluate bounds, and a global Boolean cache would conceal changes or effects. Preserve traversal/diagnostic ordering and distinguish an in-progress pair from an established result. A later implementation must first isolate the mutation-free comparison region or prove the evaluation contract. Test late mismatches and adjustment modes; neither equal hashes nor an assumed reflexivity shortcut is sufficient to skip validation of arbitrary invalid IDs.

This may be a higher-value algorithmic repair on its historical stress case than any SIMD table. It is not selected as the first batched kernel because current reproduction, ownership disposition, and the query-side-effect contract are not yet established. The interning design does not depend on resolving it.

## 7. Correctness, adversarial controls, and acceptance

### Differential tests for the selected kernel

Use the existing C frontend test infrastructure and `c_test_intern_scan_by_shape` seam; add focused cases in its owning module rather than a separate container-test framework. Run each implementation against the unchanged scalar interner with identical seed order, source bytes and options.

1. **Exact identity and boundaries:** admitted lengths 1-17, 31/32/33, 63/64/65, long identifiers near the existing length limit; equal strings at distinct addresses; misalignment; bounded buffers at guard-page ends; all partial token windows and all partial query batches. Retain the existing diagnostics for oversized nonliteral tokens and the independent literal sentinel rules.
2. **Insertion conflicts and growth:** all hits, all new keys, alternating hit/miss, every lane the same new key, multiple duplicate groups, different keys sharing the first slot, wraparound probe clusters, names-array growth, half-occupancy crossing, and several growths across runs. Compare every token ID, name bytes and first-occurrence ownership, predefined IDs, capacities and table contents after commit. Never compare unrelated process pointer values; compare which original input span each stored name references.
3. **Unfavorable distributions:** equal first/last words and length with different middles, long common prefixes, differences only at the final compared middle byte, high duplicate rates, uniformly distinct keys, and deliberately concentrated hashes. Exercise the actual half-full policy; any fixed 75-90%-load lookup microbenchmark is explicitly out of production policy, not a proposal to raise it.
4. **Pipeline semantics:** symbol-zero synthetic tokens; respelling and token pasting; builtin/predefined order; command-line define/undefine order; includes and inactive conditionals; macro rescans and source locations. Preserve byte-identical preprocessed tokens, diagnostics and canonical output. Do not modify the active #575 conditional/macro repair as part of this work.
5. **Portability and compiler gates:** ordinary Debug and Release tests; ASan/UBSan under existing fatal policy; applicable allocator/mode/target matrices; scalar fallback including AArch64/MSVC paths; self-hosted stage fixed point and exact SIMD-feature refusal/fallback behavior. Compare same-target producers: host-dependent target defaults are not an allowable source of A/B drift.

The current `simd.h` operation inventory provides neither a general AVX2 lookup API nor the proposed qword gather/scatter vocabulary. A gather prototype is not production-ready merely because Clang accepts intrinsics. An accepted SIMD form must have exact feature guards and either the established self-hosted lowering path or the correct scalar algorithm selected before canonical IR construction. Do not broaden the existing full/base feature flags to make a prototype compile. [SIMD interface][simd-source]

### Whole-compiler measurement contract

Build separate uninstrumented Release producers with the same trusted producer toolchain and flags. Freeze compiler source/tree, workload source/tree, generated inputs, target/CPU/options, binary hashes, command vectors, and environment. A compiler implementation change must not change its own compiled workload underneath the measurement. Keep the diagnostic census in a different build and archive parity checks.

Use the existing native `bench_throughput` harness, its raw-data/replay and A/A qualification, rather than a new timing script. Include all default workloads, especially `tiny_startup` and `symbol_table`; separately include the existing optional `macros` and `aggregate-abi` inputs and a frozen real unity source. Optional cases use their documented `--no-guard` path and do not replace the ordinary CI family. Synthetic long-middle collisions and controlled duplication should extend the same workload infrastructure only in a later authorized implementation.

Useful existing entry points, to run only on an explicitly authorized CI/remote worker with prepared ordinary binaries:

```sh
./build.sh bench_throughput self-test
./build.sh bench_throughput run \
  --baseline /absolute/base/ide --candidate /absolute/candidate/ide \
  --baseline-id BASE_SHA --candidate-id CANDIDATE_SHA \
  --output /new/absolute/default-comparison
./build.sh bench_throughput compare --output /new/absolute/default-comparison
# Separate optional diagnostic family; not the standard CI guard:
./build.sh bench_throughput run \
  --baseline /absolute/base/ide --candidate /absolute/candidate/ide \
  --output /new/absolute/optional-comparison \
  --workload macros --workload aggregate-abi --mode all \
  --no-guard --require-identical-output
```

On GitHub-hosted workers, use the documented Clang-bootstrapped `./build/build` entry point instead of implying that local TCC bootstrapping occurred. These command templates were checked against the harness guide, **not executed**. Pair repetitions, workload admission, exact output placement and host lease are set by the existing harness/host contract; no arbitrary two-run timing is accepted. Require exclusive measurement use of the designated host and keep builds/tests off it while measuring. Access to a runner does not establish physical-Zen-5 qualification.

Report process wall/CPU time, per-stage attribution, output identity, peak RSS, retained/logical allocation and explicit traffic separately, production text bytes, and raw per-workload/mode observations. On suitable Linux hardware, add retired instructions and cache/miss evidence. A sampled profile, Callgrind Ir, allocation census, and hardware instructions answer different questions. Lookup throughput alone is not acceptance, and generated-program speed is not compiler throughput.

### Break-even and rejection criteria

Let H be requests already present in the frozen snapshot and M the snapshot misses. Let S be average immediate scalar hit cost, V the frozen batched hit cost, and R the **extra** read/preparation cost per snapshot miss beyond the ordinary commit it must still perform. Let P include extraction, buffers, lane bookkeeping, publication and any extra metadata work. The first-order necessary condition is:

`H * (S - V) > M * R + P`.

Measure each term with consistent boundaries; do not charge shared classification twice or omit key preparation from V. Deduplication adds its own comparison cost and changes H/M work; table replacement additionally charges metadata construction, rehashing and retained footprint. A high miss fraction or a tiny cache-hot table can reject batching before any gather experiment.

For complete compilation, with baseline fraction p in the affected work, local speedup s, and normalized additional cost e, the model is:

`T_candidate / T_base = 1 - p + p/s + e`.

For illustration only, if p=0.02 and s=2, the ideal time reduction before overhead is 1%; even infinite local speed cannot save more than 2% of that hypothetical baseline. Neither p nor s is measured here.

**Proposed decision threshold for this experiment, not a change to repository policy:** require a repeatable at-least-1% whole-compiler wall-time improvement on a predeclared representative large-input family, with the one-sided uncertainty bound supporting that improvement, while passing the unchanged standard throughput guard and all correctness gates. Predeclare a tiny/small-input nonregression margin no looser than 1%, a peak-RSS margin no looser than 1% per representative workload, and a review budget of 4 KiB added production text for the selected private kernel. These are proposed review gates, not approved native-retirement thresholds; tighten or explicitly review them before collection, never after seeing outcomes.

Reject or leave disabled when the census shows no material affected share; the confidence interval is inconclusive; benefits occur only in lookup microbenchmarks or an artificial overload outside the normal policy; scalar-batched or improved scalar is as good as the wider form; any diagnostic/ID/target behavior changes; the existing guards fail; or buffer/metadata/retained memory and text costs exceed the predeclared budget. A high-duplication-only win needs a supported dispatch condition rather than a universal replacement. A wide kernel that loses on common small inputs is not rescued by an attractive peak lookups/second number.

## 8. Ownership, dependencies, and implementation sequence

[Issue #60](https://github.com/buster14a/buster/issues/60) is the matching existing SIMD/preprocessing experiment and already requires a material end-to-end opportunity. Its preference to avoid gathers supports scalar-batched and contiguous-metadata controls first. The existing run now has a shape-filtering loop; that is not proof that every historical contiguous-expansion prerequisite is complete or that the whole preprocessor may be reordered. This report does not change the issue's labels or completion state.

The inspected open PR set included [#575](https://github.com/buster14a/buster/pull/575) on conditional directives within macro invocations and [#576](https://github.com/buster14a/buster/pull/576) on validation attribution. No open interning implementation PR was returned by the targeted search. Absence of a PR is not ownership permission: a later implementation must recheck comments and current main before claiming the slice. [#241](https://github.com/buster14a/buster/issues/241), [#291](https://github.com/buster14a/buster/issues/291), and [#124](https://github.com/buster14a/buster/issues/124) remain related work, not requests for competing implementations or generic metadata containers.

The independently reviewable sequence is: obtain the frozen diagnostic census and whole-compiler attribution; decide whether the opportunity survives the break-even test; implement the exact scalar-batched/ordered-commit oracle and differential tests; compare improved scalar and B choices; only then admit a separate fingerprint or gather experiment if the remaining cost explains it. SIMD vocabulary changes, if any, are a separately justified dependency, not part of this documentation PR. No type-layout redesign is a prerequisite.

## 9. Publication and validation record

The canonical detailed report is this file, proposed through a documentation-only PR and cross-linked from #60. The PR conversation records the exact published commit and verification results. No duplicate implementation issue is required for these conditional alternatives, and nothing here closes an existing issue.

Access discovery used the connected GitHub repository/branch/file/issue/PR reads and verified repository identity. A read-only local clone route failed with `Could not resolve host: github.com`, and `gh` was unavailable; neither failure was treated as evidence that the working connector lacked write access. No credential value was requested, printed, copied, or discovered by scanning unrelated storage. Publication uses the connected GitHub write actions on a newly created, session-owned branch, not the default branch. The created file, PR and issue comment are to be read back before publication is reported as verified.

Completed for this report: pinned source inspection; primary-source comparison; arithmetic/model review; issue/PR overlap searches; main-drift comparison showing no analyzed-source change. Not run: a compiler build, tests, self-hosting, sanitizers, source census, CI/remote measurements, hardware-counter collection, or a prototype. Applicable documentation checks on the published PR must be read from its actual head; no green CI result is implied by publication.

## Sources

[token-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c.h#L1-L210
[symbol-table]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_internal.h#L320-L415
[intern-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_source.c#L3031-L3210
[intern-run]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_source.c#L3300-L3405
[builtin-caller]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_source.c#L7850-L7860
[directive-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_source.c#L8150-L8260
[binding-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_parse.c#L11600-L11800
[tag-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_parse.c#L5120-L5370
[compat-a]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_parse.c#L10700-L10980
[compat-b]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_parse.c#L10980-L11180
[layout-helpers]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_parse.c#L1080-L1260
[layout-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/frontend/c/c_parse.c#L1390-L1580
[simd-source]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/simd.h#L1-L315
[swiss]: https://abseil.io/about/design/swisstables
[f14]: https://github.com/facebook/folly/blob/main/folly/container/F14.md
[vldb]: https://www.vldb.org/pvldb/vol16/p2755-bother.pdf
[intel-avx]: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-avx-512-instructions.html
[intel-conflict]: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-xeon-processor-d-2100-product-family-technical-overview.html
