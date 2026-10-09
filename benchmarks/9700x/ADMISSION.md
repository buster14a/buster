# Admission for the direct 9700X workload workflow

The Ryzen 7 9700X is not a general Actions executor. Exactly one workflow may
reach it: `.github/workflows/9700x-direct-bench.yml`. It compiles and runs the
owner's own pull-request workloads (#2704). It compares the compiler of an
owner pull request with its merge base on request (#2769), and the compiler
of each commit that lands on main with its first parent's, or with the
nearest earlier measured main commit's after a merge burst (#2752). Its hosted
markers, `.github/workflows/9700x-direct-request.yml` and
`.github/workflows/9700x-compiler-request.yml`, never select the runner. The queued benchmark service
and its dispatch workflow are removed (#2708). No other workflow may select
the runner, and `tools/bench_direct/workflow_policy_test.py` fails the required
"Benchmark service workflow policy" check if one does. That check keeps its
historical name because the `main` ruleset requires it by name.

## Scheduling policy

Routine 9700X measurements run only after the measured commit lands on `main`
(#3087). This supersedes #2752's original automatic merge-queue proposal;
its existing post-merge main comparison remains asynchronous and report-only.
Neither that comparison nor RAD Debugger compatibility is a pre-merge required
check in the main ruleset or the merge-admission workflow inventory.

The only PR exception is an affirmative owner request for an experiment or an
explicit task/issue requirement for applicable performance evidence: a
workload/data change, `compiler-compare.request`, or `scaling.request` carries
that request through the existing gate. Routine bug fixes, CI repairs, conflict
resolution and branch refreshes do not by themselves authorize such a change.
Record the request/acceptance reference in the handoff; an already authorized
request needs no second unrelated manual approval. See the
[request decisions and examples](../../docs/agents/benchmarking.md#request-decisions-and-examples).
The trusted authorizer verifies
both the complete PR file inventory and a fresh request-file change at the
exact head relative to **every parent**. Compiler/scaling markers also need a
new request line present in every parent diff: merging old marker histories
alone is not a renewed experiment. An unrelated update, generic invocation
or merge that merely inherits a request from main cannot replay it. To request
a deliberately requested new candidate comparison, add a fresh request line
in the new head commit. A changed head alone is not a renewed request.
Before requesting again, read the exact-head check and its matching run/attempt
and receipt. Reuse complete published evidence only for its original source,
binary, baseline, workload, profile and configuration identities; stale prose
saying "queued" is not a rerun reason. Different inputs or a new head need new
evidence for a performance claim, within the applicable experiment or hold.
The authorizer log records the head and request paths; the host gate requires
that same request head and run attempt. The workload/configuration and hardware
remain bound by the existing receipts and harness.

The GitHub compare endpoint supplies at most 300 changed files. A head-parent
diff at that limit, a malformed record or an unavailable comparison fails
closed; make the explicit request in a smaller follow-up commit. For a merge
head, the request path must differ from both parents, so resolving main into
a branch without a new request does not consume the host.

Every performance claim still needs actual relevant Zen 5 evidence (#2761).
A skipped PR benchmark leaves performance validation incomplete. Report normal
correctness/policy CI, validation of the performance claim, and task acceptance
separately. Moving routine measurements after merge does not waive requested
acceptance measurements or release another issue's pre-merge hold. A source-path
classifier proves machine-verifiable routing/provenance, not human intent;
the agent guidance governs whether a request should be created.

## Gate

`.github/workflows/9700x-direct-bench.yml` starts on `workflow_run`, when
`.github/workflows/9700x-direct-request.yml` completes. The request workflow
runs on `pull_request` for changes to `benchmarks/9700x/*.c`; it is a hosted
marker job that checks out nothing and holds no capability. There is no manual
dispatch and no approval. `workflow_run` executes the bench workflow's
definition from `main`, so a pull request cannot edit the gate and the run
matches the runner group's `@refs/heads/main` pin. An edited copy of the
request workflow in a pull request gains nothing, because the gate reads who
started that run from GitHub's record. A `push` or `pull_request` trigger on
the bench workflow itself would run the branch's own copy and must not be used
for this host; the repository-wide ban on `pull_request_target` stays intact.
To run a workload from a branch, open a pull request from it; a draft is
enough.

Both jobs require the repository variable `BENCH_DIRECT_ENABLED == 'true'`, a
successful request run for a `pull_request` event whose head repository is
this repository, and `davidgmbb` by login and numeric ID 39247043 as that
run's actor and triggering actor. A re-run of the bench workflow must also be
triggered by `davidgmbb`. The hosted `authorize` job checks out only `main`'s
`tools/bench_direct` and runs `authorize.py`, which re-reads the request run
through the API and requires exactly one open pull request for its head
commit, authored by `davidgmbb`, with head and base in this repository. Only
then does it emit the run attempt and the pull request's base commit, and
`bench` is bound to an authorization from the same attempt. A request from any
other account, from a fork, or for another author's pull request skips or
fails before the self-hosted runner. GitHub reuses the outputs of jobs that
succeeded in an earlier attempt, so a maintainer re-run
must use **Re-run all jobs**. An agent that pushes with the owner's
credentials is the owner for this gate.

`bench` receives no token capability, no secret and no environment. It checks
out `main`'s `tools/bench_direct` as the trusted harness and the pull request
head's `benchmarks/9700x` as data, both without persisted credentials, then
runs only `trusted/tools/bench_direct/run_workloads.py`. The pull request
supplies C source that is compiled and executed as the runner account. That is
the intended capability, and it is why the author gate is the whole control:
there is no containment, host lease or sealed result. Do not widen the author
list.

`authorize` also reads the pull request's changed files. `bench` runs only
when a workload or its `.data` file changed. `compare-pull` and `publish-pull`
run only when `benchmarks/9700x/compiler-compare.request` was added or changed.
For that comparison `authorize` resolves the merge base with the base branch
and both trees from GitHub's records. `compare-pull` has the same
restrictions as `bench`, but checks out the whole pull request head (full
history, contents on demand), because it builds the compiler at the merge
base and at the head. That executes the pull request's build as the runner
account under the same owner-only gate. `start-pull` and `publish-pull` are
hosted and are the only jobs of that path with `checks: write`. Its check
name, `9700X compiler benchmark (pull request)`, and marker prefix,
`buster-9700x-compiler-pr-v1:<head>`, differ from the main comparison's.

### Full Clang analyzer profile

The fixed `clang-analyze-full-v1` profile uses this same owner-authorized
`compare-pull` route after its trusted harness is on `main`. The exact request
line is `profile: clang-analyze-full-v1` in
`benchmarks/9700x/compiler-compare.request`. Append one occurrence in the
exact head commit for each explicit run; a historical occurrence does not
replay the profile. The trusted harness requires exactly one new occurrence
relative to every parent, and the hosted publisher rechecks that delta against
GitHub's request-file contents and compares the retained bytes with the exact
head. The normal request gate still requires a fresh file change in the exact
PR head. No workflow, runner label,
hardware allowance or request identity is added. The baseline driver from
trusted `main` owns the native phase order, arguments, limits and process
accounting, and launches separately built baseline and candidate analyzer
drivers against one candidate-HEAD Release split-source compile database.

The fixed workload selects 182 inventory rows (135 candidate executions and
47 proven aliases), uses eight shards and two jobs, and retains two independent
preflights plus the matched baseline/candidate/candidate/baseline full runs.
Each full run has a separate aggregate verification. The existing 90-minute
job timeout is unchanged: setup is bounded to eight minutes, the native
campaign to 75 minutes, and two minutes are reserved for evidence export, with
five minutes left for workflow checkout, startup and upload. The publisher
accepts evidence only when the full inventory, diagnostics, driver provenance,
wait4 observations and every full-run process-tree sampler completeness
record revalidate from retained raw files. Sampled tree RSS remains sampled;
wait4 RSS is only the largest individual high-water and cannot establish a
simultaneous process-tree peak. The result reports process observations and
completeness without a performance verdict.

Every compiler receipt must record the observed CPU model of the host that
measured it. The harness refuses to measure, and the publisher refuses to
accept, a receipt whose CPU is not the AMD Ryzen 7 9700X (#2761).

The actor restriction governs who starts the workflow, not who edits its
definition. Changes to the workflow, its harness or this policy test need
owner review before they reach `main`.

## Main compiler comparison

`9700x-compiler-request.yml` runs on every push to `main`. Its `request` job
is a hosted marker with no permissions and no checkout; its `announce` job
creates the queued check (see [Check and commit report](#check-and-commit-report)).
The run's completion starts the main jobs of the bench workflow from `main`,
only while `BENCH_DIRECT_ENABLED` and `BENCH_COMPILER_ENABLED` are both `true`. The commit is measured after it
landed, against its baseline (its first parent, or a range baseline; see
[Range baseline](#range-baseline)), so merging never waits for the 9700X:

- `authorize-compiler` (hosted, read-only) runs `authorize_compiler.py`. It
  re-reads the request run (`push`, branch `main`, success, this repository),
  the commit (one or two parents), its first-parent chain, the chosen
  baseline on it, both trees, and that the
  commit is still on main (main equals it or descends from it). Main is
  trusted code, so there is no author gate: everything that lands is measured,
  bot-authored catch-up pull requests included. A queue merge's second parent
  and pull request number are recorded for the report (`0` for a direct push).
- `compare` (the 9700X, no token capability) checks out `main`'s `tools` and the
  commit with its history but no blobs (`filter: blob:none`), without
  persisted credentials, verifies the baseline is on the commit's first-parent
  chain, and runs
  `compiler_compare.py --mode main`. It builds tests-off Clang Release `ide`
  binaries of the baseline, then the commit, then the baseline again
  for the frozen workload's generated closure, and runs `tools/uarch_lab.py
  compare` with the frozen `compiler-compare-v1` profile
  (`compiler_receipt.PROFILE`). The evidence artifact
  `buster-9700x-compiler-<head>-<attempt>` keeps the receipt, the lab's raw
  pairs, metadata and `summary.json`, and both CMake caches for 90 days. It
  drops compiled outputs, per-run binary copies and perf data.
- `publish-compiler` (hosted, `actions: read` and `checks: write`) runs
  `compiler_publish.py`. It reads that artifact through the API as bounded
  data, requires its identities to equal this attempt's authorization, and
  re-derives validity from the lab's own `summary.json`. It then completes
  the attempt's check run, `9700X compiler benchmark`, on the main commit
  (external ID
  `buster-9700x-compiler-main-v1:<head>:<request run>.<request attempt>:<attempt>`;
  attempts before #2803 used `buster-9700x-compiler-main-v1:<head>`):
  - `success`: a valid core measurement, faster, slower or not detectably
    different.
  - `failure`: missing, mismatched or invalid evidence; the commit was not
    benchmarked.

The performance verdict is report-only. Nothing gates merging on the check.
`BENCH_COMPILER_REGRESSION_POLICY` is unset or `report-only`; any other
value, including a future `enforce`, fails closed until regression thresholds
are qualified and a separately reviewed rollout adds them.

Host time is bounded, not every commit is guaranteed a measurement. All main
comparisons share one concurrency group that never cancels a measurement in
progress; GitHub keeps only the newest pending run in a group, so during a
burst of merges the commits between the running and the newest one stay
unmeasured. Their `9700X compiler benchmark` check is completed as **Not
measured** (`skipped`) when the next main comparison starts, which is the
visible gap. A comparison takes about 13 minutes (three builds of about 55 s, then
about 10 minutes of pairs), so merges more often than that are sampled. The
host runs one job at a time because the group holds one runner, so main
comparisons, pull-request comparisons and workload runs never overlap.

### Range baseline

Sampling must not leave a landed change outside every comparison. So
`authorize-compiler` (with `checks: read`) walks the commit's first-parent
chain, at most 15 commits (`compiler_github.RECONCILE_DEPTH`), and takes the
nearest commit whose own `9700X compiler benchmark` check completed `success`
(`compiler_github.measured`, `authorize_compiler.choose_base`). Normally that
is the first parent, and nothing changes. After a burst, or after a failed
measurement, it is an older main commit, and the comparison spans the range
between them. Every unmeasured commit in the range is then covered:

- The check, the commit report and the receipt name the range: `range` is
  the number of first-parent commits it spans (`1` is the first parent alone)
  and `first_parent` the commit's own first parent
  (`compiler_receipt.range_label`). The publisher shows the authorized range,
  and refuses a receipt whose host-recorded `coverage` disagrees with it.
- `start-compiler` names that comparison on each skipped commit inside the
  range: its check still reads **Not measured**, because it has no
  measurement of its own.
- A range result covers the whole range and does not isolate which commit
  caused a change. To attribute it, request a pull-request comparison or run
  `tools/uarch_lab.py compare` on the commits in question.

Without a measured commit within reach (the first comparison after enabling,
a long outage, or a run of failures longer than the bound), or when the check
listing cannot be read, the first parent is the baseline and the older
commits stay uncovered. A forged check could only move the baseline to
another main commit on the chain, which is trusted code. The authorizer, the
host harness and publication-only recovery each require the baseline to be
on the commit's first-parent chain, never on a merged pull request's side.

## Check and commit report

The comparison is visible on the measured commit before it finishes (#2803)
and leaves a readable report there (#2804). Every write is hosted, runs
`main`'s code, and holds only the permission it needs; the 9700X never
receives a token. The measured commit is always `BQ_HEAD_COMMIT`, the
request's verified head, never the trusted harness revision `github.sha`.

- **Queued.** `announce` in `9700x-compiler-request.yml` (`checks: write`)
  runs `compiler_github.py announce` and creates the check on the pushed commit
  before the bench run exists, so the wait for the main concurrency group is
  visible. It never fails the request run (`continue-on-error`), so it never
  decides whether the comparison starts. For a pull request, `start-pull`
  creates the check after authorization; that path has no workflow-level
  wait, because a new push cancels the older run.
- **Queued setup and live execution.** `start-compiler` / `start-pull` share
  the host job's authorization gate. Each creates or adopts this exact
  attempt's queued check in one short hosted pass, performs the existing
  bounded reconciliation, and exits. Neither waits, sleeps or polls for the
  9700X scheduler. The custom check can remain queued while the host is
  running: the linked Actions `compare` / `compare-pull` job is the
  authoritative live record of runner wait, preparation and measurement.
  A successful setup job proves bookkeeping only, not that a measurement
  started or passed. Its log records `api_requests` and
  `control_execution_seconds`.
- **Completed.** `publish-compiler` / `publish-pull` complete the same check:
  success for a valid measurement, failure for a refused authorization or
  missing or invalid evidence, neutral for a superseded pull request head.
  The check summary links the workflow attempt and the evidence artifact
  explicitly, because GitHub may not honour `details_url` for checks written
  with `GITHUB_TOKEN`.
- **Ownership and retries.** A check is ours only when the GitHub Actions app,
  the exact name, the exact head and the exact attempt marker
  (`compiler_receipt.attempt_marker`) all match; a same-name check of another
  app or attempt is never adopted. A lost create response is resolved by
  looking up again before a second create. Statuses only move forward, and a
  completed check is never rewritten, so a late or repeated older attempt
  cannot replace a newer result. A deliberate re-run of either workflow is a
  new attempt with its own check; GitHub shows the newest.

  Keep the request workflow's run/attempt distinct from the benchmark
  executor's run/attempt. Recovery re-reads the exact attempt endpoints and
  binds the executor's trusted run name,
  `9700X request REQUEST.ATTEMPT head HEAD`, to the original request and
  measured head. Its own recovery run is separate bookkeeping provenance;
  it does not replace either original identity. All six hosted check-writing
  jobs serialize under `buster-9700x-check-writer`, with
  `cancel-in-progress: false` and `queue: max`. Completed checks are immutable:
  duplicate, delayed or out-of-order setup/recovery cannot reopen them or
  overwrite a newer attempt. Queue saturation beyond GitHub's 100 pending
  jobs is visible cancellation and incomplete validation, never success.
- **Orphans and cancellation.** The existing bounded first-parent/range and
  earlier-PR-head reconciliation remains a backstop. New announce/setup
  output carries the stable `Lifecycle protocol: terminal-native-v1.`
  annotation; these main rows defer to native terminal recovery, because a
  queued custom state cannot establish physical execution state. Older rows,
  including generic/exact executor URLs, close as `neutral` with execution
  metadata explicitly unavailable. Completion of the bench
  workflow also starts the short trusted
  `.github/workflows/9700x-lifecycle.yml` recovery workflow; a non-successful
  main request completion starts it even when no benchmark executor follows.
  Thus the final cancelled request is reconciled without waiting for a later
  request. Recovery uses `tools/bench_direct/lifecycle.c` with
  `recover RUN ATTEMPT` and re-reads that exact attempt's API records. For an
  unresolved owned check, a non-successful source request takes precedence
  over the executor outcome: cancelled is `cancelled`, skipped is `skipped`,
  and an unpublished success or failure is `failure`. Recovery never creates
  a successful measurement; successful publication still requires the
  existing evidence validator. Unavailable provenance/API records are
  reported as unavailable, never as a pass. No host job is started and the
  9700X receives no publication credential.

  If a callback failed or ran before a controller repair landed, an owner can
  dispatch the same lifecycle workflow on `main` with the original completed
  `run_id` and exact `run_attempt`. Choose the benchmark executor ID, or the
  main request ID if it ended before an executor existed. Both decimal inputs
  must be positive. Only `davidgmbb` (actor ID 39247043), also the triggering
  actor, can run this hosted replay on `refs/heads/main`. Checkout is pinned to
  that dispatch's trusted `github.sha`; re-running an old callback alone keeps
  its old controller revision. The native controller re-reads the selected
  terminal attempt and its original request, then requires the same
  repository, workflow path, source head, trusted executor title, app and
  external marker before closing an unfinished check. Completed checks remain
  immutable. The replay shares the existing short writer queue, has the same
  60-request/180-second controller bounds and five-minute job deadline, and
  retains separate recovery provenance and costs in both logs and JSONL.
  Pipeline failure remains a failed job. It never dispatches work,
  retries measurement, chooses a head, or promotes bookkeeping to success.
- **Commit report.** `comment-compiler`, the only bench job with
  `contents: write` (the permission of the commit-comment API), upserts one
  general comment on the main commit with `compiler_comment.py`. It downloads
  nothing: its input is the report entry that `publish-compiler` validated and
  rendered (verdict, report-only policy, baseline and candidate, observed host,
  `compiler-compare-v1` scope, wall B/A with its 95% CI, medians with units,
  pair count, capture time, collapsed full tables, and links to the check,
  the workflow attempt and the evidence). The comment is ours only when the
  `github-actions[bot]` account wrote it on exactly that commit and its hidden
  `buster-9700x-compiler-report-v1` marker names that head and mode. Human
  comments and copied markers are never edited. It shows the newest attempt,
  ordered by run and attempt; older attempts and republications stay in its
  history table and never replace the newest report. Duplicates from
  concurrent writers are merged into the oldest comment. Pull requests get no
  comment.
- **Evidence retention.** Artifacts expire after 90 days. The comment and
  the check outlive them and say when the evidence expires; they do not make
  it permanent.
- **Publication-only recovery.** A failed `comment-compiler` can be re-run
  alone ("Re-run failed jobs"), because it reuses `publish-compiler`'s
  validated output and nothing is measured. For an older attempt the owner
  dispatches `.github/workflows/9700x-compiler-report.yml` from `main` with
  the bench run ID and attempt. Its read-only `validate` job re-derives that
  attempt's identity from GitHub's records, as `authorize-compiler` does, and
  reads the attempt's own authorization and compare job results. It then
  re-validates the retained artifact with the same publisher. Expired or
  unverifiable evidence is reported and not published. Its `comment` job
  shares the per-commit concurrency group of `comment-compiler`. It writes no
  check and starts no measurement. The backfill of `c5faf05e` is
  `run_id=37486885378`, `run_attempt=1`; its comment records the original
  measurement run and trusted harness, and the recovery run separately as a
  publication.

### Lifecycle cost observations

Keep control bookkeeping separate from physical measurements. Setup logs
record `api_requests` and `control_execution_seconds`; native recovery pass
records additionally expose `api_retries`, `api_failures`, `closed`,
`already_terminal`, `other_executor` and `unavailable`.

Per-job JSONL observations use schema `buster-9700x-lifecycle-cost-v1`, role
`hosted-control` or `physical`, and separate `queue_delay_seconds` and
`execution_seconds` fields. Derive them from matching Actions job records:

| Quantity | Interval |
| --- | --- |
| Hosted control queue delay | Control job `created_at` to `started_at` |
| Hosted control execution | Control job `started_at` to `completed_at` |
| Physical job queue delay | `compare` / `compare-pull` `created_at` to `started_at` |
| Physical occupancy | Physical job `started_at` to `completed_at` |

Name the original request and executor run/attempt beside each observation;
recovery has its own hosted Actions job record. Missing timestamps are `null`
(unavailable), never zero. Physical occupancy includes preparation,
measurement and export; report retained preparation/timed-phase observations
separately and do not add them to that occupancy. These are per-attempt costs,
not proof of a repository-wide speedup or of performance acceptance.

## Administrator steps

None of these can be performed by a pull request.

1. Runner group. `buster-zen5-9700x` is an organization runner in `buster14a`,
   alone in the group `buster-9700x-service-dispatch` (the name predates the
   service's removal). Repository access: selected, only `buster14a/buster`,
   public repositories allowed. Workflow access: selected workflows, exactly
   `buster14a/buster/.github/workflows/9700x-direct-bench.yml@refs/heads/main`.
   Remove the former `9700x-service-dispatch.yml` entry.
2. Create `BENCH_DIRECT_ENABLED` with value `false`; set it to `true` only
   after the host step. Set it back to `false` on any drift.
3. On the host, the runner account needs `clang` with a static C library,
   `python3` 3.9 or newer, `git`, CPU 2 in its allowed set and a writable work
   directory. It needs no sudo rule. The compiler comparison additionally needs
   `tcc`, `cmake`, `ninja`, `perf` usable by that account (a
   `kernel.perf_event_paranoid` that permits user-space counting), `taskset`,
   and anonymous HTTPS access to github.com for the queue-ref read.
4. Create `BENCH_COMPILER_ENABLED` with value `false`. Set it to `true` only
   after a pilot comparison on the host meets the 90-minute job bound. Leave
   `BENCH_COMPILER_REGRESSION_POLICY` unset.
5. Confirm that workflows from fork pull requests still require approval.

Leftovers of the removed service that only an administrator can delete: the
`benchmark-9700x` environment, the Actions requester policy that named
`9700x-service-dispatch.yml`, and the variable `BENCH_SERVICE_DISPATCH_ENABLED`.

## Disabled sampling research route

The same workflow has separate hosted queue/publication jobs and a tokenless
`sampling` job for #3212's explicitly versioned acquisition/pilot/confirm
selectors. Owner numeric identity, source repository, first request/executor
attempt and every-parent freshness remain required. Current trusted policy's
disabled allowlist is checked natively before physical assignment; the frozen
measurement harness is pinned independently. Historical request provenance
uses associated old-commit membership and its pull number, because that pull's
live head may advance between packets. The native once-only ledger and trusted
GitHub attempt history retain cancellations and charge whole physical job time.
See the [predeclared sampling contract](../../docs/compiler-main-sampling.md).
