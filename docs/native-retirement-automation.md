# Unattended native-retirement integration

[Retirement ownership and manual integration](native-retirement-rebinding.md)
remain the default. This opt-in extension implements the **cloud phase of #1791**:
standing machine authorization and automatic scheduling through the existing
single writer. It does not install, authorize or invoke another benchmark-host
executor. The standing policy shipped **disabled** and is now enabled for the
`ordinary` class only. Source availability is not a claim that the live
repository has been activated or demonstrated.

## Authority and ordinary operation

`.github/workflows/native-retirement-automation.yml` checks out its immutable
protected-main workflow revision. It never checks out candidate content for
execution. It fetches candidate Git objects only for the existing trusted
classifier and source-candidate verifier. The controller can dispatch Actions
and maintain a bot-authored request ledger in PR comments; it cannot publish
repository contents or merge PRs.

The standing grant is `.github/native-retirement-automation.json`. It explicitly
names the repository, enablement, policy epoch, allowed transition classes and
paused PR numbers. The writer reads the **live remote policy**, bounded by main
identity checks, during preparation and immediately before publication. It also
requires the controller workflow's live state to be `active`. Disabling that
workflow is the emergency revocation operation; a workflow-start variable
snapshot is not used as a live grant.

The machine principal is `github-actions[bot]`, numeric user ID `41898282`, with
GitHub's `Bot` type, in both the writer's actor and triggering-actor records.
This is not the owner's identity or an independent maintainer review. The
`automation` mode records an empty `maintainer_approvals` list and the standing
grant, authenticated controller artifact and exact run/request identities.
Manual `configured`, `solo-maintainer` and `independent-review` modes remain
available with their previous authorization requirements.

Both controller and writer must be fresh attempt 1 runs from the fixed main-only
workflow paths. A machine request binds repository, PR number, base, live head,
recovered original source head, transition class, exact policy bytes, controller
run and a deterministic request key. Re-running jobs is not a new machine grant.
The existing merge verifier continues to require the same writer path,
`workflow_dispatch` event, current base, exact tree, bot attestation and latest
successful attempt. It does not gain an alternate status producer.

Implementation/policy separation, old-trusted-tool reconstruction, generated-file
ownership and independent candidate validation remain required. Changes to the
automation itself, `.github` policy/workflows, admission boundaries, performance
budgets, installed deployment policy and other enumerated owner-only paths are
outside the standing grant. A candidate cannot broaden its own authorization.
The allowed-class list does not override those path exclusions.

## Non-circular scheduling

The controller reconciles main pushes, completion of `Buster CI`, the trusted
writer or the catch-up opener, a ten-minute fallback schedule, and manual
reconciliation. Event delivery
is a hint: it reads current state rather than trusting a stale event payload.
GitHub schedule delays can increase latency; there is no real-time guarantee.

Only open, non-draft, same-repository PRs targeting main are eligible. Only
trust transitions and catch-up requests need a writer run. Except for a
catch-up request, the exact head must have a successful GitHub Actions
`CI complete`. Other existing Actions
checks, including performance, must be completed successfully, neutral or
skipped. The two attestation-dependent gates, `Native retirement merge admission`
and `Main integration admission`, are deliberately **not prerequisites for
starting their own writer**. Their real merge-time requirements are unchanged.
Missing CI, real test failures, cancellations and still-running checks defer the
request; automation does not replace a head just to obtain another test attempt.

A fresh source PR whose changes require retirement publication can be integrated
without setting auto-merge. Already-published heads do not trigger a publication
loop. When main advances, stale published heads with existing auto-merge intent
are prioritized; idle stale PRs are deferred rather than all being rebuilt after
every merge. The controller never invents merge intent and does not merge or
modify queue entries. If publication clears auto-merge in the live repository,
that intent-preservation behavior must be separately verified before calling
end-to-end merge progression unattended.

A candidate the writer would refuse (split-required, or one that edits
integration-owned generated files) is recorded as `blocked` with the refusal
detail. The controller keeps scanning the other PRs; one refused PR never fails
the reconcile run. The controller and merge gate share a single
`native_retirement_integration` module, so the gate's `IntegrationError` is the
class the controller catches.

The controller submits at most one request per run and leaves an already active
writer alone. GitHub concurrency serializes the existing writer; the controller's
ledger, not pending-workflow concurrency slots, carries durable request state.
No strict freshness policy is added for unrelated PRs.

## Automatic catch-up (#1893)

Ordinary PRs that change admitted sources no longer need the writer. They land
through the native merge queue, and the controller does not request writer runs
for them. `main` then carries a committed repository-source snapshot that is
behind its admitted sources, until one catch-up publishes the regenerated pair.

The catch-up is fully automatic:

1. `.github/workflows/native-retirement-catch-up.yml` runs from immutable
   trusted `main` on every `main` push, every 30 minutes and on manual dispatch.
   It uses the same standing policy and kill switch as the controller.
   `snapshot_stale` compares the committed snapshot with admitted source bytes
   read from Git objects, with no pinned closure needed. It only does the following:
   - When the snapshot is stale and no request is open, it creates one empty
     commit on `main`, points the bot-owned `native-retirement/catch-up` branch
     at it, opens the PR and enables auto-merge. Its token has `contents: write`
     and `pull-requests: write` for exactly this. It publishes no generated state
     and cannot merge.
   - When `main` is current, it closes any open catch-up request.
2. The controller treats that bot-owned PR as an ordinary request with an empty
   classification. It skips prerequisite CI, because nothing on a bot-created
   empty head needs testing. It dispatches the existing writer through the
   standing grant. The writer regenerates the pair for current `main` and
   publishes the usual two-parent integration head; that push starts PR CI.
3. Auto-merge queues the published head. The merge gate treats it as a
   catch-up: it stays admissible after other ordinary-bound PRs land first, as
   long as `main` has published no newer generated state since its recorded
   base. So it cannot fail because it lost a race, and it never evicts other
   groups. A published catch-up that is still admissible is not rebuilt when
   `main` moves. If sources moved on, the next run opens a new catch-up after
   this one lands.

Prerequisites beyond the standing-grant activation below:
- Settings -> Actions -> General must allow GitHub Actions to create pull
  requests.
- Auto-merge must be allowed in the repository.
- Both are unverified on the live repository.

## Claim, sealed request and one dispatch

Before artifact upload or dispatch, the controller persists a bot-authored claim
in the PR conversation. The immutable request key excludes the controller run
number so duplicate events for the same candidate/base/policy cannot create a
new request. Claims remain visible across controller restarts and missed events.

The controller then uploads exactly one bounded `request.json` artifact and
performs one `workflow_dispatch` POST with the full pinned inputs. The writer
independently verifies the controller's repository, main revision, workflow path,
event, attempt and state; the artifact's unique name, size, digest and complete
JSON; and equality with the request supplied in its dispatch. Publication also
requires the controller run to have completed successfully.

PR comments are a **deduplication ledger, not publication authority**. Copying a
comment, supplying a candidate artifact, changing a run title or presenting a
lookalike bot does not satisfy writer authorization. Archive download is bounded,
never extracts files, and does not forward the API token to the signed object
URL. Missing/expired artifacts or unsupported download provenance fail closed.

An accepted dispatch is recorded with its run ID when the API supplies one.
Older API responses without that ID are reconciled by exact request title,
workflow path, actor and base. A lost POST response is not permission to POST
again. The controller recovers a visible matching run; an absent or ambiguous
result becomes blocked for owner reconciliation. Duplicate runs, malformed
ledger data and pagination limits are also blocking, not silently truncated.

## Staleness, failure and cancellation

The writer preserves exit 75 for positively observed main movement and exit 76
for positively observed head movement. Trusted job outputs carry those outcomes
into a separate read-only result job. Its fixed `Superseded request` marker is
accepted only from the verified writer's exact attempt; candidate test logs or
artifacts cannot supply that decision. The first failed stage decides the
outcome, so a validation failure is not relabeled stale because main later moved.

A superseded request is discarded. A later reconciliation may construct a new
request for fresh identities, using the existing source recovery for an attested
head. The previous generated state is not reused. Input movement detected before
the dispatch POST is recorded separately as known-not-dispatched supersession.
An uncertain leased publication is never treated as successful or blindly retried.

Failed, cancelled, timed-out or ambiguous requests remain blocked for that source
and policy even if only main advances. A deliberate cancellation does not
immediately resurrect itself. A real source change, an explicitly reviewed new
policy epoch, or owner-directed manual integration/reconciliation is required
for exceptional recovery; normal successful/stale operation needs no new approval.
Historical failures are retained, never overwritten with fabricated success.

## Installation and activation

Install the source-only bootstrap through the existing permitted main integration
process. The bootstrap shipped the policy disabled, with the host workflow and
settings unchanged. A branch cannot activate its own privileged controller or
writer.

After the implementation is trusted on main:

1. Inspect the intended standing class/path scope and change only the reviewed
   policy to `enabled: true` through the existing owner-authorized policy path.
   Keep the controller disabled while installing or changing live configuration.
2. Configure `NATIVE_RETIREMENT_PUBLICATION_TOKEN` in the existing
   `native-retirement-integration` environment with the documented narrow
   repository publication permissions. Automatic mode fails before publication
   if it is absent; manual compatibility mode retains its existing warning.
   Keep the token out of candidate jobs, logs, workflow inputs and PR text.
3. Read back that environment's deployment restriction to main and remove the
   per-run required-reviewer/wait-timer rules under the owner-approved transition.
   Preserve required checks and branch/merge-queue protections. No workflow here
   alters repository settings, installs a credential or bypasses a required check.
4. Enable `Native retirement automation` and verify a real eligible PR, a main
   advancement and a published-head CI run. Retain exact request/run/artifact,
   publication SHA, CI and admission receipts. Verify archive download behavior
   and credential-triggered CI on the actual account; fixtures are not live proof.

To revoke, disable **Actions -> Native retirement automation -> Disable
workflow**. Already-prepared writers fail their next live authorization check,
including the check immediately before the leased PR update. This is a checked
revocation boundary, not an atomic transaction with GitHub's ref update: an
administrator revoking authority should also cancel active writer runs and read
back any already-completed publication. A later grant should use a new reviewed
epoch if previously failed/cancelled source requests are intentionally reopened.

## Validation and remaining acceptance

Run network-free focused regressions with:

```sh
python3 tools/native_retirement_automation_test.py -v
python3 tools/native_retirement_controller_test.py -v
```

The read-only `Native retirement automation tests` workflow also runs the existing
integration, path-security, merge-admission and preflight suites on the submitted
revision. The writer's isolated validation job includes the new focused tests.
No local desktop or benchmark host is used for performance validation here.

The cloud implementation must be distinguished from live activation, hosted
acceptance and automatic preservation of any merge intent cleared by publication.
Do not close #1791 merely because source or fixture tests have landed.

**Phase B remains separate:** the checked-in 9700X workflow still restricts its
requester to the maintainer. #880 owns live service readiness and #881 owns the
actual performance recipe. A separate reviewed gateway delegation and live
request/job/cleanup/export/replay proof is required. This controller does not
submit smoke or retirement performance jobs, enable the host, install binaries,
change acceptance thresholds or establish #36 retirement acceptance.

GitHub platform references: [workflow API](https://docs.github.com/en/rest/actions/workflows),
[artifact API](https://docs.github.com/en/rest/actions/artifacts),
[workflow triggering](https://docs.github.com/en/actions/how-tos/writing-workflows/choosing-when-your-workflow-runs/triggering-a-workflow).
