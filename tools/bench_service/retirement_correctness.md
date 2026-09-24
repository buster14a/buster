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
