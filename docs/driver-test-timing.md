# Driver-test fixture attribution

This is the first diagnostic stage of [#708](https://github.com/buster14a/buster/issues/708),
not a driver optimization or a claim that the Windows tail has been reduced.
The current-source operation split for [#949](https://github.com/buster14a/buster/issues/949)
uses the same opt-in environment variable and Windows diagnostic workflow.

## What the archived run establishes

Run `35145639100`, attempt 1, commit
`76dbb1014cf87eab5d8aae32fe8b0bdabee84b8e`, Windows x86-64 job
`104960818543`, recorded these `compiler_driver_tests` module intervals:

| Compiler/configuration | Sanitized | Fuzz available | Wall seconds | Assertions | Failed |
| --- | --- | --- | ---: | ---: | ---: |
| Clang Release | no | yes | 132.0311233 | 29740 | 0 |
| Clang Release | yes | no | 147.4104999 | 28772 | 0 |
| Clang Debug | yes | yes | 414.4824948 | 28772 | 0 |

The retained ZIP is artifact `10468275289`,
`desktop-windows-x86_64-35145639100-1`, SHA-256
`9b7aeccdece2ceaf444368573f4744a07e02f84084f60e0c7de795f2a91bf6ff`.
Its `combinations.log` is UTF-16. The sanitized Release and Debug module
records each have 39 primary-arena fixture observations: 32 named function
fixtures followed by seven inline groups. The seven are `prewarm`,
`llvm_integer_output`, `wasm64_output`, `wasm64_alignment_output`,
`wasm64_integer_output`, `assembly_output_path`, and `retired_buster_path`.
Timing only `BUSTER_TEST_FIXTURE` calls would miss those inline groups.

These are overlapping wall intervals from a contended matrix, not CPU time or
additive removable latency. Neither an arena peak nor a module's long wall
interval establishes a cause. Configurations have different coverage and must
not be treated as baseline/candidate arms of the same experiment.

## Opt-in observation, not test selection

Set `BUSTER_TEST_FIXTURE_TIMING=compiler_driver_tests` in the environment of the
existing test/build command. An exact registered module name selects that
module's existing scopes; `all` selects all modules. Empty/unmatched values
select none. This changes **reporting only**: all tests still execute in their
normal order with the normal worker quota and sanitizer environment.

For the Windows full-matrix diagnostic, use the same Visual Studio/LLVM/Zig
bootstrap as [the hosted workflow](ci-github-actions.md), then run:

```powershell
$env:BUSTER_TEST_FIXTURE_TIMING = 'compiler_driver_tests'
try {
    cmd /c '"build\build.exe" test_all_combinations_ci --verbose=1 2>&1' |
        Tee-Object -FilePath 'driver-fixture-attribution.log'
    if ($LASTEXITCODE -ne 0) { throw "Matrix failed with exit code $LASTEXITCODE" }
} finally {
    Remove-Item Env:BUSTER_TEST_FIXTURE_TIMING
}
```

The optional `Windows driver fixture attribution` workflow runs that complete
combination target on `windows-2025`, retains raw logs, source SHA, image
identity and the existing independent coverage result. It runs manually, or on
the initial `diagnostics/708-driver-fixture-timing` PR branch; ordinary PRs do
not acquire an extra Windows matrix. It does not replace any required Buster CI
lane, and its instrumented timings are not acceptance samples. An interrupted
or failed run remains an incomplete diagnostic, not a successful experiment.

Each completed arena scope emits one line, through the same per-module output
owner used by arena diagnostics:

```text
TEST_FIXTURE_TIMING_V1 kind=fixture module=compiler_driver_tests fixture=prewarm index=32 duration_ns=123 measurement=inclusive-wall status=completed
```

This line is a **format example**, not a measured result. `kind=module` describes
the enclosing `body` scope. Names plus per-module invocation indices distinguish
repeated fixtures. A disabled registered fixture makes no extra clock calls or
rows. The small internal reporting self-test still runs separately from module
assertion totals and buffers its synthetic rows rather than publishing them.

The scope snapshots enablement and takes its starting clock after arena mark
setup. Its ending clock precedes cleanup and reporting. Parent intervals include
nested scope work and nested reporting; do not sum nested intervals or infer CPU
cost. Module-scope intervals have slightly different boundaries from the existing
`TEST_MODULE_TIMING` rows and must not replace that series. `status=completed`
means only that the scope ended, **not** that assertions passed. A crash/timeout
may have no ending row. Existing assertion/failure totals and process outcomes
remain authoritative.

## Reading a capture and remaining acceptance work

Keep each run, configuration, compiler identity and instrumentation mode separate.
Within one module invocation, compare the new fixture identities and count with
`TEST_ARENA_V1` primary-arena rows and `TEST_ARENA_TOP_V1 fixtures=...`; do not
hard-code the archived count of 39 for newer source. Use the existing module
summary for module-level history; it intentionally does not merge the new rows.

A slow fixture is the next attribution boundary, not yet a root cause. Split its
in-process compilation from reference compiler/child execution, launch/wait and
capture, filesystem work, and setup/teardown before changing behavior. These
scope rows do **not** provide that child/operation split or a profiler's CPU
stacks. Replay the saved sanitized Debug test command in isolation with
`BUSTER_TEST_JOBS=1`, preserving its exact generated sanitizer environment, and
compare it with the full-matrix diagnostic. Do not regenerate a tree while its
matrix is running or confuse an isolated test with full CI coverage.

With this opt-in setting, `native_frame_vectors` also emits aggregate
`DRIVER_OPERATION_TIMING_V1` rows for Buster compilation, positive-control
compilation, object-byte identity checks, host-object compilation, host-link
launch/wait, and executable launch/wait. The `machine_fallback_primary_corpus`
rows cover only that fixture's first target/mode/frontend corpus, including
strict and reference compilations and sentinel I/O; later subcorpora remain in
the enclosing fixture interval. A `body` row gives the measured scope boundary.
Each row has `calls` and `duration_ns`; zero calls means that the platform or
test outcome did not enter that operation. The difference between `body` and
listed operations includes unsampled setup, checks, cleanup, and the observer
itself, so do not label it all as removable setup. Intervals are wall times
under matrix contention, never CPU usage. Compare identical configurations and
correlate with the retained Windows process events before choosing a repair.

For a subsequent optimization, retain at least three successful comparable full
uninstrumented runs per arm, matched runner/image and configuration identities,
coverage/assertion summaries and sanitizer evidence. Also retain unsuccessful
attempts; report median and spread for both the driver module and whole Windows
job. Keep normal exact-head CI and the independent bootstrap evidence required
by [the testing guide](agents/testing.md). Do not reduce configuration coverage,
weaken sanitizer settings, split optimized Release unity, add nested worker pools,
or claim the archived 414 seconds as guaranteed savings.
