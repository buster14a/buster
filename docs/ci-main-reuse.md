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
missing execution into green. The verification result is retained as
`main-ci-reuse-finish.json` inside `desktop-partitions-<run>-<attempt>`,
with the verified receipt (source run/job/artifact IDs and current main jobs),
or a structured failure report without a receipt. No candidate-controlled code has publication
authority, no cross-event cancellation key is shared, and no check is forged.

This is policy `buster-main-ci-reuse-v2`. Existing required checks, matrix
names, cache keys, cache write policy and artifact names remain unchanged.
Desktop cache jobs still allocate all ten platform runners and the analyzer receipt still allocates its Linux runner; this change saves
build/test and compiler-install work, not those allocations. Reducing them to
five cache publishers is a separate cache/check-contract transition. Independent
workflows require their own event/coverage review before reuse can be enabled.

## Receipt handoff and finalization (#2134)

The decision exports the complete canonical JSON receipt plus its SHA-256 digest,
not just the digest. It includes the exact main run/attempt and source run/attempt,
source branch/completion, commit, workflow blob, jobs and artifacts. The handoff is
bounded to 32 KiB. Workflow expressions enter the finish command through quoted
environment variables; missing, oversized, malformed, cross-run or digest-mismatched
handoffs fail before any source API lookup. The decision result is uploaded as
`main-ci-reuse-<run>-<attempt>`; reuse is not enabled if that upload fails.

Finalization first reads the recorded source by run ID, rather than choosing a
new source from a list. It also checks the workflow-scoped, exact-SHA merge-group
listing so a competing run cannot silently replace the recorded source. An empty
listing, moving pagination total, HTTP 429/5xx, or transport interruption permits
at most two additional discovery reads, after one and two seconds respectively.
The bound source is rechecked during those waits. Multiple/replacement runs,
changed attempts/identity, failed execution, malformed listings, exhausted page
bounds, and definite permission errors do not retry. A persistently unavailable
or inconsistent listing remains a failure: direct retrieval alone is not a
waiver of the uniqueness/completeness policy. Before scheduling, missing proof
still selects full CI immediately rather than polling.

Both phases write `buster-main-ci-reuse-result-v1` diagnostic JSON. Only a
`status: verified` result contains `receipt`. Failures record the phase, stage,
classified reason, expected digest/source identity when validated, and bounded
candidate IDs/attempts for each discovery read. These records are separate from
the canonical receipt passed between jobs. They contain no token or full API
response. No failed finish prints a successful-reuse summary. An unusable output
filesystem makes the command fail rather than authorize reuse without retention.

GitHub can represent skipped native/mobile matrices as two same-name,
unexpanded placeholders instead of eight named native/mobile/UEFI rows. The
reuse reader validates these before name-based executed-job de-duplication. It
accepts only the complete eight named skips, or exactly two recognized matrix
placeholders plus the UEFI skip. Skipped rows require unique IDs, the exact main
run/SHA/first attempt, terminal `skipped`, no steps and no allocated runner.
Mixed, missing, extra or unrecognized rows fail. Diagnostics retain their IDs
as **skipped**, never as successful executions. The workflow independently
requires each native/mobile/UEFI group result to be skipped; all source coverage
and retained main execution checks remain required.

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
