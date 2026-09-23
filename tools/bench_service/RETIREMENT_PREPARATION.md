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
source commit/tree, manifest, capacity, outcome and number of independently
verified subjects, including for partial failures. The correctness lane must
require `status=ready`, both completed subjects and the actual ready job,
then recheck its materialized inputs.

The supervisor now calls `bq_retirement_preparation_ready` after result-root
creation and before unit launch. It independently rereads the private durable
record, rechecks the installed inventory and source bytes without repeating the
pre-copy free-space reservation, and hashes both materialized source copies.
The exact canonical successful record must match the current job, token and
request. It records each materialized copy's same-job inode closure in addition
to its portable manifest identity, so a byte-equal replacement fails on
readback. A missing, failed, changed or stale record, or a modified source copy,
prevents launch. The returned service-owned record digest is an A handoff
identity; matched build manifests and the authenticated B importer are still
required before timing can become available.

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
integration owner must use the verified `source` directories and
`preparation-<job>` facts in that path, replace its smoke-specific command and
stage policy only when the retirement recipe is admitted, and retain the exact
build argv/environment, trusted compiler/linker/resource/SDK/sysroot and
dependency identities, build logs, output binary bytes/digests and source/tree
relations. Its timed compilers must come from those trusted Clang stages;
self-built stages may supply correctness evidence only. The pre-timing
correctness lane must consume the complete provenance and both verified input
identities before declaring ready for timing. This child implementation does
not assert build provenance merely because the source preparation succeeds.

## Focused fixture

Compile and run `retirement_prepare_tests.c` alongside
`tools/throughput/shared.c` with the ordinary service `-Isrc`,
`BUSTER_SINGLE_THREADED=1`, C11 and warnings-as-errors flags. The fixture
checks the pinned inventory, mismatched pin, invalid source request, impossible
entry bound, unavailable storage query, source mutation, missing file, duplicate
manifest entry, unlisted file/directory/symlink, byte-equal inode replacement,
two verified copies and removal of the temporary copy. The #923 integration owner registers this dedicated
test alongside the native and sanitizer service suite, then proves its exact
submitted head on hosted runners.
