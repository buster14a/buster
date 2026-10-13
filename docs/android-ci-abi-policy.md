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
x86-64 emulator starts first. While it boots, the native build driver's
`android_packages` command (`tools/android_packages.c`) runs one
`android/test_ci.sh --package-only` batch per ABI at the same time: x86-64 in
`build/android-ci-x86_64` and AArch64 in `build/android-ci-arm64-v8a`, each
building Debug and Release (#3912). Before this, the AArch64 batch ran only
after the x86-64 builds had finished. The job passes the Clang-built driver as
`BUSTER_ANDROID_HOST_DRIVER` and runs `android_packages --self-test` before
the build. Any configure, compile, link, package, signing or APK-verification
failure in either batch fails the same `Test (Android)` step, which is already
required by `CI complete`. A failed batch does not cancel the other one; both
finish and both are reported. No desktop artifact is reused and no second
runner is allocated.

Each batch's output is captured and replayed in ABI order, so lines from the
two builds never interleave. Each batch emits `ANDROID_PACKAGE_VERIFY` and
`ANDROID_PACKAGE_CONFIG_RESULT` for each configuration, a per-configuration
`TIMING_ANDROID_PACKAGE ... build_seconds`, and `ANDROID_PACKAGE_BATCH_RESULT`.
The x86-64 APKs are now verified as well. The driver then adds:

- one `ANDROID_PACKAGES_BATCH` line per ABI, with its exit status, wall time
  and `peak_rss_bytes`;
- `ANDROID_PACKAGES_MEMORY`, with physical memory, the sum of the batch peaks
  and the emulator's resident peak (`VmHWM`) at that point.

Each batch peak is its largest process, from `wait4`, and is never lower than
the driver's own resident memory. The sum is therefore an upper bound on what
the two builds held at once.

`TIMING_ANDROID packages_seconds=...` is the wall time of the concurrent phase,
and `ANDROID_AARCH64_COVERAGE` follows once both batches and the x86-64 APK
hand-off have succeeded. The existing `ANDROID_CONFIG_RESULT`,
`ANDROID_BATCH_RESULT`, payload, monitor, and workflow records remain the
authoritative x86-64 runtime evidence. A failure in the concurrent phase is
reported as `ANDROID_BATCH_RESULT phase=build config=none`.

Without AArch64 package coverage, a local runtime run builds x86-64 inline as
before and emits `TIMING_ANDROID configure_seconds` and per-configuration
`TIMING_ANDROID build_seconds` instead.

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

This runs the two package batches through `BUSTER_ANDROID_HOST_DRIVER`, or
`build.sh` when that is unset. The TCC-bootstrapped `build.sh` driver is
single-threaded, so it runs the batches one after another and streams their
output. A multi-threaded driver, such as the Clang-built one the hosted job
uses, runs them concurrently. `./build.sh android_packages --self-test
<scratch-directory>` checks the command with fake batches that re-invoke the
driver. With threads, it proves both batches drain and overlap, that their
environment is right, and that a failure or a capture overrun fails the
command. Without threads, it checks the serial order.

`BUSTER_ANDROID_AARCH64_BUILD_DIRECTORY` may select a separate AArch64 build
tree. It must not equal the x86-64 runtime build tree. The required GitHub
mobile job is fail-closed: it always enables this gate and rejects an attempt to
set `BUSTER_ANDROID_AARCH64_PACKAGE=0`.
