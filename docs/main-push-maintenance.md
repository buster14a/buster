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
waits for the same maintenance job in each run to finish. Discovery is owned by
`tools/main_push_inventory.c`; the existing Python adapter transports API reads
and invokes the unchanged maintenance actions. This is a bounded migration slice
under #2467, not a new Python controller. It does not wait for
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
The pin also covers the native discovery source and its shared JSON parser.
Changing any of these inputs without changing that trusted workflow fails the policy
job; changing both is classified through the existing trusted bootstrap path.

## Bounded discovery (#3412)

The native helper requests each potentially nonterminal status independently:
`queued`, `in_progress`, `waiting`, `pending`, and `requested`. There is no
creation-date cutoff: an old pending run remains visible despite newer completed
history. Runs can move between status buckets; duplicate identities across buckets
are joined conservatively and the newest attempt is retained. The existing two
clear observations remain required before publication; API reads are observations,
not an atomic lifecycle snapshot.

Every filtered inventory must supply an integer `total_count`, exact page
cardinality and unique run/job IDs within each inventory. Exactly 1,000 relevant
rows are complete only after ten full pages with that declared count; completed
history never enters these queries.

For #4019, native discovery classifies changed totals and count/list mismatches
as recoverable observations. It restarts the complete discovery, at most four
attempts, with 1/2/4-second backoff. Missing/wrong-type fields, malformed JSON or
parser bounds, invalid rows/identities, duplicate IDs/numbers and exceeded bounds
are terminal. A rejected page never proves completeness. Valid run identities
observed even on a count-rejected page remain candidates for direct rechecking.
The original failed response was not retained: the first-page mismatch fixture
reproduces one possible predicate, not the incident's proven cause.

Each predecessor discovery performs complete forward and reverse active-bucket
sweeps and compares their relevant identity sets, rather than only their counts.
A changed set restarts discovery. The union of observed identities survives
recovery and subsequent helper invocations in the same ordering wait; an older
observation cannot replace a newer attempt. Every retained predecessor is checked
directly even if both later sweeps omit it. An omission therefore cannot erase
an observed active predecessor, and a completed observation is reread to detect a
rerun. Page shifts and status movement may cause another bounded attempt or a
failure, never acceptance of the rejected page. These API observations do not
claim an atomic snapshot under arbitrary concurrent movement: opposite-order
coverage, identity reconciliation, direct current-attempt checks and the existing
two clear observations together form the publication barrier. There is no recent
history cutoff or inference that equal cardinalities mean equal identities.

Requests are bounded to 200 across all helper invocations and discovery phases
of an ordered maintenance operation, ten pages per inventory and 5,000 retained
run identities. A helper restart receives the used request count and retained
identities; it cannot renew the request allowance. Repeated inconsistency exhausts
the four-attempt bound, and persistent pending runs can exhaust the request bound
before the wait deadline. The enclosing wait deadline includes recovery backoff
and applies to the existing HTTP reader. Direct callers have a 60-second discovery
budget. Reads are checked before and after transport; a transport without a
deadline hook can overrun by its final per-GET bound, but the overdue response
cannot authorize publication.

Target jobs are read for the run's exact attempt and the run is reread afterwards.
An absent target or inherited job from an earlier attempt remains blocking while
the run is active. A terminal run can be released; an attempt change keeps the
barrier closed. Unrelated unfinished jobs do not block a completed current-attempt
target.

Successor discovery includes `head_sha` in the workflow/event/main-branch query.
Every returned identity is independently checked for the required head, event,
branch and strictly higher run number. No unrelated recent run proves supersession.

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
- a discovery/reconciliation/action exception records a failed phase, action-attempt
  flag, run ID/attempt, event SHA, workflow/action and safe query/page accounting.
  Unknown current-main identity is null, and an unattempted action is never reported
  as a successful refresh. API response bodies and credentials are not retained;
- an obsolete merge-conflict attempt remains under `attempt/`, while only a
  current or genuinely failed attempt is promoted to the artifact root and job
  summary.

Native rejection records include the precise reason/field, safe path/filter/page,
request and attempt numbers, expected/observed totals, returned row count and
retryability. Unavailable counts are JSON null. Earlier rejected observations
remain in successful recovery reports too. Final evidence identifies terminal,
attempt-bound or request-bound stopping, and adapter deadline failures retain the
observations already received. The job log and summary print the same bounded
diagnostics, including failures before any maintenance action. The JSON retains
the conservative candidate identities; logs/summaries omit that longer list.

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

The focused hosted regression compiles the native discovery tool first:

```sh
clang -std=c11 -Isrc -O2 -Wall -Wextra -Werror -Wno-unused-function -fwrapv -fno-strict-aliasing -funsigned-char tools/main_push_inventory.c -o tools/main_push_inventory
tools/main_push_inventory --self-test
python3 -B tools/main_push_maintenance_test.py -v
```

It covers three rapid main revisions, queued jobs that have not materialized,
waiting only for the relevant predecessor job, exact successor identity,
mid-action movement, stable evidence promotion, generic failure preservation,
and the event-specific workflow policy. Native synthetic controls cover completed
histories of 999, 1,000 and more than 1,000 runs, all five active states, missing
target metadata, rerun attempts, terminal transitions, exact filtered pagination
and malformed/incomplete inventories. The hosted workflow also runs the native
controls under GCC ASan/UBSan. Local agent execution is not required.
The #4019 native controls additionally cover first-page recovery, persistent
inconsistency, changed totals and identities across pages, status-bucket movement,
retention after omission, job-inventory recovery, duplicate/invalid identities,
inherited/current-attempt jobs, completed-run reruns, exact-successor recovery and
request exhaustion with retained input from an earlier helper invocation. Existing
adapter/preflight and invalidation/automation tests retain the main-movement and
no-publication-on-discovery-failure controls.
