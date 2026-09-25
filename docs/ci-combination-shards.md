# Desktop combination shards (#333)

This is the internal compiler/configuration partition, after the independent
native/mobile suite split. `build.c` remains the scheduling and coverage
policy authority. Compiler implementation, test selection, optimization,
sanitizer/fuzzer policy, deadlines and production backend dispatch do not change.

## Ownership

Each of the six existing runner labels has two jobs, named `<platform> release`
and `<platform> checks`. The workflow's `lane` × `shard` axes form twelve jobs;
the include entries add metadata by lane rather than creating extra jobs.

| Shard | Work |
| --- | --- |
| `release` | The one unsanitized Clang Release/unity tree, its runtime tests, canonical unity analysis/table audits, and supported artifact-fanout self-host/fixed-point/census consumers. Shared native diagnostics, throughput/service self-tests, workflow-tool and wrapper regressions also run here once per platform. |
| `checks` | Sanitized Clang Debug/Release and the existing non-Clang portability Debug trees. Windows ARM64 still has its nonempty MSVC Debug shard and explicit unsupported GCC/Zig/sanitizer/fuzzer exclusions. |

| Platform | Release configurations | Checks configurations | Total required |
| --- | ---: | ---: | ---: |
| Linux, either architecture | 1 | 4 | 5 |
| macOS, either architecture | 1 | 4 | 5 |
| Windows x86-64 | 1 | 5 | 6 |
| Windows AArch64 | 1 | 1 | 2 |

Selection is semantic: Clang + unsanitized + optimized belongs to `release`;
the other rows belong to `checks`. It is not an ordinal/modulo partition or
a test-name prefix. Excluded rows have explicit owners too, but never count
as execution. The native partition validator requires exactly one canonical
Release configuration, a nonempty checks selection, and intact shared trees.
In particular, the macOS sanitized Debug/Release configurations stay in the
same CMake multi-config tree. Both shards together schedule each original
required configuration exactly once.

The canonical producer and all its consumers remain in one job: there is no
cross-job compiler artifact handoff or second canonical compiler build.
All six desktop platforms have independent native execution-mode jobs; the four
Unix native jobs additionally share their producer with the differential corpus.
Mobile, UEFI and the independent analyzer jobs are unchanged. Intel macOS
retains its existing direct-matrix/self-host exception. The exact Windows mode
and compiler/configuration contract is documented in
[Windows CI coverage](windows-ci-coverage.md).

The wrapper step keeps its checkout/cancellation lifecycle guard on both
shards. Its checks-shard body reports `owned-by-release-shard` and exits
without running the suite or writing a wrapper-test verdict; only Release
requires that verdict. Verified-Zig setup creates its own log directory and
does not depend on a Release-only preflight side effect.

Shards necessarily duplicate runner allocation, checkout, the small hosted
build-driver bootstrap, tool discovery and verified-Zig setup. They keep full
compiler capability detection/re-probing against the original policy in both
jobs. Only verified download archives are cached; build trees, compiler outputs
and test verdicts are not. The extra setup and queue cost must be included in
qualification, not assumed free.

## Coverage and completion proof

`BUSTER_MATRIX_SHARD=release|checks` selects a shard. An absent value or `all`
runs the original unsharded matrix (`combinations` in the manifest). Invalid
values fail before compiler discovery, preflight or build-tree mutation.
Default shard tree prefixes are `build/build-release-` and
`build/build-checks-`; an explicit build-directory prefix remains supported.

Every manifest retains the **entire** expected/detected policy. Row IDs stay
in the original `desktop/combinations/...` namespace, so all six independently
reviewed policy counts and fingerprints from #612 remain unchanged. A new
`owner_shard` is independently checked by the consumer; it cannot redefine
ownership. A completion record must contain exactly the selected required
IDs, with no missing, duplicate or foreign-shard rows. Source, driver,
compiler executable, target/version and workflow-run bindings are still
validated on the executing runner. A checks completion explicitly says
`owned-by-release-shard` for canonical obligations; it cannot claim them.

`CI complete` has two independent requirements:

* All six job groups (`lint`, `test`, `native`, `mobile`, `uefi`, `analyzer`)
  must succeed, with the existing real-shell negative controls retained.
* The read-only Actions job inventory must contain the exact 25 expected job
  identities: all twelve desktop shard names, all six native names, three
  mobile names, UEFI, analyzer, lint and the active aggregate. Every completed
  job must succeed. Each desktop job must complete its applicable matrix,
  coverage summary, tool setup, shared regressions and log upload; each native
  job must complete its applicable mode result, and Unix native jobs must also
  complete the differential result.

For a platform's unchanged full required set E, the native producer and
independent consumer agree on disjoint selections R and C with R ∪ C = E.
Successful, correctly named R and C jobs therefore prove the full set, without
moving foreign-platform binaries to an aggregate runner for a fictitious
re-probe. Missing matrix entries cannot turn a smaller surviving group green.
The job inventory is retained as `desktop-partitions-<run>-<attempt>`.

The inventory reader paginates **all attempts of the same immutable run** and
selects each job's highest attempt, never its most recent *successful* attempt.
This supports re-running failed jobs while retaining earlier successful jobs
of that exact run/source. A newer failure cannot be replaced by an older pass;
duplicate same-attempt, foreign-run/source and future-attempt records fail.
`CI complete` itself must be active in the current attempt. The run head SHA
and actual checkout SHA are recorded separately (PR merge checkouts differ).
No branch protection, check requirement, write permission or secret is changed;
only `CI complete` adds job-scoped `actions: read` for this inventory.

## Reproduce

Use the exact checkout SHA, runner image and tool versions recorded in the
job. Follow the existing verified Zig, GCC, CMake/Ninja and platform SDK setup
in `.github/workflows/ci.yml`; Linux also retains mold. Separate checkouts are
recommended for parallel local runs. Sequential reproduction on Unix:

```sh
mkdir -p build
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o build/build
BUSTER_MATRIX_SHARD=release BUSTER_CI_COVERAGE_OUTPUT="$PWD/build/coverage-release.json" ./build/build test_all_combinations_ci --verbose=1
BUSTER_MATRIX_SHARD=checks BUSTER_CI_COVERAGE_OUTPUT="$PWD/build/coverage-checks.json" ./build/build test_all_combinations_ci --verbose=1
```

For the unsharded comparison use `BUSTER_MATRIX_SHARD=all` with a separate
coverage output. The normal TCC-bootstrapped `./build.sh` and `./build.ps1`
accept the same environment selector; the commands above reproduce the hosted
Clang bootstrap, not a trusted-TCC bootstrap measurement.

On Windows, first select the matching Visual Studio developer shell and LLVM
target exactly as the existing workflow does, then:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -Wno-microsoft-enum-forward-reference -g build.c -o build/build.exe -lws2_32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($shard in @('release', 'checks')) {
    $env:BUSTER_MATRIX_SHARD = $shard
    $env:BUSTER_CI_COVERAGE_OUTPUT = Join-Path (Get-Location) "build/coverage-$shard.json"
    .\build\build.exe test_all_combinations_ci --verbose=1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Each hosted shard keeps its own `desktop-<os>-<arch>-<shard>-<run>-<attempt>`
artifact, `coverage.json`, `result.json`, summary and combination log. The
`BUSTER_MATRIX_PARTITION` log records selected/full configuration and tree
counts plus shared-preflight ownership. The summary also records the verified
Zig cache-hit output, runner label/image metadata and selected shard.

Focused controls, without compiling the full compiler matrix:

```sh
python3 tools/matrix_shard_test.py -v
python3 tools/coverage_manifest_test.py -v
python3 tests/ci_tools_test.py -v
python3 tools/check_action_pins.py
```

Native tests export all six policies and all three selections from a temporary
instrumented copy of the real driver. Their fixture mode is deliberately
unacceptable as CI evidence. They verify unchanged anchors, exact/disjoint
ownership, the Windows x86-64 `28/6/22` and AArch64 `19/2/17` policy counts,
nonempty AArch64 Clang/MSVC coverage, explicit exclusion reasons, shared-tree
validation, invalid-selector non-mutation, consumer rejection of
shrinkage/foreign rows, missing jobs, native-step evidence, pagination and
rerun rules. The existing coverage tests retain real executable/hash/re-probe
controls.

## Performance qualification — required before closing #333

Implementation and synthetic tests do **not** establish a speedup. Require at
least three complete, successful first-attempt baseline runs and three complete,
successful candidate runs with equivalent compiler/fixture inputs, runner
labels, tool versions and controlled/documented archive-cache state. Record the
actual checkout/source identities; a PR head is not its merge checkout. Do not
substitute cancelled, partial, failed, retried or historical different-source
runs. Keep baseline/candidate workflow-blob cohorts separate.

```sh
python3 tools/github_ci_time.py collect --head-sha BASELINE_HEAD_SHA --limit 30 --output baseline-runs.json
python3 tools/github_ci_time.py collect --head-sha CANDIDATE_HEAD_SHA --limit 30 --output candidate-runs.json
python3 tools/github_ci_time.py summarize baseline-runs.json --output baseline-summary.json
python3 tools/github_ci_time.py summarize candidate-runs.json --output candidate-summary.json
```

The collector understands the historical 6/11/15/17/23-job layouts and the
current 25-job layout. All 25 execution intervals count toward candidate runner
seconds, including both Windows mode lanes and the aggregate inventory check.
It reports whole-workflow elapsed time, execution span, initial queue delay,
individual job durations and job queue delays when API creation timestamps
exist (otherwise `null`, never imputed zero). Missing successful steps or shard
identities reject a sample. Historical 23-job and current 25-job workflow blobs
remain separate cohorts.

Compare medians and retain individual shard distributions. Inspect
`result.json` cache/image fields and `BUSTER_MATRIX_PARTITION`/step logs for
repeated setup, diagnostics and any duplicate compiler build. Report aggregate
runner-seconds change alongside latency. A faster desktop job is insufficient
if native, mobile, analyzer or UEFI work remains the whole-workflow critical
path. **No measured speedup or completion of #333 is asserted by this document.**

## Per-tree critical paths

The existing desktop artifact also retains [versioned native phase records](ci-matrix-phases.md).
Their consumer joins every tree to this authoritative coverage manifest and
fails the desktop result on missing or inconsistent evidence. Scheduling and
row ownership remain unchanged; alternative-order predictions are diagnostic.
