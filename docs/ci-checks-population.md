# Prospective standard-hosted checks comparison

Issue [#2610](https://github.com/buster14a/buster/issues/2610) owns this
measurement design. It supplements the [checks partition contract](ci-combination-shards.md)
with the maintainer-approved prospective hosted-population comparison; the
historical exact-nine reader and cohorts retain their original meanings.
Resolving the design gate does not establish performance acceptance for
[#2119](https://github.com/buster14a/buster/issues/2119) or
[#2120](https://github.com/buster14a/buster/issues/2120).

## Population and interpretation

Standard labels select OS, architecture and resources, but the accepted
workflow offers no CPU-feature or assigned-VM selector. The actual successful
A1/B1 observations at `d69fd7e8e8fe88e1c668da2c045a2d9d36eca446` authenticate
different native profiles and assertion populations. A repeated or matched
later sample cannot repair their original nine-slot epoch.

The prospective experiment compares complete deployed policies on the actual
standard-hosted assignments during one sequential campaign. It retains every
observed native profile and executed assertion; it does not replace their
counts with a common workload. Numerical results concern that finite hosted
population, with its observed background demand. They do not establish a
CPU-independent scheduling mechanism, physical CPU identity, merge-queue
latency, or a guarantee for future images or CPU/load mixtures. Randomization
does not guarantee balanced profiles or adequate precision. Population
imbalance and timing uncertainty require a recorded disposition alongside
resource/deadline/capture/cleanup/reliability review.

## Closed native census catalogue

[The source review](ci-checks-native-census-review.md) binds the immutable
[catalogue](ci-checks-native-census-d69.json) to genuine original A1/B1 records,
the accepted source/tree/workflow, producer blobs and examined source regions.
It preserves five platforms, thirteen runtime row IDs and seventeen complete
observed per-row censuses. Each admitted option includes all four native
feature words, both compiled SIMD flags and the complete module/assertion,
audit, inventory and external-test evidence. Counts cannot identify a profile.

Every replacement sample first passes the unchanged
`ci_checks_qualification.sample()` with its original binary/query/test, job,
source, run, attempt and artifact joins. The new reader then requires exact
platform identity and selected-row equality, complete runtime row coverage,
and a byte-equivalent structured census for the observed per-row profile.
Different profiles may have different complete source-reviewed counts.
Missing modules, smaller counts, foreign rows, ambiguous profiles or changed
types fail. Source/workflow/normalized conditions remain equal across runs.

The catalogue is intentionally closed. In particular, a higher-feature
Windows sanitized profile was not observed in these two receipts and is not
invented from the old arithmetic deltas. An unlisted profile stops the epoch
as inconclusive; the campaign cannot learn expected counts from its timing
samples. Extending the catalogue requires separately authenticated diagnostic
evidence and source review, a new reader/catalogue publication and a new
prospective disposition. It never patches a running or failed epoch.

## Budget, order and old evidence

End the original A1/B1 epoch as incomparable, preserving its artifacts,
manifests, operational reviews and all historical refs unchanged. The
population reader explicitly excludes run IDs `37191738110` and
`37193669465` from new timing samples. Their counts remain diagnostic
catalogue witnesses, not replacement timing observations.

Experiment `issue2610-standard-hosted-v1` has exactly 36 first-attempt runs,
twelve per policy. A is combined overlap, B is combined all-builds, and C is
split overlap. The twelve sequential block orders are fixed:

```text
CAB ACB CBA BAC BCA ABC ABC CBA ACB BCA BAC CAB
```

Each permutation occurs twice. The original generation seed is
`5bb691c893e292c2469686a078cfe9ae50b6afe8ed510da4d99785428dd85050`.
The saved order is authoritative. Each block runs left to right after
complete intake of the previous run; a block is not a shared VM.

Use the existing `codex/2120-evidence-v2-<variant>` dispatch refs at the
declared accepted source and workflow, with both optional inputs false.
Read back their source pins before each dispatch. All required 21/27 jobs,
configurations, modules, sanitizers/fuzz, supported compilers, canonical
analysis, self-host, serial fallback, deadlines and cleanup remain required.
No profile selection, masking, count normalization, row deletion,
retry-until-green, early favorable stop or post-result budget extension is
allowed. A failure, cancellation, rerun, source/condition drift, incomplete
artifact join, unknown profile or unexplained census change stops the epoch.
Retain all dispatches and later violations; never renumber a successful subset.

## Freeze and executable evidence path

Prepare an immutable declaration from the reviewed reader checkout:

```sh
python3 -B tools/ci_checks_population.py --prepare work/population/declaration.json
```

This writes once, retains an exact portable catalogue copy beside the
declaration, and prints its SHA-256 reference and publication marker. It
freezes the source/tree/workflow/producer pins, full reader/collector dependency
bytes, input-witness producer blobs, catalogue digest, order, seed, false diagnostic inputs and archived-run
disposition. Publishing
the exact marker as a standalone line on issue #2610 before sampling binds
the declaration to a retained original API comment. Keep the original receipt
unedited, including `created_at == updated_at`. A marker alone is not sampling
authorization: the reviewed design/catalogue/reader and the complete source
declaration must first be recorded on #2610/#2119/#2120 by the sole campaign
writer. The collector and reader do not dispatch or move refs.

Use a clearly later API-second boundary for each dispatch, so publication
and previous completed intake provably precede the next run. GitHub's
whole-second timestamps do not prove order inside the same second; there is
no retrospective tolerance or timestamp adjustment.

After intake, retain the exhaustive current dispatch history:

```sh
python3 -B tools/ci_checks_dispatch_inventory.py \
    --publication work/population/publication.json \
    --output work/population/inventory-01
```

The collector performs read-only paginated API requests for all `ci.yml`
manual dispatches since publication, retaining the original page bytes.
It stops on changing page totals, incomplete pages, request failure or the
1,000-result API limit. It never retries, dispatches or refreshes credentials.
Use a fresh directory per capture; failed partial captures remain evidence.

A campaign manifest has this structure, with real digest references:

```json
{
  "schema": "buster-ci-checks-population-v1",
  "declaration": {"path": "declaration.json", "sha256": "<actual digest>"},
  "publication": {"path": "publication.json", "sha256": "<actual digest>"},
  "dispatch_inventory": {"path": "inventory-01/inventory.json", "sha256": "<actual digest>"},
  "attempts": []
}
```

Each ordered attempt contains `ordinal`, original enriched `run` REF,
assembled `sample` REF, `input_evidence` REF and `intake_completed_at`. Failed/incomplete attempts
keep their run receipt and may have null sample/input/intake fields. References are
relative to their containing manifest. The sample retains its own reference
root; its original run path and digest must resolve to the same dispatch.

The input manifest has schema `buster-ci-checks-population-inputs-v1`,
`configure: [{"job": "<exact desktop job>", "manifest": REF}]` for every
desktop, and `analyzer_selection: REF`. Retain each original
`<phase_directory>.parent/configure/manifest.json`. The reader checks that
exact sibling path, source/run/attempt/repository/runner identity, the observed
boolean `profile_requested: false`, integer `profiles_captured: 0`, and no
capture errors. Retain the analyzer's original `comparison-selection.txt`
beside its already authenticated selected-Ninja receipt. The source-pinned
V3 parser must observe `workflow_dispatch`, `requested=false`, same-revision
skip and the declared candidate/reference source and tree. Missing witnesses,
inconsistent same-revision provenance, enabled flags and cross-artifact
substitutions stop the epoch. These existing
worker artifacts require no change to the frozen producer workflow.

The dispatch-inventory manifest binds endpoint, event, publication lower
bound, capture time, per-page size and every original numbered response.
The reader reconciles **every** run on the three exact refs against all
declared attempt IDs in chronological order, including failure/retry status.
An omitted earlier failure, selected window or extra 37th run is not a
complete campaign. Other manual-dispatch refs remain in the raw inventory
and do not become campaign samples. Every previous executed job and complete
intake must finish before the next dispatch; inventory capture must follow
the retained intakes. A later API snapshot cannot reconstruct a run deleted
before capture; original dispatch custody remains a separate review obligation.

```sh
python3 -B tools/ci_checks_population.py work/population/campaign.json
python3 -B tools/ci_checks_population_test.py -v
python3 -B tools/ci_checks_dispatch_inventory_test.py -v
```

Retained SHA-256 references bind bytes and internal joins. They do not
authenticate fabricated API records, reconstruct missing native observations
or prove that a local declaration was published. Original trusted collection
and retained publication provenance remain required, as with the legacy
evidence path. The run API does not echo original dispatch inputs: the reader
requires the actual worker witnesses above, and original authenticated dispatch
requests and artifact custody remain subject to the separate origin review.
Declaring false does not reconstruct a missing observation. All controls are finite
parser/collector fixtures and do not
constitute hosted timing observations.

## Metrics and disposition

Use all twelve complete first attempts per variant. Report individual native
profiles/assertions, per-row profile frequencies by variant, individual
timings, median, min/max and median absolute deviation. Keep original queue
timestamps, workflow-to-job creation delay, job-created-to-start delay, native
phase journals, setup/upload steps and the aggregate tail. These intervals
do not prove a particular physical runner allocation event.

Preserve the numerical timing targets: B/A median Windows-checks job time
<=0.90, C/A median queue-inclusive whole-CI time <=0.85, and each candidate's
median aggregate runner-seconds ratio <=1.05. Ratios are ratios of variant
medians, not medians of paired ratios or timings divided by assertion counts.
Whole-CI time is workflow creation to the last required-job completion;
aggregate runner seconds sum actual executed job spans. Optional metadata
does not extend the required-job completion denominator, but every executed
job remains in runner cost and complete-intake/nonoverlap checks.

The reader always reports `performance_accepted=false`. Meeting point timing
targets leaves overall qualification pending the population uncertainty and
operational dispositions. Incomplete epochs and unknown profiles are
inconclusive, not measured regressions. Complete timing targets can be
rejected. Resolving #2610's reviewed design gate never closes #2119/#2120 or
admits the held rollout without their remaining evidence and landing gates.

Buster first-party licensing remains unspecified/unselected under
[LICENSES/README.md](../LICENSES/README.md) and
[#621](https://github.com/buster14a/buster/issues/621). No external
implementation or dependency is imported.
