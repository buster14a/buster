# Main-push maintenance and supersession

Two required workflows have default-branch side effects in addition to their
candidate checks:

- `API migration policy` invalidates trusted native-retirement integrations
  after `main` advances.
- `Merge conflict preflight` refreshes every open pull request against one exact
  default-branch snapshot.

Their pull-request and merge-group jobs are admission checks. They remain
fail-closed and may cancel an obsolete run for the same candidate. A `push` to
`main` has different semantics: a later merge supersedes the maintenance input,
but it must not turn an otherwise green historical commit into a cancelled run.

## Concurrency and ordering contract

For `push`, each workflow uses a concurrency group containing the exact
`github.sha` and sets `cancel-in-progress` to false. Three rapid pushes therefore
create three distinct groups; GitHub cannot cancel a running invocation or
replace an older pending invocation.

Exact groups permit jobs to start out of order, so
`tools/main_push_maintenance.py` supplies an explicit predecessor barrier. Before
a push mutates shared state, it lists earlier push runs of the same workflow and
waits for the same maintenance job in each run to finish. It does not wait for
unrelated jobs in those workflows.

This gives the sequence a single writer order without trusting GitHub
concurrency's one-pending-run slot:

1. The oldest active push performs its action or becomes obsolete.
2. A newer run waits for that maintenance job to finish.
3. Once unblocked, an intermediate event whose SHA is no longer live `main`
   finishes successfully as `superseded-before-action`.
4. The newest event runs with the maintenance implementation from its exact
   push revision and makes the final publication. Non-push preflight routes
   continue to execute trusted current-main code.

If `main` moves while an action is running, that action may retain partial
evidence, but it finishes before its successor is allowed to publish. The older
run verifies that an exact-SHA successor run exists and records
`superseded-after-action`; it never replays newer-main maintenance with older
workflow code. The successor therefore cannot be overwritten by an older run.

For every non-push event, the concurrency group remains candidate- or
event-specific and `cancel-in-progress` stays enabled. This preserves exact-head
admission and does not convert real failures or arbitrary cancellations to
success.

The native-retirement workflow also pins the SHA-256 of
`tools/main_push_maintenance.py` and verifies it from the exact combined tree.
Changing the helper without changing that trusted workflow fails the policy
job; changing both is classified through the existing trusted bootstrap path.

## Failure and evidence contract

Ordering waits are bounded. A missing predecessor job, an older job that never
finishes, or a moved `main` without an exact successor run is red rather than
silently skipped.

A merge-conflict refresh is neutral after movement only when its retained report
says the sole active failure is the existing default-branch-moved recheck. API
errors, publication ambiguity, incomplete inventory, exhausted budget and
script/test failures remain red. Native-retirement invalidation exceptions also
remain red.

The helper retains `main-push-maintenance.json`:

- a superseded push records event/current SHAs and its successor;
- a current run records the exact action revision;
- ordering records every predecessor run it waited for;
- an obsolete merge-conflict attempt remains under `attempt/`, while only a
  current or genuinely failed attempt is promoted to the artifact root and job
  summary.

Manual `workflow_dispatch` refreshes are not commit-supersession sentinels and
continue to run the ordinary snapshot tool directly.

## Native-retirement catch-up and automation controller

`Native retirement catch-up` and `Native retirement automation` also run on
every `push` to `main`, and they run only trusted code at `github.workflow_sha`.
Their controller commands `catch-up` and `plan` stop with the stale-main
`AutomationMoved` (exit code 75, `integration.STALE_MAIN_EXIT`) when `main` has
already advanced. Examples are `main moved before automation authorization`
and the equivalent read-policy and catch-up-opening checks. They stop before
any claim, catch-up commit or PR is written.

`native_retirement_controller.superseded_push` reuses this helper's successor
proof (`wait_successor`). A push run finishes green as `superseded` only if all
of these hold:

- The event is `push`, and the command is `catch-up` or `plan`.
- Live `main` differs from the run's `GITHUB_WORKFLOW_SHA`.
- A later push run of the same workflow file exists, with a higher run number
  and a `head_sha` equal to live `main`.

The job summary records the event/current SHAs and that successor. Every other
outcome stays red:

- A stale-main exit while `main` still equals the run's SHA.
- No exact successor run within the bounded wait.
- Any API or inventory error.
- A non-push event.
- PR movement (exit code 76).
- A disabled or invalid policy, or any other policy violation.
- The automation workflow's `dispatch` step, which follows a persisted claim.

Both workflows keep a single shared concurrency group with `queue: max` and
`cancel-in-progress: false`. This preserves one active writer while retaining up
to 100 pending invocations instead of cancelling an intermediate main push.
GitHub processes the queue by when each invocation begins waiting; event-delivery
order is not guaranteed. Existing exact-main and successor checks remain required.
Queue overflow, manual cancellation and infrastructure failures remain failures;
no terminal conclusion is rewritten.

Regressions: `SupersededPushTests` in
`tools/native_retirement_controller_test.py`.

## Validation

The focused regression is:

```sh
python3 -B tools/main_push_maintenance_test.py -v
```

It covers three rapid main revisions, queued jobs that have not materialized,
waiting only for the relevant predecessor job, exact successor identity,
mid-action movement, stable evidence promotion, generic failure preservation,
and the event-specific workflow policy.
