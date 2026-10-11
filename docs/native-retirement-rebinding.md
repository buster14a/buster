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
their admitted source bytes differ. Both can validate independently. An
ordinary PR that changes admitted sources ("ordinary-bound") needs no writer
step: it lands through the native merge queue, whose admission requires the
same ephemeral reconstruction on the exact group tree. An automatic catch-up
then publishes the regenerated pair for `main` (see
[Committed generated state and queue throughput](#committed-generated-state-and-queue-throughput-1893)).

## Trusted integration writer

`.github/workflows/native-retirement-integration.yml` is the only publication
path. It is a default-branch `workflow_dispatch` workflow with one fixed,
non-cancelling concurrency group. Protect the
`native-retirement-integration` environment with the intended reviewers.
Dispatch it with an open non-draft PR number. The default `auto` class and
`configured` authorization mode resolve the immutable request from trusted main.
Optional SHA and class overrides remain strict assertions.
Resolution refuses a candidate before authorization or any branch update when
it is non-empty and changes no admitted repository source, trusted
implementation, or policy/schema path. It shares the merge gate's
`classification_is_bound` predicate over the current-main source snapshot,
because admission would reject such an integration head. Catch-ups (empty
candidates) and bound candidates remain eligible.

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

Pull-request admission (the `Native retirement merge admission` check and the
rebind job's policy step) evaluates against the trusted main checkout. If main
advances after that checkout, the step re-fetches the new main into the trusted
checkout and re-evaluates, up to three attempts in total, rather than failing
the candidate. It fails only if main keeps advancing throughout. The candidate
checkout is fetched first, so a push landing between the two checkouts leaves
the admitted main missing from it; the step then fetches that exact main commit
into the candidate before running the gate (#2010). Policy still comes only
from the trusted checkout.

Merge-group admission is read-only: a speculative base waits until it has landed
as current main, using the independently trusted main policy checked out at
workflow start. A queued predecessor that changes that policy requires a fresh
group. For a writer-published head, the synthetic commit must then have current
main first and the attested integration head second. Its tree must be exactly
the attested final tree, except for a catch-up (below). A stale non-catch-up
writer head still needs a fresh authorized dispatch and a replacement group.
The gate resolves live publication evidence for that PR head, including the
successful latest writer attempt. Combined-head CI remains required on the
synthetic SHA.

The read-only rebinding workflow reconstructs every group on its speculative
tree as soon as the group exists. It never writes the refreshed pair into the
group. Only then does it wait for the predecessor under independently
checked-out main policy and run the trusted gate. The wait (`wait-base`) proves
that no predecessor changed the admission or rebinding policy that the
reconstruction trusted. So only cheap checks follow a predecessor's landing.
Because the pinned closures already occupy `candidate/external` by then, the
job rejects candidate-controlled reserved roots on the pristine checkout for
every event, and the late group classification runs on a pristine worktree of
the exact head. The contract, rebind and integration jobs cache the pinned SDK
archives by manifest hash; each archive is still verified against its pinned
sha256 before any member is read, so the cache only skips the download.
An attested non-catch-up head must carry exactly current generated state. The
repository job has a 310-minute limit for the bounded five-hour wait.
`Main integration admission` requires that job's success on the exact group
SHA for ordinary-bound and writer-published groups.

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
for a candidate that changes trusted implementation or reviewed policy/schema.
It admits a candidate that changes only admitted repository sources as
`ordinary-bound`. It admits a writer-produced head only after checking the
exact parents, evidence trailers, generated-only post-candidate delta,
current-main identity and GitHub Actions attestation. A main-push run
invalidates every open integration head whose recorded base no longer equals
current `main`, except a catch-up that is still admissible. Dispatching the
writer again reconstructs it without requiring a manual feature-branch rebase. The independent `API migration policy` check
continues to enforce bounded API compatibility. The PR/main admission job lives
in `native-retirement-admission.yml`, which has no merge-group trigger; for a
merge-group head the trusted-main reconciler (`merge_queue_admission.py
reconcile`) publishes the same required check only after the exact base lands
and the native gate passes twice, so no runner waits for the predecessor.
Configure admission as a
required GitHub Actions check (integration ID `15368`) without enabling strict
required-status-check policy or removing any existing required check.
On PR events, both read-only admission jobs check out independent live `main`
and require that checkout to match the remote `main` before checking the writer's
recorded base. The PR event's base SHA may still name the commit that was main
when the PR opened. Merge groups keep their exact queued base SHA; native
admission stays pending in the reconciler, and the rebinding job still waits
in-job, until the predecessor lands before final admission.

Merge-conflict preflight also waits for the exact queued predecessor with the
trusted `merge_queue_admission.py wait-base` helper (bounded to 18,000 seconds
within its 310-minute merge-group job, matching the other admission callers). It compares the candidate against the event's exact
`base_sha` after that base becomes live main. This keeps a predecessor's
generated catch-up delta out of an ordinary successor's ownership diff. A
replaced group, timeout, or main movement fails without publishing a clean
status; generated-state attestation and conflict rules remain unchanged.

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
restricted to this repository with Contents read/write, Workflows read/write
(for candidates changing workflow files) and Pull requests read/write (to queue
a published catch-up). Give it an expiry and rotate it.
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

For #1007, the current support declaration digest
`50fb3d9a4ad147ffca5eb9187fec1850bae60a8025a94fbf33110d3005543210` and both
earlier declaration digests remain accepted for historical evidence. The
trusted-reader bootstrap admits the exact proposed successor digest
`a5bf7cb23b97874b7f4ff61f2bf0672892b4185a85043f4cdb539cc140d85932`
(`PROPOSED_SUPPORT_*`): the current declaration with only the
`tests/basic_c_f80_machine.c` row changed to 7,233 bytes and SHA-256
`3f5b829b9afa84528debbd00d726644834ff66e9885cac8207bdd5bd8e54d142`; the
declaration stays 79,744 bytes. The same bootstrap admits the successor
applicability ledger digest
`31c7aa79472b271db7ae39e8b9d96b99c49632f3d47908ac5ce12f1662a6a3c9` (65,467
bytes) next to the current
`934be981e866fe3dbbdb4a5b9e551c052b4546487bb04245fac24bb271be78fa` in the
full-census validator: the same 374 identities with only the four
`tests/basic_c_f80_machine.c` `fixture_sha256` cells updated. After that
bootstrap is trusted, a separate policy transition may update only that
fixture, its support byte/hash row, those four applicability cells, the
census producer's applicability-ledger pin, and the benchmark-service support
pins; all 559 inputs, 411 subjects, and 78,912 row identities remain fixed.

### Solo-maintainer authorization

For #1835, the trusted-reader bootstrap admits the exact successor support
declaration digest
`6d975980cc6df4945334fc2846dac8e03a1480a6c65e516db37be8adbccf1106` alongside
the #1808 predecessor. Only `tests/bootstrap_wrapper_test.py` changes: its
dependency-only row becomes 19,588 bytes with SHA-256
`e03036ea0a44e47f62bb743abaa7c5e381c6f76df3dfc75ba349da1a15506862`.
All 559 inputs, 411 subjects, roles, compilation obligations and 78,912 row
identities stay fixed. The bootstrap leaves the reviewed ledger and frozen
module intact; after it lands, a separate policy transition removes duplicate
workflow assertions, repairs the immutable-driver graph assertion, updates
this row and the blocked benchmark-service profile pins. It changes no
benchmark thresholds or production wrapper behavior.

The same bootstrap admits the exact #1836 successor digest
`5834270ef2b01798b25547751fd91631295a84ccb23116bf1502d8bae0c0b115`.
It is the #1835 declaration with only the historical bridge path changed from
`tests/github_runner_bridge_test.py` to
`tests/retired/github_runner_bridge_test.py.txt`. The bridge bytes, role and
obligation remain intact, and the census inventory keeps the same cardinality.
That path move is a later policy transition; this reader bootstrap leaves both
the bridge and the reviewed ledger at their existing paths.

For #2203, the next trusted-reader bootstrap admits exact successor digest
`7d4e4ed4fc74ff57eb3005550457751cc8841f113277c116d67fb1358da09d51`.
It is the #1836 declaration with only `tests/basic_c_ir_validation_values.c`
changed from 3,124 bytes and SHA-256
`9ed89c0ff3750cb6c9bc66a894fc617c15cde5732c857e5b9cccf55ddf233080` to
3,241 bytes and SHA-256
`8c565e3b33d5630695289da2aa0030423dc833c9dfc4346d2b67d5165df89e65`;
all 559 inputs, 411 subjects, roles, compilation obligations and 78,912 row
identities remain fixed. The bootstrap leaves the reviewed ledger, fixture, support-declaration pin
and blocked recipe policy unchanged; only the duplicated validator byte pin
advances to the modified reader. After it lands, a separate policy transition may
update that fixture row and the matching blocked-profile pins.

For #2428, the next reader bootstrap admits exactly two successors while
retaining every historical declaration digest. The #1836 declaration with only
the dependency-only `tests/mobile_ci_scripts_test.sh` row changed from 40,218
bytes / `7286628dfcbf37b6e34a6fbbd421af961ab93e6137dfc187a3ed14d4e37693a6`
to 41,250 bytes / `0745356ff1ef84e3af0d09a851bf0af09647cd9961579e7a4420ae515f6b973c`
is `f17dbde795c3afc99f4b3cfd59087d4a63721218dab5018e7e77e090228b3741`. Including the separately reviewed #2203 aligned-typedef row
produces `8190b3b14ab97487a3c779ce8a51f8b4150d074eb15fb104dadf8f96705841f2`. Both declarations remain 79,756 bytes with the same
559 inputs, 411 subjects, roles, obligations and row identities. This bootstrap
changes neither reviewed declaration nor fixture bytes, support pins, blocked
recipe policy, applicability, schema or thresholds. Its only recipe pin update
is the modified validator's exact byte digest. Private test projections accept
only the known old/new mobile and aligned-fixture rows to replay historical
declarations; unknown, missing and duplicate row versions reject. A separate
protected policy transition may update the mobile fixture, its exact ledger
row and support pins after this reader lands, preserving the recorded #2203
integration order and source ownership.

For #2276, the reader bootstrap admits one exact successor of the combined
#2203/#2428 declaration `8190b3b14ab97487a3c779ce8a51f8b4150d074eb15fb104dadf8f96705841f2`:
`086b7020a566ac43c854fc84c2835a1eed872ea9f4f5763d8c521df04845194b`.
Only `tests/basic_c_overaligned_stack_caller.c` changes, from 3,618 bytes /
`58479dd6620352c5263e60ea9892e63bb6a6870170fc578a56c37f90f4e01130`
to 3,637 bytes /
`5ffed2270be9761e9abec8e63f83d12d19c3c46e3d1b3fbca82a834f71e1d804`.
The declaration remains 79,756 bytes with 559 inputs, 411 subjects and the
same 78,912 row identities, roles, obligations and axes. All historical
declaration digests remain accepted. Private test projections recognize only
the exact old/new caller rows and reject unknown, missing and duplicate rows.
The bootstrap leaves the frozen fixture, reviewed declaration and support pin
unchanged; only the retained blocked recipe's validator byte pin advances.
No applicability row names this caller, so its ledger and producer pin stay
unchanged. After the reader lands through the trusted integration writer,
#2310 needs a separate policy transition changing the caller, its one support
row and the retained blocked support pin. The retired benchmark service and
its former profile mirrors must not be restored. Fresh exact-head hosted
correctness and protected integration remain required for both transitions.

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

## Committed generated state and queue throughput (#1893)

Requiring pre-integration for every bound PR admitted at most one such PR per
writer run plus one CI cycle. A speculative merge group waits for its
predecessor, and by then its attestation names an older `main`. This section
records why the generated pair stays committed and the post-merge catch-up
design that restores native queue throughput. It is the current contract once
installed on `main`.

### Why the pair stays committed

Each consumer reads the committed bytes for a specific reason:

| Consumer | Reads | Committed bytes needed because |
|---|---|---|
| `tools/native_retirement_census.c` (`nrc_dependency_binding`) | header macros at compile time | `build.c` includes the census, so the expected quartet is compiled into the build driver. Deriving it at build time would put a Python materializer run over the pinned external/SDK closure on every driver build. |
| `native-retirement-evidence.yml` and the `native_retirement_materializer.py` CLI | committed snapshot | Acceptance evidence is bound to the pair at its exact revision. The materializer fails with `authenticated source identity mismatch` when a source differs from its snapshot record. |
| `native_retirement_contract.py` (`load_authority` at import, `validate_dependency_binding`) | both | Live evidence must equal the validator revision's snapshot and quartet. Replay needs a git checkout, not a network rematerialization. |
| `native_retirement_merge_gate.py` (`bound_sources`) | trusted base snapshot | Only the admitted source set. That set is policy-derived and does not need the hashes. |
| `native-retirement-rebind.yml` freshness step, `native_retirement_controller.py` `snapshot_stale` | both (step), snapshot (controller) | Freshness of the committed pair: attested non-catch-up heads must be current; on `main` a behind pair means a catch-up is pending. |
| `native-retirement-contract.yml`, `native-retirement-rebind.yml` reconstruction | ephemeral refresh | Uses a trusted ephemeral reconstruction, not the committed bytes. |

Deriving the pair ephemerally (not committing it) is rejected. It would
not remove the trusted reconstruction step; it would move that step onto
every consumer path, including the build driver and every
`native_retirement_contract.py` import. It would also remove the single
tracked quartet authority that #863 and #877 established, and make evidence
replay depend on rematerializing external inputs.

Archived replay identities and the legacy #508/#510 path do not depend on the
pair. Archived replay lives in reviewed policy. Legacy evidence validates
against the frozen `LEGACY_DEPENDENCY_*` constants in
`native_retirement_contract.py` and the immutable legacy descriptor.

### Design: post-merge catch-up

Two requirements are part of the decision:

- **No serialization for ordinary PRs.** A bound PR queues and lands like any
  other PR, pipelined with its neighbours.
- **Fully automatic.** No human dispatch is needed for ordinary PRs or
  catch-ups.

1. An `ordinary` candidate that changes only admitted repository sources lands
   through the native merge queue with no writer step. The existing read-only
   `Reconstruct candidate closure ephemerally` job validates its exact group
   tree. That job refreshes with the rebinder from trusted `main`, runs the
   candidate check and the contract suite, and allows no other diff.
   Admission accepts that ephemeral proof in place of a writer attestation.
   Expected values come from the trusted rebinder over source bytes, never
   from census or other candidate output.
   - The reconstruction runs on the speculative group tree as soon as the
     group is created. It does not first wait for the predecessor to land
     (`merge_queue_admission.py wait-base`).
   - After the predecessor lands, admission repeats only the cheap checks:
     live identity, and `verify_trusted_policy`, whose `POLICY_PATHS` already
     cover the rebinder/authority tools. These prove that no predecessor
     changed the policy or tools that the reconstruction trusted.
   - A predecessor that did change them forces a group rebuild, as a
     policy-changing predecessor does today.
   - `Main integration admission` collects that job's success on the exact
     group SHA (`required_checks`). It is not ruleset-required, because the
     rebind workflow is path-filtered on pull requests.
2. `bootstrap` and `policy` transitions keep the existing pre-integration
   writer path. They change the authority itself and still need the
   old-authority/new-authority check before they land.
3. After bound candidates land, the single writer publishes one catch-up that
   contains only the two generated files for current `main`.
   - **Dispatch.** `native-retirement-catch-up.yml` runs from trusted `main`
     on `main` pushes and every 30 minutes. When `snapshot_stale` finds the
     committed snapshot behind the admitted sources, it opens one bot-owned
     PR from `native-retirement/catch-up` whose only commit is empty. It does
     not enable auto-merge, because a `GITHUB_TOKEN` enqueue starts no
     `merge_group` workflows. The standing-authorization controller
     (`native-retirement-automation.yml`, #1791) then dispatches the existing
     writer for it as an ordinary request, with no prerequisite CI. No human
     dispatches anything; see
     [automation](native-retirement-automation.md#automatic-catch-up-1893).
   - **Route to `main`.** The writer publishes the usual two-parent
     integration head on that PR, then enables auto-merge with its
     publication credential, which queues it in the same native queue. The writer gets no direct write path to `main` and no ruleset
     bypass. If `main` moves during the run, the writer still publishes
     for its expected base while that head would be admitted below;
     see [staleness](native-retirement-automation.md#staleness-failure-and-cancellation).
   - **Admission.** A catch-up is a writer integration of an empty candidate.
     It is admitted when all of these hold:
     - its generated pair is byte-identical to a trusted reconstruction at
       its recorded publication base;
     - that base is an ancestor of the group base;
     - it has no other delta.

     It does not have to be fresh for the whole group tree. So a bound PR
     that lands ahead of it cannot make it fail, and it never evicts other
     groups from the queue. It moves `main` forward to a verified identity,
     and the controller publishes the next catch-up if sources have moved
     on since then.
4. While a catch-up is outstanding, `main` carries a pair that is behind but
   internally consistent: the header still binds the committed snapshot
   bytes. Each landed tree was ephemerally reconstructed before it landed.
   During that window:
   - the `push` rebind run reports the pair as catch-up pending instead of
     failing, and still reconstructs and runs the contract suite
     ephemerally;
   - acceptance evidence and census runs that need the committed quartet
     must target a revision where `rebind.py check` is clean, which is a
     catch-up or a later revision with no pending source changes.
     `native-retirement-evidence.yml` fails early with that reason;
   - no feature PR waits on a catch-up.

The stale window is one writer run plus one queue CI cycle after the last
bound merge. Several bound merges share one catch-up. In the sampled history
(`main` from 2026-09-19 to 2026-09-29), 8 of 245 first-parent merges changed
an admitted source.

Batching bound PRs is the fallback if catch-up cannot be installed, because
any unrelated merge still invalidates a batch. Regenerating inside the merge
group is not possible with GitHub's native queue.

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

## Current-native reader bootstrap

The separate `validate-current-shards` command reads diagnostic
`current-native-v1` evidence. It preserves the frozen `full-census` schema-2
entry points, four allocator identities, all 78,912 historical rows, the
same-binary reference rule, archive replay, and performance acceptance.
A successful current-native reader run always reports
`acceptance_authorized=false` and `retirement_accepted=false`.
No declaration file, receipt, producer label, or command-line flag changes
that result in this bootstrap.

The prospective declaration path
`docs/current-native-object-census-v1.json` is reserved as reviewed policy.
It is absent here. Combining its addition with trusted reader implementation
is a split-required transition. A separately reviewed producer/profile policy
and trusted writer must establish an acceptance entry point and externally
authenticate hosted build/run provenance at exact heads before these diagnostics
can support retirement acceptance. The old performance validator continues
rejecting this profile; a performance successor requires separate review.

The diagnostic population is 39,456 current-candidate FAST/QUALITY rows,
19,728 historical reference groups, 411 subjects, twelve targets, two frontend
lowerings and two PIC choices. Four shards select `group % 4`; current row
numbers are `group * 2 + mode_index`. Each shard contains its full row map and
only its selected observations. Source-authenticated controls and exclusions
remain explicit records: 576 control rows plus 2,544 target-exclusion rows,
leaving 36,336 executed candidate rows. The unchanged 192-entry frozen gap
ledger is authenticated before projecting its 128 FAST/QUALITY obligations.
Those obligations must succeed; they are not failure waivers.

Version-3 `manifest.txt` uses the existing current support/input/dependency/
environment fields, `profile=current-native-v1`, `rows=39456` and
`reference_groups=19728`. `rows.tsv` retains `ROW_FIELDS`, with
`argv_evidence=groups/<group>/<mode>.argv` and canonical sorted subjects.
The copied support/applicability ledgers and every retained input must equal
the current trusted validator checkout's bytes; historical approved ledger
variants cannot stand in for current subjects. The copied policy, snapshot,
resolved descriptor, materializer receipt and include closure must match that
checkout's live generated authority.
Historical compiler source identity is a separate dimension from the current
input/include closure.

`current-records.json` has schema
`buster-current-native-observations-v1`, `candidate_records` and
`reference_records`. Each record binds group/fixture/target/ABI/CPU/features/
frontend/PIC/recipe/obligations, compiler identity and current closure digest.
It contains exact SHA-256/byte/path descriptors for argv, a process receipt,
object, and preprocessing argv/process. Process receipts bind invocation
identity, compiler, closure and argv digest and retain separate raw stdout/
stderr descriptors. The reader parses raw TARGET, CODEGEN, CODEGEN_VERIFY and
candidate zero-record fallback census lines; producer valid flags are not
accepted. It checks successful process status, zero fallback, copied reference
count, nonempty relocatable object format/architecture and actual preprocessing
stdout parity. A failed reference cannot be replaced by Clang object success.

The reference revision is
`034d33d738f819114f00ff1db671518e9201ff8b`, source tree
`cfdcd084ba016be0899bd29e42057d3bf4cc0e97`, mode `none`, backend
`historical-mir-stack-alias`. At that source NONE selects MIR_STACK.
This is a historical count comparator, with the previous shared-frontend
count oracle's limitations; it is not an independent direct emitter.
Reference argv uses the frozen baseline spelling and permits machine fallback,
but any observed fallback blocks the reader.

Candidate revision/tree and both build-receipt digests are required caller
inputs, separate from bundle claims:

```sh
python3 tools/native_retirement_contract.py validate-current-shards \
  evidence/shard-0 evidence/shard-1 evidence/shard-2 evidence/shard-3 \
  --candidate-revision <exact-candidate-sha> --candidate-tree <exact-tree-sha> \
  --candidate-build-receipt-sha256 <trusted-hosted-receipt-sha256> \
  --reference-build-receipt-sha256 <trusted-hosted-receipt-sha256> \
  --out current-native-validation.json
```

`candidate-build.json` and `baseline-build.json` use
`buster-current-native-hosted-build-v1`, with repository/provider, exact
revision/tree, clean-source assertion, binary/builder/build-log/source-snapshot
descriptors, full build argv, workflow revision/path and hosted run ID/attempt.
Receipt digests bind those bytes to caller selections. This reader checks their
internal bindings but does not authenticate the GitHub issuer or prove arbitrary
binary bytes came from the claimed source. Its report explicitly labels
`provenance_trust=caller-bound-hosted-receipt`; the future trusted writer
must establish that external trust before acceptance.

Preprocessing is a fixed projection of the exact compile argv: `-c` becomes
`-E`; verbose, verification/fallback census flags and the object output pair
are removed. Target/CPU/PIC/frontend/dialect/defines/include order/source remain
bound. Actual retained stdout bytes must match between historical reference and
both live modes. Differences in builtins or preprocessing block diagnostics
until reviewed; the population is never reduced. These separate caller-bound
preprocessing runs do not attest the object invocation's internal token stream.
Object header checks establish format/architecture, while target ABI is bound
through exact argv and compiler provenance.

These diagnostics establish object/count evidence only. Current runtime
differential validation against independent Clang O0/O2 programs remains a
separate obligation. No current producer, reviewed profile declaration, generated
binding, workflow guard/credential or performance policy changes land with
this bootstrap.
