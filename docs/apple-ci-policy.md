# Apple CI architecture policy (#1986)

Current scope: automatic CI maintains the headless compiler on Linux, Windows
and macOS. Application, graphics/UI, image/font and mobile-hosting sources remain
preserved but are not built or tested automatically. Earlier mobile/component
commands and measurements below describe preserved paths or historical runs,
not current CI coverage. See [current build guidance](agents/build.md) and
[component status](../PROJECTS.md).

Routine CI validates Apple platforms on AArch64 / Apple Silicon only. Apple
x86-64 source and compiler-target compatibility remains best-effort. Linux
x86-64 and macOS AArch64 successes do not establish Intel Apple correctness.
There is no CI-backed Intel or universal Apple release-artifact promise.

## Job and target inventory

Compared with main `6eb73e977c32699d8828ca73b05ded20262aec28`:

| Entry point / lane | Before | Current |
|---|---|---|
| Buster CI: macOS x86-64 release and checks | 2 Intel jobs; 5 required desktop configurations in total | Removed |
| Buster CI: macOS x86-64 native | 1 Intel job; mode matrix and native differential | Removed |
| Buster CI: iOS x86-64 | 1 Intel job; Debug/Release compile, link and bundle checks; runtime already excluded by the Xcode 26 policy | Removed |
| Retirement evidence: strict macOS x86-64 differential | 1 Intel job | Removed |
| Buster CI: macOS AArch64 release and checks | 2 jobs; 5 required desktop configurations | Unchanged |
| Buster CI: macOS AArch64 native | 1 job; modes and differential | Unchanged |
| Buster CI: iOS AArch64 | 1 job; Debug/Release simulator execution and cleanup | Unchanged |
| Retirement evidence: strict macOS AArch64 differential | 1 job | Unchanged |

Buster CI has **21 jobs instead of 25**: ten desktop combination shards, five
native jobs, two mobile jobs, lint, UEFI, analyzer and `CI complete`. Its Apple
runner demand is **four jobs instead of eight** on ready PRs, merge groups,
main/tag pushes and default manual runs. Draft deferral remains available for
the four retained Apple jobs; reruns execute those jobs on Apple Silicon.
Since #2659 the macOS AArch64 lane has independent owners instead of grouped
`checks`. #2657 then moved sanitized Debug into build-only `portability`
coverage and removed the separate full-runtime `sanitized-debug` jobs, so
macOS AArch64 has `release`, `sanitized-release` and `portability` owners,
Buster CI has 25 jobs and its Apple runner demand is **five jobs** per full
run; draft deferral covers all five.
The strict retirement workflow has five native hosts instead of six.

All active GitHub workflows and their matrices are covered by the workflow
policy regression. The inspected tree contains no Forgejo workflow definitions
or reusable Intel Apple workflow. Apple-hosted auxiliaries use the explicit
`macos-26` Apple Silicon runner, including mobile monitor `launch-trace`.
The mobile `fake-tools` matrix retains the documented temporary `macos-15`
exception below. No Rosetta, nightly replacement or opt-in Intel job is introduced.
Unused Intel resource samplers, runner lint registration, iOS CI architecture
selection and the Intel macOS Zig download pin are removed.

Linux and Windows retain both architectures. Android retains its x86-64
emulator; both UEFI targets and all existing allocator obligations remain.
Retained Apple configurations keep their sanitizer, fuzz, unity/non-unity,
self-host, mode, differential and simulator obligations. No retained job's
deadline or required result is weakened. Artifact names remain keyed by the
actual surviving lane; none expects an Intel producer.

The Actions inventory gate requires exactly the current 21 names and actual
successful steps; only stale step metadata on a completed green job may pass
on its job conclusion, with a logged
[waiver](ci-github-actions.md) (#4102). Missing, failed, skipped, cancelled, duplicate or foreign
retained work fails. Historical timing inventories remain accepted only by
the timing reader and stay in separate workflow/runner cohorts; they cannot
certify a live `CI complete` run.

The merge-queue watcher's live step budgets match the five desktop release
lanes. Its separate historical Intel budget preserves cancellation of old
workflow revisions and the #1866 incident regression; it schedules no work.

## Mobile monitor runner policy (#3489)

[The mobile monitor workflow](https://github.com/buster14a/buster/blob/801a7b338f0230583d7d70e9e76ec600f3ebd43a/.github/workflows/ios-monitor-tests.yml) keeps
three separate jobs:

| Job | Runner selection | Job cap |
|---|---|---|
| `launch-trace` | `ubuntu-26.04`, `macos-26` | 5 minutes |
| `fake-tools` | `ubuntu-26.04`, `macos-15` (temporary exception) | 10 minutes |
| `native-probes` | `macos-26` | 30 minutes |

The Linux siblings and Darwin controls remain required by their existing
path-filtered pull-request/main-push workflow; it has no merge-group trigger.
The iOS controls use `/bin/bash`; Android controls use Homebrew Bash and GNU
coreutils on macOS. Both matrix jobs retain Python, system/selected Bash and
the positively verified GNU timer path/version alongside checkout and machine
records. Artifact names remain keyed by `matrix.os`, run ID and attempt.

Runner migration qualification requires repeated hosted execution of both
complete matrix suites on the recorded candidate/checkout, retaining every
attempt's image, tools, results and artifacts. Passing native simulator probes
alone does not qualify these different suites. The selected image does not
establish a speedup or resolve the independent lifecycle failures in
[#2684](https://github.com/buster14a/buster/issues/2684) or
[#3479](https://github.com/buster14a/buster/issues/3479). All inner deadlines,
child-process ownership/cleanup assertions and the 5/10/30-minute caps remain.
The maintained workflow-policy regression covers both matrix selections,
the native-probe runner, caps and artifact templates, including the single
explicit exception below.

### Temporary fake-tools version exception

- **Purpose:** retain the existing Darwin lifecycle environment while full
  macOS-26 migration qualification is blocked. This is a qualification hold,
  not a macOS-15-only SDK or compatibility requirement.
- **Evidence:** candidate `179c6520b87eab6d35502ff07ea2ab05c08bfc95`, actual
  PR merge checkout `9f3ac93b519ec836f806aeb3e03a2890858d3a37`, image
  `macos-26-arm64` / `20260907.0351.1` (macOS 26.6.2).
  [Execution 1 passed](https://github.com/buster14a/buster/actions/runs/38136064255/job/114460058889);
  [execution 2 failed](https://github.com/buster14a/buster/actions/runs/38136064255/job/114461666348)
  with `attached_status=1`: `rejected-probes` returned 143 instead of 1
  after the expected missing-marker diagnostics and simulator-shutdown message.
  Its [retained failure artifact](https://github.com/buster14a/buster/actions/runs/38136064255/artifacts/11701860722)
  records the full attempt. Android attribution/summary controls were skipped
  because the attached group failed, so that execution is not a suite pass.
- **Owner:** build-test mobile CI policy, tracked by
  [#3489](https://github.com/buster14a/buster/issues/3489); the attached failure
  diagnosis remains with [#3479](https://github.com/buster14a/buster/issues/3479)
  and the separate job-cap diagnosis with #2684.
- **Review condition:** re-evaluate after the attached lifecycle failure is
  attributed/fixed, and whenever the monitor runner policy changes. Remove the
  pin only after repeated complete hosted `fake-tools` executions pass on the
  recorded macOS-26 candidate with unchanged controls, inner deadlines and
  ten-minute cap. Preserve unsuccessful attempts; do not retry until green.

The observed failure shape also occurs on macOS 15. Retaining that label
does not certify the older image as reliable or blame the newer image.
`launch-trace` has its own qualification and no version exception; neither
the Linux sibling nor `native-probes` substitutes for the full lifecycle suite.

## Compiler and retirement boundaries

The compiler's Apple x86-64 ABI, machine-code, Mach-O and platform sources are
retained. `build.c` still exposes the original six desktop policy plans for
best-effort local diagnostics. Cheap architecture/format unit checks, including
the Linux-hosted Mach-O linker fixture, remain; they do not prove native Intel
Apple execution. No workflow publishes a validated Intel/universal release.

The retirement corpus, target cross-product, supported-gap ledger, SDK pins,
acceptance thresholds and archived evidence populations are unchanged. The
cross-target census still includes Apple x86-64. Removing an Intel host does
not certify those targets' native execution. A future retirement decision that
needs Intel host evidence must account for its unavailability separately.

`tests/ci_tools_test.py` is a pinned support file. Its updated scheduling
expectations require an exact byte/hash ledger update, with the same 559 inputs
and 411 subjects. Install the separate bootstrap that admits the new declaration
digest before authorizing this policy transition through
[trusted integration](native-retirement-rebinding.md#trust-transitions).
The source PRs do not refresh either integration-owned generated identity.
Existing declarations and historical timing fixtures remain admissible under
their original identities; old failures are not reinterpreted as passes.

## Work reduction and observations

Four Buster CI jobs and one auxiliary strict job are no longer requested. This
is a support-policy reduction, not an optimization preserving coverage. #1806's
recorded Intel desktop release/checks execution was 24m58s in one September 29
run, excluding native/iOS work. That is historical evidence, not a measurement
of this change or a prediction of queue/critical-path savings. Report observed
latency and summed runner minutes separately from these exact job counts.
