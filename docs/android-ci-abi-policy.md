# Android CI ABI policy

Buster supports both 64-bit Android NDK ABIs used by the project, with different
execution guarantees on GitHub-hosted infrastructure.

| ABI | Required configurations | GitHub CI contract |
| --- | --- | --- |
| `x86_64` | Debug and Release | NDK compile, native link, APK package/sign, emulator install, and execution of the registered suite on the pinned API 35 x86-64 image. |
| `arm64-v8a` (AArch64) | Debug and Release | NDK compile, native link, and APK package/sign for API 35. The produced APKs must contain the architecture-specific native library selected by the production packaging graph. |

The AArch64 leg is intentionally a compatibility gate, not a runtime pass. The
required GitHub Android job runs on an x86-64 Linux host and uses KVM with the
pinned x86-64 emulator image. It does not provide a reliable hardware-accelerated
AArch64 Android execution environment. Until a qualified AArch64 emulator or
physical-device runner exists, Android AArch64 runtime behavior remains
**not run**, and CI must not describe package success as execution coverage.

Both ABI legs run inside the existing required `Android x86-64` mobile job. The
x86-64 emulator starts first; its Debug and Release APKs and the AArch64 Debug
and Release APKs are then built while it boots. Any AArch64 configure, compile,
link, package, or signing failure fails the same `Test (Android)` step, which is
already required by `CI complete`. No desktop artifact is reused and no second
runner is allocated.

The AArch64 leg emits `ANDROID_PACKAGE_CONFIG_RESULT` for each configuration,
`ANDROID_PACKAGE_BATCH_RESULT` for the package batch, and the final
`ANDROID_AARCH64_COVERAGE` classification. `TIMING_ANDROID_PACKAGE` and
`TIMING_ANDROID aarch64_package_seconds=...` expose the added work without
claiming a speedup or treating unavailable runtime time as zero. The existing
`ANDROID_CONFIG_RESULT`, `ANDROID_BATCH_RESULT`, payload, monitor, and workflow
records remain the authoritative x86-64 runtime evidence.

## Reproduction

With an Android SDK, API 35 platform/build tools, the NDK, CMake, Ninja, a JDK,
and Bash available:

```sh
BUSTER_ANDROID_ABI=arm64-v8a ./android/test_ci.sh --package-only --all
```

The command writes retained artifacts to:

```text
build/android-ci-arm64-v8a/Debug/buster.apk
build/android-ci-arm64-v8a/Release/buster.apk
```

It never starts or queries an emulator. To exercise the complete hosted policy
locally while an x86-64 emulator lifecycle is already managed by the caller:

```sh
BUSTER_ANDROID_AARCH64_PACKAGE=1 ./android/test_ci.sh --all
```

`BUSTER_ANDROID_AARCH64_BUILD_DIRECTORY` may select a separate AArch64 build
tree. It must not equal the x86-64 runtime build tree. The required GitHub
mobile job is fail-closed: it always enables this gate and rejects an attempt to
set `BUSTER_ANDROID_AARCH64_PACKAGE=0`.
