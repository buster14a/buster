# Native queued execution service boundary for #437

This is a local, one-shot control tool with a deterministic fake worker and a
real installed-input materialization boundary and a Linux containment
supervisor. Linux also provides a long-lived local service endpoint: it owns
the single queue writer, authenticates Unix peer credentials, and dispatches
bounded public control frames. Installed execution is selected through a
compiled recipe registry; request bytes never select a program or command.
The current executable recipe returns only typed build-driver failures until
its operator-installed dependencies are available.
This does not change server configuration, measure performance or qualify a
9700X.
Do not close #437 or accept compiler performance changes because these tests
pass.

## Linux installed worker boundary

`worker-run DIR INSTALLED_ROOT WORKSPACE_ROOT LEASE_FILE CPU` is the Linux
single-job supervisor. A request may name only an admitted service recipe;
`validate-buster-v1` is currently the sole admitted entry. It cannot supply a
program, argument, unit name, resource property, cgroup path or timeout. The
service constructs one fixed `/usr/bin/systemd-run --wait --service-type=exec`
invocation of `/usr/local/libexec/buster-bench-service worker-unit`, explicitly
as `--uid=buster-bench --gid=buster-bench`.
`worker-unit` authenticates the private result-root handoff and adopts the exact
stable lease open-file description before mapping the passed recipe identity
through the same compiled registry. Unknown, blocked or commandless entries
fail before execution. The active validation entry maps to the fixed
`/usr/local/libexec/buster-bench-build bench_service_recipe` entrypoint. The
build driver accepts exactly
`JOB_ID ATTEMPT_TOKEN WORKSPACE_ROOT BASE_REVISION CANDIDATE_REVISION RESULT_ROOT`;
it supplies the fixed Release generate/build policy and the fixed installed
throughput harness. No request field supplies a command, flag, executable or
additional path.

The transient service carries a fixed sandbox policy: the queue directory and
lease pathname are `InaccessiblePaths`; the installed policy tree is
`ReadOnlyPaths`; the private workspace is the only `ReadWritePaths`; and the
service sets `ProtectSystem=strict`, `PrivateTmp`, `PrivateDevices`,
`NoNewPrivileges` and `RestrictSUIDSGID`. The remaining fixed systemd policy
denies kernel/home/control-group exposure, namespace creation, realtime and
non-local address families. After each build, the trusted driver removes write
permission from every baseline/candidate build directory and file before
throughput starts. Baseline generation/build runs explicitly as the trusted
`buster-bench` identity. Candidate generation/build and the throughput harness
(which executes the request-selected candidate compiler) run explicitly as the
separate `buster-bench-candidate` identity in `candidate/staging`; they cannot
write the final candidate build, queue, lease, installed tree, or final result
surface. Throughput writes only a candidate-owned staging directory. After a
successful candidate build, the service UID copies the staged executable
into the private final candidate build with descriptor identity and digest
checks, atomically publishes it without replacement, and removes write
permission from every baseline and final candidate build directory and file
before throughput starts. After throughput exits, the trusted driver copies that
staging output into a fresh service-owned result subtree and then publishes the
final manifest, bundle and outcome evidence with no-replace links; the result
tree is never writable by the candidate identity and is replayed before success
is acknowledged.

Each fixed recipe stage helper uses a deterministic unit name linked with
`PartOf=`, `BindsTo=` and `After=` to its owning worker unit and uses
`CollectMode=inactive-or-failed`. Every nested
transient unit is placed in `buster-bench.slice` and
repeats the admitted CPU, memory, swap, task and runtime limits; the
coordinator observes the same properties on the outer unit before continuing.

For a fresh real job the server acquires the cooperative host lease before FIFO
reservation and materialization. It transfers the descriptor over a private,
peer-credential-checked result-root `SOCK_SEQPACKET` handoff and closes its own
copy only after the worker acknowledges receipt. The `.lease-handoff` socket is
unlinked by device/inode identity and the result directory is fsynced before
the helper is continued; a handoff cleanup failure fails the launch rather
than signalling CONT over a stale socket. The worker then owns that
descriptor while result or failure evidence becomes durable, while TERM/KILL
escalation runs, until `cgroup.events` is unpopulated and workspace
reconciliation durably releases the queue job. No later queue job can reserve
while any of those steps is uncertain.

Cleanup sends TERM, polls descriptor-validated recursive population every
100 ms for the configured 10-second grace, then sends KILL and polls for at
most another 10 seconds. Once the outer unit can no longer launch work, the
coordinator reacquires and retains the host lease, enumerates every deterministic
stage name, validates any surviving stage's boot, invocation, relationship and
cgroup identity, directly applies the same TERM/KILL escalation, and proves all
five stage units and cgroups absent. It reaps the service helper only after
those absence proofs. A start/observation failure that cannot prove physical ownership
retains the active queue admission and the live helper's inherited host lock;
it does not guess that a transient service disappeared.

All manager subprocess pipe reads and child waits use monotonic deadlines;
signal interruption cannot restart an unbounded relative wait. Fixed manager
commands have a five-second deadline, while the joined helper is bounded by
its fixed RuntimeMax plus cleanup allowance. If the helper did not inherit the
lease or already exited before ownership became observable, the live
coordinator retains its own lease descriptor in quarantine across recovery
calls. It releases that descriptor only after physical cleanup and durable
workspace/job reconciliation. A cancellation observed before a job exists
returns without dereferencing or reserving one.

A successful result is not committed across an unchecked signal window.
Cancellation is sampled after each durable success-phase transition and after
physical workspace removal. TERM/INT are masked at a checked boundary
immediately before the final journal append; that boundary is the completion
linearization point. A cancellation ordered before it makes the terminal
outcome `cancelled` on replay. If the success bundle was already published,
the terminal cancellation retains and binds that exact success bundle/digest
and publishes a separate cancellation outcome record; replay never replaces
or loses the durable artifact.

## Result bundle and retained evidence

The trusted recipe writes a `BQ-BUNDLE-V1` index beside the final manifest.
Every non-control result file is listed as `sha256 size relative/path`; the
worker reopens and hashes every listed file, rejects unlisted regular files,
symlinks, traversal components and non-private directories, and enforces
4,096 entries, 512 MiB total bytes, 64 MiB per file, 256 directory levels and
192-byte relative paths. The index itself is bounded to 8 MiB. The final
manifest, bundle and failure/cancellation outcome record are separately bound
control records, so later retrieval does not change the measured bundle. A
failed, cancelled, or interrupted recipe publishes a digest-bound
`BQ-BUNDLE-V1` index of every regular evidence file already present in the
result root (prepare and stage manifests, nested payloads and other trusted
non-control files), plus the terminal manifest, before workspace cleanup.
Unsafe evidence such as symlinks, FIFOs, foreign-owned or other-writable
objects fails closed instead of being silently skipped. A bundle-only crash
prefix is completed idempotently: the byte-identical existing bundle is
accepted and the manifest is generated against its digest. Invalid published
controls are never repaired, rewritten or replaced. Restart replay therefore
exposes the same artifact root and digests for every terminal outcome, not
only successful measurements.

All temporary publications use `O_EXCL`, `O_NOFOLLOW`, file fsync, a
no-replace `linkat`, temporary unlink and parent-directory fsync. A planted
target, stale temporary, crash-window race or post-build tamper fails closed;
no `rename` replacement is used.

If `systemd-run` starts but no trustworthy service identity can be bound, the
server cannot signal an unverified unit name. It instead kills and reaps the
tracked launcher PID under the command deadline while retaining the active job
and coordinator quarantine lease for the still-ambiguous service state.

Before CONT, the server verifies the exact boot ID, deterministic service name,
manager ControlGroup, AllowedCPUs, MemoryMax, MemorySwapMax, TasksMax and
RuntimeMaxUSec. It opens every cgroup component without following a symlink and
verifies the leaf plus every ancestor below the trusted cgroup-v2 mount. A
tighter ancestor is a configuration mismatch, not a silently different
experiment. `systemd-run` success alone never proves completion: exact unit
identity and an empty recursive cgroup are required.

Lease parents and the cgroup root are parsed as strict absolute paths and
opened one component at a time from `/` with `O_NOFOLLOW`. Every ancestor must
be root/service-owned and not group/world writable, except a root-owned sticky
handoff directory such as `/tmp`; the final lease parent must be private to the
service account. The configured cgroup root must remain root/service-owned and
non-writable by other users. Dot components and symlink aliases fail closed.

The durable `worker-JOB` intent binds `{job, token, request digest, boot_id,
unit_name}` before launch. Before the helper is continued, an immutable
`worker-instance-JOB` record additionally binds the manager invocation ID,
validated cgroup path and descriptor-observed device/inode identities for the
configured root, service slice and leaf. Every signal and final absence proof
revalidates that instance; same-name or slice replacement is quarantined. On restart a
different boot ID is interrupted only if no terminal outcome was durable. An
already-durable success/failure/cancellation is preserved while cleanup is
completed. On the same boot the exact instance is terminated, never adopted as
successful work. A missing record, foreign unit/cgroup identity, or failed
populated-descendant cleanup leaves the active job reconciliation-required.
OOM, runtime timeout, ordinary execution failure and cancellation retain
distinct evidence/outcomes.

The production systemd path is Linux-only. Windows and macOS return
`unsupported`; those builds still compile the bounded codec and portable
manifest record. Injected tests cover fixed argv/resource/sandbox propagation,
ancestor verification, lease ordering, restart identity, reboot interruption,
distinct terminal causes, retained outcome evidence, full bundle replay, a
live `setsid` descendant that forces TERM/KILL cleanup, and a sibling stage
that survives parent TERM/KILL until it is directly killed. On Linux the
service self-test additionally drives a materializer-to-recipe bridge: a real
`bq_materialize` workspace and result root feed the real
`bench_service_recipe` build-graph entry, with only the external build and
throughput executables replaced by temporary scripts, and the published
bundle is validated by the worker's exhaustive validator. They do not replace a
privileged live-systemd qualification; if systemd is unavailable, command
construction and seams are the only validation and deployment remains explicit
operator work. Reference deployment files are under `deploy/`; nothing in the
build installs, enables or mutates them.

## Build and registered tests

The existing native `build.c` driver owns compilation and execution:

```sh
./build.sh bench_service capabilities
./build.sh bench_service self-test
./build.sh bench_service self-test --sanitize
./build.sh bench_service_recipe_self_test
```

`test_all_combinations` runs the normal service self-test beside the existing
throughput self-test on each desktop lane, and also runs its AddressSanitizer
and UndefinedBehaviorSanitizer variant on POSIX hosts. Existing compiler,
throughput, sanitizer and workflow gates are retained. `shared.c` is the same
foundation linkage used by the throughput tool. There is no new dependency,
measurement loop or general-purpose testing framework.

Linux supervisor deadline coverage lives in `worker_deadline_tests.c`. Timed
commands use explicit `exec` so the test retains an owned direct child rather
than depending on PID 1 to reap an orphaned shell descendant. The observed
`fork` PID identifies the group even when timeout precedes all output; a
stopped-before-exec control covers that case. Separate owned live/zombie group
members must prevent cleanup success until explicitly reaped. Failure
diagnostics retain individual results, elapsed time, PID/group identity and
process state before bounded cleanup. The 20 ms command deadline, 1,000 ms
test bound and production cleanup policy are unchanged, including treating
unreaped zombies as present group members.

The normal native executable is `build/bench-service-tools/service` (`.exe` on
Windows). The fixed-recipe self-test is a Linux build-driver command; it uses
private disposable workspaces and short-lived local fixture helpers, while the
service tests exercise the installed-path manifest contract and replay
validator. A passing disposable test is not a live qualification of the
operator-installed compiler or throughput binary. Tests use private,
disposable directories with deterministic source manifests;
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

POSIX write-bit removal alone is not an immutable-file security boundary
against an owning UID. The fixed systemd service supplies the actual queue,
lease, installed-tree and workspace path policy; the trusted driver additionally
locks built baseline/candidate trees before throughput. The recipe accepts only
operator-installed source directories and manifests owned by root or the
service UID, with no group/other access or write bits. It never treats a
submitted source tree as a source-free broker command. Before publication it
reopens every source and workspace identity, rechecks the baseline/candidate
tree roots, and compares the baseline and candidate executable digests captured
after their builds with the digests after throughput. Any mismatch fails closed
and leaves a durable failed manifest plus outcome evidence. No production
qualification is claimed until the operator runs the fixed deployment on a
host with systemd-v2 and reviews its authorization.

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
payloads, 564-byte maximum journal frames and 536-byte control requests/ordinary replies.
The authenticated export operation has a separate fixed reply cap; see [EXPORT.md](EXPORT.md). Normal
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
followed by schema-2 records. Schema 3 adds only supervisor-owned real-job
success and interrupted transitions. Replay accepts monotonic 1-to-2-to-3
prefixes and rejects every downgrade. Opening an older journal does not rewrite
it; the first subsequent mutation appends a durably versioned schema-3 record.
Schema-1/2 binaries cannot consume schema 3 and fail closed at its first
header; they must not open a journal after it has been upgraded.

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
bytes are checked against `profiles/validate-buster-v1.recipe`; it does not
admit arbitrary build policy. The recipe's own `schema=1` field is independent of the
journal and control schema numbers.

`native-retirement-performance-v1` is a recognized but deliberately blocked
registry entry. `profiles/native-retirement-performance-v1.blocked` pins the
landed performance contract, support declaration, binding validator and
statistics implementation by SHA-256 and records the remaining execution
requirements. It has no executable command, is not an installed `.recipe`, and
is rejected by request validation and `worker-unit`. This prevents the one-pair
smoke recipe from being relabelled as a retirement result while preserving a
machine-visible identity for the future admitted implementation.

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
slot until cleaning/finish is committed. Cancellation while failed cleanup is
pending is also persisted; cancellation during finalizing or after finish is
an idempotent no-op, not a fabricated cancelled execution.

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
is persisted. The fixed recipe's durable result lives outside that removable
attempt directory at `WORKSPACE_ROOT/results/job-<job>-attempt-<token>`; it is
opened and retained by inode, and is not removed by workspace reconciliation.
Successful worker finalization requires the retained manifest to match the
job, revisions, result root and trusted-source namespace policy before the
finished journal event.

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
If cancellation is durably requested while a failed attempt awaits recovery,
cleanup and terminal outcome are cancelled while the original failure evidence
remains available for diagnosis.

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

## Local authenticated service

`serve` is the operator-owned service loop. It opens the private queue once and
never lets clients provide installed roots, workspace roots, lease paths,
resource values, unit names or executable paths. Those six values are fixed at
startup by the deployment command; the client protocol carries only a named
recipe request and bounded status/cancel/log operations. The endpoint is a
Linux `AF_UNIX` `SOCK_SEQPACKET` socket. Each connection contains exactly one
request frame, capped at 536 bytes, and must have the
daemon's effective UID and GID through `SO_PEERCRED`. The service refuses the
materialize, workspace-reconcile and worker-run operations over this endpoint;
the worker configuration is service-owned. Export replies alone may carry up to
65,672 bytes (one 64 KiB chunk plus bounded framing and identities).
The authenticated UID/GID maps to the fixed `github-actions` principal.
Public submissions cannot override it; status/result/cancel/log/export reject
foreign and unknown jobs identically. Public logs use per-job ordinal cursors;
local operator logs retain journal sequence cursors. Public status receipts
suppress global sequence, occupancy and reconciliation fields.

The service retries a queued admitted service request on each bounded idle
tick and runs the existing supervisor with that fixed configuration. At
present, the only such request is `validate-buster-v1`; the blocked retirement
identity cannot enter the queue. A queued real request therefore follows the
same lease-before-materialization and
cleanup/recovery path as the local worker command, including progress after a
temporary `BQ_BUSY` lease result without another client frame. The synchronous
worker path remains listener-free: it does not accept,
poll or process client traffic during preparation, measurement or cleanup.
`STATUS`, `RESULT`, `LOGS` and `CANCEL` sent while the worker owns the host are
therefore deferred until the worker returns; a client may hit its bounded I/O
deadline and must retry. A queued `CANCEL` is acknowledged only after the
single queue owner durably appends it at a safe boundary. The live cancellation
path during a worker is the operator's `SIGTERM` or `SIGINT`, which cancels and
exits the service after durable cleanup. A disconnected client never releases
the queue or host lease.

The recipe's throughput invocation is deliberately `--profile smoke --pairs 1
--warmups 1 --no-guard`; this is only a fixed end-to-end validation slice and
cannot produce a performance qualification or #512 acceptance result.

```sh
./build.sh bench_service serve STATE SOCKET INSTALLED_ROOT WORKSPACE_ROOT LEASE_FILE CPU
./build.sh bench_service client SOCKET capabilities
./build.sh bench_service client SOCKET submit PRINCIPAL KEY validate-buster-v1 BASE_SHA CANDIDATE_SHA
./build.sh bench_service client SOCKET status JOB
./build.sh bench_service client SOCKET result JOB
./build.sh bench_service client SOCKET logs JOB [AFTER_SEQUENCE]
./build.sh bench_service client SOCKET cancel JOB
# Raw bounded-frame diagnostics only:
./build.sh bench_service rpc SOCKET <request-frame >response-frame
```

`client` constructs only the public schema-2 operations, rejects fake and
blocked recipe submissions before transport, and verifies that the response
schema, operation and correlation exactly match the request. Before rendering,
it validates reply sizes, job identity, enums, bounded log pages and cursors,
and result paths/digests. A bound result prints `result-bound=1`, `result-root`,
`manifest-sha256`, `bundle-sha256` and `full-result-sha256`. Execution outcome,
`validity=not-evaluated` and `statistical-decision=not-evaluated` remain separate.
These are authenticated receipts, not file downloads or performance verdicts.
Transport or malformed-response failures remain fail-closed.

The installed executable also provides a fixed smoke request encoder:

```sh
/usr/local/libexec/buster-bench-service gateway capabilities
/usr/local/libexec/buster-bench-service gateway submit KEY BASE_SHA CANDIDATE_SHA
/usr/local/libexec/buster-bench-service gateway status JOB
/usr/local/libexec/buster-bench-service gateway result JOB
/usr/local/libexec/buster-bench-service gateway logs JOB [AFTER_SEQUENCE]
/usr/local/libexec/buster-bench-service gateway cancel JOB
```

`gateway` fixes `/run/buster-bench/control.sock`, principal `github-actions`
and recipe `validate-buster-v1`. It accepts full lowercase immutable source
identities and bounded keys, never a recipe override, path, command, flag or
environment override. It shares `client`'s typed transport and reply validator;
it never opens the queue. Installed-source allowlisting and all materialization
checks remain service-owned under the host lease.

Neither command grants access: the socket still requires the daemon's exact
effective UID and GID. Operator authorization must permit only the installed
fixed gateway invocation. Never grant Actions an arbitrary service-account
shell, direct queue access, `rpc` or unrestricted service executable invocation.
The fixed encoder is not a setuid program or a completed privilege boundary.
Operator authorization and live deployment qualification still apply.
[Authenticated bundle export](EXPORT.md) provides bounded downloads and
independent reconstruction without exposing a service-side path.

`SOCKET`'s parent must already be a private, operator-provisioned directory;
the service refuses an existing socket, final symlink, non-private parent or
replacement inode. The bind uses a restrictive umask and validates the
pathname inode before listening and again after setup. On shutdown it removes
only the socket inode it created and reports cleanup or replacement failures.
Windows and macOS compile the bounded codec but report `unsupported` for
`serve`, `client`, `gateway`, `rpc` and `unpack-export`; no network listener is added there.

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
stdin, and writes one response frame through the queue opened by that process.
It remains a local pipe codec without caller authentication and retains the
fake recipes for deterministic regression work. `bench_service rpc SOCKET`
forwards one already-encoded schema-2 frame for diagnostics. `bench_service
client SOCKET ...` is the typed public-operation encoder and exact-response
validator used by a fixed-request gateway. Both reach the authenticated Linux
service; that endpoint advertises `service-recipes=validate-buster-v1` and
`blocked-recipes=native-retirement-performance-v1`, rejects fake, blocked and
supervisor-internal submissions, and performs the only service queue mutation.

The 24-byte header is `BQP1` (four bytes), schema (u32), operation (u32), payload
length (u32 <=512), correlation (u64). Control schema 2 adds operations 9..11 and the
failure field while retaining schema 1 request/response behavior and its
120-byte status body. Truncation, trailing bytes, unsupported versions or
operations, malformed payloads, and oversized lengths fail. Response operation
is request operation OR `0x80000000`; schema and correlation are echoed for a
valid envelope. Every response payload begins with a four-byte `BqError` code
(see `queue.h`); nonzero codes must be handled before parsing a success payload.

Operations 1..11 are capabilities (empty), submit (canonical request), status
(job u64), result (job u64), cancel (job u64), logs (job u64, after-sequence u64),
fake-run (empty), fake-reconcile (job u64, token u64), materialize (installed
path length u32, workspace path length u32, then both paths), and
workspace-reconcile (job u64, token u64, workspace path length u32, path).
Operation 11 is worker-run (installed/workspace/lease path lengths and CPU u32,
then the three paths). Paths are absolute and at most 192 bytes; their combined
worker payload must also fit the fixed 512-byte body.

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
| Bounded CLI/control operations | Same dispatcher for CLI, pipe and authenticated socket frames; truncation/version/length/numeric validation; bounded journal-event pagination |
| Authenticated service loop | One queue writer; bounded Unix seqpacket; peer UID/GID check; service-owned recipe/paths/resources |
| Separate execution outcome/validity | Successful, failed, cancelled and interrupted fake jobs remain not-evaluated |
| Lease before installed inputs | `bq_worker_run`; busy inherited-lease fixture leaves the FIFO job queued with no attempt |
| Durable service identity/recovery | intent plus immutable invocation/cgroup-inode binding; same-boot exact termination, same-name reuse quarantine, and a real fork/exec fixed-recipe SIGKILL followed by journal reopen and reboot-interruption recovery without contradicting a durable outcome |
| Resource and process-tree containment | Fixed systemd argv; manager plus cgroup-v2 leaf/ancestor validation; live detached descendant TERM/KILL/recursive-empty fixture |
| Distinct terminal causes | Durable worker failure, OOM and timeout evidence; cancellation outcome retained separately |
| Installed recipe/source validation | Compiled fail-closed registry; exact executable-recipe bytes; blocked retirement-contract digest pins; requested revision, safe sorted paths and source SHA-256; durable failure reasons |
| Per-attempt workspace isolation | Exclusive job/token identity; separate base/candidate source and build trees; read-only verified sources |
| Cleanup/recovery | Seal-required removal, restart reconciliation, collision/tamper/cleanup failures retain active admission |

These are new-feature acceptance tests, not a claim of a pre-existing queue bug
reproduced on main. Exact candidate SHAs, commands and observed CI results
belong in the PR evidence; this document is not an assertion that unrun gates
passed.

Still outside this slice: live operator-installed build/validate/compare
qualification and real measurement; live-systemd/polkit/deployment
qualification; transport credentials beyond the local peer UID/GID boundary;
retention migration; qualification, A/A and physical 9700X acceptance. The
repository contains the fixed validation recipe, the non-executable retirement
contract descriptor, durable whole-result bundle and failure/cancellation
evidence with injected replay tests; these are not claims about a deployed host
or a measured result.
No credentials, server settings, benchmark thresholds, production runner
ownership or parent-issue closure are authorized by this implementation.
