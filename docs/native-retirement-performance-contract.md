# Native retirement performance contract

Decision ID: `native-retirement-performance-v1`. The maintainer's explicit
[approval is recorded in issue #511](https://github.com/buster14a/buster/issues/511#issuecomment-5653141597)
on 2026-09-13. This is the numeric and experimental contract for the direct-native
backend retirement in [#36](https://github.com/buster14a/buster/issues/36). It
does not claim that a candidate has passed.

**Amendment A1 (batched native-host sampling).** The maintainer's decision
[recorded in #36](https://github.com/buster14a/buster/issues/36#issuecomment-5887192510)
on 2026-09-29 changes the unit of compiler sampling before any binding record
exists. The timed
population becomes the native-host configurations only
(`x86_64-unknown-linux-gnu` × four allocators × two frontend lowerings × two
PIC modes), and one timed compiler process compiles a whole batch group of
fixtures while a per-input metrics record supplies each fixture's observation.
The same decision adds two explicit policy changes. The batch process wall
time and peak RSS become a gated **batch process metric pair**, whose cells
are object batch groups under the existing compiler limits. That changes the
statistical family's membership, not its construction rule. The code-section
byte gate is also kept on every target, including rows that are never timed.
Paragraphs changed by A1 are marked `(A1)`. The decision ID, every existing
threshold, the two-round/60-pair minimum, two warmups, blocked AB/BA order,
A/A-then-A/B sequence, the statistical estimator, family construction rule,
simultaneous Bonferroni bounds and the four outcomes are unchanged. Every
change that the unit-of-sampling change forces is stated where it applies.

This contract reuses the native throughput harness, the support manifest from
[#508](https://github.com/buster14a/buster/issues/508), semantic acceptance from
[#509](https://github.com/buster14a/buster/issues/509), durable evidence rules
from [#510](https://github.com/buster14a/buster/issues/510), and the admitted
dedicated-host/service work in
[#422](https://github.com/buster14a/buster/issues/422),
[#426](https://github.com/buster14a/buster/issues/426), and
[#437](https://github.com/buster14a/buster/issues/437). The final execution and
verdict belong to [#512](https://github.com/buster14a/buster/issues/512).

## Immutable binding record

A run is outside this contract until its durable bundle contains every field in
this table as a full cryptographic digest or full commit/tree ID. Mutable branch
names, issue text, revision labels, runner labels, and paths are descriptive
only. The repository does not carry an acceptance record until #508, #509,
#512 and #437 have published the identities needed by the final experiment.
This is a fail-closed prerequisite, not a performance result: the checked-in
validator rejects missing, abbreviated, mutable, or `UNBOUND` identities. A
reviewed follow-up must add one complete record and link its immutable commit
from #511; it must not infer or provisionally fill any field.

| Identity | Required value |
| --- | --- |
| Contract source | This file at its final binding commit, plus SHA-256 of its bytes |
| Support/performance manifest | #508 schema/version and root digest; SHA-256 for its declaration, `manifest.txt`, `inputs.tsv`, `rows.tsv`, and canonical performance rows |
| Frozen requested work | Versioned #508 closure with inputs, dependencies, resources, sysroot, SDK and workloads (plus any source/generated/header/tool entries), each with a digest and a manifest link |
| Direct baseline | Full source commit and tree, trusted host-compiler build receipt, executable bytes and SHA-256 |
| MIR candidate | Full source commit and tree, trusted host-compiler build receipt, executable bytes and SHA-256 |
| Measurement implementation | Harness source commit/tree and executable SHA-256, service recipe digest, statistics implementation digest |
| Host/profile | Machine ID, service profile/version, qualification receipt, admitted A/A policy receipt, and supervisor-authoritative lease receipt |
| Provenance/replay | Content-checked commit/tree/source-snapshot/binary/build/toolchain/harness relations and the #510 independent replay receipt |

### Machine-checked binding record

The binding is a JSON object with schema
`buster-native-retirement-performance-binding-v1`. It is deliberately separate
from the support census: the census validator checks one object-census result,
while this validator binds the two subjects and the experiment before any
measurement verdict can be considered. It requires, and when given an evidence
root recomputes, all of the following:

- the full contract source commit/tree and source digest;
- the support declaration, performance declaration, generated `manifest.txt`,
  `inputs.tsv`, `rows.tsv`, and versioned canonical performance-row artifact,
  plus a canonical root digest over that complete set;
- the frozen requested-work closure (inputs, dependencies, resources, sysroot,
  SDK and workloads, with source/generated/header/tool entries as declared by
  #508) with a canonical digest over every item and an explicit link back to
  #508's manifest;
- the recomputed required-row count, unique row identities, allocator,
  frontend, target, CPU, PIC and artifact-stage axes, runtime/code-section
  eligibility, the native-host timed projection and its batch-group
  partition (A1), and statistical-family membership;
- full direct-baseline and MIR-candidate source commit/tree IDs, executable
  bytes/digests, build receipts, dispatch kind, and candidate stage;
- the trusted Clang binary/resource closure and exact Release unity build
  configuration/flags, with sanitizers, profiling, and allocation hooks
  forbidden;
- the harness source/tree and executable, statistics implementation, admitted
  whole-job service recipe, host qualification and A/A admission receipts,
  execution profile descriptor, and supervisor lease receipt; and
- explicit content-checked provenance receipts for commit-to-tree,
  source-to-binary/build, toolchain-to-build, and harness-to-binary relations,
  followed by an independent #510 publication/download/replay receipt; and
- the exact approved thresholds, fixed two-round paired sampling, at least 60
  pairs per round for every timed fixture row, two warmups, blocked AB/BA
  order of batch groups (A1), retained samples,
  #619 median-of-two-pair-block geometric-mean aggregation across both rounds
  and pooled analysis, the batch process metric pair over object batch
  groups (A1), exact code-byte aggregation measured once per variant and
  code-eligible row on every target with a reproduction check (A1),
  native-oracle-only
  runtime and deterministic code-section scope,
  simultaneous one-sided 95% Bonferroni bounds, fail-closed invalid data, and
  the four allowed outcomes.

The validator rejects unknown fields so an unreviewed policy extension cannot
silently change the experiment. It also rejects branch names, `HEAD`,
abbreviated IDs, duplicate artifact paths, path traversal, and evidence digest
mismatches. The canonical support root digest is SHA-256 over the complete
support set (declared files, validator source and six #508 closure artifacts)
as sorted `{name,path,bytes,sha256}` file descriptors using compact, sorted-key
JSON encoding. The requested-work root uses the same encoding over sorted
`{kind,name,path,bytes,sha256}` descriptors.

`support.files[performance_rows]` is the independent #508-produced canonical
UTF-8 JSON artifact with schema
`buster-native-retirement-performance-rows-v2`, integer version `2`, the exact
`row_identity_fields` list, a `sources` map for the #508 declaration, manifest,
inputs, dependencies, environment and validator report digests, and an array
of rows with contiguous zero-based IDs. A row contains every identity field
plus explicit boolean eligibility for compiler wall time, peak RSS (A1: this
unchanged `compiler_peak_rss` bit gates the peak-memory metric),
deterministic code-section bytes and generated runtime. The array remains an
exhaustive one-to-one audit join to every census row. Compiler eligibility is
recomputed from the production validator's authenticated
`applicability_skip_rows`; source-proven non-executed rows retain explicit null
admission/oracle observations and never enter the dense timing schedule,
result-input population, or statistical family. (A1) The row eligibility bits
keep their census meaning, so the artifact and its #508 producer are
unchanged. The **timed projection** is derived, never supplied: a row is timed
exactly when it is compiler eligible and its target is the pinned native-host
target `x86_64-unknown-linux-gnu`, which must also equal the admitted host
profile's native target. Rows outside the timed projection are never timed
and enter no timing schedule, result-input population or statistical family.
Code-section byte eligibility is not limited to the timed projection: every
code-eligible row on every target enters the exact code-byte ratio. Runtime eligibility must name
an independent native executable oracle; code eligibility must name
deterministic code sections. The bound `population.source_digests` must identify
the complete #508 output, including its versioned performance declaration;
rewriting six self-consistent artifacts or omitting the independent validator
report is rejected. The CPU axis retains the census's target-scoped fixture
recipes (including `haswell` and `skylake-avx512`); the manifest CPU is the
fallback, not a requirement that every row use the same profile. Independent
census replay authenticates each row's CPU recipe. The execution plan's numeric
CPU remains the separately admitted host affinity, not a code-generation profile.
Validator report shard directories may be absolute inside the evidence root or
normalized relative paths; replay rejects missing directories and symlinks. When
the report binds independent reference supplements, replay enables the existing
census supplement validator and compares every supplement identity. Direct
reference failures remain retained. A non-object control's source-owned skip
reason remains distinct from its final platform-execution applicability; both
are independently replayed rather than requiring their labels to coincide. Validation parses the whole array, rejects duplicate
identities, recomputes its count and all axes, and derives the aggregate,
slice and cell members for each simultaneous `round-1`, `round-2` and `pooled`
statistical family from the timed projection (A1). The complete audit join
and the twelve-target axis check still use every row. The family also binds
cell counts and canonical identity digests. A one-line declaration claiming
`999999` rows cannot satisfy this check.

`requested_work` is schema `native-retirement-requested-work-v1`, version `1`.
Its closure must contain exactly identified items for #508's `inputs`,
`dependencies`, `resources`, `sysroot`, `sdk` and `workloads` categories (and
any additional source/generated/header/tool items), and its `manifest_sha256`
must match the bound #508 manifest. Each required closure item is joined by
name, byte count and digest to the independent #508 closure and to one
requested-work artifact. Two dummy entries are therefore not a closed
performance population.

The binding also carries versioned relation, source/build, host/service and
replay receipt artifacts. With an evidence root, the validator parses the
receipts and compares every commit/tree/source-snapshot/binary/build/toolchain/
harness digest to the bound record. With `--repository-root`, it additionally
resolves the bound commits and trees in an immutable checkout and checks the
contract and validator source blobs; the #510 bundle digest and replay receipts
must match the downloaded bytes. Opaque self-attestation, arbitrary commit or
tree IDs, or a digest that merely repeats a receipt field is not proof. The
structured #437 receipts must prove supervisor ownership, CLOEXEC isolation
and descendant cgroup cleanup. Without repository or evidence input the result
is explicitly downgraded (`proof=structural-only` or
`proof=evidence-and-receipts-checked-without-independent-git`); it is never an
acceptance or provenance claim.

An evidence bundle must be checked before publication:

```sh
python3 tools/native_retirement_performance_binding.py \
  evidence/performance-binding.json --evidence-root evidence \
  --repository-root /path/to/immutable-checkout \
  --trusted-execution-receipt-sha256 "$TRUSTED_EXECUTION_RECEIPT_SHA256"
```

No unbound JSON template is checked in. A record that is not fully populated by
the admitted #508/#509/#512/#437 producers cannot pass this command, and the
command itself does not run measurements or turn a structural/evidence
validation into the approved `pass` outcome. The pull-request workflow uploads
its bundle for seven days as a diagnostic; that artifact is not the durable
#510 publication and does not bind an experiment or supply a performance
verdict.

The direct baseline is the final integrated, semantically accepted pre-cutover
tree with direct native dispatch still available. The candidate is the exact
tree being accepted, first after the MIR-only dispatch cutover and again after
direct-only deletion. They are separate source commits, built serially by the
same pinned trusted Clang toolchain with identical Release configuration and
host flags. A historical oracle or self-built compiler is not a performance
baseline. The baseline must contain every candidate-independent correctness fix
needed for the admitted manifest; a comparison across unrelated source changes
is invalid.

## Required cells and stages

Every row marked `performance-required` by #508 is mandatory. Each row fixes the
workload and work denominator, target triple, target CPU/features, allocator
mode, frontend-lowering setting, PIC setting, artifact stage, build/link flags,
working directory, environment closure, and correctness oracle. Missing,
unsupported, timed-out, nondeterministic, or incorrectly classified rows make
the experiment invalid; no row may be silently dropped or replaced. (A1) The
correctness census still covers all rows on all twelve targets; the timing and
code-byte obligations below apply to the timed projection.

The manifest must contain these comparisons where the target and workload admit
them:

- all four public allocator spellings: `none`, `mir-stack`, `fast`, and
  `quality`; `none` compares the direct baseline with the candidate's documented
  MIR-stack compatibility behavior, while the other modes use identical
  spellings on both sides;
- both supported C frontend-lowering configurations and both PIC modes on the
  native-host target, with every target-scoped fixture CPU recipe that #508
  assigns to that target (A1; this replaces "all required target/CPU/PIC
  cells from #508" for timing);
- object production, compile-and-link, and the frozen Buster self-host stage-1
  executable build as distinct compiler-latency cells;
- generated code-section bytes for every deterministic object/executable row
  on every target, timed or not (A1: kept by the maintainer's decision);
- generated-program runtime only for rows with a deterministic executable
  workload and an independently passing correctness oracle.

A **native-host configuration** is one (allocator, frontend lowering, PIC)
combination on `x86_64-unknown-linux-gnu`: 4 × 2 × 2 = 16 configurations. A
**batch group** (A1) is the set of timed object-stage rows that share a
configuration and an identical frozen compiler argv apart from the input and
output lists (the same fixture recipe, recipe flags and target CPU). Groups are
derived from the frozen rows, never chosen by the producer. With the current
declaration, each configuration has five recipe groups (`compiler-default`,
`c23`, `c23-dialect-assertions`, `x86-avx512` at `skylake-avx512`, and
`x86-cx16` at `haswell`). A timed link or self-host stage-1 row cannot use the
multi-input `-c` path, so it is its own singleton group whose one process is
that row's requested work.

(A1, replaces "Cross-target rows can satisfy compile time, peak RSS, and
code-byte obligations") Cross-target rows are never timed. They satisfy only
the code-byte obligation, from deterministic artifacts produced outside timing
(see Measurements). They also never masquerade as native generated-runtime
evidence.
Emulation, link-only, object-only, and native execution are separate
classifications. Instrumented,
sanitized, Debug, self-built, or test-embedded compilers remain correctness or
diagnostic evidence and are excluded from performance samples.

Both subjects use a warnings-as-errors Release unity build, tests and benchmark
allocation hooks excluded, no sanitizers or profiling instrumentation, the same
linker and libraries, and the exact CMake cache/build commands frozen by #508.
The trusted Clang version, binary, resource directory, `-march`/`-mcpu` flags,
and generated headers are identical. The native 9700X subject uses the admitted
Zen 5 host-build profile; target CPU/features are explicit row data rather than
inferred from the host.

## Measurements and denominators

For a paired observation `i`, `r_i = candidate_i / baseline_i`; smaller is
better. Zero, missing, overflowed, non-finite, or mismatched measurements are
invalid, not wins.

| Metric | Numerator and denominator | Aggregate weight | Per-workload policy |
| --- | --- | --- | --- |
| Compiler wall time (A1) | Object row: that fixture's per-input wall interval, in monotonic nanoseconds, from its per-input metrics record inside one batch process of identical requested work, paired per fixture. Singleton link/self-host row: the process from launch through successful wait and final requested artifact, as before | Geometric mean of cell ratios, one equal fixed weight per required cell | Upper simultaneous bound at most `1.05` |
| Compiler peak memory (A1, replaces compiler peak RSS as the per-cell metric) | Object row: that fixture's per-input arena high-water bytes (committed bytes, never reserved address space or allocation requests) from the same per-input record. Singleton link/self-host row: OS child high-water bytes, as before. Never cross-OS values | Geometric mean of cell ratios, one equal fixed weight per required cell | Upper simultaneous bound at most `1.05` |
| Batch compiler wall time (A1, new) | Object batch group: the whole batch process of one variant, from launch through successful wait, paired per batch. Singleton link/self-host groups are not cells, because their process is already the row's compiler wall time | Geometric mean of cell ratios, one equal fixed weight per required object batch group | Upper simultaneous bound at most `1.05` |
| Batch compiler peak RSS (A1, new) | Object batch group: the batch process's OS child high-water bytes, from the same process. Singleton groups are not cells, as above. Never cross-OS values | Geometric mean of cell ratios, one equal fixed weight per required object batch group | Upper simultaneous bound at most `1.05` |
| Generated code bytes (A1) | Exact sum of executable/code section payload bytes in the deterministic artifact, measured once per (variant, row) for every code-eligible row on every target; total file bytes remain diagnostic | Ratio of candidate sum to baseline sum, so each baseline code byte has fixed weight | Exact ratio at most `1.01` in every cell |
| Generated-program runtime | Fresh pinned process executing identical validated logical work; paired elapsed seconds, with workload result checked after every run | Geometric mean of cell ratios, one equal fixed weight per required executable cell | Upper simultaneous bound at most `1.03` |

(A1) A fixture's per-input wall interval covers all of that input's work in the
batch: reading, preprocessing, parsing, semantics, canonical IR, backend and
writing its object file. It is not a sum of phase timings. The per-input
record's phase timings are diagnostic, and their sum must not exceed the
interval. Metrics serialization, driver setup and process teardown fall
outside every per-input interval.

(A1) The **batch process metric pair** (batch compiler wall time and batch
compiler peak RSS) gates what per-input records cannot see: process startup,
driver setup, teardown and process-global memory such as lazily grown shared
tables. Both are recorded for every warmup and sample batch, and both variable
metrics are paired by batch in the same blocked order. Their cells are the
object batch groups, and they are statistical-family members under the same
construction rule as every other variable metric. Their figures are never
divided among fixtures or folded into per-fixture samples, and per-fixture
figures are never summed into them. Startup, reported as process wall time
minus the sum of the per-input intervals, remains diagnostic. A timed batch
whose process metric is missing, zero or inconsistent with its per-input
intervals is invalid.

(A1) Code bytes are deterministic, so the independent code-section reader parses
one frozen artifact per (variant, code-eligible row) outside timing. The bound
subject binary produces that artifact with the row's frozen argv; it may be the
artifact that the service's correctness run already produced for that variant
and row. For a timed row, every warmup and sample batch must then reproduce, for
each of its fixtures, an object byte-identical (same SHA-256) to that frozen
artifact. For a row that is never timed, one further independent compile by the
same subject binary must reproduce it byte-identically. Untimed rows may be
compiled in untimed batches of the same form (one configuration and frozen
argv per batch, multi-input `-c`, frozen order). Each input's object in such a
batch is that row's artifact, and that input's frozen oracle status and
diagnostics must still match. The first mismatch makes
the experiment invalid for nondeterminism. Code-byte observations are no longer
repeated in every pair.

The primary aggregate upper bound is `1.02` for compiler wall time, `1.02` for
peak memory (A1: formerly named peak RSS), `1.02` for batch compiler wall time
(A1), `1.02` for batch compiler peak RSS (A1), `1.01` for generated code bytes,
and `1.03` for generated runtime.
Code bytes are deterministic and use the observed exact ratio rather than a
fabricated confidence interval. A deterministic zero-byte candidate code
payload is retained as the integer observation `0` and is valid against a
positive baseline denominator; no padding or positive-value fabrication is
permitted. A zero baseline code payload has no ratio denominator and is
retained as evidence but excluded from the code-ratio population. If a format
cannot identify code sections with an existing validated parser, that cell is
unavailable until the parser is provided; whole-file size cannot be silently
substituted.

Aggregate results are also recomputed separately for every target, target CPU,
mode, frontend configuration, and artifact stage. Every slice must satisfy the
same primary aggregate limit. Wall/memory cells, and batch wall/RSS group
cells (A1), additionally satisfy their 5% per-workload guard. Code bytes and generated runtime use their tighter 1% and 3%
limits in every cell. Thus neither an overall mean nor a large workload can hide
a required target, mode, small workload, or stage regression. No positive result
in generated code or runtime offsets a wall-time or memory failure. (A1) Over
the timed projection, the target dimension has the single native-host value.
That slice is kept rather than removed, so the family construction rule stays
unchanged. The batch metric pair uses the same slice dimensions; its target
and stage slices each have a single value (native host, object stage). Code
bytes keep all twelve target slices.

The requested-work denominator comes from #508 and is immutable. Reports retain
both requested and actually performed source bytes, preprocessing tokens,
translation units, definitions/functions, and workload operations where
available. Throughput rates are diagnostic; the decision uses matched ratios and
never changes denominator because one compiler skipped work.

## Host, warm state, and paired sampling

Only the admitted physical Ryzen 7 9700X service/profile may issue an acceptance
verdict. The service owns the whole host from before source materialization and
build through cleanup and durable publication, enforces one job, and records
boot/microcode/kernel/firmware, memory, power/governor, topology, cpuset/cgroup,
thermal, selected-core and idle SMT-sibling facts. The binding protocol is
`server-authoritative-supervisor-lease-v1`: the server/supervisor owns the lease
and candidate cgroup, the harness validates the lease, and the harness sets
CLOEXEC before launching a candidate child. The candidate must not be able to
open or inherit the lease handle. Server-side cgroup and descendant cleanup is
required even after cancellation or a failed child. A cooperative lock,
affinity, absence of a hypervisor flag, or a runner label alone is insufficient;
the current cooperative file lease is not an admitted acceptance service. Any
missing required fact, overlapping work, frequency/power drift outside the
admitted profile, or incomplete process cleanup invalidates the affected run.

Builds, downloads, correctness tests, profiling, PMU and allocation probes occur
outside timing trials. Timed compiler and generated-program children are fresh
processes pinned to the predeclared logical CPU on an otherwise idle physical
core. (A1) A timed compiler child is one batch: a fresh process of one variant
that compiles every fixture of one batch group. The filesystem/page cache is
warm: two unrecorded warmups per variant and batch group (A1; formerly "and
cell") precede samples, and each warmup batch compiles the whole group. The
contract does not drop caches, mutate firmware or
power policy, or call a warm-cache run a cold-start measurement. Preparation and
warmups remain covered by the whole-job reservation but excluded from samples.

(A1) A batch uses the existing multi-input `-c` path: each fixture is its own
translation unit and object. It runs with one compilation worker (no
`-fcompile-jobs`), processing inputs serially in the frozen order: ascending
canonical row ID for timed members, followed by any registered
rejection/diagnostic controls of that group. Under a continue-on-failure
mode, one input's rejection does not stop the batch. The batch writes one
per-input metrics record for every input (`-fmetrics-out`) with its status and
diagnostic digest, monotonic per-input interval and phase timings, arena
high-water bytes, and per-section and per-function code bytes. Each input's
status and diagnostics must equal its frozen oracle, and per-input intervals
must be ordered, non-overlapping and inside the process interval. A control in
a batch is status-checked evidence, never a timed cell. Its presence and
position are frozen with the group. Batch composition, input order and argv
are part of the frozen execution plan.

Before any candidate timing is inspected, run A/A with the immutable direct
baseline in both argument positions, including label/path swaps, using two
predeclared rounds of an even 60-to-256 pair count per required cell. (A1) The
scheduled unit is the batch group. Each A/A or A/B batch pair yields exactly one
pair for every timed fixture in that group, so every fixture cell still has the
full pair count in each round. Each two-pair block contains one AB and one BA
pair; the first order and shuffled batch-group order (A1; formerly "cell
order") come from a recorded positive fixed seed. Reject serial drift, order
effects, multimodal
thermal/frequency behavior, or an interval too broad to adjudicate the declared
budgets. (A1) The batch metric pair is a family member, so its object batch
group cells face the same A/A drift, order, multimodality and precision
checks. #426 owns the concrete empirical admission calculation and its tested
version; it may require a larger fixed sample count, up to the harness limit,
but may not reduce 60 pairs per round or use an odd pair count.

Freeze that sample count and seed before A/B. Run exactly two A/B rounds with
the admitted count, the same blocked order and two warmups. Retain every sample,
warmup status, failure, timeout, cancellation and environment observation. There
is no outlier deletion, best-of-N selection, optional stopping, candidate-driven
sample increase, or rerun-until-green. A new complete job may replace an invalid
job only with both histories retained and the invalidity explained.

### Batched campaign capacity (A1)

Let `G` be the number of batch groups: object groups plus singleton link and
self-host stage groups. Let `U` be the number of runtime-eligible timed rows,
`N` the number of result-input rows and `P` the frozen pairs per round. Each
of the A/A and A/B collections then launches `G * 2 * (2 + 2P)` compiler batch
processes and `U * 2 * (2 + 2P)` runtime processes, and writes `N * 2P`
row records plus `B * 2P` batch records, where `B` is the number of object
batch groups. At `P = 60` that is `244 G` batch processes per collection and
`488 G` across A/A and A/B.

These are estimates, not an admitted budget. The 16 principal
`compiler-default` groups give `488 * 16 = 7,808` batch processes. On the
measured machine class, one process compiling 369 host fixtures takes about
1.1 s, so these take about 8,600 s (≈ 2.4 h). The current declaration's
other four recipe groups per configuration add 64 groups of one to four
fixtures, or `488 * 64 = 31,232` short processes. At about 34 ms of x86
process startup each (#1295), they add roughly 0.35 h. Singleton stage groups
add `488` processes each, and the runtime campaign adds `488 U`. The
native-host numeric population is at most `(6,576 + stage rows) * 2P` records
per collection (6,576 = 411 subjects × 16 configurations, before authenticated
skips), which is one #615 partition at any admitted `P`.

The fixed one-hour worker budget no longer applies to this recipe. It is
replaced by a reviewed budget bound into the admitted service recipe. That
budget is computed from a measured upper bound per batch (by group size) and
per runtime process, multiplied by the counts above, plus the authenticated
fixed-phase bounds. Preflight rejects the job before any timing if the
reviewed budget cannot hold the derived counts. The untimed cross-target
code-byte artifacts (A1) cost two batches per variant and untimed batch
group, outside timing but inside the whole-job reservation and budget.

## Uncertainty and verdict

For wall time, peak memory (A1: formerly RSS), and generated runtime, the #619
statistics implementation forms candidate/baseline ratios and publishes
medians plus one-sided bounds. (A1) Wall time and peak memory cells remain
timed fixture rows, whose observations come from per-input records. Batch
compiler wall time and peak RSS (A1) are the fourth and fifth variable metrics.
Their cells are object batch groups, and their observations are the batch
processes' own figures, paired by batch. A fixture's pair `(round, pair)` is its batch group's
batch pair `(round, pair)` and inherits that batch's AB/BA orientation.
Each cell is grouped into adjacent two-pair AB/BA blocks; each block statistic
is the equal-weight geometric mean of its two paired ratios across the member's
cells. Fixtures in one batch share processes, so their ratios can be
correlated. The existing whole-block bootstrap, which uses the same draw for
every cell, keeps that correlation, and blocks remain separate processes in
time, so the estimator and interval are unchanged. The round and pooled
estimates are medians of those block statistics. Bootstrap members resample
whole paired blocks (the same draw for every cell),
while exact cells use the defined distribution-free block-median interval. The
complete family comprises every required aggregate, approved target/CPU/mode/
frontend/stage slice, and exact cell for every variable metric (A1: five,
formerly three). One logical
scope-free member is assessed exactly once by `tp_retirement_assess`; that call
emits round-1, round-2 and pooled bounds. There are no separate per-scope calls.
Bounds are simultaneous at family confidence 95% using the predeclared
Bonferroni correction. Aggregate/slice members use 100,000--1,000,000 bootstrap
draws; the immutable pair count is even and bounded to 60--256 per round.
Both rounds must independently satisfy every limit, and the pooled blocked
analysis must also satisfy it.

## Result-input and replay workflow

The workflow is four explicit, non-verdict phases: a pre-sample plan frozen
before measurements, a post-A/A binding, a sealed result, and an independent
replay. Admission and oracle records are content-checked execution facts; the
validator derives metric eligibility from their status, native-target,
artifact, code-section and oracle fields. A caller-supplied eligibility bit or
`clean`/`success` receipt is not accepted.

Typed result records are joined to the canonical performance population by the
frozen `row-round-pair` ordinal. Each manifest is a contiguous partition and
each record is streamed through the existing #615 schema-2 verifier. The
canonical partition count is the minimum needed at the immutable 16,777,216
record cap, with full-cap partitions before the final partition. Before samples,
the immutable plan declares only each partition's identity, path, start and record
count. After measurement, the sealed result binds the manifest and shard byte
digests and input-byte counts. This avoids a digest cycle while bounding memory and proving global
disjointness and completeness, including stage rows and every metric that is
eligible for a given row. Ineligible metrics are explicitly absent/NA, never
filled with fabricated positive observations. The immutable experiment-wide
ceiling remains 39,518,208 records in at most three partitions. Because the full
population also includes the required link and self-host stage rows, 254 is the
maximum fitting even pair count (rather than 256); a lower predeclared count in
the approved 60--254 range remains valid and no supported row is dropped.

(A1) The result-input population is the timed projection: every timed row
with at least one sampled metric, in ascending canonical row ID. The
coordinate schema, the `row-round-pair` record identity and the linear ordinal
`(row_ordinal * rounds + round) * pairs + pair` are unchanged, and each row
still has `rounds * pairs` records. Only the dense row ordinal is renumbered
over the timed projection. A record's `(round, pair)` is its batch group's
batch coordinate (object rows) or its own process coordinate (singleton
stage rows and runtime). Records carry `compiler_wall_time`,
`compiler_peak_memory` and, where eligible, `generated_runtime`. They no
longer carry `generated_code_bytes`. The sealed result instead binds one
code-byte record per code-eligible row on every target, holding the frozen
baseline and candidate code-section bytes and digests and the reproduction
digest.

(A1) The batch metric pair has its own result-input population: every object
batch group, in ascending group ordinal. It uses the same coordinate schema,
with record identity `group-round-pair` and the linear ordinal
`(group_ordinal * rounds + round) * pairs + pair`. Each record carries
`compiler_batch_wall_time` and `compiler_batch_peak_rss`, and its
`(round, pair)` is the batch coordinate. It is streamed through the same #615
verifier as its own contiguous partition set, and the sealed result binds its
manifest and shard digests. The code-byte summary is computed
from those records. The native-host population is far below the 16,777,216
record cap (one partition at any admitted pair count). The 39,518,208-record
ceiling, the three-partition bound and the 254-pair collection maximum are
kept as immutable limits rather than widened.

The sealed closure enumerates the complete support/requested-work, subject,
producer, measurement, host/service, provenance, admission/oracle,
pre-sample/post-A/A, result-input manifest/shard, raw measurement, adapter
input and adapter result artifacts, plus (A1) every batch's per-input metrics
artifact and the code-byte record set. The outer sealed-result record binds that
closure root (it is excluded from its own digest to avoid a cycle). Independent
replay verifies a deterministic archive manifest, extracts a separate immutable
root, reconstructs the adapter input from that root, and executes the reviewed
`bench_throughput retirement-replay` C path from the checked-in transitive
source closure (`throughput.c`, `shared.c`, `retirement_stats.h`, and every
repository-local header/source reached by their include graph). The fresh
adapter executable is rebuilt with a bound compiler identity/version digest and
exact compile recipe. Its binary digest is a local replay receipt rather than a
portable cross-host invariant; the source closure, toolchain identity and
compile command are the reproducible identity. Its fresh output must byte-match
the downloaded adapter result.
The independent phase also carries a content-checked #510 publication/download
receipt for this exact sealed-result digest and archive digest (publisher,
release, run, service, and replay identity). The receipt is linked to the
phase rather than inserted into the sealed archive, avoiding a self-referential
archive digest. Structural phase completion is evidence of workflow shape only;
it is never a performance verdict and cannot accept retirement.

Verdicts are fail-closed:

- **pass**: all identities and rows validate, all correctness oracles pass, both
  rounds and the pooled analysis have upper bounds within every applicable
  limit, and exact code-byte limits pass;
- **regression**: a valid required result has a lower simultaneous bound above
  an applicable limit, or deterministic code bytes exceed a limit;
- **inconclusive**: valid data do not establish pass or regression, including an
  upper bound crossing a limit, round disagreement, or insufficient precision;
- **invalid**: identity, manifest, host, service, correctness, completeness,
  determinism, collection, or evidence validation fails.

Only `pass` accepts retirement. `inconclusive`, “no confirmed regression”, a
shared-CI guard success, or a point estimate below a limit is not acceptance.
A valid point estimate above a limit whose interval still crosses it remains
inconclusive, not a conveniently declared regression; engineering may still
choose to repair it before another fully predeclared run.

### Per-invocation execution evidence

Numeric row/round/pair records establish neither actual execution order nor
successful warmups or runtime oracles. Evidence-mode validation therefore
requires a separate execution plan, a supervisor-owned invocation transcript,
and an **independently obtained execution-receipt SHA-256**. The caller supplies
that digest through `--trusted-execution-receipt-sha256` (or the Python API's
`trusted_execution_receipt_sha256` argument). Obtain it from the authenticated,
admitted control service for the exact job/attempt. Never compute the trusted
value from the downloaded result and feed it back into the validator: that
would reintroduce self-authentication. SHA-256 provides an integrity link to
that external trust root, not hardware attestation or proof that a publisher
is trustworthy.

Both `pre_sample_plan` and `post_aa_binding` carry the same `execution_plan`
artifact descriptor. Its schema is
`buster-native-retirement-execution-plan-v2`, integer version `1`. It binds
`schedule=tp-retirement-block-schedule-v2`, the full uint64 seed, two rounds,
pairs per round, two warmups per variant, the logical CPU and native target from
the admitted profile/qualification/A/A receipts, the canonical performance-row
digest, and exactly one contract per canonical row. Each row contract binds its
canonical identity and independent oracle record. Baseline and candidate each
bind compiler-command and deterministic output-artifact SHA-256 values,
deterministic code-section digest/size, and the native runtime command and
oracle-output digest where eligible. Inapplicable fields are explicitly null;
callers cannot remove an applicable metric by changing an eligibility flag.
Native-runtime applicability is derived from the frozen row execution obligation,
artifact stage and admitted native target. A native-capable link/self-host row
cannot be relabeled `not-applicable`; object-only and non-native-target rows
cannot be relabeled as native runtime evidence.
Command digests identify the frozen installed recipe's canonical argv, working
directory and environment contract, not a label chosen after measurements.
The admitted producer must supply and preserve those exact command identities.

(A1) The batch unit requires a new plan version: schema
`buster-native-retirement-execution-plan-v3`. In addition to the fields above,
it binds the pinned native-host target and exactly one contract per batch
group. A group contract holds the group ordinal, its configuration and recipe,
the ordered timed member rows and any frozen controls, and, per variant, the
batch command digest (canonical argv including every input and output path,
the continue-on-failure and `-fmetrics-out` options, cwd and environment) and
the expected batch exit status. For a batched object row, the row contract's
compiler-command digest is its group's batch command digest. Its
output-artifact digest remains that fixture's own object, and its code facts
remain the once-parsed frozen artifact. Rows outside the timed projection carry
explicit null timing fields. Where code eligible, they bind the frozen
artifact, its reproduction digest and its code facts; otherwise their code
fields are null too. Each control binds its expected status
and diagnostic digest.

The schedule is the versioned #619
`tp_retirement_block_schedule(seed, round, block, cell_count, ...)`: SplitMix64,
domain-separated seed coordinates, unbiased bounded draws, complementary AB/BA
orientation within each two-pair block, and separate Fisher-Yates cell shuffles
for its two pairs. It is **not** the ordinary shared-CI runner's 32-bit schedule.
Compiler invocations form one serial campaign over the batch groups of the
timed projection (A1; formerly "over all canonical rows"): its `cell_count` is
the group count `G`, and a group's dense position is the ascending order of its
smallest member row ID. Eligible native-runtime invocations form a second
serial campaign. Each campaign first performs two warmups per variant in
ascending dense order (groups for compilation, rows for runtime), then the
frozen two-round blocked schedule. The schedule function itself is unchanged.
The runtime campaign uses a dense ordering of the runtime-eligible timed rows.
PMU/allocation diagnostics are not invocations in either timing campaign.

The sealed result bundle carries an `execution_receipt` artifact with schema
`buster-native-retirement-execution-receipt-v1`, integer version `1`. The receipt
contains the execution-plan digest, job/attempt/boot identities, the monotonic
pre-sample binding and completion times, exact invocation count, and ordered
`{path,bytes,sha256,records}` transcript-shard descriptors. Its `context_sha256`
is the canonical JSON digest defined by `_execution_context`: pre-sample and
post-A/A phase digests, support root, both subjects, measurement and execution
identities, admission/oracle digests, and the streamed numeric-measurement
digest. It deliberately excludes the receipt itself, result seal and future
independent replay, so there is no hash cycle.

Every canonical UTF-8 JSONL invocation record includes its global sequence,
compiler/runtime kind, warmup/sample phase, canonical row, round/pair or warmup
index, pair position and variant; process ID, a supervisor-observed boot-scoped
process-start token, their canonical process-instance digest, CPU, monotonic
start/end; exit code, signal, timeout and cancellation status; executable,
command and output digests; deterministic code-section digest/size; wall
seconds and compiler peak-RSS bytes. Fields not meaningful for that invocation are null. Compiler
and runtime executable identities are checked separately. Nonzero exits,
signals, timeouts/cancellation, overlapping or out-of-window invocations,
missing/duplicate/extra records, schedule deviations, reused or mismatched
process instances, execution on a CPU other than the admitted logical CPU,
wrong outputs, failed or non-native runtime oracles, and mismatched numeric
measurements reject before statistics replay. PIDs may be reused after exit,
so uniqueness is defined by the supervisor-bound job/attempt/boot/PID/start-token
digest rather than by PID alone; one process instance may execute exactly one
warmup or measured invocation. Process wall seconds must match the recorded monotonic
interval within one nanosecond of decimal-serialization error. Warmups cannot
substitute for samples. A numeric sample must equal its authenticated process
observation; a positive number alone is insufficient.

(A1) A compiler invocation record describes one batch process. It names its
batch group instead of one canonical row, and its wall seconds and peak-RSS
bytes are the batch process metrics. Each batch metric sample must equal the
authenticated observation of its batch process. Its output digest is the SHA-256 of the
ordered per-input object digests, and it adds the `{path,bytes,sha256}` of that
batch's per-input metrics artifact. Its code-section fields are null because
code is checked per input against the frozen artifact. The validator streams
every metrics artifact, whose records are bounded to 1 MiB each (the #615
record bound), with each artifact at most 64 MiB. It then requires:
exactly the frozen inputs in frozen order; each status, exit contribution and
diagnostic digest equal to the frozen oracle; per-input intervals ordered,
non-overlapping and inside the process interval; phase-timing sums no larger
than their interval; and each emitted object byte-identical to its frozen
artifact. A timed member's numeric wall and peak-memory sample must equal its
authenticated per-input record inside an authenticated batch process.

Transcript parsing is streamed, bounds each line to 8,192 bytes, limits receipt
JSON to 1 MiB before loading it, and allows at most 4,096 transcript shards.
The validator opens the trusted receipt once, bounds and hashes those exact
bytes against both its descriptor and the independently supplied trust root,
and only then parses those same bytes; a concurrently substituted path cannot
supply one receipt for validation and another for sealing. It derives the exact
total invocation count from the complete population and sampling policy (A1:
`(G + U) * 2 * (warmups + rounds * pairs)` for each of the A/A and A/B
collections, where `U` is the number of runtime-eligible timed rows), and
hashes the same transcript bytes that it consumes. Extra shards do not increase that
population or change the existing 39,518,208 numeric-record ceiling. The plan,
receipt and every transcript shard are members of the durable sealed closure
and independent archive verification. Missing trusted receipt input fails
closed; successful invocation verification is reported separately as
`invocations_checked` and does not itself establish performance acceptance.

This defines the **validator-side receipt contract**, not an assertion that the
9700X deployment already emits it. The installed trusted producer must collect
these observations as execution happens, freeze the command/oracle plan before
sampling, and publish the authoritative receipt through the admitted service.
A post-hoc converter from numeric sample rows cannot reconstruct missing
warmups, process outcomes or actual order and is not an acceptable producer.
Service integration, physical qualification, real A/A–A/B collection and an
independent replay remain required under #437/#512. Synthetic regression
receipts test rejection and joining; they are never deployment evidence.

## Revalidation and durable evidence

The complete raw bundle, commands, source/caches, binaries, inputs, outputs,
manifests, samples, code-section records, oracle results, host observations,
statistical-family declaration, implementation and result summaries must pass
the #510 independent publication/download/replay contract. Summary prose is not
the evidence.

Any byte change to the bound contract, #508 manifest or closure, requested
inputs, baseline/candidate/harness/toolchain binaries, service recipe,
statistics implementation, build flags, target/CPU/mode/stage matrix, oracle,
host profile, microcode, firmware, kernel, memory or power policy invalidates the
affected acceptance and requires fresh A/A admission plus the complete affected
A/B family. Adding a required row reopens the whole statistical family; rows are
never grandfathered or silently excluded. (A1) Changing batch-group
composition, input order, controls or the batch argv changes the frozen plan
and likewise requires fresh A/A admission and the complete affected A/B
family.

The MIR-only dispatch candidate is measured before cutover. Direct-only deletion
is then measured again against the same bound direct baseline. Any subsequent
candidate source change before #36 closes requires revalidation of every affected
row; if impact cannot be proven structurally disjoint from the measured path,
rerun the complete family. Limits may be changed only by a new versioned
maintainer decision made before seeing the replacement result. Version 1 must
never be edited after binding to widen a threshold or excuse an observed row.

## Eligibility replay regression (#929)

The optional full-census regression consumes an extracted, independently
hash-verified census ZIP. It relocates only report path metadata, invokes the
production schema-2 validator with its original gates and supplement mode,
compares every non-path field and all three applicability/residual files, and
checks every compiler schedule coordinate using the approved 2 warmups and
2 rounds of 60 pairs. It never executes or fabricates performance measurements.

```sh
BUSTER_RETIREMENT_CENSUS_ROOT=/absolute/path/to/extracted-archive \
BUSTER_RETIREMENT_RECORDED_ROOT=/home/runner/work/buster/buster/candidate \
python3 tools/native_retirement_performance_eligibility_test.py -v
```

On 2026-09-22, artifact `10621310872` from run `35555863719` was downloaded
and verified at 514,260,140 bytes, SHA-256
`406c799dea5650ac49070e2493343868abc89c45a16873609a59986525c764fd`.
It records compiler source `351542e826cae7ff1ebe8bfab8e0fb69b5e37446`;
this is historical census evidence, not a measurement of current main.
Independent replay retained all 78,912 rows, including all six registered
controls / 1,152 rows. The compiler projection contains 72,672 eligible and
6,240 untimed rows; its complete schedule has 17,731,968 invocations. CPU
profiles are `baseline`, `haswell`, and `skylake-avx512`. (A1) That
invocation count describes the superseded per-row schedule and stays here as
historical evidence. Under A1 this regression must additionally derive the
native-host timed projection and its batch-group schedule from the same
replayed census.

The archive also contains a real empty-code object: row 20737,
`tests/basic_c_driver.c`, `x86_64-unknown-linux-gnu`, `mir-stack`, group 5184
in shard 0. Its file is 2,216 bytes with SHA-256
`09c7f0f0c3eb38de3947117f62ccd424e9599434876ae2d6015d02aa22c6cfc5`;
`readelf -SW` independently reports `.text` size zero. Object-file size is
therefore not a substitute for the code-section denominator.

This regression proves full-census compiler scheduling and census replay. It
does not cover the complete service-produced object/link/execute performance
bundle, supervisor transcripts, or a qualified-host performance verdict. Those
require the remaining #881/#923 producers and #512 execution; do not label this
diagnostic as that end-to-end acceptance evidence.
