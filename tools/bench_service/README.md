# Native durable queue: first software slice of #437

This is a local, one-shot **fake-worker** control tool, not the dedicated-host
service. It does not execute source, spawn workers, acquire the measurement
host's inherited lease, change server configuration, listen on a socket,
authenticate callers, measure performance or qualify a 9700X. Do not close #437
or accept compiler performance changes because these tests pass.

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
Windows). Tests use private, disposable directories with deterministic scenarios;
no sleeps, timing assertions, external workers or network access are involved.
Windows validates the portable codec and rejects durable queue opening as
unsupported. **Journal durability and fake execution are POSIX-only in this
slice**; native Linux/macOS test evidence must not be described as Windows
storage support.

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

Limits are fixed in schema 1: eight unfinished jobs (including active and
cleaning), 64 lifetime submissions, 1,024 journal events, 320-byte request
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
| 8 | 4 | schema, exactly 1 |
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

A complete unacknowledged write may survive a crash and is replayed. An
incomplete unacknowledged write is discarded as above. Acknowledged writes are
not intentionally removed. Lost acknowledgments are resolved by submission
idempotency, not by claiming exactly-once execution.

Event payloads are canonical request bytes for submit; `(job, token)` as two
64-bit integers for reserve/reconcile; `(job, token, next_phase, outcome)` as
8+8+4+4 bytes for advance; and an eight-byte job ID for cancel. Submit sequence
is the job ID. Reservation sequence is the immutable fake-attempt token.

## Admission, identities and fake execution

The submission is five consecutive `(u32 byte_count, bytes)` fields: principal,
idempotency key, installed recipe, base source ID and candidate source ID.
Principals (1..32 bytes) and keys (1..64 bytes) use ASCII letters, digits, `.`,
`_`, `-`. Source IDs are full lowercase 40- or 64-hex digests of the same width,
not branch names, abbreviated revisions or shell strings. This fake slice
validates identity syntax only: it does not fetch or prove that commits exist.

Recipes `fake-success-v1` and `fake-failure-v1` have immutable schema-1 meanings:
repository `buster`, workload `fake-steps-v1`, profile `unmeasured`, toolchain
`none`, oracle `fake-v1`. The CLI cannot supply flags or arbitrary programs.
Future real recipes require their own pinned manifest and execution validation.
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
because **schema 1 cannot spawn an external worker**. A real service must prove
boot/unit/process ownership, cleanup and inherited-lease state; this fake
operation is not a real-host force-unlock interface.

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

The 24-byte header is `BQP1` (four bytes), schema (u32=1), operation (u32), payload
length (u32 <=512), correlation (u64). Truncation, trailing bytes, unsupported
versions/operations, malformed payloads and oversized lengths fail. Response
operation is request operation OR `0x80000000`; correlation is echoed for a
valid envelope. Every response payload begins with a four-byte `BqError` code
(see `queue.h`); nonzero codes must be handled before parsing a success payload.

Operations 1..8 are capabilities (empty), submit (canonical request), status
(job u64), result (job u64), cancel (job u64), logs (job u64, after-sequence u64),
fake-run (empty), fake-reconcile (job u64, token u64).

Status-like replies have 120-byte bodies: error at 0; job/token/journal-sequence
u64 at 4/12/20; phase/outcome/validity/cancel-intent/reconciliation/pending/retained
u32 at 28/32/36/40/44/48/52; request digest (64 hex bytes) at 56. Capabilities
returns fixed UTF-8 text after the error code. Logs returns count at 4 (u32),
next cursor at 8 (u64), more at 16 (u32), then up to four 32-byte event entries:
sequence u64, job u64, kind/phase/outcome/validity u32. A client follows `next`
only when `more` is nonzero. Output memory never scales with input lengths.

## Requirement mapping and remaining gates

| #437 first-slice requirement | Implementation / deterministic native coverage |
| --- | --- |
| Durable bounded single-writer journal | `bq_open`, `bq_append`, `bq_replay`; competing handles, real short writes, sync/ack faults |
| Bounded framing, sequence, checksums | Canonical encoding; every partial final-frame prefix; header/payload corruption; duplicate sequence; oversized lengths; checksum-valid illegal transitions |
| Idempotency and FIFO admission | Same retry after lost stdout/after-sync acknowledgment; conflicting keys; principal separation; queued cancellation; pending and lifetime exhaustion |
| Explicit active ownership | Persisted fake reservation token; stale-token rejection; one-active through cancellation/cleaning; restart at every phase |
| Recovery without exactly-once fiction | Poisoned I/O handles; partial/full reservation boundaries; reconciliation required before reuse; recorded outcome preserved |
| Bounded CLI/control operations | Same dispatcher for CLI and binary frames; truncation/version/length/numeric validation; bounded journal-event pagination |
| Separate execution outcome/validity | Successful, failed, cancelled and interrupted fake jobs remain not-evaluated |

These are new-feature acceptance tests, not a claim of a pre-existing queue bug
reproduced on main. Main has no `bench_service` implementation. Exact candidate
SHAs, commands and observed CI results belong in the PR evidence; this document
is not an assertion that unrun gates passed.

Still outside this slice: real recipes/source materialization; systemd and
containment; inherited measurement lease integration; boot/unit reconciliation;
transport/authentication; multi-client service loop; deployment and retention
migration; qualification, A/A, physical 9700X acceptance and real result bundles.
No credentials, server settings, benchmark thresholds, production runner
ownership or parent-issue closure are authorized by this implementation.
