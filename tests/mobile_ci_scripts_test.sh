#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# Exercise the real APK dependency graph before introducing fake CMake/Ninja.
python3 "$repo_root/tests/android_apk_assets_test.py"
# Exercise the iOS bundle graph without requiring an Apple SDK or simulator.
python3 "$repo_root/tests/ios_bundle_assets_test.py"

fake_tool="$repo_root/tests/mobile_ci_fake_tool.sh"
if [[ -n ${BUSTER_MOBILE_TEST_EVIDENCE_DIR:-} ]]; then
    mkdir -p "$BUSTER_MOBILE_TEST_EVIDENCE_DIR"
    test_root=$(mktemp -d "$BUSTER_MOBILE_TEST_EVIDENCE_DIR/cases.XXXXXX")
else
    test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-mobile-ci.XXXXXX")
fi

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -z ${BUSTER_MOBILE_TEST_EVIDENCE_DIR:-} ]]; then
        rm -rf "$test_root"
    else
        echo "Mobile lifecycle evidence: $test_root"
    fi
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

assert_android_owned_process_stopped() {
    local pid=$1
    local process_state

    if ! kill -0 "$pid" >/dev/null 2>&1; then
        return 0
    fi

    # Keep the assertion aligned with emulator_pid_is_running: kill -0 also
    # succeeds for a terminated child awaiting reaping, but an unavailable or
    # inconclusive ps result must stay fail-closed and count as still live.
    if process_state=$(ps -o stat= -p "$pid" 2>/dev/null); then
        process_state=${process_state//[[:space:]]/}
        if [[ $process_state == Z* || $process_state == X* ]]; then
            return 0
        fi
    fi

    echo "assertion failed: lifecycle stop left the owned emulator PID $pid alive" >&2
    return 1
}

test_android_owned_process_stop_assertion() (
    set -euo pipefail
    local state="$test_root/android-owned-process-stop-assertion"
    local live_pid
    mkdir -p "$state/bin"

    python3 -c 'import signal; signal.pause()' &
    live_pid=$!
    cleanup_live_process() {
        kill "$live_pid" >/dev/null 2>&1 || true
        wait "$live_pid" 2>/dev/null || true
    }
    trap cleanup_live_process EXIT

    if assert_android_owned_process_stopped "$live_pid" 2>"$state/live.err"; then
        echo "assertion failed: a genuinely live owned process was accepted as stopped" >&2
        exit 1
    fi
    assert_file_contains "owned emulator PID $live_pid alive" "$state/live.err"

    cat >"$state/bin/ps" <<'EOF'
#!/usr/bin/env bash
case ${FAKE_PROCESS_STATE:?FAKE_PROCESS_STATE is required} in
    zombie) printf 'Z\n' ;;
    exited) printf 'X\n' ;;
    unavailable) exit 1 ;;
    *) exit 2 ;;
esac
EOF
    chmod +x "$state/bin/ps"

    (
        export PATH="$state/bin:$PATH" FAKE_PROCESS_STATE=zombie
        assert_android_owned_process_stopped "$live_pid"
    )
    (
        export PATH="$state/bin:$PATH" FAKE_PROCESS_STATE=exited
        assert_android_owned_process_stopped "$live_pid"
    )
    if (
        export PATH="$state/bin:$PATH" FAKE_PROCESS_STATE=unavailable
        assert_android_owned_process_stopped "$live_pid"
    ) 2>"$state/unavailable.err"; then
        echo "assertion failed: an unavailable ps result was accepted as stopped" >&2
        exit 1
    fi
    assert_file_contains "owned emulator PID $live_pid alive" "$state/unavailable.err"
)

test_mobile_workflow_uses_batch_invocations() (
    set -euo pipefail
    local workflow="$repo_root/.github/workflows/ci.yml"
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
    assert_android_owned_process_stopped "$owned_pid"

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
    assert_android_owned_process_stopped "$owned_pid"
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

test_ios_boot_recovery() (
    set -euo pipefail
    local state="$test_root/ios-boot-recovery"
    local log="$state/xcrun.log"
    local status
    local first_udid=00000000-0000-0000-0000-000000000001
    local replacement_udid=00000000-0000-0000-0000-000000000002
    mkdir -p "$state/Debug/ide.app"
    : >"$log"

    export PATH="$fake_bin:$PATH"
    export FAKE_IOS_STATE_DIR="$state"
    export FAKE_IOS_LOG="$log"
    export FAKE_IOS_RUNTIME_AVAILABLE=1
    export FAKE_IOS_BOOTSTATUS_MODE=timeout-first-device
    export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60
    export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1
    export BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
    export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
    export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=2
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    export GITHUB_ACTIONS=true RUNNER_ENVIRONMENT=github-hosted RUNNER_OS=macOS RUNNER_ARCH=ARM64 BUSTER_IOS_ARCH=arm64
    unset BUSTER_IOS_SIMULATOR_UDID

    set +e
    bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" >"$state/run.log" 2>&1
    status=$?
    set -e
    [[ $status -eq 0 ]]
    assert_count 2 'simctl create buster-ci' "$log"
    assert_count 1 "simctl boot $first_udid" "$log"
    assert_count 1 "simctl boot $replacement_udid" "$log"
    assert_count 2 "simctl bootstatus $first_udid -b" "$log"
    assert_count 1 "simctl bootstatus $replacement_udid -b" "$log"
    assert_count 1 "simctl shutdown $first_udid" "$log"
    assert_count 1 "simctl shutdown $replacement_udid" "$log"
    assert_count 1 "simctl delete $first_udid" "$log"
    if grep -qF "simctl install $first_udid" "$log" || grep -qF "simctl launch --console-pty $first_udid" "$log"; then
        echo "old simulator identity was used after recovery" >&2
        exit 1
    fi
    assert_file_contains 'readiness diagnostic' "$state/console.log.boot.attempt-1.bootstatus.log"
    assert_file_contains "SIMULATOR_UDID=$first_udid" "$state/console.log.boot.attempt-1.bootstatus.lifecycle-context.log"
    assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=recovered-infrastructure-failure' "$state/run.log"
    echo "iOS boot recovery passed: one replacement, preserved diagnostics, new identity propagated"
)

test_ios_boot_continuation() (
    set -euo pipefail
    local state="$test_root/ios-boot-continuation"
    local log="$state/xcrun.log"
    local first_udid=00000000-0000-0000-0000-000000000001
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    : >"$log"
    export PATH="$fake_bin:$PATH" FAKE_IOS_STATE_DIR="$state" FAKE_IOS_LOG="$log"
    export FAKE_IOS_RUNTIME_AVAILABLE=1 FAKE_IOS_BOOTSTATUS_MODE=timeout-first
    export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60 BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1 BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
    export BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
    export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2 BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
    export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=2 BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    export GITHUB_ACTIONS=true RUNNER_ENVIRONMENT=github-hosted RUNNER_OS=macOS RUNNER_ARCH=ARM64 BUSTER_IOS_ARCH=arm64
    unset BUSTER_IOS_SIMULATOR_UDID
    bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" \
        Release "$state/Release/ide.app" >"$state/run.log" 2>&1
    assert_count 1 'simctl create buster-ci' "$log"
    assert_count 2 "simctl bootstatus $first_udid -b" "$log"
    assert_count 1 "simctl shutdown $first_udid" "$log"
    if grep -qF "simctl delete $first_udid" "$log"; then
        echo 'same-device continuation unexpectedly replaced its simulator' >&2
        exit 1
    fi
    assert_file_contains 'BUSTER_IOS_BOOT_CONTINUATION attempt=1' "$state/run.log"
    assert_file_contains 'outcome=success-after-continuation' "$state/run.log"
    assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=continued-original-boot-success' "$state/run.log"
    assert_file_contains "SIMULATOR_UDID=$first_udid" "$state/console.log.boot.attempt-1.bootstatus.lifecycle-context.log"
    assert_file_contains 'SIMULATOR_RUNTIME=com.apple.CoreSimulator.SimRuntime.iOS-26-5' \
        "$state/console.log.boot.attempt-1.bootstatus.lifecycle-context.log"
    assert_file_contains 'BUSTER_IOS_RESULT: SUCCESS' "$state/console.Debug.log"
    assert_file_contains 'BUSTER_IOS_RESULT: SUCCESS' "$state/console.Release.log"
    echo 'iOS boot continuation passed: same device reaches both application configurations'
)

test_ios_incomplete_capture_receipt() (
    set -euo pipefail
    local state="$test_root/ios-incomplete-capture"
    local mode row log status
    mkdir -p "$state/bin"
    cat >"$state/bin/python3" <<'PY_CAPTURE'
#!/usr/bin/env bash
if [[ ${1:-} == -c && ${2:-} == *'remaining = 65536'* ]]; then
    for path in "$@"; do :; done
    if [[ $path == *.bootstatus.log ]]; then
        : >"$path"
        if [[ ${BUSTER_CAPTURE_TEST_MODE:-} == empty ]]; then
            : >"${path}.capture-status.log"
            cat >/dev/null
        else
            sleep 60
        fi
        exit 0
    fi
fi
exec "$BUSTER_REAL_PYTHON" "$@"
PY_CAPTURE
    chmod +x "$state/bin/python3"
    export BUSTER_REAL_PYTHON="$(command -v python3)"
    export PATH="$state/bin:$fake_bin:$PATH"
    export FAKE_IOS_RUNTIME_AVAILABLE=1 FAKE_IOS_BOOTSTATUS_MODE=always-timeout
    export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60 BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1 BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2 BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2 BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=2
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    for mode in empty timeout; do
        row="$state/$mode"
        mkdir -p "$row/Debug/ide.app"
        log="$row/xcrun.log"
        : >"$log"
        export FAKE_IOS_STATE_DIR="$row" FAKE_IOS_LOG="$log"
        export BUSTER_IOS_CONSOLE_LOG="$row/console.log" BUSTER_CAPTURE_TEST_MODE="$mode"
        set +e
        bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$row/Debug/ide.app" >"$row/run.log" 2>&1
        status=$?
        set -e
        [[ $status -eq 124 ]]
        if [[ $mode == empty ]]; then
            assert_file_contains 'capture_status=0' "$row/console.log.boot.attempt-1.bootstatus.status.log"
        else
            assert_file_contains 'capture_status=124' "$row/console.log.boot.attempt-1.bootstatus.status.log"
        fi
        assert_file_contains 'capture_receipt=incomplete' "$row/console.log.boot.attempt-1.bootstatus.status.log"
        assert_file_contains 'BUSTER_IOS_CAPTURE incomplete=1 reason=missing-or-empty-receipt' \
            "$row/console.log.boot.attempt-1.bootstatus.status.log"
        assert_count 1 'simctl bootstatus' "$log"
        if grep -qF 'simctl install' "$log"; then
            echo 'incomplete readiness evidence reached application installation' >&2
            exit 1
        fi
    done
    echo 'iOS incomplete capture receipts passed: empty and helper timeout remain explicit'
)

test_ios_boot_recovery_controls() (
    set -euo pipefail
    run_case() {
        local case_name=$1 mode=$2 expected=$3
        local state="$test_root/ios-boot-recovery-$case_name"
        local log="$state/xcrun.log"
        local status
        local first_udid=00000000-0000-0000-0000-000000000001
        local replacement_udid=00000000-0000-0000-0000-000000000002
        mkdir -p "$state/Debug/ide.app"
        : >"$log"
        export PATH="$fake_bin:$PATH"
        export FAKE_IOS_STATE_DIR="$state" FAKE_IOS_LOG="$log"
        export FAKE_IOS_RUNTIME_AVAILABLE=1 FAKE_IOS_BOOTSTATUS_MODE="$mode"
        export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60
        export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
        export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1 BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
        export BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
        export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2 BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
        export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=2 BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
        export GITHUB_ACTIONS=true RUNNER_ENVIRONMENT=github-hosted RUNNER_OS=macOS RUNNER_ARCH=ARM64 BUSTER_IOS_ARCH=arm64
        unset BUSTER_IOS_SIMULATOR_UDID FAKE_IOS_SHUTDOWN_STATUS FAKE_IOS_DELETE_STATUS
        unset FAKE_IOS_CREATE_STATUS FAKE_IOS_CREATE_FAIL_AFTER_FIRST FAKE_IOS_FAIL_LABEL FAKE_IOS_NO_MARKER FAKE_IOS_APP_ALIVE
        unset FAKE_IOS_CREATE_INVALID_OUTPUT
        unset FAKE_IOS_SHUTDOWN_SLEEP_SECONDS FAKE_IOS_DELETE_SLEEP_SECONDS FAKE_IOS_CREATE_SLEEP_SECONDS
        if [[ $case_name == shutdown-reject ]]; then
            export FAKE_IOS_SHUTDOWN_STATUS=9
        elif [[ $case_name == shutdown-timeout ]]; then
            export FAKE_IOS_SHUTDOWN_SLEEP_SECONDS=60
        elif [[ $case_name == delete-reject ]]; then
            export FAKE_IOS_DELETE_STATUS=9
        elif [[ $case_name == delete-timeout ]]; then
            export FAKE_IOS_DELETE_SLEEP_SECONDS=60
        elif [[ $case_name == create-reject ]]; then
            export FAKE_IOS_CREATE_STATUS=9 FAKE_IOS_CREATE_FAIL_AFTER_FIRST=1
        elif [[ $case_name == create-timeout ]]; then
            export FAKE_IOS_CREATE_SLEEP_SECONDS=60 FAKE_IOS_CREATE_FAIL_AFTER_FIRST=1
        elif [[ $case_name == create-invalid ]]; then
            export FAKE_IOS_CREATE_INVALID_OUTPUT=1
        elif [[ $case_name == test-failure || $case_name == first-test-failure ]]; then
            export FAKE_IOS_FAIL_LABEL=Debug
        elif [[ $case_name == missing-marker || $case_name == first-missing-marker ]]; then
            export FAKE_IOS_NO_MARKER=1 FAKE_IOS_APP_ALIVE=1
        fi
        set +e
        bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" >"$state/run.log" 2>&1
        status=$?
        set -e
        [[ $status -eq $expected ]]
        case "$case_name" in
            first-boot-success)
                assert_count 1 "simctl boot $first_udid" "$log"
                assert_count 1 "simctl bootstatus $first_udid -b" "$log"
                assert_count 1 "simctl shutdown $first_udid" "$log"
                if grep -qF "simctl delete $first_udid" "$log" \
                    || grep -qF "simctl bootstatus $replacement_udid -b" "$log" \
                    || [[ $(grep -cF 'simctl create buster-ci' "$log" 2>/dev/null || true) -ne 1 ]]; then
                    echo "first-boot-success unexpectedly ran recovery commands" >&2
                    exit 1
                fi
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=first-attempt-success' "$state/run.log"
                ;;
            both-timeout)
                assert_count 3 'simctl bootstatus' "$log"
                assert_count 2 'simctl create buster-ci' "$log"
                assert_count 1 "simctl delete $first_udid" "$log"
                assert_file_contains 'readiness diagnostic' "$state/console.log.boot.attempt-1.bootstatus.log"
                assert_file_contains 'readiness diagnostic' "$state/console.log.boot.attempt-2.bootstatus.log"
                assert_file_contains "SIMULATOR_UDID=$replacement_udid" "$state/console.log.boot.attempt-2.bootstatus.lifecycle-context.log"
                assert_file_contains 'capture_receipt=complete' "$state/console.log.boot.attempt-2.bootstatus.status.log"
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure' "$state/run.log"
                if grep -qF 'simctl install' "$log" || grep -qF 'simctl launch --console-pty' "$log"; then
                    echo "both-timeout case reached application phases" >&2
                    exit 1
                fi
                ;;
            shutdown-reject|shutdown-timeout|delete-reject|delete-timeout|create-reject|create-timeout)
                assert_count 2 "simctl bootstatus $first_udid -b" "$log"
                if grep -qF "simctl bootstatus $replacement_udid -b" "$log"; then
                    echo "$case_name unexpectedly retried readiness" >&2
                    exit 1
                fi
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure' "$state/run.log"
                ;;
            create-invalid)
                assert_count 2 "simctl bootstatus $first_udid -b" "$log"
                if grep -qF "simctl shutdown create rejected diagnostic" "$log" \
                    || grep -qF "simctl bootstatus $replacement_udid -b" "$log"; then
                    echo "invalid create output was treated as an owned replacement" >&2
                    exit 1
                fi
                assert_file_contains 'replacement simulator creation did not return a valid device identity' "$state/run.log"
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure' "$state/run.log"
                ;;
            numeric-124)
                assert_count 1 "simctl bootstatus $first_udid -b" "$log"
                create_count=$(grep -cF 'simctl create buster-ci' "$log" 2>/dev/null || true)
                if grep -qF "simctl delete $first_udid" "$log" || [[ $create_count -gt 1 ]]; then
                    echo "numeric timeout-like status incorrectly triggered recovery" >&2
                    exit 1
                fi
                assert_file_contains 'readiness_outcome=command-failure' "$state/run.log"
                ;;
            continue-native-124)
                assert_count 2 "simctl bootstatus $first_udid -b" "$log"
                assert_count 1 'simctl create buster-ci' "$log"
                assert_file_contains 'phase=bootstatus-continue label=1 outcome=command-failure status=124 native_status=124' \
                    "$state/console.log.boot.attempt-1.bootstatus-continue.status.log"
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure' "$state/run.log"
                ;;
            first-test-failure|first-missing-marker)
                assert_count 1 "simctl bootstatus $first_udid -b" "$log"
                if grep -qF "simctl delete $first_udid" "$log" || grep -qF "simctl bootstatus $replacement_udid -b" "$log"; then
                    echo "$case_name incorrectly triggered boot recovery" >&2
                    exit 1
                fi
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=first-attempt-boot-success-but-test-failure' "$state/run.log"
                ;;
            test-failure|missing-marker)
                assert_count 3 'simctl bootstatus' "$log"
                assert_count 1 "simctl delete $first_udid" "$log"
                assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=recovered-boot-but-test-failure' "$state/run.log"
                if ! grep -qF "simctl install $replacement_udid" "$log"; then
                    echo "$case_name did not install the replacement simulator" >&2
                    exit 1
                fi
                ;;
        esac
        echo "iOS boot recovery control passed: $case_name"
    }

    run_case first-boot-success success 0
    run_case both-timeout always-timeout 1
    run_case shutdown-reject timeout-first-device 1
    run_case shutdown-timeout timeout-first-device 1
    run_case delete-reject timeout-first-device 1
    run_case delete-timeout timeout-first-device 1
    run_case create-reject timeout-first-device 1
    run_case create-timeout timeout-first-device 1
    run_case create-invalid timeout-first-device 1
    run_case numeric-124 numeric-124 124
    run_case continue-native-124 continue-native-124 124
    run_case first-test-failure success 1
    run_case first-missing-marker success 1
    run_case test-failure timeout-first-device 1
    run_case missing-marker timeout-first-device 1
)

test_ios_boot_recovery_policy() (
    set -euo pipefail
    run_policy_case() {
        local case_name=$1
        local state="$test_root/ios-boot-policy-$case_name"
        local log="$state/xcrun.log"
        local status
        mkdir -p "$state/Debug/ide.app"
        : >"$log"
        export PATH="$fake_bin:$PATH"
        export FAKE_IOS_STATE_DIR="$state" FAKE_IOS_LOG="$log"
        export FAKE_IOS_RUNTIME_AVAILABLE=1 FAKE_IOS_BOOTSTATUS_MODE=timeout-first
        export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60
        export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
        export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1 BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
        export BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
        export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2 BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
        export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=2 BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
        export GITHUB_ACTIONS=true RUNNER_ENVIRONMENT=github-hosted RUNNER_OS=macOS RUNNER_ARCH=ARM64 BUSTER_IOS_ARCH=arm64
        unset BUSTER_IOS_SIMULATOR_UDID FAKE_IOS_BORROWED_DEVICE
        case "$case_name" in
            explicit)
                export BUSTER_IOS_SIMULATOR_UDID=EXPLICIT-UDID
                ;;
            local)
                unset GITHUB_ACTIONS RUNNER_ENVIRONMENT RUNNER_OS RUNNER_ARCH
                ;;
            self-hosted)
                export RUNNER_ENVIRONMENT=self-hosted
                ;;
            wrong-os)
                export RUNNER_OS=Linux
                ;;
            wrong-arch)
                export RUNNER_ARCH=X64 BUSTER_IOS_ARCH=x86_64
                ;;
            borrowed)
                export FAKE_IOS_BORROWED_DEVICE=1
                ;;
        esac
        set +e
        bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" >"$state/run.log" 2>&1
        status=$?
        set -e
        [[ $status -ne 0 ]]
        assert_count 1 'simctl bootstatus' "$log"
        if grep -vF 'simctl delete unavailable' "$log" | grep -qF 'simctl delete '; then
            echo "$case_name path deleted a non-recoverable simulator" >&2
            exit 1
        fi
        create_count=$(grep -cF 'simctl create buster-ci' "$log" 2>/dev/null || true)
        case "$case_name" in
            explicit|borrowed)
                [[ $create_count -eq 0 ]]
                ;;
            *)
                [[ $create_count -eq 1 ]]
                ;;
        esac
        assert_file_contains 'BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure' "$state/run.log"
        echo "iOS boot recovery policy passed: $case_name"
    }

    run_policy_case explicit
    run_policy_case local
    run_policy_case self-hosted
    run_policy_case wrong-os
    run_policy_case wrong-arch
    run_policy_case borrowed
)

test_ios_boot_recovery_cancellation() (
    set -euo pipefail
    run_cancel_case() {
        local case_name=$1 wait_for=$2
        local state="$test_root/ios-boot-cancel-$case_name"
        local log="$state/xcrun.log"
        local pid status deadline
        local first_udid=00000000-0000-0000-0000-000000000001
        local replacement_udid=00000000-0000-0000-0000-000000000002
        mkdir -p "$state/Debug/ide.app"
        : >"$log"
        export PATH="$fake_bin:$PATH"
        export FAKE_IOS_STATE_DIR="$state" FAKE_IOS_LOG="$log"
        export FAKE_IOS_RUNTIME_AVAILABLE=1 FAKE_IOS_BOOTSTATUS_MODE=timeout-first-device
        export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60
        export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
        export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1 BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
        export BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
        export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2 BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
        export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=1 BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
        export GITHUB_ACTIONS=true RUNNER_ENVIRONMENT=github-hosted RUNNER_OS=macOS RUNNER_ARCH=ARM64 BUSTER_IOS_ARCH=arm64
        unset BUSTER_IOS_SIMULATOR_UDID FAKE_IOS_SHUTDOWN_SLEEP_SECONDS
        if [[ $case_name == recovery ]]; then
            export FAKE_IOS_SHUTDOWN_SLEEP_SECONDS=60
        fi
        bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" >"$state/run.log" 2>&1 &
        pid=$!
        deadline=$((SECONDS + 10))
        while ! grep -qF "$wait_for" "$log" 2>/dev/null; do
            if (( SECONDS >= deadline )); then
                kill -TERM "$pid" 2>/dev/null || true
                wait "$pid" 2>/dev/null || true
                echo "$case_name did not reach its cancellation point" >&2
                exit 1
            fi
            sleep 0.1
        done
        kill -TERM "$pid" 2>/dev/null || true
        set +e
        wait "$pid"
        status=$?
        set -e
        [[ $status -eq 143 ]]
        create_count=$(grep -cF 'simctl create buster-ci' "$log" 2>/dev/null || true)
        if grep -qF "simctl bootstatus $replacement_udid -b" "$log" || [[ $create_count -gt 1 ]]; then
            echo "$case_name cancellation initiated a replacement boot" >&2
            exit 1
        fi
        if grep -qF "simctl delete $first_udid" "$log"; then
            echo "$case_name cancellation deleted the original simulator" >&2
            exit 1
        fi
        echo "iOS boot recovery cancellation passed: $case_name"
    }

    run_cancel_case first-attempt 'simctl bootstatus 00000000-0000-0000-0000-000000000001 -b'
    run_cancel_case recovery 'simctl shutdown 00000000-0000-0000-0000-000000000001'

    run_cancel_after_replacement_phase() {
        local case_name=$1 mode=$2 wait_for=$3
        local state="$test_root/ios-boot-cancel-$case_name"
        local log="$state/xcrun.log"
        local pid status deadline
        local first_udid=00000000-0000-0000-0000-000000000001
        local replacement_udid=00000000-0000-0000-0000-000000000002
        mkdir -p "$state/Debug/ide.app"
        : >"$log"
        export PATH="$fake_bin:$PATH"
        export FAKE_IOS_STATE_DIR="$state" FAKE_IOS_LOG="$log"
        export FAKE_IOS_RUNTIME_AVAILABLE=1 FAKE_IOS_BOOTSTATUS_MODE="$mode"
        export FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS=60
        export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
        export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=1 BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
        export BUSTER_IOS_BOOT_CONTINUATION_SECONDS=1
        export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=2 BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
        export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=1 BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
        export GITHUB_ACTIONS=true RUNNER_ENVIRONMENT=github-hosted RUNNER_OS=macOS RUNNER_ARCH=ARM64 BUSTER_IOS_ARCH=arm64
        unset BUSTER_IOS_SIMULATOR_UDID FAKE_IOS_SHUTDOWN_SLEEP_SECONDS
        if [[ $case_name == recovery-create ]]; then
            export FAKE_IOS_CREATE_SLEEP_SECONDS=60 FAKE_IOS_CREATE_EMIT_BEFORE_HANG=1
            export FAKE_IOS_CREATE_FAIL_AFTER_FIRST=1
        else
            unset FAKE_IOS_CREATE_SLEEP_SECONDS FAKE_IOS_CREATE_EMIT_BEFORE_HANG FAKE_IOS_CREATE_FAIL_AFTER_FIRST
        fi
        bash "$repo_root/ios/launch_simulator.sh" --batch Debug "$state/Debug/ide.app" >"$state/run.log" 2>&1 &
        pid=$!
        deadline=$((SECONDS + 10))
        while ! grep -qF "$wait_for" "$log" 2>/dev/null; do
            if (( SECONDS >= deadline )); then
                kill -TERM "$pid" 2>/dev/null || true
                wait "$pid" 2>/dev/null || true
                echo "$case_name did not reach its cancellation point" >&2
                exit 1
            fi
            sleep 0.1
        done
        if [[ $case_name == recovery-create ]]; then
            deadline=$((SECONDS + 10))
            while ! grep -qF "$replacement_udid" "$state/console.log.boot-recovery.recovery-create.log" 2>/dev/null; do
                if (( SECONDS >= deadline )); then
                    kill -TERM "$pid" 2>/dev/null || true
                    wait "$pid" 2>/dev/null || true
                    echo "recovery-create did not retain the emitted replacement identity" >&2
                    exit 1
                fi
                sleep 0.1
            done
        fi
        kill -TERM "$pid" 2>/dev/null || true
        set +e
        wait "$pid"
        status=$?
        set -e
        [[ $status -eq 143 ]]
        assert_count 1 "simctl shutdown $first_udid" "$log"
        assert_count 1 "simctl delete $first_udid" "$log"
        assert_count 1 "simctl shutdown $replacement_udid" "$log"
        assert_file_contains 'prior_status=143' "$state/console.log.shutdown.status.log"
        assert_file_contains "simulator_udid=$replacement_udid" "$state/console.log.shutdown.status.log"
        if [[ $case_name == recovery-create ]]; then
            if grep -qF "simctl bootstatus $replacement_udid -b" "$log"; then
                echo "recovery-create cancellation initiated replacement readiness" >&2
                exit 1
            fi
        else
            assert_count 1 "simctl bootstatus $replacement_udid -b" "$log"
            if grep -qF 'simctl bootstatus 00000000-0000-0000-0000-000000000003 -b' "$log"; then
                echo "replacement-readiness cancellation retried beyond the permitted attempt" >&2
                exit 1
            fi
        fi
        echo "iOS boot recovery cancellation passed: $case_name (replacement cleanup identity preserved)"
    }

    run_cancel_after_replacement_phase recovery-create timeout-first-device \
        'simctl create buster-ci com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro'
    run_cancel_after_replacement_phase replacement-readiness always-timeout \
        'simctl bootstatus 00000000-0000-0000-0000-000000000002 -b'
)

test_ios_native_tools_ready() (
    set -euo pipefail
    local state="$test_root/ios-native-readiness"
    local timeout_bin=timeout status
    mkdir -p "$state"
    if ! command -v "$timeout_bin" >/dev/null 2>&1; then timeout_bin=gtimeout; fi
    # The fake boot never starts CoreSimulator. Give the real service its own
    # startup gate before timing a native invalid-device rejection; keep the
    # launcher's 30-second shutdown deadline and required native exit intact.
    echo "Waiting up to 180s for native CoreSimulator runtime discovery"
    if "$timeout_bin" --kill-after=5s 180s /usr/bin/xcrun simctl list runtimes >"$state/runtimes.log" 2>&1; then
        status=0
    else
        status=$?
    fi
    cat "$state/runtimes.log"
    printf 'BUSTER_IOS_NATIVE_READINESS status=%s deadline_seconds=180\n' "$status" | tee "$state/status.log"
    if [[ $status -ne 0 ]]; then
        echo "error: native CoreSimulator runtime discovery failed before the lifecycle test" >&2
        exit 1
    fi
    assert_file_contains 'iOS ' "$state/runtimes.log"
)

test_ios_lifecycle_evidence() (
    set -euo pipefail
    local case_name=$1
    local state="$test_root/ios-evidence-$case_name"
    local phase=codesign label=Debug status=7 native=7 outcome=command-failure
    local prior_status=1 launch_status started=$SECONDS
    local evidence
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    : >"$state/xcrun.log"
    export PATH="$fake_bin:$PATH"
    export FAKE_IOS_STATE_DIR="$state" FAKE_IOS_LOG="$state/xcrun.log"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=1 BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=1
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=3 BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=3
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3 BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    export FAKE_IOS_CODESIGN_FAIL_LABEL=Debug FAKE_IOS_CODESIGN_STATUS=7
    case "$case_name" in
        codesign-exit) ;;
        codesign-native-124) status=124; native=124; export FAKE_IOS_CODESIGN_STATUS=124 ;;
        codesign-timeout)
            status=124; native=unavailable; outcome=timeout
            export FAKE_IOS_CODESIGN_SLEEP_SECONDS=60 ;;
        codesign-large-output) export FAKE_IOS_LARGE_OUTPUT=1 ;;
        shutdown-exit|shutdown-timeout|app-and-shutdown|native-macos)
            phase=shutdown; label=batch; status=9; native=9; prior_status=0
            export FAKE_IOS_CODESIGN_STATUS=0 FAKE_IOS_SHUTDOWN_STATUS=9
            if [[ $case_name == shutdown-timeout ]]; then
                status=124; native=unavailable; outcome=timeout
                export FAKE_IOS_SHUTDOWN_SLEEP_SECONDS=60
            elif [[ $case_name == app-and-shutdown ]]; then
                prior_status=1
                export FAKE_IOS_FAIL_LABEL=Debug
            elif [[ $case_name == native-macos ]]; then
                prior_status=1
                export FAKE_IOS_REAL_CODESIGN=1 FAKE_IOS_REAL_SHUTDOWN=1 FAKE_IOS_REAL_CONTEXT=1
                export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=60 BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=30
                export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=10
            fi
            ;;
    esac
    if /bin/bash "$repo_root/ios/launch_simulator.sh" --batch \
        Debug "$state/Debug/ide.app" Release "$state/Release/ide.app" >"$state/run.log" 2>&1; then
        launch_status=0
    else
        launch_status=$?
    fi
    [[ $launch_status -eq 1 ]]
    if [[ $case_name != native-macos && $((SECONDS - started)) -ge 30 ]]; then
        echo "assertion failed: lifecycle command escaped its short test deadline" >&2
        exit 1
    fi
    evidence="$state/console.Debug.log.codesign"
    if [[ $phase == shutdown ]]; then evidence="$state/console.log.shutdown"; fi
    assert_file_contains "phase=$phase label=$label outcome=$outcome" "$evidence.status.log"
    if [[ $case_name != native-macos ]]; then
        assert_file_contains "status=$status native_status=$native capture_status=0" "$evidence.status.log"
        assert_file_contains 'deadline_seconds=1 output_limit_bytes=65536' "$evidence.status.log"
        assert_file_contains "fake $phase stdout:" "$evidence.log"
        assert_file_contains "fake $phase stderr:" "$evidence.log"
    else
        assert_file_contains 'phase=codesign label=Debug outcome=command-failure' "$state/console.Debug.log.codesign.status.log"
        assert_file_contains 'Xcode ' "$state/console.log.lifecycle-context.log"
        assert_file_contains 'iOS ' "$state/console.log.lifecycle-context.log"
        [[ -s $state/console.Debug.log.codesign.log && -s $evidence.log ]]
        if grep -qF 'native_status=unavailable' "$evidence.status.log"; then exit 1; fi
    fi
    assert_file_contains 'command:' "$evidence.status.log"
    assert_file_contains 'elapsed_seconds=' "$evidence.status.log"
    [[ $(wc -c <"$evidence.log") -le 65536 ]]
    if [[ $case_name == codesign-large-output ]]; then
        [[ $(wc -c <"$evidence.log") -eq 65536 ]]
        assert_file_contains 'retained_bytes=65536 truncated=1' "$evidence.status.log"
    else
        assert_file_contains 'truncated=0' "$evidence.status.log"
    fi
    assert_file_contains 'GITHUB_SHA=' "$state/console.log.lifecycle-context.log"
    assert_file_contains "$result_marker_success" "$state/console.Release.log"
    assert_count 1 'simctl boot FAKE-UDID' "$state/xcrun.log"
    assert_count 1 'simctl shutdown FAKE-UDID' "$state/xcrun.log"
    assert_file_contains "prior_status=$prior_status" "$state/console.log.shutdown.status.log"
    assert_file_contains 'result_status=1' "$state/console.log.shutdown.status.log"
    if [[ $case_name == app-and-shutdown ]]; then
        assert_file_contains 'BUSTER_IOS_RESULT: FAILURE' "$state/console.Debug.log"
    fi
    cat "$evidence.status.log"
    echo "iOS lifecycle evidence passed: $case_name"
)

result_marker_success='BUSTER_IOS_RESULT: SUCCESS'
test_android_owned_process_stop_assertion
test_mobile_workflow_uses_batch_invocations
test_android_success_and_snapshots
test_android_timeout_and_cleanup_fallback
test_ios_batch_and_cleanup
test_ios_failure_and_cleanup_failure
test_ios_true_timeout_after_early_launcher_exit
test_ios_boot_recovery
test_ios_boot_continuation
test_ios_incomplete_capture_receipt
test_ios_boot_recovery_controls
test_ios_boot_recovery_policy
test_ios_boot_recovery_cancellation
for lifecycle_case in codesign-exit codesign-native-124 codesign-timeout codesign-large-output \
    shutdown-exit shutdown-timeout app-and-shutdown; do
    test_ios_lifecycle_evidence "$lifecycle_case"
done
if [[ $(uname -s) == Darwin ]]; then
    # Real macOS tools reject an empty app and an invalid device ID. These
    # bounded rejections exercise native evidence without touching any device.
    test_ios_native_tools_ready
    test_ios_lifecycle_evidence native-macos
fi

# Attached launchers must not survive a result marker, timeout, or interruption.
/bin/bash "$repo_root/tests/ios_launch_monitor_test.sh"

echo "mobile CI script tests passed"
