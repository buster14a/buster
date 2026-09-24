# Private correctness gate handoff (#1020)

`retirement_correctness.{h,c}` is the private A→B→D handoff for the
`native-retirement-performance-v1` recipe. It has no request-facing fields and
does not change the blocked recipe descriptor. The importer and the production
caller have not yet been wired to this child branch.

`retirement_correctness_service.{h,c}` provides the private service entry
point for the first A→B join. Its public entry now imports the same-attempt
preparation, matched-build receipts and binary record using service-held
installed/workspace descriptors and authenticated record digests. It opens
both verified frozen binaries, checks
the independently supplied B declaration's preparation, source and binary
digests against that readback, and checks its support declaration against the
compiled profile pin. Only then does it call `bq_retirement_correctness_begin`.
Failure poisons a fresh gate and releases any descriptors acquired by this
call; an already live holder is left alone. A successful holder stays open
through all correctness work and subsequent dependent launches and must be
released by the caller. The #923 integrator must compile this private module
after the A and B implementations and call the public entry point from the
actual service producer. Its pinned test seam also checks the B row join after
four real miniature host-compiled process stages and readback; the one-row B
declaration remains synthetic. Neither fixture provides a trusted Clang build,
full population, oracle receipt or timed invocation.

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

The service must first verify the complete #1018 preparation and independently
replay the #508/#929 support, census, validator-report and sparse eligibility
projection. It supplies a contiguous canonical row array, including every
untimed control and inapplicable row, and derives `object_rows` from that
independently checked inventory. The gate uses caller-provided identity hash
slots (at least `2 * rows + 1`) and a census bitmap (`object_rows` bytes) to
reject duplicate identities or missing/duplicate object census rows. A
`retained-control` label alone does not exclude a row. The importer derives
each skip proof, source, configuration,
target, stage and execution obligation from those authenticated inputs. No
candidate output, manifest or request may select these values.

The production gate takes population counts from the independently replayed
current support/census authority. It checks the complete object join, the
eligible count in the required matrix and no-fallback checks, and the bounded
row array; historical dimensions do not authorize a population. The importer
must prove that the prepared counts equal its independently replayed inventory.
The miniature and synthetic-capacity fixtures do not establish that proof.

The required-check array must enumerate every applicable #509 native semantic
lane, supported configuration matrix, no-fallback/census validation, self-host
and fixed-point gate. The service executes or independently validates each
exact-source and exact-binary check, then verifies the receipt bytes against a
separate trusted digest before passing its status to `check`. Each required
check carries that independently established digest; `check` requires equality
and the readiness seal covers both the expected and observed values. A digest obtained
from the candidate or downloaded result is not a trusted receipt. Any required
host unavailable at qualification leaves the campaign blocked.

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
