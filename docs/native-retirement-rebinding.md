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
Dispatch it with an open non-draft PR number. The default `auto` class and
`configured` authorization mode resolve the immutable request from trusted main.
Optional SHA and class overrides remain strict assertions.

For a previously attested head, preparation first verifies its original trusted
publication and recovers the original source candidate. It requires the recorded
base to be an ancestor of current main, a still-valid successful writer attempt,
and a clean source merge. The source is reclassified against current main and
the normal authorization policy is applied to the live PR head. All three jobs
reconstruct from that source and current main; the publication lease targets the
live PR head. The replacement integration has current main and original source
as parents, so generated commits can be replaced without losing source history.
Manual generated edits, edits stacked on unrecognized integration output and
genuine conflicts remain blocked. A fresh dispatch is still required after main
advances; this recovery does not grant automated dispatcher authority.

Merge-group admission is read-only: GitHub's single-candidate synthetic commit
must have current main first, the attested integration head second, and exactly
the attested final tree. It resolves live publication evidence for that PR head,
including the successful latest writer attempt. Combined-head CI remains required
on the synthetic SHA. Rebinding checks that existing generated pair in place;
it does not refresh it into a different, untested group tree.

The workflow has three separately permissioned jobs:

- **prepare** is read-only. It resolves current `main`, authorizes the immutable
  PR head, constructs `main + candidate`, runs only current-main trusted tools,
  and records deterministic evidence.
- **validate** independently reconstructs the same final tree with read-only
  permissions, proves byte equality with prepare, and only then executes the
  candidate's affected tests and census self-test.
- **publish** is the sole `contents: write` job. It independently reconstructs
  and compares the same tree but never executes candidate code. It reauthorizes
  the PR immediately before a leased update of its same-repository head branch.
  The two-parent commit contains current main, the candidate, and generated
  state. A temporary branch makes the commit available for the bot-authored
  `Native retirement trusted integration` status before the PR head moves.
  Publication leaves main unchanged; normal merge admission follows.

The required `Native retirement merge admission` check refuses ordinary merge
for a candidate that changes an admitted repository source, trusted implementation,
or reviewed policy/schema. It admits the writer-produced head only after
checking the exact parents, evidence trailers, generated-only post-candidate
delta, current-main identity, and GitHub Actions attestation. A main-push run
invalidates every open integration head whose recorded base no longer equals
current `main`; dispatching the writer again reconstructs it without requiring
a manual feature-branch rebase. The independent `API migration policy` check
continues to enforce bounded API compatibility. Configure admission as a
required GitHub Actions check (integration ID `15368`) without enabling strict
required-status-check policy or removing any existing required check.

Evidence records base/head commits and trees, the pre-generation combined tree,
the final tree, the old trusted rebinder revision/tree and file digests, the
exact next-trusted file digests in the final tree, both generated artifact
digests, and the policy/receipt/project/ledger identities. The integration
commit records the evidence digest, base, candidate, transition kind, and final
tree.

The writer rechecks both `main` and the PR head immediately before publication.
A moved PR head fails closed. A moved `main` discards the prepared result and
fails closed before any branch update. A maintainer/admin must start a fresh
trusted dispatch, which reconstructs from the new current `main`; no previous
snapshot or quartet is reused. The workflow deliberately has no
`actions: write` permission, so stale recovery cannot silently continue under
the identity of `github-actions[bot]`. Publication is one leased ref update, so
a failure or cancellation cannot leave a partially published generated state
on the PR head. A failure after staging can leave a temporary
`native-retirement-staging/` branch; it does not authorize another commit.

Configure `NATIVE_RETIREMENT_PUBLICATION_TOKEN` as an Actions secret in the
protected `native-retirement-integration` environment to trigger PR CI from
the publication push automatically. Use a fine-grained personal access token
restricted to this repository with Contents read/write and Workflows read/write
(for candidates changing workflow files). Give it an expiry and rotate it.
Never paste the token into a PR, workflow input, log, or chat.

The secret is exposed only to the final publication step, after independent
read-only validation and reauthorization. The attestation API uses the built-in
GitHub Actions token so the admission gate still verifies the bot creator.
The publication credential does not grant permission to skip required checks.

Without the secret, publication remains compatible with the built-in token,
but emits a warning: approve the new PR workflow runs in GitHub. GitHub may
require this additional approval for PR updates using its built-in token.
See [GitHub workflow triggering](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow). Verify checks belong
to the published SHA before merging; old-head checks are not evidence for it.

## Trust transitions

Candidates are classified before any privileged operation:

- `ordinary` changes neither authority implementation nor policy/schema;
- `bootstrap` changes trusted rebinder/materializer/validator/workflow code but
  not policy, generated schema, or consumers;
- `policy` changes reviewed policy/schema/consumer state but not the trusted
  implementation; and
- a candidate that changes both implementation and policy/schema is rejected
  and must be split.

By default, `bootstrap` and `policy` dispatches require a maintainer/admin
dispatcher and an approval from a maintainer/admin other than the PR author
on the exact candidate head. An explicitly configured solo-maintainer repository
can instead use the admin-dispatch authorization described below. The publisher always executes the
implementation from the old/current trusted `main`, never the candidate's
version. In the read-only validation job, the candidate authority must then
accept the exact generated state produced by that old authority for the final
tree. A bootstrap that would immediately make `main` stale therefore fails
before publication. A schema change that the old implementation cannot
understand must land as a backwards-compatible bootstrap first, then as a
separate policy transition after that bootstrap is trusted.

For #935, the first bootstrap admits exactly the existing support declaration
digest and the digest of the proposed aggregate-test ledger update in the
materializer, full-census validator, and performance binding. It classifies the
support ledger as reviewed policy while leaving that ledger and the frozen test
bytes intact. Once trusted, a separate policy transition may change the test,
its exact byte/hash ledger row, and the benchmark-service profile pins. Old
census/performance evidence remains bound to its original declaration digest;
the matching manifest and exact declaration bytes are checked together.

For #1007, the current support declaration digest remains accepted for
historical evidence. The trusted-reader bootstrap admits the exact proposed
successor digest `0d878bf0a3df9f0528803a5b08275d950f9edda57e373fd7618229dee264e427` alongside both prior digests. After that
bootstrap is trusted, a separate policy transition may update only the
`tests/basic_c_f80_machine.c` byte/hash row and benchmark-service support pins;
all 559 inputs, 411 subjects, and 78,912 row identities remain fixed.

### Solo-maintainer authorization

`authorization_mode: solo-maintainer` is explicit owner authorization of one
bootstrap or policy transition. It is recorded separately from independent
review; `maintainer_approvals` remains empty. The CLI default remains `independent-review`. The workflow default `configured`
selects solo mode only for a trust transition dispatched by the explicitly
configured account; all other dispatchers retain independent-review policy.
Missing reviews never select the solo route.
Ordinary integrations retain their existing dispatcher authorization.

After this implementation is installed on trusted `main`, configure the
repository Actions variable `NATIVE_RETIREMENT_SOLO_MAINTAINER` with the exact
GitHub login of the solo maintainer (`davidgmbb` for this repository). That
account must currently have the `admin` role. A candidate file cannot set this
configuration. The variable is captured by each workflow run; removing it
disables future dispatches, so cancel any already-running solo dispatch when
revoking the configuration. Current admin permission is checked again before
publication.

To authorize an integration, open **Actions -> Native retirement trusted
integration -> Run workflow**, select `main`, enter the PR number, and run.
Leave `expected_head` and `expected_base` blank, `transition_kind` at `auto`,
and `authorization_mode` at `configured`. No SHA copying is needed.

The run checks out its immutable workflow revision, resolves the current PR
head once, classifies it with trusted tools, records both SHAs/class/mode in
the job summary, and reuses them across validation and publication. If you
need to approve a specific head rather than the current head when preparation
starts, fill the optional exact-head override. Explicit overrides must match.
The configured account must still have current admin permission, use attempt 1,
and pass the existing fresh-dispatch checks. Main/head movement fails closed;
start a new run for the changed request. No background process renews approval.

This manual dispatch is the approval action. It must originate from the
configured admin account, use the default-branch writer at the approved base,
and be attempt 1 of a fresh workflow run. Re-running jobs is not fresh approval:
start a new dispatch after a failure or cancellation. If either main or the
candidate changes, inspect the new revisions and dispatch again. Approval is
checked during preparation and immediately before publication. Both resulting
`authorization.json` records are retained alongside the generated-tree evidence
and contain the PR/head, base, actor, transition, workflow revision and run ID.

Solo authorization does not skip the old-trusted-tool reconstruction, separate
read-only candidate validation, second-refresh equality check, protected writer
environment, generated-file ownership rules, or leased publication. A writer
environment configured to require another person's approval is still blocking
for a solo maintainer; this option does not bypass environment protection.

**Installation is a separate policy change.** A PR introducing this mode cannot
authorize its own privileged execution. Review and install this source-only
prerequisite through the repository's existing permitted change process before
setting the variable or dispatching solo mode. Do not run the candidate workflow
with write credentials or manufacture a successful admission status. This
prerequisite does not install #927's PR-head publisher or attest #927; that
publisher transition remains separate work after the authorization policy is
trusted.

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

The required admission gate is selective to retirement-sensitive PRs. General
multi-PR serialization remains #867, and conflict preflight remains #869.

### PR-head publisher bootstrap for #927

Install the publisher-only prerequisite on main before dispatching retirement
integration for #927. This prerequisite changes publication and its status
permission without installing the new merge-admission gate or changing
generated artifacts. Once installed, update #927 against current main and
start a fresh authorized dispatch for its exact head and base. A successful
run attests the new PR head; it does not itself merge the PR.

### Merge-conflict preflight and generated output

Preflight keeps reporting generated paths, but a live GitHub PR check accepts
those paths only when the trusted retirement verifier validates the exact head:
current-main first parent, candidate second parent, matching tree/evidence
trailers, no manual generated edits in the source candidate, generated-only
post-candidate changes, and the bot-authored evidence status. It loads the
verifier from the trusted checkout, never the candidate. Missing, stale, or
invalid attestation retains the generated-ownership failure; merge conflicts
remain blocking. Offline checks without live status evidence stay conservative.

On `merge_group`, the synthetic queue SHA is not treated as the published PR
head. When its tree contains generated changes, preflight delegates to the
trusted merge-group verifier. That path checks current `main` as the first
parent, the exact attested PR head as the second parent, the conflict-free
combined tree, the latest successful writer attempt, and equality with the
writer's final tree. Wrong parents, altered trees, stale or failed publication,
and generated-only candidates remain blocked.

Install this compatibility bootstrap on main before relying on the exception.
Landing it advances main, so existing attested heads need fresh trusted
integration. Do not hand-edit generated artifacts or post replacement statuses.
