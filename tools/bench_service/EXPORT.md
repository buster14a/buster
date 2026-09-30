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
They also report `receipt_derived_capacity.copies` for retained service-result
regular files, the sealed spool including its fixed reserved chunk index,
gateway download, immutable test publication, fresh retrieval and extracted
clean replay. `logical_six_copy_file_bytes` sums these six logical file-byte
counts. The 1,024-byte export receipt is included in the spool and each of
the three exported copies. The result and extraction each use receipt offset
32, which includes every regular result file, including control files. Before
creating a pending publication, the utility checks that the externally pinned
receipt reports a positive file inventory, no more than 4,096 entries,
regular-file bytes within the 128 GiB payload limit and no larger than the
archive. A matching digest for an internally inconsistent receipt cannot
authorize publication. Receipt-derived capacity remains a model until the
same-attempt service producer, gateway transfer and clean replay run; it is
not a filesystem quota or a proof of actual retained copies.

The native unpacker checks every archived byte, canonical inventory and worker
result binding. The production Python validator then reconstructs the entire
population, eligibility/census and transcript-to-sample joins, statistics and
the publication/replay closure using the external execution receipt digest and
immutable Git identities. The handoff additionally joins the export's numeric
job and attempt to the validated execution receipt (`authenticated_attempt_join`):
the receipt names the service's job label `job-JOB`
(`bq_retirement_campaign_job_label`) and the attempt token, and it must hash
to the independently supplied execution-receipt digest, so a self-consistent
forged receipt with recomputed in-bundle descriptors is rejected. The binding
record's location inside the result is owned by E's result composer: the
worker-unit producer writes it at the result root as `retirement-binding.json`
(#881 PR 3), `COMPOSER_BINDING_PATH` in the script fixes that path, and any
other `--binding` is refused. Every evidence file that record names, besides
the composer's and lane F's workflow phases, is a result-root entry too: the
producer publishes the binding context's evidence (support files, closures,
subjects' snapshots, binaries and build receipts, producer toolchain, harness
and statistics implementation, service, host-profile, qualification and lease
receipts, provenance receipts, contract source and admission record) beside
`retirement-aa-admission.json`, each at the size and digest the record binds
and under the name the replay's layout maps its binding path to
(`retirement-evidence-` and the path with each `/` as `--`), and the composer
seals them under their binding paths; they are ordinary regular files of
the bundle index and the export. That record carries its sealed-result and
independent-replay phases as pending descriptors (the sealed result binds the
record's digest), so it never passes a replay itself. Lane F's final binding
is produced after the export, so it lives outside the service result: the
operator passes lane F's directory with `--lane-f`, holding
`retirement-final-binding.json` (`FINAL_BINDING_NAME`) and the evidence it
names. The replay copies that directory's single-link regular files into the
clean replay destination, refusing any name the unpacked result already has
(`lane_f_import`), requires the final binding to be the composed record with
only its sealed-result phase set to the composed sealed result and its
independent-replay phase set to a non-pending descriptor at the composed path
(`final_binding_check`), and then runs the validator and the join over it.
`retirement_export_replay_real_test.py`, run after `bench_throughput self-test`
with its output directory, drives real A1 output (metrics shards, untimed
batches, the two-shard execution receipt and numeric samples) through these
readers and the publication path, including reordered, missing, truncated and
forged inputs. With the real binding validator the CLI chain stops there,
because its minimal join record is no complete binding. With a validator test
double, the CLI's `authenticated_attempt_join` accepts the genuine receipt and
refuses a wrong attempt or a forged receipt. `--worker-unit DIRECTORY`, which
`bench_service self-test` runs right after the preparation runner, reads the
worker unit's composed job-82 result: the whole CLI (publication, separate
retrieval, unpack stand-in, lane F import and final-binding check, validator
double, join) accepts it with lane F's final binding and refuses a final
binding that changes anything else; the join accepts the producer authority's
receipt digest and refuses another job, attempt or trust root and a tampered
receipt. The archive and the unpacker in that test
are Python stand-ins. The native `unpack-export` needs a receipt the service
itself sealed for a finalized service result, and the worker's full-result
binding; the blocked retirement recipe cannot finalize such a result. A failed, interrupted or
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
original archive bytes. The first read verifies the complete index digest
and keeps one SHA-256 per 64 KiB index page in memory. Subsequent reads reuse
that check only while the exact receipt, inode, size, owner, mode and
nanosecond timestamps remain unchanged, and even then reread the cursor's
whole index page and require its verified page digest, so a rewrite that
leaves the timestamps unchanged still fails. Every chunk checks its own
indexed digest and the opened/named inode before replying. A service restart
verifies the complete index again. The client independently
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

### Retirement capacity ledger (A1)

The larger limits only remove a transport ceiling; the recipe remains blocked
until its complete producer, validators and service tests are reviewed. An
operator must provision space for every retained copy below. All figures in
this section are **arithmetic over the checked-in sources, not a trusted
support/census population, a transferred archive or an executed retirement
run**. Recompute them from the trusted support/census inputs and the frozen
plan at the exact attempt before sizing or admission.

(A1, M4) The amended campaign times native-host batch groups instead of rows.
`tools/throughput/retirement_capacity.py` derives, from the support
declaration and the fixture recipe table, 80 object groups (16 configurations
of a compiler-default group of up to 416 inputs, a 4-input c23 group and three
single-fixture groups), 880 untimed cross-target object groups, and stage
singletons counted from canonical #508 rows when supplied, otherwise at the
validator's declaration minimum of 2 (one native link, one native self-host;
6,482 timed rows at most). Per-batch metrics artifacts are byte ranges of
64 MiB metrics shards (at most `2 * ceil(bytes / 64 MiB)` shards per writer),
so they no longer need one store entry each. The untimed code-artifact
batches (two variants times production and reproduction, 3,520 batches) add
their own metrics shards and one sealed batch-record file. Worst-case store
payload, excluding the caller's external entries and bytes, with a reviewed
4 KiB header per artifact:

| Pairs | Per-input metrics bound | Invocations per stage | Metrics artifacts per stage | Payload entries | Payload bytes | Verdict |
|---:|---:|---:|---:|---:|---:|---|
| 60 | 4 KiB | 20,496 | 19,520 | 461 | 15,433,221,440 | fits |
| 60 | 16 KiB | 20,496 | 19,520 | 1,777 | 59,521,385,792 | fits |
| 254 | 4 KiB | 85,680 | 81,600 | 1,803 | 60,775,041,120 | fits |
| 254 | 8 KiB | 85,680 | 81,600 | 3,523 | 118,495,217,760 | fits |
| 254 | 16 KiB | 85,680 | 81,600 | 6,963 | 233,935,571,040 | rejected before timing |

Both stage singletons are counted as runtime rows. The declaration bounds the
stage rows only from above (both stages on every declared object identity), so
the report also gives how many further runtime-eligible stage singletons fit
at 254 pairs (17,079 at 4 KiB, 4,220 at 8 KiB), and states the largest
fitting per-input bound (9,472 bytes at 254 pairs) as an assumption the
measured metrics sizes must satisfy. The same campaign with one store entry
per metrics artifact would need 166,779 entries at 254 pairs. The
per-input metrics bound and every time bound are reviewed pins of the campaign
budget; nothing here admits the recipe.
`tp_retirement_campaign_store_preflight` then adds, before any timing, the
store-owned control files (at least the execution receipt at its 1 MiB bound,
plus any manifest the store publishes) and the caller's exact external entries
and byte reservation; `tp_retirement_store_plan` enforces the resulting
owned-file and owned-byte budget, shards plus controls, at publication.

**Export-side ledger.** `python3 tools/bench_service/retirement_export_replay.py
a1-capacity` (functions `a1_export_ledger`, `composer_bounds`) maps every
scenario of that model onto the export and worker limits. It adds:
- the lane-E composer's outputs, mirroring `tp_retirement_compose_bounds`
  (#1879): the #615 result-input manifests, the code-record set (at most
  78,914 rows × 649 bytes), the #619 adapter input (#1880: its series shards
  of at most 64 MiB each, bounded as `1 + (series - 1) / (64 MiB - 255)`
  files, and their manifest of 512 + 256 bytes per shard) and output
  (replay), the result bundle, the execution receipt, the retained manifest
  (27 + 4,096 × 320 bytes) and the sealed-result record;
- the prior sealed-closure files (at least 40, derived from the validator's
  `_all_artifacts` plus `contract.source` and the execution plan; census
  projections add more);
- the worker's three control entries (manifest, `BQ-BUNDLE-V1` index,
  outcome).

It checks each file kind against the 64 MiB per-file cap and reports what is
left for census projections, retained logs and every directory. The family's
aggregate and slice members are taken at the #619 cap of 80, which can only
enlarge the adapter bounds. With the declaration's two runtime stage
singletons:

| Pairs | Per-input metrics bound | Owned files | Entries left | Owned bytes | Bytes left | Adapter series | Series shards | Verdict |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 60 | 4 KiB | 477 | 3,576 | 15,909,125,253 | 121,529,828,219 | 406,664,536 | 7 | fits |
| 60 | 8 KiB | 917 | 3,136 | 30,605,180,037 | 106,833,773,435 | 406,664,536 | 7 | fits |
| 60 | 16 KiB | 1,793 | 2,260 | 59,997,289,605 | 77,441,663,867 | 406,664,536 | 7 | fits |
| 254 | 4 KiB | 1,838 | 2,215 | 62,554,736,805 | 74,884,216,667 | 1,710,443,864 | 26 | fits |
| 254 | 8 KiB | 3,558 | 495 | 120,274,913,445 | 17,164,040,027 | 1,710,443,864 | 26 | fits |
| 254 | 16 KiB | 6,998 | -2,945 | 235,715,266,725 | -98,276,313,253 | 1,710,443,864 | 26 | refused (entries, bytes) |

**The #619 adapter input is sharded (#1880).** At A1 scale its series of
about `8 × cells × 2P` 32-byte ratio lines is far above the 64 MiB per-file
cap as one file, so the composer stores it as greedy whole-line shards of at
most one store file (7 at 60 pairs, 26 at 254 pairs) beside a manifest, and
neither cap is raised. Every two-runtime scenario except 254 pairs at 16 KiB
fits; that one exceeds the entry and byte caps with its metrics shards alone.
A scenario with no runtime-eligible row is refused, because generated
runtime would have no #619 cell. The format is described in
`tools/bench_service/README.md` under the composer.

A full metrics or transcript shard is exactly 67,108,864 bytes, the per-file
cap; the worker and `bq_export_inventory` reject only a larger file, and
`bq_test_export_inventory` pins that boundary. The untimed batch-record file
is at most 3,520 × 747 = 2,629,440 bytes and a numeric shard at most
131,072 × 330 = 43,253,760 bytes. The export envelope never binds before the
store: the 128 GiB store ceiling plus a 208-byte header and path for all 4,096
entries is 137,439,805,440 bytes, below the 137,448,259,584-byte archive cap.
At 254 pairs and an 8 KiB per-input bound only 522 entries and about 16.0 GiB
remain for census projections, logs and every directory. A real layout must
be checked against this ledger before that bound is pinned.

At maximum capacity, reserve six independent copies: retained service result,
sealed service spool (including its chunk index), gateway download, immutable
test publication, fresh retrieval and extracted clean replay. The
128 GiB indexed-payload ceiling yields **824,805,176,192 logical file/transport
bytes** when both file and archive fields reach their respective ceilings.
The ledger also reports each scenario's owned-file six-copy floor, before the
prior closure, logs and controls: 721,778,744,380 bytes at 254 pairs and 8 KiB.
These are simultaneous upper bounds, not measured storage requirements or
proof that any producer can fill both ceilings. They exclude directory
metadata, filesystem allocation, validator temporary space and any separate
#510 publication copy. At the archive ceiling of 137,448,259,584 bytes, the
sealed spool reserves 137,582,487,424 bytes including the 1,024-byte receipt
and 2,097,294 64-byte chunk digests. These are reservations, not transferred
bytes. The reviewed whole-job deadline must also accommodate both stages; the
current 3,600-second smoke-unit limit does not prove this population fits.
Export preparation, native unpack and independent binding replay have separate
24-hour budgets; bound the full end-to-end operator window as the sum of their
observed times, not the per-operation receipt wait alone. The actual producer
must still inventory **every** retained file and directory and respect the
64 MiB per-file, 192-byte path, 8 MiB index and 1 MiB execution-receipt
bounds. The existing service tests transfer small smoke archives; no full
retirement export or clean replay has been completed at this point.

#### Superseded: pre-A1 per-row capacity (kept for the record)

**Superseded by Amendment A1** (the #36 decision of 2026-09-29). This model
timed one process per canonical row on all twelve targets, gave each
per-invocation metrics artifact no store entry and had no untimed code-byte
batches. Do not size or admit a campaign from it; use the A1 ledger above.

For the proposed 60-pair retirement population, 17,441,280 paired numeric
records across A/A and A/B alone require at least 3,139,430,400 bytes at
180 bytes per record, before transcripts, manifests, binaries or logs.

Arithmetic capacity for the historical 78,912-row census and 72,672
compiler-eligible rows:

| Component | Modeled two-stage maximum at 60 pairs | Relevant ceiling |
|---|---:|---:|
| Compiler invocations, including warmups | 35,463,936 (17,731,968 per stage) | Transcript: 65,536 records and 64 MiB per shard; at most 902 bytes per line |
| Additional runtime invocations if every compiler row is runtime eligible | 35,463,936 | Same transcript ceilings |
| Transcript shards for compiler and runtime at that upper bound | 1,084 (542 per stage) | 2,048 transcript shards per stage; 4,096 bundle entries shared with all files/directories |
| Paired numeric records | 17,441,280 (8,720,640 per stage) | 16,777,216 records per partition; 16 GiB total #615 input per manifest |
| Numeric shards at 131,072 records each | 134 (67 per stage) | 64 MiB per published file; at most 415 bytes per line; 2,878 bundle entries remain for controls, binaries, logs and directories |
| Worst-case transcript plus numeric shard bytes at the proven line widths | 71,215,071,744 bytes (63,976,940,544 transcript, 7,238,131,200 numeric) | Within the 128 GiB indexed-payload ceiling, leaving 66,223,881,728 bytes for all other files |

The line widths were the maxima of the pre-A1 serializers, so a full
65,536-record transcript shard was at most 59,113,472 bytes and a full
131,072-record numeric shard at most 54,394,880 bytes. At that time
`tp_retirement_campaign_capacity` admitted at most 198 pairs with no runtime
rows and 108 pairs when every row of the 77,762-row envelope was runtime
eligible; 254 pairs needed 175,875,870,640 and 318,964,171,600 bytes
respectively.

### Export wire format

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
