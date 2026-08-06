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
# The CI image is deliberately fixed to x86_64. Do not query adb before the
# build: the emulator can be starting while CMake/Ninja compile and package.
android_abi=${BUSTER_ANDROID_ABI:-x86_64}
if [[ $android_abi != x86_64 ]]; then
    echo "error: Android CI requires the fixed x86_64 emulator ABI (got '$android_abi')" >&2
    exit 1
fi

build_configs_string=${BUSTER_ANDROID_BUILD_CONFIGS:-}
if [[ -z $build_configs_string ]]; then
    if [[ -n ${BUSTER_ANDROID_BUILD_CONFIG:-} ]]; then
        build_configs_string=$BUSTER_ANDROID_BUILD_CONFIG
    else
        build_configs_string="Debug Release"
    fi
fi

if [[ $# -gt 0 ]]; then
    case "$1" in
        --all)
            if [[ $# -ne 1 ]]; then
                echo "usage: $0 [--all|Debug|Release ...]" >&2
                exit 2
            fi
            build_configs_string="Debug Release"
            ;;
        --configs)
            shift
            if [[ $# -eq 0 ]]; then
                echo "error: --configs requires at least one configuration" >&2
                exit 2
            fi
            build_configs_string="$*"
            ;;
        *)
            build_configs_string="$*"
            ;;
    esac
fi

build_configs=()
for build_config in $build_configs_string; do
    case "$build_config" in
        Debug|Release) ;;
        *)
            echo "error: Android CI configurations must be Debug or Release (got '$build_config')" >&2
            exit 1
            ;;
    esac
    build_configs+=("$build_config")
done
if [[ ${#build_configs[@]} -eq 0 ]]; then
    echo "error: no Android build configurations were selected" >&2
    exit 1
fi

build_directory=${BUSTER_ANDROID_BUILD_DIRECTORY:-build/android-ci-${android_abi}}
android_start_script=${BUSTER_ANDROID_START_SCRIPT:-android/start_emulator_ci.sh}
android_run_tests_script=${BUSTER_ANDROID_RUN_TESTS_SCRIPT:-android/run_tests.sh}
android_emulator_started_marker=${BUSTER_ANDROID_EMULATOR_STARTED_MARKER:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/buster-android-emulator.started}
android_cleanup_timeout_seconds=${BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS:-${BUSTER_ANDROID_COMMAND_TIMEOUT_SECONDS:-30}}

cleanup_on_failure() {
    local status=$?
    trap - EXIT INT TERM
    if [[ $status -ne 0 && -f $android_emulator_started_marker ]]; then
        if ! BUSTER_ANDROID_EMULATOR_STARTED_MARKER="$android_emulator_started_marker" \
            BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS="$android_cleanup_timeout_seconds" \
            bash "$android_start_script" stop; then
            echo "warning: Android emulator cleanup failed after test status $status" >&2
        fi
    fi
    exit "$status"
}
trap cleanup_on_failure EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake --version
ninja --version

configure_started=$SECONDS
cmake --warn-uninitialized -Werror=dev \
    -B "$build_directory" \
    -G "Ninja Multi-Config" \
    -DCMAKE_TOOLCHAIN_FILE="${android_ndk}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$android_abi" \
    -DANDROID_PLATFORM="$android_platform" \
    -DCMAKE_DEFAULT_BUILD_TYPE="${build_configs[0]}" \
    -DCMAKE_CONFIGURATION_TYPES="Debug;Release" \
    -DCMAKE_LINKER_TYPE=DEFAULT \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUSTER_CI=ON \
    -DBUSTER_OPTIMIZE=OFF \
    -DBUSTER_SANITIZE=OFF \
    -DBUSTER_FUZZ_AVAILABLE=OFF \
    -DBUSTER_INCLUDE_TESTS=ON \
    -DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF \
    -DBUSTER_DEVELOPER_TARGETS=OFF
echo "TIMING_ANDROID configure_seconds=$((SECONDS - configure_started))"

apk_paths=()
for build_config in "${build_configs[@]}"; do
    echo "Building and packaging Android ${build_config} while the emulator boots"
    build_started=$SECONDS
    cmake --build "$build_directory" --config "$build_config" --target apk --verbose
    apk_source="$build_directory/buster.apk"
    apk_path="$build_directory/$build_config/buster.apk"
    if [[ ! -f $apk_source ]]; then
        echo "error: expected Android APK not found at '$apk_source' after ${build_config} build" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$apk_path")"
    cp -f "$apk_source" "$apk_path"
    apk_paths+=("$apk_path")
    echo "TIMING_ANDROID build_seconds config=$build_config value=$((SECONDS - build_started))"
done

wait_started=$SECONDS
echo "Android artifacts are ready; waiting for the emulator before install/run"
bash "$android_start_script" wait
echo "TIMING_ANDROID boot_wait_seconds=$((SECONDS - wait_started))"

adb_path=${BUSTER_ANDROID_ADB:-}
if [[ -z $adb_path ]]; then
    adb_path=$(command -v adb || true)
fi
if [[ -z $adb_path && -x $android_sdk/platform-tools/adb ]]; then
    adb_path=$android_sdk/platform-tools/adb
fi
if [[ -z $adb_path ]]; then
    echo "error: adb was not found after the Android build; cannot install/run tests" >&2
    exit 1
fi

android_package=${BUSTER_ANDROID_PACKAGE:-dev.buster.ide}
android_activity=${BUSTER_ANDROID_ACTIVITY:-${android_package}/android.app.NativeActivity}
android_test_args=${BUSTER_ANDROID_TEST_ARGUMENT_STRING:-"test --verbose=1 --ci=1"}

overall_status=0
for index in "${!build_configs[@]}"; do
    build_config=${build_configs[$index]}
    apk_path=${apk_paths[$index]}
    echo "Running Android ${build_config} tests"
    test_started=$SECONDS
    if BUSTER_ANDROID_EXPECTED_ABI="$android_abi" \
        bash "$android_run_tests_script" \
            "$adb_path" \
            "$apk_path" \
            "$android_package" \
            "$android_activity" \
            "$android_test_args"; then
        echo "TIMING_ANDROID install_test_seconds config=$build_config value=$((SECONDS - test_started))"
    else
        test_status=$?
        echo "error: Android ${build_config} tests failed with status $test_status" >&2
        overall_status=1
    fi
done

if [[ $overall_status -ne 0 ]]; then
    exit "$overall_status"
fi
