# Private campaign bind seams and the in-unit campaign driver

`retirement_campaign_service.h` binds the fixed #1022 campaign to
service-held executable descriptors through a queue entry and the worker
unit's store entries (an import and a bind). They share one verification
tail: `bq_retirement_campaign_service_gate_matches` (A and both source
manifests against the ready correctness gate),
`bq_retirement_campaign_service_bind_verified` (the compiled profile's
`campaign-budget-sha256=` pin, then `bq_retirement_campaign_bind_held`) and
`bq_retirement_campaign_service_refuse` (a refusal leaves nothing bound and
poisons both stages). `retirement_unit_campaign.h` is the in-unit driver,
`retirement_unit_documents.h` writes its workflow documents and
`retirement_unit_handoff.h` maps its result onto lane E's composer request.
No entry is called by the production worker, and the recipe stays blocked.

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
carries the sealed #509 authority flag, which only
`bq_retirement_correctness_authorize` sets (lane B's step 9 issuer, over the row
plan's evidence and the required checks). The queue fixture binds a
singleton-only campaign and one with an object group (its test gate sets that
flag by hand) under a test-pinned profile, and refuses
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
and the pre-sample plan document's digest to the A/A transcript shard chain,
numeric digest, metrics totals, the untimed and A/A launch-log chains and the
campaign's post-A/A identities; the fixture admission must present it.
`bq_retirement_unit_campaign_post_context` exists only for a completely
collected campaign whose frozen context is the pre-sample digest; it chains it
to both stages' transcript shard chains, numeric digests and metrics totals
and to all three launch-log chains. The post-sample digest is not the
validator's `_execution_context`, which lane E's composer derives.

## Workflow documents

`retirement_unit_documents.h` writes the validator's workflow documents as
its canonical JSON (`json.dumps(sort_keys=True, separators=(",", ":"))`),
hashing each while it is streamed: the oracle records
(`workflow.records.oracle`), the plan-v3 execution plan
(`workflow.execution_plan`), the result-input plan v3
(`workflow.records.result_input_plan`) and the pre-sample and post-A/A phase
documents. The pinned performance rows are parsed by
`bq_retirement_documents_population` (in `retirement_campaign_service.h`,
through the installed census and its profile pin, each row's identity
recomputed and required to be the gate's). From them the header derives the
validator's timed and untimed partitions (`_batch_groups`, `_untimed_groups`)
and the statistical family (`_derive_statistical_family`: its digest and both
per-scope counts), then joins the timed object groups to the gate's frozen
batch groups, the singletons to their rows' commands, and the untimed groups
to the untimed production batches and their observed reproductions. Every
field comes from the pinned rows, the sealed gate, the frozen plan and pins,
the reviewed budget, the untimed batches and the observed code facts.

The driver writes them in two steps, into the evidence root the caller passes
(`BqRetirementUnitCampaignDocumentSources`; the leaves are
`bq_retirement_unit_campaign_document_paths`, created, never replaced, and
synced). `bq_retirement_unit_campaign_documents` runs after attach and before
A/A: it rederives the partitions and family, requires every pinned row's
canonical identity (`bq_retirement_document_identity`) to be the gate's
sealed one and each timed row's runtime flag the gate's, the timed partition
to be the campaign's groups with each object group's members exactly the
member inputs of its gate batch group's frozen contracts (singletons one
row), the family counts to be the plan's, the budget digest to be the
campaign's and the performance-rows pin to be the parsed rows' digest, and
writes the oracle, execution-plan, result-input and pre-sample documents. It
keeps the three source pins and a digest of the timed-row layout (each timed
row's id, group, runtime flag and six dimension values from the pinned rows,
`bq_retirement_unit_campaign_timed_line`). The A/A stage refuses until they
exist. `bq_retirement_unit_campaign_post_aa_document` runs after the A/A
admission: the post-A/A binding over the same sources (all three pins must be
unchanged) and the admission receipt digest the admission step recorded. The
A/B freeze refuses until it exists.

The admitted digest cannot be the validator's post-A/A document: that
document binds the admission receipt (`aa_admission_sha256`), so it exists
only after the admission. The admission names D's post-A/A evidence digest
instead, which binds the pre-sample plan document (and through it the
execution plan, the result-input plan and the family), and the post-A/A
document then binds the admission receipt and the pre-sample plan.

The #437 receipt schema has no field that could name the post-A/A evidence
digest (the validator refuses unknown receipt fields). The fixture admission
therefore takes the receipt's bytes: they must hash to the named digest and
be the approved schema, admitted and native only, on the campaign's CPU and
native target, over the family the pre-sample plan named.

The preparation runner has the driver's two steps write all five documents
from the issued unit gate over the real census fixture (a driver standing at
BOUND, then ADMITTED over a stand-in receipt digest) and runs
`retirement_unit_documents_test.py`,
which rederives the sources from the census files and applies the validator's
own checks (`_performance_rows_with_sources`, `_family_member_counts`,
`_check_execution_plan`, `_result_input_plan`, `_workflow_phase` and the
pre-sample and post-A/A field joins; the oracle-record and result-population
conditions are inline in `_check_workflow_evidence_open`, so the script
repeats them) and requires every document to be canonical. A tampered plan is
refused.

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
   scratch output is retired. The observed rows must be exactly the gate's
   code-observed untimed rows: every compile-eligible row, whose oracle parsed
   its code section, a zero baseline included (the validator's
   `_code_observed`; the gate's `code_eligible` is narrower, a nonzero
   baseline section). Each side must equal the gate's artifact (which a
   code-observed row must have) and its code-section digest and size. The
   driver keeps the batches, their singleton rows and the reviewed budget
   (borrowed) for its documents.
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
5. `bq_retirement_unit_campaign_documents`: the oracle, execution-plan,
   result-input and pre-sample documents (above).
6. `bq_retirement_unit_campaign_stage` (A/A): every cursor item runs its
   frozen command on the held descriptor of its stage and variant through
   `bq_retirement_campaign_run`; transcript and metrics shards rotate onto
   service-supplied streams; then the metrics writer, transcript and numeric
   export finish, the stage must be ready and the post-A/A digest is formed.
7. `bq_retirement_unit_campaign_admit`: production has no authority (no
   approved #426 A/A decision, no #1021 one-use capability), so it refuses and
   leaves the attempt awaiting one. Only the functional fixture build
   (`BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA`, rejected with
   `BQ_SERVICE_INSTALLED`, as is the campaign's own fixture macro) enters A/B
   through the campaign's fixture stand-in, and only with this campaign's
   plan, pre-sample and post-A/A digests and a #437 receipt (above). It
   records the receipt digest.
8. `bq_retirement_unit_campaign_post_aa_document`: the post-A/A binding.
9. `bq_retirement_unit_campaign_freeze`: before the first candidate child, the
   post-A/A document, held join, sealed gate, frozen plan and context,
   finished A/A evidence and untouched A/B stage are rechecked.
10. `bq_retirement_unit_campaign_stage` (A/B). After each successful compiler
    launch, the first artifact of every code-observed timed row and variant
    is observed and must equal the gate's facts.
11. `bq_retirement_unit_campaign_ready`: the code facts of every
    code-observed row, then the post-sample context, written with the bound
    documents, the timed-row layout digest and all three launch-log chains to
    the post-sample record (`BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD`) on a
    service stream, flushed and synced; the driver is READY and
    `bq_retirement_unit_campaign_result` returns what lane E's composer takes
    from it: job, attempt, boot, the pre-sample binding and A/B completion
    times, the frozen #619 plan, D's plan, pre-sample, post-A/A and
    post-sample digests, the untimed record stream, the code rows, the
    per-stage log chains, the five documents' sizes and digests (their paths
    are fixed), the result-input partitions, the family, source rows,
    admission receipt and timed-row layout digests and the record's size and
    digest. The driver writes no execution receipt.
12. `bq_retirement_unit_campaign_measured`: only after the caller confirms the
    composed sealed result and the producer authority handoff, the MEASURED
    acknowledgement, recording both confirmed digests and a measured digest
    over them, the post-sample digest and the record. A failed or incomplete
    campaign never sends it. The fixed 48-byte phase message carries no
    digest (`phase_channel.h`), so the record is what makes the post-sample
    digest, and with it the A/B log chain, durable: the service publishes it
    as a retained store file, and lane E's retained manifest, which the
    producer authority binds, seals its digest.

`retirement_unit_handoff.h` maps a READY driver onto lane E's
`TpRetirementComposeRequest`: each `TpRetirementTimedRow` becomes a
`TpRetirementComposeRow` (its dimension pointers point into it) after the
rows reproduce the documents' timed-row layout digest and each is joined to
the campaign's frozen sample row, the campaign groups' kinds form the layout,
and the plan, identity and window, document digests, partitions, code facts
and the A/A metrics tag fill the request; D's five documents are
prior-closure entries under the validator's names, and the post-sample record
and failure log are retained-file declarations. The caller supplies the rest
(store, scratch, adapter, the rest of the retained declaration, stream paths,
the #511 binding document and the rest of the prior closure).

Every launch is refused when its timeout could outlive the absolute deadline
(a frozen timeout is never shortened) and when the cancellation descriptor is
readable; while a child runs, the process layer polls that descriptor with the
child's pidfd and kills the process group when it becomes readable
(`TpProcessInputs.cancellation`, `TpProcess.cancelled`). The process layer
disarms the timeout alarm as soon as the child is reaped and reports a
timeout only for a child the alarm's kill signalled, so a late alarm neither
marks a clean exit as timed out nor kills a reaped group.

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
Its pinned rows are an in-memory population: the driver writes the five
documents into a scratch evidence root (their sizes and digests, the
post-A/A document's receipt and pre-sample joins and the partitions are
checked), and the A/A stage refuses without them, as does the documents step
for a row identity that is not the sealed one, a split object group, an
existing document or a wrong performance-rows pin, the post-A/A step for a
changed rows or support pin and the freeze without the post-A/A document.
The admission refuses a receipt whose bytes are not the named digest, over
another family or CPU, or not admitted; the untimed step refuses a gate
without an artifact digest for a code row. READY's post-sample record is read
back and checked, and the measured digest changes with it. The READY driver
is mapped onto lane E's request and each mapped field is checked; a timed row
outside the frozen layout, two swapped dimension columns and a foreign
dimension value are refused. Launch timeouts are 30 s, so the 1,468 pinned
launches do not time out under sanitizers and load, and a complete run that
stops prints its retained failure. Its plan tests refuse `U = 0` and
object-group members out of order or after a control input.

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
- Lane E: D cannot derive the stream paths (the service creates and names
  every stream), the #511 binding document and its digest (the binding
  writer's), or the rest of the prior closure; the handoff leaves them to the
  caller. D's plan digest remains its own candidate-independent schedule
  digest and its post-sample digest is not `_execution_context`.
- Lane B's #1895 row-plan layout: B's measured commands run the side binary
  at fd 3 or 4, A's two roots at fds 5 and 6 and the work directory at fd 7,
  with cwd `/proc/self/fd/7`. D launches through `TpProcessInputs`: the held
  descriptor by `fexecve`, cwd the work directory descriptor, and no A roots,
  so its command digests do not yet reproduce that layout.
- The A/B freeze's `execution->sequence` check is implied by
  `!samples[1]->collected` on every reachable path, so no test can fail it
  alone; it stays as a defensive recheck.
- The driver supplies streams in index order; the store must name transcript
  shards so they sort in that order. Stage metrics tags are `aa` and `ab`
  (freeze refuses `untimed`).
