# Register allocation: exact bit-parallel queries before wider allocation policies

Research design, 13 September 2026. **No implementation or new performance measurement.**

Analyzed repository: `buster14a/buster`, repository ID `1071732997`.
Analyzed main: `5865fa7add27908656aedf28a3c0e6a3de47dc04`;
tree: `950eae4171f20ece45f1092fbc311c950665ed5b`.
Publication-base recheck: `5217113aa33a74cd78524c29bd8addfc0e219ef0`,
tree `a1a85005b5e6ebc48b5af6cfb66a9b31cc73ec8d`. The comparison contains only
the two retired Vulkan installers and their CI retirement check, not allocator
or research-guide changes. Source links below deliberately retain the analyzed SHA.

**Disposition:** develop the exact QUALITY pin-budget index below as a bounded,
census-gated implementation experiment under [#125][I125]. For default FAST
compilation, investigate the remaining immutable row-classification work under
[#131][I131] first. No available evidence establishes which would save more
current-main wall time. The first is the best-developed, tightly bounded
same-policy design here, not a demonstrated globally largest bottleneck.

This is the canonical detailed design, not a measurement audit or an implementation
claim. No historical audit or closed audit index is changed. No production code,
CI configuration, allocation policy, issue state, or another session's branch is
changed. Related work should link here rather than duplicate this report.

## 1. Current state and ownership

The review used [AGENTS.md][G0], the [machine][G1], [SIMD][G2],
[benchmarking][G3], [parallelism][G4], and [testing][G5] guides, the
[native throughput harness][H1], its [dedicated-host contract][H2], the
[QUALITY scratch census][H3], and the newest dated audit found at the analyzed
revision: [2026-09-12T224432Z][A1]. The historical
[SIMD backend catalogue][A2] is useful context, not a description of what remains
unimplemented today.

The relevant source identities are:

| Source | Blob at analyzed SHA | Important entry points |
|---|---|---|
| `register_allocator_fast.c` | `abb6d2e5fed3bc5a2bf904f0097bfc4a4340fdee` | `machine_fast_prepass_build`, `machine_fast_placement_build_prepassed`, `machine_fast_owner_is_dead`, `machine_fast_conform_edge` |
| `register_allocator_quality.c` | `da55966b6ba60455fdfd8dd796ea73cc1922588b` | `machine_quality_placement_build_core`, `machine_quality_heap_sift`, `machine_quality_region_next`, `machine_quality_foreclosure_prefix_ensure` |
| `register_allocator_predicate.c` | `47059dc94f7ec147e4c0f712447933bad47a869f` | `machine_predicate_placement_build`, `machine_predicate_pick`, `machine_predicate_release` |
| `machine.c` | See [pinned source][S8] | `machine_stack_placement_build_core` and placement integration |

Several proposed optimizations in old issues already have current equivalents:

- FAST already consumes a compact `MachineOpcodeRow`, classifies the four operand
  slots into masks, reuses cached call classification, builds edge indexes, and
  omits QUALITY-only interval/disqualification/loop facts when
  `wants_quality_facts` is false. Its current prepass is not loading an entire
  96-byte descriptor per row. [Source][S1]
- FAST already tracks held/dirty registers with masks. Its block owner rows are
  initialized only at held positions. Dense sixteen-owner tiles can use an
  existing SIMD comparison; sparse populations use guarded set-bit traversal.
  Blindly loading every owner lane would read uninitialized scratch. [Sources][S2]
  [and block-state construction][S3]
- QUALITY already shares its prepass across the baseline and retries, uses lazy
  foreclosure prefixes, resets pin mappings through touched values, and uses
  64-bit weighted traffic. Re-proposing these as missing work is incorrect.
  The current candidate-region table is **8CL bytes**, not the historical 4CL
  cited in #125. [Sources][S4] [and candidate/region construction][S5]
- Predicate placement is a separate k1-k7 bank, not seven more bits in the
  ordinary GPR/vector file. An existing absence certificate avoids inspection
  when valid. When predicates exist, the wrapper constructs a temporary
  non-predicate projection and invokes ordinary placement inside it. [Source][S7]

[#125][I125] already covers repeated budget scans, sparse candidate-region traffic,
local IDs and scratch costs. Its [latest coordination comment][I125last] records
the census as landed and names existing recovery branches. This report neither
claims those branches nor republishes their implementation. The older closure
experiment and negative result remain relevant. [#131's correction][I131correction]
explicitly retracts stale descriptor, cache-residency and speed-ceiling claims.
Targeted open-PR searches also found the retirement-dispatch draft [#522][P522]
and performance-contract draft [#568][P568]; neither is modified here. A search
snapshot is not a claim that all work is unowned. Implementation must recheck
these threads and the recovery branches immediately before editing.

The [#36][I36] retirement remains a separate coordinated effort. At this analysis
snapshot, do not assume the proposed `none`-to-MIR_STACK compatibility dispatch
in #522 has landed. Research the maintained MIR allocators; do not optimize,
extend, delete, or re-enable the retiring direct native emitter. The newer #508
support manifest and #511 contract must be used by the retirement owners, not
replaced with this report's experiment criteria. In particular, #568 records
maintainer approval of a performance contract but not acceptance of a candidate.

## 2. Evidence ledger: what is and is not measured

| Evidence | Actual scope | Permitted conclusion |
|---|---|---|
| Current source and census definitions | The analyzed SHA, inspected statically | Work patterns, bounds, existing mechanisms, and exact policy dependencies |
| [Native 9700X capture reported on #131][I131capture] | One 8 kHz Superluminal capture at `44a90fcbff44988624878aff9db3346a8bc6550b` | Historical prioritization, not an A/B or current-main phase decomposition |
| [Newest checked-in audit][A1] | Older frozen input `5dab18af0208cfa7fa65673b6884d5c253fda85b`, shared KVM Xeon, Callgrind, x86-64-v3 producer | Historical instrument-specific counts, not Zen 5 wall time |
| [Recovered closure experiment][I125closure] | Extracted older kernel; original attachment unavailable; one-region regression reported | A caution and a future reproduction lead, not an accepted compiler optimization |
| Algorithms in sections 4-6 | Not implemented, executed, timed, or profiled in this session | Hypotheses and reasoned correctness/complexity arguments only |

The historical 9700X capture reported prepass 21.481 ms inclusive / 21.356 ms
exclusive, placement scan 38.852 ms inclusive / 29.731 ms exclusive, and complete
FAST placement 60.339 ms inclusive, against a 708.685 ms compiler lane. These
figures are not additive across parents and children. They do not identify the
classification share of the prepass. A single sampled capture does not establish
an achievable reduction or a rigorous Amdahl ceiling for this source revision.

The newest audit lists `machine_predicate_placement_build` at approximately
945 million inclusive Callgrind Ir, 6.9% of its old compile. Because that wrapper
calls FAST/QUALITY/stack placement, attributing the entire number to predicate
register selection would be wrong. Its x86-64-v3 producer excludes the AVX-512
paths; Ir is not hardware retired instructions and its memory-fill accounting
cannot be interpreted as current wall time. No result from that audit is a new
measurement of this report's proposals.

**No compiler, test binary, allocation kernel, benchmark or profiler was run
locally. No CI or remote experiment was dispatched by this research session.**
Documentation processing is not an allocator experiment. All implementation,
correctness, platform, self-host, physical-host and performance gates below are
unrun for these proposals.

## 3. Separate the work before choosing an instruction set

Let N be machine rows, V virtual-register IDs, E explicit edges/edge uses, C
admitted QUALITY candidates, L merged regions, R active physical resources,
and M allocation edits. C is currently bounded by 4096. These quantities are
work denominators, not time fractions.

| Component | Current symbols/representation | Safe first direction; decisions that must remain ordered |
|---|---|---|
| Liveness facts | `machine_fast_prepass_build`: textual `last_use`, defining/use blocks, `escapes`, rematerialization, edge uses | Batch immutable classification; replay duplicate-vreg updates in source order. Do not build precise global live sets that these consumers do not need. |
| Interval construction | QUALITY-only start/end arrays; merged loop-span closure | Preserve inclusive bounds, dead sentinels and edge uses. Existing nested closure is an algorithmic lead, not a license to adopt a historically regressing replacement. |
| Ordering | `machine_quality_heap_sift`; `machine_quality_region_next` | Preserve heap tie behavior and admitted candidate prefix; cache only an exactly equivalent region order. |
| Interference/legality queries | Lazy foreclosure prefix per reached register; up to eight assigned spans per register; excluded boundary rows | Keep O(1) static-prefix queries. Batch independent span tests; budget bitmap addresses a different, register-independent predicate. |
| Register choice | FAST reservation/class/free/dead masks and age policy; predicate `pick`; QUALITY preference file | Compute masks in parallel, then perform the same ordered choice and ownership transfer. |
| Spills/rematerialization | `machine_fast_spill`, `machine_predicate_release`; ordered `MachineEdit` stream | Preserve dirty, escaping, use-consumption and rematerialization rules. A different victim heuristic is not a representation optimization. |
| Splitting | QUALITY region legality, entry/exit lists, traffic and capacity limits | Query unchanged conditions faster; never move a boundary or alter its economics implicitly. |
| Stack slots | FAST post-scan `slot_needed`, defining-block pool, dedicated escaping/vector homes; predicate homes | Preserve exact layout/order. Finer lifetime-based slot reuse is a separate policy/output change. |
| Operand rewriting | Four placement bytes per row in `operand_registers`, plus edits | Batch only committed independent stores. Do not create a second mutable IR or reorder equal-point edits. |
| Parallel copies | `machine_fast_conform_edge`, parameter edge handling, predicate captures | Preserve snapshot semantics, repeated sources, cycle breaking and deterministic edit order; independent ready copies may be classified together. |

[FAST prepass][S1], [owner/edge machinery][S2], [frame layout and edit merge][S6],
[QUALITY preparation and ordering][S4], and [predicate wrapper][S7] are the source
anchors for this decomposition. MIR_STACK is a placement/selector/encoder
verification mode, not an interference-solving allocator to replace. [Source][S8]

### Three actionable opportunities

| Opportunity | Existing owner | Expected beneficiary, not measured outcome | Main risk |
|---|---|---|---|
| A. Exact saturation index for repeated whole/split budget queries | #125 | QUALITY functions with many long repeated budget scans, especially rejected probes | Setup/maintenance loses on small, early-reject or low-reuse functions |
| B. Batch remaining immutable FAST prepass classification | #131 | Default FAST and QUALITY's shared prepass on sufficiently large row populations | Deinterleave/gather and stable-update costs exceed saved scalar work |
| C. Sparse candidate-region traffic with reusable exact ordering | #125 | QUALITY functions with many regions but sparse candidate incidence/repeated rejected region choices | Dense/tiny cases, sorting workspace and construction costs |

A is developed below because it has a particularly small semantic boundary and
uses a monotone property already present in the implementation. B has the clearest
historical relevance to default FAST. C removes a different repeated traversal;
it should not be bundled into A's first implementation or used to obscure A's result.

## 4. Opportunity A: exact monotone pin-budget index

### 4.1 The predicate and its source sites

Inside `machine_quality_placement_build_core`, the [whole candidate gate][S9]
and [split gate and commits][S10] ask the same question:

```text
reject [a,b] iff there exists i in the inclusive range [a,b]
                    with pin_depths[i] >= pin_budgets[i]
```

Budgets are fixed for the attempt; depths start at zero and increase by one on
each accepted covering span. There is no mid-attempt unpin operation. A retry
starts a new pack. The marginal caller-saved foreclosure gate currently precedes
the whole-span budget gate; this ordering and its control flow remain unchanged.

This query does not need the numerical depth of every covered row. It needs an
exact answer to whether any row has exhausted its budget. That permits reducing
query precision without reducing allocation correctness or liveness precision.

### 4.2 Representation

Retain the existing `u8 pin_depths[N]` and `u8 pin_budgets[N]`. Add temporary,
attempt-local bitsets, not fields on canonical IR or persistent MIR:

```text
S[i] = (pin_depths[i] >= pin_budgets[i])
level[0] = packed S bits, one u64 per 64 instruction rows
level[j+1][k] = 1 iff word k in level[j] is nonzero
```

Continue until the top level is one word. Store exact bit/word counts for each
level. No owner pointers or tree nodes are needed. For a u32-sized row domain,
there are at most six levels. With rounded word counts w0=ceil(N/64) and
wj+1=ceil(wj/64), additional storage is `8 * sum(wj)` bytes plus bounded level
metadata: asymptotically about **0.127N bytes**, not another V- or C-by-L table.
Use checked wide arithmetic for allocations and indices.

Initialization is crucial: zero depth does **not** imply zero S. Every row with
budget zero must start set. Fully initialize used words and zero all padding
bits even on dirty arena reuse. Build summaries bottom-up. Allocate only after
the existing no-target/no-candidate/empty-function early exits. Reset/rebuild
for every retry; never retain saturation from a rejected earlier pack. Reuse the
same allocated capacity, and fuse initialization with an existing reset walk
where practical rather than adding a second unconditional row pass.

Retaining depths avoids replacing an easy-to-check invariant with a compressed
counter scheme. Budgets are bounded by the general-register file. Verify that
bound before any u8 arithmetic; this design does not silently saturate counters.

### 4.3 Query pseudocode

The following is algorithmic pseudocode, not compiled C. Production helpers must
follow the repository's one-return, explicit-bounds and SIMD-wrapper rules.
`NONE` is outside the row-index domain, represented with a wide type, not a valid
row sentinel accidentally included by `<= b`.

```text
next_set_at_or_after(p):
    result = NONE
    level_index = 0
    searching = (p < bit_count[0])
    while searching:
        if p >= bit_count[level_index]:
            searching = false
        else:
            word_index = p >> 6
            lane = p & 63
            bits = level[level_index][word_index] & (U64_MAX << lane)
            if bits != 0:
                hit = 64 * word_index + ctz_nonzero(bits)
                while level_index != 0:
                    level_index -= 1
                    word_index = hit
                    bits = level[level_index][word_index]  // known nonzero
                    hit = 64 * word_index + ctz_nonzero(bits)
                result = hit
                searching = false
            else if level_index + 1 == level_count:
                searching = false
            else:
                p = word_index + 1  // next child-word bit in parent
                level_index += 1
    return result

budget_rejects(a, b):
    hit = next_set_at_or_after(a)
    return hit != NONE and hit <= b
```

Ascending skips empty child words. Descending takes the first nonempty child
at each level, so the returned row is the earliest set bit at or after a.
A query visits at most `2h-1` words for h levels. A one-word function needs one
masked word query; direct endpoint-row or short-span scalar checks may still be
cheaper. No shift by 64 and no ctz of zero occurs. `b+1` is unnecessary here.

### 4.4 Commit pseudocode and monotonicity

Perform the exact existing register/interference/boundary checks and sequential
choice. Only after that candidate is accepted, replace its depth-update loop:

```text
commit_depths(a, b):
    for each leaf-word segment of [a,b], in increasing row order:
        new_bits = 0
        for each valid row i in that segment:
            assert depth[i] < budget[i]
            depth[i] += 1
            if depth[i] == budget[i]:
                new_bits |= 1 << (i & 63)
        old = level[0][leaf_word]
        level[0][leaf_word] = old | new_bits
        if old == 0 and new_bits != 0:
            propagate_child_nonempty(leaf_word)
```

Propagation sets the child-word bit in the parent. Continue upward only when
the parent's old word was zero; otherwise its own parent was already informed.
No deletion propagation is needed within an attempt. At most N row bits change
from zero to one; each summary word becomes nonempty at most once. Initial
zero-budget bits are accounted for during initialization, not repeatedly inserted.

The inner update is a good SIMD candidate: rows in one accepted interval are
independent and contiguous. Pack new equality bits, OR once per leaf, then update
the small hierarchy with scalar word operations. Queries for the next candidate
must wait for the entire current commit. No threads race on summary words.

**The design retains accepted-span O(length) depth updates and current pin-mask
painting.** It eliminates repeated row-by-row *queries*, not every span traversal.
A separate difference-event construction for pin-active masks might eventually
remove painting, but is not included in this bounded experiment.

### 4.5 Correctness and nonconflicting resources

The proof is by induction over the original candidate/region/register decision
sequence and separately over each retry.

Initially both implementations have identical zero depths, and S exactly
represents `0 >= budget`. The hierarchical query is an exact existential test,
so every budget decision agrees. Other predicates and all tie/order rules are
unchanged. An accepted span had `depth < budget` at every row before its update;
therefore an increment either leaves the predicate false or makes it true at
exact equality. The packed update restores the invariant. Rejected candidates
change neither depth nor S. A retry resets both implementations identically.
Thus the same candidates receive the same registers/spans, and subsequent FAST
placement, final cost comparison and pin validation receive identical inputs.
This establishes conditional same-policy equivalence, not a claim that the
existing allocator has no bugs.

Resource exclusion is still checked by the original per-register interval and
boundary-row tests. Two overlapping inclusive pinned spans cannot occupy the
same physical register; split install/store rows also cannot overwrite another
span. Fixed/physical operands, target scratch requirements and call clobbers
remain covered by the original foreclosure information. The budget bitset is
**not** a substitute for those checks. QUALITY remains GPR-only at this boundary;
no new vector/predicate pinning is introduced.

Naively committing multiple passing candidates concurrently is unsafe. Suppose
one row has exactly one spare budget unit and two candidates cover it. Both can
pass the old snapshot even if they choose different free registers; committing
both exhausts the spare required by the local scan. Same-register candidates
also conflict. This design parallelizes observations and contiguous updates,
not those choices. A hypothetical concurrent commit would require both disjoint
expanded resource/time footprints (including boundary rows and aliases) and
noninteracting budget updates, plus unchanged tie outcomes. Proving and testing
such a scheduler is outside this experiment.

### 4.6 Complexity and a measurable break-even condition

Let Q be actual whole/split budget queries in an attempt; B the number of rows
actually examined by the old early-stopping loops; U the sum of lengths of
accepted spans; h the hierarchy depth; and K the number of monotone summary
transitions. The old slice costs O(B+U). The indexed slice costs
O(N+Qh+U+K), with O(N/64) extra words. Here K is bounded by the number of summary
nodes, while forming changed row bits remains included in U. Three attempts
multiply bounded initialization/query costs; no per-attempt unbounded retention
is introduced.

A useful implementation experiment estimates, rather than assumes:

```text
old_row_query_cost * B - indexed_word_query_cost * Q*h
    > incremental_initialization_cost * N
      + incremental_update_cost * U
      + summary_transition_cost * K
      + extra_cache_and_allocation_cost
```

These coefficients are not universal constants. Existing
`whole_budget_rows`, `split_budget_rows`, attempt, rejection and candidate/region
counters provide the first workload census. Add bounded diagnostics only when
needed for actual bitmap word reads, saturation transitions, query lengths and
accepted update work. Do not keep rescanning rows merely to preserve an obsolete
counter's value. Diagnostic logical stores are not measured cache traffic.

Tiny functions, empty candidate heaps, one-region functions, call-heavy marginal
rejections and first-row budget rejections can favor the old scalar query.
Long spans queried repeatedly before saturation, or repeatedly rejected far from
their starts, can favor indexing. High register pressure alone is not a sufficient
selection rule. Initially benchmark explicit variants; choose any later deterministic
size/work dispatch from observed crossovers, not an invented 64/512 threshold.

### 4.7 Scalar, scalar-batched, AVX2 and AVX-512 comparison

| Form | Query and maintenance | Expected win region / disadvantage |
|---|---|---|
| Improved scalar/bitset | u64 hierarchy and ctz; scalar byte-depth updates | Strong portable baseline; avoids row scans without wide-vector overhead; added index can still lose on tiny/early exits |
| Scalar-batched | Unroll existing byte comparisons and OR partial results; optionally operate on short chunks before indexing | Better independent loads, little setup; still O(B); batch-wide work may defeat an early exit |
| AVX2 | Compare 32 depth/budget bytes per full tile, or increment/equality-pack 32 accepted rows into a leaf half | More useful on long full tiles; unsigned ordering needs a correct unsigned comparison construction, e.g. sign-bit bias for signed comparison; movemask/half assembly cost matters |
| AVX-512 | 64-byte accepted-row update and equality mask; optional 64-row direct-query competitor; hierarchy itself remains scalar | One leaf per full tile, subject to BW support and wrapper availability; tails, short spans, setup and host behavior can erase the advantage |

For accepted updates, equality after increment is sufficient because admission
proved strict inequality beforehand. This is not valid as a replacement for
arbitrary unsigned `>=` queries. Scalar tails or correctly guarded/masked loads
must not read beyond allocated storage. No raw intrinsics belong in the allocator:
use `<buster/lib/simd.h>`. Dword owner helpers already exist, but this report does
not assert that every needed byte-update or AVX2 wrapper is already available.
Any missing operation owes the complete builtin/IR/validation/encoding/scalar
fallback/self-host test path in the SIMD guide; that dependency must not inflate
the initial all-scalar bitset experiment. MSVC and AArch64 retain a correct path.
Zen 5 and Zen 4 must be measured independently.

## 5. Opportunity B: immutable row classification, not simultaneous allocation

The exact seam is `machine_fast_prepass_build` in [the current prepass][S1]. Its
compact `MachineOpcodeRow` table is already shared with other machine consumers.
Do not introduce another opcode schema. Also preserve
`machine_instruction_opcode_row(function, instruction)`: dynamic inline-assembly
effects and side successors cannot be reconstructed from the opcode number alone.

A bounded implementation sequence is:

```text
for each block:
    take a bounded contiguous row batch without crossing its end
    validate row/opcode access before table lookup
    classify ref-kind nibbles and combine current opcode-row roles/flags
    preserve scalar handling for dynamic/irregular rows and side successors
    write each row's existing operand_masks slot
    consume virtual operand updates in original (row, slot) order
```

Commutative min/max/OR updates are not the whole prepass. `definition_seen`,
first defining block, disabling a rematerialization recipe on a second definition,
mutable values and malformed-row handling are order-sensitive. SIMD scatters to
repeated vreg IDs must not race or lose updates. A transient compacted batch of
actual virtual operands followed by ordered scalar replay is safer than adding
conflict-detection/scatter machinery immediately. It must replace old decoding,
not coexist with a second persistent command stream.

Compare the current scalar loop, a scalar-batched loop with ordinary table loads,
an AVX2 four-row projection (96 bytes of 24-byte rows), and an AVX-512 eight-row
projection (192 bytes). Three vector loads describe input size, not total cost:
deinterleaving, compact-table fetches, exceptional lanes, stable replay and tails
all count. Compare scalar table loads against gathers only after observing the
actual working set; fitting a table in L1 proves neither residency nor a bottleneck.
Keep block-local backward `next_call` segmentation and the already-cached call bit.

Acceptance is equality of all prepass facts, classification words, fail-closed
behavior, allocation edits and emitted bytes, followed by whole-compiler timing.
Historical simple-row run-length allocation batching was negative in the old
catalogue; this proposal batches immutable *prepass* work, not the stateful
placement decisions that experiment tried to group. It remains unmeasured.

## 6. Opportunity C: sparse region traffic and an exact next-region order

In [QUALITY construction][S5], C admitted candidates and L merged regions reserve
and clear C*L `MachineQualityTraffic` cells, currently u64. Baseline spill/reload
edits accumulate weighted traffic into these cells. `machine_quality_region_next`
scans a complete candidate row to find the next region, with descending traffic
and ascending region-ID ties. Visiting many regions can therefore do O(C*L^2)
selection work per attempt. This is a source-derived upper bound, not an observed
census or a latency estimate.

Build candidate-local sparse rows when incidence justifies them:

```text
baseline memory edits -> (candidate_slot, region_id, weight)
    -> stable bounded grouping by (candidate_slot, region_id)
    -> sum with the existing exact u64 arithmetic
    -> per-candidate order: descending traffic, ascending region_id
```

Keep the original candidate-slot mapping assigned before heap reordering. Reuse
the resulting immutable rows/order across attempts, resetting iteration position
for each candidate. Absent regions have zero traffic and are never returned by
the existing selector; do not invent entries for them. Do not change candidate
admission, weights, marginal restrictions, retry sequence or split profitability.

With Z nonzero candidate-region incidences, SoA `u32 region_ids[Z]`,
`u64 traffic[Z]`, and `u32 offsets[C+1]` use roughly `12Z+4(C+1)` bytes before
alignment, if Z fits the chosen index width. Checked wider offsets or a dense
fallback are required otherwise. Construction records, sorting scratch and the
baseline edit stream also contribute to peak memory: **12Z is not a peak-memory
claim**. A comparison-sort construction can cost O(M log M), followed by
O(sum z_c log z_c) ordering; using an existing bounded integer-sort path may
reduce that, but must be priced with its temporary storage. A streaming cursor
then chooses the next region in O(1). Preserve u64 totals; do not resurrect the
old u32 weighted-arithmetic problem.

Improved scalar dense rows and sorting only once are the first controls. A
scalar-batched dense maximum reduction can be competitive for small L. AVX2
four-u64 and AVX-512 eight-u64 reductions can process dense rows, provided the
region-ID tie reduction is exact, but still repeat O(L) work if no order is
cached. Sparse scalar lists avoid reading absent regions entirely. SIMD is not
a reason to force sparse rows back into a dense matrix. Predeclare dense/sparse
population cells and include construction, clearing and all retries in timing.
This belongs to #125, not a new issue or a second allocator framework.

## 7. Other query and representation choices

### Sparse/dense live sets and interval precision

The current FAST consumers need conservative last-use/escape information, not a
new complete live-in/live-out matrix. QUALITY adds inclusive touch intervals and
loop extension. Do not introduce lifetime holes, exact next-use lists or full
interference graphs merely to create SIMD work. Those can change admission and
spill decisions or add work to FAST that it currently avoids.

Where an existing consumer genuinely needs a set, compare sorted compact IDs for
very sparse incidence, one u64 for up to 64 local IDs, eight words for up to 512,
and sorted nonempty 64-bit chunks for sparse large domains. A global-to-local
bridge also costs construction, accesses and reset/storage; it is not free just
because the set is compact. Dense AND/OR/AND-NOT and change-detection can use four
words with AVX2 or eight with AVX-512; sparse iteration should touch only nonempty
chunks. Local-ID order and any emitted worklist order must remain deterministic.
The census's size/density bins are diagnostic categories, not proven dispatch
thresholds. No new general live-set subsystem is proposed here.

QUALITY's merged-loop interval closure currently revisits V values per region.
For sorted disjoint regions, endpoint searches are a plausible O(V log(L+1))
replacement: an already-covered interior region cannot extend an interval,
whereas an intersecting endpoint region can. But the recovered #125 experiment
reports a 9.3% one-region Clang regression, has no available original attachment,
and is not a current integrated result. Preserve sentinels, containment,
inclusive endpoints and loop/edge semantics before revisiting that experiment;
do not publish it again as a fresh measured win.

### Overlap, expiration and candidate-register masks

QUALITY already stores at most eight assigned spans per register in separate
start/end arrays. AVX2 can compare eight u32 intervals with
`a <= assigned_end && assigned_start <= b` and reduce only valid lanes; AVX-512
can batch more read-only tests or multiple register candidates. Signed-order
shortcuts are wrong for arbitrary u32 coordinates. Existing capacity limits,
excluded entry/exit rows and earliest-register preference still apply. At these
small bounded sizes, scalar early rejection is a serious competitor.

Static foreclosure queries already have a lazy-built O(1) prefix answer. A
compressed bitmap/rank representation would trade memory for query work; it is
not automatically better. Repeated identical immutable queries can potentially
be reused, but only after counting reuse and preserving lazy initialization on
dirty scratch. Do not build prefix rows for unreachable register classes or
widen the prefix range to the vector file.

FAST expiration can classify multiple held owners together, but its dead test
uses `>` before uses are consumed and `>=` afterwards. [Source][S2] A tied or
fixed-register use must not lose its source early. A cached contiguous death-index
mirror needs maintenance at every bind/evict/transfer site and costs extra writes;
it is a hypothesis, not a free vectorization. The existing masks already avoid
many empty owners. k1-k7 choice is small enough that scalar masks should remain
the baseline. A full next-use list or farthest-next-use victim changes the current
policy and requires a separate generated-code experiment.

### Slots, operand patches and copies

FAST already assigns homes only to memory-edit subjects, reuses nonescaping
scalar slots across defining blocks, and retains dedicated escaping/vector
storage. Preserve vector widths, explicit slot alignment, outgoing areas,
callee-save placement, `incoming_base`, stack probing and edge-copy temporaries.
Exact within-block slot coloring is not part of the same-layout experiment.

`operand_registers` is already a compact output separate from MIR refs. Once
choices are committed, row-local writes can be grouped, but extra gather/rewrite
passes can cost more than the existing four-byte store. Preserve allocation edit
order, especially main edits before retroactive edits at equal points. Copy
subjects are physical registers and rematerialization subjects are immediate IDs;
neither can index the spill-home marking array. [Source][S6]

Parallel-copy resolution is not sequential renaming. Classifying ready copies
in a mask can be batched, but sources must be obtained from the pre-copy state;
cycles and repeated sources require the current safe memory/cycle-break behavior.
An edit at a conditional predecessor can affect multiple successors, so a repair
safe for one edge is not automatically safe for all. The existing conformer
explicitly restricts such repairs. [Source][S2] The older #136 ownership failure
was superseded by #212 according to #125's history; it is not asserted as an
unfixed current bug.

## 8. Parallelism, ABI constraints and the sequential boundary

SIMD across **intervals** batches independent overlap/closure/query observations.
Across **candidate registers**, it produces a legal-resource mask followed by
the same scalar preference selection. Across **independent functions**, allocation
state and stack frames can be disjoint, so reusing the same architectural register
names is not a conflict: ABI call preservation is enforced within each function.

That last observation does not prove the existing compiler's whole function
pipeline is parallel-safe. The lane guide identifies shared signature plans,
source cursors, line suppression and inline-assembly symbols. Those must have
explicit ownership/publication before more functions enter a gang. Reuse
`lane_run`, worker-local arenas and stable source-indexed result slots; perform
required serial prewarm and deterministic merge. No new task queue, per-phase
thread creation or completion-order output is justified. The QUALITY census is
thread-local and its current snapshot is not an automatic all-lane aggregate.

The fixed sequential boundary in these proposals is candidate priority,
register choice, owner/age/dirty transfer, spill decision, split acceptance and
retry selection within a function. Classification, fixed metadata, masked
queries and contiguous updates around that boundary can be prepared in batches.
The proof is not that register allocation is indivisibly sequential; it is that
these particular shared decisions cannot read stale state without a new policy.

Every optimized query must preserve the following resource model. GPR subregisters
and XMM/YMM/ZMM views that alias refer to the same underlying resource; width-
specific writes and upper-lane effects cannot create imaginary independent
registers. Predicate resources stay separate and k0 stays unavailable to ordinary
predicate allocation. Fixed operands, tied use/defs and early clobbers observe
the existing instruction-point ordering. Dynamic inline-assembly descriptors,
implicit effects, call clobbers and target-reserved scratch remain authoritative.
Do not substitute a static opcode summary for instance-specific effects.

Tests must retain incoming fixed-register captures before reuse, call-result
capture before stack restoration, SysV/Win64 vector and aggregate transport,
AArch64 fixed/reserved registers, callee-save preservation, flags across boundary
edits, loop-carried block parameters and sources used only on edges. Inclusive
QUALITY spans cannot be silently changed to half-open row lifetimes to obtain
more pins. These are correctness and policy constraints, not optional quality
tradeoffs. [Machine guide][G1] and [FAST placement][S6]

## 9. Independent allocation checking and validation plan

### 9.1 Test-only checker independent of optimized facts

Add or extend a narrow registered test seam, not a production validation pass
or third-party allocator dependency. Reconstruct expected operand/edge semantics
from authoritative MIR descriptors and original references, independently of the
optimized prepass, budget index and interval tables. Sharing the optimized facts
would make an erroneous fact agree with its own checker.

The proposed checker symbolically tracks each physical resource/alias unit and
stack-byte region, processes edits at their actual machine points, and checks
that every use sees the required value. Handle control flow with a bounded
worklist and a must-information meet; distinguish unreachable blocks from an
empty known state. At joins require facts common to all reachable incoming
paths, after simultaneous edge-parameter substitution. Track equivalence sets
where copies make multiple names denote one value. Mutable redefinitions must
invalidate stale names in other locations. This approach is informed by the
[regalloc2 checker documentation][R2], not copied into the repository.

Explicit checks include fixed/tied constraints and early writes; call-clobber
kills; scalar/vector/predicate width and aliases; rematerialization recipes;
spill/reload sizes and frame bounds; callee-save preservation; and simultaneous
edge-copy sources. Initial ABI state and final return locations are part of the
fixture contract. The existing QUALITY pin validation remains enabled but is
narrower than this proposed end-to-end allocation-dataflow checker.

Negative controls deliberately introduce a conflicting assignment, an early
source overwrite, a missing spill before a call, a stale mutable value, a wrong
class, a truncated vector home, a broken cycle and a repeated-source rename.
The checker must reject each for the intended reason. It does not replace
independent generated-program execution or cross-compiler ABI exchange.

### 9.2 Exact representation and decision oracles

For A, retain the original scalar budget predicate as a test-only oracle. Compare
every whole/split query and all depths after each accepted span, not just the
final success bit. Verify every summary level independently from leaves. Cover
N=0, 1, 63, 64, 65, 4095, 4096, 4097; all-zero budgets; no saturation; fully
saturated rows; first/last set bits; intervals touching at endpoints; cross-word
and cross-level gaps; last valid index; dirty buffers; and every retry.
Use checked index-arithmetic tests near the supported coordinate limit without
requiring a multi-gigabyte allocation just to test a boundary calculation.

For B, compare every prepass array, flag and error outcome, including repeated
vreg lanes, mutable multiple definitions, dynamic asm and edge-only uses. For C,
compare the entire sequence returned by `machine_quality_region_next`, not merely
its set, and preserve the stable pre-heap candidate-slot mapping.

For all, compare ordered placements/edits, pin spans, stack offsets, saved masks,
mode/fallback decisions and byte-identical artifacts against baseline on the
same frozen input. A difference is a failed representation experiment until
explained; a faster run with changed spill policy is not accepted under this plan.

### 9.3 Workload and platform matrix

| Axis | Required cells and purpose |
|---|---|
| Size and shape | Tiny/no candidates; many small functions; large single bodies; long intervals; many disjoint regions; dense/sparse region incidence; all available register pressure regimes |
| CFG | Straight line, diamonds, nested and irreducible loops where supported, backedges, switches/cold paths, parameterized joins, edge-only uses, parallel-copy cycles/repeated sources |
| Constraints | No calls/call-dense; fixed and tied operands; early clobbers; general asm/asm-goto; indirect branches; mutable vregs; rematerialization; arguments/results held across calls |
| Register classes | Scalar-only, scalar/vector mixing, SIMD-heavy compiler source, live vector/predicate pressure, mask absence certificate, k1-k7 exhaustion, class-specific spills |
| ABI/storage | SysV and Win64 cross-compiler calls in both directions; target-appropriate wide vectors/aggregates; AArch64 fixed registers; alignment/large frames/outgoing areas; native supported-platform observers |
| Modes | NONE, MIR_STACK, FAST and QUALITY as actually dispatched at the tested SHA; before/after retirement are distinct baselines, not relabeled historical runs |
| Producer/build | Trusted Clang Release for speed; Clang/GCC/Zig cc/MSVC compatibility; Debug/Release; unity/non-unity; sanitizer/fuzz; baseline/AVX2/AVX-512 producers and scalar fallbacks |
| Hosts | Physical Zen 5 and Zen 4 performance separately; desktop Linux/macOS/Windows at x86-64/AArch64, applicable Android/iOS and UEFI gates for compatibility |

Use existing registered machine/driver/SIMD/ABI tests, `test_all`,
`test_mode_matrix`, and `test_self_host`. Preserve the stage-1/stage-2 byte-identical
fixed point and one-lane/multilane determinism. Run sanitized tests with the
existing failure canaries; preserve compiler/configuration coverage. A compile-
only target row is not a native execution pass. `CI complete` and any separately
required bootstrap evidence are distinct checks under the testing guide.

The throughput harness's frozen own-source jobs currently use FAST. **Selecting
`--mode all` does not make those self-host jobs exercise QUALITY.** Use the existing
mode matrix and correctly scoped own-source/QUALITY validation as separate gates;
do not invent a harness flag or claim coverage from the ordinary series name.
Likewise, do not include dormant `.bbb` fixtures as active source workloads.

## 10. Measurement and acceptance: four separate result families

Freeze source tree, generated headers, trusted producer version/flags, compiler
binary hashes, target/CPU/ABI/PIC/optimization/debug options and exact workload
bytes. Build both subjects and the harness before exclusive measurement; do not
change target defaults accidentally by comparing differently configured
`-march=native` producers. Use the native harness and a new output directory for
each run; no replacement timing runner or historical cross-host baseline.

On a specifically authorized dedicated host, use the established host-wide lease
and verified physical-core/SMT arrangement, A/A qualification and then blocked
paired A/B samples. The lease serializes cooperating runs, not arbitrary host
activity. A successful `qualify` or the default regression guard is not proof of
isolation, a speedup, equivalence, or a low enough noise floor. Invoke the already
built immutable throughput executable during the measurement session: the build
wrapper currently rebuilds the harness before that tool's lease.

| Result family | Report independently |
|---|---|
| Allocation | Prepass, closure, candidate construction/order, legality queries, placement/retries, edit/copy resolution and frame work; distinguish inclusive parents from exclusive/disjoint subphases |
| Full compilation | Uninstrumented fresh-process wall time, CPU time and throughput on the same inputs; per-mode/per-workload distributions, paired uncertainty and outliers |
| Memory | Requested scratch and explicit clear/copy bytes, lifetime/retained arena capacity, peak RSS and page faults; never equate these quantities |
| Generated code | Ordered edit equality; spills/reloads/copies/rematerializations; callee-save set, frame size, output/code bytes; semantic/ABI execution and separately measured program runtime |

Use the existing diagnostic allocation replay and `BUSTER_BENCH_ALLOCATIONS`
census separately from timing subjects. Its current fixed counters and
thread-local scope are documented; verify overflow validity and aggregation
before interpreting data. PMU replays and Callgrind remain separate instruments;
unavailable events are unavailable, not zero. Do not subtract timings of
artificially separated loops to claim causal costs in the original fused prepass.

The original six harness workloads plus frozen own-source FAST cover broad
controls; optional macro/aggregate ABI and focused SIMD/constraint cells supplement
them without silently changing the default statistical family. Retain every
failed, timed-out, inconclusive or regressing case. Existing CI regression margins
are not the acceptance threshold for claiming this optimization faster.

For a representation-only candidate, the initial generated-code criterion is
exact output and allocation-policy equivalence, not a tolerated quality loss.
Require an allocation-time improvement larger than admitted A/A uncertainty on
the targeted cells, no material tiny/default-mode or full-compile regression,
and separately assessed memory behavior. Predeclare numerical performance and
memory margins for the actual experiment and host before looking at its A/B
results; this report does not approve new repository-wide thresholds or amend
#511. A phase win without a useful end-to-end result must be reported as such.

## 11. Bounded integration and stop conditions

**First slice, #125:** use the landed census to establish Q, B, U, function sizes,
query rejection positions and retry populations. Inspect the existing recovery
branches for equivalent work. A weak or tiny population is a legitimate reason
to reject the index and prefer the scalar implementation. No duplicate census,
new allocator abstraction or live-set framework is required.

**Second slice:** implement A behind a narrow internal test seam with the scalar
oracle and bounded hierarchy. Change only the whole/split budget query and
accepted depth maintenance in `register_allocator_quality.c`, plus necessary
existing private tests/census documentation. Keep foreclosure, ordering, packing,
capacity limits, pin-mask painting, cost acceptance and final verification intact.
Publish it as an implementation experiment, not an already-measured improvement.

**Third slice:** after exactness and scalar-bitset measurements, compare
scalar-batched, AVX2 and AVX-512 update/query forms using existing SIMD vocabulary.
Missing operations must earn their integration cost; no broad SIMD vocabulary
expansion is required to test A. Choose or reject dispatch from measured crossovers.
A losing wide variant should be removed, not retained for architectural aesthetics.

B and C remain separately reviewable experiments with existing owners. Candidate
heuristics, lifetime precision, victim selection, slot coloring, split economics,
whole-function vector pins and concurrent speculative commits are excluded.
No backend retirement barrier, required check or validation semantic is weakened.

Unrun gates: all proposed kernel tests, independent allocation checker,
compiler/mode/sanitizer/fuzz/platform tests, self-host fixed points, dedicated
Zen 5/Zen 4 A/A and A/B, PMU runs, phase timing, RSS comparison and generated-program
execution/performance. There are no new numerical speedup, memory saving or
quality results to publish from this session.

## 12. Primary allocator designs consulted

[Wimmer and Mössenböck's optimized linear scan][R1] motivates treating lifetime
holes, use positions and fixed constraints as distinct design choices. Those
choices are not automatically compatible with Buster's conservative facts or
mutable MIR; this report does not import that allocator or its quality policy.

[LLVM's original greedy allocator description][R3] separates the allocation
priority queue from per-register live-interval unions. The transferable lesson
is that resource-overlap queries can be optimized independently of decision
order, not that Buster should acquire LLVM's interval-union framework.

[Fallin's regalloc2 account][R4] distinguishes live-range preparation, bundles,
priority-driven placement and later move insertion, and emphasizes compact data
and efficient implementation. It supports investigating the data-parallel work
around decisions. Its compile-speed and generated-code results are properties
of that implementation and workloads, not predictions for Buster.

[The regalloc2 checker][R2] motivates the independent symbolic dataflow observer
in section 9. Buster's checker must model its actual mutable definitions, edge
parameters, register aliases and point semantics rather than assume a foreign
SSA or register interface. These are primary design references only; no source,
dependency or replacement framework is imported.

## Source and coordination links

[G0]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/AGENTS.md
[G1]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/machine.md
[G2]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/simd.md
[G3]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/benchmarking.md
[G4]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/parallelism.md
[G5]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/agents/testing.md
[H1]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/tools/throughput/README.md
[H2]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/tools/throughput/DEDICATED.md
[H3]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/quality-scratch-census.md
[A1]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/performance-audits/2026-09-12T224432Z.md
[A2]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/docs/simd-backend-research.md
[S1]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_fast.c#L1180-L1560
[S2]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_fast.c#L287-L535
[S3]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_fast.c#L1600-L1800
[S4]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_quality.c#L1-L590
[S5]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_quality.c#L650-L880
[S6]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_fast.c#L2290-L2570
[S7]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_predicate.c#L1-L235
[S8]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/machine.c
[S9]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_quality.c#L880-L943
[S10]: https://github.com/buster14a/buster/blob/5865fa7add27908656aedf28a3c0e6a3de47dc04/src/buster/lib/compiler/codegen/register_allocator_quality.c#L940-L1260
[I125]: https://github.com/buster14a/buster/issues/125
[I131]: https://github.com/buster14a/buster/issues/131
[I36]: https://github.com/buster14a/buster/issues/36
[I125last]: https://github.com/buster14a/buster/issues/125#issuecomment-5624425527
[I125closure]: https://github.com/buster14a/buster/issues/125#issuecomment-5601512484
[I131capture]: https://github.com/buster14a/buster/issues/131#issuecomment-5639996816
[I131correction]: https://github.com/buster14a/buster/issues/131#issuecomment-5648730776
[P522]: https://github.com/buster14a/buster/pull/522
[P568]: https://github.com/buster14a/buster/pull/568
[R1]: https://ssw.jku.at/Research/Papers/Wimmer05/
[R2]: https://docs.rs/regalloc2/latest/regalloc2/checker/index.html
[R3]: https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html
[R4]: https://cfallin.org/blog/2022/06/09/cranelift-regalloc2/
