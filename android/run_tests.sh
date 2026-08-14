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
expected_abi=${BUSTER_ANDROID_EXPECTED_ABI:-${BUSTER_ANDROID_ABI:-x86_64}}
android_serial=${BUSTER_ANDROID_SERIAL:-}

for timeout_value in \
    "$timeout_seconds" \
    "$adb_wait_timeout_seconds" \
    "$adb_command_timeout_seconds" \
    "$adb_install_timeout_seconds"; do
    if [[ ! $timeout_value =~ ^[1-9][0-9]*$ ]]; then
        echo "error: Android test timeouts must be positive integers; got '$timeout_value'" >&2
        exit 1
    fi
done

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: timeout command is required for Android test log monitoring" >&2
    exit 1
fi
if [[ ! -f $apk ]]; then
    echo "error: Android APK not found at '$apk'" >&2
    exit 1
fi

adb_serial_args=()
if [[ -n $android_serial ]]; then
    adb_serial_args=(-s "$android_serial")
fi

android_shell_quote() {
    local value=$1
    printf "'%s'" "${value//\'/\'\\\'}"
}

adb_with_timeout() {
    local seconds=$1
    shift
    timeout --kill-after=10s "${seconds}s" "$adb" "${adb_serial_args[@]}" "$@"
}

print_adb_diagnostics() {
    echo "----- adb devices -----" >&2
    adb_with_timeout "$adb_command_timeout_seconds" devices >&2 || true
    echo "----- adb logcat tail -----" >&2
    adb_with_timeout "$adb_command_timeout_seconds" logcat -d -v time -t 200 >&2 || true
}

device_is_ready() {
    local devices_output=$1
    if [[ -n $android_serial ]]; then
        awk -v serial="$android_serial" 'NR > 1 && $1 == serial && $2 == "device" { found = 1 } END { exit found ? 0 : 1 }' <<<"$devices_output"
    else
        awk 'NR > 1 && $2 == "device" { count++ } END { exit count == 1 ? 0 : 1 }' <<<"$devices_output"
    fi
}

wait_for_device_ready() {
    local deadline=$((SECONDS + adb_wait_timeout_seconds))
    local devices_output
    local boot_output

    echo "Waiting up to ${adb_wait_timeout_seconds}s for a booted Android device..."
    while true; do
        if devices_output=$(adb_with_timeout "$adb_command_timeout_seconds" devices 2>&1); then
            if device_is_ready "$devices_output"; then
                if boot_output=$(adb_with_timeout "$adb_command_timeout_seconds" shell getprop sys.boot_completed 2>&1) \
                    && [[ $(printf '%s' "$boot_output" | tr -d '\r') == 1 ]]; then
                    printf '%s\n' "$devices_output"
                    return 0
                fi
            fi
        else
            echo "warning: adb devices probe failed while waiting for the test device" >&2
        fi

        if (( SECONDS >= deadline )); then
            echo "error: no booted Android device became ready within ${adb_wait_timeout_seconds}s" >&2
            print_adb_diagnostics
            return 1
        fi
        sleep 1
    done
}

validate_device_before_install() {
    local devices_output
    local device_abi
    local sdk_level

    # Keep this validation immediately adjacent to install. The ABI used for
    # compilation is fixed x86_64, but the actual device is still checked after
    # it has booted so a stale or wrong emulator cannot receive the APK.
    if ! devices_output=$(adb_with_timeout "$adb_command_timeout_seconds" devices 2>&1) || ! device_is_ready "$devices_output"; then
        echo "error: final Android device validation failed" >&2
        print_adb_diagnostics
        return 1
    fi
    if ! device_abi=$(adb_with_timeout "$adb_command_timeout_seconds" shell getprop ro.product.cpu.abi 2>&1); then
        echo "error: could not read Android device ABI" >&2
        print_adb_diagnostics
        return 1
    fi
    device_abi=$(printf '%s' "$device_abi" | tr -d '\r')
    if [[ -n $expected_abi && $device_abi != "$expected_abi" ]]; then
        echo "error: Android device ABI '$device_abi' does not match expected '$expected_abi'" >&2
        print_adb_diagnostics
        return 1
    fi
    if ! sdk_level=$(adb_with_timeout "$adb_command_timeout_seconds" shell getprop ro.build.version.sdk 2>&1); then
        echo "error: could not read Android device SDK level" >&2
        print_adb_diagnostics
        return 1
    fi
    printf 'Android device validated: abi=%s sdk=%s\n' "$device_abi" "$(printf '%s' "$sdk_level" | tr -d '\r')"
    return 0
}

monitor_reader_pid=
monitor_producer_pid=
monitor_fd=
terminate_monitor_producer() {
    local force=${1:-0}
    local pid=${monitor_producer_pid:-}
    if [[ -z $pid ]] || ! kill -0 "$pid" >/dev/null 2>&1; then
        return 0
    fi

    # GNU timeout puts the adb command in a private process group. Kill that
    # group so adb/logcat cannot outlive the timeout process after a terminal
    # marker, launch failure, interruption, or cleanup.
    if kill -TERM -- -"$pid" >/dev/null 2>&1; then
        if [[ $force == 1 ]]; then
            kill -KILL -- -"$pid" >/dev/null 2>&1 || true
        fi
    else
        kill -TERM "$pid" >/dev/null 2>&1 || true
        if [[ $force == 1 ]]; then
            kill -KILL "$pid" >/dev/null 2>&1 || true
        fi
    fi
}

monitor_logcat_reader() {
    local line
    while IFS= read -r -u "$monitor_fd" line; do
        printf '%s\n' "$line"
        if [[ $line =~ BUSTER_ANDROID_TEST_RESULT:([0-9]+)$ ]]; then
            terminate_monitor_producer
            if [[ ${BASH_REMATCH[1]} == 0 ]]; then
                return 10
            fi
            return 11
        fi
    done
    return 0
}

stop_monitor() {
    if [[ -n ${monitor_reader_pid:-} ]] && kill -0 "$monitor_reader_pid" >/dev/null 2>&1; then
        kill "$monitor_reader_pid" >/dev/null 2>&1 || true
        wait "$monitor_reader_pid" 2>/dev/null || true
    fi
    monitor_reader_pid=

    if [[ -n ${monitor_producer_pid:-} ]]; then
        terminate_monitor_producer 1
        wait "$monitor_producer_pid" 2>/dev/null || true
    fi
    monitor_producer_pid=

    if [[ -n ${monitor_fd:-} ]]; then
        exec {monitor_fd}<&-
        monitor_fd=
    fi
}

cleanup_monitor() {
    local status=$?
    trap - EXIT INT TERM
    stop_monitor
    adb_with_timeout "$adb_command_timeout_seconds" shell am force-stop "$package" >/dev/null 2>&1 || true
    exit "$status"
}
trap cleanup_monitor EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

wait_for_device_ready
validate_device_before_install
adb_with_timeout "$adb_command_timeout_seconds" uninstall "$package" >/dev/null 2>&1 || true
if ! adb_with_timeout "$adb_install_timeout_seconds" install -r "$apk"; then
    echo "adb install -r failed; uninstalling ${package} and retrying" >&2
    adb_with_timeout "$adb_command_timeout_seconds" uninstall "$package" >/dev/null 2>&1 || true
    adb_with_timeout "$adb_install_timeout_seconds" install -r "$apk"
fi
adb_with_timeout "$adb_command_timeout_seconds" shell am force-stop "$package" >/dev/null 2>&1 || true
adb_with_timeout "$adb_command_timeout_seconds" logcat -c

# Keep the log producer and its reader as separately owned processes. A
# background pipeline only exposes its last (reader) process through $!, so the
# timeout/adb producer can survive after the reader sees a terminal marker.
coproc buster_android_logcat {
    exec timeout --kill-after=1s "${timeout_seconds}s" "$adb" "${adb_serial_args[@]}" logcat -v time -s buster:I '*:S'
}
monitor_producer_pid=$buster_android_logcat_PID
exec {monitor_fd}<&"${buster_android_logcat[0]}"
monitor_logcat_reader &
monitor_reader_pid=$!

# NativeActivity does not receive argv from adb, so pass the same test flags used
# by desktop CI through an intent extra. The app turns this back into argv.
# Do not use `am start -W` here: on some emulator builds the wait-for-launch
# shell command can lose its adb connection even though the activity started and
# is still producing the logcat test result we actually care about.
if adb_with_timeout "$adb_command_timeout_seconds" shell "am start -n $(android_shell_quote "$activity") --es buster_args $(android_shell_quote "$test_args")"; then
    start_status=0
else
    start_status=$?
fi
if [[ $start_status -ne 0 ]]; then
    echo "error: am start failed with status ${start_status}" >&2
    stop_monitor
    print_adb_diagnostics
    exit "$start_status"
fi

if wait "$monitor_reader_pid"; then
    monitor_reader_status=0
else
    monitor_reader_status=$?
fi
monitor_reader_pid=

if wait "$monitor_producer_pid" 2>/dev/null; then
    monitor_producer_status=0
else
    monitor_producer_status=$?
fi
monitor_producer_pid=
exec {monitor_fd}<&-
monitor_fd=

if [[ $monitor_reader_status -eq 10 || $monitor_reader_status -eq 11 ]]; then
    status=$monitor_reader_status
elif [[ $monitor_producer_status -ne 0 ]]; then
    status=$monitor_producer_status
else
    echo "error: Android compiler test log monitor ended without a terminal result" >&2
    status=1
fi

adb_with_timeout "$adb_command_timeout_seconds" shell am force-stop "$package" >/dev/null 2>&1 || true

case "$status" in
    10)
        echo "Android compiler tests passed"
        ;;
    11)
        echo "Android compiler tests failed" >&2
        print_adb_diagnostics
        exit 1
        ;;
    124|137)
        echo "Android compiler tests timed out after ${timeout_seconds}s" >&2
        print_adb_diagnostics
        exit 1
        ;;
    *)
        echo "Android compiler test log monitor exited with status ${status}" >&2
        print_adb_diagnostics
        exit "$status"
        ;;
esac
