# GitHub workflow audit — 2026-09-07

Base: `66bf321936ccb2b4f0df42cb6f65ed197f8652b5`. The three workflow blobs
are unchanged from the measured revision `9834a4253c61934a8a253641a84741a25d3717e3`.
This change concerns the public repository's GitHub workflows, **not** the
source-free broker under `.forgejo/github-bridge/`. Broker cache/artifact and
credential restrictions remain untouched.

## Coverage contract and scheduling

The six existing desktop names and runner labels remain. Every desktop lane
invokes the entire `test_all_combinations_ci` command; build.c still owns the
compiler/configuration cross product, CPU budget, sanitizer/fuzz policy,
static-analysis lane, table audits, and supported self-host fixed points.
There is no additional sanitizer-only build duplicating those configurations.
The four Unix lanes also invoke the complete `test_mode_matrix --config Release`
with the same explicit Clang/Release/default-linker generation. A failed
combination no longer suppresses that independent check. Neither a step nor a
job uses `continue-on-error`, and both matrix strategies disable fail-fast.

Mobile work is sharded by suite, not by arbitrary test-name prefixes:

| Shard | Existing coverage retained |
| --- | --- |
| Android x86-64 / ubuntu-26.04 | Fixed API-35 x86-64 image; `android/test_ci.sh --all` (Debug and Release) |
| iOS x86-64 / macos-26-intel | `ios/test_ci.sh --all`: existing Debug/Release compile, link and bundle validation |
| iOS AArch64 / macos-26 | `ios/test_ci.sh --all`: Debug/Release simulator execution |

The Intel iOS compile-only exception already exists in `ios/test_ci.sh`; this
PR neither introduces nor expands it. Both mobile entry points configure their
own build trees, use the default linker, and disable developer targets. They
do not consume the desktop build driver or desktop compiler outputs. Thus
separating them removes a dependency without duplicating desktop test work.
This is **suite-level sharding**, not a new per-case shard protocol.

**Require `CI complete` for merging.** It waits for workflow lint, all six
desktop lanes, and all three mobile lanes. A skipped, cancelled, or failed
prerequisite fails the gate. Requiring only the old six desktop names no longer
covers mobile results. This PR does not change repository branch protection.
The independent, path-filtered mobile lifecycle workflow retains its Linux and
macOS fake-tool tests; its filter now includes Android/iOS-specific test files
and the main workflow, not just files containing `mobile` in their names.

Desktop and mobile jobs retain the existing 300-minute outer timeout. Existing
per-command self-host/emulator/simulator deadlines are unchanged. Reducing the
outer deadline without a broader runtime distribution would be speculative.

## Cache and artifact trust boundaries

Only Zig's **upstream compressed archive** is cached. The exact key includes a
schema version, runner OS/architecture, Zig target, and the hash of `.github/zig.json` (version and all SHA-256 pins). There
are no fallback restore keys and no cached build trees or compiler outputs.
Every restore, including an exact hit, is SHA-256 checked before extraction and
PATH publication. Only successful default-branch push setup writes a cache,
and it writes immediately after verification, before repository tests run.
Other branches and manual runs are read-only cache consumers. A corrupt
archive fails setup rather than executing unverified bytes.

Mutable Android SDK packages, AVD state, and Homebrew prefixes are deliberately not cached. Their present installation commands
do not provide immutable per-package revisions/checksums suitable for portable
cache keys. Caching those directories by a coarse OS key would create stale or
cross-toolchain state. Unused Vulkan SDK setup is removed because these jobs
leave renderer/shader options disabled; mobile SDK coverage remains.

Actions are pinned to full commit IDs. Checkout credentials are not persisted.
Workflow tokens request `contents: read` only; no job consumes repository
secrets or uses `pull_request_target`. No broad environment dump is uploaded.
Desktop, mobile, lifecycle, and lint transcripts are retained for seven days,
with unique lane/run/attempt names. They live outside `build/`, because `generate`
deletes that tree. There is no compiler artifact promoted for downstream use.

The issue-33 recovery workflow remains scoped to its original branch and keeps
its one-day source archive. It now has pinned actions, shallow checkout,
non-persisted credentials, a GitHub-only guard, and cancellation of obsolete
snapshots. `git archive` includes tracked source only. Its gzip file is uploaded without redundant ZIP compression.

## Cancellation and duplicate work

PR revisions and merge groups supersede their own obsolete runs. Main and tag
pushes and explicit manual runs have unique run-ID groups so pending-run
replacement cannot discard measurements. PR triggers include forks with read-only
permissions; feature pushes do not create duplicate matrices. Mobile lifecycle
PR updates cancel only their own stale runs; main lifecycle results are retained.

Android INT/TERM handlers retain nonzero cancellation status and use the
existing bounded lifecycle stop command through the EXIT cleanup handler.
Cleanup failure after otherwise successful tests still fails the shard.
Bounded summaries use `always()`; uploads use `!cancelled()` and do not start
after cancellation. Required-suite summaries fail closed through `ci_summary.py`.

## Reproduction

Match the platform/architecture, Zig version/checksum, image toolchains and SDK
settings in `.github/workflows/ci.yml`. Run from the repository root, with no
other process using the selected build directory. Local entry points retain
the trusted TCC bootstrap:

- Compiler/configuration matrix: `./build.sh test_all_combinations_ci --verbose=1`; Windows: `./build.ps1 test_all_combinations_ci --verbose=1`.
- Unix modes: `./build.sh generate --cc clang --config Release --linker DEFAULT && ./build.sh test_mode_matrix --config Release`.
- iOS: `BUSTER_IOS_ARCH=arm64 ./ios/test_ci.sh --all` (use `x86_64` on Intel).
- Android: start with `bash ./android/start_emulator_ci.sh start`, run `./android/test_ci.sh --all`, and always call `bash ./android/start_emulator_ci.sh stop`, preserving the first failure.
- Lifecycle: `bash tests/mobile_ci_scripts_test.sh` with coreutils and modern Bash available on macOS.
- Workflows: `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.7 .github/workflows/*.yml`.

To reproduce the hosted bootstrap on Unix, run `mkdir -p build && clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o build/build && ./build/build test_all_combinations_ci --verbose=1`.
For the hosted mode gate, keep the second Clang-built driver outside `build/`
and retain its `CFLAGS=-Wno-invalid-feature-combination`, as spelled out in the
workflow. Windows reproduction must enter the architecture-specific VS shell
and then put standalone LLVM first; the workflow contains the complete command.
Each job summary records the commit, suite outcome, and relevant entry points.

## Runtime evidence and limits

Pre-change successful run: [34162112885](https://github.com/buster14a/buster/actions/runs/34162112885),
revision `9834a4253c61934a8a253641a84741a25d3717e3`, attempt 1.
REST reports `run_started_at=2026-09-07T21:09:51Z` and
`updated_at=2026-09-07T21:30:53Z`: **1,262 seconds (21:02)** of workflow elapsed
time. This is a one-observation sample, not a statistically persuasive
before/after median. `updated_at` is the final workflow metadata timestamp,
not CPU time; prefer the last job's `completed_at` for subsequent samples.

For the same run, the macOS AArch64 job started at 21:10:23 and completed at
21:27:56. Its combination step took 574 seconds, mode step 90 seconds, and iOS
step 351 seconds. Those sequential timings motivate removing the mobile
ordering dependency. Their sum is **not** a measured post-change speedup.

For a comparison, collect at least three complete successful runs per side at
fixed revisions, retain failed/cancelled runs separately, and use the same
runner labels and coverage. Record per-run elapsed time (run start to last
job completion), summed job seconds, queue delay, step durations, run attempt,
and cache-hit state. Report cold and warm cache cohorts separately. Compute
medians within those cohorts; do not pool older workflows with different
matrices, compare only the fastest runs, or infer total-cost savings from a
shorter critical path. The extra mobile runners can reduce wall time while
increasing total hosted-runner seconds.

No post-change timing or green-CI claim is recorded here before an actual
completed run. The PR discussion must report the submitted commit's checks and
measurement limits explicitly.

## Remaining scope

A macOS `gcc` command still resolves to Apple's Clang shim; selecting real
versioned Homebrew GCC would expand coverage and needs its own validated fix.
Windows execution-mode coverage and cross-host PE emulation remain as defined
by build.c/current CI. This change does not suppress those gaps, add expected
failures, alter sanitizer flags, or claim newly executed coverage.

## Rebase and lint repair (2026-09-08)

The original CI run [34164933026](https://github.com/buster14a/buster/actions/runs/34164933026)
failed workflow lint on eleven SC2016 diagnostics: ShellCheck interpreted the
literal Markdown backticks in five summary commands as unexpanded shell
expressions. Those commands intentionally quote Markdown rather than execute
it. Command-scoped SC2016 annotations now document that intent; all other
ShellCheck diagnostics and the workflow gate remain enabled.

Local verification reproduced all eleven diagnostics on original revision
`4a9df654b9396a4d93cce1a1defb2da45a8ee508`, then passed actionlint 1.7.7 with
ShellCheck 0.10.0 on the repaired workflows. All 21 embedded Bash scripts pass
`bash -n`, the five edited summaries produce byte-identical output, and the
aggregate gate accepts only complete success across all 64 combinations of
success/failure/cancelled/skipped. `bash tests/mobile_ci_scripts_test.sh` and
`git diff --check` also pass. Six desktop lanes and three mobile lanes remain.

These checks validate workflow behavior, not the hosted compiler/mobile
matrix. Classic branch-protection settings were inaccessible to the GitHub
integration (403); the documented requirement for `CI complete` still needs
to be enforced by the repository's merge policy.
