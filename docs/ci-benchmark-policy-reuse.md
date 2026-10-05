# Benchmark-policy reuse and reconciler triggers (#2462)

The independent `Benchmark service workflow policy` job may reuse its completed
merge-queue execution on an **identical-SHA, first-attempt main push**. This is
separate from [Buster CI reuse](ci-main-reuse.md). It does not extend reuse to
self-host, GPU, API-migration, retirement or other independent workflows.

## Work and authority

The required job name, runner and five-minute timeout stay unchanged. No job is
added. `Require CI admission to be enabled` always executes freshly, followed by
the stateless-concurrency and small offline reuse regression suites. Contents and Actions permissions are
read-only; the helper uses the existing bounded GET transport in
`merge_queue_admission.GitHub`. It never publishes a check or changes a ref.

Only these original steps can skip after a positive reuse decision:

- fixed-gateway dispatch policy and its Python controls;
- Zen 5 qualification and micro-architecture lab format controls;
- atomic exclusive admission and broker-group controls;
- static credential-gate build and credential/account rejection controls.

Their original commands, flags and assertions are unchanged. They inspect or
exercise the checked-out repository and local disposable test outputs; they do
not deploy, dispatch a physical benchmark, publish a cache, or write repository
state. The live CI-enabled variable is deliberately outside reuse. The source
run records the actual hosted runner and compiler environment: reuse certifies
that execution, not that a hypothetical second run would resolve identical
latest tools. No host-performance qualification is inferred from these tests.

## Decision and finalization

`tools/bench_policy_reuse.py decide` requires a unique source run from the
workflow-scoped, exact-SHA `merge_group` listing. A full one-row listing must
agree with direct run lookup. It verifies repository name and numeric identity,
workflow ID/path, exact checkout SHA and workflow Git blob, main ref/event,
queue ref, first attempts, successful terminal source execution, and completion
no more than two hours before the main run was created.

The source must have exactly one successfully executed policy job with matching
run/attempt/head/branch, the declared runner label, a valid execution interval,
and unique step names/numbers. Every named step declared in the exact workflow
must be present, including additional controls added by other changes; each
required test/control step must have succeeded. No extra failed or skipped step is accepted. The only skipped source
step is finalization itself: merge-group runs execute fresh tests, never reuse.
Discovery and direct source/current-run reads are repeated around collection.
Incomplete pages, duplicate runs/jobs, missing proof, API errors, reruns and
changed identities select the **original full test execution**, not a pass.
PRs, merge groups, manual dispatches, foreign contexts and explicit reruns do not
perform reuse discovery at all. There is no polling or cross-event cancellation.

The decision writes a bounded, SHA-256-bound JSON receipt in `RUNNER_TEMP` before
emitting `reused=true`. No summary claims verification at that point. After the
original test steps skip, `finish` reconstructs the proof and compares it with
that exact receipt. A replaced/rerun source, changed job proof, missing/tampered
receipt or unavailable API now fails the job; it cannot fall back after tests
have skipped. A successful finalization links the source run/attempt and retains
the receipt in the main job summary and log, explicitly distinguishing prior
execution from newly run controls. The source job API and logs are the original
execution evidence; this workflow has no required source artifact contract.
This is point-in-time verification, not an immutable guarantee against a user
rerunning the source after the main job has finished.

The [stateless concurrency fix](ci-stateless-concurrency.md) remains independent:
its event/run-ID keys retain main invocations instead of cancelling them. Its
regression runs freshly on main and must also be present and successful in the
source evidence. Integration order is #2463, then this reuse change.

## Reconciler noise

`merge-queue-reconcile.yml` filters **upstream** `workflow_run` branches to
`gh-readonly-queue/main/**`, before creating a workflow run. It keeps all seven
completion sources, the existing job-level `merge_group` guard, trusted-main
checkout, serialized sweep and main-push/manual/scheduled recovery triggers.
The filter is not applied to the reconciler's own main checkout. Relevant queue
completions may still produce multiple bounded reconciliation passes; this
change removes irrelevant PR/main completion wakeups, not necessary queue work.

## Validation and observation

Run `python3 -B tools/bench_policy_reuse_test.py -v`. The existing policy workflow
runs these controls on every event, including main reuse. They cover exact
success, wrong/stale/moving source identities, incomplete/duplicate listings,
step omissions/failures/skips, reruns, receipt tampering, API uncertainty,
fallback/finalization, and the actual YAML gates and upstream branch filter.
Existing actionlint and benchmark/admission policy checks remain required.

For live acceptance, record the new source/main run, attempt and job IDs. Verify
all seven original test steps execute on the queue and skip only after a positive
main decision, finalization succeeds, and the fresh CI-enabled/control steps
execute. Check an explicit dispatch or rerun executes the full original tests.
Distinguish repeated job names from repeated tests and compare actual job busy
intervals rather than assuming saved runner minutes. Verify nonqueue upstream
completions no longer create reconciler runs while queue/recovery wakeups work.
Unit controls alone do not establish this hosted transition or measured savings.
