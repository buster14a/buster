#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 <adb> <apk> <package> <activity> <test-args>" >&2
    exit 2
fi

adb=$1
apk=$2
package=$3
activity=$4
test_args=$5
timeout_seconds=${BUSTER_ANDROID_TEST_TIMEOUT_SECONDS:-60}
adb_wait_timeout_seconds=${BUSTER_ANDROID_ADB_WAIT_TIMEOUT_SECONDS:-60}
adb_command_timeout_seconds=${BUSTER_ANDROID_ADB_COMMAND_TIMEOUT_SECONDS:-30}
adb_install_timeout_seconds=${BUSTER_ANDROID_ADB_INSTALL_TIMEOUT_SECONDS:-60}

android_shell_quote() {
    local value=$1
    printf "'%s'" "${value//\'/\'\\\'\'}"
}

adb_with_timeout() {
    local seconds=$1
    shift
    timeout --kill-after=10s "$seconds" "$adb" "$@"
}

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: timeout command is required for Android test log monitoring" >&2
    exit 1
fi

echo "Waiting up to ${adb_wait_timeout_seconds}s for an Android device..."
if ! timeout "$adb_wait_timeout_seconds" "$adb" wait-for-device; then
    echo "error: no Android device became available within ${adb_wait_timeout_seconds}s" >&2
    adb_with_timeout "$adb_command_timeout_seconds" devices >&2 || true
    exit 1
fi
adb_with_timeout "$adb_command_timeout_seconds" shell getprop ro.build.version.sdk
adb_with_timeout "$adb_command_timeout_seconds" uninstall "$package" >/dev/null 2>&1 || true
if ! adb_with_timeout "$adb_install_timeout_seconds" install -r "$apk"; then
    echo "adb install -r failed; uninstalling ${package} and retrying" >&2
    adb_with_timeout "$adb_command_timeout_seconds" uninstall "$package" >/dev/null 2>&1 || true
    adb_with_timeout "$adb_install_timeout_seconds" install -r "$apk"
fi
adb_with_timeout "$adb_command_timeout_seconds" shell am force-stop "$package" >/dev/null 2>&1 || true
adb_with_timeout "$adb_command_timeout_seconds" logcat -c

set +e
set -o pipefail
timeout "$timeout_seconds" "$adb" logcat -v time -s buster:I '*:S' | while IFS= read -r line; do
    printf '%s\n' "$line"
    case "$line" in
        *BUSTER_ANDROID_TEST_RESULT:0*) exit 10 ;;
        *BUSTER_ANDROID_TEST_RESULT:*) exit 11 ;;
    esac
done &
monitor_pid=$!

# NativeActivity does not receive argv from adb, so pass the same test flags used
# by desktop CI through an intent extra. The app turns this back into argv.
# Do not use `am start -W` here: on some emulator builds the wait-for-launch
# shell command can lose its adb connection even though the activity started and
# is still producing the logcat test result we actually care about.
adb_with_timeout "$adb_command_timeout_seconds" shell "am start -n $(android_shell_quote "$activity") --es buster_args $(android_shell_quote "$test_args")"
start_status=$?
if [[ $start_status -ne 0 ]]; then
    echo "warning: am start exited with status ${start_status}; waiting for Android test result from logcat" >&2
fi

wait "$monitor_pid"
status=$?
set -e

adb_with_timeout "$adb_command_timeout_seconds" shell am force-stop "$package" >/dev/null 2>&1 || true

case "$status" in
    10)
        echo "Android GUI tests passed"
        ;;
    11)
        echo "Android GUI tests failed" >&2
        exit 1
        ;;
    124)
        echo "Android GUI tests timed out after ${timeout_seconds}s" >&2
        if [[ $start_status -ne 0 ]]; then
            echo "am start had exited with status ${start_status}" >&2
        fi
        exit 1
        ;;
    *)
        echo "Android GUI test log monitor exited with status ${status}" >&2
        adb_with_timeout "$adb_command_timeout_seconds" devices >&2 || true
        adb_with_timeout "$adb_command_timeout_seconds" logcat -d -v time -t 200 >&2 || true
        exit "$status"
        ;;
esac
