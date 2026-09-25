# #882 pre-cutover experiment: off-host preparation and operator handoff

Status: **prepared draft; sampling prohibited**. This packet is scoped to the
pre-cutover candidate that [#522](https://github.com/buster14a/buster/pull/522)
intends to supply for [#513](https://github.com/buster14a/buster/issues/513).
It is not a frozen `native-retirement-performance-v1` binding, an A/A or A/B
observation, an admitted service receipt, or a performance decision. The
machine-readable [preparation record](issue-882-precutover-plan.json) uses the
existing binding, workflow, execution-plan and result-input field names and
schema IDs as projections. Its `null` values and `predeclared=false` are
intentional blockers. Do not submit it to the service or rename it as a binding
record. The [approved contract](native-retirement-performance-contract.md)
forbids checking in an unbound production binding template; the actual strict
record must be produced from authenticated, complete evidence later.

## Authority and observed identities

Read-only observation on 2026-09-25 UTC: protected `main`
`dce1b8caa3d368dede16ad4aa9da6760e26fe704`, tree
`486099e282770e64b0a4e3844e2f39d91fd720d1`. The earlier clean replay
checkout was `850433a06c309195e86b0a4827f11d3e1f16febc`, tree
`a634dffdfad43a8e81da76c9b097f53961acc20f`; a path-limited comparison
confirmed that AGENTS.md and every contract, validator, recipe descriptor,
workflow, export guide and operator packet cited here have identical bytes at
those two commits. A moving `main` or PR branch is never a frozen experiment
identity. Recheck the live refs and exact Git objects before any freeze.

- [#882](https://github.com/buster14a/buster/issues/882) had no assignee,
  comment, preparation PR or matching remote `882` branch before this packet.
  This issue-scoped branch is the sole writer for this packet. No #522, #923,
  #1014, #880 or #881 branch was changed.
- #522 is the designated, still-draft #513 source owner: head
  `be93ed3047044b099872ced1679ad8b795c9ce94`, tree
  `fd6be9eedbc7f145c66437708df006d556fb9596`. This is an **observed
  draft revision**, not an integrated, correctness-qualified candidate. Its
  merge base with observed main is `1d64654d72b259485dbac6ddc36c784fe2bac6c3`.
  Live exact-head checks include failed Buster CI, self-host, throughput and
  rebinding runs; the native-retirement contract workflow passed. The later
  merge-conflict preflight status is failure. The #522 owner must identify a
  reviewed exact integration commit/tree intended for #513 after resolving
  those failures. Read-only coordination found no evidence to freeze this head.
- [#1014](https://github.com/buster14a/buster/pull/1014) is separate, stacked
  #514 deletion preparation: draft head
  `cce86bd108fb402fd8eac86bd1532f87facadb06`, tree
  `4b71f94f5875c18463cbd270d219dea10849fd16`. It is outside #882's
  pre-cutover subject. The accepted final integrated cutover-and-deletion tree
  belongs to [#883](https://github.com/buster14a/buster/issues/883).
- The #510 historical direct oracle is commit
  `3e912a3c5ce9b3e905f3096ff0a0d3e68d80e46e`, tree
  `ffe6f065ec9a4594a18e5f984d621ba28145d247`, archived binary SHA-256
  `1190c010537be78a81dc00b85fa6ca8af5573765a278f8ff52c95318d22f16c1`.
  Release tag `native-retirement-evidence-eb1bef2` resolves to
  `38f150f7affdf458a9b394961709d659cc738bf3`; the checked-in
  [archive contract](performance-audits/evidence/2026-09-12-retirement/archive-contract.json)
  hashes to `ba2772933c95d7e1d5dd16185bff857b2983ad938a863bf00a8ff91407a26806`.
  Its census/strict ZIP SHA-256 values are respectively
  `fe48776d3116f1620e940d132965b306a6714c5f86d3ab1a1ada59413650505b`
  and `0351edbcd30062fb72790559396bf771c17b6364b16a408ee83b7c157dbf9670`.
  The old direct-oracle rebuild was not byte-identical to the archived binary:
  host/resource headers and environment were not closed. This is a durable
  correctness replay anchor, **not** the matched trusted performance baseline.
- [#511](https://github.com/buster14a/buster/issues/511) approved the numeric
  contract, not a result. Current contract bytes hash to
  `67fff9a8b53764792046ba1c1ec104a24cc6e525c4322a6206b218b188a431b0`;
  binding validator `2d3c97e5f31b6af5dc5cdfe7a956b7ea7910fd61a5c71ebab7be29e725ec4d95`;
  result-input verifier `f3d750a65ba63d252ad72236ab56913eee89e67153846a557fbcd8f491f1dbb0`;
  statistics source `72a7c6aa80c46bb4246865a2991b34e2dfbc4ce2db9547d5b69143712383e6c8`.
  The current blocked recipe descriptor has SHA-256
  `2885d160d74ee44eeee3751660773ca19de097fa4359472f2cb7aa11c659cb19`
  and pins the current support declaration SHA-256
  `932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82`.
  #508's 2026-09-17 closure record instead cited
  `aa883c302a326143ff5e2f670bdb5e66dd85162e45033246c40df5f5a3411c21`.
  Reconcile the current declaration, executed #508 population and installed
  recipe with the trusted integration process; this packet does not refresh
  integration-owned bindings.

## Prerequisite matrix

“Implemented” means source exists. “Admitted” needs installed/authenticated
receipts. “Qualified” needs actual 9700X evidence. “Accepted” needs the exact
candidate and full bound experiment. A closed implementation issue alone never
supplies a later category.

| Requirement / owner | Exact observed source or artifact | Present status | Missing receipt or validation | Invalidated by |
| --- | --- | --- | --- | --- |
| Protected request route / #879, #880 administrator | Main above; `.github/workflows/9700x-service-dispatch.yml` SHA-256 `7ac3ab8a7b34ebbead945f8d774b1807bdc887f62ed6cf0f2cb285c638dc2fd9` | #879 software closed. #880 reports one approved rehearsal, job 1, failed `workspace-mismatch` before a transient unit started. | Fresh disabled-state administrator readback, exact workflow/principal/request/approval and runner exclusivity for a later #882 window. | Workflow/policy/runner-group/principal/setting change, or competing executor. |
| Authenticated export / #878; durable replay / #510, #1165 | Main `EXPORT.md` SHA-256 `819a402c00604ace7f401bf43f4f3112d3f4c4757c7bf244df3316f5e079ccc2`; #510 archive identity above; #1165 packet SHA-256 `f6f35927b5024bee01e51b65609d712ea8db71b69d1562bb97f28dd28df45631` | Export software and historical #510 archive completed; no #882 attempt/export. #1165 is open. | Complete same-attempt #878 export, immutable #510 publication, independent download and clean replay receipts. | Exporter/protocol/source, result bytes, publication locator or retention changes. |
| Live smoke, recovery, lease and cleanup / #880, #1162–#1165 operator | #880 operator packet SHA-256 `070e3c733be963e598ac70f60de82b8ca71571369719d0254dbb38df279554b5`; #1159 draft head `a33a1ac33149ccccef6added886218ab838c3bed`, tree `6fd3f7213d3e5d448f405794add5470c026a08a5` | Open. #1159 fixes the reported job-1 failure but is still draft. Five real-systemd scenarios and subsequent new clean reservation are outstanding. | Normally admitted #1159, fresh installation/readback, normal plus four recovery attempts, continuous lease/recursive absence, authenticated export/replay and later clean job. | Installed binary, service/unit/cgroup/lease/host state or source closure changes; unresolved failed attempt. |
| Fixed retirement recipe / #881 integrator, #1018–#1024 | #923 draft head `ad6058af763570c6a12d22dec3fc1aae7d99c592`, tree `d5395192bd5eb2813cf44801539e6bbc70aefde9`; blocked descriptor SHA-256 above | Draft implementation; recipe remains blocked. Exact-head Buster CI failed; native evidence workflow was in progress at read. | A–F real producer handoffs, reviewed inventory/build/correctness/campaign/seal/export capacity, trusted integration, installed recipe identity and admission receipt. | Recipe, worker, collector, statistics, profile or publication code/bytes change. |
| Physical host/noise qualification / #422, #426 and 9700X operator | #422 historical audit `2026-09-12T001148Z`, SHA-256 `0d08719b03aef664245c1923c40449ef29c8a41959af50bd7c1cc0e824f35f4d`; current calibration handoff SHA-256 `7ed3d6e212f822b1a265ceb66ae228b4e4c6d0ce594dec68c4373ced0e4ffd13` | #422 basic qualification closed; #426 empirical A/A decision and physical acceptance remain open. The descriptive calibration does not authorize A/B. | Protected-service, real-host raw A/A and cross-build controls; reviewed versioned noise/resolution policy, PMU availability, host/profile/boot and admitted qualification receipts. | Host, boot, kernel, microcode, firmware, memory, power policy, topology, toolchain, build root or profile drift. |
| Complete support/workload population / #508 and trusted rebinder | Current main support SHA-256 `932fb6e2e8aeb3fdd01409e06b2f58e3b7e09d7d1cf03621e5f98d95172c1e82`; 559 inputs, 411 subjects, 19,728 groups, 78,912 object rows | #508 historic acceptance complete; current declaration is larger than its closure record. | Actual exact-candidate census manifest, inputs/dependencies/environment/rows, independent validator report, performance rows and authenticated eligibility projection; exact installed recipe match. | Any supported input, closure, generated binding, applicability, workload, target/mode/stage or source change. |
| Exact-candidate semantic/census/fixed point / #522/#513 owner and #509 | Historic #509 accepted merge `24263a6ab21aa29c7da197ad7527447738deddb5`, tree `cdc0e38e7b8bc87db8fc14b8bbf3117ad04bdeb4`; #522 draft head/tree above | Historic #509 pass cannot be transferred. #522 exact-head broad CI and fixed-point failed. | Reviewed integrated cutover source/tree, zero fallback, full census and supported semantic/platform gates, output/oracle checks, repeated fixed point and current-head CI. | Candidate/main integration, compiler, fixture, dependency, oracle or generated binding change. |
| Matched subjects and approved plan / #882 operator with #522, #881 and #426 | Historical oracle and current policy pins above; actual direct/MIR source snapshots, Clang/linker/SDK closure, build receipts and binary hashes unresolved | No frozen plan or measured binary exists. | Same-root serial trusted Release unity builds, same-source path controls, exact binary/build hashes, canonical row/family plan, fixed seed/pair count, reviewed pre-sample publication and post-A/A binding. | Any source, binary, toolchain, workload, recipe, profile or predeclared sampling/policy change; start a new attempt and retain the old one. |

The approved [rebinding contract](native-retirement-rebinding.md) makes the
generated source snapshot and aggregate header integration-owned. Neither this
branch nor the #882 operator may hand-edit or refresh them. #522's owner
supplies source intent read-only; the trusted writer supplies a later exact
integrated identity. #882 does not include #1014's deletion tree.

## Plan population and inventory derived from the actual validator

The current declaration has four allocator spellings (`none`, `mir-stack`,
`fast`, `quality`), two frontend forms, two PIC values and twelve target
triples. Its 411 subjects expand to 78,912 object rows. The validator also
requires link and self-host-stage-1 rows: **78,914 is only the current minimum
canonical total**, not the authenticated timing population. Target-specific
applicability, independent oracle results, native runtime and deterministic
code-section eligibility must come from the executed #508 output. Do not copy
the older #923 synthetic `72,672` compiler-eligible count into the final plan.

The admitted result-input schema requires
`sample_row_count × 2 rounds × pairs_per_round` unique `row-round-pair`
numeric records, split into the **minimal** contiguous partitions capped at
16,777,216 records each; total cap 39,518,208. For illustration only, if all
78,914 current minimum rows were sampled, 60 pairs/round require 9,469,680
records, while 250 require 39,457,000; 252 exceed the cap. The contract prose
still calls 254 the maximum based on an older population. The production
validator's current counts and ceiling govern validation; #511/#881 must
reconcile that explanatory sentence before plan freeze without changing
thresholds or dropping rows. The operator derives actual partitions only from
the authenticated rows and chosen admitted even count (60–256 before the
capacity check). A/A observations have their own predeclared #426 inventory.

For each eligible compiler or native-runtime cell, the execution-plan validator
derives **four warmup invocations plus `4 × pairs_per_round` measured
invocations** across two rounds, with fresh process identities and the exact
seeded two-pair AB/BA schedule. Compiler and runtime campaigns remain separate.
The expected invocation count is
`(compiler_eligible_cells + runtime_eligible_cells) × (4 + 4 × pairs_per_round)`;
the service receipt and transcript must agree. No PMU or allocation diagnostic
is a timing trial. The pair count, positive 64-bit seed, 100,000–1,000,000
resamples, entire family size, CPU, row/oracle command/output digests and
partition SHA-256 remain unresolved pending #426, the real service producer
and exact candidate. Stopping is at the fixed predeclared count; there are no
exclusions, outlier deletions, optional stops or selective reruns.

| Evidence group | Complete future inventory |
| --- | --- |
| Four metrics | Per required row/round/pair matched compiler wall seconds and peak RSS bytes; deterministic code-section payload bytes/digests; native generated-program elapsed seconds and checked output only where an independent executable oracle admits runtime. Ineligible facts are explicit null/NA, never fabricated values. |
| Population and correctness | #508 declaration, manifest, inputs, rows, dependencies, environment, independent validator report and canonical performance-row artifact; all requested work/source/SDK/tool closure; exact baseline/candidate source snapshots, build/binary receipts; zero fallback, semantic/platform/census/fixed-point results, admission and per-row code/runtime oracle records. |
| Plan and timing | Frozen `pre_sample_plan`, `result_input_plan`, `execution_plan`; approved A/A data and `post_aa_binding`; every warmup and A/B invocation in ordered JSONL transcript shards; service-authoritative execution receipt obtained independently; numeric raw shards/manifests and complete row/metric sample coordinates. |
| Service validity | Protected workflow/run/job/attempt, requester and review release, request digest, recipe/profile and installed binaries, host/boot/kernel/microcode/topology/power/cpuset, authenticated lease and cgroups, quiet phase, lifecycle journal, cleanup/quarantine and no-competing-executor facts. |
| Sealing and independent decision | `sealed_result` closure/index and all file byte counts/digests; trusted statistics adapter source/toolchain/build input and output, per-cell/target/CPU/mode/frontend/PIC/stage and aggregate round-1/round-2/pooled bounds, code exact guards, raw-to-statistics reproduction, independent `independent_replay` phase and final #511 verdict. |
| Durable transfer | #878 full-result and 1,024-byte export receipt, complete `.bqexport` bytes and digests, every raw/log/manifest file, immutable #510 object/release version, separate publisher and downloader receipts, clean-workspace unpack and replay logs. Retain all failed/partial transfer attempts. |

## Clean-workspace replay: exercised synthetic path and future real path

The preparation check used a fresh detached worktree at
`850433a06c309195e86b0a4827f11d3e1f16febc`, with no host connection,
service submit, reservation, measurement or actual export. On that tree, 15
targeted tests passed in 22.541 seconds and five archive/partition/capacity
tests passed in 0.018 seconds. The tests use clearly synthetic values; the
end-to-end fixture intentionally mocks the real support-output and population
producers and cannot prove admission. The existing validators rejected missing,
duplicate, reordered and extra invocation records; duplicate shard/record
identities; digest and archive mutations; changed/stale plan context;
post-measurement partition descriptors; incomplete canonical population; and
unsafe archive members. A separate ephemeral synthetic mutation joined 732
valid fixture invocations, then rejected a process-instance digest from a
different attempt as `not supervisor-bound` and a changed candidate binary as
`not joined to this job's frozen binding and samples`. These are negative
controls, **not** a performance pass. The exact commands, test names, outcomes
and limits are in the [offline validation log](issue-882-precutover-validation.md).
The clean replay checkout had identical relevant source bytes to the packet's
observed main. No actual `BQEXP001` archive was available, so the production
unpacker and real #510 publication round trip were **not** executed.

When an actual finalized attempt exists, use the exact retained, reviewed
service/replay binaries and immutable Git objects. Get the trusted execution
receipt SHA-256 **out of band** from the authenticated service for that job and
attempt; never take it only from the downloaded bundle. Obtain the sealed
`.bqexport` through #878 and separately download the same bytes from the
approved immutable #510 destination into a new private workspace. Compare
whole-file size/SHA-256 to the authenticated export and publication receipts,
then run the existing producer-agnostic unpacker with the independent receipt
digest:

```sh
bench_service unpack-export result.bqexport /absolute/private/new-result RECEIPT_SHA
python3 tools/native_retirement_performance_binding.py \
  /absolute/private/new-result/performance-binding.json \
  --evidence-root /absolute/private/new-result \
  --repository-root /absolute/immutable/repository-checkout \
  --trusted-execution-receipt-sha256 TRUSTED_EXECUTION_RECEIPT_SHA256
```

The binding path must be taken from the sealed result inventory, not guessed
from a host path. The immutable checkout must contain and resolve every bound
commit/tree and matching contract/validator bytes. The validator streams #615
result inputs, checks the complete population and actual invocation transcript,
rebuilds/replays the #619 adapter and compares its output bytes. Retain its
stdout/stderr/exit and all publication/download receipts. A structural-only
result, successful `unpack-export`, or successful job is not a #511 pass.

## Attempt ledger and later release authority

**#882 actual attempts: zero.** The #880 preliminary job 1
`workspace-mismatch` failure belongs to that service-qualification ledger,
not to this measurement ledger. On the first #882 attempt, append an immutable
record keyed by `(service installation, job ID, attempt token)` and linked to
GitHub workflow run/run-attempt/job, authenticated requester/review, request
digest, pre-sample and post-A/A plan digests, exact subject/build/host/recipe
identities, A/A and A/B raw locations, execution outcome, measurement validity
and reasons, physical qualification, statistical verdict, export/publication
and independent replay. Use separate values for **execution outcome**,
**measurement validity**, **A/A qualification**, **statistical verdict** and
**durable replay**. A transport retry is not another service attempt; a new
service token is. Keep failed, interrupted, invalid, unavailable and
superseded attempts, including partial evidence and cleanup decisions. Do not
overwrite a record or present a later good attempt as the only history.

The designated operator may freeze only after #880's live normal/recovery,
continuous-lease/cleanup and export/replay receipts; #881's exact recipe
admission; #426's real-host A/A policy and qualification; the protected #879
route and #878 exporter; #508/#511/installed recipe identity reconciliation;
and complete exact-candidate #509/census/no-fallback/fixed-point/platform
correctness all pass. The operator then publishes a complete machine-readable
reviewed pre-sample binding/plan with exact source/tree/snapshot, trusted
matched binary/build/toolchain/SDK, workload/family/count/schedule/seed,
service/profile/host/lease and publication identities **before any timing
sample**. Only the protected service may execute. A source, binary, toolchain,
workload, recipe, host profile or plan change requires a new reviewed attempt.

One valid #882 pass would apply **only to its bound pre-cutover candidate**.
[#883](https://github.com/buster14a/buster/issues/883) still requires fresh
acceptance on the final integrated cutover-and-deletion tree; #882 must stay
open until its real evidence and verdict exist.
