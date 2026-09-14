# Native queue and installed-input materializer for #437

This is a local, one-shot control tool with a deterministic fake worker and a
real installed-input materialization boundary. It is not yet the dedicated-host
service. It does not execute source, spawn or contain workers, acquire the
measurement host's inherited lease, change server configuration, listen on a
socket, authenticate callers, measure performance or qualify a 9700X. Do not
close #437 or accept compiler performance changes because these tests pass.

## Build and registered tests

The existing native `build.c` driver owns compilation and execution:

```sh
./build.sh bench_service capabilities
./build.sh bench_service self-test
./build.sh bench_service self-test --sanitize
```

`test_all_combinations` runs the normal service self-test beside the existing
throughput self-test on each desktop lane, and also runs its AddressSanitizer
and UndefinedBehaviorSanitizer variant on POSIX hosts. Existing compiler,
throughput, sanitizer and workflow gates are retained. `shared.c` is the same
foundation linkage used by the throughput tool. There is no new dependency,
measurement loop or general-purpose testing framework.

The normal native executable is `build/bench-service-tools/service` (`.exe` on
Windows). Tests use private, disposable directories with deterministic source manifests;
no sleeps, timing assertions, external workers or network access are involved.
Windows validates the portable codec and rejects durable queue opening as
unsupported. **Journal durability and fake execution are POSIX-only in this
slice**; native Linux/macOS test evidence must not be described as Windows
storage support.

The optional materializer additionally takes two absolute, bounded paths from
the trusted local worker/operator interface, never from a submitted job:

* `INSTALLED_ROOT` is an operator-provisioned, mode-read-only tree. The selected
  recipe, source directories, manifests, and source files must have no write bits.
  Every absolute-path component is opened relative to its verified parent with
  symbolic-link following disabled.
* `WORKSPACE_ROOT` is an already provisioned private local directory owned by
  the service account. Every attempt uses an exclusive deterministic child and
  refuses an existing child rather than deleting or reusing it.

These paths configure installed policy and job storage. They do not add
request-controlled commands, flags, source paths, or executable paths. The
eventual worker unit must pin them; this unauthenticated local interface must
not be exposed to untrusted callers.

POSIX write-bit removal prevents accidental writes but is not an immutable-file
security boundary against the owning effective UID, which can restore write
permission. Tests explicitly exercise that limitation. A production unit must
install policy/source inputs under a distinct operator identity and must not run
an executable worker with the materializer's storage authority. This slice has
no executable worker; privilege separation or a platform sealing facility is a
later containment gate.

## Ownership and storage contract

The caller supplies an **already provisioned, durable, private local POSIX
directory**, owned by the current user with no group/other permissions. Its
parent-directory provisioning is outside this tool. Do not use a network
filesystem, replace files underneath the tool or copy a live queue. Storage
must honor `fsync` and advisory locking; hardware power-loss guarantees are not
established by CI.

`writer.lock` is a separate, stable-inode, nonblocking exclusive `flock` held
until the handle closes. Production code never unlinks it. Journal and lock
files must be private regular single-link files; final-component symlinks are
rejected. This excludes concurrent cooperating writers, including separate CLI
invocations. It is **not** #429's inherited measurement-host lease.

All state changes are validated on a bounded tentative state, encoded, fully
written with short-write/EINTR handling and `fsync`ed before being applied to
caller-visible state or acknowledged. File and directory creation entries are
synced on opening, including an empty journal left by an interrupted creator.
Any append/sync uncertainty poisons that handle: close/reopen and reconcile or
retry, never roll it back in memory and continue appending.

Limits are fixed across journal schemas 1 and 2: eight unfinished jobs
(including active and cleaning), 64 lifetime submissions, 1,024 journal events, 320-byte request
payloads, 480-byte maximum journal frames and 536-byte control frames. Normal
job transitions use fewer than 16 events each. There is no compaction, rotation,
expiry or tombstone eviction. Once all 64 lifetime slots are used, new keys fail
closed even if every job has finished; existing identical retries still work.
A migration/retention scheme is a subsequent slice, not silent deletion of
accepted identities.

## Journal wire format and recovery

All integers are unsigned little-endian. Every record has a 160-byte header:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 8 | `BQJNL001` magic |
| 8 | 4 | schema, legacy 1 or current 2 |
| 12 | 4 | event kind: submit/reserve/advance/cancel/reconcile = 1..5 |
| 16 | 4 | bounded payload byte count |
| 20 | 4 | reserved, must be zero |
| 24 | 8 | sequence, starting at 1, increasing by exactly 1 |
| 32 | 64 | lowercase SHA-256 hex of header bytes 0..31 then bytes 96..159 |
| 96 | 64 | lowercase SHA-256 hex of payload |
| 160 | variable | event payload |

Checksums reuse the repository SHA-256 implementation. They detect corruption;
they are not authentication or tamper-proof evidence. Replay also checks event
sizes, installed recipes, unique keys, FIFO order, ownership tokens and legal
state transitions, not merely checksums.

An incomplete **final** record is a short header, or a complete valid header
with the expected sequence and an incomplete payload. Recovery truncates only
that final incomplete record to its starting offset, syncs the truncation, and
reports its byte count on the queue handle. A complete corrupt record, invalid
full header, oversized length, unknown version/kind, sequence gap/duplicate or
illegal transition causes opening to fail without truncation. The parser never
scans forward for a convenient later record. An entirely removed accepted
suffix cannot be detected without an external witness; such loss violates the
assumed durable local-storage contract and is not a supported repair path.

Schema 1 is the exact fake-recipe format published by the queue-only slice.
Schema 2 retains the same framing and event kinds but admits the pinned real
recipe and its fail-to-cleaning transition. Replay accepts a schema-1 prefix
followed by schema-2 records and rejects a 2-to-1 downgrade. Opening a legacy
journal does not rewrite it; the first subsequent mutation appends a durably
versioned schema-2 record. Old binaries cannot consume schema 2 and fail closed
at its first header; they must not open a journal after it has been upgraded.

A complete unacknowledged write may survive a crash and is replayed. An
incomplete unacknowledged write is discarded as above. Acknowledged writes are
not intentionally removed. Lost acknowledgments are resolved by submission
idempotency, not by claiming exactly-once execution.

Event payloads are canonical request bytes for submit; `(job, token)` as two
64-bit integers for reserve/reconcile; `(job, token, next_phase, outcome)` as
8+8+4+4 bytes for advance; and an eight-byte job ID for cancel. Submit sequence
is the job ID. Reservation sequence is the immutable attempt token.

## Admission, identities and fake execution

The submission is five consecutive `(u32 byte_count, bytes)` fields: principal,
idempotency key, installed recipe, base source ID and candidate source ID.
Principals (1..32 bytes) and keys (1..64 bytes) use ASCII letters, digits, `.`,
`_`, `-`. Source IDs are full lowercase 40- or 64-hex digests of the same width,
not branch names, abbreviated revisions or shell strings. Fake recipes validate
identity syntax only; the real recipe below verifies an installed snapshot.

Recipes `fake-success-v1` and `fake-failure-v1` have immutable schema-1 meanings:
repository `buster`, workload `fake-steps-v1`, profile `unmeasured`, toolchain
`none`, oracle `fake-v1`. The CLI cannot supply flags or arbitrary programs.
`validate-buster-v1` is the first real materialization recipe. Its exact installed
bytes are checked against `profiles/validate-buster-v1.recipe`; it still does
not execute a build. The recipe's own `schema=1` field is independent of the
journal and control schema numbers.
The request digest is SHA-256 of `BQ-request-v1` followed by the canonical
request bytes, not an in-memory C structure with padding.

An identical principal/key/request returns the original job ID, even after
completion or capacity exhaustion. A reused principal/key with any changed
request byte fails. Different principals have separate key namespaces. New
work is admitted FIFO; terminal cancelled jobs are skipped. The principal is
**an assertion of a trusted local caller with directory access**, not an
authenticated network identity. Do not expose this tool to untrusted callers.

Phase is separate from execution outcome and benchmark validity:

* Phases: queued, reserved, preparing, settling, measuring, finalizing, cleaning,
  finished (numeric values 0..7).
* Outcomes: none, succeeded, failed, cancelled, interrupted (0..4).
* Validity: not-evaluated, valid, invalid (0..2). This fake slice only ever emits
  **not-evaluated**, including for succeeded jobs. No statistical decision exists.

The worker is synchronous, in-process and bounded. It persists reservation
before receiving a token and moves through the real journal transitions.
Only one reserved/active/cleaning job exists. Cancellation of queued work is
terminal; active cancellation is persisted as intent and does not free the
slot until cleaning/finish is committed. Cancellation after execution has
already finalized is an idempotent no-op, not a fabricated cancelled execution.

Reopening any nonterminal active job sets `needs_reconciliation`; dispatch and
ordinary fake advancement stop. The explicit `fake-reconcile JOB TOKEN`
operation validates the persisted token and finishes interrupted work without
rerunning it. If execution outcome was already recorded during finalization,
it is preserved while cleanup is reconciled. This operation is justified only
because **fake recipes cannot spawn an external worker**. A real service must prove
boot/unit/process ownership, cleanup and inherited-lease state; this fake
operation is not a real-host force-unlock interface.

## Installed snapshots and sealed attempt workspaces

The materializer reserves the next FIFO job before reading installed inputs, so
validation and copying stay inside the queue's existing whole-job authority. A
fake operation refuses a real job, and a materialization operation refuses a
fake job without reserving it.

Each requested identity selects `INSTALLED_ROOT/sources/<identity>`, containing
a read-only `source.manifest`:

```text
BQ-SOURCE-V1
repository=buster14a/buster
revision=<exact 40- or 64-hex request identity>
<lowercase file sha256> <strict relative path>
...
```

The manifest is capped at 64 KiB and 4,096 sorted unique entries; individual
files are capped at 64 MiB, each snapshot at 512 MiB, and each copied source
tree at 480 directories. Empty components, `.`, `..`, absolute paths,
backslashes, symbolic-link traversal, writable/nonregular source files, hash or
revision mismatch, missing files, duplicates, and unsorted paths fail closed.
Recipe, manifest, and source-file leaves are opened nonblocking and type-checked
before reads, so FIFOs and devices are rejected rather than waited on.
Only listed, verified bytes are copied, so unlisted installed files cannot
affect a job. These are implementation limits, not a claim that an operator has
installed a complete Buster snapshot.

An admitted attempt is `WORKSPACE_ROOT/job-<job>-attempt-<token>` with disjoint
`base/source`, `base/build`, `candidate/source`, and `candidate/build`
directories. Source copies and their directories become mode-read-only after
hash verification. Build directories remain writable and are never shared, so
`generate` on one subject cannot delete a tree used by the other subject or by
another attempt. A read-only `.identity` binds the directory to job, token,
request digest, recipe, and both source identities before the preparing event
is persisted.

Every attempt whose roots validate first publishes a mode-read-only
`attempt-<job>` record binding both configured paths and root device/inode
identities. Recipe/source/materialization failures publish a mode-read-only
`failure-<job>` record in the queue directory before the existing journal advances to cleaning.
The record binds job, attempt token, request digest, and a bounded reason.
Status and result responses retain that reason after restart. Admission is
released only after the seal-matched workspace has been completely removed and
the finished event is durable. A stale directory, altered seal, ambiguous
failure record, directory-count overflow, or failed removal keeps the job active
and reconciliation required. Cleanup publishes an idempotent `cleanup-<job>`
inode binding before it changes either journal phase or workspace contents;
`cleanup-failure-<job>` durably overrides the reported reason when removal
fails. Cleanup walks already-open directory descriptors, verifies child and
root inode identities, preserves `.identity` until all other removals are
synced, and is bounded at 1,024 directories, 256 nested directory descriptors,
and 16,384 entries. Native coverage exercises a 220-level writable build tree;
deeper or larger unexpected output fails closed with the active job retained.
It never follows symbolic links.

`workspace-reconcile` is accepted only after reopen has marked the active job
as needing reconciliation. It may remove only the deterministic directory
whose seal matches the active job/token. A missing directory is accepted only when the
journal says preparation did not complete or cleaning already began; a missing
prepared workspace is an error. This does not inspect cgroups or prove process-
tree cleanup, and mode-read-only files remain mutable by the same effective
UID, so no real command can execute until the later Linux supervisor
and containment slice supplies that boundary.

There is one narrower crash case before root provenance exists: a durable real-
job reservation can survive before `attempt-<job>` is written. Reconciliation
accepts that state only while reserved (or while cleaning a recorded
configuration failure), and only when a valid private workspace root has no
deterministic attempt entry. A recorded configuration failure also permits an
unopenable root because workspace creation occurs only after the attempt record.
All other missing-attempt combinations retain active admission.

## CLI

Use a pre-provisioned durable private directory, represented here by `STATE`:

```sh
./build.sh bench_service submit STATE alice request-1 fake-success-v1 \
  1111111111111111111111111111111111111111 \
  2222222222222222222222222222222222222222
./build.sh bench_service status STATE 1
./build.sh bench_service fake-run STATE
./build.sh bench_service result STATE 1
./build.sh bench_service logs STATE 1
./build.sh bench_service logs STATE 1 4
./build.sh bench_service cancel STATE 1
# Only after recovery of a crashed fake attempt; use its actual persisted token:
./build.sh bench_service fake-reconcile STATE JOB TOKEN
# Trusted worker/operator paths, not submitted request fields:
./build.sh bench_service materialize STATE INSTALLED_ROOT WORKSPACE_ROOT
./build.sh bench_service workspace-reconcile STATE WORKSPACE_ROOT JOB TOKEN
```

`status` and `result` report phase/outcome/validity independently. `logs` pages
journal control events, not nonexistent worker stdout or benchmark artifacts.
Integer arguments must parse successfully and consume the entire argument.
An output failure after persistence is reported as uncertain I/O; retry the
same principal/key/request to recover its ID. No mutation is rolled back merely
because stdout or the response channel failed.

## Bounded one-frame control protocol

`bench_service protocol STATE` accepts exactly one frame followed by EOF on
stdin, and writes one response frame. It is a local pipe codec, not a daemon,
network transport, timeout policy or authentication service. Human CLI commands
use this same dispatcher.

The 24-byte header is `BQP1` (four bytes), schema (u32), operation (u32), payload
length (u32 <=512), correlation (u64). Control schema 2 adds operations 9/10 and the
failure field while retaining schema 1 request/response behavior and its
120-byte status body. Truncation, trailing bytes, unsupported versions or
operations, malformed payloads, and oversized lengths fail. Response operation
is request operation OR `0x80000000`; schema and correlation are echoed for a
valid envelope. Every response payload begins with a four-byte `BqError` code
(see `queue.h`); nonzero codes must be handled before parsing a success payload.

Operations 1..10 are capabilities (empty), submit (canonical request), status
(job u64), result (job u64), cancel (job u64), logs (job u64, after-sequence u64),
fake-run (empty), fake-reconcile (job u64, token u64), materialize (installed
path length u32, workspace path length u32, then both paths), and
workspace-reconcile (job u64, token u64, workspace path length u32, path).
Both paths are absolute and at most 192 bytes.

Schema 2 status-like replies have 124-byte bodies: error at 0; job/token/journal-sequence
u64 at 4/12/20; phase/outcome/validity/cancel-intent/reconciliation/pending/retained
u32 at 28/32/36/40/44/48/52; request digest (64 hex bytes) at 56; durable
materialization failure code at 120 (zero when absent). Capabilities
returns fixed UTF-8 text after the error code. Logs returns count at 4 (u32),
next cursor at 8 (u64), more at 16 (u32), then up to four 32-byte event entries:
sequence u64, job u64, kind/phase/outcome/validity u32. A client follows `next`
only when `more` is nonzero. Output memory never scales with input lengths.
Control-schema-1 clients may continue to inspect fake jobs, but status, result,
cancel, and logs reject real-recipe jobs as unsupported rather than omitting
schema-2 evidence. Corrupt or unreadable durable evidence also fails a schema-2
status/result response at the top-level error field.

## Requirement mapping and remaining gates

| #437 queue/materializer requirement | Implementation / deterministic native coverage |
| --- | --- |
| Durable bounded single-writer journal | `bq_open`, `bq_append`, `bq_replay`; competing handles, real short writes, sync/ack faults |
| Bounded framing, sequence, checksums | Canonical encoding; every partial final-frame prefix; header/payload corruption; duplicate sequence; oversized lengths; checksum-valid illegal transitions |
| Idempotency and FIFO admission | Same retry after lost stdout/after-sync acknowledgment; conflicting keys; principal separation; queued cancellation; pending and lifetime exhaustion |
| Explicit active ownership | Persisted fake reservation token; stale-token rejection; one-active through cancellation/cleaning; restart at every phase |
| Recovery without exactly-once fiction | Poisoned I/O handles; partial/full reservation boundaries; reconciliation required before reuse; recorded outcome preserved |
| Bounded CLI/control operations | Same dispatcher for CLI and binary frames; truncation/version/length/numeric validation; bounded journal-event pagination |
| Separate execution outcome/validity | Successful, failed, cancelled and interrupted fake jobs remain not-evaluated |
| Installed recipe/source validation | Exact recipe bytes; requested revision, safe sorted paths and source SHA-256; durable failure reasons |
| Per-attempt workspace isolation | Exclusive job/token identity; separate base/candidate source and build trees; read-only verified sources |
| Cleanup/recovery | Seal-required removal, restart reconciliation, collision/tamper/cleanup failures retain active admission |

These are new-feature acceptance tests, not a claim of a pre-existing queue bug
reproduced on main. Exact candidate SHAs, commands and observed CI results
belong in the PR evidence; this document is not an assertion that unrun gates
passed.

Still outside this slice: build/validate/compare execution; systemd and process
containment; inherited measurement lease integration; boot/unit reconciliation;
transport/authentication; multi-client service loop; deployment and retention
migration; qualification, A/A, physical 9700X acceptance and real result bundles.
No credentials, server settings, benchmark thresholds, production runner
ownership or parent-issue closure are authorized by this implementation.
