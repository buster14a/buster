# Desktop Zig configure evidence

[Build guidance](agents/build.md) · [CI reproduction](ci-github-actions.md) ·
[Investigation #707](https://github.com/buster14a/buster/issues/707)

This is diagnostic infrastructure, not a configure optimization or a speedup
claim. The native `test_all_combinations_ci` command still owns compiler
selection, graph generation, probes, scheduling, coverage and failure policy.
No compiler-works result is preseeded and no Zig compilation cache is restored.

## Retained evidence

Every desktop lane copies a fixed allowlist from the native driver's
`build/build-ci_on-cc_*` trees after the combination step into the existing
`desktop-<os>-<arch>-<run>-<attempt>` artifact, under `configure/`:

- `CMakeCache.txt` and `CMakeFiles/CMakeConfigureLog.yaml`;
- legacy `CMakeFiles/CMakeOutput.log` and `CMakeFiles/CMakeError.log`, when present;
- `cmake-profile.json`, when native profiling was enabled.

`configure/manifest.json` records each path as captured, missing or error, with
byte length and SHA-256 for captured bytes. It includes only an explicit
allowlist of run/runner identifiers, not a copy of the environment. The source
and native driver identities remain in the adjacent authoritative
`coverage.json`; the exact configure command, native timings and profile
summary remain in `combinations.log`. Keep these files together with
`result.json`, `zig.log`, the original job log and the downloaded ZIP digest.
The manifest is diagnostic-only: it neither certifies successful probes nor
replaces the coverage/result consumers.

The collector performs no build/toolchain subprocesses and does not parse or
sum profile events. It examines at most 4,096 build-root entries and 32 matching
trees, retains at most 32 MiB per file and 128 MiB total, refuses links and
Windows reparse points below the selected root, and requires a fresh output
directory outside the build tree. Collection/size errors are retained in the
manifest and fail the collection step. Missing optional diagnostics are
explicit, including a profile absent from a failed configure. Raw files are
not truncated or normalized. Run collection only after the matrix has stopped;
it is not a live or atomic snapshot of concurrently changing build trees.

Raw CMake diagnostics may contain absolute toolchain/SDK paths and command-line
options. Do not pass credentials through CMake options or place secrets in these
files. This artifact is never restored as a build or test cache.

## Diagnostic run

Dispatch the existing **Buster CI** workflow with `cmake_profile=true`. The
workflow maps that input to the existing native `BUSTER_CMAKE_PROFILE=1`
switch on desktop lanes. The driver writes one `cmake-profile.json` per
configure tree and prints its existing `cmake_profile_summary` output. There
is no separate configure-only workflow and no reduction of the matrix.
PR, main/tag, merge-group and ordinary manual runs leave profiling disabled.

For an existing, correctly prepared hosted-equivalent checkout, the relevant
native invocation is:

```sh
BUSTER_CMAKE_PROFILE=1 ./build/build test_all_combinations_ci --verbose=1
```

On Windows, use the same Visual Studio/LLVM environment and verified Zig setup
as `.github/workflows/ci.yml`:

```powershell
$env:BUSTER_CMAKE_PROFILE = '1'
.\build\build.exe test_all_combinations_ci --verbose=1
```

These snippets do not install tools or replace the workflow's bootstrap. Do not
move the hosted driver from `build/build[.exe]`: coverage pins that location.
After completion, manual collection uses a fresh output directory:

```sh
python3 tools/ci_configure_evidence.py --build-root build --output /path/outside/build/configure --profile-requested 1
```

Use `python` on the hosted Windows lane. An existing profile can also be
summarized with the native driver:

```sh
./build/build cmake_profile_summary build/build-ci_on-cc_zig-sanitize_off-fuzz_available_off-configs_Debug/cmake-profile.json --limit 30
```

CMake's profile describes time inside CMake commands, including time waiting
for children. It is not process CPU, RSS or filesystem attribution. Nested
commands must not be summed as independent wall time. Use the raw configure
log and exact subprocess commands to decide whether OS-level tracing of a
compiler/ABI/runtime-cache operation is needed. Windows attribution must not
be assumed to hold on Intel macOS.

## Performance experiment remains separate

Use profiling only to locate work. For timed repetitions set
`BUSTER_CMAKE_PROFILE=0` (or leave it unset outside the workflow). Independently
prepare and document cold and warm **Zig compilation/runtime-cache** cases;
a hit on the verified download-archive cache does not establish either state.
Do not reuse Buster objects, compiler artifacts or passing test verdicts. Keep
cache preparation outside measured configuration and retain its cost separately.

Before comparing a candidate, retain exact source/driver, CMake/Ninja/Zig,
compiler/SDK, runner-image, CPU budget and cache identities, including effective
Zig cache paths and how they were prepared. Match the authoritative expected,
detected and executed coverage identities and obligations. Record at least
three successful, comparable, uninstrumented full runs per arm, preserving all
failed/cancelled attempts separately. Report configure, build, full-job, queue
and aggregate runner time, plus CPU/RSS and any transfer/verification cost where
available. Do not report the historical configure interval as whole-workflow
savings or as idle CPU time. Scheduling/overlap changes remain owned by #333.

Reproduce the collector's platform-independent controls with:

```sh
python3 tools/ci_configure_evidence_test.py -v
```
