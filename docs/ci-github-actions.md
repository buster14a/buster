# GitHub Actions CI

`.github/workflows/ci.yml` owns the active GitHub CI matrix on **standard**
hosted runners. The former Forgejo workflows and source-free broker were
removed in commit `02c0400a34d04be9e984f29a59291750b3998d3f`; they do not run
alongside this matrix. See the [broker retirement record](ci-github-hosted-runners.md).

Linux and Windows cover x86-64 and AArch64; routine macOS CI covers AArch64.
The table below lists the active desktop runner labels.

| Runner | Architecture |
|---|---|
| `ubuntu-26.04` / `ubuntu-26.04-arm` | x86-64 / AArch64 |
| `macos-26` | AArch64 |
| `windows-2025` / `windows-11-arm` | x86-64 / AArch64 |

These are the newest label of each pair. `ubuntu-26.04` and `ubuntu-26.04-arm`
are still labelled *public preview* by GitHub; the rest are production-ready.
`.github/actionlint.yaml` has to name every one of them, because actionlint
validates `runs-on` against a list baked into its own release.

See [main admission policy](main-branch-protection.md) for the live ruleset,
required independent workflows, review ownership, and verification limits.

## Two gates

Workload jobs carry:

```yaml
if: ${{ github.server_url == 'https://github.com' && vars.GH_ACTIONS_CI_ENABLED == 'true' }}
```

The required `CI complete` job keeps only the server guard plus `always()`: it
must fail when the workload jobs are disabled or skipped. Independent required
checks likewise execute and fail an explicit enablement check instead of skipping.

The server guard keeps GitHub runner labels from being scheduled by another
forge that reads `.github/workflows`. On Forgejo the expression is false — or
empty, which is also false — so
the job skips and no status context is created.

The second half keeps the workflow inert until the repository variable
`GH_ACTIONS_CI_ENABLED` is set to `true`. A private repository draws on a
monthly included-minutes allowance in which macOS minutes count tenfold, so the
workflow stays off until the repository is public, where standard runners are
free.

To enable it:

```sh
gh variable set GH_ACTIONS_CI_ENABLED --body true --repo OWNER/REPOSITORY
```

## What runs

The `test` matrix retains five desktop runner labels, with fourteen internal combination
jobs: three owners (`release`, `sanitized-release`, `portability`) each on
Linux x86-64/AArch64, macOS AArch64 and Windows x86-64, and two owners on
Windows AArch64. Sanitized Debug is build-only `portability` coverage; the
checks-enabled sanitized Release job owns sanitizer runtime (#2657). Five independent `native` lanes
run the execution-mode suite. The three Unix
lanes additionally run the configuration-differential suite, reusing their
fresh Release compiler; the two Windows lanes report the mode gate
independently. Mobile retains its two independent suite-level shards; lint,
UEFI and the independent analyzer remain required. **Require `CI complete`**,
which checks all groups and the exact 25-job inventory, including all fourteen
desktop partitions and all five native jobs, for full executions. On a
qualifying same-commit main push, twenty-three native/mobile/UEFI, desktop and analyzer validation jobs are instead
proven by the exact queue run while desktop cache publication, lint and the analyzer receipt run on main;
see [queue-to-main reuse](ci-main-reuse.md) for its admission and fallback.
The old six names alone do not
prove coverage. See [combination sharding](ci-combination-shards.md) for native
ownership, fail-closed completion, reproduction and mandatory performance
qualification; [Windows CI coverage](windows-ci-coverage.md) records the
Windows mode and compiler-policy contract; [suite partitioning](ci-suite-partition.md)
documents the earlier split.

| Work | Runners | Command |
|---|---|---|
| Combination matrix | `release` plus three isolated check owners on Linux/x86 Windows; grouped `checks` on macOS/Windows ARM | `BUSTER_MATRIX_SHARD=<shard>` + `test_all_combinations_ci` |
| Execution-mode matrix | all five independent desktop native lanes | `test_mode_matrix --config Release` |
| Native differential matrix | the three Unix native lanes | `test_differential --ide build/Release/ide --out <fresh-directory> --sanitize-oracle --jobs 4` |
| Android shard | `ubuntu-26.04` | `android/start_emulator_ci.sh start`, then `android/test_ci.sh --all` |
| iOS shard | `macos-26` | `ios/test_ci.sh --all` |

The native build driver still owns the complete compiler/configuration matrix,
including sanitized Debug (compile-link) and Release (runtime), fuzz policy,
static analysis and supported self-hosting. No configuration is removed; #2657
deliberately moved the sanitizer runtime suite from sanitized Debug to the
checks-enabled sanitized Release tree. Both mobile entry points
are standalone and retain Debug and Release. The existing Intel iOS gate is
compile/link/bundle-only; Apple Silicon retains simulator execution.

The main workflow covers pull requests (including forks), main pushes, tags,
merge groups and manual runs. Feature pushes use their PR run without a duplicate matrix.
The first attempt of a draft pull-request run defers the five macOS-runner
jobs to named Linux no-ops, `<job> (deferred for draft PR)`; merge groups
always run them, and `CI complete` rejects a deferral anywhere else. A
"Re-run failed jobs" attempt of a draft run carries its attempt-1 deferrals
forward unchanged (see
[draft pull-request deferral](ci-runner-queue.md#draft-pull-request-deferral)).
`fail-fast` is off
in all three matrices. Native and mobile lanes have no desktop prerequisite;
combination failure cannot hide their results or turn green. Within each Unix
native lane, the differential step still runs after mode failure unless
cancelled; Windows requires its independent mode result.

PR revisions and merge groups coalesce per PR/ref. Main/tag pushes and manual
runs have unique run-ID groups so neither active nor pending results are
superseded. Lifecycle PR updates separately cancel obsolete fake-tool runs.
The seven-day log artifacts, summaries, cache boundaries, reproduction commands,
and timing methodology are documented in [the workflow audit](ci-workflow-audit.md).

Linux and Windows run both desktop architectures; macOS runs AArch64.
Matching native legs execute rather than falling back to the disassembly
oracle: ELF on the two Linux runners, AArch64 Mach-O on macOS, and PE/COFF on
the two Windows runners. Foreign-format or foreign-architecture legs that cannot
execute on the current host remain explicitly oracle-checked.

Apple x86-64 is no longer scheduled by routine or default manual CI. Source
compatibility remains best-effort, and passing Linux/x86-64 or macOS/AArch64
does not validate Intel Apple execution or universal release artifacts. The
[Apple CI policy](apple-ci-policy.md) records the removed lanes and retirement
census boundary.

## Desktop and lint scheduling

Ordinary pull requests, tags and manual runs schedule desktop shards without
waiting for full workflow lint. The only desktop prerequisites are the
main-only reuse decision and the merge-group-only lint job; both are root jobs
and skip outside their own events. Native remains independent of desktop/lint.
Main pushes still wait for their exact-SHA reuse decision.

Merge groups retain the full lint preflight before desktop execution. The two
mutually exclusive lint jobs have equivalent explicit steps, enforced by a
policy regression, so every event executes the same full lint workload once.
The pinned actionlint and action-pin scanner do not support steps-list aliases. The executing job retains the
`Workflow lint` check identity. The inactive branch is explicitly named
`Ordinary lint (inactive)` or `Queue lint preflight (inactive)` and must be
skipped. Inventory and timing readers remove only that skipped branch; they
never rename it to a successful execution. `CI complete` requires successful
active lint and skipped inactive lint even when every workload already passed.
Failed, cancelled, missing or disabled lint cannot pass.

Measure scheduling separately from execution: workflow creation to job creation
includes dependency wait; job creation to start is runner queue delay; start to
completion is execution time. Total workflow latency ends at the last required
completion. Earlier desktop eligibility alone proves no whole-CI speedup.
The existing `github_ci_time` reports retain `job_dependency_seconds` from
workflow creation to job creation, `job_queue_seconds` from creation to start,
execution durations and workflow elapsed time. Dependency time includes
scheduler overhead; absent historical job-creation timestamps remain unknown.
Use the retained job API timestamps for overlap evidence.

## Supplementary bootstrap scheduling and tested revision

`Self-host fixed point` supplies the independent **Linux x86-64 bootstrap
evidence** check (spelled `Linux x86-64 bootstrap evidence` in GitHub). It
preserves the ordinary fixed point and alternate-backend gates, then runs the
three-generation repeated audit and full compiler regressions. It has no
prerequisite on the platform matrix and does not replace that matrix's
per-platform self-host coverage.

Buster CI retains pre-merge validation; the heavy audit is main-only (#3045):

| Event | Buster CI tested revision/scheduling | Heavy self-host audit |
| --- | --- | --- |
| Pull request opened, synchronized or reopened, including forks | GitHub PR merge revision (`GITHUB_SHA`); newer revisions supersede that PR's older run | No run |
| Push to `main` | Pushed commit; exact recent queue evidence may reuse the eight equivalent jobs | Full audit of the exact pushed SHA; every run retained |
| Tag push | Tag-selected commit; every run retained | No run |
| Merge group | Exact synthetic group; coalesced within its workflow/group ref | No run |
| Explicit workflow dispatch | Selected Buster CI revision; independent run-ID group | No dispatch trigger; rerun an original main-push run for recovery |
| Other branch push | No automatic run; validate its PR | No run |

GitHub supplies the PR merge revision to the Buster CI checkout. Check the
actual run/checkout SHA: the REST run's `head_sha` can identify the PR head,
while the runner checks out the merge revision. The separate head and merge
SHAs must not be substituted for one another in validation reports. Re-running
a job retains the original event SHA; a newer PR head requires its own run.
Branch protection requires `CI complete` and the other normal admission gates;
`Linux x86-64 bootstrap evidence` is post-merge detection. The
[ordered main-only rollout](self-host-audit.md#main-only-rollout-3045) removes
only that audit requirement after its compatibility prerequisite lands.
`CI complete` aggregates its lint, desktop, native, mobile, UEFI and analyzer
obligations; it is not a proxy for the separate bootstrap result. For a reused
main run, its receipt links the actual queue job executions and the skipped
main jobs remain visibly skipped. This documentation does not change rules.

Buster CI uses its workflow name and event in the concurrency key. Only its
PR and merge-group runs permit cancellation. Main, tag and manual runs include their
run ID: `cancel-in-progress: false` alone would still allow a newer pending run
to replace an older pending run. This also applies to benchmark-service policy
and both disposable systemd gate validations: their main-push invocations have
unique run-ID groups. The systemd gates retain their existing non-cancelling
candidate policy. Controllers that mutate shared state (native-retirement
catch-up, native-retirement automation and main integration reconciliation)
keep their fixed serial groups and use `queue: max` to retain up to 100 pending
invocations. Overflow beyond that bound can still cancel a run; waiting order
follows entry into the concurrency queue, not guaranteed event order. See
[main-push maintenance](main-push-maintenance.md) for exact-main side-effect guards. The bootstrap workflow no longer starts an
expensive audit for PR, group, feature, tag or dispatch events. Its exact
main-push runs use unique run-ID groups with cancellation disabled. This
source policy establishes no measured latency or runner-minute saving.

Fork validation uses only standard hosted runners, read-only contents access,
non-persisted checkout credentials, and no secrets or bootstrap caches.
GitHub's normal approval requirements still apply. The trusted cancellation
recovery workflow remains scoped to `Buster CI` and eligible same-repository
PRs; this change does not broaden recovery or the source-free broker.

The maintained entry preserves every frozen CI/action test on the legacy topology.
After the queue-lint transition it delegates to the full current policy suite;
it rejects a runner-only substitute. The protected trusted writer uses this
entry so the frozen support-file identities remain unchanged.

`python3 tools/ci_workflow_policy_test.py -v` preserves the frozen suite's
unaffected cases and checks the separate platform/audit event/concurrency contracts,
retained bootstrap command order, and the actual `CI complete` shell predicate
under all 625 combinations of success, failure, cancellation, skip and missing
results. The current suite also rejects unsuccessful or missing inactive lint results.
The frozen `tests/ci_tools_test.py` and `tests/action_pins_test.py` bytes remain
unchanged; only their obsolete topology and inventory assertions are replaced
in the maintained subclass. These checks validate the checked-in policy; they are not evidence
that a live fork, merge queue, manual dispatch or cancellation race was run.

## Bootstrapping and prerequisites

`build.c` is compiled with the Clang already installed on the image rather than
with TCC: the images ship no TCC, and modern macOS cannot run it. It lands at
`build/build` (`build\build.exe` on Windows), where `build.sh` and `build.ps1`
put it, because the superbuild writes that exact path into the manifest it
hands CMake — anywhere else and configure stops at
`BUSTER_SUPERBUILD_BUILD_DRIVER must name an existing absolute build driver`.
The combination matrix removes only the `build/build-*` trees it generates, so
the driver survives its own run.

The execution-mode matrix is the exception: it bootstraps a second driver into
`RUNNER_TEMP`, because its `generate` targets the default tree — `build/`
itself — and removes it, which would delete a driver inside between that
command and the next. Diagnostic transcripts also live outside that tree.

The combination matrix needs Clang, GCC, Zig and, on Windows, MSVC together.
The images provide all of those except Zig, so every desktop runner installs a
**pinned, checksummed** Zig from `ziglang.org` — version and per-target
SHA-256 both live in `.github/zig.json`, so a rerun of an old commit cannot pick up
a different toolchain. Only the upstream archive is cached, and every cache
hit is checked again before extraction. Only default-branch push setup saves
verified archives; no generated compiler or build tree is cached.
Three more image gaps are filled in place:

- **mold.** On Linux `build.c` defaults every non-Zig tree to `CMAKE_LINKER_TYPE=MOLD`
  and the images carry no mold, so the Linux desktop runners install the distribution's
  own package. The execution-mode matrix keeps its own `generate` spelled out
  with `--linker DEFAULT` regardless, so its tree is pinned rather than
  inherited: the matrix would otherwise generate one itself, and `--linker` is
  accepted by the `generate` command alone.
- **`gtimeout`.** The iOS simulator launcher bounds every step with
  `timeout(1)`, which macOS ships under neither name, so the iOS shards
  install Homebrew's `coreutils`.
- **The Android emulator system image.** The Linux image ships the SDK, the
  platform and the NDK but no system image, and it leaves `/dev/kvm` owned by
  root. The Android shard installs
  `system-images;android-35;google_apis;x86_64` and adds the udev rule that
  lets the unprivileged runner user accelerate the emulator; unaccelerated, an
  x86-64 system image boots far past the emulator's own timeout.

On Windows the native toolchain runs through `cmd` so its progress on stderr
cannot become a terminating PowerShell error record. The VS developer shell is
launched per target (`amd64` with `VC.Tools.x86.x64`, `arm64` with
`VC.Tools.ARM64`) but always with `-HostArch amd64`: `Launch-VsDevShell.ps1`
validates that parameter against `x86,amd64` alone, so the AArch64 runner
drives an emulated x64 toolchain host at an arm64 target.

That shell also puts Visual Studio's own x64 clang ahead of the image's
standalone LLVM, which on the AArch64 runner emits x86-64 objects against the
shell's arm64 import libraries — every link then fails on `strlen` and
`__imp_GetCommandLineW`. The latest stable LLVM installed by
[`tools/ci_llvm.py`](ci-llvm.md) (`BUSTER_CI_LLVM_BIN`) is therefore prepended
after the shell is entered, and the step asserts clang's default target
matches the runner rather than letting a wall of unresolved externals explain
it a minute later. Every Windows step enters the shell through
`tools/ci_vs_dev_shell.ps1` (tested by `tools/ci_vs_dev_shell_test.py`); its
options keep each step's own scope, so the MSVC reference differential still
skips the LLVM prepend and the clang probe. Each of that test's seven helper
probes is still its own `powershell.exe` step process with its own files, but
they launch together from `setUpClass`: one Windows PowerShell start takes
about 23 s on the hosted AArch64 runner, and serial starts pushed the shared
workflow-tools step past its five-minute budget
([#2021](https://github.com/buster14a/buster/issues/2021)). That step now
runs its suites in concurrent lanes; see
[bootstrap wrapper CI](ci-bootstrap-wrapper.md#required-gate-and-budgets).

The native driver selects `gcc-15` for the macOS GCC row and verifies its
preprocessor identity before configuration. `BUSTER_GCC` can select a different
installed GCC explicitly; missing compilers and Clang shims fail the row rather
than substituting another compiler. Discovery prints the resolved executable,
identity, target and version in `combinations.log`. The regression also records
the unversioned `gcc` identity on both macOS architectures and checks that
Clang and missing-compiler failures preserve an existing build tree. The GCC
row remains an unsanitized Debug compilation with warnings as errors; all
AppleClang, Zig, sanitizer, unity/split and static-analysis work is retained.

The baseline main run [34708311595](https://github.com/buster14a/buster/actions/runs/34708311595)
at `f75949b0e27820b02a0fe337b58db1fe700e8f54` reported Apple Clang
21.0.0 for `gcc` on both macOS architectures, confirming the former coverage
gap. [PR #500](https://github.com/buster14a/buster/pull/500) records the
replacement compilers, exact tested revisions and complete CI results. Discovery
alone does not establish compilation: the GCC Debug build and the full macOS
matrices must also pass.

The self-host fan-out runs only where the fixed point exists —
the x86-64 Linux and Windows runners and macOS AArch64 — so the Linux and
Windows AArch64 rows build and test without it.

## What does not run here

- **Wine and `qemu-user`.** The images carry neither. Each native runner executes
  the matching operating-system format and architecture directly; unsupported
  foreign formats or architectures remain oracle-checked rather than silently
  emulated. Across the five native runners, both ELF and PE/COFF architectures and
  AArch64 Mach-O have a real host execution avenue. Intel Mach-O does not.
- **The performance series.** Hosted virtual machines expose no performance
  counters, and their wall times are too noisy to trend. `STEP_INSTRUCTIONS`
  needs hardware under your control. CI elapsed time and total job seconds
  are distinct operational metrics, not compiler-throughput measurements.
- **A trusted compiler artifact.** Nothing here bootstraps through TCC, so
  nothing it produces is a reusable toolchain.

A self-hosted runner attached to a **public** repository executes code from
fork pull requests on your own machine. If the performance series moves to a
self-hosted runner here, restrict its jobs to `push` on the default branch,
require approval for fork workflow runs, and keep the machine off any private
network it does not need.

## Local verification

```sh
go run github.com/rhysd/actionlint/cmd/actionlint@03d0035246f3e81f36aed592ffb4bebf33a03106 .github/workflows/*.yml
```

Without `.github/actionlint.yaml` every `runs-on` above is reported as an
unknown label, so keep the two in step when a runner changes.

## Helper validation and timing

`python3 tools/ci_workflow_policy_test.py -v` exercises the archive installer, fail-closed
summaries, native evidence packer and timing collector on each desktop platform
(`python` on Windows).
`python3 tools/ci_artifact_upload_test.py -v` runs in required Workflow lint.
It checks the local upload action's two approved pins, failed-first-attempt and
cancellation guards, identical artifact options, replacement on retry and
blocking terminal failure, then executes its Bash backoff and reporting bodies.
The backoff uses a fake sleep command so this offline test does not wait.
These regressions leave the frozen support tests and their reviewed identities
unchanged. They cover upload failures after the action starts; action dependency
resolution before composite execution is tracked separately in #1790.
`python3 tools/analyzer_selection_test.py -v` separately exercises the analyzer
comparison-selection record, conservative event fallback, baseline-path
admission and reference/candidate failure propagation on Unix runners.
Unix runners also compile a tiny Clang probe to verify that recoverable UBSan
diagnostics become fatal with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
The same environment applies to the compiler matrix; no sanitizer is suppressed.

`tools/ci_zig.py` owns download, checksum verification, staging, version checking,
and PATH publication. The exact archive cache key includes the manifest hash;
only main pushes save verified archives, before compiler tests run. SDK setup
for unused Vulkan rendering/shader support is removed; those options are off
in these configurations. Android and iOS SDK setup and test commands remain.

Desktop, native and mobile summaries use `tools/ci_summary.py`, explicitly requiring each
applicable suite. Missing, skipped, cancelled or failed work fails the summary.
Diagnostic uploads do not start after cancellation. Native lanes upload one
verified `native-ci-logs.tar.gz` beside `result.json` and `summary.md`, packed
by `tools/ci_pack_evidence.py`; a packing failure fails the lane and uploads the
unpacked tree instead. See
[native evidence packaging](ci-suite-partition.md#native-evidence-packaging).
The aggregate `CI complete` requires all fourteen desktop combination jobs, five
native jobs, two mobile jobs, workflow lint, UEFI and the analyzer. Its
read-only Actions inventory rejects missing shard identities even when a
smaller surviving matrix group reports success. It selects each logical job's
latest attempt from the exact run and source head, then requires a unique
successful record for every required desktop/native step. GitHub can briefly
publish job or step metadata before those records are complete, so the gate
re-reads inconsistent inventories with 1/2/4-second backoff, at most three
refreshes and a 30-second total metadata budget. A later exact snapshot can
recover a transient omission; a persistent empty, stale or ambiguous record
fails closed. It never borrows required-step proof from an older attempt when a
newer attempt shadows that job. A completed job whose API record has
`steps=[]` is reported as that distinct case and stays unresolved; it is never
treated as success. Transient API reads (HTTP 5xx or 429, connection loss or a
timeout) retry inside the same 30-second budget. The run read retries with the
same backoff. A failed jobs page discards that snapshot and is re-read as a
pending refresh; after a 5xx, later snapshots request 30 jobs per page instead
of 100. Any other HTTP status, and every identity, pagination or consistency
violation, still fails immediately. The retained `desktop-partitions.json`
records the exact run/head, final job attempts, observed required-step status
and conclusion, refresh count, the final page size, the outcome of every API
read, and any unresolved proof errors. An unsuccessful verdict also prints
those errors, with run, attempt, head, snapshot and refresh counts, to the job
log and step summary; a raised read or input error prints `CI timing failed:`
instead. A green job-level conclusion alone cannot pass the gate.

A hosted runner that stops reporting cannot run its own summary or upload
steps, so the gate also records controller-visible interruption evidence. For
each failed job that GitHub finalized with a step still `in_progress`, it reads
that check run's annotations (job-scoped `checks: read`, 30-second budget) and
adds an `interruption` record to `desktop-partitions.json`: the active and
pending steps, runner name/labels, last reported step transition, finalization
time, `silent_seconds` between them, and the annotations. It also prints a
`CI_RUNNER_INTERRUPTION` line to the gate's log. The classification is
`runner-communication-lost` only when GitHub's own annotation says
`The hosted runner lost communication with the server.`; otherwise it is
`unterminated-step`, or `annotation-unavailable` when the read fails. The
record never changes the gate's verdict and does not identify a root cause.
`silent_seconds` bounds the unobserved tail; it is not compiler work. Retry
policy is unchanged: see [cancellation recovery](ci-cancellation-recovery.md).

The Android summary also exposes the existing wrapper records from
`RUNNER_TEMP/buster-ci/android.log` in both `summary.md` / the job summary and
`result.json`'s `android` field. Its two configuration rows show batch status,
payload phase/status, monitor reader/producer statuses, the deadline, and the
payload's elapsed time and remaining headroom. Separate
batch and final-CI records distinguish a failed Debug payload followed by a
passing Release from required emulator cleanup failing after successful tests.
A terminal `BUSTER_ANDROID_TEST_RESULT:0` describes one payload, not the entire
Debug/Release job. Monitor reader status 10 denotes the success marker. The
wrapper then sends `SIGTERM` to the GNU `timeout`/`adb logcat` process group and
records the raw status returned by waiting for the `timeout` group leader. The
real emulator lane's `adb logcat` exits normally with status 15 after that stop,
so `timeout` propagates 15. The offline shell fake is instead terminated by
signal 15, so `timeout` re-raises `SIGTERM` and Bash reports `128 + 15 = 143`.
Both 15 and 143 are normal signal-derived producer statuses after reader status
10; the reader result remains authoritative.

A `Payload deadline:` line names either outcome the table alone leaves implicit.
Reader status 0 with producer status 124/137 is an exhausted payload deadline —
the wrapper says so at the failure itself, reports how many log lines the payload
emitted with a truncated last line, and states that emulator cleanup has not run
yet; that tail is printed by the workflow trap only when the status is already
nonzero, so it is never the cause. A configuration that passes with less headroom
than `BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT` (default 25) of its deadline
is reported as thin on a green run, before a slower runner or new tests cross it.
Headroom reporting changes no deadline and fails nothing by itself.

These are diagnostics, not a replacement acceptance gate: the existing required
step outcomes remain authoritative even when logs are missing or contradictory.
Only complete, anchored wrapper records are copied, never emulator text, command
echoes or arbitrary payload output. Missing/ambiguous records remain explicitly
missing; duplicate records cannot replace an earlier failure with a later pass.
The scan is capped at 64 MiB with 4 KiB line fragments and reports truncation.
`python3 android/ci_summary_test.py -v` covers these cases without an SDK; the
existing Linux/macOS mobile lifecycle workflow runs it independently and retains
`android-summary.log`. No payload deadline, cleanup policy or test selection is
changed. See [#685](https://github.com/buster14a/buster/issues/685) for the original
Debug-timeout/Release-success diagnosis and [#686](https://github.com/buster14a/buster/pull/686)
for the already-landed producer and lifecycle repairs.

### Why the runs in #685 failed

The three `Android x86-64` attempts cited in
[#685](https://github.com/buster14a/buster/issues/685) — runs `35008982632`
(`fe9569a`) and `35012645467` attempts 1 and 2 (`c6ccdc6`) — were not emulator
teardown failures. In each, the Debug payload reached its 60-second deadline and
`error: Android Debug tests failed with status 1` before Release ran and reported
`BUSTER_ANDROID_TEST_RESULT:0`; the success text quoted in that issue belongs to
Release alone. The extra Debug time came from an `O(values x rows)` residue in
the MIR debug-location recorder that put roughly 33 seconds into one
`codegen_tests` fixture, fixed inside
[#676](https://github.com/buster14a/buster/pull/676) (`f9f817d`), after which the
lane passed again on `af4efd6`. The same-day passing runs simply carried no such
payload regression. No deadline was raised, no cleanup policy relaxed and no test
skipped; what the diagnostics above add is that the next occurrence is legible
from the job summary instead of from a complete-log reading.

The iOS launcher retains separate signing logs for each Debug/Release bundle
and one shutdown log under `BUSTER_IOS_CONSOLE_LOG`; the GitHub mobile job
places these in `RUNNER_TEMP/buster-ci/`, inside its existing artifact. Each
phase keeps the first 64 KiB of combined raw stdout/stderr while draining the
remaining output, reports truncation, and records the exact shell-quoted
command, helper status, native command status when available, separate command
and capture elapsed times, receipt completeness, overall elapsed time and the
command deadline. An empty or missing capture receipt is explicitly incomplete
and cannot turn a successful command into a passing phase. Native exit 124 is an ordinary command failure;
helper exit 124 without a native completion record is a timeout. Status 137
without that record remains ambiguous between timeout escalation and a signal.
Each failed boot phase retains a separate bounded source SHA, Xcode/SDK, selected
device and runtime context, including the replacement device after exhaustion.
The context includes statuses/timings for exact-device inventory, runtime,
host memory and disk probes. A `Booted` inventory entry alone does not satisfy
readiness. Shutdown records the prior batch status and cannot
erase an earlier failure or turn otherwise successful tests green.

On an invocation-owned GitHub-hosted ARM64 simulator, a first true readiness
timeout is followed by one `bootstatus -b` continuation on that same device,
bounded to 120 seconds by default. Only if that continuation itself times out
does the existing exact-device replacement run. Its boot and readiness remain
bounded by 180 seconds each and it cannot trigger another continuation or
replacement. The default maximum command budgets across boot, continuation,
replacement shutdown/delete/create, replacement boot/readiness and final
shutdown sum to 1290 seconds; capture and diagnostics have their own finite
deadlines in addition to that sum. This does not change local, borrowed,
explicit or self-hosted devices, application-test retry policy, result-marker
validation or mobile coverage requirements. The controlled override
`BUSTER_IOS_BOOT_CONTINUATION_SECONDS` must be a positive integer and only
affects the hosted owned continuation. A deliberately shortened first-readiness
deadline keeps its original single-replacement path unless that override also
opts into continuation. `ios/hosted_signing_budget_test.py` covers the real
launcher with timeout, continuation, replacement and native failure fixtures.

`bash tests/mobile_ci_scripts_test.sh` covers nonzero and hanging commands,
bounded output, independent batch results and cleanup failure propagation.
The macOS lifecycle job first requires real CoreSimulator runtime discovery
within 180 seconds, retaining its output and status. The fake boot does not
initialize that service; startup must finish before measuring the native
negative case against the unchanged 30-second shutdown deadline. Readiness
failure or timeout fails the job, and the negative case still requires an
ordinary native error with diagnostic output, never a timeout.
It then invokes real codesign against an empty app and real simctl shutdown
against an invalid device ID. These intentional,
bounded rejections retain native diagnostics and Xcode/runtime provenance;
they do not diagnose the intermittent signing/shutdown stalls from #394.
Set `BUSTER_MOBILE_TEST_EVIDENCE_DIR` to retain each case's files; the lifecycle
workflow includes them alongside its established console-log artifact. The
attached launch-monitor ownership suite continues to use macOS `/bin/bash`.

Collect timing using `python3 tools/github_ci_time.py collect --branch main --limit 30 --output /tmp/before.json`
and summarize using `python3 tools/github_ci_time.py summarize /tmp/before.json`.
For a candidate, replace `--branch main` with `--head-sha COMMIT`. The collector
accepts historical six-, eleven-, fifteen-, seventeen-, nineteen-, twenty-three-
and twenty-five-job workflows plus the historical twenty-one-job and current twenty-seven-job layouts; all applicable suites must
succeed on a complete first attempt. All five current native jobs must report mode
success; historical layouts retain their original inventories. The three
current Unix native jobs must additionally report differential
success, and the UEFI and analyzer gates must report their key coverage steps.
Their execution intervals and runner seconds are included. Workflow hashes and
runner labels define separate cohorts. Reports include queue delay, elapsed
time, execution span and summed runner seconds, including mobile/lint/aggregate
jobs. Never attribute differences to this PR without matching source/cache state
and multiple completed observations. No speedup is claimed before that evidence.
Runner assignment latency and macOS capacity across all workflows are measured
with `queue-collect`/`queue-summarize`; see [ci-runner-queue.md](ci-runner-queue.md).

## Independent Clang analyzer gate

`Clang analyzer shards` analyzes the generated split-source Release database
with bounded module workers and mandatory fail-closed aggregation. Desktop
unity analysis remains covered. The job exercises native failure controls and
resolves the candidate and reference commit identities before analysis. Equal
identities on pushes and dispatches run the full candidate campaign once and
retain an explicit skip reason; pull-request, merge-group and distinct identities
keep the reference/candidate comparison on identical commands. Missing or invalid
selection evidence fails before candidate analysis; the campaign re-resolves the
identities and checks the complete retained record rather than trusting an
exported decision alone.
Both modes retain candidate coverage, diagnostics, timing and child RSS evidence,
and `CI complete` requires the result. See
[the analyzer contract and reproduction](clang-analyze-shards.md).

## Matched manual Zig cache cohorts

Ordinary `Buster CI` events admit the split checks owners on Linux x86-64,
Linux AArch64 and Windows x86-64. Every sibling uses the same exact Zig archive
key as its lane's Release owner. Ordinary publication still occurs only on a
default-branch push; unsupported split targets remain refused. Manual dispatches
retain the existing input surface and cache behavior. A deliberate matched
cohort selects its mode through the
workflow-dispatch ref, so the event contract remains identical to ordinary CI:

- `ci-cohort-prime-<namespace>` restores and, on a verified miss, publishes the
  exact arm-scoped Zig archive from the lane's `release` shard;
- `ci-cohort-read-<namespace>` requires that same exact frozen key and never
  writes it.

The suffix is the explicit cache namespace. It must contain 1–48 lowercase
alphanumeric words separated by single hyphens. Both refs for one arm must
point to the same reviewed commit. Any other selected ref is ordinary mode.
The effective key still binds runner OS and architecture, Zig target, and the
pinned `.github/zig.json` digest; it adds only the validated namespace.

## Native runner phase observations

Every native matrix lane retains calibrated, process-local phase evidence through its existing artifact. Provider preamble and Actions API clocks are joined only during audit; missing or contradictory identity is retained but cannot enter a performance comparison. See [Native runner observations](native-runner-observations.md).

## Intel-macOS runner interruption observations (#1749)

The Intel-macOS workflow-tool, Zig installation, desktop combination, native
mode/differential, and iOS simulator steps start a step-owned resource sampler.
It emits a `CI_RESOURCE_SAMPLE` JSON line immediately and every 30 seconds to
the live step log and to `resources-<phase>.jsonl` in the existing job artifact.
The step shell stops and waits for the sampler on exit; the sampler does not
change the payload result or start another build/test worker.

The resource cleanup regression waits for a complete first sample in its fresh
log before making the step fail with exit 7. Its readiness wait is bounded
inside the existing 12-second fixture timeout, and timeout cleanup owns the
shell's entire process session. A fixed interpreter-startup delay cannot prove
that the first sample was emitted; missing sample or END evidence still fails.

Each record identifies the phase, UTC time and elapsed time; it reports host
load, a descendant-only process count, CPU percentage, RSS, and the three
largest process names (no arguments or environment). It also reports macOS
memory-pressure free percentage, swap used, cumulative pageouts, and free disk
space under `RUNNER_TEMP`. A failed, unsupported, timed-out, or unparseable probe
records an explicit status or `unknown`, never zero. The CPU percentage is a
snapshot of processes still visible to `ps`; RSS is their current sum, not
peak memory or the runner's total footprint. `sampling_ms` measures each
observation's overhead. These fields are diagnostic only and cannot complete a
missing required job or replace the exact phase evidence.

Sampling starts after checkout, within the named step. It does not cover time
before that step or between steps. A runner that stops communicating may lose
both its artifact and the live log; the last available sample, if any, bounds
what was observed and cannot prove the cause of a later outage. The separate
`CI complete` interruption record in PR #1754 uses controller-visible job
metadata and annotations even when the runner cannot finish cleanup. Keep
failed-run elapsed time separate from successful-run performance in #709.

## Machine specifications in every executing job (#2758)

Every runner-backed job invokes the pinned shared machine-specifications action
as its first step, before checkout or workload setup. It bootstraps only the tiny
standalone C collector using preinstalled Clang; it does not build the project.
The report appears immediately in the log and job summary. Later workload
failure cannot erase it. Runnerless reusable-workflow callers report nothing;
the called executing jobs report their own machines. A skipped job or a job
cancelled before its first step has no report. Draft deferral jobs still execute
on a real host, so they report that observed host before the deferral decision.

The versioned JSON record is retained at
`RUNNER_TEMP/buster-machine-specifications/report.json` and emitted in the log
as `MACHINE_SPECIFICATIONS_JSON`. Existing evidence uploads also retain these
JSON files where uploads already exist. Jobs with no existing upload retain the
record in their log; no extra upload job is introduced. Each checkout appends
an actual `git rev-parse HEAD` identity to `sources.jsonl` and the summary; event
and workflow SHAs remain separate from those actual source identities. Host
architecture describes the executing OS; process architecture describes the
reporter executable. Neither describes an emulator guest or compiler target.
Guest identity remains explicitly unknown until the workload provides it.

Every record has the same allowlisted fields, each with value, status and reason.
Memory/storage use bytes. Topology describes OS-visible cores/sockets, not a
claim about the physical machine underneath a VM. Logical CPUs, affinity,
process availability, cpusets and CPU-time quotas are distinct. Linux inspects
visible cgroup v1/v2 membership and ancestor limits, keeping the smallest memory
limit and CPU quota; hidden host ancestors remain unobservable. macOS available
memory is explicitly a free-plus-inactive snapshot estimate. Windows pagefile
capacity is not fabricated from commit capacity; inaccessible fields stay
unknown. Dynamic or absent facts never become inferred zeroes. Unknown and
partial fields are visible diagnostics, with safe escaping in all outputs.

`tools/check_action_pins.py` and `tools/ci_job_environment_test.py` enforce startup order,
exact implementation/pin identity, runnerless semantics and actual-checkout
reporting for future jobs. Add the startup step and immediate checkout identity
steps whenever adding an executing job. Do not add conditions or error suppression
to the startup reporter. Update the pin, allowlist, implementation blob identities
and approval guide together when changing the reporter. Its native self-tests run
before collection on every executing platform. Collection overhead is recorded
as `collection_elapsed_ms`; Actions step timings include compiler bootstrap.

## Durable hosted execution history

[Hosted CI timing history](ci-timing-history.md) extends the existing operational
observations with bounded native collection, exact numeric-job machine joins,
append-only data-branch retention, and advisory CPU-aware reports. It preserves
`github_ci_time.py require-jobs`, existing queue/wait definitions and coverage.
Its guide records schema, commands, raw metric boundaries, missing context,
retention/recovery, statistical policy and outstanding live acceptance.

## No-code classification and staged admission (#3107)

The native build driver exposes `ci_no_code --repo PATH --base SHA --head SHA
--tested SHA --policy SHA --event pull_request|merge_group`. Its deterministic
`buster-ci-no-code-v1` record distinguishes `no-code` from `full`; it is a
classification decision, never execution or coverage evidence. The PR head,
event base, actual tested merge and independently trusted policy are separate
identities. Merge groups are independently evaluated against their actual base
and complete synthetic head.

The initial exact allowlist is `README.md`, `docs/compiler-lifetime.md`,
`docs/diagnostics.md`, and `docs/incremental-compilation.md`. Arbitrary Markdown,
agent instructions, executable policy under docs, source, tests, fixtures,
runtime assets, build inputs and workflow changes retain ordinary validation.
Only regular mode-100644 blobs are eligible. Additions/deletions are supported;
Git rename collapsing is disabled so both paths of a move are checked. Mode,
type and gitlink changes always select full validation. A bounded Git capture
failure, missing object, malformed raw diff or stale merge identity selects full
validation with a reason. No comment/whitespace stripping is attempted.

`./build.sh ci_no_code --self-test` exercises the raw parser. Hosted
`No-code classifier controls` also exercises authentic Git PR/group graphs,
mixed changes, a prose-only final commit on a code PR, and missing/stale objects.
The immutable trusted-base reader is installed before automatic omission.
During the transition it continues accepting genuinely executed legacy gates;
once producers declare no-code plans, deliberately omitted gates must be skipped
without runner allocation and their exact-attempt classifier must succeed.
Normal code changes still require every current gate. No-code records cannot
substitute for native-retirement execution, benchmarks or full queue-to-main
coverage. Explicit workflow dispatch continues to request normal/full work.
