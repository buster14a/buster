#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

# shellcheck source=android/payload_deadline_policy.sh
source "$repo_root/android/payload_deadline_policy.sh"

android_execution=${BUSTER_ANDROID_EXECUTION:-runtime}
if [[ $# -gt 0 && $1 == --package-only ]]; then
    android_execution=package
    shift
fi
case "$android_execution" in
    runtime|package) ;;
    *)
        echo "error: BUSTER_ANDROID_EXECUTION must be runtime or package (got '$android_execution')" >&2
        exit 1
        ;;
esac

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
android_abi=${BUSTER_ANDROID_ABI:-x86_64}
case "$android_execution" in
    runtime)
        # The runtime CI image is deliberately fixed to x86_64. Do not query adb
        # before the build: the emulator can start while CMake/Ninja package.
        if [[ $android_abi != x86_64 ]]; then
            echo "error: Android runtime CI requires the fixed x86_64 emulator ABI (got '$android_abi')" >&2
            exit 1
        fi
        ;;
    package)
        case "$android_abi" in
            x86_64|arm64-v8a) ;;
            *)
                echo "error: Android package CI supports x86_64 or arm64-v8a (got '$android_abi')" >&2
                exit 1
                ;;
        esac
        ;;
esac

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
                echo "usage: $0 [--package-only] [--all|--configs Debug Release|Debug|Release ...]" >&2
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

# See docs/android-ci-abi-policy.md for the supported-ABI and runtime boundary.
android_aarch64_package=${BUSTER_ANDROID_AARCH64_PACKAGE:-auto}
if [[ $android_execution == package ]]; then
    android_aarch64_package=0
elif [[ ${GITHUB_ACTIONS:-false} == true && ${GITHUB_JOB:-} == mobile ]]; then
    if [[ $android_aarch64_package == 0 ]]; then
        echo "error: the required GitHub Android job cannot disable AArch64 package coverage" >&2
        exit 1
    fi
    android_aarch64_package=1
elif [[ $android_aarch64_package == auto ]]; then
    android_aarch64_package=0
fi
case "$android_aarch64_package" in
    0|1) ;;
    *)
        echo "error: BUSTER_ANDROID_AARCH64_PACKAGE must be auto, 0, or 1 (got '$android_aarch64_package')" >&2
        exit 1
        ;;
esac
android_aarch64_build_directory=${BUSTER_ANDROID_AARCH64_BUILD_DIRECTORY:-build/android-ci-arm64-v8a}
if [[ $android_aarch64_package == 1 && $android_aarch64_build_directory == "$build_directory" ]]; then
    echo "error: x86-64 runtime and arm64-v8a package coverage require separate build directories" >&2
    exit 1
fi

android_phase=configure
android_config=none
config_statuses=()

cleanup_on_failure() {
    local status=$?
    local cleanup_status=not-run
    local result_index
    trap - EXIT INT TERM
    if [[ $android_execution == runtime && $status -ne 0 && -f $android_emulator_started_marker ]]; then
        if BUSTER_ANDROID_EMULATOR_STARTED_MARKER="$android_emulator_started_marker" \
            BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS="$android_cleanup_timeout_seconds" \
            bash "$android_start_script" stop; then
            cleanup_status=0
        else
            cleanup_status=$?
            echo "warning: Android emulator cleanup failed after test status $status (cleanup status $cleanup_status)" >&2
        fi
    fi
    if [[ $android_execution == package ]]; then
        for result_index in "${!build_configs[@]}"; do
            printf 'ANDROID_PACKAGE_CONFIG_RESULT abi=%s config=%s status=%s\n' \
                "$android_abi" "${build_configs[$result_index]}" "${config_statuses[$result_index]:-not-run}"
        done
        printf 'ANDROID_PACKAGE_BATCH_RESULT abi=%s phase=%s config=%s status=%s\n' \
            "$android_abi" "$android_phase" "$android_config" "$status"
        if [[ $status -ne 0 ]]; then
            printf 'error: Android package batch failed in phase %s (abi=%s config=%s status=%s)\n' \
                "$android_phase" "$android_abi" "$android_config" "$status" >&2
        fi
    else
        for result_index in "${!build_configs[@]}"; do
            printf 'ANDROID_CONFIG_RESULT config=%s status=%s\n' \
                "${build_configs[$result_index]}" "${config_statuses[$result_index]:-not-run}"
        done
        printf 'ANDROID_BATCH_RESULT phase=%s config=%s status=%s cleanup_status=%s\n' \
            "$android_phase" "$android_config" "$status" "$cleanup_status"
        if [[ $status -ne 0 ]]; then
            printf 'error: Android batch failed in phase %s (config=%s status=%s); a later configuration success does not clear an earlier failure\n' \
                "$android_phase" "$android_config" "$status" >&2
        fi
    fi
    exit "$status"
}
trap cleanup_on_failure EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

verify_android_apk() {
    local apk_path=$1
    local abi=$2
    python3 - "$apk_path" "$abi" <<'PY_VERIFY_APK'
import sys
import zipfile

apk_path, abi = sys.argv[1:]
entry = f"lib/{abi}/libide.so"
expected_machine = {"x86_64": 62, "arm64-v8a": 183}[abi]
with zipfile.ZipFile(apk_path) as archive:
    names = archive.namelist()
    if names.count(entry) != 1:
        raise SystemExit(f"error: {apk_path} must contain exactly one {entry}")
    image = archive.read(entry)
if len(image) < 20 or image[:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
    raise SystemExit(f"error: {entry} is not a little-endian ELF64 image")
machine = int.from_bytes(image[18:20], "little")
if machine != expected_machine:
    raise SystemExit(
        f"error: {entry} has ELF machine {machine}, expected {expected_machine} for {abi}"
    )
print(
    f"ANDROID_PACKAGE_VERIFY abi={abi} entry={entry} "
    f"elf_machine={machine} status=0"
)
PY_VERIFY_APK
}

configure_android_tree() {
    local directory=$1
    local abi=$2
    # CMake 4.4 promotes the NDK r27 toolchain's own pre-3.10 compatibility
    # declarations to developer errors under -Werror=dev. Keep every other
    # developer diagnostic fatal while leaving third-party deprecation policy to
    # the pinned NDK instead of patching its installed files.
    cmake --warn-uninitialized -Werror=dev -Wno-error=deprecated \
        -B "$directory" \
        -G "Ninja Multi-Config" \
        -DCMAKE_TOOLCHAIN_FILE="${android_ndk}/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
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
}

cmake --version
ninja --version

configure_started=$SECONDS
configure_android_tree "$build_directory" "$android_abi"
if [[ $android_execution == package ]]; then
    echo "TIMING_ANDROID_PACKAGE abi=$android_abi configure_seconds=$((SECONDS - configure_started))"
else
    echo "TIMING_ANDROID configure_seconds=$((SECONDS - configure_started))"
fi

apk_paths=()
android_phase=build
for index in "${!build_configs[@]}"; do
    build_config=${build_configs[$index]}
    android_config=$build_config
    if [[ $android_execution == runtime ]]; then
        echo "Building and packaging Android ${build_config} while the emulator boots"
    else
        echo "Building and packaging Android ${android_abi} ${build_config}"
    fi
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
    if [[ $android_execution == package ]]; then
        verify_android_apk "$apk_path" "$android_abi"
        config_statuses[$index]=0
        printf 'ANDROID_PACKAGE_ARTIFACT abi=%s config=%s path=%s\n' \
            "$android_abi" "$build_config" "$apk_path"
        echo "TIMING_ANDROID_PACKAGE abi=$android_abi build_seconds config=$build_config value=$((SECONDS - build_started))"
    else
        echo "TIMING_ANDROID build_seconds config=$build_config value=$((SECONDS - build_started))"
    fi
done

if [[ $android_execution == package ]]; then
    android_phase=package
    android_config=none
    exit 0
fi

if [[ $android_aarch64_package == 1 ]]; then
    android_phase=aarch64-package
    android_config=none
    aarch64_started=$SECONDS
    echo "Building required Android AArch64 compile/link/package coverage"
    BUSTER_ANDROID_EXECUTION=package \
        BUSTER_ANDROID_ABI=arm64-v8a \
        BUSTER_ANDROID_BUILD_DIRECTORY="$android_aarch64_build_directory" \
        BUSTER_ANDROID_AARCH64_PACKAGE=0 \
        BUSTER_ANDROID_EMULATOR_STARTED_MARKER="${android_emulator_started_marker}.aarch64-package" \
        bash "$repo_root/android/test_ci.sh" --package-only --configs "${build_configs[@]}"
    printf 'ANDROID_AARCH64_COVERAGE abi=arm64-v8a execution=compile-link-package runtime=not-run reason=github-hosted-x86_64-kvm status=0\n'
    echo "TIMING_ANDROID aarch64_package_seconds=$((SECONDS - aarch64_started))"
fi

android_phase=boot-wait
android_config=none
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
first_failed_config=none
android_phase=tests
for index in "${!build_configs[@]}"; do
    build_config=${build_configs[$index]}
    android_config=$build_config
    apk_path=${apk_paths[$index]}
    buster_android_payload_deadline_policy "$build_config"
    printf 'ANDROID_DEADLINE_POLICY config=%s timeout_seconds=%s headroom_warning_percent=%s\n' \
        "$build_config" "$android_payload_timeout_seconds" "$android_payload_headroom_warning_percent"
    echo "Running Android ${build_config} tests"
    test_started=$SECONDS
    if BUSTER_ANDROID_EXPECTED_ABI="$android_abi" \
        BUSTER_ANDROID_TEST_CONFIG="$build_config" \
        BUSTER_ANDROID_TEST_TIMEOUT_SECONDS="$android_payload_timeout_seconds" \
        BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT="$android_payload_headroom_warning_percent" \
        bash "$android_run_tests_script" \
            "$adb_path" \
            "$apk_path" \
            "$android_package" \
            "$android_activity" \
            "$android_test_args"; then
        config_statuses[$index]=0
        echo "TIMING_ANDROID install_test_seconds config=$build_config value=$((SECONDS - test_started))"
    else
        test_status=$?
        config_statuses[$index]=$test_status
        echo "error: Android ${build_config} tests failed with status $test_status" >&2
        overall_status=1
        if [[ $first_failed_config == none ]]; then
            first_failed_config=$build_config
        fi
        # An interrupted payload is a cancelled batch, not another test
        # failure to aggregate. Preserve its status and do not launch more work.
        if [[ $test_status -eq 130 || $test_status -eq 143 ]]; then
            android_config=$first_failed_config
            exit "$test_status"
        fi
    fi
done

android_config=$first_failed_config
android_phase=tests
exit "$overall_status"
