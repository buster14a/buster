# Desktop Zig configure evidence

[Build guidance](agents/build.md) · [Desktop combination shards](ci-combination-shards.md) ·
[CI reproduction](ci-github-actions.md) ·
[Investigation #707](https://github.com/buster14a/buster/issues/707)

This is diagnostic infrastructure, not a configure optimization or a speedup
claim. The native `test_all_combinations_ci` command still owns compiler
selection, graph generation, probes, scheduling, coverage, and failure policy.
No compiler-works result is preseeded and no Zig compilation cache is restored.

## Retained evidence

Each `release` or `checks` desktop shard copies a fixed allowlist from the
native driver's `build/build-<release|checks>-ci_on-cc_*` trees after its
combination step into that shard's existing
`desktop-<os>-<arch>-<release|checks>-<run>-<attempt>` artifact, under
`configure/`. The unsharded local `BUSTER_MATRIX_SHARD=all` path retains the
legacy `build/build-ci_on-cc_*` names; the collector recognizes those names too
without admitting either shard's separate superbuild directory.

- `CMakeCache.txt` and `CMakeFiles/CMakeConfigureLog.yaml`;
- legacy `CMakeFiles/CMakeOutput.log` and `CMakeFiles/CMakeError.log`, when present;
- `cmake-profile.json`, when native profiling was enabled.

Zig belongs to the `checks` partition, so the primary #707 evidence is in the
Windows x86-64 checks and macOS x86-64 checks artifacts. Keep both shards when
reconstructing a complete desktop lane: each artifact contains only the trees
owned by that partition.

`configure/manifest.json` records every allowlisted path as captured, missing,
or error, with byte length and SHA-256 for captured bytes. It includes only an
explicit allowlist of run/runner identifiers, not a copy of the environment.
The source, driver, compiler, target, and coverage identities remain in the
adjacent authoritative `coverage.json`; exact commands and native timings remain
in `combinations.log`. The manifest is diagnostic-only: it neither certifies
successful compiler probes nor replaces the coverage/result consumers.

The collector performs no build or toolchain subprocesses and does not parse or
sum profile events. It examines at most 4,096 build-root entries and 32 matching
trees, retains at most 32 MiB per file and 128 MiB total, refuses links and
Windows reparse points below the selected root, and requires a fresh output
directory outside the build tree. Collection and size errors are retained in
the manifest and fail the collection step. Missing optional diagnostics are
explicit, including a profile absent from a failed configure. Raw files are not
truncated or normalized. Run collection only after the shard's matrix has
stopped; it is not an atomic snapshot of changing trees.

Raw CMake diagnostics may contain absolute toolchain/SDK paths and command-line
options. Do not pass credentials through CMake options or place secrets in these
files. The artifact is never restored as a build or test cache.

## Diagnostic run

The normal PR, push, tag, and merge-group paths keep profiling disabled. A
manual **Buster CI** dispatch also remains uninstrumented unless the repository
variable `BUSTER_CMAKE_PROFILE` is explicitly set to `true`. For one diagnostic
cohort, set that variable, dispatch the workflow, and clear it after the runs
have been created. The workflow maps it to the existing native
`BUSTER_CMAKE_PROFILE=1` switch only for `workflow_dispatch` events.

This control changes instrumentation only. It does not merge the `release` and
`checks` partitions, remove a row, or create a configure-only workflow. The
native driver writes one `cmake-profile.json` per configured tree and emits its
existing profile summary.

For an already prepared hosted-equivalent checkout, reproduce a shard directly:

```sh
BUSTER_MATRIX_SHARD=checks BUSTER_CMAKE_PROFILE=1 \
    ./build/build test_all_combinations_ci --verbose=1
```

On Windows, use the same Visual Studio/LLVM environment and verified Zig setup
as `.github/workflows/ci.yml`:

```powershell
$env:BUSTER_MATRIX_SHARD = 'checks'
$env:BUSTER_CMAKE_PROFILE = '1'
.\build\build.exe test_all_combinations_ci --verbose=1
```

These snippets do not install tools or replace the workflow bootstrap. Do not
move the hosted driver from `build/build[.exe]`: coverage pins that location.
After completion, collect into a fresh directory outside `build/`:

```sh
python3 tools/ci_configure_evidence.py \
    --build-root build --output /path/outside/build/configure \
    --profile-requested 1
```

Use `python` on the hosted Windows lane. An existing Zig profile can be
summarized with the native driver:

```sh
./build/build cmake_profile_summary \
    build/build-checks-ci_on-cc_zig-sanitize_off-fuzz_available_off-configs_Debug/cmake-profile.json \
    --limit 30
```

CMake's profile describes time inside CMake commands, including time waiting for
children. It is not process CPU, RSS, or filesystem attribution. Nested commands
must not be summed as independent wall time. Use the raw configure log and exact
subprocess commands to decide whether OS-level tracing of a compiler, ABI, or
runtime-cache operation is needed. Do not transfer Windows attribution to Intel
macOS without separate evidence.

## Performance experiment remains separate

Use profiling only to locate work. For timed repetitions set
`BUSTER_CMAKE_PROFILE=0`. Independently prepare and document cold and warm **Zig
compilation/runtime-cache** cases; a hit on the verified download-archive cache
does not establish either state. Do not reuse Buster objects, compiler artifacts,
or passing test verdicts. Keep cache preparation outside measured configuration
and retain its cost separately.

Before comparing a candidate, retain exact source/driver, CMake/Ninja/Zig,
compiler/SDK, runner-image, CPU budget, shard, and cache identities, including
effective Zig cache paths and how they were prepared. Match the authoritative
expected, detected, and executed coverage identities across both partitions.
Record at least three successful comparable uninstrumented full runs per arm,
preserving failed and cancelled attempts separately. Report configure, build,
full-job, queue, and aggregate runner time, plus CPU/RSS and transfer or
verification cost where available. Do not report the historical configure
interval as whole-workflow savings or idle CPU time. Scheduling and overlap
changes remain owned by #333.

Reproduce the collector's platform-independent controls with:

```sh
python3 tools/ci_configure_evidence_test.py -v
```
