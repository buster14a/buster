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
runner allocation**. Desktop, lint and analyzer continue on main. `CI complete`
rechecks the source evidence and receipt digest, validates the retained main
jobs and their steps, and requires the three skipped job groups. A source
rerun or missing proof at this point fails `CI complete`; it never converts
missing execution into green. The receipt is retained as
`main-ci-reuse-<run>-<attempt>` with source run/job/artifact IDs and the
current main job inventory. No candidate-controlled code has publication
authority, no cross-event cancellation key is shared, and no check is forged.

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
