# Collecting checker-layout acceptance for #1175

The compiler change is already merged in [#1225](https://github.com/buster14a/buster/pull/1225).
The remaining acceptance is **measured taken branches and cycles over placements on the 9700X**,
not another source change or a synthetic counter-reader test.

`.github/workflows/checker-layout-1175.yml` supplies a narrowly scoped execution path through
GitHub Actions, without SSH. It does not modify `9700x.yml`, its correctness inventory, required
checks, nightly policy or merge queue. Its new hardware job never runs on pull requests.

## Fixed inputs and admission

The workflow pins the integrated comparison, not the earlier unmerged PR head:

- Reference: `e36740b8f70cf5755cabff9b2a9a998b173c2878`.
- Candidate: `c79fd0076fc8ee3ba0a43d02f94dca417a7557ea` (reference is its first parent).
- Harness: buster-performance `961e2893fe13ac4c30547ff86f1e2e279e09b06b`, the merge of
  [buster-performance#248](https://github.com/buster14a/buster-performance/pull/248).

There are no caller-supplied source refs, commands, corpus paths or counter events. Changing
these pins requires a workflow review. The harness and its raw measurements remain in
buster-performance; no measurement harness or raw evidence is added to this source repository.

A hosted job first checks the pins and main ancestry, runs the pinned reader's synthetic C tests
with GCC, Clang and AddressSanitizer/UndefinedBehaviorSanitizer, and checks the campaign's shell
syntax. These are harness tests, not performance measurements.

It then reads **every page** of the latest attempt's jobs for a successful main-push `9700x.yml`
run of each exact source SHA. All 48 expected hosted shards across both architectures and
sanitizer modes, all reported hosted test rows, the three named fuzz jobs and the native
correctness step must have succeeded. Missing, skipped or
failed qualification refuses hardware admission. A successful Measurement storage job or an
all-skipped aggregate is not accepted. The JSON responses are retained separately as qualification
receipts. This is qualification of the two historical compilers actually built, not a substitute
for the full main CI that still runs on the workflow's own merge.

## Running it

Merging a change to this workflow into main triggers its hosted qualification and, when that
passes, its fixed hardware campaign. Subsequent ordinary main commits do not repeat it.
There is no recurring schedule. A deliberate repeat uses **Actions → Checker layout acceptance
(#1175) → Run workflow**, with branch **main**. Dispatches on other branches cannot queue hardware.
A PR changing the workflow runs hosted qualification only; there is no PR performance gate.

The job uses the existing `buster-9700x-service-dispatch` runner group and 9700X labels. No runner,
group, permission or branch-protection configuration is changed. If runner-group policy rejects
this workflow, that is an infrastructure refusal, not permission to bypass it.

The runner's lifecycle hook owns the existing shared lock, as it does for `9700x.yml` and
`setup-benchpress.sh`. Before setup, this job requires the 9700X identity and an owner receipt
containing its own run URL. Within this already-locked CI job only, `CAMPAIGN_LOCK=env` avoids
reacquiring the same lock inside the standalone campaign. No lock process is stopped or released.
Standalone host invocations continue using the campaign's default per-run `exclusive.sh` locking.
The whole hardware job is bounded to ten minutes; the campaign itself is bounded to eight, with
partial output retained on ordinary failures. A cold setup or timeout can make a run incomplete;
never increase the shared-host occupation silently or call partial collection acceptance.

The job stages the same verified official Clang/build tools and frozen corpus as native CI,
without modifying system packages. It uses a fresh per-run directory, CPU 2 with the performance
governor, znver5, the default compilation lanes, and the original campaign's workloads:
SQLite syntax checking, SQLite object compilation and the 139-unit musl object compilation.
It builds four placements per tree, collects three repetitions of each user-mode perf event,
and invokes `time-runs --runs 11 --warmups 1 --copies 2`.

## Evidence and the remaining decision

The campaign checks generated objects byte for byte. Additional collection checks require
140 objects from each of the eight executables, matching object manifests, the expected actual
within-page displacements of both checker functions and `main`, all 216 counter observations,
72 raw perf files and a nonempty timing log. The counter reader is rerun before collection is
marked complete. Full executable hashes, symbol tables, object hashes, placement checks,
source/run identities, raw perf output, timing output and the campaign log are uploaded.
The collection status is explicit; a successful collection is **not** a positive performance verdict.

Artifacts are retained for 30 days, named `checker-layout-1175-RUN-ATTEMPT` and
`checker-layout-1175-qualification-RUN-ATTEMPT`. Preserve them under
`buster-performance/docs/measurements/` before expiry. Failed runs can have partial artifacts
and no collection-complete marker. No compiler binaries or corpus copies are published by this workflow.

To finish #1175, review the real taken-branch and cycle changes across all placements, the
`time-runs` placements/copies and its process statuses, and the scope of each workload. Record
regressions or inconclusive ranges honestly; do not retain a change merely because taken branches
fall. Publish the raw results with their identities in buster-performance, link a concise summary
and remaining profile-layout gap from `docs/compile-time.md`, and only then decide whether the
issue's acceptance criteria are met. The existing BOLT counts are historical evidence, not a
replacement for this hardware comparison.
