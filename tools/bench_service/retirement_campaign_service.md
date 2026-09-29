# Private campaign bind seams and the in-unit campaign driver

`retirement_campaign_service.h` has two private entries that bind the fixed
#1022 campaign to service-held executable descriptors. They share one
verification tail: `bq_retirement_campaign_service_gate_matches` (A and both
source manifests against the ready correctness gate),
`bq_retirement_campaign_service_bind_verified` (the compiled profile's
`campaign-budget-sha256=` pin, then `bq_retirement_campaign_bind_held`) and
`bq_retirement_campaign_service_refuse` (a refusal leaves nothing bound or
held and poisons both stages). Neither entry is called by the production
worker, and the recipe stays blocked.

## Queue entry

`bq_retirement_campaign_service_bind` reads the job and attempt token from the
service queue, requires that job to be the active measuring attempt, and
derives the transcript label as `job-<queue id>`. Callers do not provide a
separate transcript job string.

Before freezing the campaign, the entry independently calls the pinned A record
readback and import paths. It derives the A record SHA from the durable record,
then requires that SHA and both source manifest hashes to match the ready
correctness gate. It reads the durable binary record digest from the private
queue directory itself and passes that digest to the existing acquire importer,
which rereads the record and frozen files while opening the two held
descriptors. Only those acquired descriptors reach
`bq_retirement_campaign_bind_held`.

(M4) The entry also takes the reviewed campaign budget, reads the compiled
profile's `campaign-budget-sha256=` pin and passes it into the binding, where
freeze requires the budget's digest to equal it; the checked-in blocked
profile has no such pin, so the entry returns `BQ_RECIPE_MISMATCH`. Timed
object rows bind through the gate's frozen batch groups, and only when the gate
carries the sealed #509 authority flag, which only the future #509 importer
sets. The fixture binds a singleton-only campaign and one with an object group
(with a test stand-in for that flag) under a test-pinned profile, and refuses
an object row without a frozen contract, the unpinned profile, a budget other
than the pin, object groups without the #509 flag under the pinned profile,
and a singleton costed at another stage.

The pinned entry exists for the miniature fixture, whose installed inventory
and toolchain pins are test-created. The ordinary private entry obtains the
compiled profile from the queued request. The preparation test runner uses an
injected active blocked job because the public registry continues to reject
that recipe. The focused fixture checks a successful same-job, same-attempt
durable A and held-binary bind, then refuses an unknown job, wrong token,
wrong queue phase, and a changed preparation record.

## Store entry (in the worker unit)

The worker unit never holds the queue or the lease, so
`bq_retirement_campaign_service_bind_unit` authenticates from the per-attempt
record store instead, under the MEASURING acknowledgement on the private
phase channel:

1. The channel must belong to this job and attempt and hold the MEASURING
   acknowledgement, the SIGTERM self-pipe must be quiet and the absolute
   deadline unexpired (`bq_retirement_campaign_service_measuring`, the unit's
   stand-in for the queue's active MEASURING attempt). This is checked again
   just before the held bind.
2. `bq_retirement_unit_prepare_pinned` re-imports A from the coordinator's
   sealed export (exact closure, attempt seal, request digest), the toolchain
   and the reference policy; the request must name the blocked retirement
   recipe.
3. The B -> D handoff: `bq_retirement_campaign_ready_import` runs the
   coordinator's replay (`bq_retirement_unit_replay_pinned`) over the sealed
   `retirement-ready/` record, which re-derives every field from the store
   (A, toolchain, reference policy, matched builds, binaries, the census
   projection, every `reference-oracle/` file, each runtime command from its
   recorded descriptor numbers, the oracle attempt and the gate seal) and
   requires the stored bytes to be exactly the record those facts format. The
   record is then reread by its content address and its fields parsed; job,
   attempt, A, template and inventory must be this attempt's.
4. The shared gate match, then `bq_retirement_campaign_ready_gate`: the
   correctness gate must be ready and carry the record's A, support and census
   digests, both binaries, row counts, native target and population hash, and
   exactly the record's reference rows with their oracle outputs.
5. `bq_retirement_campaign_ready_held` holds both binaries again from the
   record's build and binary record digests through the unit's own build
   import, and hands over only a pair whose digests and A are the record's.
6. The entry derives the plan, its digest and the pre-sample context itself
   (below) and refuses any caller plan, plan digest or context that differs.
7. The shared tail binds the held descriptors.

The production wrapper passes the compiled (blocked) profile and the installed
driver, toolchain and broker paths, so it fails closed. The pinned seam takes
a `BqRetirementCampaignUnitStore` for the fixture.

## Plan and context

`retirement_unit_campaign.h` derives them from service-authenticated inputs
only. `bq_retirement_unit_campaign_pins` reads #426's frozen seed and pair
count, the #619 resample count, the validator-derived family's bootstrap member
count and the budget digest from the recipe profile
(`campaign-seed=`, `campaign-pairs=`, `campaign-resamples=`,
`campaign-bootstrap-members=`, `campaign-budget-sha256=`). Nothing here
chooses those numbers; the blocked profile carries none, so every derivation
fails closed. The pair count must be even and within 60..254.
`bq_retirement_unit_campaign_plan` fills the #619 plan from the pins and
derives the exact-cell count from the sealed gate as `2R + U + 2B` (wall and
peak memory per timed row, runtime per runtime-eligible timed row, the batch
pair per object group).

`bq_retirement_unit_campaign_plan_digest` is candidate independent: it binds
the schedule, the pins, every #508 row's identity, eligibility and reference
oracle, and the frozen batch layout, never a subject binary, command or
output. `bq_retirement_unit_campaign_pre_context` joins that plan digest to
the subjects: the gate seal, A, both sources and binaries, the ready record
digest, the budget, the positional digest of every frozen A/A and A/B command,
the sealed untimed record stream and the host/transcript identity (job,
attempt, boot, CPU, pre-sample binding time). The campaign freezes this
pre-sample digest. `bq_retirement_unit_campaign_post_context` exists only for
a completely collected campaign whose frozen context is that digest; it chains
it to both stages' transcript shard chains, numeric digests and metrics
totals. The post-sample digest is not the validator's `_execution_context`,
which lane E's receipt composer must still bind.

## In-unit driver

The driver steps must run in this order; any other call refuses and poisons
the attempt:

1. `bq_retirement_unit_campaign_begin`: after the build's PREPARING
   acknowledgement, the SETTLING acknowledgement.
2. `bq_retirement_unit_campaign_untimed`: the untimed production and
   reproduction batches (`retirement_untimed.h`) on the unit's held binaries,
   each group's shape the reviewed one, then the sealed untimed records.
3. `bq_retirement_unit_campaign_measuring`: the MEASURING acknowledgement.
4. The store-based bind (above), then lane E's
   `tp_retirement_compose_plan` over the frozen capacity, then
   `bq_retirement_unit_campaign_attach`: the binding's job and attempt are
   the channel's, its held pair is the one the untimed batches ran, every
   untimed batch finished before the pre-sample binding time, the plan, plan
   digest and context re-derived from the frozen snapshot equal the
   campaign's, and the store plan reserves both stages' payload plus at least
   the execution receipt within the store ceilings
   (`bq_retirement_unit_campaign_store_planned`).
5. `bq_retirement_unit_campaign_stage` (A/A): every cursor item runs its
   frozen command on the held descriptor of its stage and variant through
   `bq_retirement_campaign_run`, with a fresh log each time; transcript and
   metrics shards rotate onto service-supplied streams; then the metrics
   writer, transcript and numeric export finish and the stage must be ready.
6. `bq_retirement_unit_campaign_admit`: production has no authority (no
   approved #426 A/A decision, no #1021 one-use capability), so it refuses and
   leaves the attempt awaiting one. Only the functional fixture build
   (`BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA`, rejected with
   `BQ_SERVICE_INSTALLED`) enters A/B through the campaign's fixture stand-in.
7. `bq_retirement_unit_campaign_freeze`: before the first candidate child, the
   held join, sealed gate, frozen plan and context, finished A/A evidence and
   untouched A/B stage are rechecked.
8. `bq_retirement_unit_campaign_stage` (A/B).
9. `bq_retirement_unit_campaign_finish`: the post-sample context, then the
   MEASURED acknowledgement. A failed or incomplete campaign never sends it.
   `bq_retirement_unit_campaign_result` then returns what lane E's composer
   takes from the driver: job, attempt, boot, the pre-sample binding and A/B
   completion times and the frozen #619 plan. The driver writes no execution
   receipt; the composer writes the post-sample one.

The phase protocol has four phases, so admission and the A/B freeze are
driver boundaries without their own acknowledgement.

## Fixtures

`tools/throughput/retirement_unit_campaign_test.h` runs the whole driver with
real fixture children and a forked supervisor stand-in on the campaign that
`retirement_campaign_test.h` binds, plus one untimed singleton group, and
refuses an untimed shape other than the review, swapped held descriptors, a
corrupted MEASURING acknowledgement, a binding older than the untimed
batches, a caller plan or context override, a missing or short result-store
plan, an early freeze, cancellation,
A/B without admission or without freeze, and a denied or stale admission.
`retirement_unit_campaign_tests.h` uses the real unit-oracle attempt and its
ready record: the import, re-addressed mutations of the job, attempt, build
and binary record digests, candidate binary, template, inventory, gate seal,
reference command and descriptor numbers, a foreign token or job, the gate
join, the held re-import, and a store-based bind that succeeds and each of its
refusals. That bind's gate uses synthetic row facts and the #509 authority
stand-in, and no timed child runs there. All fixture digests, pins and
admissions are test data, never a verdict.

## Open interfaces

- The ready record's `gate=admitted` seal is today only the test issuer's
  digest of the attempt, A, population and oracle attempt. D joins the
  correctness gate to the record field by field, but the record does not yet
  bind `BqRetirementCorrectness.sealed_sha256`; the production #509 gate
  issuer (lane B) should seal it, or provide a verifier D can call.
- The untimed batch commands have no frozen, authenticated contract in the
  gate yet; the driver checks them only against the reviewed shapes.
- The frozen #426 pins and the admission capability (#1021) remain
  integration work.
- Lane E's composer (#1879) also needs, and this lane does not produce: the
  six #619 dimension values per timed row (the gate rows carry only the
  configuration digest, so they must come from B's projection of #508's
  rows), the per-row code facts in `TpRetirementCodeSide` form, and the
  validator's plan-v3 execution-plan artifact digest. D's plan digest is its
  own candidate-independent schedule digest, not that artifact, and D's
  post-sample digest is not the validator's `_execution_context`.
- The driver supplies streams in index order; the store must name transcript
  shards so they sort in that order. Stage metrics tags are `aa` and `ab`
  (freeze refuses `untimed`). Successful per-launch scratch logs and outputs
  are retired after each launch (thousands would exceed the store's entry
  cap); a failed launch keeps them as evidence.
