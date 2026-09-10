`2026-09-10T010336Z` (source inspection at ef99ec72; compiler validation pending;
**Z07 semantic-query census and constexpr leaf storage candidate (#259)**).

## Baseline, classification and coordination

Baseline main is `ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`.
This is a source-confirmed work-elimination candidate, **not a measured Zen 5
speedup**. The old #259 measurements are historical leads, not measurements
of this revision. No local compiler build or compiler test was run.

Scope was recorded before editing in
[#259](https://github.com/buster14a/buster/issues/259#issuecomment-5611019737).
The read-only [coordination export](https://github.com/buster14a/buster/actions/runs/34423044621)
followed four issue/PR pages (363 records, including 137 open issues), the
one-page open-PR collection (#357 and #362), and both parser-history pages
(109 commits). It includes selected issue comments and those PRs' file
patches, reviews and comments, plus merged #244. Relevant closed records and
local performance-audit text were searched by symbols and symptoms. This is
not a claim to have reviewed every comment on every closed issue. Current
GitHub numbers were not remapped through the Forgejo migration table.

#357 changes separate preprocessing logic, shared test files and frontend
guidance; its changed hunks do not implement this validator. #362 overlaps
the audit index, not parser code. #302 has an active A09 implementation scope
on this same baseline and is deliberately left to that effort. Its cited
recovery commit was not retrievable from GitHub. These records are not locks;
main and open work must be checked again before publication.

## Census and decisions

| Population or query | Current authority and classification | Z07 decision |
| --- | --- | --- |
| Token dispatch | `c_parse_token_class`, well-known symbol IDs and byte shapes already precompute common categories. | Preserve existing dispatch. No measured reason to rebuild a category table. |
| Delimiter matching | `c_parse_position_index_build` and `c_parse_matching_delimiter_indexed` use matched-position sidecars; `c_shape_matching_delimiter` retains scalar and 64-token window paths. | Already implemented. The prior eight-token word trick lost instructions; no changed premise justifies retrying it. |
| Scope queries | `c_parse_scope_for_token` uses sorted child intervals and iterative binary-search descent after merged #244. Binding undo is a separate query in `c_parse_binding_bind`. | Indexed scope work is complete; fresh-name undo scans belong to active #302, not this branch. |
| Interning | `c_symbol_intern` uses compact IDs and keys, linear probing, exact short-key comparison and `c_symbol_middle_equal` for long-name collisions. | Preserve exact identity checks and growth semantics. Batched independent probes remain an unmeasured hypothesis. |
| Aggregate tags | `c_parse_aggregate_lookup_insert` saturates a fixed 16,384-slot table at half occupancy; `c_parse_aggregate_lookup` retains scope-aware fallback. | #291 remains a source-confirmed scaling candidate. No parallel tag table is added. |
| Type compatibility | `c_parse_types_compatible` has an explicit pair stack but no visited-pair set. Repeated function-type DAG pairs can expand again. | #241 remains an unresolved graph-work candidate. A memo table needs its own mutation/qualifier/collision evidence. |
| Constexpr subobject checks | `c_parse_validate_constexpr_declaration` allocates a graph stack and clears a universe-sized visited array even for one leaf. | Selected scalar work elimination under #259. Composite graph queries remain unchanged. |

The newest baseline audit is `2026-09-09T163649Z` (ABI correctness work),
not evidence about these query populations. Relevant prior frontend notes
include `2026-09-05T122006Z`, `2026-09-06T161430Z`,
`2026-09-07T013544Z`, `2026-09-08T235845Z` and
`2026-09-09T011843Z`. The #128 handoff does not establish physical Zen 5
acceptance for its old experiments. Sparse first-hit probes and tiny queries
should retain predictable scalar branches until complete packing, tail,
remapping and teardown costs demonstrate otherwise.

## Smallest complete storage change

For a leaf root, the production validator now uses one local `CTypeId` entry.
Only array/struct/union roots acquire the existing private graph scratch.
Both storage choices feed the same checks in the same order: atomic,
volatile/restrict, complete-object kind, then array bounds or aggregate
members. Pointees are not subobjects. Invalid declaration/storage/type
preconditions retain their diagnostic precedence and source locations.
Touched control flow has one final return and remains nonrecursive.

For `T` types and `M` members the old leaf path requests
`(T + M + 1) * sizeof(CTypeId) + (T + 1) * sizeof(bool)` scratch bytes and
explicitly clears `T + 1` visited bytes. The candidate requests no scratch for
that leaf. These are source-derived work counts, not observed timings or RSS.
Composite queries still have the old universe-sized storage and explicit
visited walk; #259 is **not closed** by this slice. No persistent cache, wider
row, second IR, hash table, SIMD primitive or concurrency is introduced.

The private test-only seam calls the production validator without adding a
reference walk to its hot path. `c_test_constexpr_leaf_storage` checks four
real scalar/pointer declarations with 0, 1, 63, 64, 65, 1,024 and 65,536
unrelated type rows, then returns to the tiny population. It moves through
the public arena allocator to the previous dirty high-water mark before each
query, so scratch rewinds and dirty reuse cannot hide allocations. Optional
`BUSTER_CONSTEXPR_CENSUS=1` reports that query's scratch growth, not total
process memory. Mutation/restoration and invalid type IDs have deterministic
semantic checks; existing C23 negatives retain qualifier and bound coverage.

`tests/basic_c_constexpr_leaf.c` adds C23 scalar, null pointer to volatile,
fixed/inferred arrays, nested aggregate, union and shadowed-local execution
coverage through the existing native four-allocator driver matrix. No new
test runner or performance measurement stack is introduced.

## Predeclared acceptance and current limitations

Require exact diagnostic/output preservation and all applicable Release,
sanitizer, platform, mode and own-source self-host gates on the submitted
SHA. The unmodified compiler source's existing self-host workflow completed
at [run 34423044592](https://github.com/buster14a/buster/actions/runs/34423044592);
its transport-only SHA is not candidate validation. Baseline storage-regression
failure and candidate success must be demonstrated in CI, not inferred from
this description. Independent reference runs must actually support C23
constexpr and must retain warning/error flags.

Use the native `bench_throughput` paired protocol, frozen identical inputs,
separate allocation/PMU diagnostic probes and uninstrumented trusted Clang
compilers. Distinguish implementation throughput from building Buster and
from generated-program runtime. Require identical A/B output for this
representation-only change; retain raw samples, host/affinity/SMT facts,
compiler flags/hashes, phase and whole-compiler time, and RSS. Existing
harness wall/RSS guards remain unchanged. No new persistent allocation is
allowed; new production stack storage is one root ID and bounded control
state. Inspect trusted-compiler disassembly and report any code-size growth
rather than assuming that fewer source allocations proves faster execution.

No physical Zen 5 or Zen 4 hardware, idle SMT sibling, instruction-specific
throughput or usable PMU has been established in this session. Hosted timing
alone cannot fulfill that acceptance contract. Missing final-SHA gates or
representative end-to-end acceptance leave the PR draft. Actual CI evidence
belongs in the linked issue/PR and retained run artifacts; this immutable
entry records the candidate and its predeclared requirements, not a future
success claim.
