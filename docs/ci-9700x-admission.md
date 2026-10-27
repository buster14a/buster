# 9700X admission, priority and recovery

Issues [#546](https://github.com/buster14a/buster/issues/546),
[#547](https://github.com/buster14a/buster/issues/547) and
[#548](https://github.com/buster14a/buster/issues/548) share one default-branch
controller. It decides eligibility and priority before work enters the physical
runner queue. The runner hooks' shared host lock remains mandatory and unchanged.

## Activation and security boundary

The cutover is **off** until repository variable `CI_9700X_ADMISSION_ENABLED` is
`true`. With it off, the existing PR/push/daily/manual qualification path and
required job remain in use. The hosted policy-fixture workflow tests this change
before it is installed on `main`.
Targeted dispatch inputs are rejected on hosted capacity while admission is off;
they cannot fall back to a legacy physical run or download a nonexistent guard.
An ordinary maintenance dispatch with no PR target retains the original path.

First verify the compatible pool really has one host-lock slot and restrict
**this repository's** physical entrypoint to:

```text
buster14a/buster/.github/workflows/9700x.yml@refs/heads/main
```

Either enforce that exact workflow in a dedicated runner-group policy, or install
the root-owned Buster-only job-start source guard below. The existing group is
shared with `buster-legacy`: changing its group-wide all-workflow policy would
change legacy access. The hook alternative preserves legacy's policy and the
original lock script exactly. Do not broaden the group, add another runner, or
allow Buster PR merge refs/arbitrary workflows. Set `CI_9700X_POOL_CAPACITY=1`
only after verification. The controller/guard refuse admission without that
attestation; it is not permission to change the host lock or add another slot.
The repository token intentionally has no organization-runner administration
permission; use the existing administrator session to inspect settings instead
of adding a broader token to CI. The existing host needs `gh` in addition to its build
tools, Node 24-capable Actions runner, network access to GitHub and artifact access.

`9700x-control.yml` checks out `refs/heads/main` without credentials on a hosted
runner. Its permissions are contents/PR read and Actions/checks/statuses write.
It never checks out a PR, evaluates PR strings as commands, reads a PR artifact
as authority, imports a cache, runs a build on the physical host, or exposes its
write token to an executor. A read-only review-signal workflow wakes it; live
review data and actual repository permissions decide the result.

The dispatched executor uses the reviewed default-branch workflow and only read
permissions. It reconstructs the immutable merge SHA selected for the current
live head/base pair. Its admission guard is compiled on hosted capacity, transferred
as an immutable artifact of that same run/attempt, and checked against the
hosted job's SHA-256 before each execution. A conflicted/stale admission therefore
does not compile the guard or the compiler on the 9700X. Public fork revisions
also require a trusted current-revision approval before admission; a persistent
priority override alone cannot authorize new fork code.

### Shared-group host source guard

`tools/ci/admission-hook.sh` wraps, rather than reimplements, the existing
`/usr/local/libexec/buster-9700x/job-started.sh`. During installation save that
original file as `job-started.before-admission.sh`, with its owner/mode/content
unchanged. Install the wrapper and a reviewed-main build of `admission` in the
same root-owned directory. Keep `admission-enabled` absent during staging; no
runner restart or `.env` change is needed. Do not overwrite an existing backup.

When `admission-enabled` exists, `admission --host-source-guard` checks the
GitHub-provided job identity before the original hook takes the lock. Buster
requires the exact reviewed executor workflow/ref above and `refs/heads/main`;
missing identity fails closed. Legacy and other repositories retain their
existing workflow policy. Buster also refuses the original hook's permissive
missing-lock fallback. The original holder, lock, cancellation cleanup and
completed hook are unchanged. The gate, executable and marker must be root-owned
and not writable by the runner account; placing the marker in the shared writable
lock directory would let PR code alter the gate.

Inspect the installed runner's default-context export before activation and
verify a PR workflow is refused and a reviewed main executor is accepted. The
guard uses no API/credential and executes no PR code. Create the root-owned marker
only after the staged PR has qualified and the reviewed controller is on main;
then enable repository admission. Removing the marker restores the original hook
behavior; keep admission off during rollback. Repository-scoped source enforcement
avoids changing either legacy access or the existing physical runner registration.
Runner 2.338.0, verified on the shared host, exports `workflow_ref`, `ref` and
`repository` from its server-supplied
[GitHub context](https://github.com/actions/runner/blob/v2.338.0/src/Runner.Worker/GitHubContext.cs).
Its [script handler](https://github.com/actions/runner/blob/v2.338.0/src/Runner.Worker/Handlers/ScriptHandler.cs)
applies these runtime values to the hook environment; PR job-env strings do not
supply the identity used by this gate. No runner implementation is copied here.

## Required-check attribution

The required context remains **Build and execute on Ryzen 7 9700X**, from the
GitHub Actions integration (15368). Do not change branch protection or require a
rebase merely because a PR is behind its base.

In active mode, a native PR workflow supplies the independent hosted `changes`
preview; its skipped physical job uses a separate queue-request name. GitHub may
display the literal name expression for a skipped job rather than its evaluated
`9700X queue request` label; neither form is the required result. Native PRs
from public forks always take that hosted-only preview path, even if repository
variables are absent from its event context. The host source gate also enforces
the boundary when a fork changes its own workflow. A selected PR
gets a `workflow_dispatch` on reviewed `main`, whose physical job is named
`9700X admitted execution`. That dispatch's native check belongs to `main`; it is
**not** the PR's required result. Instead the controller publishes the existing
required Check Run and same-name commit status on the **PR head**, identifying
the tested base/merge and exact executor run in the check's output.

GitHub requires both a check and a commit status when they share a required
name ([GitHub's required-check rules](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks)).
This prevents a fork's read-only hosted workflow from copying the check name
and overriding the authoritative pending/failing commit status. Worker and PR
tokens cannot write that status. Verify the same integration/name association
on a real representative PR before completing the rollout.

Success requires the exact leased default-workflow run/attempt, successful
admission guard and physical execution job, a nonempty runner identity, overall
success, and a final live recheck of the PR head, base, merge SHA and mergeability.
A skipped/cancelled/failed executor, stale head/base, unbound dispatch, conflict
or UNKNOWN cannot produce required success. A new base invalidates a prior
success and qualifies the new merge without changing the PR branch. Unknown
mergeability is retried three times, then left pending; it is never inferred to
be a conflict. A known changed identity still invalidates the old lease.

Documentation-only PRs receive the existing successful required result without
hardware work, after confirming mergeability and the immutable base-to-merge
diff. `docs/performance/**` remains code for qualification. Both names of a
rename count; the local object diff reads every changed path without REST's file cap.
Conflicting or indeterminate docs-only PRs do not receive a false green skip.

The event, dispatcher and reporter have separate SHA identities. GitHub reruns
retain their original event SHA/ref ([rerun semantics](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/re-run-workflows-and-jobs));
rerunning a stale native PR workflow is therefore not the dispatch architecture.
Current-head qualifications can also be created for PRs that opened with a
conflict and never had an original PR run.

### Live refs and the frozen merge

The controller reads `headRef.target.oid` and `baseRef.target.oid` for the current
branches. PR `baseRefOid`, REST `base.sha` and the generated test merge can be
older snapshots. GitHub documents [deferred test-merge generation](https://docs.github.com/en/enterprise-server@3.21/admin/release-notes),
and the live rollout observed this on an unchanged head after `main` advanced.
Those cached fields are not current-pair authority. UNKNOWN still receives three
bounded metadata retries and remains pending; neither a stale clean result nor
a stale conflict decides admission for a changed base.

For known metadata, hosted preflight reads the pinned Git objects over public
HTTPS into a private bare repository and uses `git merge-tree --write-tree` to
establish the actual current conflict state. It never checks out PR files, runs
a PR program, follows a submodule, executes hooks or inherits external merge
drivers. Git configuration, credentials, prompts, replacement objects, protocols
and thread counts are constrained. No GitHub ref or PR branch is changed and
the controller retains read-only contents permission. Clean behind PRs therefore
qualify the new base without a forced update/rebase or a twelve-hour cache wait.

A deterministic, unpublished automation commit records the resulting tree and
the exact base/head parents. Its fixed metadata identifies the construction as
`Buster 9700X frozen qualification merge v1`; it is not a human/DCO source commit.
The lease's merge SHA is this canonical object, not GitHub's possibly stale test
merge. Hosted admission independently reconstructs the same object and transfers
only new merge objects in a small pack alongside its guard, through that exact
run/attempt's immutable artifact. SHA-256 outputs authenticate both files.

The hosted selector and physical executor check out the pinned base, fetch the
pinned head and import the authenticated pack. They verify both parents and the
resulting checkout SHA before reading/building PR source. A changed pair,
different merge result, missing/corrupt pack or failed guard remains blocking.
The physical guard rechecks live refs against the authenticated lease; it does
not calculate mergeability or build the guard while holding the host lock.

### Preflight cache and resource limits

The durable PR row caches the immutable conflict result, canonical merge SHA,
complete changed-path classification and Git-engine/method identity against both
live input SHAs. An unchanged pair does not fetch or merge again during later
reconciliations. A new head/base or engine invalidates the proof; a base advance
never reuses an older result. The selected PR's pinned pair and cached proof are
revalidated before grant. Each completed cache entry is saved independently so
one later failure cannot erase work already done by this controller invocation.

Uncached preflight runs in a separate process group. Its watchdog allows 30
seconds per pair and 120 seconds total per reconcile. Actual Git workers inherit
a 16 MiB OS file-size limit for fetched packs, a 15-second CPU limit per process,
and a 512 MiB address-space limit. The parent monitors the private object store
against 64 MiB and 8,192 entries every 20 ms, and kills the entire worker/helper
group on a wall/storage limit. Core dumps are disabled. These limits constrain
the fetch itself; the JSON/output-buffer limit alone does not bound transport.
The monitored store may overshoot its threshold by in-flight writes before the
watchdog kills the group; the per-file and process limits still apply.

A fetch/resource failure discards the partial store and blocks only that PR's
pair with a five-minute retry backoff. Cached eligible PRs remain usable, and
other pairs can use the remaining reconcile budget. Exhausting the total budget
defers uncached work to a later pass; it cannot become a conflict or green skip.
`/ci retry` clears the failed-pair backoff. Frozen-pack construction has the same
30-second worker/file/store limits before any physical job is admitted. The
controller summary exposes computed/cache/deferred counts and Git work time.

## Priority and commands

| Priority | Meaning |
| --- | --- |
| P0 critical | Explicit maintainer override |
| P1 high / approved | Explicit high override or trusted current-revision approval |
| P2 normal | Ordinary eligible PR |
| P3 main/background | Main, daily and manual work under PR congestion |
| P4 low | Explicit low override |
| hold | Ineligible until resumed; distinct from low priority |

A run override takes precedence over a PR override, which takes precedence over
automatic approval priority. `normal`/`clear` removes an override and lets the
underlying automatic/PR policy apply again. PR-wide priority and hold survive a
new head until reset/resume or closure. Run overrides bind a request ID and SHA;
they expire on supersession, completed qualification or closure. Old commands
from before closure do not resurrect an override on reopen.

Only actual repository `write`, `maintain` or `admin` actors can issue controls
or supply approval priority. The controller checks login **and user ID** against
the permission API. Labels, author associations, PR files, quoted text and
untrusted actors confer no authority. No trusted-bot allowlist is configured.

Use one standalone command in a PR conversation comment:

```text
/ci priority critical
/ci priority high
/ci priority normal
/ci priority low
/ci hold
/ci resume
/ci retry
/ci status
/ci priority high run 40481067492
/ci priority normal run 40481067492
/ci hold run 40481067492
/ci resume run 40481067492
/ci retry run 40481067492
/ci status run 40481067492
```

The run ID is the current **request** run shown in the PR's check. A fast native
PR request may have finished its hosted preview while its logical qualification
is still queued; its ID remains usable until qualification/supersession. A
controller-owned executor has already been admitted and is non-preemptible.
Main/daily/manual run IDs can instead be controlled through the dispatch form.

`hold` prevents future admission; it does not interrupt an already-admitted run.
`resume` removes the hold. `retry` requests one new qualification of the current
revision after failure, or is an idempotent no-op while one is already active.
It never dispatches an obsolete SHA. A background retry reruns only a verified
admission-enabled main workflow; use a new main maintenance dispatch for a legacy
run. Normal approval/priority changes never cancel a running benchmark. There is
no urgent-preemption command.

Current-revision GitHub `APPROVED` reviews elevate automatically; the latest
meaningful decision per reviewer supersedes their older one. Dismissed/stale
reviews do not approve, and a current trusted `CHANGES_REQUESTED` vetoes automatic
approval priority. An explicit manual override still applies.

A comment consisting of `LGTM`, `APPROVED`, `SHIP IT`, `READY TO MERGE` or
`/ci approve` also counts. Matching is intentionally conservative: one complete
line, apart from blank lines and fewer than four leading spaces. Prose,
substrings, questions, negations, quotes, code fences and code indentation do not
match. Edits/deletions are read from live comments; approval disappears when the
line disappears. Since GitHub does not attach conversation comments to a commit,
an approval comment must be newer than the controller's observation of that head.
Same-second/unobserved-head comments do not guess a SHA: edit/repost after
observation, or submit a review with an explicit commit association. A new head
invalidates all comment-derived approval; a base-only change does not.

For manual dispatch, use **9700X admission controller**, on `main`, with `pr`
or `run`, an action (`priority`, `clear`, `hold`, `resume`, `retry`, `status`) and
optional priority. The actual event actor must have the same repository
permission. Exact command envelopes in the resulting Actions run names are an
auditable recovery log: even a coalesced/cancelled controller wakeup is recovered
in order. Arbitrary title substrings and non-main controller runs are ignored.
Comment edits use `(updated_at, comment ID, body hash)`; replay cannot retry twice.
Priority and hold have independent durable ordering for each PR/run target.
Comments use their edit time and dispatch controls their creation time, with
stable source IDs breaking same-second ties. A delayed older comment cannot
overwrite a newer dispatch setting; a priority change does not discard a separate
hold/resume. Status queries do not advance those setting clocks. The one-time
ledger migration recovers PR settings from bounded dispatch history without
replaying an already-processed retry.
Deleting a manual command does not undo an already-applied override; reset it
explicitly. The required check and controller job summary show effective priority,
queue position, blocked reason, admitted SHA/run/state, expiry and command outcome.

## Ordering, fairness and capacity

The controller admits at most one qualification to the compatible host-lock
pool. Equal priorities are FIFO by request age, with stable PR/run tie breakers.
New verified approval or a manual promotion reorders queued work without a code
push. Already-admitted work retains its slot.

With at least three eligible **waiting** PRs, main's P3 work goes behind ordinary
PRs. Below that threshold, automatic background work competes at P2/FIFO. Set
`CI_9700X_BACKLOG` to another integer from 1 to 64 to change the threshold. Held,
conflicting, stale, already-admitted and unapproved fork revisions do not inflate
the waiting backlog. A main/daily request waiting two hours takes the next slot
in FIFO order among overdue background work, ahead of priority traffic. The daily
run retains its separate concurrency group and is never cancelled/coalesced into
a main push by this controller.

After eight priority admissions, the oldest eligible PR aged at least 30 minutes
gets a reserved FIFO service slot. This includes low priority and prevents an
unbounded stream of promotions starving ordinary work. The two-hour background
deadline and these reserved slots are explicit exceptions to priority order.
They do not preempt a running qualification. Bounds exclude runner/GitHub
downtime and include the current non-preemptible run and older eligible reserved
work; one bounded active queue cannot gain new older entrants indefinitely.

Only background workflows that actually reached their hosted admission job can
receive a lease. A run still pending behind its ref's existing concurrency group
cannot deadlock the active gate by taking its lease. The original per-PR stale
commit cancellation, per-ref groups, job timeouts, host pinning, benchmark gates
and host-wide shared lock remain. At cutover, clean legacy work and unrelated
maintenance drain before new admission. A run listed as queued can already have
a running job, and Actions has no atomic cancel-if-queued operation. Clean legacy
PR runs therefore are not cancelled just to replace their queue entry; the active
host source gate refuses old queued PR jobs before checkout or taking the lock.
Confirmed conflicts, closure and obsolete heads still receive scoped cancellation.
Other repositories' use of the host still goes through the lock.

## Durable state and failure recovery

The versioned ledger is the GitHub Actions Check Run `9700X controller state v1`
with external ID `buster-9700x/state/v1`, on immutable commit
`335424b18635fa9ab3ac5eb91e7497aaddd37cab`. Only integration 15368's check is
authoritative. `output.text` contains JSON `version: 1`, generation, one lease,
PR priority/eligibility/queue/reason/result rows, command cursors and run overrides.
This is the stable control-plane interface; descriptive labels are unnecessary.
The required PR check also retains a version-1 JSON receipt in `output.text`.

A global controller concurrency group serializes updates; it is a mutex, not a
priority queue. Reconciliation rereads live facts on PR/base/comment/review/run
events and every five minutes. The platform may delay scheduled wakeups; the
guard stays closed until a valid lease exists. State is committed before dispatch.
Head/base/mergeability metadata is batched in one bounded GraphQL query. Review
and comment events refresh their target immediately; a missed signal is recovered
within the 15-minute signal refresh interval while capacity is occupied, subject
to GitHub scheduling. Every candidate's approvals/commands are refreshed before
a free-pool choice, so a coalesced promotion cannot remain hidden behind cached
ordinary work. The selected PR is also read again immediately before its grant. Immutable
diff classifications and unchanged ledger/check descriptions are reused. Waiting
maintenance gates poll once a minute; a selected PR polls every five seconds for
at most three minutes while its dispatch ID is bound. These bounds conserve the
repository token's API allowance without trusting cached eligibility for a grant.
Each dispatch has a unique controller-run/generation nonce and exact run/attempt
binding. A 204/lost response is recovered by the nonce, without redispatching.
An unbound dispatch cannot pass the guard. An unbound nonce not found after
three minutes is revoked; a late arrival then fails closed.

Leases last 40 minutes, covering the qualification/selector/host-hook budgets.
For an identified live PR executor, revocation, conflict, identity change or
expiry requests cancellation but **retains the slot until Actions confirms a
terminal run**. UNKNOWN alone does not cancel an unchanged pair. Main/daily/manual
work is not cancelled by conflict reconciliation. Expired maintenance remains
owned until it terminates; an operator can cancel a stuck maintenance run in
Actions. Controller/runner outages cannot create a second lease.

API/permission errors, unsupported/corrupt state, ambiguous dispatches or bounds
exhaustion stop new admission. Bounds are 64 open PRs, 128 active candidates,
64 run overrides, 100 unprocessed dispatch controls and 60,000 state JSON bytes.
Metadata and frozen-object transfers are each bounded to 16 MiB.
Do not delete the ledger or clear a lease while an executor could still be alive.
First cancel/confirm terminal status, then reconcile. No branch-protection bypass,
force-update or new service is a recovery step. A maintainer can inspect without
writing using `admission --snapshot`.

Disabling admission intentionally leaves existing controller statuses blocking
until a valid qualification is restored. A new source-fix head can qualify through
the ordinary pre-activation path; do not fabricate status success or bypass branch
protection to recover. Stop controller writes before removing the host marker,
and cancel/confirm terminal any controlled fork probe before restoring old behavior.

Commit statuses are appended only when the required pending/failure/success state
changes, avoiding queue-position updates consuming GitHub's per-SHA/context quota.
Check Runs carry the frequently changing descriptions. A quota/API failure remains
blocking; it cannot be interpreted as a successful qualification.

## Validation and measurements

```sh
cc -std=c17 -O2 -Wall -Wextra -Werror tools/ci/admission.c -o /tmp/admission
/tmp/admission --self-test
cc -std=c17 -O2 -Wall -Wextra -Werror tools/ci/changes.c -o /tmp/changes
/tmp/changes --self-test
```

The original in-memory API fixtures exercise the real adapter, controller,
guard, Check/Status publication, commands and policy without network access or
physical jobs. They cover conflicts, UNKNOWN, head/base changes, closure/reopen,
permission rejection, current/stale/dismissed approvals, edits/deletions, replay,
promotion/FIFO/fairness, background backlog, leases, cancellation and ambiguous
dispatch recovery. `9700x-policy.yml` runs these on hosted capacity.

Roll out after source review and the ordinary pre-activation 9700X qualification:

1. Merge with admission off. Verify the exact Buster entrypoint and pool capacity,
   using the Buster-only host source guard for the shared legacy group (or a
   dedicated existing workflow restriction). Preserve unrelated access.
2. Record the old queue's run creation time and physical job `started_at`/
   `completed_at`, the associated review commit/time and the exact source SHA in
   `buster-performance/docs/measurements`. Mark historical approval uncertainty.
3. Set `CI_9700X_POOL_CAPACITY=1`, then `CI_9700X_ADMISSION_ENABLED=true`; dispatch
   one controller `status` request from `main`. Check both the required Check Run
   and commit status come from the expected integration on the PR head.
4. Exercise a representative clean PR, a docs-only PR and a public fork; then
   controlled conflict/resolve, current-head promotion, hold/resume and retry.
   Check that only the selected default-branch executor reaches the physical pool,
   that its checkout equals the receipt's merge SHA, and that no skipped/stale run
   turns either required result green. Verify main/daily recovery too.
5. Copy the resulting structured receipts and Actions job metadata to the
   performance repository. Compare event-to-admission wait, hardware job wall time
   and approval-to-green latency on real runs, with matched workload/queue context.
   A hardware job's wall time includes any host-hook wait; admission-to-completion
   is reported separately. Fixture success alone is not this acceptance evidence.

Receipts retain queued/approved/admitted/completed timestamps and hardware job
seconds even after the mutable controller ledger drops a closed PR. The check's
summary also exposes these durations. Do not claim a measured scheduling benefit
or close the live-acceptance criteria until that campaign is recorded.
