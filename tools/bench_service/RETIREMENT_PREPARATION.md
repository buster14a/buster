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
compares them with B's rows. `project_service` derives those rows from the
pinned census and binds them to the #1020 per-row digest derived from pinned
`rows.tsv`
([definition](retirement_census_import.md#per-row-configuration_sha256-1020)),
and the unit passes that same array to `authority_begin`, which binds the
template transitively.
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
inventory pin. `bq_worker_unit` calls it only from the forked
[producer](#worker-unit-producer-881), which it admits only with a complete
profile; with the compiled blocked profile the `worker-unit` admission and the
supervisor's recipe-name gate still reject the job before any child launches.
The supervisor's export
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
stage within the worker's 10-second stop budget; the unit then returns
`BQ_WORKER_CANCEL_SIGNAL` or `BQ_WORKER_TIMEOUT`, or `BQ_CLEANUP_FAILED` if
absence is not proven. The self-pipe byte is not consumed. What is proven
depends on the launcher (`bq_retirement_matched_build_cancel`):

- **Broker stage (the unit's launcher).** The held child is only the broker
  CLI, so its process group says nothing about the stage unit, and a
  successful `signal ... KILL` (`systemctl kill`) does not prove absence.
  The unit first sends the broker `signal
  buster-bench-<job>-<attempt>-<stage>.service KILL`. A KILL that fails, for
  example because the unit is already collected (`--collect`) or not created
  yet (not loaded, `126`), is expected and not an error. While it has not
  succeeded and the CLI is alive, the unit repeats it every 250 ms, so a unit
  created after an early KILL is still reached. The unit keeps reaping the
  CLI and draining its log pipe until the deadline. The CLI exits only when
  the broker's status frame arrives or its connection breaks. Absence is
  proven only by a normal CLI exit whose status is not `125`
  (`BQ_RETIREMENT_STAGE_UNPROVEN_STATUS`). Such a status is one of three
  things: the relayed `systemd-run --wait` result, which means PID1 reported
  the unit finished; a refusal before any unit was started; or an abandoned
  relay after which the broker read the unit back as gone (see
  [SYSTEMD_BROKER.md](deploy/SYSTEMD_BROKER.md#abandoned-stage-relays-1785)).
  If the CLI is still running at the deadline, the unit kills its process
  group as a last resort and returns `BQ_CLEANUP_FAILED`.
- **Broker stage that settled badly.** A stage can settle without exit 0
  and complete capture: a nonzero CLI status (including `126` and `125`), a
  log overflow, or a capture or wait failure. It takes the same KILL and
  proof before anything else. If absence is proven, the stage is completed
  as before: a reaped nonzero exit writes its failed receipt, and the unit
  returns `BQ_WORKER_FAILED`. If absence is not proven, the unit returns
  `BQ_CLEANUP_FAILED` and writes no receipt.
- **Direct stage (fixture and queue API).** The child is the stage itself.
  The unit sends `SIGKILL` to its process group, reaps it and waits until
  `kill(-group, 0)` reports `ESRCH`. Descendants that leave the process group
  are outside this proof.

A failed or cancelled build leaves its partial evidence unsealed in the
attempt for the coordinator's failure path. The stage unit's cgroup and the
coordinator's recursive stop proof remain authoritative.

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

`bq_worker_unit` reaches the build only through the forked
[producer](#worker-unit-producer-881), and with the compiled profile its
admission still rejects the job first.

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

## Unit-side projection and oracle (#1020)

After `bq_retirement_unit_build`, the unit runs design steps 6 to 8 in
`retirement_unit.c`. Step 9, the correctness gate, runs the installed #509
required checks and the pinned row plan in the unit and issues the gate, but
still refuses in production because the blocked profile pins neither
authority ([step 9](#step-9-509-receipts-and-the-gate-issuer-1020)). Step 10, the ready
record, and the coordinator replay are described in the
[next section](#ready-record-and-coordinator-replay-1020).

`bq_retirement_unit_project` is step 6. It performs these steps in order:

1. It opens the nine pinned census files beside the reference template,
   in `recipes/native-retirement-performance-v1.census/`. The files are
   `support.tsv`, `source-applicability.tsv`, `inputs.tsv`, `rows.tsv`,
   `manifest.txt`, `validator-report.json`, `applicability.tsv`,
   `applicability-skips.tsv` and `performance-rows.json`. The last is
   #508's canonical performance-row population. Each file is opened without
   following links and held close-on-exec at descriptor 3 or above. The
   projection then checks every file against its own profile pin. The
   population's pin is the new key `performance-rows-sha256=`, and a profile
   without it fails closed with `BQ_RECIPE_MISMATCH`.
2. It opens the sealed `retirement-build/` directory and requires its exact
   closure.
3. It calls `bq_retirement_correctness_project_profile` with
   `BqRetirementBuildStores` made of the export and `retirement-build/`,
   never the queue. That re-imports A, the matched-build sequence (with its
   broker binding) and the binary record, and joins their preparation,
   source and binary identities into `BqRetirementPrepared`. It then runs
   the eligibility projection. The report must declare `full-census`, and
   its compiler and baseline digests must equal the two frozen binaries.
4. `bq_retirement_performance_rows_derive` imports the whole
   `BqRetirementTrustedRow` array from the pinned population, joined to that
   projection, for the pinned template's `native_target`. No caller can
   supply rows. The raw census join and `bq_retirement_validator_rows_join`
   then check that same array.

The projection is returned only when every check passes. It must name the
binaries the unit holds, and its support and census digests must equal the
template's.

### The imported population

The population artifact is the `performance_rows` support file that
`tools/native_retirement_performance_binding.py` reads (`ROW_SCHEMA`
`buster-native-retirement-performance-rows-v2`). Its bytes are exactly
`json.dumps(record, sort_keys=True, separators=(",", ":"),
ensure_ascii=False) + "\n"`. The C parser accepts only that canonical byte
sequence, in its one fixed key order. It is bounded to 128 MiB and to one
row per 256 bytes. Strings must be printable ASCII, and only the `\"` and
`\\` escapes are accepted. Anything else fails closed.

The import requires the following:

- **`row_identity_fields`.** The binding's `ROW_IDENTITY_FIELDS`, exactly.
- **Row numbering.** Rows are numbered contiguously from zero.
- **Object rows.** Rows `0..N-1` are the census object rows in census order.
  Row `i` must carry `artifact_stage=object` and the same approved #508
  identity digest as census row `i`.
- **Stage rows.** Every later row is a declared `link` or `self-host-stage1`
  row, and at least one of each must appear. Its census row is the unique
  census row whose identity it carries (every identity field except
  `artifact_stage`), found through a table of the census identity digests.
  The binding comes from the declaration, not a heuristic. The row's
  identity is the same serialization with its own `artifact_stage`, and no
  identity may repeat, as in the binding.
- **Stage rows avoid the supplement-resolved set.** Option 3
  ([#36](https://github.com/buster14a/buster/issues/36#issuecomment-5895408613))
  disposes of census object rows only. A declared `link` or `self-host-stage1`
  row must therefore not carry the identity of a supplement-resolved census
  row (one of the approved 276 allocator-`none` rows). The C import
  (`bq_retirement_performance_rows_derive`) and the binding
  (`_derive_compiler_eligibility`) both reject such a row. Whoever declares the
  stage rows must choose identities outside that set.
- **Values from the census row.** Classification, compiler eligibility and
  skip proof come from the census row's schema-2 projection. An option-3
  supplement-resolved allocator-`none` row is compiler-ineligible, and its
  skip proof is its supplement proof
  ([census import](retirement_census_import.md)). The source
  digest is the subject's support-declaration digest, and
  `configuration_sha256` is the census row's #1020 digest.
- **Eligibility markers.** `compiler_wall_time` and `compiler_peak_rss`
  must equal that compiler eligibility. The code-section and runtime-oracle
  markers follow the binding's pairing rules. `code_obligation` is the
  declared `generated_code_bytes`.
- **Native runtime.** `execution_obligation` is set when the identity's
  execution obligation is `semantic-gate-509`. `generated_runtime` must
  equal compiler eligibility combined with `_native_runtime_required`: that
  obligation, a `link` or `self-host-stage1` stage, and the native target.
- **Sources.** The `sources` digests for `support_declaration`, `inputs`,
  `rows`, `manifest` and `validator_report` must equal their profile pins.
  `dependencies` and `environment` must be well-formed, but those files are
  not held here.

The reviewed template pins the result, because its `population_sha256`
covers every row. Its reference rows are exactly the native-runtime stage
rows, in row order.

Command, oracle and #509 fields stay empty. `population_sha256` seals the
rows that `rows_join` accepted. This is an in-process misuse check, not a
security boundary. `bq_retirement_correctness_begin_service` now takes only
that projection. An intact projection still returns `BQ_RECIPE_MISMATCH`,
and one whose rows changed returns `BQ_SOURCE_MISMATCH`. Either way the gate
is poisoned and no binary is held.

`bq_retirement_unit_oracle` is steps 7 and 8. It requires these first:

- the profile's `reference-template-sha256` equals the policy's template;
- the projection still matches its seal;
- the cancellation descriptor is not readable and the deadline has not
  passed.

It then performs these steps in order:

1. `bq_retirement_oracle_authority_begin` receives the policy template, its
   pin, the projection's `prepared` and the same `rows` array.
2. The unit creates `retirement-work/reference-oracle/`, new and `0700`, as
   the producer's output directory. A second oracle into the same attempt is
   refused.
3. It holds A's materialized `base/source` and `candidate/source` copies as
   the producer's source roots. Each must still match the imported manifest
   and same-job materialized inode closure. Both copies are made `0550` by
   the materializer under the service UID, which also runs `worker-unit`, so
   they satisfy the producer's owner and write-bit checks on source roots.
4. `bq_retirement_reference_producer_begin` receives the decoded plan, both
   pins, the held inventory, `policy.source`, the roots, the toolchain
   manifest pin, the held Clang and the output directory.
5. For each reference row, the unit calls `producer_next`, then
   `bq_retirement_reference_producer_runtime`, then `authority_next`. The
   producer builds the runtime command from its own plan row, with argv[0]
   `/proc/self/fd/<held binary>` and cwd `/proc/self/fd/<output>`. The
   caller never composes argv.
6. It calls `authority_finish`, `authority_ready` and `producer_ready`.

Each step's deadline is the unit's absolute deadline, or 3500 s away if
that is earlier, because the producer and authority refuse child deadlines
more than an hour away. A step that exhausts its own 3500 s also returns
`BQ_WORKER_TIMEOUT`. The cancellation descriptor must be a close-on-exec,
read-only FIFO or socket (the SIGTERM self-pipe); any other descriptor is
refused up front with `BQ_CONFIGURATION_MISMATCH`. A readable cancellation descriptor returns
`BQ_WORKER_CANCEL_SIGNAL` and an expired deadline `BQ_WORKER_TIMEOUT`. In
both cases the running child's process group is killed and reaped. Any
failure releases the producer, the directories and the references, and
returns no facts. A partial reference directory stays in the attempt for
the failure path. `BqRetirementUnitOracle` keeps the finished authority,
which points into the prepared policy, the projection's rows and its
references.

`main.c` now defines `BQ_RETIREMENT_REFERENCE_PRODUCER_LINKED` before it
includes `retirement_oracle_authority.c`. The service's adapter therefore
accepts only the pending token of the live producer bound to that authority.
`bq_worker_unit` reaches these steps only through the forked
[producer](#worker-unit-producer-881), and with the compiled profile every
recipe gate still rejects the job first. `BqRetirementUnitOracle` also keeps, for each
reference, the held binary and output directory numbers of its
`/proc/self/fd` runtime command, so the replay can rebuild that command.

## Step 9: #509 receipts and the gate issuer (#1020)

Design step 9 has three parts: an installed authority that names every
required #509 check, a runner that executes those checks inside the unit,
and the issuer that joins their receipts with the per-row evidence in the
correctness gate and seals the result. The unit still never reaches the
queue or the lease.

### The required-check authority

`bq_retirement_required_checks_import` (in `retirement_correctness_service.c`)
reads `recipes/native-retirement-performance-v1.required-checks` from the
installed tree. The file's SHA-256 must equal the compiled profile's
`required-checks-sha256=` pin. The candidate tree, the request and the result
never name a check. The blocked profile carries no pin, so the import
returns `BQ_RECIPE_MISMATCH` before it reads any file.

The file is canonical LF-terminated text:

```
BQ-RETIREMENT-REQUIRED-CHECKS-V1
support=<sha256>
census=<sha256>
population=<sha256>
native-target=<id>
hosted=<commit> <tree> <sha256>|none
tools=<n>
tool=<i> <sha256> <name>
checks=<m>
check=<i> <kind> <target> <rows> <evidence> <timeout-s> <memory-MiB> <stdout-sha256>
configuration=<text>
argv=<a>
arg=<argument>
environment=<e>
env=<NAME=value>
```

The `tool=` line repeats `n` times. The five lines from `check=` to
`environment=` form one block, repeated `m` times, with `a` `arg=` lines and
`e` `env=` lines inside each block.

- **Joined facts.** `support`, `census`, `population` and `native-target`
  must equal the sealed projection's values.
- **Tools.** Each tool is an ELF executable under
  `recipes/native-retirement-performance-v1.checks/<name>`; an interpreter
  script is refused, so a tool cannot name an unpinned interpreter. It is
  held read-only and close-on-exec, and must hash to its pinned digest; a
  mismatch is `BQ_CONFIGURATION_MISMATCH`.
- **Kind and evidence.** `kind` is one of `census`, `semantic`, `matrix`,
  `no-fallback`, `self-host` or `fixed-point`. `evidence` is one of
  `native`, `emulated`, `compile-only`, `link-only` or `hosted`.
- **Hosted acceptance.** `hosted=` names A's candidate commit and tree and
  the SHA-256 of `recipes/native-retirement-performance-v1.hosted-acceptance`,
  the record of the exact-head hosted acceptance run. The commit and tree
  must be A's candidate subject, never the compiled profile's. The record
  must hash to the pinned digest, start with
  `BQ-RETIREMENT-HOSTED-ACCEPTANCE-V1`, `commit=` and `tree=` lines naming
  the same commit and tree, and hold a `lane=<target> passed` line for
  every hosted check. An absent record is `BQ_CONFIGURATION_MISMATCH`; a
  record that does not bind is `BQ_RECIPE_MISMATCH`. A hosted check has
  `argv=0` and `environment=0`, is a `semantic` check, and its pinned output
  is the record's digest. The record schema is provisional until the hosted
  acceptance workflow publishes it.
- **Tokens.** An argument or environment value may contain one token:
  `{{binary:0|1}}` (a held matched binary), `{{source:0|1}}` (A's
  materialized root), `{{tool:N}}` or `{{work}}` (the check's new working
  directory). `argv[0]` must be exactly a binary or tool token, never a
  path.
- **Environment.** Entries must be strictly ascending, so the resolved
  environment is canonical for the command digest.

The import rejects the whole authority (`BQ_RECIPE_MISMATCH`) unless it
meets every coverage rule below:

- It has every check kind.
- Each of the six #509 native hosts (Linux, macOS and Windows, each on
  x86-64 and AArch64: targets 11, 5, 8, 2, 10 and 4) is covered by a
  `native` semantic check on the projection's native target or by a
  `hosted` semantic check. `emulated`, `compile-only` and `link-only` checks
  are extra controls: they may appear but never cover a host.
- It has a `native` semantic lane on the projection's native target.
- No two checks share the same kind, target, evidence label and
  configuration.
- Census rows equal the object rows, and matrix and no-fallback rows equal
  the compiler-eligible rows.
- Only a check on the native target, or a host-wide check (target 0), is
  labelled `native`.

For each check the import derives three digests:

- a configuration digest over kind, target, evidence label and the
  configuration line;
- a command digest over the argv and environment templates, both bounds
  and every tool digest, with no descriptor numbers;
- the receipt digest that a passing run of this exact attempt must produce.

### The runner

`bq_retirement_check_run` (in `retirement_check_runner.c`) runs one check
in a bounded child, with these limits:

- The child is normalized by `bq_retirement_build_child`: it leads its own
  process group, has default SIGTERM, SIGINT, SIGPIPE and SIGCHLD handlers
  and an empty signal mask, umask `0077`, reads `/dev/null`, and every
  descriptor above stderr is made close-on-exec (`close_range`).
- It runs under the check's `RLIMIT_AS` with no core dumps.
- Its working directory is a new `retirement-work/check-work-<i>`.
- It inherits only the held binaries, both source roots, the tools and that
  directory; a fixture check probes `/proc/self/fd/<n>` for every number
  below 256 to prove it.
- It runs in the child sandbox described below, the one the row steps use.
  The run record names the Landlock ABI it enforced (`sandbox-abi=`).
- It runs under the check's wall bound, the job's absolute deadline and the
  cancellation self-pipe.
- A hosted check starts no child: its output is the held hosted record.

stdout is the check's summary, bounded to 1 MiB and compared with the
pinned digest. stderr is its log, bounded to 16 MiB.

**The child sandbox.** `bq_retirement_sandbox` builds it and
`bq_retirement_sandbox_enter` applies it just before exec, after
`PR_SET_NO_NEW_PRIVS`. Both live in `tools/throughput/retirement_sandbox.h`
with the canonical slot constants and the slot placement
(`bq_retirement_sandbox_slots`), so the row producer and lane D's measured
launches share one implementation of each. Lane D's child normalization
(`bq_retirement_sandbox_normalize`) sits beside them; it follows
`bq_retirement_build_child`'s signal, mask, umask and close-on-exec policy but
also resets `SIGALRM` and sets `RLIMIT_CORE` and `RLIMIT_AS`, which the row
spawn sets after `bq_retirement_build_child`. What it covers:

- **Kernel.** It requires Landlock ABI 6 (`BQ_RETIREMENT_SANDBOX_MIN_ABI`),
  the first ABI with the signal and abstract-socket scopes. An older kernel,
  or one without Landlock, fails every non-hosted check and every row step
  with `BQ_CONFIGURATION_MISMATCH`; it never runs with fewer rules. The
  hosted runners have ABI 7.
- **Files (Landlock).** The child may read and execute `/usr`, `/lib*`,
  `/bin` and `/sbin`; read `/etc` and A's two source roots; execute and read
  the held binaries (a check's tools too); use `/dev/null`; read the zero
  and random devices; and use its own new work directory freely. Opening,
  listing, creating, renaming, linking, removing and truncating anything
  else is denied. That includes `retirement-checks/` (this process writes
  every capture file), the reference oracle's outputs, the row evidence,
  other checks' and steps' directories, `/proc` and `/sys`.
- **Sockets (Landlock and seccomp).** Landlock denies TCP bind and connect
  and scopes abstract unix sockets and signals to the child's domain. It
  does not govern `connect()` to a path-named unix socket, and the child
  runs as the service user, whose UID is all the broker's and the service's
  control sockets check. So a seccomp filter refuses (`EPERM`) `socket`,
  `socketpair`, `connect`, `bind`, `listen`, `accept`, `accept4`, `sendto`,
  `sendmsg`, `sendmmsg` and the three io_uring calls, which could issue those
  operations without the system calls. It kills the child on another
  architecture and on an x32 call. No check or row step needs a socket: they
  compile and run programs over files.
- **Not covered.** Landlock governs no metadata at any ABI. The child can
  `stat` a path it knows, and can `chmod`, `chown`, `utimes` or change the
  xattrs of any file the service user owns, for example `chmod 000` an
  evidence or oracle directory. That is denial of service, not forgery: the
  child still cannot write those files. The gate notices a changed mode only
  where it checks one: the sealed check evidence (exact directory and file
  modes), the held binaries and tools (`fstat` identity around every child)
  and A's roots (the rescan requires read-only entries). Anywhere else, such
  as the reference oracle's outputs, the change shows up as a later failed
  read, which is a refusal. Without `/proc` and `/sys`, libc's
  `get_nprocs()` probably falls back to the affinity count, so a program
  sees a different CPU count than it would unconfined. Both sides see the
  same count (unverified).

The fixtures attack it:

- A check tries to read the oracle's output, another check's output and
  another check's work directory, and to plant a file beside its own. It is
  denied each time, prints `denied`, and its receipt does not match.
- A check copies a probe out of A's candidate root and runs it to connect
  to a listening unix socket beside its work directory. The probe is denied,
  prints `denied`, and the receipt does not match.
- With the ABI capped at 5 (`bq_retirement_sandbox_abi_ceiling`, a
  test-only seam), a check is refused with `BQ_CONFIGURATION_MISMATCH`.

Before the child the runner requires that this process has no child at
all, rehashes both held binaries and every tool, and `fstat`s the binaries,
tools and hosted record. After the child it requires each of those to be the
same file with the same device, inode, size, mtime and ctime, and the
binaries to rehash to the same digests. The sandbox denies writing a held
binary, and a same-user mode change and restore (which Landlock does not
govern) still fails with `BQ_SOURCE_MISMATCH`.
The sources in the receipt are the digests the caller's scan produced. It
then writes four files into the attempt's new `retirement-checks/`:
`check-output-<i>`, `check-log-<i>`, `check-receipt-<i>` and a
`check-run-<i>` record. The run record holds the concrete
`/proc/self/fd` command digest, the output and log digests and the CPU
model and affinity. Each file is created `O_EXCL`, then made 0400 and
fsynced.

The `BQ-RETIREMENT-CHECK-RECEIPT-V1` receipt binds:

- the job, token, attempt identity and request;
- A and the authority;
- the check's index, kind, target, rows, evidence label and configuration
  and command digests;
- the sources and binaries the child actually ran against;
- how the child ended: exit status, signal, timeout, OOM and failures;
- its stdout digest.

The runner classifies how the child ended:

- A child killed by a SIGKILL that the runner did not send counts as out
  of memory.
- The check's own wall bound sets `timed_out`.
- A capture overflow, a descendant that outlives the child, or wrong
  output counts as a failure.
- A cancelled or expired job returns `BQ_WORKER_CANCEL_SIGNAL` or
  `BQ_WORKER_TIMEOUT` after killing the group and reaping the child, and
  writes no receipt.

Descendants are contained with `PR_SET_CHILD_SUBREAPER` while the child
runs: one that leaves the group, even through `setsid`, is reparented to the
runner. After the child ends, `bq_retirement_check_sweep` lists this
process's children through `/proc`, kills and reaps every one, and a check
that left any fails. A per-check cgroup v2 leaf, which would also let OOM be
read from `memory.events`, needs the worker unit's delegated cgroup and is
not implemented; OOM is still inferred from an unrequested SIGKILL.

### The row-plan authority

The per-row half of step 9 has its own installed authority.
`bq_retirement_row_plan_import` (in `retirement_row_plan.c`) reads
`recipes/native-retirement-performance-v1.row-plan`, whose SHA-256 must equal
the compiled profile's `row-plan-sha256=` pin. The candidate never names a
row command. The blocked profile carries no pin, so the import returns
`BQ_RECIPE_MISMATCH` before it reads any file. The file is canonical
LF-terminated text:

```
BQ-RETIREMENT-ROW-PLAN-V1
support=<sha256>
census=<sha256>
population=<sha256>
native-target=<id>
cpu=<cpu-model-sha256> <logical-cpu>
templates=<t>
template=<i> <compile|batch|runtime> <timeout-s> <memory-MiB>
argv=<a>
arg=<argument>
environment=<e>
env=<NAME=value>
rows=<n>
row=<i> <-|batch|template> <-|template> <-|fixture>
groups=<g>
group=<g> <template> <allocator> <metrics-leaf> <metrics-bytes-max> <exit-status> <inputs>
input=<row|-> <member> <status> <error> <-|object-leaf> <fixture>
```

- **Joined facts.** `support`, `census`, `population` and `native-target`
  must be the sealed projection's, and `rows` its exact row count, one `row=`
  per row in order.
- **Templates.** Fields may hold any number of the tokens `{{binary}}`,
  `{{source:0}}`, `{{source:1}}`, `{{work}}`, `{{fixture}}`, `{{output}}`,
  `{{metrics}}`, `{{inputs}}` and `{{label}}`, as the kind allows. A compile
  template must use `{{fixture}}`, `{{output}}` and `{{metrics}}`; a batch
  template `{{inputs}}` and `{{metrics}}`. `argv[0]` is exactly `{{binary}}`,
  or `./{{output}}` (the artifact of the row's compile step) for a runtime
  template. Environment entries are strictly ascending.
- **Rows.** A timed object row (compiler eligible, native target, object
  stage) must be compiled in a batch group. Any other compiler-eligible row,
  including every cross-target row, has a compile template, so it yields
  compile-only and code facts and is never timed. A native, ineligible object
  row is either untouched or a batch control. A row with a native
  generated-runtime obligation, and only such a row, has a runtime template.
- **Groups.** Each group lists its inputs as the A1 contract freezes them:
  members first in ascending row order, then status-checked controls, each
  with its pinned status and error, and an object leaf exactly when it must
  compile. Each member and row-naming control names its row's own fixture,
  and every batch row is claimed by exactly one input. The skeleton must be a
  valid batch contract, and groups are ordered by their smallest member.

From the plan the importer derives, for the attempt:

- every command digest, by resolving the templates in the canonical child
  layout (the side's held binary at descriptor 3 or 4, A's base and
  candidate roots at 5 and 6, the step's work directory at 7, which is also
  the working directory) and hashing argv, cwd and environment exactly as
  the campaign's measured commands are hashed;
- each group's batch key (its template digest and allocator), which every
  member and control carries, and each control's `batch_control` mark;
- each group's per-side batch command, which becomes every member's and
  control's compiler command, and its response-file leaf;
- the second A/A label aggregate (`{{label}}` as `2`), in the campaign's
  order under lane D's domain;
- the projection's rows completed with all of the above; their population
  seal must still hold.

### The row producer

`bq_retirement_row_produce` (in `retirement_row_producer.c`) runs only on an
x86-64 Linux host (`BQ_RETIREMENT_ROW_HOST_NATIVE`), since it executes the
native target's (x86_64-linux) programs itself. On any other host it refuses
with `BQ_CONFIGURATION_MISMATCH` before any step, and the fixture's step
cases are skipped there with that reason reported. It runs the plan
on the plan's CPU alone (`sched_setaffinity`, restored afterwards). It
records the affinity and the model of that CPU as `/proc/cpuinfo` names it
(`model name`; on AArch64 the `CPU implementer`, `variant`, `part` and
`revision` lines; else `unknown`). It rehashes both held binaries once before
the first step and once after the last. Every step runs as a check does: no
other child first, `bq_retirement_build_child`'s normalized child with the
step's `RLIMIT_AS` and bounds, the job deadline and cancellation, and the
subreaper sweep. Each binary's `fstat` identity (device, inode, size, mode,
links, owner, mtime and ctime) must be unchanged around every step, so a
modify-and-restore during a step fails it with `BQ_SOURCE_MISMATCH`.

**Isolation.** Each step runs candidate-derived code: the candidate binary
compiling, and the program it generated running. Before exec the step
enters the child sandbox described under the runner, with its side's held
binary as the one executable. Its limits apply unchanged: no metadata
control, and a refusal below Landlock ABI 6. The producer checks the ABI
before the first step and records it in the evidence (`sandbox-abi=`), and
the join refuses an observation below ABI 6. The fixtures:

- A generated program reads the reference oracle's output and plants a
  file beside its step. It is denied, prints `denied`, and the row is
  refused.
- A generated program connects to a listening unix socket beside its step.
  It is denied and prints `denied`, and the row is refused.
- With the ABI capped at 5, the producer refuses before any step.

The steps still run as the service user: the broker's separate candidate
UID is not used here (see remaining work below).

For each side it runs:

- each batch group once, in a new `retirement-work/group-work-<g>-<side>`
  with the response file written there. The compiler's metrics records must
  authenticate (`tp_retirement_metrics_check`) against the contract made of
  the plan's pinned statuses, errors, exit status and bound with the
  observed diagnostics and objects, or the producer returns
  `BQ_RECIPE_MISMATCH`. Members' and controls' facts come from their inputs.
  Every object is frozen `0400` and read with the service's own artifact
  check (`bq_retirement_artifact_target`, `bq_retirement_artifact_read`) as an
  x86_64-linux relocatable, which also yields its code section;
- each per-row compile in a new `retirement-work/row-work-<row>-<side>`. The
  artifact is frozen (`0500` for an executable) and read with the same check
  for the row's own target and stage: the target's format and machine, a
  relocatable at the object stage and an executable otherwise. The metrics
  are checked as a batch's are: schema, record and status counts, the
  process's exit status, the stage's action (`object`, or `link` for link and
  self-host) and the row's fixture as the first input. The diagnostic is
  that input's compiler diagnostic digest (never a digest of stderr), and
  the fallback count is summed over all inputs. Link and self-host templates
  must pass the metrics flag (`{{metrics}}` is required in every compile
  template); without a record the fallback count is `UINT32_MAX` and the gate
  refuses. A row with a runtime template then runs its artifact there, with
  stdout and stderr on one writer, as the oracle captured its output; that
  digest is the runtime output the gate compares with the independent
  oracle.

`semantic_pass` is compile acceptance, not a per-row #509 semantic proof,
which the required checks' receipts carry. It holds when the compile exited
0 with no capture failure, every metrics input compiled (or, linking an
executable, was a `prebuilt` input), and the artifact passed the per-target
check.

The observation is `BqRetirementRowObserved`, with a canonical form,
`BQ-RETIREMENT-ROW-EVIDENCE-V1`. `bq_retirement_row_observed_parse` accepts
only bytes that format back to themselves, for this plan and attempt.
`bq_retirement_row_evidence_join` requires the observation to name this
plan, attempt and population, to have run on the plan's CPU alone with the
plan's model, and to record a sandbox of at least Landlock ABI 6
(`sandbox-abi=`; `BQ_CONFIGURATION_MISMATCH` otherwise). It then builds the
`BqRetirementRowEvidence` the correctness gate admits: the plan's completed
rows, the observed facts, and frozen batch groups from the plan's skeleton
with the observed diagnostics and objects. Whether each fact matches its
derived command, oracle, status and object is the gate's to judge.

### Lane D's measured launches in the same layout

Lane D's campaign launches (`tp_process_observe_inputs` in
`tools/throughput/platform.h`, driven by `retirement_unit_campaign.h`) run
in exactly this layout, so the digest `tp_retirement_command_hash` gives each
measured command is the one the plan derived for the same row, side and
label:

- **Commands.** `bq_retirement_campaign_plan_commands`
  (`retirement_campaign_service.h`) resolves every campaign command from the
  row plan the unit gate was issued on, with the plan's own resolver: argv[0]
  is `/proc/self/fd/3` or `/4` by side, A's roots are `/proc/self/fd/5` and
  `/6`, the directory is `BQ_RETIREMENT_ROW_WORK_PATH` and the environment is
  the template's. Side is the variant in the A/B stage and the baseline in the
  A/A stage, whose second variant uses `{{label}}` = `2`. Timeout and
  address-space bound come from the template. Every label-1 command must hash
  to the digest the gate sealed for it (its batch group's, or its row's
  compiler or runtime command); the label-2 commands are bound by the gate's
  A/A aggregate when the campaign binds.
- **Child.** The launch takes the side, A's two roots (held by the store
  import, `bq_retirement_unit_source_root`, as the gate holds them) and a
  ruleset built by `bq_retirement_sandbox` over the side's held binary, the
  roots and the work directory. The child takes `/dev/null` as stdin and
  normalizes (`bq_retirement_sandbox_normalize`: default signals, an empty
  mask, umask 0077, every descriptor from 3 marked close-on-exec, no core,
  the template's `RLIMIT_AS`), parks the held descriptors and the ruleset
  above 64 and `dup2`s the four into 3 + side, 5, 6 and 7
  (`bq_retirement_sandbox_slots`), `fchdir`s to 7, then sets
  `PR_SET_NO_NEW_PRIVS` and enters Landlock and the seccomp filter through
  `bq_retirement_sandbox_enter`, and finally `execve`s argv[0].
- **Parent.** Before the child the launch requires the directory to be
  exactly `BQ_RETIREMENT_ROW_WORK_PATH` and argv[0] the side's slot, the
  work directory's `fstat` identity to be owned by the service user and not
  group- or world-writable, the memory bound to be the command's, and a
  compile's output directory to be the work directory.
- **Timer.** Everything above happens before the timer: the ruleset is built
  before the fork, and the child reports over a pipe only once it is placed
  and inside its sandbox. The parent then reads that report, takes the start
  time, arms the timeout and writes the go byte; the child reads it and calls
  `execve`. The measured interval is that one pipe wake-up, the exec and the
  program.
- **Refusal.** The report is 0, or the errno of the first child step that
  failed (affinity, process group, log, stdin, normalization, parking or
  placing a slot, `fchdir`, no new privileges, Landlock or seccomp); the child
  closes its end right after writing it. The parent waits for the report
  together with the cancellation descriptor, bounded by the launch's timeout,
  so a child that fails before reporting never blocks it: the launch returns
  as refused (`TpProcess.refused`) with that errno as its launch error, and
  the driver records it as `REFUSED`. A cancellation during the wait is a
  cancellation, a child that exits without a report is `ECHILD` and one that
  never reports is `ETIMEDOUT`.

The preparation runner's campaign fixture issues the gate over a row plan
that follows the validator's timed partition and checks, for a timed batch
group and a timed singleton with a runtime row, per side and for the A/A
second label, that D's digests equal B's. The throughput fixture has a child
report what it sees: exactly descriptors 0, 1, 2, its side's slot, 5, 6
and 7, cwd `/proc/self/fd/7`, a file outside the sandbox denied and a
connection to a listening unix socket outside it denied; a command naming
the other side's slot is refused before any child.

### The issuer

`bq_retirement_unit_gate` imports both authorities for the attempt and
requires that the unit has no child process. It then creates new
`retirement-checks/` and `retirement-rows/` (mode 02700), with their
directory entries made durable, holds and scans A's two materialized roots,
and runs every check in authority order. The first failing check stops the
gate with `BQ_RECIPE_MISMATCH`; its receipt stays in the unsealed directory.
It then runs the row producer, writes the canonical row evidence as
`retirement-rows/row-evidence` (`O_EXCL`, `0400`, fsynced), parses those bytes
back and joins them with the plan, so the gate admits exactly what it
persisted. Before the first row step it closes, digests and seals
`retirement-checks/` (`0500`), and after the rows it requires the sealed
evidence to be unchanged. After the rows it rescans both roots with `bq_retirement_scan` (a
change is `BQ_SOURCE_MISMATCH`), requires the check evidence to close
exactly, computes the ordered digest of every check's run record and log,
and requires again that no descendant is left (`BQ_CLEANUP_FAILED`).

`bq_retirement_unit_gate_admit` then drives the correctness gate:

1. `begin`, over the projection's sealed rows as the row plan completes
   them;
2. `check` for each observed result in order, which requires each receipt
   to equal this attempt's expected receipt;
3. `row` for each row fact;
4. `batches` for the frozen plan-v3 batch groups;
5. `bq_retirement_correctness_authorize` with the authority digest, then
   `finish` and `ready`.

`bq_retirement_correctness_authorize` in `retirement_correctness.c` is the
only writer of `batch_authority`. It grants the authority once, only to a
gate whose checks, rows and batches are complete, and records the authority
digest, which the correctness seal covers. A fixture scans every non-test
`tools/bench_service` source for any assignment (plain or compound),
increment, decrement or address-of of `batch_authority` and requires exactly
that one. The issuer also requires the projection's
native target to be `BQ_RETIREMENT_UNIT_NATIVE_TARGET`, the A1 native-host
target (x86_64-unknown-linux-gnu, id 11). The tests check that this target
equals lane D's `BQ_RETIREMENT_NATIVE_TIMED_TARGET`.

`bq_retirement_unit_gate_issue` ties the result to the attempt's joined
objects. Its seal binds these facts:

- the attempt identity and request;
- A and the population;
- the oracle attempt;
- the authority digest and check count;
- the ordered receipt aggregate;
- the ordered digest of the run records and logs;
- the row-plan authority's digest;
- the persisted row evidence's digest, which also binds its CPU
  provenance;
- the correctness gate's own seal, which covers every check result, row
  fact, frozen batch group, the batch authority and the authority digest.

The correctness seal cannot also cover the unit seal, which is computed
over it; the unit seal and the ready record's `gate=admitted` line cover the
correctness seal instead.

The issuer then seals `retirement-checks/` and `retirement-rows/` to `0500`.
The issued gate owns the frozen batch groups its correctness gate points
into.

The gate refuses receipts from another job or token, a swapped or changed
binary, a missing check, a forged or foreign row plan, an observation of
another plan or attempt, a row missing or added, a command, CPU, batch-key
or control-status mismatch, and changed row evidence.

**Remaining work.** The blocked profile pins neither authority, so the
public entry refuses before either directory exists or any child starts.
Integration must install and pin a reviewed row plan for the real corpus;
the fixture plan and stand-in compilers prove mechanics only. The row steps
and the required checks share one sandbox (Landlock plus a seccomp socket
filter) but still run as the service user. Running them as the broker's separate
candidate UID, in a unit that cannot read the service's workspace at all,
needs per-step broker stages (a follow-up on #1021) and is not done. A
per-step cgroup v2 leaf is still missing.

## Ready record and coordinator replay (#1020)

`bq_retirement_unit_ready` is design step 10. It first requires a gate that
step 9 issued for exactly this attempt, with a seal that verifies against
the live facts, and a profile whose `required-checks-sha256=` pin still
equals the gate's and the correctness gate's authority digest and whose
`row-plan-sha256=` pin still equals the gate's row plan. Otherwise it
returns `BQ_RECIPE_MISMATCH` before it examines any object or touches the
attempt; the public entry uses the compiled blocked profile, so it always
refuses. It then requires every one of these, or
returns `BQ_BAD_REQUEST`:

- The prepared, built, projected and oracle objects are live and belong to
  the same job and attempt.
- The authority points at the prepared policy's template and the
  projection's rows.
- The projection still matches its population seal and names the held
  binaries.
- `authority_ready` holds.
- The gate's correctness gate is ready, carries the batch authority and
  binds this projection's A and binaries.
- The gate's receipt aggregate still matches its required checks.

It then requires the sealed `retirement-checks/` to hold exactly those
receipts, with run records and logs whose ordered digest is the gate's, and
the sealed `retirement-rows/` to hold exactly the row evidence the gate
admitted (`BQ_SOURCE_MISMATCH` otherwise).

`BqRetirementUnitGate.issuer` is only a marker, not a capability: any
in-process caller can set it. Admission rests on the seal verifier and, on
the coordinator side, on receipts re-derived from the pinned authority.
`build.c` compiles the service and its `tests.c` with `BQ_SERVICE_INSTALLED`,
and `retirement_unit.c` stops with `#error` if that build also defines
`BQ_RETIREMENT_CORRECTNESS_TEST_ONLY`. `tests.c` checks two things in the
installed build: that the verifier refuses a well-formed seal over facts
without check digests, and that the writer refuses an issuer-marked gate it
does not own without creating `retirement-ready/`.

With a verified gate the writer performs these steps in order:

1. It seals `retirement-work/reference-oracle/` to `0500` and rehashes its
   exact closure (below). Each reference's binary, receipt and output must
   equal the authority's (`BQ_SOURCE_MISMATCH` otherwise), and each runtime
   command rebuilt from the recorded descriptor numbers must hash to the
   authority's concrete command digest. It then formats the canonical
   record. A failure in this step leaves no `retirement-ready/`, so the
   attempt can still write its record once the cause is gone. An allocation
   failure is `BQ_IO`.
2. It creates `job-<id>-attempt-<token>/retirement-ready/` with the
   inherited-group helper (mode `02700`) and fsyncs the attempt, so the
   directory entry is durable before the record exists. The directory must
   be new, so a second record into the same attempt is refused.
3. It writes the record as `ready-partial-<id>` (`O_EXCL`, mode `0400`,
   fsynced). It links that file to `ready-<SHA-256 of the record>`, unlinks
   the temporary, requires the directory to hold exactly that one file,
   seals it to `0500` and fsyncs it.

Nothing needs an fsync after that publish, so a published record always
returns its digest; closing the read-only descriptors afterwards cannot
undo it.

Sealing `reference-oracle/` does not stop another process with the service
UID, such as a descendant of the unit, from making it writable again. That
is only a denial of service: the writer and the replay both compare every
file with a digest, and the replay also requires the `0500` mode, so a
changed file or mode fails the record rather than changing what it binds.

It returns the record digest. The coordinator must receive that digest over
an authenticated channel, as it does the other record digests; the channel
itself belongs to the #923 integrator.

### The record

The record is `BQ-RETIREMENT-READY-V1`, one `key=value` line each, in this
order:

| Key | Value |
|---|---|
| `job`, `attempt` | the job number and attempt token |
| `attempt-identity` | SHA-256 of the attempt's canonical `.identity` seal |
| `request` | the canonical request digest |
| `preparation` | A's preparation record digest |
| `binaries`, `matched-builds` | the binary and final matched-build record digests |
| `binary-base`, `binary-candidate` | both frozen executables' SHA-256 |
| `support`, `census`, `population`, `rows`, `object-rows`, `native-target`, `evidence` | the projection's joined identities, row counts and seals |
| `template`, `inventory` | the installed reference policy pins |
| `oracle-attempt`, `oracle-observed` | the authority's attempt digest and observation chain |
| `observed-rows` | the reference count |

Then one `observed=` line per reference, in template order: index, row,
census row, target, the runtime command's binary and working-directory
descriptor numbers, then the output, binary, receipt and concrete command
SHA-256 values and the output name. Then come the step 9 lines:

| Key | Value |
|---|---|
| `checks-authority` | the installed required-check authority's SHA-256 |
| `checks` | the number of required checks |
| `check-receipts` | the ordered aggregate of their same-attempt receipt digests |
| `check-evidence` | the ordered digest of their run records and logs |
| `row-plan` | the pinned row-plan authority's SHA-256 |
| `correctness` | the correctness gate's seal |

The last line is `gate=admitted <seal>`.

### Replay

`bq_retirement_unit_replay` runs on the coordinator side, outside the unit,
and only reads. It takes the attempt's export store, the coordinator's own A
digest and the authenticated record digest, and performs these steps in
order:

1. It re-imports A, the toolchain and the reference policy through the
   unit's prepare, which also checks the export's exact closure.
2. It requires `retirement-ready/` to be sealed `0500` and to hold exactly
   `ready-<digest>` as a single-link, owner-read-only regular file. It
   requires that file's SHA-256 to be the digest.
3. It reads only these from the record: the two build record digests, the
   gate seal, and each observed row's descriptor numbers and command digest.
   Observed lines must be consecutive and in index order, with canonical
   decimal fields bounded by their separators. With the build digests it
   re-imports the matched builds, holds both binaries and reruns the census
   projection.
4. It requires `reference-oracle/` to be sealed `0500` and to hold exactly,
   for each template reference `i`, `reference-<i>`, `reference-log-<i>`,
   `reference-receipt-<i>` and the template's output name. Each must be a
   single-link, service-owned, read-only regular file. It rehashes them and
   rebuilds each receipt from the policy. Only the receipt's observed build
   command, whose `/proc/self/fd` paths nothing else records, is taken from
   the file, and it must be hex.
5. It rebuilds each runtime command from the plan row and the recorded
   descriptor numbers. Every row must use the same working-directory
   descriptor, and no row's binary descriptor may equal it. The rebuilt
   command's digest must be the recorded one, and its logical digest the
   template's.
6. It rebuilds the finished authority from the template, the projection and
   those observations. As `authority_next` does, it requires each reference
   binary to differ from both matched binaries. It recomputes the ledger's spec and seal, the
   observation chain and the attempt digest with the authority's own
   hashes, and then calls `authority_ready`.
7. It re-imports the required-check authority for this attempt and
   requires `retirement-checks/` to be sealed `0500` and to hold exactly the
   four files of each check. Each receipt must be the one a passing run of
   this attempt produces. Each run record must name that receipt and the
   digests its output and log files rehash to. It recomputes the ordered
   digest of the run records and logs.
8. It re-imports the row-plan authority from its pin, requires
   `retirement-rows/` to be sealed `0500` and to hold exactly `row-evidence`,
   parses it (canonical only), joins it with the plan and reruns the
   correctness gate over it and the passing results the receipts prove. That
   gives the row-plan digest, the correctness seal and the row evidence's
   digest.
9. It verifies the gate seal against the re-derived facts, formats the
   record those facts give and requires the stored record to equal it byte
   for byte.

A missing, extra, symlinked, writable or reordered entry fails closed, as
does an unsealed directory. A crash between the temporary and the link
leaves only `ready-partial-<id>` in an unsealed directory, and a crash after
the link leaves two links; both fail the replay, and the unit refuses to
write into the leftover directory. The public wrapper uses the compiled
profile, so the blocked profile fails closed, and a profile without the
required-checks or row-plan pin fails its authority import.

The replay cannot re-derive two values from anything but the record and the
receipt:

- the runtime command's descriptor numbers, which it rebinds through the
  command digest and the attempt digest;
- the reference receipt's observed build command.

Only the record digest from the authenticated channel anchors those values.
The check receipts and the row plan, by contrast, are re-derived from the
pinned authorities, and the correctness seal is recomputed from the
persisted row evidence. So another job's or token's receipt, a swapped
binary, a missing check, a row-evidence byte that the gate rejects, and a
changed `row-plan=` or `correctness=` value each fail the replay.

The row observations themselves (artifacts, codes, diagnostics, runtime
outputs, CPU provenance) are not re-observed. Like the descriptor numbers,
they are anchored only by the authenticated record digest and the unit's
honesty. The fixture shows both limits:

- A record whose row descriptor, command digest, observation chain, oracle
  attempt and gate seal are all changed together still replays.
- Row evidence rewritten consistently still replays: one compile's artifact
  digest changed, the correctness gate rerun over it, and the gate seal and
  record recomputed under a new record digest.

## Worker-unit producer (#881)

`retirement_worker_unit.c` wires the steps above into `bq_worker_unit`
(PR 1 of 4 of the #881 worker-unit wiring). It is compiled only into the
service translation unit, after `retirement_unit.c`.

The steps cannot run in the recipe executable, because that binary is also
the pinned matched-build driver. They cannot run in the unit process either:
step 9 requires a process without children, and the lease keeper is already
a child of `worker-unit`. So the unit process keeps the lease and the keeper
and forks a producer.

**Admission.** `bq_worker_unit_pinned` admits the retirement recipe only when
`bq_retirement_profile_complete` accepts the profile. That requires every
integration pin (`bq_retirement_worker_unit_pins`: the A, toolchain, driver,
reference-policy, nine census, required-checks, row-plan and campaign-budget
digests) and no `status=blocked` line. `bq_worker_unit` passes the compiled
profile (`bq_retirement_worker_unit_installed`), which is blocked. A
production retirement job is therefore still refused with `BQ_BAD_REQUEST`
before the lease handoff, the keeper, any directory or any child. The other
recipe gates (`bq_recipe_admitted`, `bq_recipe_service`, `bq_request_valid`,
`bq_worker_run`'s recipe-name check and `bq_worker_finalization_recipe`) are
unchanged and still refuse the recipe. Only the test seam admits it, with a
complete fixture profile.

**The unit process.** After the pause and the pre-exec lease recheck,
`bq_retirement_worker_unit_run` performs these steps in order:

1. It blocks SIGTERM, installs a handler that forwards SIGTERM to the
   producer, and refuses if SIGTERM is already pending or the deadline has
   passed.
2. It forks the producer. The lease and phase descriptors stay close-on-exec
   on this path.
3. It closes its own copy of the phase descriptor. The producer is the
   channel's only writer, so the coordinator sees EOF when the producer
   exits.
4. It waits for the producer on a pidfd until the execution deadline plus
   twice the 10-second stop budget. A producer still running then is killed.

`bq_worker_unit_pinned` then stops the keeper as before and returns the
mapped status:

- An exit status between 1 and `BQ_EXPORT_TIMEOUT` is the producer's
  `BqError`.
- Exit 0 cannot be a success before MEASURED is wired, so it returns
  `BQ_WORKER_FAILED`.
- A producer killed by a signal, or at the bound, never proved its
  descendants absent, so it returns `BQ_CLEANUP_FAILED`.

**The producer.** `bq_retirement_worker_unit_child` performs these steps:

1. It closes its lease reference and sets `PR_SET_PDEATHSIG` to SIGTERM.
2. It becomes a child subreaper.
3. It installs a SIGTERM handler that writes to a close-on-exec,
   non-blocking self-pipe. The pipe's read end is every step's cancellation
   descriptor.
4. It requires that it has no child yet.

`bq_retirement_worker_unit_produce` then runs store open, prepare, build
(which sends PREPARING), project, oracle, gate and ready through their
profile seams. The projection holds the pinned census files itself. The
stop reason is rechecked between steps.

Everything is released in reverse on every path, and a surviving
descendant turns the result into `BQ_CLEANUP_FAILED`. A failure sends no
phase message after PREPARING. Its partial evidence stays in the attempt,
and the ready record is never written.

**Not wired yet.** After the ready record, PR 1 stops with
`BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED` (`BQ_UNSUPPORTED`). It does not send
SETTLING or MEASURED, so the job fails closed. The record's digest appears
only in the producer's stderr diagnostic. Decision 1 of the design moves it
to a versioned phase packet (PR 4), never through files, so this PR writes no
`ready-sha256=` failure-bundle line.

The remaining work is split across three PRs:

- **PR 2:** the in-unit campaign through READY.
- **PR 3:** composition, the authority and MEASURED.
- **PR 4:** the coordinator's phase packet, the replay at finalization and
  the coordinator-side gates.

A/A admission stays compiled out (#426, #1021).

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
`bq_retirement_correctness_project_service` requires that authenticated build
record digest before it derives B's rows, and `begin_service` accepts only
that projection before it would open B's binary holder or gate.
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
`broker-launches`. It runs the stage as the test user in a forked child,
which plays the unit, records the child's pid, waits for it the way
`systemd-run --wait` does and relays its status. Its `signal ... KILL`
kills a recorded unit, or returns `126` when there is none. It does not
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
`trusted-build` is written. The broker cleanup cases call the cancel
directly:
- A stage that already finished and was collected is proven by the CLI's
  relayed exit, although its KILL returns `126`. The old
  group-then-signal order reported `BQ_CLEANUP_FAILED` here.
- A cancel before the unit exists retries the KILL until it reaches the
  stage.
- A KILL that reports success but leaves the stage running makes the
  cancel false at its deadline, with the CLI and the stage killed.
Through the unit, four more jobs check the other broker outcomes:
- A relayed exit 5 and a refusal with `126` are proven and write failed
  receipts, and the unit returns `BQ_WORKER_FAILED`.
- A log overflow is proven and refused.
- A relayed `125` returns `BQ_CLEANUP_FAILED` with no receipt.
Each of the four sends exactly one KILL. Descriptor counts match before and
after.
The unit-oracle fixture (`retirement_unit_oracle_tests.h`) runs the whole
sequence on a separate real-A tree:

- **Census.** `retirement_validator_eligibility_test.py --emit` writes the
  genuine schema-2 census with that module's Python configuration digests.
  It also writes a #508 performance-row population built the way the
  binding test builds one, which the binding's own parser accepts, and that
  module's `expected_population` for it. The population has a native `link`
  row on census row 0, a native `self-host-stage1` row on census row 1, a
  foreign-target `link` row on census row 64 and an untimed
  `self-host-stage1` row on census row 16. (A1) The native target is the
  pinned native-host timed target `x86_64-unknown-linux-gnu`, since the
  binding rejects generated runtime on any other target; the fixture ledger
  therefore makes the `aarch64-unknown-linux-gnu` rows (16 to 31) unavailable.
- **Sources.** The baseline snapshot also holds the census subject's bytes
  at `tests/unit.c`. This is the reference source.
- **Toolchain.** `bin/clang` is a copy of the host compiler.
- **Build.** A `census-fixture` marker makes the fixture driver freeze the
  census manifest's literal compiler bytes, so the compiler/baseline join
  holds.

The sequence is prepare, a fixture-broker build, project and oracle. The
fixture checks these properties:

- Every imported row's census row, stage, native-runtime applicability and
  configuration digest equal the Python reference.
- Each stage row names the census row its declaration carries.
- A profile without, or with a changed, `performance-rows-sha256` pin fails
  closed.
- The blocked profile, a missing census pin, a changed report pin and a
  `full-census` requirement on the self-test report each fail closed.
- `begin_service` stays fail-closed, and a changed row is refused by both
  `begin_service` and the oracle.
- The oracle's success produces the reference and oracle files, and a second
  oracle is refused.
- In the service translation unit, forged and foreign tokens are refused
  before any child runs, and a consumed token is stale.
- With a second template whose second reference program spins, a SIGALRM
  cancellation and an expired deadline each fail closed with no live child
  after the first row finished.
- No descriptor leaks.
- `worker_linux.c`, `main.c`, `workspace.c` and `queue.c` name none of these
  entry points, and `main.c` never defines
  `BQ_RETIREMENT_CORRECTNESS_TEST_ONLY`.

The same fixture then covers the ready record and the replay
(`bq_prep_test_unit_ready`):

- The production gate refuses. With that gate, a forged gate or no gate,
  the writer returns `BQ_RECIPE_MISMATCH` and creates or seals nothing.
- A fixture gate with a changed seal is refused, and so is a changed row.
- A reference output changed after the oracle makes the writer return
  `BQ_SOURCE_MISMATCH` without creating `retirement-ready/`; restored, the
  same attempt then writes its record.
- Through the fixture gate the writer seals one `0400` record under its
  content address and seals the reference directory. A second record is
  refused.
- The replay accepts the record. The blocked profile, another token, a
  wrong A digest and a wrong record digest each fail.
- Each header field, each field of an observed row and both gate fields is
  tampered with, re-addressed under its new digest and passed to the
  replay with that digest. The same holds for reordered observed rows, an
  appended line, a changed first line and a row descriptor moved to another
  valid number. Every case fails with the error of the check meant to catch
  it, and the restored original replays after each.
- The consistent forgery above replays, documenting the anchors.
- Both crash states fail, and so do an unsealed directory, an extra entry, a
  symlinked record and a writable record.
- An unsealed or extended reference directory fails, and so do a missing
  output and a changed output, receipt or log, each with its expected error.
- Descriptor counts match, and no child is left.

The #1018 completion case builds on an attempt created by the real
`bq_materialize`, so the attempt is the production `02710` rather than a
fixture directory. Real materialization admits only the smoke recipe, so a
`validate-buster-v1` job with the same two commits creates the layout; the
retirement request then reseals that attempt, records A and exports it. It
checks that `bq_retirement_unit_build` succeeds with the four broker requests
in order. Afterwards the attempt is still `02710`, `retirement-work` is
private, and the two roots are `02700` and `02770`. Requiring a private
attempt again makes this case fail.

The worker-unit producer fixture (`retirement_worker_unit_tests.h`) runs on
the unit-oracle fixture.

**Setup.** A reference attempt's projection and oracle pin B's stand-in
required checks, a row plan and the reviewed campaign budget. That completes
the profile. The gate takes an honest synthetic observation of the plan
through its seam, re-addressed to each producer attempt; the plan's
commands are logical, so only the job and token differ.

**Driving the unit.** The test process plays the coordinator:

1. It hands the lease and the phase channel to a forked `worker-unit`
   through `bq_worker_lease_handoff_send`.
2. It resumes the paused unit.
3. It acknowledges every phase message and reads the channel to EOF.

The unit reports its `BqError` as its exit status. It reports 200 instead if
a child was left over (an unstopped keeper or an unreaped producer) or its
descriptor count changed.

**Checks.**

- **Profile.** Every digest pin of the complete profile is required, and so
  is every pin in the list. A non-digest pin, a `status=blocked` line and a
  second status line are refused.
- **Blocked profile.** The public `bq_worker_unit`, a blocked-status copy and
  a copy without the row-plan pin each return `BQ_BAD_REQUEST`. No keeper
  directory, no `retirement-build/` and no child exists afterwards.
- **Success.** The producer runs through the ready record and exits with the
  not-yet-wired `BQ_UNSUPPORTED` after exactly one PREPARING message and EOF.
  `retirement-ready/` holds one `ready-<digest>` that
  `bq_retirement_unit_replay_pinned` accepts. The gate's no-children check
  passed, so the keeper was not the producer's child, and the keeper was
  stopped.
- **SIGTERM.** A SIGTERM to the unit during job 63's hanging generate is
  forwarded to the self-pipe. The unit returns `BQ_WORKER_CANCEL_SIGNAL`, the
  stage's pid is gone and no ready record exists.
- **Failed stage.** Job 67's failing generate returns `BQ_WORKER_FAILED`.
- **Killed producer.** Killing the producer during job 64's hanging generate
  returns `BQ_CLEANUP_FAILED`. The test reaps the orphaned broker CLI and
  stage as a temporary subreaper.

In every case the keeper's socket is gone and no descriptor or child leaks.

**Mutations.** Each of these makes the fixture fail:

- running the producer in the unit process;
- dropping the not-yet-wired result;
- dropping the admission check, a pin or the status check;
- not forwarding SIGTERM, or not writing the self-pipe;
- not stopping the keeper;
- mapping a signalled producer by its exit code.
