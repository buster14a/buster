# Authenticated finalized-result export

`gateway export JOB ATTEMPT FULL_SHA` retrieves a complete immutable service
result, including the original manifest, `BQ-BUNDLE-V1` index, outcome record
when present, every indexed file and empty directories. `FULL_SHA` is the
`full-result-sha256` from `gateway result JOB`; `ATTEMPT` is its token.
The optional fourth argument `EXPECTED_RECIPE` must exactly match the job's
fixed service recipe. It selects the client's preparation wait; a mismatch is
rejected before the daemon prepares an archive. The admitted smoke recipe
continues to use the three-argument command. A future retirement export must
specify `native-retirement-performance-v1`.
No filename, root, glob, command, environment, URL or branch crosses this
protocol. The daemon derives the root from `BQ_RESULT_BIND` in its durable
queue. A raw local `protocol` call cannot invoke export.

The daemon authenticates its effective UID/GID with `SO_PEERCRED` and maps
that identity to the one installed public principal, `github-actions`.
Neither a submission field nor the fixed encoder grants authority. Operator
configuration must still restrict access to the reviewed gateway commands;
do not grant an arbitrary service-account shell, queue access or `rpc`.
Other local/operator principals cannot be queried through this endpoint.
Unknown and foreign jobs have the same `not-found` reply with no job data.
All export failures contain a typed error only, never a host path or inventory.

## Download and independent replay

On the authorized gateway client, capture stdout as binary and retain the
receipt digest printed on stderr **only after successful completion**:

```sh
/usr/local/libexec/buster-bench-service gateway export JOB ATTEMPT FULL_SHA > result.bqexport
```

Check the exit status before publishing the file. A partial stdout file is not
a successful export. With the reviewed service utility in a clean Linux
workspace, use the exact reported `export-receipt-sha256`:

```sh
bench_service unpack-export result.bqexport /absolute/private/new-result RECEIPT_SHA
```

The destination must not exist; its parent must be private and trusted. The
command checks the pinned receipt, exact archive length and SHA-256, canonical
paths, entry/file/byte counts, and the original worker's exhaustive bundle and
full-result binding validators. It never executes a downloaded file. It
preserves the manifest's original recorded root as evidence while validating
through a descriptor for the new root; it never tries to open the recorded
host path. A failed reconstruction may leave an incomplete local destination;
it returns failure and never changes the input archive or service state.

The reconstructed tree is the input to the existing recipe-specific replay
commands, including `throughput compare` or the archived native-retirement
result-input/binding validators when that recipe is admitted. Use the pinned
replay source/tool identities retained with those results. This adds no
retirement result schema, measurement loop, statistical rule or #511 threshold.
The currently admitted `validate-buster-v1` receipt still means smoke validation,
not a #512 performance verdict. Failed/cancelled/interrupted terminal bundles
remain downloadable evidence; their recorded outcomes do not become success.

### Retirement test publication and independent replay

The blocked retirement recipe has a separate offline handoff, exercised only
after its service-owned producer supplies a complete finalized bundle and the
authenticated control service supplies **two independent values**: the export
receipt digest printed after a successful gateway download, and the execution
receipt digest for the exact job and attempt. Neither digest is read from the
download as its own authority. The binding path is relative to the reconstructed
service result; obtain its fixed location from the reviewed producer contract.
Use a reviewed local service executable and an immutable checkout containing
the referenced Git objects:

```sh
bench_service gateway export JOB ATTEMPT FULL_SHA native-retirement-performance-v1 > /private/download.bqexport
python3 tools/bench_service/retirement_export_replay.py \
    /private/download.bqexport \
    --test-publication /private/test-publication \
    --publish-only \
    --bench-service /usr/local/libexec/buster-bench-service \
    --repository-root /private/pinned-checkout \
    --binding PATH_WITHIN_RESULT --job JOB --attempt ATTEMPT \
    --full-result-sha256 FULL_SHA \
    --export-receipt-sha256 AUTHENTICATED_EXPORT_SHA \
    --trusted-execution-receipt-sha256 AUTHENTICATED_EXECUTION_SHA
```

Check the gateway exit status and capture its stderr receipt before running the
publication command. The test publication directory must exist, be private and
owned by the caller. The command exclusively creates
`retirement-JOB-ATTEMPT.bqexport`, syncs it and its parent, reads back all
bytes, and reports only a published test copy, not a successful replay. An interrupted
copy leaves `.pending` evidence; an existing pending or published name is a
collision and is never overwritten. The original archive and result remain
untouched. Real durable #510 publication is a separate operator action with
its own approved destination and receipt. This local test copy is not a #512
acceptance artifact.

In a separate clean consumer workspace with no producer working directory,
result tree or environment inherited, obtain the published test object and
the export and execution receipt digests through the authenticated control
channel independently of that object. Use a reviewed service executable and an
immutable checkout containing all referenced Git objects. Run the same script
with the published object as input and a new private retrieval directory:

```sh
python3 tools/bench_service/retirement_export_replay.py \
    /private/test-publication/retirement-JOB-ATTEMPT.bqexport /private/new-result \
    --consume-published --retrieval /private/clean-retrieval \
    --bench-service /usr/local/libexec/buster-bench-service \
    --repository-root /private/pinned-checkout \
    --binding PATH_WITHIN_RESULT --job JOB --attempt ATTEMPT \
    --full-result-sha256 FULL_SHA \
    --export-receipt-sha256 AUTHENTICATED_EXPORT_SHA \
    --trusted-execution-receipt-sha256 AUTHENTICATED_EXECUTION_SHA
```

The consumer makes a new exclusive byte copy in a different private directory,
syncs and reads it back, then runs native unpack and production binding replay
against that retrieved copy. A duplicate retrieval or interrupted copy retains
its evidence and cannot be reported as a replay. `--publish-only` never reports
`verified-without-admission`; only the separate consumer can do so. For a test
across machines, transfer the immutable publication through an approved test
destination, then run the consumer there with independently captured receipt
authority. This command does not implement or attest durable #510 publication.
Both invocations report actual copied bytes and receipt-declared archive,
indexed-file, entry, chunk and reserved spool sizes separately. These describe
transported evidence; they do not count executed workloads or physical samples.

The native unpacker checks every archived byte, canonical inventory and worker
result binding. The production Python validator then reconstructs the entire
population, eligibility/census and transcript-to-sample joins, statistics and
the publication/replay closure using the external execution receipt digest and
immutable Git identities. The handoff additionally joins the export's numeric
job and attempt to the validated execution receipt. A failed, interrupted or
invalid exported attempt can be unpacked and retained but exits before the
performance replay. A verified replay reports
`verified-without-admission`; it cannot admit the recipe or invent a performance
pass.

## Immutability, persistence and retention

The initial request requires a terminal job with a durably bound result and
matching attempt/full-result digest. The exporter inventories the entire root,
runs the existing exhaustive validator, copies original bytes, then reruns the
validator and compares all inode, size, mode, owner, link, nanosecond mtime and
ctime observations. It also reopens and checks the root identity. Every path
component uses `openat` with `O_NOFOLLOW`; non-regular files, symlinks, hard-link
aliases, foreign/other-writable objects, traversal, replacements and capacity
exhaustion fail closed. Directory entries count toward the same bounded
inventory, including empty directories.

A fixed service-owned child constructs `export-JOB-ATTEMPT.pending` in the
private queue directory. The parent enforces a five-minute deadline for the
admitted smoke recipe; the blocked retirement recipe has a separate 24-hour
capacity budget for future admission. This is not a host execution deadline.
After the deadline there is a
one-second kill/reap allowance. An unreapable child keeps the inherited queue
lock and poisons the daemon; it cannot admit another worker. The snapshot is
synced and made mode `0400`, published without replacement by `linkat`, and
the directory is synced. The pending link is removed and synced before success.
The only automatically repaired crash prefix is two names for that exact
sealed inode. An orphan pending file without a sealed receipt is reported as
`export-interrupted`; after stopping the daemon, an operator may inspect and
remove only that uncommitted pending artifact before retrying. A corrupt sealed
receipt is never replaced automatically.

The sealed file contains a durable receipt, a digest-bound chunk index, and
original archive bytes. The first read verifies the complete index digest;
subsequent reads reuse that check only while the exact receipt, inode, size,
owner, mode and nanosecond timestamps remain unchanged. Every chunk checks
its own indexed digest and the opened/named inode before replying. A service
restart verifies the complete index again. The client independently
hashes the complete archive. A modified snapshot or partial transfer cannot
be reported as success. An identical retry, including after daemon restart,
returns the same receipt and bytes. No cursor, queue event or journal record is
written by a download. Failed exports leave the finalized result and journal
unchanged; a disconnect cannot authorize another execution or discard evidence.

Retain the sealed export, queue receipt and pinned replay tools together for
the journal lifetime. There is no automatic eviction or quota expansion.
A completed snapshot can outlive its original result directory; retries use
that same validated immutable snapshot. Creating an export after its original
result was removed returns `export-missing`. Retention/repair is operator work,
never a request-controlled delete or a measurement-phase operation. The current
synchronous daemon serializes export with worker activity; transfers cannot run
concurrently with an admitted measurement.

## Fixed limits and wire format

All integer fields below are little-endian. This is a transport envelope around
unchanged result files, not a second result schema. `BQP1` control schema 2 adds
operation 13 (`EXPORT`). Existing request and ordinary reply limits stay fixed.

| Limit | Bound |
|---|---:|
| Request frame / body | 536 / 512 bytes; export body exactly 152 |
| Export reply frame / body | 65,672 / 65,648 bytes |
| Chunk and cursor quantum | 65,536 bytes |
| Entries, including directories and control files | 4,096 |
| Individual file | 64 MiB |
| Existing indexed non-control payload | 512 MiB for smoke; 128 GiB reserved for blocked retirement recipe |
| Existing bundle index | 8 MiB |
| Total archive, including controls and entry headers | 546,177,024 bytes for smoke; 137,448,259,584 bytes reserved for retirement |
| Relative path | 192 bytes |
| Depth | Fewer than 256 components; path bound also applies |
| Receipt | 1,024 bytes |
| Preparation / chunk operation / client transfer budget | 300 / 30 / 300 seconds for smoke; 86,400 / 30 / 86,400 seconds reserved for retirement |
| Socket send/receive wait | 1 second; initial receipt wait 305 seconds for smoke, 86,405 seconds for an explicit matched retirement recipe |

The inventory is one fixed-capacity mapping; payload buffers are 64 KiB.
The smoke chunk index has at most 8,334 fixed 64-byte hashes; the reserved
retirement offset allows at most 2,097,294 hashes. No allocation
or response size is proportional to an unchecked request value. Index and
payload hashing stream in fixed buffers. Capacity exhaustion fails rather
than truncating a successful archive. Filesystem syscalls still depend on a
responsive local filesystem; the preparation child provides the outer deadline
for exhaustive validation and file reads.

For the proposed 60-pair retirement population, 17,441,280 paired numeric
records across A/A and A/B alone require at least 3,139,430,400 bytes at
180 bytes per record, before transcripts, manifests, binaries or logs. An
operator must provision space for every retained copy below. The larger limits
only remove a transport ceiling; the recipe remains blocked until its complete
producer, validators and service tests are reviewed.

Arithmetic capacity for the historical 78,912-row census and 72,672
compiler-eligible rows, **not a current trusted support/census population,
transferred archive or executed retirement run**. Recompute every row count
from the trusted support/census inputs at the exact attempt before sizing or
admission:

| Component | Modeled two-stage maximum at 60 pairs | Relevant ceiling |
|---|---:|---:|
| Compiler invocations, including warmups | 35,463,936 (17,731,968 per stage) | Transcript: 32,768 records and 64 MiB per shard |
| Additional runtime invocations if every compiler row is runtime eligible | 35,463,936 | Same transcript ceilings |
| Transcript shards for compiler and runtime at that upper bound | 2,166 (1,083 per stage) | 4,096 transcript shards; 4,096 bundle entries shared with all files/directories |
| Paired numeric records | 17,441,280 (8,720,640 per stage) | 16,777,216 records per partition; 16 GiB total #615 input per manifest |
| Numeric shards at 32,768 records each | 534 (267 per stage) | 64 MiB per published file; 1,396 bundle entries remain for controls, binaries, logs and directories |
| Transcript bytes if every shard reached 64 MiB | 145,357,799,424 bytes | Exceeds the 128 GiB indexed-payload ceiling even before numeric shards |

This modeled worst case cannot establish whether actual compact transcript lines
fit. The actual producer must inventory **every** retained file and directory
and respect the 64 MiB per-file, 192-byte path,
8 MiB index and 1 MiB execution-receipt bounds. A fixed transcript shard also
fails before its 32,768th record if its JSONL lines exceed 64 MiB in total.
These arithmetic figures cannot establish that real lines, logs and binaries
fit. The existing service tests transfer small smoke archives; no full
retirement export or clean replay has been completed at this point.

At maximum capacity, reserve six independent copies: retained service result,
sealed service spool (including its chunk index), gateway download, immutable
test publication, fresh retrieval and extracted clean replay. The
128 GiB indexed-payload ceiling therefore implies **over 768 GiB** plus
archive headers, index, filesystem and #510 publication headroom. At the
archive ceiling of 137,448,259,584 bytes, the sealed spool reserves
137,582,487,424 bytes including the 1,024-byte receipt and 2,097,294
64-byte chunk digests. These are reservations, not transferred bytes. The
reviewed whole-job deadline must also accommodate both stages; the current
3,600-second smoke-unit limit does not prove this population fits. Export
preparation, native unpack and independent binding replay have separate 24-hour
budgets; bound the full end-to-end operator window as the sum of their observed times, not the
per-operation receipt wait alone.

Export request body:

| Offset | Field |
|---:|---|
| 0 / 8 | Job ID / attempt token, u64 |
| 16 | Expected full-result digest, 64 lowercase hex bytes |
| 80 | Cursor, u64; `UINT64_MAX` requests the initial receipt |
| 88 | Chunk: expected export-receipt digest, 64 hex bytes. Initial receipt: optional expected recipe ID (u32; 0 unspecified, 3 smoke, 4 retirement), then 60 zero bytes |

Successful reply body:

| Offset | Field |
|---:|---|
| 0 | `BQ_OK`, u32 |
| 4 / 12 | Job ID / attempt token, u64 |
| 20 / 28 / 36 | Requested cursor / next cursor / total archive bytes, u64 |
| 44 | Payload length, u32 |
| 48 | Export-receipt SHA-256, 64 hex bytes |
| 112 | Receipt (initial reply) or archive chunk |

The initial reply has next cursor zero. Data cursors are aligned, strictly
less than total, and must carry the exact receipt digest. The last next cursor
equals total. `gateway export-chunk JOB ATTEMPT FULL_SHA CURSOR RECEIPT_SHA`
returns one validated binary `BQP1` reply for resumable clients. Such a client
must retain the receipt and contiguous prefix, validate every cursor/identity,
and check the full archive digest before accepting completion.

The 1,024-byte `BQEXP001` receipt records job/attempt (8/16), archive and raw
file bytes (24/32), regular-file/entry counts (40/44), and 64-byte SHA-256 hex
fields for request (48), manifest (112), bundle index (176), full result (240),
archive (304), chunk index (368), and the running export service executable
(432). Fixed NUL-padded fields contain principal (496, 64 bytes) and recipe
(560, 48 bytes). Offset 608 holds the compiled immutable recipe/profile bytes'
SHA-256. Offset 672 contains the original bounded request (320-byte slot), with
its length at 992. Authenticated effective UID/GID occupy 996/1004 (u64);
terminal outcome, validity and phase occupy 1012/1016/1020 (u32). Profile and
service hashes identify the reviewed exporting software; execution evidence
continues to come from the original sealed manifest and files. Versioned
recipe definitions must not be changed in place.

The downloadable file is this receipt followed immediately by the archive;
the service-private chunk index is not included. Archive entries are sorted by
bytewise relative pathname. Each has `(u32 type, u32 path_length, u64 size)`,
then exact non-NUL path bytes and file contents. Type 1 is an empty directory
record with size zero; type 2 is a regular file. Duplicate/out-of-order paths,
extra/trailing/truncated bytes and unsafe types are rejected on reconstruction.

Errors distinguish `export-not-finalized`, `export-invalid`, `export-missing`,
`export-unauthorized` (peer/submission identity), `export-oversized`,
`export-interrupted`, `export-corrupt`, `export-timeout`, `conflicting-key`
(identity mismatch), and `not-found` (unknown or foreign job). No error is a
successful receipt or a permission to regenerate a finalized result.
