# Apple CI architecture policy (#1986)

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
The strict retirement workflow has five native hosts instead of six.

All active GitHub workflows and their matrices are covered by the workflow
policy regression. The inspected tree contains no Forgejo workflow definitions
or reusable Intel Apple workflow. Other Apple-hosted auxiliaries use `macos-26`; the fake-tool mobile
lifecycle lane retains its existing Apple Silicon `macos-15` label. No Rosetta, nightly replacement or opt-in Intel job is introduced.
Unused Intel resource samplers, runner lint registration, iOS CI architecture
selection and the Intel macOS Zig download pin are removed.

Linux and Windows retain both architectures. Android retains its x86-64
emulator; both UEFI targets and all existing allocator obligations remain.
Retained Apple configurations keep their sanitizer, fuzz, unity/non-unity,
self-host, mode, differential and simulator obligations. No retained job's
deadline or required result is weakened. Artifact names remain keyed by the
actual surviving lane; none expects an Intel producer.

The Actions inventory gate requires exactly the current 21 names and actual
successful steps. Missing, failed, skipped, cancelled, duplicate or foreign
retained work fails. Historical timing inventories remain accepted only by
the timing reader and stay in separate workflow/runner cohorts; they cannot
certify a live `CI complete` run.

The merge-queue watcher's live step budgets match the five desktop release
lanes. Its separate historical Intel budget preserves cancellation of old
workflow revisions and the #1866 incident regression; it schedules no work.

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
