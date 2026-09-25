# Private correctness gate handoff (#1020)

`retirement_correctness.{h,c}` is the private A→B→D handoff for the
`native-retirement-performance-v1` recipe. It has no request-facing fields and
does not change the blocked recipe descriptor. The service-side validator
projection is wired as a fail-closed check, but the production worker has no
caller for a ready correctness gate or timed launch.

The lower-level `_built_pinned` seams read same-attempt preparation,
matched-build receipts and binary records through service-held descriptors.
The public `begin_service` performs only a staged eligibility projection and
stops before matched-build import. That projection separately pins #508 support, inputs, rows,
manifest, the source applicability ledger, schema-2 report, and applicability
and skip sidecars. It recomputes the raw identities, checks the complete fixed
matrix and report class-row partition, and requires the exact source-derived
non-executed set. For full-census it requires the approved source applicability
ledger digest/count and exactly four shards. It then checks projection object
ordinals against the supplied B row inventory. It derives classes, reasons, and
ownership for default rows missing from the source applicability ledger using
the raw row obligations, allocator role, and retained direct-reference failure
set; it does not replay baseline shard results or independent supplements. It
also fails closed on candidate and final reference failure rows, checks that
acceptance failures match unresolved `unavailable` rows, checks
`clean_acceptance`, requires both acceptance flags for full-census, and rejects
nonempty defect arrays. A report with retained
direct-reference failures and four supplement digests remains only a staged
projection; independent supplemental proof and the separate #508 binding are
required before admission.

This importer is not a C replay of every #508 shard, result, argv, environment,
supported-gap ledger contents, residual TSV bytes, or report field. It checks
the supported-gap report list against the approved full-census count/digest
and permits no gaps in the self-test profile. The current hosted full-census
validator uses four reference supplements. The projection checks their digest
syntax and count but cannot grant their correctness authority until the
service reopens and replays independent supplement evidence. With no held
residual-file descriptor in this service boundary, it permits only the canonical zero-row,
untruncated residual summary and matching header-only digest; it does not read
the residual TSV. It also does not authenticate per-row configuration
identities, compiler/runtime command plans, #509
required-check receipt bytes/digests, or independent-oracle bytes. A valid
projection therefore returns fail-closed `BQ_RECIPE_MISMATCH` before the
correctness gate begins. Before the validator projection, the production entry
reimports the durable A preparation and fixed matched-build/binary records and
compares their identities to B's declaration. The checked-in production profile
lacks the new validator pins and fails earlier. The low-level fixture probe tests projection
mechanics only; it does not authorize readiness or a timed launch.
The separate `_built_pinned` fixture seam can import matched-build evidence and
acquire held binaries for lower-level tests. The production `begin_service`
entry intentionally does not call it after projection: independent
configuration, command, #509 receipt, and oracle authority must be supplied
before that handoff can be enabled. The existing pinned service test seam uses
a synthetic one-row B declaration after four real miniature host-compiled
process stages and readback. Neither fixture provides a trusted Clang build,
full population, oracle receipt, or timed invocation.

`retirement_artifact_service.{h,c}` provides the B-side artifact observation
around each service-owned compiler process and before `row`. Before launch,
`bq_retirement_artifact_start` records that the fixed output name is absent
inside the held service-owned directory without other-user write access. The
private launcher rechecks absence, executes the exact argv/cwd/environment
plan, and nonblocking poll observes that child's wait result. Only after exit
zero does the wrapper require the same name and directory identity, consume
the start, and open each frozen, read-only, single-link file by a
single-component name under the same service directory. It streams bytes
through a descriptor, parses the actual object or executable with the existing
independent ELF/COFF/PE/Mach-O reader, and checks name, inode, mode, size and
timestamps again before using the parsed file and
code-section digests. `bq_retirement_correctness_row_service` derives
`code_eligible` from the observed baseline section size and passes the readback
to the existing gate. Format, machine and object/executable status derive from
the authenticated row's target and stage (the twelve target IDs follow the
performance contract's `TARGETS` order); mismatches fail. It also rejects
supplied artifact/code values and artifact names for untimed controls. The
producer must derive both output names from the frozen command plan and freeze
files under the service UID before readback, then hold and recheck them through
later timed launches. The recorded child wait does not alone prove that the
compiler created the named file or that an independent semantic oracle passed;
those still require service-owned output/receipt and #509 execution.

For native link/self-host rows, the same wrapper requires completed, read-only,
service-owned stdout/stderr log descriptors from both runtime processes. It
creates each empty log exclusively before launch. The private runtime launch
validates the exact argv, cwd and environment, forks the child with both output
streams on that writer, and records the command digest. Nonblocking poll reaps
that child and records its actual wait result; a nonzero exit or signal prevents
freezing. The worker supplies the deadline and whole-job cancellation. At row
readback the wrapper checks the frozen log's original inode and the launched
command digest before accepting its output bytes. It hashes the exact
bytes, including an empty output, checks CLOEXEC, ownership, single-link
status, access mode, size and stable metadata, and rejects caller-filled
output digests. The existing gate compares these observed hashes
to the independently imported oracle digest. Object-only, foreign-target and
untimed rows require absent runtime descriptors. The runner must capture both
streams in the oracle's declared order and obtain the independent oracle
from the admitted path. Runtime launch and poll alone do not qualify a correctness row or a timed invocation.

The row wrapper also derives both compiler command hashes and applicable
runtime command hashes from exact argv, cwd and explicit sorted environment
through the same bounded canonical serializer used by the timed measurement
lane. It requires those digest slots empty on entry and rejects supplied
commands for untimed or inapplicable processes. The private launch and poll helpers execute the exact plans and capture
actual child wait results for each side. The service runner still owns
deadlines, cancellation, complete semantic receipts and the independent oracle.

## Trusted inputs

The eventual production caller must verify the complete #1018 preparation and
independently replay the #508/#929 support, raw census, validator report, and
sparse eligibility projection. The current C code checks only the staged raw
identity and source-ledger projection described above. It does not yet derive
or authenticate each row's configuration, compiler/runtime commands, #509
receipts, or oracle from an admitted source. A `retained-control` label alone
does not exclude a row. No candidate output, manifest, or request may select
these missing authority values.

The production gate must take population counts from the independently replayed
current support/census authority. It checks the complete object join, the
eligible count in the required matrix and no-fallback checks, and the bounded
row array; historical dimensions do not authorize a population. The importer
must prove that the prepared counts equal its independently replayed inventory.
The miniature and synthetic-capacity fixtures do not establish that proof.

The required-check array must enumerate every applicable #509 native semantic
lane, supported configuration matrix, no-fallback/census validation, self-host
and fixed-point gate. The gate checks equality with the expected digest supplied
in each required-check record and seals both expected and observed values, but
the current service importer does not authenticate the required-check list or
receipt bytes/digest against a separate authority. A digest obtained from the
candidate or downloaded result is not a trusted receipt. Any required host
unavailable at qualification leaves the campaign blocked.

Before `row`, the service derives exact compiler and applicable runtime command
hashes (argv, cwd and environment) independently from the admitted oracle
path. The gate binds those hashes for both binary sides to observed commands;
a changed command fails before timing. The service compiles each eligible cell with both matched trusted
Clang-built binaries from #1018, checks semantic and generated-output evidence,
parses actual artifact code sections, and runs native link/self-host outputs
through an independent oracle. It obtains the expected output digest from the
admitted oracle path, never from the candidate. A foreign target keeps its
compiler and code facts but has no local runtime invocation. A zero-byte code
section remains zero; a zero baseline has no code ratio denominator.

The second A/A baseline label can have a distinct output path. Before `begin`,
the importer also authenticates its compiler and applicable runtime commands
from the frozen service plan and commits their aggregate SHA-256 in
`aa_second_commands_sha256`. The byte stream starts with the ASCII domain
`bq-retirement-aa-second-commands-v1` without a terminator. For each eligible
row in canonical order it appends its four-byte little-endian row ID, the 64
ASCII lowercase hex bytes of the second A/A compiler command digest, one byte
for runtime applicability (zero or one), and, when applicable, the 64 ASCII
lowercase hex bytes of the second A/A runtime command digest. The gate seals
this commitment and the A→D binder compares it before freezing any campaign.
The aggregate has no authority on its own: the service importer must verify
the frozen plan independently of candidate or request supplied values.

## Call sequence and ownership

1. Import and authenticate the whole immutable preparation, population,
   requirement list, and oracle closure, then allocate `rows` output facts,
   `check_count` receipt facts and the scratch arrays above.
2. Call `begin`, followed by every required `check` in order. No row or timed
   invocation may run while a check is missing or failed.
3. Call `row` once per canonical row, in increasing `row` order, with genuine
   observations or an explicit empty untimed fact. Failure poisons the gate.
4. Call `finish` once. `ready` rehashes the preparation, declarations, receipt
   digests and all row facts. It is a structural check, not authorization to
   launch: the service must authenticate all underlying producers, compare
   the same identities across lane D's frozen commands and gate its first timed
   invocation. A later mutation removes structural readiness.

`retirement_campaign_binding.h` contains the tested held-binary binding seam.
`bq_retirement_campaign_bind_held` expects the upstream service to have already
reimported the durable A and binary records and acquired their read-only files
with `bq_retirement_binaries_acquire`. It compares the held record's
preparation, source and binary digests to the ready gate, recomputes each
held-descriptor identity, and initializes the campaign executables from those
descriptors. It derives the private transcript label `job-<id>` from its
numeric job-ID argument and checks that label and the supplied attempt token
against both A/A and A/B transcripts. The caller must obtain that ID and token
from the active authenticated queue job; this lower-level seam does not read
queue state or reimport records itself.

`bq_retirement_campaign_run` rejects bindings without held service descriptors
and rechecks the held record, gate join and transcript identity before launch.
It checks the gate seal on each call and rehashes all gate facts before the
first A/A and first A/B child. Held descriptor records must remain live and
immutable for the campaign, and the production measurement path must launch
the exact held descriptors and check their identities around each child. A
failed binding check poisons the campaign and both sample streams before
launch. A queue-aware service wrapper that reimports same-attempt A and binary
records and rechecks queue ownership before each launch remains a producer
integration step; the production worker has no caller for this seam.
Rehashing all 78,912 object rows before each of millions of child launches is
deliberately avoided.

The D handoff only checks `bq_retirement_correctness_ready` and joins its
structural facts to held binary readback. This branch does not authenticate the
B producer's raw census rows or establish complete-population authority; that
producer evidence is not integrated here. The recipe remains blocked.

The focused real-child fixture exercises descriptor-backed binding and checks
same-source/same-binary cross-job, cross-attempt, and changed-source rejection
before any launch. Its held-binary record is synthetic; it does not test durable
queue reimport, queue ownership checks, or trusted Clang output. The production
worker has no caller for this seam, and no full-corpus or performance claim
follows from these tests.

`sealed_sha256` is a private in-process integrity check, not publication,
service authority, or a performance verdict. The integration owner must make
the trusted importer, execution and receipt readback concrete, persist the
receipts through lane E, and serialize the shared recipe, build driver, test
registration, and workflow wiring. Until that integration runs against real
#1018 outputs and the complete #509 evidence, these miniature tests establish
only the local fail-closed behavior.

The standalone fixture can be run without a configured build tree:

```sh
cc -std=c11 -Isrc -Wall -Wextra -Wpedantic -Werror -fwrapv \
  -fno-strict-aliasing -funsigned-char \
  tools/bench_service/retirement_correctness_tests.c -o /tmp/retirement-correctness-test
/tmp/retirement-correctness-test
```

The focused fixture records vacant output names before two actual compiler
children copy the compiled executable into frozen service files for link-row
readback. It polls their waits, launches two runtime children with exact
explicit plans and freezes their service-owned logs after observed exits.
Unlaunched compiler artifacts or logs, nonzero exits and changed command
identities fail. The fixture also rejects preexisting outputs, symlink/path
and finished-log substitution, mismatched starts, predeclared code facts,
writable outputs, wrong bytes, missing descriptors, caller-filled digests,
mutable logs and changed argv/cwd/environment.
The other synthetic rows remain structural tests, not a full-corpus pass.
