# Windows CI coverage contract

[Agent instructions](../AGENTS.md) · [GitHub Actions guide](ci-github-actions.md) · [Combination shards](ci-combination-shards.md)

This document records the machine-checkable Windows coverage contract for
issues [#332](https://github.com/buster14a/buster/issues/332) and
[#334](https://github.com/buster14a/buster/issues/334). `build.c` remains the
single owner of compiler/configuration and execution-mode policy. The workflow
selects runners and preserves results; it does not duplicate the matrix in YAML.

## Compiler/configuration inventory

The unsharded policy manifest is generated before compiler discovery. Every row
therefore has a stable semantic identity and is either `required` or `excluded`.
A missing required compiler fails the lane; discovery cannot silently shrink the
expected matrix. `tools/ci_summary_core.py` freezes the counts and policy
fingerprints, while `tools/matrix_shard_test.py` exports the real `build.c`
policy for all six platforms and rejects missing, duplicated, foreign, re-owned
or shortened rows.

| Windows lane | Total rows | Required | Explicitly excluded | Policy fingerprint | Release owner | Checks owner |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| x86-64 | 28 | 6 | 22 | `46ffb69c2ceae9c0` | 1 | 5 |
| AArch64 | 19 | 2 | 17 | `709a010f922e385b` | 1 | 1 |

Windows AArch64's two required rows are the canonical unsanitized Clang Release
configuration and the MSVC Debug portability configuration. Its remaining rows
are not absent from the contract: each is retained with an explicit exclusion
reason. These include unsupported GCC and Zig compiler rows and unsupported
sanitizer/fuzzer variants. Windows x86-64 retains its broader six-row required
set. The Release and checks selections are disjoint, nonempty, and together
cover every required row.

The retained artifact `desktop-windows-<arch>-<shard>-<run>-<attempt>` contains
`coverage.json`, its human-readable summary, compiler probes, and the ordinary
combination logs. `CI complete` independently reloads the exact run inventory;
a missing or malformed manifest, changed count/fingerprint, absent required row,
missing compiler, failed shard, duplicate job, skipped job, or unexpected job
fails closed.

## Execution-mode ownership

Execution modes are a separate gate from `test_all_combinations_ci`:

| Native job | Hosted runner | Required work |
| --- | --- | --- |
| `Windows x86-64 native` | `windows-2025` | `test_mode_matrix --config Release` |
| `Windows AArch64 native` | `windows-11-arm` | `test_mode_matrix --config Release` |

Both jobs select the corresponding Visual Studio target architecture, prepend
the hosted standalone LLVM, verify Clang's default target, compile the native
`build.c` driver, generate a fresh Clang Release tree, and run the mode matrix.
`build.c` owns the allocator/mode set, native execution decisions, external
oracle comparisons, and explicit expected failures. A mode failure is therefore
reported as its own native job failure rather than being hidden by, or ordered
after, the compiler/configuration matrix.

The four Unix native jobs continue to run both the mode matrix and the native
differential corpus. Windows does not claim that differential corpus: its native
jobs require only the independently accounted mode result. All six native jobs
publish the same fail-closed summary and packed evidence shape.

`tools/github_ci_time.py` defines the current exact aggregate as 25 jobs:
12 desktop combination shards, 6 native jobs, 3 mobile jobs, UEFI, analyzer,
workflow lint, and `CI complete`. The live gate requires the Windows mode step
and native summary exactly once in each Windows native job. The timing reader
also recognizes the historical 23-job layout so older measurements remain
readable, but that legacy layout cannot satisfy the current live gate.

## Reproduction

Run from a fresh checkout in a Visual Studio developer shell for the target
architecture, with standalone LLVM first on `PATH`:

```powershell
$ErrorActionPreference = 'Stop'
$env:PATH = "$env:ProgramFiles\LLVM\bin;$env:PATH"
$env:CFLAGS = '-Wno-invalid-feature-combination'
$Driver = Join-Path $env:TEMP 'buster-build.exe'
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable `
  -Wno-microsoft-enum-forward-reference -g build.c -lws2_32 -o $Driver
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Driver generate --cc clang --config Release --linker DEFAULT -- `
  -DBUSTER_DEBUG_INFO=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Driver test_mode_matrix --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

For the hosted jobs, `Launch-VsDevShell.ps1` uses `-Arch amd64` on x86-64 and
`-Arch arm64 -HostArch amd64` on AArch64. Before accepting a result, verify that
`clang --version` reports the matching `x86_64-*` or `aarch64-*` default target.
The ordinary compiler/configuration reproduction remains in
[Combination shards](ci-combination-shards.md).

## Cost and acceptance

This contract adds two independently schedulable hosted jobs. It intentionally
does not append the mode matrix to either Windows combination shard, so it adds
runner-seconds without adding a serial dependency to those already long jobs.
No latency improvement is inferred from that scheduling model. Use a completed,
first-attempt workflow on the submitted revision to report actual queue delay,
mode duration, native job duration, critical path, and total runner-seconds:

```sh
python3 tools/github_ci_time.py collect \
  --repository buster14a/buster \
  --head-sha <submitted-commit> \
  --limit 10 \
  --output windows-ci-runs.json
python3 tools/github_ci_time.py summarize windows-ci-runs.json
```

Do not pool the new 25-job workflow with historical 23-job cohorts. Workflow
blob and runner-label identities deliberately separate them. Green acceptance
requires both Windows native jobs, every compiler/configuration shard, the exact
25-job inventory, and the aggregate `CI complete` result on the same immutable
source revision.
