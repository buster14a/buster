# Serialized main integration (#867)

## Rollout state: enabled; live acceptance in progress

Adapter baseline: `f5ce6cdc10e0e9f49b8698a82944daf4688c0f2e` (after #927 and #933).
On 2026-09-22, after #945 landed, ruleset `22537199` was updated and read back:
eight required GitHub Actions checks, non-strict branch freshness, no bypass,
and an initial build limit of one with a merge limit of one, ALLGREEN, and
merge commits. The saved response passed `check-ruleset` at main
`6929d847fbab0014284f698cd235ddb570d60e9d`. The live limit was later raised to
20. On 2026-09-29, #1805 measured that limit exhausting the 50-job macOS runner
ceiling ([ci-runner-queue.md](ci-runner-queue.md)); the checked-in ruleset now
describes a build limit of 4. Verify live settings before relying on it. The
checker has no write API.

On 2026-09-24, the administrator intentionally added Repository admin (role 5)
and `davidgmbb` (user 39247043) as `always` bypass actors. The repository
contract now expects exactly those two. This records the live setting; a bypass
does not satisfy or replace any of the eight required checks or admission
receipts for a normal queued merge.

The trusted retirement gate and queue collector have landed. The adapter admits
one synthetic merge commit only when its first parent has become current main.
For a sensitive group its entire tree must equal the existing writer's attested
PR head. It verifies the open
same-repository PR, creator-bearing status and completed successful writer run.
The acceptance exercises below are still required; local
fixtures do not establish GitHub's live synthetic-commit shape or queue behavior.

## One admission owner, no branch-freshness requirement

GitHub's native merge queue owns order, synthetic heads and rebuilding. Configure
`max_entries_to_build: 4`, `max_entries_to_merge: 1`, `min_entries_to_merge: 1`,
`min_entries_to_merge_wait_minutes: 0`, `check_response_timeout_minutes: 360`,
`grouping_strategy: ALLGREEN`, and `merge_method: MERGE`. Retain
`strict_required_status_checks_policy: false`. A clean feature branch does not
need to be manually updated merely because main advanced. The queue, not the
feature author, constructs and validates the combined candidate.

Build concurrency permits up to 4 queued candidates to run speculative
combined-head validation concurrently; it does not authorize 4 merges. Each
`ci.yml` group needs eight macOS jobs, so four groups hold at most 32 of the 50
observed macOS runners and leave room for pull-request and main validation. A
later candidate may have the preceding unmerged synthetic commit as its base.
Both admission jobs keep that exact group pending until the base lands on main;
they never grant success while the predecessor is speculative. The merge limit
of one serializes merging, and GitHub replaces stale groups when necessary.
`ALLGREEN` requires every queued group to satisfy
its required checks. `MERGE` retains merge commits. Required checks still bind
the exact group and the trusted retirement gate still owns sensitive trees.
The 360-minute queue timeout exceeds the admission workflow's 310-minute job
limit and its five-hour bounded wait. A timeout is a failure, not permission to
merge. Workflow concurrency cancellation only coalesces the same PR or
merge-group ref; main-push policy runs use unique run-ID groups. Separately, the
trusted default-branch CI lifecycle controller starts from the in-progress
Buster CI merge-group run. It watches that run's jobs and the live ruleset's
required GitHub Actions checks across workflows. A completed non-success
required check or Buster CI job cancels active Actions runs for that exact
merge-group head. Optional check failures do not invalidate the group.
Candidate workflows retain read-only authority; the
write-capable watcher executes only the default-branch controller and never
checks out candidate bytes or artifacts. Workflow concurrency is not a FIFO
queue and is never used as a replacement for GitHub queue enforcement.
The read-only native-retirement rebinding workflow also waits for the exact
predecessor to land before checking a later group's closure. It loads that
wait policy from independently checked-out main, checks that the predecessor
did not change admission or rebinding policy, and requires the queue ref to
retain the same group identity at admission. Its bounded 310-minute job accommodates the
five-hour wait.
The self-hosted 9700X benchmark service is manual `workflow_dispatch` work,
not a `merge_group` workflow, so the queue does not schedule it.

There is no second retirement publisher. The existing protected
`native-retirement-integration.yml` writer remains the sole authority allowed
to regenerate and publish the generated pair. This read-only gate neither
regenerates state nor changes PR branches, main, statuses or rulesets.

## Required-check inventory

The six existing checks remain separately required from GitHub Actions app
15368. Preserve the separate `Native retirement merge admission` check installed
by the retirement rollout. Add `Main integration admission` from the same app
only as part of the reviewed queue rollout; never remove an existing requirement.

| Required check | Workflow | PR revision | Merge-group revision |
| --- | --- | --- | --- |
| CI complete | ci.yml | GitHub PR merge revision | Exact synthetic group |
| Linux x86-64 bootstrap evidence | self-host-audit.yml | GitHub PR merge revision | Exact synthetic group |
| Canonical TCC bootstrap | tcc-bootstrap.yml | Explicit PR head (existing #245 policy); its [source-size](source-size.md) step measures the GitHub PR merge revision | Exact synthetic group |
| GPU Linux consumers | gpu-toolchains.yml | Workflow-selected PR revision | Exact synthetic group |
| Benchmark service workflow policy | bench-service-policy.yml | GitHub PR merge revision | Exact synthetic group |
| API migration policy | api-migration-policy.yml | Bounded API compatibility policy | Exact synthetic group |
| Native retirement merge admission | native-retirement-admission.yml (PR/main); trusted reconciler (merge group) | Exact head and trusted integration evidence | Exact generated tree plus successful trusted writer publication |
| Main integration admission | merge-queue-admission.yml (PR/main); trusted reconciler (merge group) | Readiness/regression checks only | Trusted-base verification of the exact group and all six gates |

`CI complete` also runs the [merge-parent preservation guard](merge-parent-preservation.md) over merges introduced by each PR candidate, merge-group candidate, and main push. It uses the event's exact base commit and does not require a feature branch to be updated when `main` advances.

`CI complete` retains desktop x86-64/AArch64, mobile, native-mode, UEFI, lint and
static-analysis ownership. The independent self-host and canonical bootstrap
checks are not replaced by it. GPU Metal remains optional; a failed optional
job does not itself replace the required GPU Linux result. A cancelled workflow
is nevertheless rejected even if its required job had previously succeeded.
Private-runner and physical throughput qualification are not new requirements.

All six workflows already declare unfiltered `pull_request` and
`merge_group: checks_requested` triggers. `audit-workflows` checks that small
contract and the exact required job names; existing actionlint owns general
YAML/expression validation. This does not broaden #245 fork semantics. Fork PR
readiness uses hosted runners, read-only permissions, no secrets, and no
persisted checkout credentials. GitHub's normal fork approval rules still apply.

## Event-driven reconciliation (#1807)

The same trusted reconciler is the merge-group producer for
`Native retirement merge admission` (#1811). The PR/main job lives in
`native-retirement-admission.yml`, which has no `merge_group` trigger. While a
group's own `api-migration-policy.yml` still defines the native-admission job
(`native_owner`), the reconciler only shadow-evaluates the native gate and
publishes nothing. Otherwise it publishes an exact-head check with the marker
`buster-native-retirement-admission-v1:<head>`. It requires the queued base to
be live main and the trusted checkout to be that base, then validates the
native gate and policy twice with intervening identity checks. Pending never
becomes success; denied or changed publication is terminal failure. The native
check can finish before the six other workflows because it validates its own
exact-tree publication contract independently. Activation requires shadow
validation and a live queue trace before relying on the new producer.

The retired legacy `merge_group` job held a hosted Ubuntu runner for up to 310
minutes. It spent most of that time in `run_gate`'s 30-second sleep loop waiting
for the predecessor and the six gates, and did almost no verification. The
`merge-queue-reconcile.yml` workflow replaces that wait with short passes:

- **Triggers.** A completed `merge_group` run of any of the six required
  workflows or of the rebinding workflow (whose reconstruction job
  `required_checks` adds for unattested retirement groups, #1893), a `push` to main (predecessor landing), a 15-minute scheduled sweep
  (bounded recovery for missed or coalesced deliveries) and `workflow_dispatch`.
  Every pass enumerates the live `gh-readonly-queue/main/*` refs itself and never
  trusts a delivery payload. Duplicate, out-of-order, missed and coalesced events
  therefore converge to the same result. One global concurrency group
  serializes passes; each job has a 10-minute limit and never sleeps.
- **Trust.** The job checks out live `main` and runs only that code. It fetches
  group commits as data and never checks out or executes them. The job has read
  scopes plus `checks: write`, and `CheckWriter` can only create or update a
  check run named `Main integration admission`.
- **Classification (`reconcile_group`).** A head already contained in main is
  `landed`. A group whose first parent is a descendant of main is waiting for
  its predecessor and costs no API request. The front group (first parent ==
  main) and divergent groups run `identity` and `evaluate`. The front group is
  the only one that pays for evidence collection.
- **Admission (`evaluate`).** The group gets the same identity, six-gate
  collection, retirement gate and ruleset validation as `run_gate`. Evidence is
  still collected twice and the retirement gate is still rerun. Admission also
  requires the trusted checkout to **be** the landed base. A pass whose
  checkout predates the base stays pending, and that base's main push
  reconciles again. So older authority never judges a tree under a predecessor's
  newer policy, which is stronger than the predecessor-diff rejection. A workflow
  attempt that changes between the two collections leaves the group pending.
  The rerun's own completion reconciles it again.
- **Publication.** A pending group gets one `in_progress` check run carrying the
  exact-head external ID `buster-merge-queue-admission-v1:<head>`. The check run
  is updated only when the pending reasons change. `AdmissionError` completes it
  as `failure` and success completes it with the JSON report. Completed runs are
  terminal: failures are not retried to green, and cancelled work is not
  resurrected. Transport or response errors publish nothing and fail the pass
  so the next event or sweep can retry. A pending check run is never success, so
  exiting while prerequisites are pending cannot satisfy the queue. The queue's
  360-minute timeout remains the bound for groups that never complete.
- **Producer identity.** The check run is created with `GITHUB_TOKEN`, so its app
  is GitHub Actions (15368). The required-check inventory and ruleset are
  unchanged. On a reconciler-owned group, a same-name check run without the
  exact-head external ID gets a published failure; it is never treated as
  authority. The CI fail-fast watcher (`recover-ci.py`) accepts only a run that
  carries this exact-head marker, because the run has no workflow check suite.
- **One producer per group (`group_owner`).** The group's own
  `merge-queue-admission.yml` decides the producer. A group that still declares
  `merge_group` there (queued before activation) keeps the legacy job; the
  reconciler then only shadow-evaluates it and writes nothing, recording the
  decision in its `merge-queue-reconcile-*` artifact and job log. This
  deterministic split lets both versions coexist during rollout without racing.
  `run_gate` (`check-group`) remains in the tool only for such groups.

### Activation and measurement

Activation removes `merge_group` and the group-only steps from
`merge-queue-admission.yml` and moves the native PR/main job out of
`api-migration-policy.yml` into `native-retirement-admission.yml`. Readiness
checks on pull requests and main pushes keep the same job names, so the
required-check inventory (eight checks, app 15368) is unchanged. Both files are
trust-implementation paths, so the change is a `bootstrap` transition that
needs a maintainer dispatch of `native-retirement-integration.yml`. The first
group containing it is reconciled by the older trusted main, which already
publishes for groups whose own workflows lack the legacy producers.

Land activation only after the shadow decisions match the legacy verdicts for
the same heads (recorded on #1807). After landing, record a live two-entry
M → G1 → G2 trace: revisions, run and attempt IDs, reconciler passes per group,
API reads, admission latency after the last gate completes, and the runner
minutes that are no longer held. The offline fixtures do not substitute for
that trace.

Report admission time in three separate parts:

- **Verification work:** a reconciler pass, measured in seconds.
- **Orchestration wait:** time for a predecessor or gate. After activation, no
  admission runner is held during this wait.
- **Build/test queue delay:** runner assignment for the six gates themselves.

This change does not explain or fix host-specific assignment delay (#1805).
The rebinding workflow keeps #1907's in-job predecessor wait (`wait-base`) in
`Reconstruct candidate closure ephemerally`; that remaining runner-held wait is
tracked on #1807.

## Exact identities and fail-closed evidence

For a group, both admission workflows check out live `main` as independently
trusted policy, never the speculative `merge_group.base_sha` or candidate-modified
authority code. The collector fetches the immutable group object without
checking out or executing it; the native gate's candidate checkout is Git data
only for enforcement. They require `GITHUB_SHA == merge_group.head_sha`, main
as the base ref, a main queue ref, the expected repository, and a two-parent
group head with the event base first. The bounded first-parent chain of the
base must contain the trusted policy SHA and observed main. A main SHA outside
that chain, a replaced queue ref, or a changed admission policy between the
initial trusted main and the landed base rejects this group. The report binds
the trusted policy SHA, base SHA/tree, group SHA/tree and queue ref.

Once the predecessor lands, the collector invokes the trusted policy's
`native_retirement_merge_gate.py check --event merge_group --repository ...`
without `--allow-pending`. An absent gate, pending result, rejected candidate or
mismatched base/head fails. For sensitive groups, the gate verifies a two-parent
synthetic commit with landed current main first and the attested integration second.
The synthetic tree must equal both Git's clean combined tree and the full
attested final tree. Additional candidates or any changed byte require a fresh
writer preparation. A generated-only delta also requires publication evidence.

The PR head must still identify exactly one open, ready, same-repository PR
targeting main. The latest integration status must be successful and authored
by `github-actions[bot]`; an earlier green cannot hide a later pending/failure.
Its target must be this repository's trusted integration workflow run from the
exact base. That run must have completed successfully, and the status timestamp
must fall within its latest attempt. Cancelled, incomplete and superseded writer
attempts cannot authorize a group. Sensitive fork heads are rejected because
the existing writer cannot publish to them; ordinary fork groups retain CI.

After main advances, dispatch the same protected writer again with the PR number
and default inputs. It verifies the previous publication, recovers the original
source candidate and reconstructs against current main. Authorization and the
publication lease still bind the current PR head; old generated output is never
merged or reused. The new attested head replaces the old generated integration
commit while retaining its source ancestry. Then enqueue the new head. This is
a fresh authorized dispatch, not autonomous reuse of a historical approval.

The collector reads workflow runs by exact group SHA and `merge_group` event,
then resolves each of the six workflow **paths**, latest run and latest attempt.
It reads jobs from that attempt-specific endpoint and requires one unambiguous
required job with the same head, run ID and attempt and conclusion `success`.
Wrong-workflow same-name jobs, PR-head greens, old successful attempts, missing
jobs, skipped/neutral jobs and cancelled workflows do not count. Missing or
running workflows remain pending until the bounded timeout. API errors,
truncation and ambiguous results fail closed.

Immediately before admission, the collector repeats the six-result read and
trusted-publication verification, requires the same evidence, rechecks that the
group base still equals live main and the queue ref still names this head, and validates
the active ruleset again. The ruleset validator retains the six original checks,
preserves independent retirement admission, adds the exact-group gate, rejects
visible bypass inventories other than the two reviewed actors and strict branch updates, and
requires the exact 4-build/one-merge policy. The success artifact records each
required workflow's run ID, run attempt and job ID. These are read-only checks;
GitHub's enforced queue still owns the final atomic admission/rebuild decision.

### Read-only ruleset visibility

GitHub returns `bypass_actors` only to callers with write access to the ruleset.
The Actions read-only token normally receives no such property. The live
collector validates the ruleset identity, enforcement, scope, required checks,
queue settings and every other policy field it can read. An explicitly returned
bypass list must equal the two reviewed actors; an explicitly returned caller bypass capability must
be `never`. Missing bypass data is recorded as `hidden` in both ruleset reads
in the admission artifact and explained in the log. It is not reported as a
verified actor inventory.

The exact two-actor bypass configuration is an administrator-audited deployment
invariant, not something the read-only workflow can independently prove.
`check-ruleset` remains strict: a saved administrator response must explicitly
contain both actors with `always` mode and no others. Audit that response at activation, after every
ruleset change, and after any emergency recovery. Do not give the admission
workflow ruleset-write credentials to expose this field.

The first live group for #956 exposed the former bug: treating a hidden list as
a standing bypass. The repair preserves trusted-base execution. Consequently,
it cannot authorize its own first merge while main still contains the bug.
Follow the emergency policy below for an explicitly authorized, narrowly scoped
bootstrap; do not run candidate authority code or forge a successful check.

A PR readiness success is not combined-head authorization. Neither a reused
artifact nor a manually posted status may authorize another SHA/tree. A newer
main/group invalidates the current attempt. GitHub must build a fresh group and
rerun required workflows. Content conflicts use #869's exact-path explanation;
nothing chooses `ours`, `theirs` or a union merge. GitHub does not build a
group for a conflicting entry, but it does not remove one either: see
[queued PRs that start conflicting](#queued-prs-that-start-conflicting).

## Queued PRs that start conflicting

When `main` moves under a queued PR and the PR starts to conflict, GitHub skips
the entry while it builds groups and later entries still get groups, but the
entry stays queued. Every push to a queued PR's branch is refused with `GH006`
("Branches that are queued for merging cannot be updated"), so the author
cannot push the resolving merge until the PR is dequeued. This was observed
live on 2026-09-29 for #1521 and #1523 (#1865; acceptance item 5). Neither
entry was removed during the roughly 40 minutes observed. It is unverified
whether GitHub removes such an entry later, for example on a timeout or once
the entries ahead of it drain, and whether the build limit affects the skip.

The trusted `merge-conflict-preflight` job reports this state. The
default-branch sweep reads merge-queue membership once through GraphQL
`isInMergeQueue` with its existing `pull-requests: read` token; the
PR-regression route reads only its own PR. Membership binds the exact head
the preflight analyzed. For a queued head the status description gains a
trailing token, `v1 m=<main> h=<head> o=<n> c=<state> q=queued`. When that
head also conflicts, the report's `merge_queue` record sets
`dequeue_required_before_push` and carries the dequeue instruction. The job
summary lists every such PR. A failed read, or a PR the read did not cover at
its analyzed head, is reported as `unknown` membership with a conditional
instruction. Membership never changes the outcome, the status state or the
sweep's coverage result. The token is omitted unless the read found the head
queued.

To recover a queued PR that conflicts:

1. Dequeue it with **Remove from queue** on the PR or the GraphQL
   `dequeuePullRequest` mutation.
2. Resolve the named paths as the preflight classification prescribes
   ([workflow guide](agents/workflow.md#merge-conflict-preflight)) and
   validate the result.
3. Push the resolution. Enqueue the new head again after its PR checks pass.

Nothing dequeues such a PR automatically. That needs write authority the
read-only preflight does not hold, and it must keep the admission and
authorization policy above, so it is a separate policy decision.

## Build-limit rollout and acceptance

### Changing the live build limit

The merge-group workflow runs admission code from the main revision checked out
as trusted policy, so a PR
changing the repository queue contract must first pass the policy already on
`main`. Keep the live `Build concurrency` field in ruleset `22537199` at the
value the trusted policy accepts (20 before #1805) while the policy PR passes
and merges through the existing queue with all required checks; do not bypass
admission or edit the live value first. Resolve the exact resulting `main`
commit before changing the live setting to `4`. Read the ruleset back, run
`check-ruleset` on the saved response and observe `Main integration admission`
accept a new exact merge group. Between the PR merge and the live edit, the
trusted policy expects `4` while GitHub still reports `20`, so new groups fail
closed; groups already dispatched keep their actual outcomes. Do not bulk-cancel
or reorder the queue.
Only the build limit changes; leave the merge limit, grouping strategy, merge
method, timeout, checks, non-strict freshness and bypass settings untouched.
The read-only collector cannot update rulesets; a repository administrator
changes **Settings → Rulesets → main → Require merge queue → Build concurrency**.

### Remaining acceptance

Activation is complete. Retain evidence for the remaining live exercises below;
do not close #867 based on settings or offline fixtures alone:

1. Land the exact-tree adapter and source-recovery bootstrap through the existing
   trusted writer. Verify a fresh dispatch after main advancement, and verify
   the actual GitHub group shape against the adapter's single-candidate contract.
   Keep writer authorization, trust transitions, fork disposition and cancellation
   fail-closed. Do not create another writer/queue to work around that gate.
2. Merge this prerequisite and run its policy tests on actual main. Resolve
   any newly added required checks by auditing and extending the inventory;
   never overwrite a newer live ruleset with the historical desired JSON.
3. Have an authorized repository administrator apply the reviewed queue and
   required-check changes, preserving other live protections. Read back
   `GET /repos/buster14a/buster/rulesets/22537199` and the effective branch rules;
   run `check-ruleset` against the saved current response. Contents/PR write
   access does not imply permission to edit repository rulesets.
4. Queue two nonconflicting PRs from the same old base and retain both exact
   group reports. Record M, G1 and G2: the later base may be G1 while live main
   is M. Confirm both admission jobs stay pending on G2, then succeed only if
   G1 lands unchanged on main and G2 remains the exact queue ref; prove both
   PRs merge in order without manual re-enqueue. Repeat with different
   retirement-bound sources and attestations;
   retain the fresh authorized writer dispatch and regenerated second head.
   A stale sensitive group must block until that dispatch finishes and the
   replacement head is enqueued. Automatic dispatch/re-enqueue is not implemented
   or authorized by this adapter; any such automation must preserve the existing
   exact-head admin/reviewer authorization policy.
5. Inject a controlled combined-candidate check failure despite green PR heads;
   prove no merge. Exercise cancellation/rerun, main advance during preparation,
   fork approval, and genuine conflict removal with exact path diagnostics.
   Live case, 2026-09-29 (#1865): `main` advanced under queued #1521
   (`68e7655` from #1703 on `c_internal.h`, then `9d96fb9` from #1538 on
   `c_parse.c`) and queued #1523 (`f75c091` from #1587 on
   `c_parse_internal.h`). The preflight posted `c=conflicted` on #1521's head
   for each new main. PRs enqueued after both received groups. GitHub
   recorded no `removed_from_merge_queue` event for either in about 40
   minutes and refused both validated resolution pushes with `GH006`. The
   path diagnostics held, but GitHub did not remove the entries; recovery is
   the manual dequeue above. Still to retain: the first live sweep report
   that shows `q=queued` on a conflicted head.
6. Verify UI merge, API merge, squash/rebase and every automation are subject to
   the effective queue/retirement controls. No live production rejection or
   queue experiment is claimed by the offline fixtures.

This policy repair requires a first-position queue merge under the old trusted
admission code. The native workflow uses that older gate directly only when
the group base equals the trusted main revision; a speculative base requires
the new trusted `wait-base` command and fails closed until the repair lands.
Keep the repair at the front of the queue during rollout; older later groups
still run the old code and can fail. After the repair lands, queue
fresh groups and retain their exact-head workflow and queue progression links.
The read-only collector cannot alter the ruleset or enqueue a PR. Do not report
the offline M/G1/G2 fixtures as a live acceptance trace.

Useful read-only commands:

```sh
python3 -B tools/merge_queue_admission_test.py -v
python3 -B tools/merge_queue_admission.py audit-workflows .
python3 -B tools/merge_queue_admission.py check-ruleset .github/main-merge-queue.ruleset.json
python3 -B tools/merge_queue_admission.py check-ruleset /tmp/live-main-ruleset.json
# One bounded pass; publishes only for reconciler-owned groups (needs checks: write).
GH_TOKEN=... python3 -B tools/merge_queue_admission.py reconcile --repo-root . \
  --repository buster14a/buster --details-url URL --output /tmp/reconcile.json
```

## Emergency policy

There are two reviewed standing bypass actors. A repair PR with genuine
passing checks is preferred; using bypass does not make a failed or missing
check successful. Any emergency settings change needs
explicit administrator authorization, a recorded reason and exact before/after
settings, followed by restoration and read-back verification. Never mint a green
check for cancelled, missing, failed or stale validation. Do not run a post-merge
rebinding repair as the normal publication path.

GitHub references: [merge queue behavior](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-a-merge-queue),
[ruleset API](https://docs.github.com/en/rest/repos/rules),
[attempt-specific jobs](https://docs.github.com/en/rest/actions/workflow-jobs#list-jobs-for-a-workflow-run-attempt).
