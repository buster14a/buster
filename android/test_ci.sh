#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

if [[ -f /opt/vulkan-sdk/current/setup-env.sh ]]; then
    # Desktop CI uses the same SDK setup; Android still needs glslc/slangc to
    # compile shaders even though Vulkan headers come from the NDK sysroot.
    # shellcheck disable=SC1091
    source /opt/vulkan-sdk/current/setup-env.sh
fi

android_sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [[ -z ${android_sdk} ]]; then
    echo "error: ANDROID_HOME or ANDROID_SDK_ROOT must point at an Android SDK" >&2
    exit 1
fi

android_ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [[ -z ${android_ndk} ]]; then
    if [[ -d ${android_sdk}/ndk ]]; then
        android_ndk=$(find "${android_sdk}/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)
    elif [[ -d ${android_sdk}/ndk-bundle ]]; then
        android_ndk=${android_sdk}/ndk-bundle
    fi
fi

if [[ -z ${android_ndk} || ! -f ${android_ndk}/build/cmake/android.toolchain.cmake ]]; then
    echo "error: could not find the Android NDK CMake toolchain" >&2
    exit 1
fi

android_platform=${BUSTER_ANDROID_PLATFORM:-android-35}
build_config=${BUSTER_ANDROID_BUILD_CONFIG:-Debug}
adb_wait_timeout_seconds=${BUSTER_ANDROID_ADB_WAIT_TIMEOUT_SECONDS:-60}

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: timeout command is required for Android CI device detection" >&2
    exit 1
fi

adb_getprop_retry() {
    local property=$1
    local attempt output
    for attempt in {1..10}; do
        if output=$(timeout 10 adb shell getprop "$property" 2>/dev/null | tr -d '\r') && [[ -n $output ]]; then
            printf '%s\n' "$output"
            return 0
        fi
        sleep 2
    done
    return 1
}

adb version
cmake --version
ninja --version
echo "Waiting up to ${adb_wait_timeout_seconds}s for an Android device..."
if ! timeout "$adb_wait_timeout_seconds" adb wait-for-device; then
    echo "error: no Android device became available within ${adb_wait_timeout_seconds}s" >&2
    adb devices >&2 || true
    exit 1
fi
adb devices

android_abi=${BUSTER_ANDROID_ABI:-}
if [[ -z ${android_abi} ]]; then
    if ! android_abi=$(adb_getprop_retry ro.product.cpu.abi); then
        echo "error: could not read Android device ABI" >&2
        adb devices >&2 || true
        exit 1
    fi
fi
if [[ -z ${android_abi} ]]; then
    echo "error: could not determine Android device ABI; set BUSTER_ANDROID_ABI" >&2
    exit 1
fi

# One directory for all configs: the Ninja Multi-Config generator keeps
# per-config artifacts separate, so the Debug and Release CI invocations
# share a single CMake configure (the NDK toolchain + try_compile probes
# are the expensive part) instead of paying it once per config.
build_directory=${BUSTER_ANDROID_BUILD_DIRECTORY:-build/android-ci-${android_abi}}

# This job intentionally uses an already-running full GUI Android device/emulator.
# Do not start a headless emulator here; the test_all target launches the app and
# waits for the in-app GUI test result through logcat.
cmake --warn-uninitialized -Werror=dev \
    -B "$build_directory" \
    -G "Ninja Multi-Config" \
    -DCMAKE_TOOLCHAIN_FILE="${android_ndk}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$android_abi" \
    -DANDROID_PLATFORM="$android_platform" \
    -DCMAKE_DEFAULT_BUILD_TYPE="$build_config" \
    -DCMAKE_CONFIGURATION_TYPES="Debug;Release;RelWithDebInfo;MinSizeRel" \
    -DCMAKE_LINKER_TYPE=DEFAULT \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUSTER_CI=ON \
    -DBUSTER_OPTIMIZE=OFF \
    -DBUSTER_SANITIZE=OFF \
    -DBUSTER_FUZZ_AVAILABLE=OFF \
    -DBUSTER_INCLUDE_TESTS=ON \
    -DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF \
    -DBUSTER_DEVELOPER_TARGETS=OFF

cmake --build "$build_directory" --config "$build_config" --target test_all --verbose
