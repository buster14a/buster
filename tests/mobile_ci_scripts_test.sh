#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fake_tool="$repo_root/tests/mobile_ci_fake_tool.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-mobile-ci.XXXXXX")

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

fake_bin="$test_root/bin"
mkdir -p "$fake_bin"
for tool in adb emulator cmake ninja codesign xcrun; do
    ln -s "$fake_tool" "$fake_bin/$tool"
done

assert_file_contains() {
    local needle=$1
    local path=$2
    if ! grep -qF "$needle" "$path"; then
        echo "assertion failed: '$needle' not found in $path" >&2
        exit 1
    fi
}

assert_count() {
    local expected=$1
    local needle=$2
    local path=$3
    local actual
    actual=$(grep -cF "$needle" "$path" 2>/dev/null || true)
    if [[ $actual -ne $expected ]]; then
        echo "assertion failed: expected $expected occurrences of '$needle' in $path, got $actual" >&2
        exit 1
    fi
}

test_mobile_workflow_uses_batch_invocations() (
    set -euo pipefail
    local workflow="$repo_root/.forgejo/workflows/ci.yml"
    assert_count 1 'bash ./android/start_emulator_ci.sh start' "$workflow"
    assert_count 1 'bash ./android/start_emulator_ci.sh stop' "$workflow"
    assert_count 1 './android/test_ci.sh --all' "$workflow"
    assert_count 1 './ios/test_ci.sh --all' "$workflow"
    if grep -qF 'adb emu kill' "$workflow"; then
        echo "assertion failed: workflow still bypasses the Android lifecycle helper" >&2
        exit 1
    fi
    if grep -qF 'rm -f "$BUSTER_ANDROID_EMULATOR_STARTED_MARKER"' "$workflow"; then
        echo "assertion failed: workflow still removes the owned Android PID marker" >&2
        exit 1
    fi
    if grep -qF 'BUSTER_ANDROID_BUILD_CONFIG=' "$workflow"; then
        echo "assertion failed: workflow still has per-config Android invocations" >&2
        exit 1
    fi
    if grep -qF 'BUSTER_IOS_BUILD_CONFIG=' "$workflow"; then
        echo "assertion failed: workflow still has per-config iOS invocations" >&2
        exit 1
    fi
)

run_android_workflow_cleanup_contract() {
    local prior_status=$1
    local marker=$2
    local cleanup_status

    if BUSTER_ANDROID_EMULATOR_STARTED_MARKER="$marker" \
        BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS=1 \
        bash "$repo_root/android/start_emulator_ci.sh" stop; then
        cleanup_status=0
    else
        cleanup_status=$?
        if [[ $prior_status -eq 0 ]]; then
            prior_status=1
        fi
    fi
    if [[ $cleanup_status -ne 0 && $prior_status -eq 0 ]]; then
        prior_status=1
    fi
    return "$prior_status"
}

test_android_success_and_snapshots() (
    set -euo pipefail
    local state="$test_root/android-success"
    local sdk="$state/sdk"
    local ndk="$sdk/ndk/fake"
    local build="$state/build"
    local marker="$state/emulator.started"
    mkdir -p "$state" "$sdk" "$ndk/build/cmake" \
        "$sdk/system-images/android-35/google_apis/x86_64"
    : >"$ndk/build/cmake/android.toolchain.cmake"

    export PATH="$fake_bin:$PATH"
    export ANDROID_HOME="$sdk"
    export ANDROID_NDK_HOME="$ndk"
    export FAKE_ANDROID_STATE_DIR="$state"
    export BUSTER_ANDROID_BUILD_DIRECTORY="$build"
    export BUSTER_ANDROID_EMULATOR_STARTED_MARKER="$marker"
    export BUSTER_ANDROID_EMULATOR_LOG="$state/emulator.log"
    export BUSTER_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS=8
    export BUSTER_ANDROID_COMMAND_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_ADB_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_TOOL_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS=2
    export FAKE_ANDROID_BOOT_DELAY_SECONDS=5
    export FAKE_CMAKE_LOG="$state/cmake.log"

    start_started=$SECONDS
    bash "$repo_root/android/start_emulator_ci.sh" start
    start_elapsed=$((SECONDS - start_started))
    [[ -f $marker ]]
    if [[ $start_elapsed -ge 5 ]]; then
        echo "assertion failed: async Android start waited ${start_elapsed}s for boot" >&2
        exit 1
    fi

    bash "$repo_root/android/test_ci.sh" --all
    assert_file_contains Debug "$build/Debug/buster.apk"
    assert_file_contains Release "$build/Release/buster.apk"
    assert_file_contains 'Debug apk' "$state/cmake.log"
    assert_file_contains 'Release apk' "$state/cmake.log"
    bash "$repo_root/android/start_emulator_ci.sh" stop
    [[ ! -f $marker ]]
)

test_android_timeout_and_cleanup_fallback() (
    set -euo pipefail
    local state="$test_root/android-timeout"
    local sdk="$state/sdk"
    local ndk="$sdk/ndk/fake"
    local marker="$state/emulator.started"
    local error_log="$state/wait.err"
    mkdir -p "$state" "$sdk" "$ndk/build/cmake"
    : >"$ndk/build/cmake/android.toolchain.cmake"

    export PATH="$fake_bin:$PATH"
    export ANDROID_HOME="$sdk"
    export ANDROID_NDK_HOME="$ndk"
    export FAKE_ANDROID_STATE_DIR="$state"
    export BUSTER_ANDROID_EMULATOR_STARTED_MARKER="$marker"
    export BUSTER_ANDROID_EMULATOR_LOG="$state/emulator.log"
    export BUSTER_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_COMMAND_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_ADB_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_TOOL_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS=1
    export FAKE_ANDROID_NEVER_BOOT=1
    export FAKE_ANDROID_BOOT_DELAY_SECONDS=1

    bash "$repo_root/android/start_emulator_ci.sh" start
    set +e
    bash "$repo_root/android/start_emulator_ci.sh" wait 2>"$error_log"
    wait_status=$?
    set -e
    [[ $wait_status -ne 0 ]]
    assert_file_contains 'did not connect within' "$error_log"
    [[ ! -f $marker ]]

    # Exercise the workflow's helper-stop contract after a fresh owned start.
    rm -f "$state/kill"
    export FAKE_ANDROID_ADB_KILL_STATUS=7
    bash "$repo_root/android/start_emulator_ci.sh" start
    [[ -f $marker ]]
    owned_pid=$(cat "$marker")
    set +e
    run_android_workflow_cleanup_contract 0 "$marker"
    cleanup_status=$?
    set -e
    [[ $cleanup_status -ne 0 ]]
    [[ ! -f $marker ]]
    if kill -0 "$owned_pid" >/dev/null 2>&1; then
        echo "assertion failed: lifecycle stop left the owned emulator PID alive" >&2
        exit 1
    fi

    # A pre-existing job failure must survive a cleanup failure unchanged.
    rm -f "$state/kill"
    bash "$repo_root/android/start_emulator_ci.sh" start
    [[ -f $marker ]]
    owned_pid=$(cat "$marker")
    set +e
    run_android_workflow_cleanup_contract 23 "$marker"
    cleanup_status=$?
    set -e
    [[ $cleanup_status -eq 23 ]]
    [[ ! -f $marker ]]
    if kill -0 "$owned_pid" >/dev/null 2>&1; then
        echo "assertion failed: lifecycle stop left the owned emulator PID alive" >&2
        exit 1
    fi
)

test_ios_batch_and_cleanup() (
    set -euo pipefail
    local state="$test_root/ios-success"
    local build="$state/build"
    local log="$state/xcrun.log"
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    : >"$log"

    export PATH="$fake_bin:$PATH"
    export FAKE_IOS_STATE_DIR="$state"
    export FAKE_IOS_LOG="$log"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_BUILD_DIRECTORY="$build"
    export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=3
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=3
    export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=3
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3
    export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=3

    bash "$repo_root/ios/test_ci.sh" --all
    assert_count 1 'simctl boot FAKE-UDID' "$log"
    assert_count 1 'simctl shutdown FAKE-UDID' "$log"
    assert_count 2 'simctl install FAKE-UDID' "$log"
    assert_count 2 'simctl launch --console-pty FAKE-UDID' "$log"
    assert_file_contains "$result_marker_success" "$state/console.Debug.log"
    assert_file_contains "$result_marker_success" "$state/console.Release.log"
)

test_ios_failure_and_cleanup_failure() (
    set -euo pipefail
    local state="$test_root/ios-failure"
    local log="$state/xcrun.log"
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    : >"$log"

    export PATH="$fake_bin:$PATH"
    export FAKE_IOS_STATE_DIR="$state"
    export FAKE_IOS_LOG="$log"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=3
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=3
    export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=3
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3
    export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=3
    export FAKE_IOS_FAIL_LABEL=Release

    set +e
    bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" Release "$state/Release/ide.app"
    launch_status=$?
    set -e
    [[ $launch_status -ne 0 ]]
    assert_count 1 'simctl boot FAKE-UDID' "$log"
    assert_count 1 'simctl shutdown FAKE-UDID' "$log"
    assert_file_contains 'BUSTER_IOS_RESULT: FAILURE' "$state/console.Release.log"

    : >"$log"
    unset FAKE_IOS_FAIL_LABEL
    export FAKE_IOS_NO_MARKER=1
    set +e
    bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" Release "$state/Release/ide.app" 2>"$state/early.err"
    early_status=$?
    set -e
    [[ $early_status -ne 0 ]]
    assert_file_contains 'early app/console exit' "$state/early.err"
    assert_file_contains 'unified log' "$state/early.err"
    assert_count 1 'simctl shutdown FAKE-UDID' "$log"

    : >"$log"
    rm -f "$state/shutdown"
    unset FAKE_IOS_NO_MARKER
    unset FAKE_IOS_FAIL_LABEL
    export FAKE_IOS_SHUTDOWN_STATUS=9
    set +e
    bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" Release "$state/Release/ide.app"
    shutdown_status=$?
    set -e
    [[ $shutdown_status -ne 0 ]]
    assert_count 1 'simctl shutdown FAKE-UDID' "$log"
)

test_ios_true_timeout_after_early_launcher_exit() (
    set -euo pipefail
    local state="$test_root/ios-timeout"
    local log="$state/xcrun.log"
    local error_log="$state/timeout.err"
    mkdir -p "$state/Debug/ide.app"
    : >"$log"

    export PATH="$fake_bin:$PATH"
    export FAKE_IOS_STATE_DIR="$state"
    export FAKE_IOS_LOG="$log"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=3
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=3
    export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=3
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=3
    export FAKE_IOS_NO_MARKER=1
    export FAKE_IOS_APP_ALIVE=1

    set +e
    bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" 2>"$error_log"
    timeout_status=$?
    set -e
    [[ $timeout_status -ne 0 ]]
    assert_file_contains 'real launch timeout' "$error_log"
    assert_file_contains 'attachment returned early' "$error_log"
    assert_count 1 'simctl shutdown FAKE-UDID' "$log"
)

result_marker_success='BUSTER_IOS_RESULT: SUCCESS'
test_mobile_workflow_uses_batch_invocations
test_android_success_and_snapshots
test_android_timeout_and_cleanup_fallback
test_ios_batch_and_cleanup
test_ios_failure_and_cleanup_failure
test_ios_true_timeout_after_early_launcher_exit

# Attached launchers must not survive a result marker, timeout, or interruption.
/bin/bash "$repo_root/tests/ios_launch_monitor_test.sh"

echo "mobile CI script tests passed"
