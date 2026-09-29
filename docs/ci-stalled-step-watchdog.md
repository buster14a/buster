# Independent merge-group step watchdog

The existing [merge-group watcher](ci-cancellation-recovery.md#merge-queue-fail-fast)
executes `.github/scripts/recover-ci.py watch` from its trusted default-branch
checkout on an independent Linux runner. Besides observing completed failures,
it now checks a narrowly defined stalled-step condition. It does not create
another workflow, allocate another runner, or change token permissions.

## Incident and scope

[Issue #1866](https://github.com/buster14a/buster/issues/1866) records
[Buster CI run 36571009755](https://github.com/buster14a/buster/actions/runs/36571009755),
at candidate `fd1ebb286a90639535e654456084395aa92ab84c`, attempt 1.
The `macOS x86-64 release` job's `Workflow tool regression tests` step started
at `2026-09-29T12:53:23Z` and remained `in_progress` more than 30 minutes later,
although its configured step timeout was five minutes. The other 23 jobs
were successful at inspection. Downloadable logs were unavailable; the
underlying test, process, or runner failure has not been established.

The watchdog protects only `Workflow tool regression tests` in the six named
desktop **Release** jobs of same-repository `merge_group` Buster CI runs.
It does not infer a hang from time queued, total workflow age, a running build,
or lack of log output. Checks shards, other steps/workflows, ordinary PRs,
main/tag pushes, manual runs, completed jobs, and pending steps are excluded.
Existing completed-failure cancellation and bounded PR recovery remain separate.

## Deadlines and cancellation

`WORKFLOW_TOOLS_TIMEOUT_SECONDS` mirrors the `workflow_tools` step in
`.github/workflows/ci.yml`; update both when changing that budget or lane names.
`STEP_REPORTING_GRACE_SECONDS` allows another 120 seconds for normal timeout
cleanup and metadata propagation. Time is measured from the API step start.

| Release lanes | Workflow step budget | Observer cancellation threshold |
| --- | --- | --- |
| Linux x86-64 and AArch64 | 120 seconds | 240 seconds |
| macOS and Windows, x86-64 and AArch64 | 300 seconds | 420 seconds |

The existing observer polls every 30 seconds. At a threshold, it re-fetches the
latest exact-head Buster CI run, reads jobs from the **specific attempt**, and
re-reads the run immediately before mutation. Run ID, head SHA, branch,
workflow, repository, attempt, job ID/name, step name/number, and start timestamp
must still agree. Missing/malformed identity or timestamps cannot authorize a
cancellation; API errors remain visible rather than guessing.

Only the affected Buster CI run is targeted by this deadline path. The first
request is normal cancellation. After `STEP_CANCEL_GRACE_SECONDS` (120 seconds),
a fresh confirmation of the same overdue step permits **one** force-cancel
request. Completed/progressing runs and changed attempts/identities do not
permit escalation. A normal-cancel conflict (HTTP 409) stops the path without
force cancellation. Other HTTP or transport errors propagate; uncertain POSTs
are not retried. The controller reports requests, not unverified termination.

GitHub documents [normal and force cancellation](https://docs.github.com/en/rest/actions/workflow-runs#force-cancel-a-workflow-run)
as separate operations. Force cancellation is reserved for an unresponsive
normal cancellation; it can bypass cleanup guarded by `always()`. Consequently
artifact retention on the stuck runner is not guaranteed. The independent
observer flushes its evidence before the first mutation.

## Evidence, trust and limits

`CI_STEP_TIMEOUT_V1` records the run/head/attempt/job/step, start timestamp,
budget, threshold and elapsed seconds. `CI_STEP_TIMEOUT_CANCEL_V1` records an
accepted normal cancellation request; `CI_STEP_TIMEOUT_DISPOSITION_V1` records
the final decision. Successful controller completion adds that decision to its
existing lifecycle summary. Evidence already flushed to the log survives a
later controller API error even when a summary cannot be completed.

The watchdog never executes candidate code, consumes candidate artifacts,
publishes a successful check, reruns a test, or changes required checks. The
cancelled candidate still lacks successful admission evidence. Other required
workflows are not force-cancelled by this path, and successful results are not
rewritten. Recovery of the interrupted queue candidate remains an explicit
operator action, not an unbounded retry loop.

Thresholds are not hard wall-clock guarantees: observer scheduling, API delays,
GitHub outages and failure of the observer itself can delay or prevent action.
GitHub's cancellation endpoint is keyed by run ID, not a conditional attempt
identity. The final reads narrow but cannot eliminate a rerun/completion race
between the last read and the POST. The existing bounded watcher window and
job timeout remain in place.

The new protection becomes active only after this code lands on the default
branch and a watcher using that revision starts. It does not retrofit code
into an already-running controller. This protection is not a diagnosis of the
original macOS failure and does not replace native reproduction when logs
become available.

## Validation

Run the existing offline suites without network access:

```sh
python3 -B tests/ci_recovery_test.py -v
python3 -B .github/scripts/test_merge_queue_fail_fast.py -v
```

The watchdog fixtures simulate the recorded step identity/timestamps, all six
lane boundaries, queued/completed work, malformed metadata, duplicate records,
progress and identity changes before either write, normal cancellation,
escalation, conflicts and API failures. The older tests retain coverage of
required-check failures, optional-check exclusion, exact-head cancellation,
successful completion, and ordinary PR retry eligibility. No sleeps or real
cancellation requests are made by these tests.

The existing `CI lifecycle policy regression` workflow runs these tests and its
workflow-shape suite when either controller file changes. Hosted CI and a
post-merge watcher trace are distinct evidence: offline passing tests do not
prove a live runner can be cancelled or that this protection is deployed.
