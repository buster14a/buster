# GitHub Actions CI

`.github/workflows/ci.yml` runs the Forgejo matrix's coverage on GitHub's
**standard** hosted runners. It exists for the migration to GitHub and runs
beside the Forgejo matrix rather than replacing it while Forgejo is still the
source of truth.

Where Forgejo owns four fixed machines, GitHub hands out the newest image of
every operating system in both architectures, so this workflow spends that on
the axis the native runners cannot cover: **every desktop platform is tested on
x86-64 and on AArch64.**

| Runner | Architecture |
|---|---|
| `ubuntu-26.04` / `ubuntu-26.04-arm` | x86-64 / AArch64 |
| `macos-26-intel` / `macos-26` | x86-64 / AArch64 |
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

The first half exists because **Forgejo also reads `.github/workflows`**.
Without it, Forgejo would schedule this job against `runs-on: ubuntu-26.04`, a
label no Forgejo runner carries, and it would queue until the workflow timed
out. On Forgejo the expression is false — or empty, which is also false — so
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

The `test` matrix retains all six desktop runner labels, with two internal
combination jobs per platform: `<platform> release` and `<platform> checks`.
Six independent `native` lanes run the execution-mode suite. The four Unix
lanes additionally run the configuration-differential suite, reusing their
fresh Release compiler; the two Windows lanes report the mode gate
independently. Mobile retains its three independent suite-level shards; lint,
UEFI and the independent analyzer remain required. **Require `CI complete`**,
which checks all groups and the exact 25-job inventory, including all twelve
desktop partitions and all six native jobs. The old six names alone do not
prove coverage. See [combination sharding](ci-combination-shards.md) for native
ownership, fail-closed completion, reproduction and mandatory performance
qualification; [Windows CI coverage](windows-ci-coverage.md) records the
Windows mode and compiler-policy contract; [suite partitioning](ci-suite-partition.md)
documents the earlier split.

| Work | Runners | Command |
|---|---|---|
| Combination matrix | `release` and `checks` on each of six desktop labels | `BUSTER_MATRIX_SHARD=<shard>` + `test_all_combinations_ci` |
| Execution-mode matrix | all six independent desktop native lanes | `test_mode_matrix --config Release` |
| Native differential matrix | the four Unix native lanes | `test_differential --ide build/Release/ide --out <fresh-directory> --sanitize-oracle --jobs 4` |
| Android shard | `ubuntu-26.04` | `android/start_emulator_ci.sh start`, then `android/test_ci.sh --all` |
| iOS shards | `macos-26-intel`, `macos-26` | `ios/test_ci.sh --all` |

The native build driver still owns the complete compiler/configuration matrix,
including sanitized Debug/Release, fuzz policy, static analysis and supported
self-hosting. No configuration or test is removed. Both mobile entry points
are standalone and retain Debug and Release. The existing Intel iOS gate is
compile/link/bundle-only; Apple Silicon retains simulator execution.

The main workflow covers pull requests (including forks), main pushes, tags,
merge groups and manual runs. Feature pushes use their PR run without a duplicate matrix. `fail-fast` is off
in all three matrices. Native and mobile lanes have no desktop prerequisite;
combination failure cannot hide their results or turn green. Within each Unix
native lane, the differential step still runs after mode failure unless
cancelled; Windows requires its independent mode result.

PR revisions and merge groups coalesce per PR/ref. Main/tag pushes and manual
runs have unique run-ID groups so neither active nor pending results are
superseded. Lifecycle PR updates separately cancel obsolete fake-tool runs.
The seven-day log artifacts, summaries, cache boundaries, reproduction commands,
and timing methodology are documented in [the workflow audit](ci-workflow-audit.md).

Both architectures of all three desktop platforms run the execution-mode
matrix. Matching native legs therefore execute rather than falling back to the
disassembly oracle: ELF and Mach-O on the four Unix runners and PE/COFF on the
two Windows runners. Foreign-format or foreign-architecture legs that cannot
execute on the current host remain explicitly oracle-checked.

## Supplementary bootstrap scheduling and tested revision

`Self-host fixed point` supplies the independent **Linux x86-64 bootstrap
evidence** check (spelled `Linux x86-64 bootstrap evidence` in GitHub). It
preserves the ordinary fixed point and alternate-backend gates, then runs the
three-generation repeated audit and full compiler regressions. It has no
prerequisite on the platform matrix and does not replace that matrix's
per-platform self-host coverage.

Both workflows have the same event policy:

| Event | Checkout/tested revision | Scheduling |
| --- | --- | --- |
| Pull request opened, synchronized or reopened, including forks | GitHub's `refs/pull/<number>/merge` revision (`GITHUB_SHA`), not merely the head SHA | One run of each workflow per event; a new revision supersedes that PR's older run |
| Push to `main` | Pushed commit | Every run retained, including pending runs |
| Tag push | Commit selected by the tag event | Every run retained |
| Merge group | GitHub's generated merge-group revision | Coalesced only within that workflow and merge-group ref |
| Explicit workflow dispatch | Revision selected for that workflow dispatch | Independent run-ID group; no automatic coalescing |
| Other branch push | No automatic run | Open/update its PR, or deliberately dispatch a fixed-revision measurement |

GitHub supplies the PR merge revision to both default checkouts. Check the
actual run/checkout SHA: the REST run's `head_sha` can identify the PR head,
while the runner checks out the merge revision. The separate head and merge
SHAs must not be substituted for one another in validation reports. Re-running
a job retains the original event SHA; a newer PR head requires its own run.
Branch protection should require **both `CI complete` and
`Linux x86-64 bootstrap evidence`** when the stronger audit is mandatory.
`CI complete` aggregates its lint, desktop, native, mobile, UEFI and analyzer
jobs; it is not a proxy for the separate bootstrap result. No same-name skipped
check is introduced to stand in for a missing run, and this documentation does
not change repository rules.

Each workflow uses its own name and event in the concurrency key. Only PR and
merge-group runs permit cancellation. Main, tag and manual runs include their
run ID: `cancel-in-progress: false` alone would still allow a newer pending run
to replace an older pending run. The bootstrap workflow no longer starts an
expensive audit just because an inspection/transport branch is published.
This avoids automatic work on non-PR feature pushes, not deliberate main,
tag or manual validation. It does not establish a measured latency speedup.

Fork validation uses only standard hosted runners, read-only contents access,
non-persisted checkout credentials, and no secrets or bootstrap caches.
GitHub's normal approval requirements still apply. The trusted cancellation
recovery workflow remains scoped to `Buster CI` and eligible same-repository
PRs; this change does not broaden recovery or the source-free broker.

`python3 tests/ci_tools_test.py -v` checks the shared event/concurrency contract,
retained bootstrap command order, and the actual `CI complete` shell predicate
under all 625 combinations of success, failure, cancellation, skip and missing
results. These checks validate the checked-in policy; they are not evidence
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
`__imp_GetCommandLineW`. `C:\Program Files\LLVM\bin` is therefore prepended
after the shell is entered, and the step asserts clang's default target
matches the runner rather than letting a wall of unresolved externals explain
it a minute later.

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
the x86-64 Linux and Windows runners and both macOS runners — so the two
AArch64 desktop rows build and test without it.

## What does not run here

- **Wine and `qemu-user`.** The images carry neither. Each native runner executes
  the matching operating-system format and architecture directly; unsupported
  foreign formats or architectures remain oracle-checked rather than silently
  emulated. Across all six native runners, the matching x86-64 and AArch64 ELF,
  Mach-O and PE/COFF legs have a real host execution avenue.
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

`python3 tests/ci_tools_test.py -v` exercises the archive installer, fail-closed
summaries, native evidence packer and timing collector on each desktop platform
(`python` on Windows).
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
The aggregate `CI complete` requires all twelve desktop combination jobs, six
native jobs, three mobile jobs, workflow lint, UEFI and the analyzer. Its
read-only Actions inventory rejects missing shard identities even when a
smaller surviving matrix group reports success. It selects each logical job's
latest attempt from the exact run and source head, then requires a unique
successful record for every required desktop/native step. GitHub can briefly
publish job or step metadata before those records are complete, so the gate
re-reads inconsistent inventories with 1/2/4-second backoff, at most three
refreshes and a 30-second total metadata budget. A later exact snapshot can
recover a transient omission; a persistent empty, stale or ambiguous record
fails closed. It never borrows required-step proof from an older attempt when a
newer attempt shadows that job. The retained `desktop-partitions.json` records
the exact run/head, final job attempts, observed required-step status and
conclusion, refresh count, and any unresolved proof errors. A green job-level
conclusion alone cannot pass the gate.

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
accepts historical six-, eleven-, fifteen-, seventeen- and twenty-three-job
workflows plus the current twenty-five-job layout; all applicable suites must
succeed on a complete first attempt. All six native jobs must report mode
success, the four Unix native jobs must additionally report differential
success, and the UEFI and analyzer gates must report their key coverage steps.
Their execution intervals and runner seconds are included. Workflow hashes and
runner labels define separate cohorts. Reports include queue delay, elapsed
time, execution span and summed runner seconds, including mobile/lint/aggregate
jobs. Never attribute differences to this PR without matching source/cache state
and multiple completed observations. No speedup is claimed before that evidence.

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

Ordinary `Buster CI` manual dispatches retain the existing input surface and
cache behavior. A deliberate matched cohort selects its mode through the
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
