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
put exactly the timed executables at `job-<id>-attempt-<token>/trusted-build/`
`base-ide` and `candidate-ide` beneath the workspace root. The
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
using the same `matched-build` configured pathname, fixed Release/Clang flags,
single-job Ninja build and explicit environment. The source of `PATH` is the
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
