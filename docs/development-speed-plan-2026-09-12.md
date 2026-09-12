# Development speed: decisions, feedback and integration

Date: 2026-09-12. Inspected main: `5dab18af0208cfa7fa65673b6884d5c253fda85b`.

This records recommendations for getting compiler changes implemented, tested and
merged sooner. It is **not** a compiler-throughput benchmark or an adopted change
to repository policy. No compiler build, self-host run, CI measurement or
productivity experiment was performed for this filing. Issue/PR observations are
historical snapshots; consult their current threads before acting.

Follow [AGENTS.md](../AGENTS.md) and the existing [workflow](agents/workflow.md),
[build](agents/build.md) and [testing](agents/testing.md) guides. This note does
not override them. Live ownership and progress belong in issues, not in a second
status ledger here. The distinct workflow experiment is tracked in
[#532](https://github.com/buster14a/buster/issues/532).

## Main recommendation

Optimize the path from a demonstrated problem to a verified merged change before
increasing the number of simultaneous tasks. Prioritize dependency resolution,
clear ownership, cheap local feedback and controlled integration; add agent or
runner capacity only after measuring a capacity bottleneck.

This is a working hypothesis, not a measured attribution of development time.
[PR #522](https://github.com/buster14a/buster/pull/522) is a concrete example:
its MIR-only dispatch implementation is published, but its description says
**Do not merge yet** pending supported-input coverage and retirement acceptance.
At review, its issue API snapshot was non-draft. Published implementation and
merge readiness are different states; that example does not establish a
repository-wide rate of readiness mistakes.

Distinguish three metrics throughout: time to build Buster, time for Buster to
compile a workload, and execution time of the generated program. This plan
primarily concerns the first metric and the surrounding development workflow.

## 1. Work on the critical path, including maintainer decisions

Reuse [#36](https://github.com/buster14a/buster/issues/36) as the retirement
coordinator. Its recorded separation of implementation, acceptance and final
cutover already supplies the relevant dependency structure.

| Category | Existing owner | Development-speed action |
|---|---|---|
| Supported compiler behavior | [#70](https://github.com/buster14a/buster/issues/70), [#507](https://github.com/buster14a/buster/issues/507), coordinating with [#506](https://github.com/buster14a/buster/pull/506) | Assign coherent remaining cases with reproducers and bounded regression coverage; do not duplicate an active implementation. |
| Workload and budget decisions | [#508](https://github.com/buster14a/buster/issues/508), [#511](https://github.com/buster14a/buster/issues/511) | Obtain explicit maintainer decisions instead of asking implementation sessions to infer acceptance. |
| Semantic and reproducibility acceptance | [#509](https://github.com/buster14a/buster/issues/509), [#510](https://github.com/buster14a/buster/issues/510) | Provide the required environment and preserve independently replayable evidence. |
| Dedicated-host performance acceptance | [#512](https://github.com/buster14a/buster/issues/512), reusing [#422](https://github.com/buster14a/buster/issues/422) and [#437](https://github.com/buster14a/buster/issues/437) | Finish the admitted host/profile and approved contract before expecting a final performance verdict. |
| Cutover and deletion | [#513](https://github.com/buster14a/buster/issues/513) / [#522](https://github.com/buster14a/buster/pull/522), then [#514](https://github.com/buster14a/buster/issues/514) | Prepare dependent patches in parallel where useful, but land in acceptance order and rerun affected gates on the integrated revision. |

The September 12 snapshot of #36 says the then-current #504 evidence artifacts
expire on **September 19, 2026**. Recheck actual retention and finish #510's durable
archive/replay work before losing evidence. This note does not claim that
archival has occurred or that every historical artifact remains downloadable.

Do not make final-tree validation a circular prerequisite to preparing the
cutover patch. Equally, do not treat preparation as permission to merge before
the pre-cutover acceptance conditions are met. The numeric retirement budgets
in #511 require actual approval; this note approves none of them.

## 2. Limit overlapping work, not useful parallelism

Proposed pilot: one active implementation owner per tightly coupled area, with
at most two genuinely independent compiler implementation tasks plus one
validation/infrastructure task initially. This is an experiment, not a proven
optimum, a mandatory staffing level or a new repository rule. #532 owns the
cohort, exceptions, measurement and retain/adjust/abandon decision.

Different issue numbers are insufficient evidence of independence. Compare the
shared representation, invariants, interfaces and primary symbols being changed.
One integration coordinator should resolve coupled changes and ownership
handoffs. After interruption, confirm that an earlier owner is no longer active
before taking over; do not infer abandonment from an old branch alone.

Keep one current record in each existing task issue:

```text
Owner/session and last confirmed handoff:
Base revision and branch:
Exact observable behavior being changed:
Primary symbols and shared interfaces:
Overlapping work and dependencies:
Smallest reproducer and required gates:
State, current blocker, and phase to resume:
```

Use explicit states: investigating, implementing, blocked, ready for validation,
ready to merge. Represent blocked PRs visibly using the existing draft/status
mechanism. A status label does not substitute for reading the actual evidence.
Do not change another task's owner or readiness without coordination.

## 3. Assign executable contracts to agents

Use one observable behavior, its implementation, registered regression coverage
and exact acceptance evidence as the unit of implementation work. Avoid combining
an open-ended audit, architecture redesign, speculative optimization and a full
migration into one assignment.

A reusable implementation prompt:

```text
Resolve ISSUE in buster14a/buster as an implementation task.

Read current AGENTS.md, the relevant subsystem guide, the issue's latest
comments, and overlapping PRs. Use the agreed base and preserve any explicit
continuation-branch dependency.

Before editing, reproduce the problem with the smallest relevant test and
establish the baseline gates required by the repository.

Implement one coherent fix. Preserve supported behavior, canonical IR,
the existing C architecture and the no-new-dependencies policy.
Do not mix unrelated cleanup or speculative optimization into the patch.

During iteration, run focused checks. Before claiming readiness, run the
required broader checks and verify the actual submitted/tested revision.

Deliver code, a registered regression, exact commands/results, and remaining
blockers. Distinguish PASS, FAIL and NOT RUN. Source inspection, a proposed
patch or fixture-only coverage is not completed implementation.
```

Research should end with one bounded decision or falsifiable hypothesis. A
negative result can be useful: retain the current policy when evidence does not
justify changing it. Record paid-for negative experiments so a new session does
not repeat them unknowingly. Keep usage-limited research sessions focused on a
decision that changes the next implementation step rather than surveying the
whole compiler again.

Separate the deliverables: research produces a decision; implementation produces
a tested diff; review challenges that diff; integration validates the combined
result. Review should ask which invariant can fail, whether the new regression
fails before the fix, and which supported cases are missing.

## 4. Make ordinary iteration cheaper without weakening acceptance

| Stage | Appropriate work |
|---|---|
| Establish a baseline | Required baseline checks and the smallest reproducer. |
| Iterate | Incremental build and focused affected checks. |
| Validate a stable candidate | Required full regressions, self-hosting, modes, sanitizers and affected compatibility harnesses. |
| Integrate | Required checks on the combined revision with exact source and test identities. |

Reuse configured builds. The build guide explicitly says `generate` deletes and
recreates its selected build directory. Never regenerate a directory while a
build, test, self-host or harness consumes it. Give independent tasks isolated
worktrees, build outputs and scratch/evidence directories, with explicit CPU and
memory budgets rather than accidental nested parallelism.

Use the supported split-translation-unit Debug configuration for local debugging
where appropriate, while preserving the production Release/unity contract.
The latest [#89 follow-up](https://github.com/buster14a/buster/issues/89#issuecomment-5648862766)
explicitly scopes module-inventory consolidation **without changing unity
policy**. Its older broad prescriptions are not prerequisites for improving
bookkeeping or the local loop. Do not turn this plan into a Release split,
shipping/test-executable separation, or new build-framework project.

Existing commands on appropriately configured trees include:

```sh
./build.sh build --config Debug -t ide
./build.sh build --config Release -t test_all
./build.sh test_self_host --config Release
./build.sh test_mode_matrix --config Release
```

These are established build/acceptance entry points, not a claim that all four
should run after every small edit. Consult the build guide for fresh-tree,
sanitizer, platform and bootstrap requirements. No new test-filter option is
asserted to exist by this note.

Measure a no-op rebuild, a representative one-file edit, a shared-header edit,
and the first useful affected-test result on a fixed host/configuration. Retain
raw timing, build work, linking time and memory observations. Start with the
existing CMake/Ninja/time-trace/test timing summaries rather than another timer
framework. Clean-build time alone does not characterize incremental development.

Make failures reproducible with one copied command where practical. Reuse the
existing harness's exact argv, source/binary identity, fixture, target, allocator,
frontend configuration and artifacts. Distinguish compilation, linking,
execution and evidence-validation failures. First inventory what is already
available; implement only demonstrated gaps. Never call cross-compilation an
execution pass or disable a failing check to make reproduction look successful.

## 5. Optimize CI for a trustworthy answer and controlled integration

Classify the observed delay before changing infrastructure:

| Observation | Appropriate response |
|---|---|
| Queue delay dominates | Check available capacity, admission limits and duplicated work. |
| One running job dominates completion | Optimize or partition that job while preserving coverage and reporting total cost. |
| A job hangs | Preserve its command, add useful progress/deadlines and retain failure evidence. |
| Cancellations or repeated reruns | Diagnose event, revision and cancellation behavior before attributing it to runner capacity. |
| Validated patches wait for review or a decision | Fix review, decision or integration ownership rather than buying runners. |

The inspected workflow already has PR and merge-group event support, scoped
supersession, and disabled matrix fail-fast. Do not commission another generic
concurrency rewrite without a reproduced gap. Preserve independent required
results and the fail-closed aggregate described by the testing guide.

Reuse [#333](https://github.com/buster14a/buster/issues/333) for its desktop
critical-path partitioning scope and [#409](https://github.com/buster14a/buster/issues/409)
for its packaging work. [PR #524](https://github.com/buster14a/buster/pull/524)
reports macOS x86-64 packaging/upload improving from 112 seconds to 22 seconds
in its historical comparison, while the whole-workflow median was slower as
other work varied. That is a published packaging result, not a fresh measurement
or proof of an end-to-end development speedup.

Use one controlled integration queue for coupled changes instead of rebasing
all active work after every unrelated merge. Evaluate a platform merge queue
only after checking actual repository rules and required-check semantics; its
activation is not established here and no setting was changed. Existing
`merge_group` workflow support does not prove that a queue is enabled.

Record the PR head, base and actual tested merge/group revision separately. A
green result for an earlier or different revision cannot certify the candidate.
Follow the existing rebase-validation workflow; this plan does not replace it.

## 6. Prefer changes that make subsequent changes cheaper

Prioritize precise verifier failures, deterministic reproduction, narrow tests,
actionable diagnostics and authoritative ownership of shared compiler facts.
Judge each by the recurring debugging or implementation step it removes, not
by how many abstractions it adds. Reuse [#90](https://github.com/buster14a/buster/issues/90)
for its phase-contract scope rather than opening another broad refactor epic;
reproduce historical claims against the current tree before implementation.

For a compiler defect, use: small C reproducer -> first incorrect representation
or boundary -> focused regression -> minimal coherent fix -> wider acceptance.

Completing accepted backend retirement can remove duplicate semantic maintenance;
premature deletion merely exchanges maintenance work for regressions. Defer
unrelated target expansion, sweeping file moves and speculative SIMD changes
unless they remove a demonstrated blocker or fit an explicitly independent task.
Preserve the sole active C frontend and existing canonical-IR architecture.

## Measurement and next decisions

Track edit-to-feedback, stable-candidate-to-merge, decision/review/CI delays and
rebase/review rework. Count accepted behavior changes, not prompts, audit pages
or raw PR count. Missing timestamps are unknown, not zero. Report unfinished,
blocked and abandoned tasks at the evaluation cutoff; do not select only merged
successes. Do not sum overlapping blocked and CI intervals twice.

[#532](https://github.com/buster14a/buster/issues/532) owns the small operational
pilot and its evidence. It can use existing issue/PR events and a Markdown table;
a dashboard, daemon or custom agent scheduler is unnecessary. An inconclusive
result is acceptable and does not justify a claimed productivity multiplier.

First resolve human-owned retirement decisions and archival risk, then make task
ownership/readiness explicit, then shorten the measured edit-to-answer bottleneck.
Only afterward decide whether additional parallel agents or runner capacity are
the best next investment. Saving this plan does not complete any implementation,
approve a retirement budget or close #36/#532.
