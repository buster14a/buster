# Raw #508 Census Import Boundary

The staged C projection reads held descriptors for #508's support declaration,
`inputs.tsv`, `rows.tsv`, `manifest.txt`, and the source-owned
`docs/native-retirement-applicability-v1.tsv` ledger, plus the schema-2
validator report, `applicability.tsv`, and `applicability-skips.tsv`. Every file
has a separate compiled-profile SHA-256 pin. The importer recomputes the
manifest identity, `rows_identity_sha256`, and `input_ledger_sha256`, and
cross-checks the source ledger digest/count in the manifest and report. For
full-census it requires the approved 374-entry ledger digest and four shards.
It checks the report's schema, profile, binary/support identities, row/group
counts, complete partition, exact class names/maps/membership, clean-candidate
flags, and failure arrays. Candidate and final reference failures,
fallback/telemetry/execution/artifact defects, and unexpected failures must
be empty. It retains the direct-reference failures recorded before supplemental
resolution and uses their group-level pattern to derive the
`retained-reference` class. The current full-census hosted recipe uses
`--reference-supplements`; this staged projection checks four canonical
supplement digest strings, but does not independently authenticate or replay
the supplemental bytes. For retained direct-reference failures that the report
says were resolved, it checks the validator's rewritten reason, ownership, and
acceptance flag while preserving the original class. Other acceptance failures
must match the `unavailable` class; `clean_acceptance` must agree with the
resulting set. Full-census requires both `require_clean_acceptance` and
`clean_acceptance`, as in the hosted producer. The separate #508 performance
binding's stricter admission checks still apply before trusted use.

The source ledger and authenticated non-object source obligation determine the
exact expected non-executed set, class, and reason. For rows absent from that
ledger, the C projection derives its default class, reason, and ownership from
the row's execution and compile obligations, allocator role, and retained
direct-reference failure set. It does not replay baseline results or the
supplemental controls from the shards. The report skip IDs and
`applicability-skips.tsv` must match that set row by row. In particular, every
ledger-declared platform-inapplicable/unavailable row must be skipped, and an
ordinary supported object row cannot be skipped as a retained control. The
projection derives compiler eligibility from this exact source-derived set
and creates row-bound skip proofs from the pinned report digest and skip row.

This is a staged validator projection, not a C replay of the #508 producer.
It does not inspect/replay every shard's object, result, argv, environment,
supported-gap ledger contents, or the residual TSV bytes. For full-census it
checks the report's supported-gap row list against the approved count and
canonical digest, ties the report ledger digest to the manifest and approved
ledger digest, and requires every listed row to remain admitted supported
object work. The `self-test` profile must declare no supported gaps. Since no
held residual-file descriptor is part of this service boundary, the projection
accepts only the canonical zero-row residual summary: matching evidence-path
aliases, zero rows, no truncation, the fixed retention limit, and the SHA-256
of the header-only TSV. It does not read that TSV itself. The real schema-2
fixture runs the repository validator and Python replay before exercising the
C projection; it proves importer mechanics only. The low-level C probe accepts
the `self-test` profile for this purpose. The production service entry also
requires `profile=full-census`, but still returns a fail-closed error after a
valid projection because the remaining B/#509 authorities are not joined.

The projection checks the complete fixed target, frontend, PIC, and allocator
matrix; zero-based row and group order; fixture path, recipe, and compile
obligation against the authenticated source ledger; and the approved #508
canonical JSON identity digest for each row. The staged service entry checks
projection ordinals against the supplied B rows, but does not import the
matched build or begin the correctness gate. The lower-level `_built_pinned`
fixture seam exercises matched-build import separately. The `inputs.tsv` projection joins
every line to the pinned support declaration, validates byte count, SHA-256,
role, and compile obligation, and checks exact path-based `fixture_recipe` and
`fixture_flags` values from #508's approved recipe table. Each held file must
remain a read-only, close-on-exec, single-link regular file owned by root or
the effective user, with no write bits. `rows.tsv` is capped at 64 MiB and
`inputs.tsv` at 1 MiB.

The `buster_hash_64` value is checked as a decimal and covered by the input
file digest, but is not recomputed from source bytes in this importer slice.
The byte count and SHA-256 values are joined to the independently pinned
support declaration.

The expected raw input and row digests come from separate compiled profile
keys, `census-inputs-sha256` and `census-rows-sha256`. The digest in
`BqRetirementPrepared` is checked against the row pin and is never treated as
authority. The raw files are independently hashed and their stable inodes are
checked while read. The same compiled profile independently pins the support
declaration used to derive the subject population, source digests, and compile
obligations.

The production profile
`profiles/native-retirement-performance-v1.blocked` currently lacks the raw
input/row pins and all validator manifest/report/applicability/skip and source
applicability-ledger pins, so the production entry fails closed before build
import. The current census attempt used the #923 test-merge tree and failed its
candidate gate, so it does not establish current source-head pins. No
current-source full census artifact has established these exact pins. The checked-in support
declaration currently contains 411 subjects, which project to 78,912 object
rows at 192 rows per subject.

The production service entry reimports the durable A preparation, fixed
matched-build and frozen-binary records, then checks their source and binary
identities against the supplied B declaration. It checks projection ordinals
against the supplied B rows but does not overwrite them, acquire launchable
binary descriptors, or call the correctness-gate begin function.
Even a valid full-census projection returns `BQ_RECIPE_MISMATCH` until the
separate authority joins are implemented. Missing facts include an approved
#508 per-row `configuration_sha256` serializer, independent verification of
per-row compiler/runtime argv/cwd/environment and CPU provenance, an
authenticated #509 required-check list and receipt bytes/digests, and
independent-oracle bytes/digest from an admitted producer. The gate can validate
that a caller's check digest matches its caller-supplied expected digest; this
import does not establish where that expectation or receipt came from. It also
does not prove clean acceptance or replay the complete validator. The existing
worker has no B-to-timed-launch caller, so this projection does not prove a
campaign can start or that zero timed launches occurred. The production recipe
remains blocked; do not treat the staged projection as full authority.

`retirement_validator_eligibility_test.py` generates genuine #508 schema-2
validator output from a miniature source fixture, runs the Python evidence
checks/replay, then invokes the C probe with runtime-computed pins. Its positive
case includes both unavailable and platform-inapplicable rows; re-pinned
tamper cases cover failure arrays, acceptance/unavailable equality,
`clean_acceptance`, absent-ledger classification, supported-gap and residual
summaries, class-map membership, skip membership/sidecar data, and required
pins. It does not grant production authority. The separate private fixture in
`retirement_prepare_tests.c` continues to test the raw support/input/row join.
