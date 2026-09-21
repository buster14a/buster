# Native-retirement dependency rebinding

Native-retirement dependency state has three owners. They are intentionally
separate so ordinary source changes do not become editors of reviewed trust
policy or concurrent writers of generated identities.

## Authorities

1. `docs/native-retirement-dependencies-v1.json` is reviewed policy. It owns
   inventory, destinations, provenance, external checkout revisions,
   generated-external rules, SDK/resource pins, archived replay, and every
   other non-derived declaration. Eligible `repo:<source>` project records do
   **not** contain `bytes` or `sha256`.
2. `docs/native-retirement-repository-sources-v1.json` is generated state. It
   contains exactly one sorted `{source, bytes, sha256}` record for every
   eligible repository-owned project source and binds those records to the
   exact policy bytes.
3. `tools/native_retirement_dependency_binding.generated.h` is the single
   generated aggregate authority. It binds the policy and source snapshot to
   the materializer receipt, project closure, and dependency ledger. The C
   census includes it directly; Python parses the same canonical file.

`docs/native-retirement-dependencies-legacy-v1.json` is an immutable
compatibility input. It preserves the monolithic descriptor used by archived
#508/#510 evidence. Rebinding never writes it. The live materializer
reconstructs that v1 projection from policy plus snapshot and independently
checks it before publication.

## Commands

From a checkout containing all pinned SDK/external inputs:

```sh
python3 tools/native_retirement_rebind.py check --repo-root "$(pwd)"
python3 tools/native_retirement_rebind.py refresh --repo-root "$(pwd)"
```

`check` is read-only and exits 2 when generated state is stale. `refresh` may
replace only the generated source snapshot and aggregate header. A second
refresh is byte-identical. Both commands reject policy drift, malformed or
incomplete snapshots, duplicate records, external/SDK changes presented as
repository refreshes, source TOCTOU, and a quartet that does not match an
independent materialization.

`refresh` is an implementation primitive, not feature-branch ownership.
Ordinary feature PRs must not commit its two outputs. The read-only
`Native retirement rebinding` workflow runs the previously trusted rebinder
against an ephemeral exact candidate/merge checkout, then runs consumers
against that temporary result.

External-input preparation runs before that reconstruction. For split policy,
it resolves admitted repository-source identities in memory from the exact
candidate, then verifies the complete resolved closure. It does not write the
snapshot/header or refresh external/SDK pins. The trusted rebinder subsequently
publishes the temporary generated pair and checks it independently. The legacy
monolithic descriptor retains exact byte verification against its frozen pins.

The census evidence workflow follows this same order before compiling its build
driver or materializing dependencies. Pull requests classify the exact head
using the base revision's trusted implementation, prepare the pinned inputs,
then reconstruct and check the temporary authority for the integration tree.
The separate validation checkout receives that exact generated pair so its
Python consumers and the census C consumer agree. Manual evidence runs check
published state without refreshing it. Reconstruction receipts remain in the
uploaded evidence; no generated identity is committed by this workflow.

## Ordinary feature PR ownership

An ordinary feature PR changes substantive source, tests, and documentation
only. CI classifies its diff before reconstruction and rejects either generated
artifact when it appears in the PR. It then:

1. resolves the exact current base and immutable candidate identities;
2. uses rebinding/materialization code from the previously trusted base, not
   candidate-modified authority code;
3. reconstructs the complete pinned closure in a disposable checkout;
4. runs the rebind, materializer, contract, and source-level consumers against
   the ephemeral snapshot/header; and
5. records the generated identities without mutating or pushing the feature
   branch.

Two sibling feature PRs therefore share no generated-file diff merely because
their admitted source bytes differ. Both can validate independently; the
single writer regenerates the second from the tree containing the first after
the first lands.

## Trusted integration writer

`.github/workflows/native-retirement-integration.yml` is the only publication
path. It is a default-branch `workflow_dispatch` workflow with one fixed,
non-cancelling concurrency group. Protect the
`native-retirement-integration` environment with the intended reviewers.
Dispatch it with an open non-draft PR number, its exact reviewed 40-hex head,
and the trusted transition class.

The workflow has three separately permissioned jobs:

- **prepare** is read-only. It resolves current `main`, authorizes the immutable
  PR head, constructs `main + candidate`, runs only current-main trusted tools,
  and records deterministic evidence.
- **validate** independently reconstructs the same final tree with read-only
  permissions, proves byte equality with prepare, and only then executes the
  candidate's affected tests and census self-test.
- **publish** is the sole `contents: write` job. It independently reconstructs
  and compares the same tree but never executes candidate code. It reauthorizes
  the PR immediately before one `--force-with-lease` ref update whose merge
  commit contains both the candidate and the generated state.

Evidence records base/head commits and trees, the pre-generation combined tree,
the final tree, the old trusted rebinder revision/tree and file digests, the
exact next-trusted file digests in the final tree, both generated artifact
digests, and the policy/receipt/project/ledger identities. The merge commit
records the evidence digest, base, candidate, and final tree.

The writer rechecks both `main` and the PR head immediately before publication.
A moved PR head fails closed. A moved `main` discards the prepared result and
fails closed before any ref update. A maintainer/admin must start a fresh
trusted dispatch, which reconstructs from the new current `main`; no previous
snapshot or quartet is reused. The workflow deliberately has no
`actions: write` permission, so stale recovery cannot silently continue under
the identity of `github-actions[bot]`. Publication is one leased ref update, so
a failure or cancellation cannot leave a partially published generated state.

## Trust transitions

Candidates are classified before any privileged operation:

- `ordinary` changes neither authority implementation nor policy/schema;
- `bootstrap` changes trusted rebinder/materializer/validator/workflow code but
  not policy, generated schema, or consumers;
- `policy` changes reviewed policy/schema/consumer state but not the trusted
  implementation; and
- a candidate that changes both implementation and policy/schema is rejected
  and must be split.

`bootstrap` and `policy` dispatches require a maintainer/admin dispatcher and an
approval from a different maintainer/admin. The publisher always executes the
implementation from the old/current trusted `main`, never the candidate's
version. In the read-only validation job, the candidate authority must then
accept the exact generated state produced by that old authority for the final
tree. A bootstrap that would immediately make `main` stale therefore fails
before publication. A schema change that the old implementation cannot
understand must land as a backwards-compatible bootstrap first, then as a
separate policy transition after that bootstrap is trusted.

Generated files are never an escape hatch: manual edits to either generated
artifact fail for all classes. External/SDK pins remain reviewed policy and
cannot be smuggled through ordinary repository-source rebinding.

The ownership-cutover PR itself is the one-time reviewed bootstrap that installs
both the ephemeral feature validation and the trusted writer atomically. It
does not alter either generated artifact or an admitted source identity, so
normal PR admission can land the complete cutover without an interval in which
feature branches relinquish ownership before the writer exists. Every later
publication uses the trusted default-branch path above.

## Evidence compatibility

New evidence carries copies of the reviewed policy, generated source snapshot,
and resolved v1 descriptor. The validator reconstructs the resolved descriptor,
recomputes the materializer ledger, and cross-checks the receipt/project closure
rather than trusting producer output. Evidence without the generated snapshot
is treated as legacy only when all frozen previous identities match exactly.

This ownership cutover does not enable a GitHub merge queue/ruleset (#867) and
does not implement the general conflict-preflight mechanism (#869).
