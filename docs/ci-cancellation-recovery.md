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

`.github/workflows/ci-recovery.yml` observes `Buster CI` lifecycle events. It
becomes active only after landing on the default branch, with the existing
`GH_ACTIONS_CI_ENABLED=true` setting. Completed ordinary PR runs retain the
bounded recovery behavior below. An in-progress merge-group run instead starts
the trusted fail-fast watcher described in the next section. Neither path
modifies compiler/build policy, runner labels, coverage, or merge requirements.

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

For a `merge_group` Buster CI run, the same default-branch controller starts one
bounded watcher with job-scoped `actions: write` and `checks: read`. Every 30
seconds it reads the live main ruleset's required-check names, exact-head
merge-group run identities, and the corresponding GitHub Actions check runs.
It also reads Buster CI jobs so a failed shard need not wait for `CI complete`.
The first completed non-success required check or Buster CI job invalidates the
group. The watcher then requests cancellation of every active Actions run with
the same merge-group head SHA and `merge_group` event. Optional check failures
do not trigger cancellation; completed or different-head runs are never targeted.
It keeps watching after Buster CI succeeds and stops only when every required
check has succeeded. The bounded watch lasts at most five hours.

On `merge_group`, Buster CI's desktop and mobile matrices, plus the independent
rebinding and materializer matrices, use native matrix `fail-fast`. The twelve
desktop shards first wait for the cheap workflow-lint job; ordinary PR, main,
tag, and manual runs still execute after a lint failure for diagnostics. The
Buster native matrix retains `fail-fast: false` under the frozen CI test
contract, but its first failed job still triggers the trusted watcher. The separate required
workflows have no shared `needs` dependency, so the watcher closes that gap.

The watcher is intentionally not implemented inside candidate-controlled
`ci.yml`: merge-group candidate code retains read-only permissions. The
write-capable controller executes `github.sha` from the default branch and
uses only GitHub event/API metadata. Race responses indicating a run already
finished are treated as a no-op; other API errors remain visible failures of the
controller rather than authorization to continue or fabricate success. The
watcher is a cost-saving controller, not a required status check; the existing
eight required checks remain the authority for merge admission.

## Validation and escalation

Run `python3 tests/ci_recovery_test.py` for ordinary PR recovery and
`python3 .github/scripts/test_merge_queue_fail_fast.py` for required-check
failure, optional-check exclusion, exact-head cancellation, and successful
completion. These offline tests run for PR changes to the controller files.
They do not prove a live merge-group cancellation; the watcher can execute only
after this workflow lands on the default branch. Workflow lint covers the YAML.
No local compiler build is needed for this workflow-only change.

If attempt 2 is cancelled again, retain its run/job URLs, UTC timestamps,
runner names, and annotations for GitHub Support. An organization owner can
also inspect the Actions audit log around those timestamps; the ordinary
run's actor/triggering_actor fields identify triggering/re-running actors and
must not be treated as proof of who cancelled it. Increasing runners addresses
capacity only after queue/capacity evidence, and changing concurrency is not
a demonstrated repair for these six original runs.
