# Native retirement performance contract

Decision ID: `native-retirement-performance-v1`. The maintainer's explicit
[approval is recorded in issue #511](https://github.com/buster14a/buster/issues/511#issuecomment-5653141597)
on 2026-09-13. This is the numeric and experimental contract for the direct-native
backend retirement in [#36](https://github.com/buster14a/buster/issues/36). It
does not claim that a candidate has passed.

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
  eligibility, and statistical-family membership;
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
  pairs per round, two warmups, blocked AB/BA order, retained samples,
  geometric-mean/exact-code-byte aggregation across both rounds and pooled
  analysis, native-oracle-only runtime and deterministic code-section scope,
  simultaneous one-sided 95% Bonferroni bounds, fail-closed invalid data, and
  the four allowed outcomes.

The validator rejects unknown fields so an unreviewed policy extension cannot
silently change the experiment. It also rejects branch names, `HEAD`,
abbreviated IDs, duplicate artifact paths, path traversal, and evidence digest
mismatches. The canonical support root digest is SHA-256 over the sorted JSON
array of `{name,path,bytes,sha256}` file descriptors using compact, sorted-key
JSON encoding. The requested-work root uses the same encoding over sorted
`{kind,name,path,bytes,sha256}` descriptors.

`support.files[performance_rows]` is a canonical UTF-8 JSON artifact with
schema `buster-native-retirement-performance-rows-v1`, integer version `1`,
the exact `row_identity_fields` list, and an array of rows with contiguous
zero-based IDs. A row contains every identity field plus explicit boolean
eligibility for compiler wall time, peak RSS, deterministic code-section bytes
and generated runtime. Runtime eligibility must name an independent native
executable oracle; code eligibility must name deterministic code sections.
There is intentionally no trusted row-count field: validation parses the whole
array, rejects duplicate identities, recomputes its count and all axes, and
derives the aggregate/slice/cell statistical-family members before comparing
the bound `population` fields. A one-line declaration claiming `999999` rows
cannot satisfy this check.

`requested_work` is schema `native-retirement-requested-work-v1`, version `1`.
Its closure must contain exactly identified items for #508's `inputs`,
`dependencies`, `resources`, `sysroot`, `sdk` and `workloads` categories (and
any additional source/generated/header/tool items), and its `manifest_sha256`
must match the bound #508 manifest. Two dummy entries are therefore not a
closed performance population.

The binding also carries versioned relation and replay receipt artifacts. With
an evidence root, the validator parses the receipts and compares every
commit/tree/source-snapshot/binary/build/toolchain/harness digest to the bound
record, then requires the #510 replay producer, candidate/contract IDs, and
successful identity/evidence replay marker. Without an evidence root the
result is explicitly `proof=structural-only`; it is never an acceptance or
provenance claim.

An evidence bundle must be checked before publication:

```sh
python3 tools/native_retirement_performance_binding.py \
  evidence/performance-binding.json --evidence-root evidence
```

No unbound JSON template is checked in. A record that is not fully populated by
the admitted #508/#509/#512/#437 producers cannot pass this command, and the
command itself does not run measurements or turn a structural/evidence
validation into the approved `pass` outcome.

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
the experiment invalid; no row may be silently dropped or replaced.

The manifest must contain these comparisons where the target and workload admit
them:

- all four public allocator spellings: `none`, `mir-stack`, `fast`, and
  `quality`; `none` compares the direct baseline with the candidate's documented
  MIR-stack compatibility behavior, while the other modes use identical
  spellings on both sides;
- both supported C frontend-lowering configurations and all required
  target/CPU/PIC cells from #508;
- object production, compile-and-link, and the frozen Buster self-host stage-1
  executable build as distinct compiler-latency cells;
- generated code-section bytes for every deterministic object/executable row;
- generated-program runtime only for rows with a deterministic executable
  workload and an independently passing correctness oracle.

Cross-target rows can satisfy compile time, peak RSS, and code-byte obligations,
but never masquerade as native generated-runtime evidence. Emulation, link-only,
object-only, and native execution are separate classifications. Instrumented,
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
| Compiler wall time | Fresh compiler process from launch through successful wait and final requested artifact; paired seconds for identical requested work | Geometric mean of cell ratios, one equal fixed weight per required cell | Upper simultaneous bound at most `1.05` |
| Compiler peak RSS | OS child high-water bytes from the same compile; never reserved bytes, allocation requests, or cross-OS values | Geometric mean of cell ratios, one equal fixed weight per required cell | Upper simultaneous bound at most `1.05` |
| Generated code bytes | Exact sum of executable/code section payload bytes in the deterministic artifact; total file bytes remain diagnostic | Ratio of candidate sum to baseline sum, so each baseline code byte has fixed weight | Exact ratio at most `1.01` in every cell |
| Generated-program runtime | Fresh pinned process executing identical validated logical work; paired elapsed seconds, with workload result checked after every run | Geometric mean of cell ratios, one equal fixed weight per required executable cell | Upper simultaneous bound at most `1.03` |

The primary aggregate upper bound is `1.02` for compiler wall time, `1.02` for
peak RSS, `1.01` for generated code bytes, and `1.03` for generated runtime.
Code bytes are deterministic and use the observed exact ratio rather than a
fabricated confidence interval. If a format cannot identify code sections with
an existing validated parser, that cell is unavailable until the parser is
provided; whole-file size cannot be silently substituted.

Aggregate results are also recomputed separately for every target, target CPU,
mode, frontend configuration, and artifact stage. Every slice must satisfy the
same primary aggregate limit. Wall/RSS cells additionally satisfy their 5%
per-workload guard. Code bytes and generated runtime use their tighter 1% and 3%
limits in every cell. Thus neither an overall mean nor a large workload can hide
a required target, mode, small workload, or stage regression. No positive result
in generated code or runtime offsets a wall-time or memory failure.

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
core. The filesystem/page cache is warm: two unrecorded warmups per variant and
cell precede samples. The contract does not drop caches, mutate firmware or
power policy, or call a warm-cache run a cold-start measurement. Preparation and
warmups remain covered by the whole-job reservation but excluded from samples.

Before any candidate timing is inspected, run A/A with the immutable direct
baseline in both argument positions, including label/path swaps, using two
predeclared rounds of an even 60-to-256 pair count per required cell. Each
two-pair block
contains one AB and one BA pair; the first order and shuffled cell order come
from a recorded positive fixed seed. Reject serial drift, order effects, multimodal
thermal/frequency behavior, or an interval too broad to adjudicate the declared
budgets. #426 owns the concrete empirical admission calculation and its tested
version; it may require a larger fixed sample count, up to the harness limit,
but may not reduce 60 pairs per round or use an odd pair count.

Freeze that sample count and seed before A/B. Run exactly two A/B rounds with
the admitted count, the same blocked order and two warmups. Retain every sample,
warmup status, failure, timeout, cancellation and environment observation. There
is no outlier deletion, best-of-N selection, optional stopping, candidate-driven
sample increase, or rerun-until-green. A new complete job may replace an invalid
job only with both histories retained and the invalidity explained.

## Uncertainty and verdict

For wall time, RSS, and generated runtime, the statistics implementation forms
paired ratios and publishes medians plus one-sided upper confidence bounds. The
complete family comprises every required overall aggregate, target/CPU/mode/
frontend/stage slice, and cell for all three variable metrics. Bounds must be
simultaneous at family confidence 95% using a predeclared Bonferroni correction;
the family and sample count are frozen before A/B. Resampling, if used for an
aggregate, is paired and blocked by round, uses a recorded fixed seed and at
least 100,000 resamples, and resamples whole paired blocks rather than individual
baseline/candidate observations. The existing distribution-free cell interval
is preferred where applicable. Both rounds must independently satisfy every
limit, and the pooled blocked analysis must also satisfy it.

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
never grandfathered or silently excluded.

The MIR-only dispatch candidate is measured before cutover. Direct-only deletion
is then measured again against the same bound direct baseline. Any subsequent
candidate source change before #36 closes requires revalidation of every affected
row; if impact cannot be proven structurally disjoint from the measured path,
rerun the complete family. Limits may be changed only by a new versioned
maintainer decision made before seeing the replacement result. Version 1 must
never be edited after binding to widen a threshold or excuse an observed row.
