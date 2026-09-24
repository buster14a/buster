# Serialized main integration (#867)

## Rollout state: enabled; live acceptance in progress

Adapter baseline: `f5ce6cdc10e0e9f49b8698a82944daf4688c0f2e` (after #927 and #933).
On 2026-09-22, after #945 landed, ruleset `22537199` was updated and read back:
eight required GitHub Actions checks, non-strict branch freshness, no bypass,
and a single-build/single-merge ALLGREEN queue using merge commits. The saved
response passes `check-ruleset` at main `6929d847fbab0014284f698cd235ddb570d60e9d`.
`.github/main-merge-queue.ruleset.json` describes that configuration; verify live
settings before relying on it. The checker has no write API.

The trusted retirement gate and queue collector have landed. The adapter admits
one synthetic merge commit only when its first parent is current main and its
entire tree equals the existing writer's attested PR head. It verifies the open
same-repository PR, creator-bearing status and completed successful writer run.
The acceptance exercises below are still required; local
fixtures do not establish GitHub's live synthetic-commit shape or queue behavior.

## One admission owner, no branch-freshness requirement

GitHub's native merge queue owns order, synthetic heads and rebuilding. Configure
`max_entries_to_build: 1`, `max_entries_to_merge: 1`, `min_entries_to_merge: 1`,
`grouping_strategy: ALLGREEN`, and `merge_method: MERGE`. Retain
`strict_required_status_checks_policy: false`. A clean feature branch does not
need to be manually updated merely because main advanced. The queue, not the
feature author, constructs and validates the combined candidate.

Build concurrency and merge batch size are distinct controls; both are one.
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
| Canonical TCC bootstrap | tcc-bootstrap.yml | Explicit PR head (existing #245 policy) | Exact synthetic group |
| GPU Linux consumers | gpu-toolchains.yml | Workflow-selected PR revision | Exact synthetic group |
| Benchmark service workflow policy | bench-service-policy.yml | GitHub PR merge revision | Exact synthetic group |
| API migration policy | api-migration-policy.yml | Bounded API compatibility policy | Exact synthetic group |
| Native retirement merge admission | api-migration-policy.yml | Exact head and trusted integration evidence | Exact generated tree plus successful trusted writer publication |
| Main integration admission | merge-queue-admission.yml | Readiness/regression checks only | Trusted-base verification of the exact group and all six gates |

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

## Exact identities and fail-closed evidence

For a group, the admission workflow checks out `merge_group.base_sha`, never
candidate-modified authority code. It fetches the immutable group object without
checking out or executing it. It requires `GITHUB_SHA == merge_group.head_sha`,
main as the base ref, a main queue ref, the expected repository, and base ancestry.
The report binds base SHA/tree, group SHA/tree and queue ref.

Before waiting for CI it invokes the trusted base's
`native_retirement_merge_gate.py check --event merge_group --repository ...`
without `--allow-pending`. An absent gate, pending result, rejected candidate or
mismatched base/head fails. For sensitive groups, the gate verifies a two-parent
synthetic commit with current main first and the attested integration second.
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
trusted-publication verification, requires the same evidence, rechecks live main and the queue ref, and validates
the active ruleset again. The ruleset validator retains the six original checks,
preserves independent retirement admission, adds the exact-group gate, rejects visible bypasses/strict branch updates, and
requires the single-build/single-merge policy. The success artifact records each
required workflow's run ID, run attempt and job ID. These are read-only checks;
GitHub's enforced queue still owns the final atomic admission/rebuild decision.

### Read-only ruleset visibility

GitHub returns `bypass_actors` only to callers with write access to the ruleset.
The Actions read-only token normally receives no such property. The live
collector validates the ruleset identity, enforcement, scope, required checks,
queue settings and every other policy field it can read. An explicitly returned
bypass list must be empty; an explicitly returned caller bypass capability must
be `never`. Missing bypass data is recorded as `hidden` in both ruleset reads
in the admission artifact and explained in the log. It is not reported as a
verified empty list.

No-standing-bypass configuration is an administrator-audited deployment
invariant, not something the read-only workflow can independently prove.
`check-ruleset` remains strict: a saved administrator response must explicitly
contain `bypass_actors: []`. Audit that response at activation, after every
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
the queue removes/blocks the PR, never chooses `ours`, `theirs` or a union merge.

## Activation and acceptance

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
   group reports. After the first lands, prove the second was rebuilt against
   the new main. Repeat with different retirement-bound sources and attestations;
   retain the fresh authorized writer dispatch and regenerated second head.
   A stale sensitive group must block until that dispatch finishes and the
   replacement head is enqueued. Automatic dispatch/re-enqueue is not implemented
   or authorized by this adapter; any such automation must preserve the existing
   exact-head admin/reviewer authorization policy.
5. Inject a controlled combined-candidate check failure despite green PR heads;
   prove no merge. Exercise cancellation/rerun, main advance during preparation,
   fork approval, and genuine conflict removal with exact path diagnostics.
6. Verify UI merge, API merge, squash/rebase and every automation are subject to
   the effective queue/retirement controls. No live production rejection or
   queue experiment is claimed by the offline fixtures.

Useful read-only commands:

```sh
python3 -B tools/merge_queue_admission_test.py -v
python3 -B tools/merge_queue_admission.py audit-workflows .
python3 -B tools/merge_queue_admission.py check-ruleset .github/main-merge-queue.ruleset.json
python3 -B tools/merge_queue_admission.py check-ruleset /tmp/live-main-ruleset.json
```

## Emergency policy

There is no standing bypass. A repair PR
with genuine passing checks is preferred. Any emergency settings change needs
explicit administrator authorization, a recorded reason and exact before/after
settings, followed by restoration and read-back verification. Never mint a green
check for cancelled, missing, failed or stale validation. Do not run a post-merge
rebinding repair as the normal publication path.

GitHub references: [merge queue behavior](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-a-merge-queue),
[ruleset API](https://docs.github.com/en/rest/repos/rules),
[attempt-specific jobs](https://docs.github.com/en/rest/actions/workflow-jobs#list-jobs-for-a-workflow-run-attempt).
