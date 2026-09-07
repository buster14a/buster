# GitHub Actions CI

The GitHub workflows run beside Forgejo during the migration. Forgejo remains
separate and unchanged. This guide concerns the public source repository's CI,
**not** the source-free private broker in `.forgejo/github-bridge/`; do not add
source archives, caches, credentials, verbose logs or untrusted-PR triggers to
that broker. See [the broker guide](ci-github-hosted-runners.md).

## Coverage and gates

Every job checks both `github.server_url == 'https://github.com'` and
`vars.GH_ACTIONS_CI_ENABLED == 'true'`. Keep those gates: Forgejo also reads
`.github/workflows` but does not have these hosted runner labels.

The main workflow preserves the existing six check names and runner labels:

| Check | Runner | Zig target | Additional suites after the combination matrix |
|---|---|---|---|
| Linux x86-64 | ubuntu-26.04 | x86_64-linux | Execution modes, Android Debug/Release |
| Linux AArch64 | ubuntu-26.04-arm | aarch64-linux | Execution modes |
| macOS x86-64 | macos-26-intel | x86_64-macos | Execution modes, iOS Debug/Release |
| macOS AArch64 | macos-26 | aarch64-macos | Execution modes, iOS Debug/Release |
| Windows x86-64 | windows-2025 | x86_64-windows | None |
| Windows AArch64 | windows-11-arm | aarch64-windows | None |

`build.c` owns `test_all_combinations_ci`, including compiler/configuration
selection, resource-bounded longest-first shards, Debug/Release, unity/split,
supported sanitizer/fuzz configurations, static analysis and the established
self-host fan-out. CI does not replace that matrix with shell loops or smaller
standalone samples. The four Unix jobs also run `test_mode_matrix --config
Release`; both architectures execute ELF/Mach-O natively where supported. The
original Android/iOS commands retain `--all`.

All six names remain complete platform gates. Splitting mobile work into new,
potentially non-required checks would change that contract. Instead, independent
later suites use explicit prerequisite conditions and `!cancelled()`: a failed
combination does not suppress execution modes or mobile tests. Missing tools
skip only their dependants and still fail the job. Matrix `fail-fast` is false.
No `continue-on-error`, disabled tests or sanitizer suppressions make CI green.

Known existing gaps remain visible: Windows does not run the execution-mode
matrix, so PE legs lack native coverage there; no Wine/qemu-user is installed
for those oracle-only legs. macOS's unversioned `gcc` resolves to an Apple Clang
shim, not Homebrew GCC. Self-host fixed points remain limited to the supported
x86-64 Linux/Windows and macOS configurations. This CI audit does not claim to
solve those gaps or static-analyzer scheduling issue #92.

## Events and cancellation

`ci.yml` runs on pull requests, main pushes, tags, merge-group checks and manual
dispatch. Feature-branch pushes use their PR instead of running a duplicate
matrix. Fork PRs use `pull_request`, never `pull_request_target`; repository
approval policy still applies. There are no path filters on the desktop checks,
including documentation-only changes. Permissions are read-only.

A new PR revision cancels its obsolete run. Merge groups are isolated by ref.
Main, tag and manual runs have unique run-ID concurrency groups: intentional
observations are not discarded as active or pending work. Only bounded local
summaries use `always()`; cancelled runs do not start new tests or uploads.
Android's owned-emulator EXIT/signal cleanup preserves test failures and also
fails a previously successful step when cleanup fails.

`ios-monitor-tests.yml` retains its two original fake-tool lanes, Ubuntu 24.04
and macOS 15, plus path-scoped PR/main triggers. Those lifecycle fault-injection
tests complement, rather than duplicate, the real simulator suites.

`issue-33-snapshot.yml` retains its issue-33 branch trigger and adds manual
invocation. It archives tracked files with `git archive`, uses shallow
credential-free checkout and pinned actions, and supersedes obsolete branch
snapshots. Manual snapshots have independent run-ID groups.

## Dependencies and cache safety

`.github/zig.json` is the single version/SHA-256 manifest for all six Zig
archives. `tools/ci_zig.py` downloads the pinned official archive or restores an
exact-key cached copy, **rehashes it before tar or Zig executes**, installs into
a fresh directory, verifies the version and publishes `GITHUB_PATH`. A corrupt
hit fails visibly; it is not silently executed, downloaded over or republished.
Downloads have bounded retries and partial files never become cache entries.

The cache key includes runner OS, runner architecture, target and manifest
hash. There are no restore prefixes. Only the compressed archive is cached,
not extracted executables, build trees, compiler objects, CMake state,
credentials or test verdicts. Saving happens after verified installation and
before compiler tests; GitHub's normal PR/base cache-scope rules still apply.
The installer prints cache/download origin, digest, bytes and elapsed seconds.

CI no longer downloads the mutable `ci-latest` Vulkan SDK or executes its setup
scripts: these build configurations already leave Vulkan/rendering/shader
compilation disabled. No enabled renderer coverage was removed. Optional manual
SDK installers remain untouched. Distribution mold, Homebrew coreutils and the
Android SDK/system image remain explicit installs, not unsafe wholesale caches.

All referenced actions are full-SHA pinned. Checkouts do not persist Git
credentials. Diagnostic artifacts contain captured logs, revision/runner
metadata and `result.json`, live outside build trees erased by `generate`, and
expire after seven days. Names include platform, architecture, run and attempt.
Source archives expire after one day, skip redundant ZIP compression, and use
`issue-33-source-<run>-<attempt>`; no in-repository consumer uses the old name.

## Sanitizers and summaries

The existing sanitizer matrix inherits
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. This matters because the
non-MSVC configuration enables sanitizer recovery: a UBSan diagnostic must not
recover into a passing test process. Leak checks and sanitizer categories are
not disabled. Issue [#235](https://github.com/buster14a/buster/issues/235) reports
an invalid-bool load on the audit's starting revision; a reproduction must fail,
not acquire a suppression. The workflow patch does not claim a compiler fix.

`tests/ci_tools_test.py` verifies cache integrity, failure propagation, summary
contracts and timing cohorts. Its Unix probe demonstrates a recoverable UBSan
error returning zero with recovery and nonzero with the CI environment. The
probe is supplementary to, not a replacement for, native sanitizer coverage.

`tools/ci_summary.py` requires an explicit applicable-suite list. Missing,
skipped, cancelled or failed required work is not success; `outcome`, not a
possibly masked `conclusion`, controls failure. Summaries record an allowlist of
revision/runner metadata, never the complete environment or tokens. Full logs
remain in the diagnostic artifact. Runner labels can move to new images;
`ImageOS`/`ImageVersion` are recorded when available. Label changes also require
updating `.github/actionlint.yaml`.

## Reproduce a hosted failure

Check out the exact `GITHUB_SHA` in the summary and match the recorded tool/image
versions. GitHub's driver bootstrap uses installed Clang, **not TCC**; its outputs
are not trusted reusable TCC-bootstrapped compiler artifacts. The combination
superbuild expects `build/build` (`build/build.exe` on Windows). In contrast,
execution-mode generation deletes `build/`, so that driver must live outside it.

For Linux, choose the Zig target from the table and fresh install paths:

```sh
python3 tools/ci_zig.py --target x86_64-linux --cache-directory /tmp/buster-zig-cache --install-directory /tmp/buster-zig
export PATH="/tmp/buster-zig:$PATH"; export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
mkdir -p build && clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o build/build && ./build/build test_all_combinations_ci --verbose=1
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/buster-build && CFLAGS=-Wno-error=invalid-feature-combination /tmp/buster-build generate --cc clang --config Release --linker DEFAULT && CFLAGS=-Wno-error=invalid-feature-combination /tmp/buster-build test_mode_matrix --config Release
```

On macOS change the target and omit the Linux-only `CFLAGS` exception. The
exception addresses the hosted CPU/Clang AVX10 feature warning without hiding
other warnings. Install mold for the Linux combination command and coreutils
for macOS simulator timeouts. `--linker DEFAULT` belongs to `generate` only.

On Windows use `python` and Windows temporary paths. Follow the workflow's
`vswhere`/`Launch-VsDevShell.ps1` setup for `amd64` or `arm64`, with
`-HostArch amd64`. Prepend `C:\Program Files\LLVM\bin` **after** entering the
shell: VS's x64 Clang can otherwise precede the native ARM64 compiler. Assert
Clang's target before building. The bootstrap is:

```powershell
$env:UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'; New-Item -ItemType Directory -Force build | Out-Null; clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-microsoft-enum-forward-reference -g build.c -lws2_32 -o build/build.exe; if ($LASTEXITCODE -eq 0) { ./build/build.exe test_all_combinations_ci --verbose=1 }
```

Hosted logging redirects native stderr inside `cmd`, then checks
`$LASTEXITCODE`; PowerShell must not turn ordinary diagnostics into terminating
`NativeCommandError` records. Bash pipelines retain `set -euo pipefail`.

For Android reproduce the complete start/owned-EXIT-cleanup wrapper from
`ci.yml`, not an unowned `adb emu kill`; it runs `./android/test_ci.sh --all`.
For iOS use `BUSTER_IOS_ARCH=arm64 ./ios/test_ci.sh --all` or the x86-64 variant.
The fake-tool reproduction is `bash tests/mobile_ci_scripts_test.sh`.

## Comparable timing reports

`tools/github_ci_time.py` uses Python's standard library and optionally a
read-only `GH_TOKEN`/`GITHUB_TOKEN`; tokens are never serialized. Existing
`tools/ci_time.py` retains Forgejo's different timestamp/retention semantics.

```sh
python3 tools/github_ci_time.py collect --branch main --limit 30 --output /tmp/before.json
python3 tools/github_ci_time.py summarize /tmp/before.json --output /tmp/before-summary.json
python3 tools/github_ci_time.py collect --head-sha CANDIDATE_COMMIT --limit 30 --output /tmp/after.json
python3 tools/github_ci_time.py summarize /tmp/after.json --output /tmp/after-summary.json
```

Each exact workflow-blob/runner-label cohort has its own sample count and
median. Only successful first attempts with all six platforms and applicable
original suites count. Failed, cancelled, partial, rerun and unknown-timestamp
observations are exclusions, never imputed or pooled into a speedup.
`elapsed_seconds` measures workflow creation to final job completion, including
queueing. Execution span, initial queue delay, aggregate active runner seconds
and per-step durations are reported separately. `updated_at` is not used as a
completion time. One observation is not a stable performance claim; review
source changes and warm/cold cache states before attributing differences.

## Local checks

```sh
python3 tests/ci_tools_test.py -v
bash tests/mobile_ci_scripts_test.sh
actionlint .github/workflows/ci.yml .github/workflows/ios-monitor-tests.yml .github/workflows/issue-33-snapshot.yml
git diff --check
```

These helper tests are not replacements for Buster's C tests. Inspect the
submitted revision's real hosted Linux, Windows and macOS checks before calling
it merge-ready. A local YAML or shell syntax check cannot certify those runners.
