# Stateless validation concurrency

The benchmark-service policy and both disposable systemd gate workflows run
read-only validation on independent GitHub-hosted runners. They do not publish
shared controller state or dispatch work to the physical benchmark host.

## Retention and candidate behavior

For these three workflows, every `push` invocation uses an event-specific,
run-ID-qualified concurrency group. A later main push can therefore neither
cancel its active predecessor nor replace a pending predecessor. Using only
`cancel-in-progress: false` would protect active work but still let the third
invocation replace the second pending one. Using a SHA rather than a run ID
would also coalesce duplicate invocations of the same commit.

Benchmark-policy PR revisions retain per-PR cancellation, and merge groups
retain per-queue-ref cancellation. Explicit benchmark-policy dispatches have
independent run-ID groups. Event and workflow prefixes prevent cross-event
and cross-workflow collisions.

Both systemd workflows retain their existing non-cancelling PR policy, path
filters, checkout identity, timeouts, cleanup and evidence retention. This
change does not add merge-group or manual triggers to either systemd workflow.
Their obsolete pending candidate runs may still be coalesced; only main-push
retention is changed. Real failures and external cancellations remain visible.

## Validation and limits

Run `python3 -B tools/bench_service/workflow_concurrency_test.py -v` from the
repository root. The benchmark-policy job runs the same regression before its
existing validation. It reads the actual workflow concurrency blocks, evaluates
their bounded scalar/equality/AND/OR expressions without executing workflow
code, and covers three overlapping same-SHA invocations, manual isolation,
candidate coalescing, distinct PR/queue references and cross-workflow isolation.
Negative controls reject both active cancellation and pending-slot replacement.
This is an offline policy regression, not a controlled live scheduling race.

Shared-state controllers are a separate contract: this change does not modify
native-retirement catch-up, automation or merge-queue reconciliation. Their
bounded queue-retention follow-up remains tracked by #2159/#2160 and must use
the applicable protected integration route. See
[main-push maintenance](main-push-maintenance.md) and
[native-retirement rebinding](native-retirement-rebinding.md).
