# Exact queue-to-main CI reuse (#1808)

`Buster CI` may reuse eight jobs from a successful `merge_group` execution when
GitHub merges **that same commit SHA** to `main`. The source run remains the
authoritative execution. The main `CI complete` job links it and says that the
reused main jobs were skipped; it does not claim that they ran twice. Required
check names and the merge-queue ruleset are unchanged.

## Deterministic coverage inventory

| Main-push obligation | Policy | Reason |
| --- | --- | --- |
| Ten desktop `release`/`checks` shards | Run on main | Main-push Zig archive cache publication and its normal matrix coverage remain intact. |
| Workflow lint | Run on main | The merge-parent guard evaluates the main push's `before` SHA and event. |
| Clang analyzer shards | Run on main | Reference selection depends on the event and main baseline. |
| Five desktop-native mode lanes and three Unix differentials | Reuse exact queue jobs if admitted | Same commit, job definitions and input-free test commands; source job and required step results remain authoritative. |
| Windows native reference differentials | Reuse exact queue jobs if admitted | The two Windows native jobs also require their MSVC differential steps. |
| Android x86-64 and iOS AArch64 shards | Reuse exact queue jobs if admitted | Same commit and fixed workflow commands; source test steps and retained diagnostics are required. |
| UEFI boot | Reuse exact queue job if admitted | Same commit, pinned firmware packages and boot commands; source boot result and retained artifact are required. |
| Independent required workflows | Unchanged | This policy affects only `Buster CI`; their checks still run under their own contracts. |

The eight exact names and artifact prefixes are declared in
`tools/main_ci_reuse.py`; tests compare those names with the workflow matrix
inventory. The source artifact record retains its digest, run/attempt binding,
runner and resolved toolchain logs. Native LLVM is resolved at execution time
and hosted runner images may change; reuse means the exact commit **already
passed** on the source run's actual toolchains, not a claim that a hypothetical
second execution would resolve identical tools. The two-hour window bounds
staleness. Nothing in this policy removes a test, architecture or mode from
queue coverage.

## Admission and fallback

The cheap `Main CI reuse decision` job runs only on a main push, with read-only
contents and Actions permissions. It checks the current run's repository ID,
workflow ID/path, exact commit, main ref, push event and first attempt. It
requires one successful, completed first-attempt merge-queue run with the same
SHA, the expected queue ref, and completion within two hours before the main
run. It checks the exact workflow blob against the local checkout, all 21
successful source job identities and mandatory coverage steps, and eight
nonempty, unexpired, source-bound artifacts with SHA-256 digests. Paged API
results must be complete. It rereads the source and current runs after
collection to catch attempt movement.

A direct push, tag, workflow dispatch, explicit rerun, missing or conflicting
source, failed/skipped/cancelled job, expired artifact, mismatched identity,
API error or incomplete pagination schedules all normal jobs. `CI complete`
then uses its original exact job and step inventory. A deliberate manual
measurement retains its normal full execution.

After a positive decision, native, mobile and UEFI jobs are skipped **before
runner allocation**. Desktop, lint and analyzer continue on main. The decision
exports a canonical JSON receipt and its SHA-256 digest as job outputs. The
`buster-main-ci-reuse-receipt-v1` handoff binds the exact main run/attempt,
repository, SHA, workflow blob, source run/attempt/ref/completion, source jobs
and artifact IDs/digests. `SOURCE_RECEIPT` carries it as environment data, never
as shell source; the parser rejects missing, oversized, duplicate-key or
misbound receipts. Its 32 KiB bound is below GitHub's job-output limit.

`CI complete` verifies that handoff, fetches the selected source **by run ID**,
and recomputes its job/artifact proof. It does not rediscover or substitute the
source. A separate workflow-ID-scoped listing still checks that the source is
unique for this SHA and event. An empty scoped listing gets at most three
reads with 1/2-second backoff. Persistent absence, multiple candidates, a
replacement, a rerun, malformed/incomplete pagination or an API error remains
fatal; a cached receipt or a successful direct read alone cannot authorize the
skips. Uniqueness is checked again after main-job collection, followed by
fresh direct source/current-run reads. This avoids the repository-wide
rediscovery dependency observed in #2134 without dropping ambiguity checks.

The retained main jobs and required steps must succeed. GitHub may represent
the skipped native and mobile matrices as **two unexpanded jobs with the same
expression name**, plus one skipped UEFI job. The reuse reader recognizes only
that exact three-entry shape, validates every ID/run/attempt/SHA and requires
no steps or assigned runner. It does not send those two intentional
placeholders through logical-job deduplication, nor count them as executed
coverage. An entirely expanded eight-job skip inventory is also accepted;
missing, extra, mixed, executed or misbound entries fail. The workflow's
separate aggregate still requires both named matrix groups and UEFI to be
skipped. All 13 retained job identities remain mandatory.

Both phases retain `buster-main-ci-reuse-result-v1` records: an initial
`incomplete` record is atomically replaced by `verified`, `unavailable`
(decision fallback), or `rejected` (finalization failure). Only `verified`
contains a receipt; failed validation never emits a positive summary or reuse
output. Results record the failure stage/class, safe endpoint and bounded
candidate ID/attempt observations, not tokens, response bodies or arbitrary
transport exception messages. The decision JSON is in
`main-ci-reuse-<run>-<attempt>`; `main-ci-reuse-finish.json` is in
`desktop-partitions-<run>-<attempt>`. Both uploads run on failure and reject
missing files. A lost runner can still prevent upload; no workflow can promise
evidence recovery from an unavailable host.

No candidate-controlled code has publication authority, no cross-event
cancellation key is shared, and no check is forged. Required checks,
permissions, main-specific work, full-validation fallback and the eight-job
coverage policy are unchanged. The historical list response from #2134 was
not retained; this repair does not claim to establish its provider-side cause.

## Qualification and measurement

Run `python3 -B tools/main_ci_reuse_test.py -v` and the existing workflow
policy/inventory tests. On a bounded queue-to-main transition, retain the
source and main run/attempt IDs, the eight source job IDs, the receipt and the
current job inventory. Compare the source and main jobs' `created_at`,
`started_at` and `completed_at` values: sum actual runner busy seconds, report
queue delay separately, and report run creation to aggregate completion for
end-to-end latency. Report the observed saving only after this hosted
transition; an older incident is a baseline, not an after measurement.

The existing `github_ci_time.py` normal matrix cohorts describe full
executions. Reused main runs must be measured separately and must not be
pooled into those full-execution medians.
