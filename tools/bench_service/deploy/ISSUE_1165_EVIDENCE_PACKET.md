# #1165 off-host attempt inventory and clean-replay packet

This is preparation for [#1165](https://github.com/buster14a/buster/issues/1165),
not an execution receipt or completion claim. Keep #1165 open until
[#1163](https://github.com/buster14a/buster/issues/1163) and
[#1164](https://github.com/buster14a/buster/issues/1164) supply actual evidence
and every applicable export/publication/replay and retention gate below passes.
A documentation PR may be reviewed independently; its merge is not a live gate
and does not close #1165. #1165 is post-admission evidence work, not an added
pre-merge condition for #1159 or its successors.

## Scope, references and authority

Preparation baseline: commit `57add224ebff6b599be6cbf316d6677b3ae989dc`,
tree `8931d8d3ab3d75a19e74b73ad4b4e5a1e76ea9a3` (2026-09-25).
These identify the inspected instructions, NOT the future producer installation,
workflow, source inputs, exporting binary or replay binary. Record those exact
identities separately when evidence arrives; do not substitute current main.

Read [AGENTS.md](../../../AGENTS.md),
[workflow guidance](../../../docs/agents/workflow.md),
[the #880 operator packet](ISSUE_880_OPERATOR_PACKET.md),
[the smoke deployment gate](VALIDATE_BUSTER_V1.md),
[the export contract](../EXPORT.md), and
[#510's durable-preservation contract](https://github.com/buster14a/buster/issues/510).
Command/byte-layout references are [main.c](../main.c)
(`bq_client_arguments`, `bq_cli`) and [export_client.c](../export_client.c)
(`bq_export_download`, `bq_export_unpack`). Refresh these at the reviewed
producer and replay revisions before use; this packet does not change them.

One #1163/#1164 operator owns every host-facing operation. This off-host lane
owns the inventory, independent receipt checks, durable-publication accounting
and replay of retrieved bytes. Do not contact the host, query the gateway,
start an export, dispatch a workflow, change permissions, restart services or
run a benchmark merely to prepare this packet. Any later host-facing export
needs the existing separately reviewed fixed operator path and a quiet window
coordinated with that operator. Do not overlap protected execution intervals.
The current smoke dispatch workflow is NOT an export/publication workflow.
No new workflow, exporter, archive format, authority or runner access is created.

## 1. Coverage matrix: planning slots are not attempt identities

Copy this table to the live #1165 inventory; keep versioned records of updates.
`UNASSIGNED` and `NOT RECEIVED` are blockers, never successful observations.

| Slot | Producer / purpose | Required boundary | Actual service job / attempt | Evidence at preparation |
| --- | --- | --- | --- | --- |
| H0 | #1163 / preliminary history | Preserve reported job 1 `workspace-mismatch` failure and reconcile retention | job 1 reported; token NOT RECEIVED | Historical #1159/#1163 report only; no fresh readback/export |
| R | #1163 / separately keyed rehearsal | Real installed fixed recipe, result, cleanup and dispatch-disabled readback | UNASSIGNED | NOT RECEIVED |
| N | #1164 / normal scenario | Full source closure; source manifest greater than 4 KiB; real normal completion | UNASSIGNED | NOT RECEIVED |
| P | #1164 / prepare interruption | Trigger after `validate-buster-v1.prepare.manifest` exists; exact journal boundary | UNASSIGNED | NOT RECEIVED |
| S | #1164 / completed-stage interruption | Trigger after a completed stage manifest exists; preserve that stage | UNASSIGNED | NOT RECEIVED |
| B | #1164 / bundle-before-final restart | Bundle published, final manifest absent, then reviewed restart/recovery | UNASSIGNED | NOT RECEIVED |
| L | #1164 / lease handoff without acknowledgement | Connected peer never acknowledges; timeout/recovery and continuous lease | UNASSIGNED | NOT RECEIVED |
| C | #1164 / subsequent clean job | NEW reservation and real completion after reconciliation/absence proof | UNASSIGNED | NOT RECEIVED |

Exactly N/P/S/B/L are the five scenario obligations: one normal and four
recovery scenarios. R may reference the SAME actual attempt as N only when
its predeclared inputs, complete execution, source-manifest size and receipts
explicitly satisfy N, with the producer's equivalence decision retained.
Record one attempt and two role references, not two archives or two scenarios.
No attempt can satisfy two different scenarios. H0 is not normal completion;
C must be a different new job after recovery. Do not drop R when it cannot
satisfy N; run planning remains the operator's responsibility.

Maintain an actual-attempt ledger separately from these slots. Its identity is
(service/queue installation identity, job ID, attempt token). Attach ALL exact
GitHub (repository, workflow path/ref, run ID, run-attempt number, job ID)
observations to that identity. One retry of a transport/export/download is not
a new service execution. A new service token IS a new attempt, even if its job
or scenario is unchanged. Include failed, interrupted, superseded, unfinalized
and ambiguous attempts; use a provisional record with unresolved identity
rather than guessing a token. Retain old records when resolving the identity.
Reconcile the ledger against the producer's full campaign/journal inventory,
not only successful workflow jobs. Deduplicate by authenticated identity, not
filenames, scenario labels, outcome or matching archive size.

## 2. Per-attempt evidence record

For each real attempt, copy this record table into its issue/evidence receipt.
Use `NOT RECEIVED`, `NOT RUN`, `FAIL`, `UNVERIFIED`, or a specific blocked reason
until the underlying observation exists. Use `NOT APPLICABLE` only with an
explicit, reviewed justification; it cannot erase required evidence. Do not
invent literal placeholders as service IDs, hashes, URLs or timestamps.

| Record group | Required fields and references |
| --- | --- |
| Identity and coverage | Stable attempt key; role/scenario; supersedes/retry/alias references; workflow path/ref/commit/tree, run URL/ID/attempt and job URL/ID; requester and separate approval/release references; predeclared idempotency key; request bytes/digest; authenticated principal and effective UID/GID |
| Installation and inputs | Operator handoff/signoff; installed commit/tree; base and candidate commit/tree-to-source-manifest mapping; manifest bytes/count/hash (normal row must exceed 4 KiB); source closure; service/build-driver/harness/gateway/broker/helper SHA-256 and bootstrap/dependency identities; recipe/profile/schema identities; separate exporting service hash |
| Host and lifecycle | Host/boot/kernel/manager identity; stable lease device/inode and ownership timeline; outer and every stage unit/invocation/cgroup identity; stage manifests/journal sequence range; exact trigger/action/time; observed terminal phase, outcome, validity, failure reason, reconciliation/quarantine decision |
| Service authority | Exact `gateway result` receipt/reference and authenticated origin; job/token; request/manifest/bundle/full-result digests; service-owned result binding; finalization/exportability decision; no candidate-supplied authority |
| Export invocation | Operator/window authorization; command/tool identity; invocation ID and times; binary stdout file; complete stderr and exit code; independently retained `export-receipt-sha256`; failed/partial invocation disposition; exporter error text |
| Bytes and contents | Whole `.bqexport` file byte count/SHA-256; receipt bytes/SHA-256; archive payload bytes/SHA-256; raw-file bytes, regular-file and entry counts; manifest/bundle/control digests; receipt chunk-index digest and declared chunks/shards when applicable; no silently omitted controls |
| Durable publication | Existing #510 destination approval and publisher identity; exact immutable location plus object/release/asset/version identifiers; retention/no-replace policy evidence; publication tool/source/version; command, timestamps, stdout/stderr/exit; unchanged whole-file bytes/hash; associated authority and lifecycle receipt locations/digests |
| Independent retrieval | Separate consumer identity, host/workspace and access; exact immutable retrieval locator/version; retained retrieval command/tool/time/exit/logs; expected byte count/hash from authenticated publication handoff; actual downloaded bytes/hash; no producer-directory/cache reuse |
| Reconstruction | Trusted replay source commit/tree, binary SHA-256, build/dependency receipt; independently supplied expected receipt digest; fresh absolute destination/private-parent checks; exact unpack command/stdout/stderr/exit; binding and count checks; recipe-specific replay command/tool/output/exit or reason not applicable |
| Physical cleanup and next admission | Authenticated absence and lease receipts with job/boot/unit/cgroup binding; no final `.lease-handoff`, no active/ambiguously failed outer/stage unit, no populated owned cgroup or descendants; continuous lease through final absence, then release; boundary to next reservation; new clean job identity; dispatch-disabled readback |
| Disposition | Separate integrity, binding, recipe-replay, execution, cleanup and retention results; remaining blocker, responsible owner and exact missing evidence; review identity/time; immutable references for this record and all unsuccessful retrieval/replay attempts |

Condensed publication view (repeat once per actual attempt, linking its detail):

| Attempt / roles | Workflow run-attempt / job | Service job / token | Export exit / receipt SHA | Immutable object / bytes / file SHA | Download / unpack / recipe replay | Outcome / validity | Cleanup / continuous lease / next job | Retention or remaining blocker |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| NOT RECEIVED | NOT RECEIVED | NOT RECEIVED | NOT RUN | NOT PUBLISHED | NOT RUN | UNVERIFIED | NOT RECEIVED | Await #1163/#1164 handoff |

### Do not conflate three SHA-256 domains

The downloaded `.bqexport` is the 1,024-byte `BQEXP001` receipt followed by the
archive payload. Record the SHA-256 and length of the ENTIRE published file
for the publication round trip. `export-receipt-sha256` hashes just the receipt;
it must come from the authenticated successful export channel, independently
of the downloaded bundle. The receipt's archive bytes/hash (offsets 24/304)
cover only the payload, not the first 1,024 bytes. Thus whole-file length must
equal 1,024 plus receipt archive length. These values are not interchangeable.
The production unpacker checks the pinned receipt and payload and reruns the
existing binding validator. Do not introduce a custom parser as the authority.

## 3. Producer handoff and successful-export boundary (NOT RUN here)

Process each safely finalized attempt as it becomes available; do not wait
until the campaign ends. Finalized failed/cancelled/interrupted results still
require the normal export path. For a non-finalized or unavailable result use
section 6, not an improvised transfer.

Before export, obtain the operator's quiet-window release, exact service-authoritative
job/token/full-result digest and source/lifecycle receipts. An unknown identity,
missing approval or unresolved cleanup means BLOCKED. Only the reviewed operator
may execute the existing authenticated command. A gateway status line is not
an archive, and neither local `protocol`/`rpc`, SSH/scp, candidate output nor a
host-local result path is an alternative authority or transfer route.

The following Bash capture fragment documents the existing command only. It
is not an authorization, runner step or installed wrapper. `CAPTURE_DIR` is a
new empty private directory in the approved capture environment; all values
come from the trusted handoff. Keep stdout binary and stderr separate. The
subshell prevents capture settings from modifying the caller's shell.

```sh
(
    set -euC
    umask 077
    cd -- "${CAPTURE_DIR:?new approved private capture directory required}"
    if /usr/local/libexec/buster-bench-service gateway export \
        "${JOB:?authenticated job required}" \
        "${ATTEMPT:?authenticated token required}" \
        "${FULL_SHA:?service-authoritative full-result digest required}" \
        > result.bqexport 2> export.stderr; then
        export_rc=0
    else
        export_rc=$?
    fi
    printf '%s\n' "$export_rc" > export.exit
    test "$export_rc" -eq 0
)
```

Stop on nonzero status; retain partial stdout/stderr/exit under the authorized
failure-retention policy, never as a successful export. On zero, require and
retain the exact stderr receipt digest via the authenticated operator handoff;
check identity correspondence before publication. Do not recover the expected
digest by hashing the receipt inside the candidate/downloaded archive. Hashing
that receipt is a consistency check only, not authentication.

## 4. Exact-byte durable publication and independent retrieval

1. Confirm the existing approved #510 destination, publisher authorization,
   no-replace/version identity, consumer read access and retention scope for
   THIS attempt and its companion receipts. Historical #510 release names are
   precedent, not automatic approval to append new smoke evidence to them.
   Missing destination or permission stays BLOCKED; do not create another
   service/workflow, request broader runner access or treat Actions retention
   as durable storage.
2. Record the successful export's whole-file byte count and SHA-256. Publish
   the unchanged file through that existing authorized path; preserve its
   1,024-byte receipt, every archive byte and original failures. Do not unpack,
   repack, normalize line endings, trim, reconstruct or replace the original.
   Record tool/publisher, command, times, exit and immutable object/version.
   Retain receipt/lifecycle references with the same durability and access.
3. On a different off-host consumer workspace, receive the expected whole-file
   length/hash, receipt digest and identities through the authenticated handoff.
   Do not take authority solely from the object, adjacent untrusted metadata,
   its name, or an embedded receipt. Record who authenticated each expectation.
4. Create a fresh private workspace on a clean Linux consumer. Obtain replay
   tools from reviewed pinned sources/build receipts, NOT the evidence bundle.
   Verify ownership/modes and all parent path components, no symlink/untrusted
   writers, destination length supported by the reviewed utility, free space
   for the archive plus reconstructed files/logs, and no inherited producer
   checkout/cache/mount or host-local result path. Do not mount the measurement
   host's directories. The execution host is not the replay workspace.
5. Download the named immutable object/version afresh through the approved
   read path into a new local filename; record command/tool/logs/exit and time.
   No local copy from the producer or successful old cache hit is a fresh
   independent download. Require exact whole-file length/hash equality with
   the authenticated publication receipt before unpacking. Preserve interrupted
   retrievals under separate invocation IDs; retry to a new file/workspace.

The exact publication/download commands are supplied by the approved #510
publisher/consumer path, not guessed by this packet. Record them before use.
No destination, credentials or real exports have been supplied by this template.

## 5. Clean reconstruction and independent binding review

Only after section 4 passes, choose a new absolute `REPLAY_PARENT/result` that
does not exist under the verified private trusted parent. `ARCHIVE` is the
absolute path of the just-downloaded unchanged file. `REPLAY_TOOL` is an
absolute path to the independently verified reviewed Linux service utility.
`RECEIPT_SHA` is the independently authenticated export digest, never derived
as authority from `ARCHIVE`. All log names below must be new as well.

```sh
(
    set -euC
    umask 077
    : "${REPLAY_PARENT:?verified fresh absolute private parent required}"
    : "${ARCHIVE:?verified downloaded absolute archive path required}"
    : "${REPLAY_TOOL:?verified trusted absolute tool path required}"
    : "${RECEIPT_SHA:?independently authenticated export receipt required}"
    if "$REPLAY_TOOL" unpack-export "$ARCHIVE" \
        "$REPLAY_PARENT/result" "$RECEIPT_SHA" \
        > "$REPLAY_PARENT/unpack.stdout" \
        2> "$REPLAY_PARENT/unpack.stderr"; then
        replay_rc=0
    else
        replay_rc=$?
    fi
    printf '%s\n' "$replay_rc" > "$REPLAY_PARENT/unpack.exit"
    test "$replay_rc" -eq 0
)
```

This calls the production `bq_export_unpack` path, including
`bq_worker_result_binding_validate_at`, not a substitute tar/ZIP extractor.
It validates the receipt, exact archive length/hash, paths, entry/file/raw-byte
counts, exhaustive bundle and original full-result bindings without opening
recorded host paths or executing downloaded files. Capture the actual command
arguments, source/binary identity and times separately; a zero exit may have
empty stdout. Do not edit the retained manifest's original `result-root`.
A failure can leave a partial destination: retain it and its logs as failed
replay evidence; never overlay it, delete it to hide failure, or retry in place.

Independently reconcile the reconstructed request/source/profile/service/job/
attempt identities with the authenticated producer handoff and publication
receipt. Check host/boot/lease and lifecycle bindings through the original
manifests and separately authenticated operator receipts. Unpack integrity
alone does not authenticate a missing external handoff or prove physical host
absence, continuous lease ownership, or the later new reservation.

For a complete smoke throughput result, use the independently pinned trusted
harness's documented command, after the outer/bundle checks:

```sh
"$TRUSTED_THROUGHPUT_TOOL" compare --output "$RETRIEVED_THROUGHPUT_DIRECTORY"
```

Resolve that directory from the validated reconstructed inventory (not the
original host path), and retain tool/source/binary identity, argv, times,
stdout/stderr and actual exit exactly as for unpack. Check the pinned harness
contract before running it; never execute a harness copied from the bundle.
Use a separate derived working copy if that reviewed replay command writes
outputs, preserving the downloaded archive and reconstructed source evidence.
For partial/failure bundles without complete throughput inputs, record the
recipe-replay limitation and expected scenario outcome; do not manufacture
inputs or call that stage successful. Apply no retirement validator or
performance threshold to this smoke packet.

Keep `integrity_replay`, `binding_review`, `recipe_replay`, `execution_outcome`,
`measurement_validity`, `statistical_decision`, and `physical_cleanup` separate.
A failed service attempt may pass integrity replay. Preserve literal raw
validity/outcome values; smoke measurement validity and statistical decision
remain not evaluated. A successful smoke replay is not correctness-suite,
self-host, A/A qualification, retirement-recipe or #511/#512 acceptance.

## 6. Missing evidence and negative-case checklist

| Condition | Required disposition |
| --- | --- |
| H0 or another non-finalized/unexportable attempt | Preserve reported failure, exact available identities/journal/partial manifests and typed export-unavailability reason. Operator supplies the separately reviewed authorized retention path, immutable locations and reconciliation receipt. Do not fabricate a final manifest/export digest or bypass private state access. Until resolved, #1165 remains blocked. |
| Interrupted/failed export or download | Keep partial bytes, command, status and diagnostics as failed transfer evidence; no success publication. New transfer invocation ID, same authenticated service identity; no new service submission. |
| Same-attempt export retry after daemon restart | Operator-owned, only in an approved quiet window. Compare the same receipt and exact bytes; preserve any mismatch/error. This packet authorizes no restart or sealed/pending-file repair. |
| Missing/wrong/stale/substituted authority | Stop before acceptance. An archive and a matching digest supplied together by an untrusted party do not authenticate one another. |
| Corrupt/truncated/trailing bytes, missing/duplicate/unlisted entries or unsafe paths | Require production validator rejection; preserve exact input hash, tool, command, output and status. Never relax bounds or rebuild the archive to make it pass. |
| Existing destination, symlink/untrusted parent or interrupted reconstruction | Do not reuse/overwrite. Preserve evidence and allocate a different verified fresh private destination. |
| Missing cleanup/lease/next-job receipts or mismatched boot/unit identity | Integrity pass remains separate; physical evidence is UNVERIFIED/FAIL. Host operator retains quarantine/admission authority; the off-host consumer cannot infer absence from an archive. |
| Missing durable destination/authority/receipt access | BLOCKED with owner and missing approval/reference. A local file, release name, expiring Actions artifact or inaccessible receipt is not completion. |

These are review/replay cases, NOT claimed test results. Any optional off-host
corruption/collision exercise uses a disposable COPY, separate case identity
and the existing production unpacker/tests; never modify published originals,
forge authoritative receipts, or change host state. Label fixtures as fixtures and
keep their outcomes out of the live coverage matrix. Do not require speculative
new negative tests as another producer pre-merge gate.

## 7. Consolidated report and completion decision

Publish one versioned consolidated matrix linked from #1165, #880 and #437.
Keep the detailed per-attempt records and their immutable authority, lifecycle,
publication and replay receipts reachable from every row. Have the producer
reconcile full attempt coverage and the independent consumer sign its actual
retrieval/binding/replay results. Record every unresolved item and its owner.
A later report supersedes a summary, not the original attempts or receipts.
Never post credentials in issue/PR receipts; use the approved access-controlled
retention path for sensitive operator records without editing archive bytes.

- [ ] All actual attempts, including H0 and unsuccessful/retried history, are
  accounted for; five distinct scenarios and the later new job are mapped.
- [ ] Every finalized attempt has successful authenticated export, unchanged
  durable publication, independent fresh retrieval and full integrity/binding
  replay, with applicable recipe replay and original outcomes retained.
- [ ] Byte/hash domains, exact identities, counts and all companion receipts
  agree; missing authority or content is not silently marked not applicable.
- [ ] Every inter-attempt absence/continuous-lease boundary and subsequent new
  completion is independently checked against authenticated operator evidence.
- [ ] Non-finalized/unexportable attempts have resolved authorized durable
  failure retention and reconciliation; no fabricated successful export.
- [ ] #880/#437 link the consolidated matrix; #880's remaining original gates
  are assessed separately by its owner rather than closed automatically.

Until those boxes have real receipts, report **PREPARED; LIVE EVIDENCE BLOCKED**.
No checkbox is satisfied by this template, a documentation merge, fixtures,
capacity arithmetic or a successful unrelated CI job. This packet does not
qualify #422/#426, admit #881, approve #511/#512, or close #36.
