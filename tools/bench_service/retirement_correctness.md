# Private correctness gate handoff (#1020)

`retirement_correctness.{h,c}` is the private A→B→D handoff for the
`native-retirement-performance-v1` recipe. It has no request-facing fields and
does not change the blocked recipe descriptor. The importer and the production
caller have not yet been wired to this child branch.

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

The required-check array must enumerate every applicable #509 native semantic
lane, supported configuration matrix, no-fallback/census validation, self-host
and fixed-point gate. The service executes or independently validates each
exact-source and exact-binary check, then verifies the receipt bytes against a
separate trusted digest before passing its status to `check`. A digest obtained
from the candidate or downloaded result is not a trusted receipt. Any required
host unavailable at qualification leaves the campaign blocked.

Before `row`, the service compiles each eligible cell with both matched trusted
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
   digests and all row facts before lane D freezes its own command/host plan and
   starts the first timed invocation. A later mutation removes readiness.

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
