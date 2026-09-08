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

## Two gates

Every CI job carries:

```yaml
if: ${{ github.server_url == 'https://github.com' && vars.GH_ACTIONS_CI_ENABLED == 'true' }}
```

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

The `test` matrix retains the six desktop runners and names above. The `mobile`
matrix runs three independent suite-level shards, so desktop build/test work is
not repeated to obtain earlier mobile results. `lint` validates every GitHub
workflow. **Require the aggregate `CI complete` check**, which fails unless
lint, all desktop lanes and all mobile lanes succeed; the six desktop names
alone no longer include mobile results.

| Work | Runners | Command |
|---|---|---|
| Combination matrix | all six desktop lanes | `test_all_combinations_ci` |
| Execution-mode matrix | the four Unix desktop lanes | `test_mode_matrix --config Release` |
| Android shard | `ubuntu-26.04` | `android/start_emulator_ci.sh start`, then `android/test_ci.sh --all` |
| iOS shards | `macos-26-intel`, `macos-26` | `ios/test_ci.sh --all` |

The native build driver still owns the complete compiler/configuration matrix,
including sanitized Debug/Release, fuzz policy, static analysis and supported
self-hosting. No configuration or test is removed. Both mobile entry points
are standalone and retain Debug and Release. The existing Intel iOS gate is
compile/link/bundle-only; Apple Silicon retains simulator execution.

The main workflow covers pull requests (including forks), main pushes, tags,
merge groups and manual runs. Feature pushes use their PR run without a duplicate matrix. `fail-fast` is off in both
matrices. Unix execution-mode tests still run after a combination failure,
without allowing that earlier failure to pass. Mobile shards have no desktop
prerequisite and their results cannot be hidden by a failed desktop build.

PR revisions and merge groups coalesce per PR/ref. Main/tag pushes and manual
runs have unique run-ID groups so neither active nor pending results are
superseded. Lifecycle PR updates separately cancel obsolete fake-tool runs.
The seven-day log artifacts, summaries, cache boundaries, reproduction commands,
and timing methodology are documented in [the workflow audit](ci-workflow-audit.md).

Both architectures of both Unix platforms run the execution-mode matrix because
that is what makes its legs *execute* rather than fall back to the disassembly
oracle: x86-64 ELF and Mach-O on the Intel runners, AArch64 ELF and Mach-O on
the Arm ones. Windows is excluded from it as it is on Forgejo.

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

Two rows are weaker than they look. On macOS `/usr/bin/gcc` is an Apple Clang
shim, so the GCC row is a second Clang row; the images do carry real Homebrew
GCC, but only under versioned names (`gcc-15`), which is not what `build.c`
resolves. And the self-host fan-out runs only where the fixed point exists —
the x86-64 Linux and Windows runners and both macOS runners — so the two
AArch64 desktop rows build and test without it.

## What does not run here

- **Wine and `qemu-user`.** The images carry neither, so an execution-mode
  matrix leg whose target no runner can execute is oracle-checked instead —
  the row still reports, with its avenue downgraded. Between the four Unix
  runners every ELF and Mach-O leg executes natively; the PE legs are the ones
  that stay on the oracle, because Forgejo covers them under wine and this
  workflow does not run the execution-mode matrix on Windows.
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
go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.7 .github/workflows/*.yml
```

Without `.github/actionlint.yaml` every `runs-on` above is reported as an
unknown label, so keep the two in step when a runner changes.

## Helper validation and timing

`python3 tests/ci_tools_test.py -v` exercises the archive installer, fail-closed
summaries and timing collector on each desktop platform (`python` on Windows).
Unix runners also compile a tiny Clang probe to verify that recoverable UBSan
diagnostics become fatal with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
The same environment applies to the compiler matrix; no sanitizer is suppressed.

`tools/ci_zig.py` owns download, checksum verification, staging, version checking,
and PATH publication. The exact archive cache key includes the manifest hash;
only main pushes save verified archives, before compiler tests run. SDK setup
for unused Vulkan rendering/shader support is removed; those options are off
in these configurations. Android and iOS SDK setup and test commands remain.

Desktop and mobile summaries use `tools/ci_summary.py`, explicitly requiring each
applicable suite. Missing, skipped, cancelled or failed work fails the summary.
Diagnostic uploads do not start after cancellation. The aggregate `CI complete`
continues to require all six desktop jobs, three mobile jobs and workflow lint.

Collect timing using `python3 tools/github_ci_time.py collect --branch main --limit 30 --output /tmp/before.json`
and summarize using `python3 tools/github_ci_time.py summarize /tmp/before.json`.
For a candidate, replace `--branch main` with `--head-sha COMMIT`. The collector
accepts both historical six-job workflows and the current eleven-job workflow;
all applicable suites must succeed on a complete first attempt. Workflow hashes
and runner labels define separate cohorts. Reports include queue delay, elapsed
time, execution span and summed runner seconds, including mobile/lint/aggregate
jobs. Never attribute differences to this PR without matching source/cache state
and multiple completed observations. No speedup is claimed before that evidence.
