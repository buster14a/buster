# Private campaign bind seams and the in-unit campaign driver

`retirement_campaign_service.h` binds the fixed #1022 campaign to
service-held executable descriptors through a queue entry and the worker
unit's store entries (an import and a bind). They share one verification
tail: `bq_retirement_campaign_service_gate_matches` (A and both source
manifests against the ready correctness gate),
`bq_retirement_campaign_service_bind_verified` (the compiled profile's
`campaign-budget-sha256=` pin, then `bq_retirement_campaign_bind_held`) and
`bq_retirement_campaign_service_refuse` (a refusal leaves nothing bound and
poisons both stages). No entry is called by the production worker, and the
recipe stays blocked.

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

## Store entries (in the worker unit)

The worker unit never holds the queue or the lease, so it authenticates from
the per-attempt record store instead, in two steps on the private phase
channel. `bq_retirement_campaign_service_phase` is the unit's stand-in for the
queue's active attempt: the channel belongs to this job and attempt and holds
exactly the named acknowledgement, the SIGTERM self-pipe is quiet and the
absolute deadline has not passed.

`bq_retirement_campaign_service_import_unit` is the heavy import, under the
SETTLING acknowledgement and before the untimed batches. The held pair and
ready holder must be empty; the channel is checked before and after the
import, and a refusal releases only what the call filled.

1. `bq_retirement_unit_prepare_pinned` re-imports A from the coordinator's
   sealed export (exact closure, attempt seal, request digest), the toolchain
   and the reference policy. The request must name the blocked retirement
   recipe. That check is defense in depth: the request record is bound to A,
   so no fixture can build a store whose A imports under another recipe.
2. The B -> D handoff: `bq_retirement_campaign_ready_import` runs the
   coordinator's replay (`bq_retirement_unit_replay_pinned`) over the sealed
   `retirement-ready/` record, which re-derives every field from the store
   (A, toolchain, reference policy, matched builds, binaries, the census
   projection, every `reference-oracle/` file, each runtime command from its
   recorded descriptor numbers, the oracle attempt and the gate seal) and
   requires the stored bytes to be exactly the record those facts format. The
   record is then reread by its content address and its fields parsed; job,
   attempt, A, template and inventory must be this attempt's.
3. The caller passes lane B's issued step 9 gate (`BqRetirementUnitGate`
   from `bq_retirement_unit_gate`), never a correctness gate it built. The
   shared gate match (A and both source manifests) runs on its correctness
   gate, then `bq_retirement_campaign_ready_unit_gate`: the unit gate must be
   owned and issued, its seal the record's `gate=admitted` seal, and its
   required-check authority and check evidence the record's
   `checks-authority=` and `check-evidence=`. Then
   `bq_retirement_campaign_ready_gate`: the correctness gate must be ready,
   its seal (which covers the #509 batch authority and the authority digest)
   exactly the record's `correctness=`, and it must carry the record's A,
   support and census digests, both binaries, row counts, native target and
   population hash, and exactly the record's reference rows with their oracle
   outputs.
4. `bq_retirement_campaign_ready_held` holds both binaries again from the
   record's build and binary record digests through the unit's own build
   import, and hands over only a pair whose digests and A are the record's.

`bq_retirement_campaign_service_bind_unit` is the cheap bind, under the
MEASURING acknowledgement and after the untimed record stream is sealed. It
rechecks the channel; that the held pair and record are this attempt's import
(`bq_retirement_campaign_ready_holds`: A, binary digests and descriptor
identities); that the record still stands at its content address with the
imported bytes (`bq_retirement_campaign_ready_standing`); that the request's
gate is the unit gate's own correctness gate and the unit gate still joins;
and
that the pre-sample binding time follows the MEASURING acknowledgement. It
derives the plan, plan digest and pre-sample context itself (below), refuses
any caller value that differs, binds through the shared tail and marks the
binding with the record's digest (`unit_ready_sha256`). The driver's attach
requires that mark. A refusal clears the binding and poisons both stages;
the held pair and record stay the caller's.

Both production wrappers pass the compiled (blocked) profile and the installed
driver, toolchain and broker paths, so they fail closed. The pinned seams take
a `BqRetirementCampaignUnitStore` for the fixture.

`bq_retirement_campaign_service_timed_rows` gives lane E's composer its
per-timed-row layout: each native-host timed row of the sealed gate in
ascending order, its campaign batch-group ordinal (groups in ascending
smallest-member order), its runtime flag and its six #619 slice dimensions
(target, cpu, allocator, frontend_lowering, PIC, artifact_stage) as
`TpRetirementTimedRow`. The values come from the pinned performance-row
artifact, read through the installed census under its profile pin. Every
declared row's identity is recomputed from its fields and must equal the gate
row's sealed identity, so each value is one the gate authenticated.

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
pair per object group). It requires `U > 0` and `B > 0`, as the validator's
`_derive_statistical_family` does, and requires every object group's members
to be the first inputs of both frozen contracts in ascending row (census)
order (`bq_retirement_unit_campaign_members_first`), which lane E's composer
relies on.

`bq_retirement_unit_campaign_plan_digest` is candidate independent: it binds
the schedule, the pins, every #508 row's identity, eligibility and reference
oracle, and the frozen batch layout, never a subject binary, command or
output. `bq_retirement_unit_campaign_pre_context` joins that plan digest to
the subjects: the gate seal, A, both sources and binaries, the ready record
digest, the budget, the positional digest of every frozen A/A and A/B command,
the sealed untimed record stream and the host/transcript identity (job,
attempt, boot, CPU, pre-sample binding time). The campaign freezes this
pre-sample digest. After A/A, `bq_retirement_unit_campaign_post_aa` chains it
to the A/A transcript shard chain, numeric digest, metrics totals and the
campaign's post-A/A identities; the fixture admission must present it.
`bq_retirement_unit_campaign_post_context` exists only for a completely
collected campaign whose frozen context is the pre-sample digest; it chains it
to both stages' transcript shard chains, numeric digests and metrics totals.
The post-sample digest is not the validator's `_execution_context`, which
lane E's composer derives.

## In-unit driver

The driver steps must run in this order; any other call refuses and poisons
the attempt:

1. `bq_retirement_unit_campaign_begin`: after the build's PREPARING
   acknowledgement, the SETTLING acknowledgement. The store import runs next.
2. `bq_retirement_unit_campaign_untimed`: the untimed production and
   reproduction batches (`retirement_untimed.h`) on the imported held pair,
   each group's shape the reviewed one. After each production batch its
   objects are linked into the service's code directory as
   `code-<row>-<variant>.o`; after each reproduction the retained object and
   the reproduction are observed (`tp_retirement_code_observe`) before any
   scratch output is retired. The result carries one `TpRetirementCodeRow`
   per untimed row, both variants, in ascending row order.
3. `bq_retirement_unit_campaign_measuring`: the MEASURING acknowledgement.
4. The store bind (above), then lane E's `tp_retirement_compose_plan` over
   the frozen capacity, then `bq_retirement_unit_campaign_attach`, which takes
   the imported `BqRetirementCampaignReady`: the binding must carry its mark,
   its job and attempt are the channel's and the record's, its held pair is
   the one the untimed batches ran, the pre-sample binding follows the
   MEASURING acknowledgement and every untimed batch, the plan, plan digest
   and context re-derived from the frozen snapshot equal the campaign's, lane
   E's family counts (`TpRetirementFamilyCounts`, derived from the same layout
   before timing) equal the plan's, and the store plan reserves both stages'
   payload plus at least the execution receipt within the store ceilings
   (`bq_retirement_unit_campaign_store_planned`).
5. `bq_retirement_unit_campaign_stage` (A/A): every cursor item runs its
   frozen command on the held descriptor of its stage and variant through
   `bq_retirement_campaign_run`; transcript and metrics shards rotate onto
   service-supplied streams; then the metrics writer, transcript and numeric
   export finish, the stage must be ready and the post-A/A digest is formed.
6. `bq_retirement_unit_campaign_admit`: production has no authority (no
   approved #426 A/A decision, no #1021 one-use capability), so it refuses and
   leaves the attempt awaiting one. Only the functional fixture build
   (`BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA`, rejected with
   `BQ_SERVICE_INSTALLED`, as is the campaign's own fixture macro) enters A/B
   through the campaign's fixture stand-in, and only with this campaign's
   plan, pre-sample and post-A/A digests.
7. `bq_retirement_unit_campaign_freeze`: before the first candidate child, the
   held join, sealed gate, frozen plan and context, finished A/A evidence and
   untouched A/B stage are rechecked.
8. `bq_retirement_unit_campaign_stage` (A/B).
9. `bq_retirement_unit_campaign_ready`: the post-sample context; the driver is
   READY and `bq_retirement_unit_campaign_result` returns what lane E's
   composer takes from it: job, attempt, boot, the pre-sample binding and A/B
   completion times, the frozen #619 plan, D's plan, pre-sample, post-A/A and
   post-sample digests, the untimed record stream, the untimed code rows and
   the per-stage log chains. The driver writes no execution receipt.
10. `bq_retirement_unit_campaign_measured`: only after the caller confirms the
    composed sealed result and the producer authority handoff, the MEASURED
    acknowledgement. A failed or incomplete campaign never sends it.

Every launch is refused when its timeout could outlive the absolute deadline
(a frozen timeout is never shortened) and when the cancellation descriptor is
readable; while a child runs, the process layer polls that descriptor with the
child's pidfd and kills the process group when it becomes readable
(`TpProcessInputs.cancellation`, `TpProcess.cancelled`).

The first failure is retained (`bq_retirement_unit_campaign_failure`): the
reason (refused, cancelled, deadline, channel, launch), the step and stage,
whether a child was launched, the invocation's coordinates (group, row,
variant, phase, round, pair, warm-up, untimed purpose and sequence), the
measurement status, exit code, signal, timeout, cancellation and launch
error, and the kept log's full size and digest. The unit cannot see a cgroup
OOM kill directly: it shows as SIGKILL, and the supervisor's memory events are
the authority.

Every launch writes one scratch log, `unit-campaign-log-0000.log`. A
successful launch's log digest and size join its stage's chain (0 untimed,
1 A/A, 2 A/B) and the log is unlinked with the launch's scratch outputs,
since thousands would exceed the store's entry cap. A failed launch keeps its
log and outputs and stops the driver, so an attempt retains at most one log
(`BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_PREFIX`, `_SUFFIX`, `_LOGS_MAX`), truncated
to `BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX` (1 MiB, a storage bound, not a
measurement value): lane E declares it as a capped retained group.

The phase protocol has four phases, so admission and the A/B freeze are
driver boundaries without their own acknowledgement.

## Fixtures

`tools/throughput/retirement_unit_campaign_test.h` runs the whole driver with
real fixture children and a forked supervisor stand-in on the campaign that
`retirement_campaign_test.h` binds, plus one untimed singleton group whose row
is a cross-target code row, through READY and MEASURED. It checks the code
facts, the log chains and the post-A/A digest, and refuses an untimed shape
other than the review, swapped held descriptors, a wrong untimed output, an
untimed child killed on cancellation, a launch that could outlive the
deadline, a corrupted MEASURING acknowledgement, a binding older than the
untimed batches, a caller plan or context override, an unmarked binding,
other family counts, another held file with the same bytes, a missing or short
result-store plan, an early freeze, cancellation before the first A/A launch
(with its coordinates), A/B without admission or freeze, a denied, stale or
wrong post-A/A admission, a freeze after an A/B launch and an invalid handoff.
Its plan tests refuse `U = 0` and object-group members out of order or after
a control input.

`retirement_unit_campaign_tests.h` uses the real unit-oracle attempt and its
ready record. It covers the ready import, the template and inventory compare,
re-addressed mutations of the job, attempt, build and binary record digests,
candidate binary, template, inventory, gate seal, reference command and
descriptor numbers, a foreign token or job, the gate join, and the held
re-import with a forged binary or A digest. Under SETTLING it checks the
import and its refusals: each phase and channel state, cancellation, an
expired deadline, a deadline that passes during the import, a live holder, a
gate over other source manifests and the blocked production profile. Under
MEASURING it checks the bind on that pair and its refusals: each phase and
channel state, a late binding, a record of another job or token, changed
record bytes, swapped descriptors, a changed held digest or A, another gate,
the blocked profile, an unpinned profile, and a plan, context or untimed
override. It also checks the timed-row layout (and refuses a short workspace,
a changed gate identity and an unpinned profile), and runs the driver from
SETTLING through the import to the first untimed launch of the imported held
baseline. The census fixture's matched builds are text files, so that launch
exits 125 and the driver keeps its coordinates, process facts and log. Its
unit gate is lane B's issuer on this attempt (`bq_prep_campaign_issue`): B's
stand-in required checks run in the unit and B's passing test evidence,
carrying this campaign's commands, joins; the bind also refuses another
unit-gate seal and a caller-held copy of the correctness gate. No timed child
runs there. All fixture digests, pins and admissions are test data,
never a verdict.

## Open interfaces

- Lane B's production issuer has no row-plan producer yet, so
  `bq_retirement_unit_gate` refuses in production; only the fixtures issue a
  gate, from test row evidence and stand-in required checks.
- The untimed batch commands have no frozen, authenticated contract in the
  gate yet; the driver checks them only against the reviewed shapes.
- The frozen #426 pins and the admission capability (#1021) remain
  integration work.
- Lane E: `TpRetirementCodeRow` has the layout of `TpRetirementComposeCode`,
  `TpRetirementTimedRow` supplies `TpRetirementComposeRow` (its dimension
  pointers can point into it) and `TpRetirementFamilyCounts` carries the two
  `TpRetirementComposeBounds` family counts; E can adopt these shared types.
  D does not produce the validator's plan-v3 execution-plan digest or its
  partitions: D's plan digest is its own candidate-independent schedule
  digest, and D's post-sample digest is not `_execution_context`.
- The driver supplies streams in index order; the store must name transcript
  shards so they sort in that order. Stage metrics tags are `aa` and `ab`
  (freeze refuses `untimed`).
