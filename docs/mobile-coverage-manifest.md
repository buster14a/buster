# Mobile CI effective coverage manifest

Each GitHub mobile lane writes the shared CI coverage artifact
`RUNNER_TEMP/buster-ci/coverage.json`. The existing mobile artifact upload
retains it beside `android.log` or `ios.log`, `result.json`, and `summary.md`.
`tools/ci_summary.py` routes the mobile manifest through the same fail-closed
coverage slot used by desktop lanes, then renders the mobile contract in the
GitHub job summary.

`tools/mobile_coverage.py` does not select configurations or launch devices.
`android/test_ci.sh`, `ios/test_ci.sh`, and the platform lifecycle scripts
remain the execution-policy authorities. After they finish, the result step
consumes their anchored final records and binds them to the exact checkout,
workflow, driver/lifecycle scripts, CMake cache, configured compiler executable,
configuration artifacts, runner/run/attempt, and device or simulator identity.
The producer re-probes Git `HEAD` and the workflow-declared runner label instead
of trusting the event variables as self-authenticating evidence.

## Reviewed lanes

| Lane | Required configurations | Effective execution |
|---|---|---|
| Android x86-64 | Debug and Release APKs | Both execute the registered suite on the exact emulator serial, AVD, ABI, API level, and system image recorded in the manifest. |
| iOS x86-64 | Debug and Release app bundles | Compile, link, bundle, and architecture validation only. The manifest retains the hosted Xcode 26 Intel-runtime rationale. |
| iOS AArch64 | Debug and Release app bundles | Both execute in the exact simulator runtime/UDID; cleanup must leave that device in `Shutdown`. |

Every required row records the actual Clang/AppleClang executable identity,
target, Debug/Release configuration, sanitizer/fuzz state, ABI, artifact kind,
execution class, artifact digest, and completion status. Explicit exclusions
name the owning lane or policy reason for mobile omissions: sanitizer, fuzz,
register-allocation mode matrix, static analysis, table audit, self-host,
fixed point, Android AArch64, and hosted iOS Intel runtime execution where
applicable.

## Fail-closed behavior

On a real GitHub mobile lane the summary wrapper automatically marks coverage
as mandatory and points the common summary consumer at `coverage.json`. The
result step fails when the manifest is missing or malformed, either
configuration is absent, final runtime/build-only evidence is not successful,
an artifact/toolchain/evidence hash is stale, runner/event identity disagrees,
an exclusion rationale changes, or the workflow no longer contains exactly
the three reviewed mobile lanes. The required mobile matrix still feeds `CI
complete`, so a smaller surviving lane set cannot certify the aggregate gate.

The deterministic regression guard is also available locally:

```sh
python3 tools/mobile_coverage.py self-test
```

It verifies the current workflow lane census and rejects missing Release rows,
execution reclassification, count shrinkage, removed rationales, weakened
obligations, duplicate detections, incomplete completion evidence, and
malformed artifact hashes. The guard runs again while every hosted mobile
manifest is produced; a producer cannot silently shrink its own expected set
and then report success.
