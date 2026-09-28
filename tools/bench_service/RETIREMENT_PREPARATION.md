# Retirement input preparation (#1018)

This is the private preparation boundary under #881 and #923. The retirement
recipe remains blocked. Its compiled profile must add an exact
`inventory-sha256` pin before the new path can execute; the request cannot
choose an inventory file, count, source path, command, flag, or environment.

## Operator-installed inventory

Install a mode-read-only, single-link regular file at
`recipes/native-retirement-performance-v1.inventory` beneath the existing
immutable installed root. Its entire SHA-256 must equal the value in the
reviewed compiled recipe profile. The file has exactly these six newline-ended
records and no additional fields:

```text
BQ-RETIREMENT-INPUTS-V1
repository=buster14a/buster
support-sha256=<64 lowercase hex>
contract-sha256=<64 lowercase hex>
base=<full commit> <full tree> <source.manifest SHA-256> <entries> <bytes> <directories> <max path bytes> <max depth>
candidate=<full commit> <full tree> <source.manifest SHA-256> <entries> <bytes> <directories> <max path bytes> <max depth>
```

The support and contract SHA-256 fields must equal the separate compiled
profile pins. Both requested source identities must exactly equal the two
inventory commits. The inventory is an operator assertion of the complete
approved source, workload, dependency, SDK and tool input closure. An inventory
prepared from an incomplete Git tree or #508 closure must never be pinned:
the materializer can prove that the installed files match the reviewed
inventory, while independent commit/tree and support-closure validation must
still establish that the reviewed inventory is complete. A tree string inside
an inventory is not by itself proof of Git object provenance.

Each `sources/<commit>/source.manifest` retains the existing sorted
`BQ-SOURCE-V1` format. Preflight hashes its exact bytes, walks every listed
regular read-only file through `O_NOFOLLOW` descriptors, verifies each SHA-256,
checks strict path ordering, counts unique directories and path components,
independently traverses the installed directory closure, and compares every
measured bound with the inventory. The traversal rejects unlisted regular
files, empty extra directories, links, and non-regular entries, and rechecks
held directory identities against their names before releasing them. It rejects unknown,
stale, missing, duplicate, replaced or content-mismatched entries before any
timing. The preflight retains the same-job source inode closure so a byte-equal
replacement between preflight and the independent second copy fails.

For each subject the materializer copies the selected snapshot into its
existing `source` directory, verifies that copy, copies independently into a
temporary sibling, verifies both the second copy and the installed source
again, then removes the temporary sibling by its held device/inode identity.
A failed copy remains part of the sealed attempt for the existing durable
failure and cleanup path. Once retirement preflight starts, an immutable
`preparation-<job>` queue record retains the request, attempt, inventory,
source commit/tree, manifest, capacity, both installed and copied source inode
closures, outcome and number of independently verified subjects, including for
partial failures. The correctness lane must require `status=ready`, both
completed subjects and the actual ready job, then recheck its materialized
inputs.

The supervisor now calls `bq_retirement_preparation_ready` after result-root
creation and before unit launch. It independently rereads the private durable
record, rechecks the installed inventory and source bytes without repeating the
pre-copy free-space reservation, and hashes both materialized source copies.
The exact canonical successful record must match the current job, token and
request. The V2 record binds each installed source and materialized copy's
same-job inode closure in addition to the portable manifest identity, so a
byte-equal replacement of either tree fails on readback. A missing, failed,
changed or stale record, or a modified source copy, prevents launch. The
returned service-owned record digest is an A handoff
identity. The supervisor requires its lowercase 64-byte value for the
retirement recipe and carries it in the V2 authenticated lease response. The
unit validates the recipe and echoes the digest in the acknowledgement before
the supervisor releases the lease. Smoke requires an empty value and retains
its six-argument build-driver interface. On eventual retirement admission the
fixed private build command receives the digest after the result root, before
the phase descriptor. The service-side `bq_retirement_preparation_import`
accepts that authenticated digest and independently rereads the durable record,
installed inventory and both materialized source trees. It returns the checked
inventory, commit/tree, manifest, capacity and same-job source inode identities
only when the record digest matches; failures clear the returned facts. The
#923 integrator still must call this importer from the correctness producer,
attach the matched trusted build manifest and independently recheck its binary
outputs before timing. A digest relayed through the lease does not establish
build provenance by itself.

The closure walk opens a new directory description from each held root, so
repeated verification does not consume the caller's directory cursor. The
combined preparation/readback fixture verifies that an unlisted directory in
a materialized copy rejects the durable handoff, that removing it restores the
same record digest, and that a byte-equal file replacement still rejects after
the old name is removed. Directory closure must not mask inode-identity checks.
The supervisor's absolute deadline starts at lease acquisition and is retained
across this readback and final result validation; readback never starts a new
execution budget.

## Installed reference policy (#1020)

The independent reference producer takes its template and inventory from two
more operator-installed files beside the A inventory, never from a request or
a caller-computed digest:

- `recipes/native-retirement-performance-v1.reference-template`
- `recipes/native-retirement-performance-v1.reference-inventory`

Each must be a mode-read-only, single-link regular file owned by root or the
service user, read through an `O_NOFOLLOW` descriptor whose metadata must not
change while it is read. Their whole-file SHA-256 values must equal the
compiled profile keys `reference-template-sha256=` and
`reference-inventory-sha256=`; a missing key fails closed with
`BQ_RECIPE_MISMATCH`. The template file is exactly the byte stream hashed by
`bq_retirement_oracle_template_hash`, so its file SHA-256 is the template hash
the oracle authority checks. Its fields have fixed widths or explicit
little-endian counts and length prefixes, so it needs no extra framing. The
inventory file is the existing V1 stream written by
`bq_retirement_reference_inventory_encode`, whose header embeds that template
hash. `retirement_reference_template.{h,c}` decodes both with bounded,
length-prefixed parsing and strict row order. It rejects trailing bytes,
impossible counts, a wrong domain, a side outside the two held roots and a
mismatched reference count or embedded template hash. It then re-derives the
canonical digest through the existing hash/encoder, which also re-applies the
source-path (`..`, absolute), side-0 and command-hash policy, and requires
it to equal the SHA-256 of the input. `bq_retirement_reference_template_write` emits the
template stream for installers and fixtures; neither writer installs a pin.

`bq_retirement_reference_policy_import` (in
`retirement_correctness_service.c`) always uses the compiled
native-retirement profile. After both pins and decodes it joins the template's
commit, tree and source manifest digest for each side to A's imported
`BqRetirementPreparation` subjects. It checks `support_sha256` against
`support-declaration-sha256=`, `census_sha256` against
`census-rows-sha256=` and the template and inventory toolchain identity
against `toolchain-manifest-sha256=`. That toolchain identity is the reviewed
manifest pin, not the per-job inode `identity_sha256` from
`retirement_toolchain.c`. The verified toolchain must carry the same manifest
digest. `bq_retirement_toolchain_hold_clang` then opens `bin/clang` without
following links beneath the verified root and hashes the held descriptor. It
rechecks the complete bundle, whose identity binds every file inode, and
requires the name still to resolve to the held inode. The inventory's
`clang_sha256` must equal that digest. The returned policy owns the decoded
rows, the held Clang descriptor and the held descriptor of the installed
inventory file it decoded, for a later `producer_begin`, which rehashes that
held inventory rather than reopening its name. Both descriptors are
close-on-exec, read-only and at least 3. Release the policy on every path.

The checked-in blocked profile has none of the reference pins, so the public
importer fails closed today, and nothing in the worker calls it. Real pin
values still need a reviewed installed template and inventory. This importer
holds no `rows.tsv` or validator projection and does not check template
`configuration_sha256` values itself. `bq_retirement_oracle_authority_begin`
compares them with B's declared rows. `begin_service` binds those rows to the
#1020 per-row digest derived from pinned `rows.tsv`
([definition](retirement_census_import.md#per-row-configuration_sha256-1020)),
so a caller that passes the same B array to both binds the template
transitively.
The fixture in `retirement_prepare_tests.c` exercises the importer through its
`BQ_RETIREMENT_CORRECTNESS_TEST_ONLY` seam
`bq_retirement_reference_policy_import_pinned` with a synthetic profile.

## Worker-unit handoff (#1020)

The lane-B caller will run in the service binary's `worker-unit`, inside the
outer transient unit as `buster-bench`. There the installed tree is read-only,
the attempt workspace is writable, and the queue and lease are inaccessible,
so the unit cannot read the queue's `preparation-<job>` record. The A record
functions (`bq_retirement_preparation_record`, `_ready` and `_import`) now
take a `BqRetirementStore`, a borrowed directory descriptor that holds the
record. The coordinator passes `bq_retirement_queue_store(queue)`, which is
the queue directory; the unit passes the export described below. Record
reads and writes keep the existing no-follow, single-link, owner and
read-only checks.

After `bq_retirement_preparation_ready` returns the record digest, the
supervisor calls `bq_retirement_preparation_export` on the retirement path
only; smoke is unchanged. The export rereads the queue record and requires its
SHA-256 to equal that digest. It checks the attempt's `.identity` seal, then
creates `job-<id>-attempt-<token>/retirement/` with the inherited-group
directory helper (mode `02700`). It writes `preparation-<id>`, the exact
record bytes, and `request-<id>`, the canonical request bytes that the
record's `request=` digest names. Each file is created with `O_EXCL`, mode
`0400` and a single link, and each is fsynced. The directory is then sealed to
`0500` and fsynced, as is the attempt. Nothing is replaced: a second export
fails. A partial export fails the attempt before launch and is removed with
the workspace. The exported copy carries no authority of its own.

`retirement_unit.c` holds the unit side. `bq_retirement_unit_store_open`
opens the export without following links. `bq_retirement_unit_prepare` takes
that store, the workspace root, the installed root, the job and attempt
numbers, and the preparation digest carried by the authenticated lease
handoff. It performs these steps in order:

1. It reads `request-<id>` and requires five nonempty length-prefixed fields
   that exactly fill the file. Then it rebuilds the job and its request
   digest.
2. It checks that the store is this attempt's sealed `retirement` directory,
   by device and inode through the workspace root and after the attempt's
   `.identity` seal. The directory must be owned by the service, have mode
   `0500`, and contain exactly `preparation-<id>` and `request-<id>` as
   owner-read-only, single-link regular files.
3. It re-imports A through `bq_retirement_preparation_import` against that
   store. This rereads the record, checks its canonical bytes for this job,
   token and request, rechecks the installed inventory and both materialized
   source trees, and requires the record digest to equal the handoff digest.
4. It runs `bq_retirement_toolchain_verify` with the profile.
5. It imports the pinned reference policy with the imported preparation and
   the verified toolchain.

The returned `BqRetirementUnitPrepared` owns the policy's held Clang and
inventory descriptors. It must stay in place and be released with
`bq_retirement_unit_release`. A second prepare into a live object is refused.

The public wrapper uses the compiled profile and the fixed toolchain root.
With the blocked profile it fails with `BQ_RECIPE_MISMATCH` at the A
inventory pin. `bq_worker_unit` does not call it: the recipe stays
unadmitted, and the `worker-unit` recipe check and the supervisor's recipe-name
gate still reject the job before any child launches. The supervisor's export
therefore runs only on a path that is still rejected. The unit authenticates
file contents, not the inodes the coordinator wrote. A byte-identical
replacement by the same service identity carries the same lease-authenticated
digest, so the unit does not treat it differently.

## Unit-side matched builds (#1020)

`bq_retirement_unit_build` runs the four matched-build stages inside
`worker-unit` after `bq_retirement_unit_prepare`. It takes the prepared
object, the export store, the phase channel, the read end of the SIGTERM
self-pipe and the absolute `CLOCK_MONOTONIC` execution deadline from the lease
handoff. It performs these steps in order:

1. It requires the profile's `build-driver-sha256` pin before it touches the
   channel. It then exchanges PREPARING with the supervisor, or accepts a
   channel that already holds that acknowledgement. No child exists before
   the acknowledgement. A refused or late acknowledgement returns
   `BQ_WORKER_MISMATCH` without a launch or an evidence directory.
2. It creates `job-<id>-attempt-<token>/retirement-build/` with the
   inherited-group helper (mode `02700`). The directory must be new, so a
   second build into the same attempt is refused. The coordinator's sealed
   `retirement/` export is never written and still holds exactly two files.
3. `bq_retirement_matched_build_begin_stores` re-imports A from the export,
   checks the driver pin and the toolchain, and binds the broker launcher.
   A broker sequence requires the caller's workspace root to be exactly the
   broker's `BQ_RETIREMENT_STAGE_WORKSPACE_ROOT`
   (`/var/lib/buster-bench/workspaces`, from `retirement_stage.h`), because
   the broker derives cwd and `--build-directory` from that constant; any
   other root returns `BQ_WORKSPACE_MISMATCH` before the channel is touched,
   and the re-import applies the same check. A DIRECT (fixture or queue
   API) sequence may use a test root; the pinned test seam passes the
   fixture broker's own root, from which that stand-in derives its paths.
   The toolchain manifest must equal the one prepare verified. It checks that
   the attempt is service-owned and not group- or world-writable (the
   materializer's `02710`; #1018 no longer requires a private attempt). It
   then creates the helper's private `retirement-work/` (`02700`), which
   must be new and holds the stage logs and `trusted-build/`.
4. It runs baseline generate and build, then candidate generate and build.
   Each generate launch first creates that subject's configured root, new,
   empty and service-owned: `base/build/matched-build` (`02700`) or
   `candidate/matched-build` (`02770`, candidate group). The candidate never
   starts from the baseline configuration and cannot write it. Each stage
   polls the log pipe and the cancellation descriptor under the deadline.
5. Each completed stage writes `matched-log-<n>-<id>` and
   `matched-stage-<n>-<id>` into `retirement-build/`. The last stage also
   writes `binaries-<id>` and `matched-builds-<id>` there. Every record is
   created with `O_EXCL`, mode `0400` and a single link, and is fsynced.
   After the fourth stage the unit requires exactly those ten files, seals
   the directory `0500` and fsyncs it and the attempt.
6. `bq_retirement_unit_build_import_pinned` re-imports the sequence from the
   sealed directory and the export. It checks the directory's closure and
   identity, rederives every command (including the broker binding), rereads
   logs and receipts, compares the binaries and final record digests, and
   rechecks A, the toolchain and both frozen executables. The unit then
   acquires both executables as held, close-on-exec descriptors.

`BqRetirementUnitBuilt` returns the verified sequence, the held binaries and
both record digests. Release it with `bq_retirement_unit_built_release` on
every path. The coordinator's later replay reads the same files through the
same importer.

A readable cancellation descriptor or an expired deadline stops the running
stage. The unit sends `SIGKILL` to the stage's process group, reaps the
child and waits until `kill(-group, 0)` reports `ESRCH`, within the worker's
10-second stop budget. It then asks the broker to KILL the stage unit and
returns `BQ_WORKER_CANCEL_SIGNAL` or `BQ_WORKER_TIMEOUT`. If absence is not
proven it returns `BQ_CLEANUP_FAILED`. The self-pipe byte is not consumed.
A failed or cancelled build leaves its partial evidence unsealed in the
attempt for the coordinator's failure path. Descendants that leave the
process group are outside this proof; for broker stages, the stage unit's
cgroup and the coordinator's recursive stop proof remain authoritative.

### Isolation: both subjects through the broker

The smoke recipe sends all four build stages through
`buster-bench-systemd-broker start-stage`. Baseline stages run as
`buster-bench` and candidate stages as `buster-bench-candidate`, each in its
own sandboxed transient unit whose command the root broker constructs from
constants. The unit keeps that model for both subjects rather than forking
the driver as `buster-bench`, so the candidate keeps the #880 candidate-user
isolation and the baseline gets its own stage sandbox.

A `BQ_RETIREMENT_LAUNCH_BROKER` sequence forks only the fixed broker CLI,
executed from a held and verified descriptor. Its only arguments are
`start-stage <job> <attempt> retirement-<base|candidate>-<generate|build>
<base commit> <candidate commit>`. The CLI's stdout and stderr go to the
bounded log pipe and its exit status is the stage's. Before each request the
unit still checks the driver pin, the source tree through a held descriptor,
the configured root, the absence of `Release/ide` and the toolchain. It
freezes and records the outputs itself. A broker stage's command digest also
binds the broker path and the stage name, so receipts from a direct fork
cannot satisfy a broker sequence. The direct launcher (fork and `fexecve` of
the held driver in the held source) remains for the fixture and the queue
API.

`bq_worker_unit` does not call `bq_retirement_unit_build`, and the recipe
gates still reject the job first.

### Broker retirement stages

The broker source (`systemd_broker.c`) and the credential gate now accept the
four typed stages, and `signal` accepts their units. The stage table, the
exact unit properties, the environment and the layout are documented in
[SYSTEMD_BROKER.md](deploy/SYSTEMD_BROKER.md#retirement-matched-build-stages-1020).
`retirement_stage.h` is the single source of the stage names, driver path,
toolchain root, environment values, configured-root paths and driver
arguments. The helper, broker and gate all include it, so the broker runs
exactly the command whose digest the helper records:

- The driver is `/usr/local/libexec/buster-bench-build`.
- The argv is `bq_retirement_matched_build_stage`'s.
- The cwd is `<attempt>/<subject>/source`.
- The environment is `PATH=<toolchain>/bin`, `LC_ALL=C`, `TZ=UTC` and
  `HOME=/nonexistent`, set by the gate after it clears PID1's environment.
- Umasks are `0077` and `0007`, for identities `buster-bench` and
  `buster-bench-candidate`.

The only writable path of a stage is its subject's service-created
configured root.

The installed broker, credential gate and service still predate this
contract, so every production broker launch fails closed until LOCAL
installs the reviewed binaries. That installation needs its own packet
(new binary digests, and a disposable real-systemd rehearsal of the four
units).

## Capacity derivation

At the inspected #923 head `ffdc9213e74128df5e759c76d52897eddfe4cd7e`,
the **tracked repository tree alone** contains 2,842 entries, 134,007,799
regular-file bytes, 217 directories, a 105-byte maximum path and seven path
components. A `BQ-SOURCE-V1` manifest of all those paths needs 341,377 bytes.
The historical `archive/36-direct-reference-20260912` tree has 1,926 paths,
154 directories, a 105-byte maximum path, seven components and a 224,482-byte
manifest; that historical tree is **not** the final approved comparison
baseline. These measured manifest sizes explain the retirement-only 512 KiB
buffer. Smoke keeps its 64 KiB limit. The current per-subject fixed ceilings
remain 4,096 files, 512 MiB copied bytes, 480 directories, 192 path bytes,
64 MiB per file and 256 cleanup depth. The final complete admitted closure
must be measured and fit every ceiling; its values are not inferred from the
tracked tree counts above. A final inventory exceeding any ceiling fails
before copying and requires a separately reviewed capacity change.

`source_reservation_bytes` is calculated before copying as twice the sum of
both measured source byte totals and manifest sizes, plus 8 KiB per declared
entry and 1 MiB for metadata. `fstatvfs` checks available storage against
that predeclared source reservation. Build-directory, worker lifetime and
sealed-result budgets belong to the #923 integrator and lanes C/E/F,
respectively. This capacity check does not promise that free space cannot
change concurrently; a later write failure remains a failed attempt with
durable attributable evidence.

## Matched build handoff

The existing #923 build driver has fixed Release `--cc clang` generate/build
stages for both subjects and separates service and candidate identities. The
private matched-build stage helper now creates each log exclusively beneath
the held attempt directory, launches the exact fixed argv, cwd and explicit
environment, and polls the actual child wait. It opens and verifies the installed
native build driver on a held descriptor, then executes that descriptor after
the child redirects its output and marks other inherited descriptors close on
exec. A replaced driver pathname therefore cannot select different bytes for
that launch; a kernel without `close_range(CLOSE_RANGE_CLOEXEC)` fails the
launch. Completion freezes and reopens that same log inode, checks the launched
stage and command digest, then stores
the observed exit status and log bytes in the durable stage receipt. A caller
cannot supply an exit code or substitute a log from a different process. The
whole-job worker still must own process isolation, deadlines, cancellation,
and cleanup; a stage wait alone is not proof of a trusted Clang build.

Before executing the descriptor, the child clears its inherited signal mask,
restores the default TERM, INT, PIPE and CHLD dispositions, and selects the
same fixed file creation mask as the existing nested recipe policy: `0077`
for baseline stages and `0007` for candidate stages. The stage command digest
includes this mask; the service's ambient mask and transient cancellation
handlers cannot change the build's process startup policy. Candidate stages
still require the worker's separate candidate UID and sandbox.

At begin the helper retains both subjects' imported preparation facts. Each
launch opens the selected source directory, scans its complete manifest and
same-job inode closure against those facts, then gives the child that held
directory as its cwd with `fchdir`. A source pathname replaced before the open
poisons the build; a pathname replaced after verification cannot redirect the
child to another tree. The candidate sandbox and toolchain dependency closure
remain requirements of the integrated worker.

The integration owner must use the verified `source` directories and
`preparation-<job>` facts in that path, replace its smoke-specific command and
stage policy only when the retirement recipe is admitted, and retain the exact
build argv/environment, trusted compiler/linker/resource/SDK/sysroot and
dependency identities, build logs, output binary bytes/digests and source/tree
relations. Its timed compilers must come from those trusted Clang stages;
self-built stages may supply correctness evidence only. The pre-timing
correctness lane must call `bq_retirement_preparation_import` with the digest
from the authenticated lease, consume both verified source identities and the
complete build provenance before declaring ready for timing. The importer
does not assert build provenance merely because the source preparation succeeds.

The private `bq_retirement_binaries_record` and
`bq_retirement_binaries_import` seam now binds frozen binary outputs to that
preparation. After both successful *trusted* Clang stages, the integrator must
put exactly the timed executables at
`job-<id>-attempt-<token>/retirement-work/trusted-build/` `base-ide` and
`candidate-ide` beneath the workspace root. `retirement-work` must be
service-owned and private. The attempt above it must be service-owned and
not group- or world-writable. The
`trusted-build` directory must be service-owned, private and mode-read-only;
each executable must be service-owned, single-link, executable, mode-read-only,
nonempty and at most 512 MiB. The recorder independently imports A, hashes
both actual files through no-follow descriptors, checks their inode identities,
checks that the frozen directory contains exactly those two executable files,
binds its device/inode identity, and writes a single durable V2
`binaries-<job>` queue record. The importer repeats both source and binary
observations and accepts only the exact canonical
record digest provided through a service-authenticated channel. It clears
returned facts on failure. A missing output, planted entry, replaced directory,
failed A handoff, edited file, same-byte inode replacement, stale attempt or changed record cannot yield
binary facts. The fixture's tiny executable-shaped bytes cover this readback
boundary; they are not real compiler builds.
The 512 MiB file ceiling is a fail-closed interim bound; final capacity must
be checked against the actual reviewed compiler outputs before admission.

`bq_retirement_binaries_acquire` now opens both exact files as held,
close-on-exec descriptors after importing the durable binary record. It
recomputes their content and inode identities against that record, repeats
the directory closure and source/binary readback, and returns the descriptors only if both still
match. Descriptors are promoted to at least 3 to meet
`tp_retirement_executable_init`'s launch boundary even when a standard stream
was closed. A pathname swap after acquisition cannot change the file held by
the service. The caller initializes the holder to zero or releases it before
acquiring; an acquisition refuses to overwrite live descriptors. Release is
safe for a zeroed holder. The launcher must execute these descriptors rather
than reopen the names, retain them until all dependent launches finish, and
call `bq_retirement_binaries_release` on every exit path. The private fixture
proves a byte-identical replacement makes a new acquisition fail while a
previously held descriptor still reads the original inode; it does not
execute a compiler or assert Clang provenance.

That record intentionally has **no toolchain, command, environment, cwd or
build-log attestation**. `retirement_matched_build.{h,c}` now defines the
private service-side sequence around it. `begin` imports the exact A record,
checks the compiled profile's `build-driver-sha256` against the installed fixed
driver, and validates the workspace descriptor/path identity. `stage` returns
four exact commands: baseline generate/build, then candidate generate/build,
each with its own subject's configured root (`base/build/matched-build`,
`candidate/matched-build`), fixed Release/Clang flags, a single-job Ninja
build and an explicit environment. The source of `PATH` is the
fixed `/opt/buster-bench/installed/toolchain/native-retirement-performance-v1/bin`.
Before issuance, the service requires `toolchain-manifest-sha256` in the
compiled profile and verifies the installed bundle beneath the held installed
root. Its `toolchain.manifest` has `BQ-RETIREMENT-TOOLCHAIN-V1`,
`platform=linux-x86_64`, then sorted lines of `<SHA-256> <relative path>`.
The tree must include executable `bin/clang`, `bin/cmake`, `bin/ld` and
`bin/ninja`. The verifier hashes every file, checks no-follow descriptors,
read-only ownership, single links and an independent complete directory walk;
it rejects extra files/directories, symlinks, wrong manifest bytes and inode
replacement. Limits are 4,096 files, 512 KiB of manifest, 512 MiB per file,
2 GiB of total listed bytes and 480 directories. Bundle directories must be
traversable and readable by both service identities, and all files readable;
executable files must also be executable by both. The operator must make the
fixed `/opt` path's parent directories traversable by the candidate identity.
It rechecks the exact bundle and fixed driver before each command, opens and
hashes the driver descriptor again at launch, rechecks the selected source tree
through its held descriptor, and rechecks the bundle on completion.
Stage/final receipts bind the manifest SHA-256 and same-job inode identity;
readback rechecks both along with the output binaries. A change to the bundle
after the first stage poisons the sequence before another command is issued.
Before either build stage creates a log or child, the helper checks through the
configured build directory that `Release/ide` is absent (whether or not the
`Release` directory exists). A leftover baseline executable after candidate
generation or any stale output before baseline build poisons the sequence;
a successful no-op stage cannot freeze an inherited executable. The worker
must isolate that directory and reap descendants across this check and the
build so another process cannot replace the name during the attempt.
Each generate launch creates its subject's configured root itself, with an
exclusive `mkdir`. The root is service-owned with the exact mode (`02700`
baseline, `02770` candidate), and the helper holds it through child
completion. A pre-existing root, such as a stale or planted configuration,
refuses the launch before any log or child exists. On completion the named
root must still be that inode with that owner and mode. A generate may fill
the root but must not replace it. In a broker stage the root is a bind mount
the stage cannot replace; a DIRECT driver that replaces it fails. After
either successful generate the helper holds the root through the matching
build launch and checks the name and inode before creating the build log or
child.
The build stage keeps its root descriptor through completion, preventing inode
reuse. The runner must release an unfinished generated root after cancellation
and descendant cleanup.
The freeze step verifies the expected name still resolves to that held root,
reads `Release/ide` through held directory and file descriptors, and rechecks
both names after copying the executable. It also rejects changes to the input
file's size, mode, owner, links, and nanosecond modification/change times
during the copy. Replacing the configured root, `Release` directory, or
`ide` name during the freeze, even with byte-equal content, fails the stage
and cannot publish binary records.
The #923 worker must apply its fixed containment, separate candidate identity,
deadlines and cancellation to the launched child. `complete` stores each
command/exit/log as an immutable queue log plus receipt, reimports A, freezes
`Release/ide` by descriptor after each successful build, and publishes the existing binary
record and one final build record only after all four stages succeed. Any
failure poisons the session and leaves its prior stage receipts attributable.
Each child writes stdout/stderr through a private pipe drained into a
service-owned log. The capture stops writing at 16 MiB, continues draining so
the child cannot block on a full pipe, and rejects the stage even if that child
exits zero. A missing pipe EOF or failed log write also prevents publication.
This bounds disk use during execution, not just during receipt readback. The
worker still owns the whole-job deadline and descendant cleanup. Four logs are
individually capped at 16 MiB; each frozen binary at 512 MiB.

`bq_retirement_matched_build_import` rederives the commands, rereads each
immutable log and stage receipt, checks the exact final build record digest,
and reimports A and both binary outputs. The public
`bq_retirement_correctness_begin_service` requires that authenticated build
record digest and workspace root before opening B's binary holder or gate.
This is a private handoff, not an executable retirement recipe: the blocked
profile has neither `build-driver-sha256` nor `toolchain-manifest-sha256`; the
#923 runner has not been wired to these four commands and the final reviewed
Clang/linker/resource/SDK/sysroot/dependency closure has not been pinned or
independently verified. A complete bundle scan proves its installed files,
but only the runner's executable resolution and sandbox can prove the child
did not read ambient tools, system libraries, headers or scripts outside that
bundle. Its fixed absolute paths and dynamic dependencies must be reviewed
against the final build, and the runner must forbid unapproved ambient inputs.
The actual runner must keep the tool closure stable across each stage, enforce
the candidate UID/sandbox and log bound, then pass the final record digest
through the authenticated lease. The
correctness/oracle lane and dependent first timed launch must consume these
verified facts. A test fixture compiles two real miniature executables through
the same pathname with the local host compiler and proves that a failed
generate leaves no binary record and cannot open B. It does not prove trusted
Clang provenance or exercise a timed child. The compiled recipe stays blocked.

## Focused fixture

Compile and run `retirement_prepare_tests.c` alongside
`tools/throughput/shared.c` with the ordinary service `-Isrc`,
`BUSTER_SINGLE_THREADED=1`, C11 and warnings-as-errors flags from the repository
root; it also compiles `retirement_matched_build_fixture.c` with the local `cc`.
The fixture
checks the pinned inventory, mismatched pin, invalid source request, impossible
entry bound, unavailable storage query, source mutation, missing file, duplicate
manifest entry, unlisted file/directory/symlink, byte-equal inode replacement,
two verified copies, removal of the temporary copy, and frozen binary readback
after missing output, stale digest, same-byte inode replacement and changed
content. The stage fixture also runs the actual fixed child process, retains
its service-owned log, and rejects a changed launch digest before building any
binary record. It also replaces the driver's pathname after verifying its held
descriptor, replaces the selected source pathname after its full scan, and
confirms the original executable still runs in the original cwd without an
unrelated inheritable parent descriptor. A replacement present before launch
produces no log and poisons the build. An unexpectedly reaped child cannot become a
successful stage and releases its local descriptors after failing the build.
The native and sanitizer service suites register this dedicated test; verify
the exact submitted head on hosted runners.
The build fixture also replaces the generated `ide` name with a byte-equal
inode, moves its `Release` directory, and rewrites one byte without changing
the file size. Each change invalidates the held output observation before the
fixture restores the executable and completes the normal stage.
It swaps the configured directory after both generate stages and checks that
the corresponding build never starts. A candidate root planted before its
generate refuses the launch without a log. A generated root whose mode
changed fails before build.
The reference-policy fixture installs a template and inventory built for the
real A fixture's subjects and toolchain bundle. It checks a successful import,
refusal to overwrite a live policy and each missing or mismatched pin. It also
checks commit, tree, manifest, support, census-row and toolchain-manifest
mismatches, a writable template file and a byte-equal `bin/clang` inode
replacement. The public importer must fail on the blocked profile. It also
checks that the policy holds a close-on-exec, read-only inventory descriptor
whose release resets it.
The worker-unit fixture uses the same A job and a synthetic profile with the
reference pins. On the export side it checks that a wrong digest creates
nothing, that a second export is refused, and that the sealed directory's
bytes equal the queue record and request. On the unit side, a successful
prepare must yield the handoff digest, the A identities, the toolchain and a
held inventory whose hash equals the pin. The unit must refuse each of these
cases: a live object, the blocked profile, a profile without reference pins,
a mismatched handoff digest, another attempt token, a tampered record, a
tampered request, a writable file or directory, an extra file, an extra hard
link, a symlinked record and a replaced directory. The fixture counts
`/proc/self/fd` entries before and after to show that no descriptor leaks.
The unit-build fixture compiles the matched-build fixture a second time as
`fixture-broker`. That stand-in records each typed request in
`broker-launches` and runs the stage itself as the test user; it does not
prove the broker's identity split or sandbox. On fresh real-A attempts it
checks that the success path acknowledges PREPARING, issues the four
`start-stage` requests in order, seals ten evidence files, re-imports cleanly
and holds two different executables. It also checks that re-import rejects
a direct-launch binding, a tampered `matched-builds` record, a byte-equal
`bin/clang` inode replacement and a changed frozen executable. A live
result, a second build into the same attempt, the blocked profile and a
profile without the driver pin must all be refused. A corrupted
acknowledgement must leave no launch, log or evidence directory. A SIGALRM
self-pipe write and an expired deadline each stop a hanging generate: the
build returns cancelled or timed out, the recorded driver pid no longer
exists, the broker receives a `signal ... KILL` request and no receipt or
`trusted-build` is written. Descriptor counts match before and after.
The #1018 completion case builds on an attempt created by the real
`bq_materialize`, so the attempt is the production `02710` rather than a
fixture directory. Real materialization admits only the smoke recipe, so a
`validate-buster-v1` job with the same two commits creates the layout; the
retirement request then reseals that attempt, records A and exports it. It
checks that `bq_retirement_unit_build` succeeds with the four broker requests
in order. Afterwards the attempt is still `02710`, `retirement-work` is
private, and the two roots are `02700` and `02770`. Requiring a private
attempt again makes this case fail.
