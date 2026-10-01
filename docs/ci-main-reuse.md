# Exact queue-to-main CI reuse (#1808)

`Buster CI` may reuse validation from nineteen jobs from a successful `merge_group` execution when
GitHub merges **that same commit SHA** to `main`. The source run remains the
authoritative execution. The main `CI complete` job links it and says that the
native/mobile/UEFI main jobs were skipped and desktop jobs ran only their cache lifecycle. It does not claim that their validation ran twice. Required
check names and the merge-queue ruleset are unchanged.

## Deterministic coverage inventory

| Main-push obligation | Policy | Reason |
| --- | --- | --- |
| Ten desktop `release`/`checks` shards | Reuse validation; run cache lifecycle on main | Same queue-proven compiler/configuration/fixture coverage. Main retains exact-key Zig restore, digest verification, publication and evidence, logs and existing job names. |
| Workflow lint | Run on main | The merge-parent guard evaluates the main push's `before` SHA and event. |
| Clang analyzer shards | Reuse exact queue analysis; retain main receipt job | Both events use the exact SHA as candidate and baseline. Queue performs the full candidate analysis and failure/coverage controls, possibly with an additional comparison; main ordinarily requests candidate-only analysis. All four source execution steps and its artifact must succeed. |
| Five desktop-native mode lanes and three Unix differentials | Reuse exact queue jobs if admitted | Same commit, job definitions and input-free test commands; source job and required step results remain authoritative. |
| Windows native reference differentials | Reuse exact queue jobs if admitted | The two Windows native jobs also require their MSVC differential steps. |
| Android x86-64 and iOS AArch64 shards | Reuse exact queue jobs if admitted | Same commit and fixed workflow commands; source test steps and retained diagnostics are required. |
| UEFI boot | Reuse exact queue job if admitted | Same commit, pinned firmware packages and boot commands; source boot result and retained artifact are required. |
| Independent required workflows | Unchanged | This policy affects only `Buster CI`; their checks still run under their own contracts. |

The nineteen exact names and artifact prefixes are declared in
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
successful source job identities and mandatory coverage steps, and nineteen
nonempty, unexpired, source-bound artifacts with SHA-256 digests. Paged API
results must be complete. It rereads the source and current runs after
collection to catch attempt movement.

A direct push, tag, workflow dispatch, explicit rerun, missing or conflicting
source, failed/skipped/cancelled job, expired artifact, mismatched identity,
API error or incomplete pagination schedules all normal jobs. `CI complete`
then uses its original exact job and step inventory. A deliberate manual
measurement retains its normal full execution.

After a positive decision, native, mobile and UEFI jobs are skipped **before
runner allocation**. Desktop jobs run their existing Zig cache lifecycle, sanitation and log retention on main. They skip workflow/wrapper regressions, mold/LLVM installation, compiler probes, combination matrices and configure/coverage collection. A separate summary explicitly identifies this as reused validation. Lint continues in full on main. Analyzer retains its existing named job and artifact, but runs only a truthful reuse summary; all four analyzer execution steps must be skipped on main and their source counterparts must have succeeded. `CI complete`
rechecks the source evidence and receipt digest, validates the retained main
jobs and their steps (cache-only desktop jobs require every cache/evidence step, and successful skipping of every validation step), and requires the three skipped job groups. A source
rerun or missing proof at this point fails `CI complete`; it never converts
missing execution into green. The receipt is retained as
`main-ci-reuse-finish.json` inside `desktop-partitions-<run>-<attempt>`,
with source run/job/artifact IDs and the
current main job inventory. No candidate-controlled code has publication
authority, no cross-event cancellation key is shared, and no check is forged.

This is policy `buster-main-ci-reuse-v2`. Existing required checks, matrix
names, cache keys, cache write policy and artifact names remain unchanged.
Desktop cache jobs still allocate all ten platform runners and the analyzer receipt still allocates its Linux runner; this change saves
build/test and compiler-install work, not those allocations. Reducing them to
five cache publishers is a separate cache/check-contract transition. Independent
workflows require their own event/coverage review before reuse can be enabled.

## Qualification and measurement

Run `python3 -B tools/main_ci_reuse_test.py -v` and the existing workflow
policy/inventory tests. On a bounded queue-to-main transition, retain the
source and main run/attempt IDs, the nineteen source job IDs, the receipt and the
current job inventory. Compare the source and main jobs' `created_at`,
`started_at` and `completed_at` values: sum actual runner busy seconds, report
queue delay separately, and report run creation to aggregate completion for
end-to-end latency. Report the observed saving only after this hosted
transition; an older incident is a baseline, not an after measurement.

The existing `github_ci_time.py` normal matrix cohorts describe full
executions. Reused main runs must be measured separately and must not be
pooled into those full-execution medians.
