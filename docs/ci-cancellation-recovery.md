# CI cancellation recovery and merge-queue fail-fast

On 2026-09-08, PRs #243, #244, #249, #250, #251 and #252 had cancelled
`Buster CI` runs on their current head commits with no replacement. Run
[34166189941](https://github.com/buster14a/buster/actions/runs/34166189941)
had five cancelled platforms and one successful platform. Its first attempt
used `fail-fast: false`; the logs did not identify the cancellation requester.
This evidence does not establish concurrency or runner capacity as the cause.

## Immediate recovery

Inspect the current PR head, its latest workflow run, and all job conclusions.
For an interrupted run with no real validation failures, use GitHub's
**Re-run failed jobs** operation. It retries cancelled jobs and their
dependents while retaining successful results. For example:

```sh
gh run rerun 34166189941 --failed --repo buster14a/buster
```

That operation was accepted for #243, #244, #250, #251 and #252 on 2026-09-08.
The API showed attempt 2 and retained #252's successful Windows AArch64 result.
This is a recovery request, not evidence of passing CI. #249 also had a Linux
x86-64 job marked `failure`; investigate that failure separately.

## Automatic recovery

Two trusted default-branch workflows observe `Buster CI`. The completion-only
`.github/workflows/ci-recovery.yml` reviews ordinary PR failures and
cancellations; `.github/workflows/ci-merge-group-watch.yml` reviews queue starts,
required-workflow completions and a scheduled sweep. Both retain the upstream-run
concurrency key, `GH_ACTIONS_CI_ENABLED` switch and job-level identity checks.
`GH_ACTIONS_CI_RECOVERY_ENABLED=false` still disables only the recovery job.
Neither path modifies compiler/build policy, runner labels, coverage, or merge
requirements. They take effect only after landing on the default branch.

The separate `.github/workflows/ci-recovery-tests.yml` runs both existing
offline suites for PR and main changes to the two handlers, their helper and
test sources. It also runs for `ci.yml` changes, because the watcher's step
deadline mirrors a `ci.yml` step timeout. The new workflow-shape tests live in
`.github/scripts/test_ci_recovery_workflows.py` so the frozen native-retirement
support inventory and tracked `tests/` census stay unchanged. The regression workflow
has no `workflow_run` trigger and no write credential. This
preserves the source-change regression coverage without adding a skipped test
check to every lifecycle delivery.

| Upstream Buster CI notification | Handler workflow invocation | Job check / assigned runner |
| --- | --- | --- |
| Same-repository PR success, first or later attempt | Recovery completion | One skipped `recover` check / none |
| Same-repository PR failure or cancellation, first attempt | Recovery completion | One conditional `recover` check / one if enabled; helper determines retry eligibility |
| Same-repository PR later attempt, stale or duplicate completion | Recovery completion | One skipped check for later attempt; a duplicate first-attempt delivery may run the helper, whose current-head/attempt checks prevent an extra retry |
| Main push start or completion | None: `main` is excluded | None / none |
| Merge-group Buster CI start, including a rerun | Watcher in-progress | One short watcher check / one if enabled and same-repository |
| Merge-group required-workflow completion | Watcher completion | One short watcher check / one if enabled and same-repository; recovery excludes the queue branch |
| Scheduled queue sweep or manual dispatch | Watcher sweep | One short watcher check / one if enabled; no upstream run required |
| Fork completion on a PR branch | Recovery completion | One skipped check / none; same-repository guard |
| Recovery-policy file changed on a PR or main | Regression test through its path filter | One test check / one if enabled; no privileged lifecycle step |

The branch filters use the **upstream** `workflow_run.head_branch`, not the
trusted handler's `GITHUB_REF=main`. Run `36537752584` recorded
`gh-readonly-queue/main/pr-1769-...` for a merge-group Buster CI run; run
`36541652880` recorded `main` for the subsequent push at the same head SHA.
The helper still rejects unrelated source events, tags, main, forks, changed
attempts, obsolete PR heads and non-cancellation failures. A tag delivery, if
GitHub selects it despite the branch filters, cannot authorize recovery.

Each lifecycle run name identifies the upstream event, branch, SHA, run and
attempt, and the handler's default-branch SHA. When a job actually runs, its
summary links both the upstream and handler runs and separates the upstream
head from the trusted code revision. A skipped check has no runner summary;
its run title says *recovery review*, not *cancellation*. GitHub's
[`workflow_run` event](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_run)
still attaches handler checks to the latest default-branch commit. Checking
out another ref, or adding job-level `if`, does not change that association or
delete historical checks.

The original handler created three job checks for each notification: `test`,
`watch-merge-group` and `recover`. Run
[`36545740179`](https://github.com/buster14a/buster/actions/runs/36545740179)
is a measured no-op: one workflow invocation, three skipped check records,
zero jobs assigned to a runner, and zero runner minutes. An ordinary start and
completion pair could therefore create two invocations and six checks even on
success. The new event/branch selection predicts one invocation and one
skipped check for an ordinary PR success, and zero for a main push; a queue
start predicts one watcher check and each required-workflow completion another;
the sweep runs every 15 minutes. These are source-level predictions pending a
post-merge hosted trace, not measured after counts. The source-change test
workflow still consumes a runner when it is triggered. [#1808](https://github.com/buster14a/buster/issues/1808)
tracks the independent, genuinely expensive duplicate queue/main build work.

The default-branch helper `.github/scripts/recover-ci.py` requests at most one
automatic retry (attempt 2), only when all these conditions hold:

- The original event is a same-repository push or pull request, not main,
  a manual run, a merge group, a fork, or an unrelated workflow.
- Exactly one open PR still has the run's branch and commit as its current head.
- At least one job was cancelled, all jobs completed, and no validation job
  failed or timed out. The `CI complete` aggregate may fail because a
  prerequisite was cancelled; it is retried with its prerequisites.
- No newer workflow run exists on the branch, no other run of that commit is
  active, and neither the PR head nor the attempt changed during the checks.

Successful jobs are retained. A failed test stays failed. A second cancellation
remains visible for investigation and cannot cause an automatic retry loop.
The recovery job summary records why it retried or skipped; API errors fail the
recovery job without guessing. Reads are paginated with a conservative bound.

For an intentional cancellation, add the PR label `ci-no-retry` before
cancelling. To disable recovery repository-wide, set
`GH_ACTIONS_CI_RECOVERY_ENABLED=false`, or disable the recovery workflow.
GitHub's completion event does not reliably distinguish intentional from
unexpected cancellation, so an unlabelled intentional cancellation may receive
the same single retry. This is interruption recovery, not a diagnosis or a
guarantee of available runner capacity. GitHub does not offer an atomic
"retry only if PR head still equals SHA" operation: a push immediately after
the final read can still race the request; normal stale-run cancellation remains.

The write-enabled watcher and recovery jobs check out only `github.sha` from
their own repository: for `workflow_run`, that is the trusted default-branch
revision. They never check out the triggering branch or read its artifacts.
Only those lifecycle jobs have `actions: write`; the separate offline test job
has `contents: read`.

## Merge-queue fail-fast

For a `merge_group` Buster CI run, the trusted watcher makes a short pass when
Buster CI starts or a required workflow completes. A 15-minute sweep recovers
missed notifications and catches failed Buster CI shards that finished before a
required-workflow completion. Each pass uses job-scoped `actions: write` and
`checks: read` and reads the live main ruleset's required-check names, exact-head
merge-group run identities, and the corresponding GitHub Actions check runs.
It also reads Buster CI jobs so a failed shard need not wait for `CI complete`.
The first completed non-success required check or Buster CI job invalidates the
group. The watcher then requests cancellation of every active Actions run with
the same merge-group head SHA and `merge_group` event. Optional check failures
do not trigger cancellation; completed or different-head runs are never targeted.
It keeps watching after Buster CI succeeds and stops only when every required
check has succeeded. The handler takes no runner-held wait between passes; its
job has a five-minute timeout. The only wait inside a pass is the bounded
step-deadline escalation described below. The sweep inspects at most 25 live
queue refs.

On `merge_group`, Buster CI's desktop and mobile matrices and the independent
materializer matrix use native matrix `fail-fast`. The twelve
desktop shards first wait for the cheap workflow-lint job; ordinary PR, main,
tag, and manual runs still execute after a lint failure for diagnostics. The
Buster native matrix retains `fail-fast: false` under the frozen CI test
contract, but its first failed job is found by a completion event or sweep. The separate required
workflows have no shared `needs` dependency, so the watcher closes that gap.

The watcher is intentionally not implemented inside candidate-controlled
`ci.yml`: merge-group candidate code retains read-only permissions. The
write-capable controller executes `github.sha` from the default branch and
uses only GitHub event/API metadata. Race responses indicating a run already
finished are treated as a no-op; other API errors remain visible failures of the
controller rather than authorization to continue or fabricate success. The
watcher is a cost-saving controller, not a required status check; the existing
eight required checks remain the authority for merge admission.

## Merge-group workflow-tool step deadline

Buster CI run [36571009755](https://github.com/buster14a/buster/actions/runs/36571009755)
attempt 1 (merge-group candidate `fd1ebb286a90639535e654456084395aa92ab84c`)
blocked its queue entry on `macOS x86-64 release`. That job's `Workflow tool
regression tests` step started at 2026-09-29T12:53:23Z and was still
`in_progress` more than 30 minutes later, despite its 5-minute step timeout.
The other 23 jobs succeeded. The job log was unavailable (`BlobNotFound`), so
no cause is established ([#1866](https://github.com/buster14a/buster/issues/1866)).
The fail-fast path could not help: it reacts only to *completed*
non-success jobs.

The same trusted watcher therefore applies a narrow deadline in every pass:
a Buster CI start, a required-workflow completion, the 15-minute sweep, or a
manual dispatch. It covers only the six desktop Release job names
(`Linux x86-64 release` … `Windows AArch64 release`) and only their
`Workflow tool regression tests` step:

| Lanes | Step budget (`ci.yml`) | Grace | Deadline after step start |
| --- | --- | --- | --- |
| Linux x86-64, Linux AArch64 | 2 minutes | 10 minutes | 12 minutes |
| macOS x86-64, macOS AArch64, Windows x86-64, Windows AArch64 | 5 minutes | 10 minutes | 15 minutes |

`WORKFLOW_TOOLS_BUDGET_SECONDS`, `STEP_DEADLINE_GRACE_SECONDS`,
`FORCE_CANCEL_GRACE_SECONDS` and `STEP_DEADLINE_POLL_SECONDS` in
`.github/scripts/recover-ci.py` hold the policy. `test_budgets_mirror_the_ci_workflow_step` fails if the budgets drift
from the step's `timeout-minutes` or the matrix names. The watcher executes
default-branch code, so a PR that raises that timeout past budget plus grace
is protected only after the matching watcher budget lands.

The grace is wider than the runner's own timeout cleanup needs. A false
positive cancels a healthy merge candidate, which must then requeue and run
all of CI again. That happens when a step finishes near its budget but GitHub
reports the completion late. A genuine hang costs at most the extra grace.
A shorter 2-minute grace, giving 4- and 7-minute deadlines, was proposed in
#1867 and not adopted for this reason.

A step that has passed its deadline is acted on by the next pass. Once the
other jobs and required workflows have finished, as in the incident, that is
normally the sweep, up to 15 minutes later plus any scheduling delay.

Elapsed time runs from GitHub's step `started_at`, compared with the
controller's clock. The job's `started_at` must not follow the step's. Run
creation, queue time and total workflow age are ignored. A step is a candidate
only while the run is active and both the job and the step are `in_progress`
with no conclusion and no `completed_at`. A finished run's stale step metadata
is never used. The job's `run_id`, `run_attempt` and `head_sha` must match the
exact merge-group attempt. The job ID and step number must be positive
integers (not booleans), and the job ID must appear only once. Queued,
waiting and completed jobs and steps are never candidates. Neither are other
steps of a release lane, completed policy steps in long healthy jobs, `checks`
shards, native lanes or other workflows. The watcher already refuses main,
tag, manual and PR runs, other heads and superseded attempts. A duplicate step
or job, a mismatched identity, or a missing, naive or unparseable timestamp is
refused and recorded in that pass, and fail-fast continues. Discovery only
reads and never raises on this metadata, so malformed step data cannot
disable fail-fast.

When a step is overdue (strictly past its deadline), the pass:

1. Re-reads the live queue ref, the latest merge-group runs, the jobs of the
   watched attempt (`actions/runs/{id}/attempts/{attempt}/jobs`), and finally
   the run itself. It confirms the same ref, run, attempt, head, job, step
   number and step start are still in progress and overdue. A step that
   stopped or restarted is recorded as `refused-progressed`. A moved ref,
   replaced run, new attempt or changed head is recorded as `refused-changed`
   and nothing is cancelled.
2. Requests normal cancellation of that exact Buster CI run
   (`POST actions/runs/{id}/cancel`), after flushing a `cancel-requesting`
   record. GitHub cannot cancel a single job. When the same pass has already
   run fail-fast, that fail-fast cancel request is the normal cancellation
   and is not repeated. Completed job results are retained.
3. Waits inside the same pass, re-reading as in step 1 every
   `STEP_DEADLINE_POLL_SECONDS` (20 seconds). If the step stops, the pass
   records `step-stopped` and ends. If that same step is still in progress
   after `FORCE_CANCEL_GRACE_SECONDS` (2 minutes), the final re-read is
   immediately followed by a `force-cancel-requesting` record and **one**
   call to the documented
   [force-cancel endpoint](https://docs.github.com/en/rest/actions/workflow-runs#force-cancel-a-workflow-run)
   (`POST actions/runs/{id}/force-cancel`). A step or run that progressed or
   completed, or a changed identity, does not permit escalation. Neither does
   a `409` from the normal cancel. A `409` from either endpoint records
   `run-already-finished`. Any other HTTP or transport error fails the
   watcher visibly, and an uncertain POST is never retried.
4. When the pass did not itself run fail-fast, the cancelled run's
   completion event, or the next sweep, runs the existing fail-fast. That
   cancels the rest of the exact-head merge group.

Passes keep no state, so the force-cancel grace has to elapse inside one
pass. That is why it is 2 minutes rather than a longer cross-probe wait: it
must leave room for checkout and API reads within the watcher job's
five-minute timeout. `test_in_pass_grace_fits_the_watcher_timeout` enforces
that bound. The runner is held only while a revalidated overdue step is being
stopped.

Nothing is rerun and no success is published. A deterministic failure stays
failed, and the job's existing `actions: write` permission covers both
endpoints. Every decision prints one greppable, versioned `STEP_DEADLINE_V1`
line with the action: `refused`, `refused-progressed`, `refused-changed`,
`cancel-requesting`, `cancel-requested`, `step-stopped`,
`force-cancel-requesting`, `force-cancel-requested` or `run-already-finished`. Each line carries the
exact run, attempt, head, job, step, start timestamp, budget and grace, and
the deadline and elapsed seconds once the step is overdue. Records are flushed
before each POST, so an uncertain request still leaves its exact identity
behind. The job summary repeats these lines even when a later API error fails
the controller. Records report requests, not verified termination.

Limits:

- Deadlines are not hard wall-clock guarantees. Observer scheduling, API
  delays, GitHub outages or a failed observer can delay or prevent action.
  If several merge groups are stuck at once, one sweep may reach its
  five-minute timeout before handling them all. The next pass repeats the
  idempotent normal cancel and its own bounded wait.
- The cancel endpoints are keyed by run ID and have no
  "only if this attempt still matches" condition. The final reads narrow the
  rerun and completion race between the last read and the POST, but cannot
  remove it.
- Force cancellation can bypass cleanup guarded by `always()`, so artifacts
  from the stuck runner are not guaranteed. The controller's own records are
  flushed first for that reason.
- The deadline path cancels only the stuck Buster CI run. Other required
  workflows receive the ordinary fail-fast cancellation, never
  force-cancellation. Successful results are not rewritten.
- Recovering the interrupted queue candidate is an explicit operator action,
  not a retry loop. The candidate still lacks successful admission evidence.
- Protection starts only when this code is on the default branch and a
  watcher using that revision starts. Controllers that are already running
  are not changed.
- This protection does not diagnose why the affected runner did not report the
  step's own timeout, and it does not replace native reproduction once logs
  are available.

## Validation and escalation

Run `python3 tests/ci_recovery_test.py` for ordinary PR recovery,
`python3 .github/scripts/test_ci_recovery_workflows.py` for workflow selection and
attribution, then `python3 .github/scripts/test_merge_queue_fail_fast.py` for required-check
failure, optional-check exclusion, exact-head cancellation, and successful
completion. The same suite covers the step deadline: the 2026-09-29 incident,
each lane's boundary second, a future or finished start, progression and
replaced jobs or steps between reads, changed attempts, heads and runs before
either POST, queued/completed jobs, malformed or duplicate metadata, evidence
ordering, normal cancellation, fail-fast's cancel as the normal cancellation,
the in-pass force-cancel grace and its fit within the watcher timeout, sweep
passes, API errors, and budget drift from `ci.yml`. The tests neither sleep
nor send real cancellation requests.
These offline tests run for PR changes to the controller files and `ci.yml`.
They do not prove a live merge-group cancellation; the watcher can execute only
after its workflow lands on the default branch. Workflow lint covers the YAML.
After landing, capture an ordinary PR completion, a main push, and a merge-group
start/completion and sweep with their exact upstream and handler run/attempt IDs. Count
handler invocations, check records, assigned jobs and runner minutes separately
against the observed three-skipped-check baseline; verify the required CI and
preflight checks remain independent and blocking.
No local compiler build is needed for this workflow-only change.

If attempt 2 is cancelled again, retain its run/job URLs, UTC timestamps,
runner names, and annotations for GitHub Support. An organization owner can
also inspect the Actions audit log around those timestamps; the ordinary
run's actor/triggering_actor fields identify triggering/re-running actors and
must not be treated as proof of who cancelled it. Increasing runners addresses
capacity only after queue/capacity evidence, and changing concurrency is not
a demonstrated repair for these six original runs.
