# GitHub Actions CI

`.github/workflows/ci.yml` builds and tests Buster on GitHub's **standard**
hosted runners — `ubuntu-24.04`, `macos-15`, and `windows-2022`. It exists for
the migration to GitHub and runs beside the Forgejo matrix rather than
replacing it while Forgejo is still the source of truth.

## Two gates

Every job carries:

```yaml
if: ${{ github.server_url == 'https://github.com' && vars.GH_ACTIONS_CI_ENABLED == 'true' }}
```

The first half exists because **Forgejo also reads `.github/workflows`**.
Without it, Forgejo would schedule these jobs against `runs-on: ubuntu-24.04`,
a label no Forgejo runner carries, and they would queue until the workflow
timed out. On Forgejo the expression is false — or empty, which is also false —
so the jobs skip and no status context is created.

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

| Job | Runners | Command |
|---|---|---|
| `test` | ubuntu-24.04, macos-15, windows-2022 | Release build and the full `test_all` suite |
| `mode_matrix` | ubuntu-24.04, macos-15 | `test_mode_matrix --config Release` |

The two jobs run in parallel rather than one after the other, so wall time is
the slower of them instead of their sum. `concurrency` cancels an in-flight run
when a newer commit lands on the same ref, which is what keeps latency flat
when several pushes arrive together.

Both bootstrap `build.c` with the Clang already installed on the image: the
hosted images ship no TCC, and modern macOS cannot run it. The bootstrap driver
is written to `RUNNER_TEMP` rather than `build/`, because `generate` wipes
`build/` — and Windows cannot delete a running executable. `generate` pins
`--linker DEFAULT` on Unix because the images carry no mold, and the Windows
toolchain runs through `cmd` so its progress on stderr cannot become a
terminating PowerShell error record.

## What does not run here

- **The multi-compiler combination matrix** (`test_all_combinations_ci`). It
  needs TCC, GCC and Zig present together; the hosted images have none of TCC
  or Zig. It stays on the native runners.
- **Android and iOS.** Both depend on emulator and simulator behaviour that
  this workflow does not attempt to reproduce.
- **The performance series.** Hosted virtual machines expose no performance
  counters, and their wall times are too noisy to trend. `STEP_INSTRUCTIONS`
  needs hardware under your control.

A self-hosted runner attached to a **public** repository executes code from
fork pull requests on your own machine. If the performance series moves to a
self-hosted runner here, restrict its jobs to `push` on the default branch,
require approval for fork workflow runs, and keep the machine off any private
network it does not need.

## Local verification

```sh
actionlint .github/workflows/ci.yml
```
