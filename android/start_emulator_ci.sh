#!/usr/bin/env bash
set -euo pipefail

# Lifecycle modes:
#   (default)       start an emulator and wait for it to finish booting
#   start           prepare and start an emulator, then return immediately
#   wait            wait for the emulator started by `start` (or an existing
#                   device) to finish booting
#   stop            stop the owned emulator, if one was started
#
# The default keeps this script useful as a standalone blocking helper. CI uses
# `start` followed by a later `wait` so CMake/Ninja can build while Android boots.
lifecycle=${1:-start-and-wait}
if [[ $# -gt 1 ]]; then
    echo "usage: $0 [start|wait|stop]" >&2
    exit 2
fi
case "$lifecycle" in
    start|wait|stop|start-and-wait) ;;
    *)
        echo "error: unknown Android emulator lifecycle '$lifecycle'" >&2
        echo "usage: $0 [start|wait|stop]" >&2
        exit 2
        ;;
esac

avd_name=${BUSTER_ANDROID_AVD:-buster-ci}
gpu_mode=${BUSTER_ANDROID_EMULATOR_GPU:-host}
headless=${BUSTER_ANDROID_EMULATOR_HEADLESS:-1}
boot_timeout_seconds=${BUSTER_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS:-180}
create_avd=${BUSTER_ANDROID_CREATE_AVD:-1}
device_profile=${BUSTER_ANDROID_DEVICE:-pixel_6}
system_image=${BUSTER_ANDROID_SYSTEM_IMAGE:-}
command_timeout_seconds=${BUSTER_ANDROID_COMMAND_TIMEOUT_SECONDS:-30}
adb_timeout_seconds=${BUSTER_ANDROID_ADB_TIMEOUT_SECONDS:-$command_timeout_seconds}
tool_timeout_seconds=${BUSTER_ANDROID_TOOL_TIMEOUT_SECONDS:-$command_timeout_seconds}
cleanup_timeout_seconds=${BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS:-$command_timeout_seconds}
avd_create_timeout_seconds=${BUSTER_ANDROID_AVD_CREATE_TIMEOUT_SECONDS:-${BUSTER_ANDROID_CREATE_AVD_TIMEOUT_SECONDS:-120}}
if [[ -z $system_image ]]; then
    system_image="system-images;android-35;google_apis;x86_64"
fi
IFS=';' read -r system_image_prefix system_image_platform system_image_tag system_image_abi <<< "$system_image"
if [[ $system_image_prefix != system-images || -z $system_image_platform || -z $system_image_tag || -z $system_image_abi ]]; then
    echo "error: BUSTER_ANDROID_SYSTEM_IMAGE must look like system-images;android-35;google_apis;x86_64" >&2
    exit 1
fi

android_sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/opt/android-sdk}}
export ANDROID_HOME=$android_sdk
export ANDROID_SDK_ROOT=$android_sdk
android_user_home=${ANDROID_USER_HOME:-${HOME}/.android}
android_avd_home=${ANDROID_AVD_HOME:-${android_user_home}/avd}
export ANDROID_USER_HOME=$android_user_home
export ANDROID_AVD_HOME=$android_avd_home
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: timeout command is required for Android emulator lifecycle management" >&2
    exit 1
fi
if ! command -v adb >/dev/null 2>&1; then
    echo "error: adb was not found; check ANDROID_HOME/ANDROID_SDK_ROOT" >&2
    exit 1
fi
if [[ $lifecycle == start || $lifecycle == start-and-wait ]]; then
    if ! command -v emulator >/dev/null 2>&1; then
        echo "error: emulator was not found; install the Android SDK emulator package" >&2
        exit 1
    fi
fi

for timeout_value in \
    "$boot_timeout_seconds" \
    "$command_timeout_seconds" \
    "$adb_timeout_seconds" \
    "$tool_timeout_seconds" \
    "$cleanup_timeout_seconds" \
    "$avd_create_timeout_seconds"; do
    if [[ ! $timeout_value =~ ^[1-9][0-9]*$ ]]; then
        echo "error: Android command timeouts must be positive integers; got '$timeout_value'" >&2
        exit 1
    fi
done

timed_command() {
    local timeout_seconds=$1
    local description=$2
    local status
    shift 2

    if timeout --kill-after=1s "${timeout_seconds}s" "$@"; then
        return 0
    else
        status=$?
    fi

    if [[ $status -eq 124 || $status -eq 137 ]]; then
        echo "error: $description timed out after ${timeout_seconds}s" >&2
    else
        echo "error: $description failed with exit status $status" >&2
    fi
    return "$status"
}

timed_capture() {
    local output_name=$1
    local timeout_seconds=$2
    local description=$3
    local output
    local status
    shift 3

    if output=$(timeout --kill-after=1s "${timeout_seconds}s" "$@" 2>&1); then
        printf -v "$output_name" '%s' "$output"
        return 0
    else
        status=$?
    fi

    printf -v "$output_name" '%s' "$output"
    if [[ $status -eq 124 || $status -eq 137 ]]; then
        echo "error: $description timed out after ${timeout_seconds}s" >&2
    else
        echo "error: $description failed with exit status $status" >&2
    fi
    if [[ -n $output ]]; then
        printf '%s\n' "$output" >&2
    fi
    return 1
}

optional_timed_command() {
    local timeout_seconds=$1
    local description=$2
    local status
    shift 2

    if timeout --kill-after=1s "${timeout_seconds}s" "$@" >/dev/null 2>&1; then
        return 0
    else
        status=$?
    fi

    if [[ $status -eq 124 || $status -eq 137 ]]; then
        echo "warning: $description timed out after ${timeout_seconds}s; continuing" >&2
    else
        echo "warning: $description failed with exit status $status; continuing" >&2
    fi
    return 0
}

log_dir=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
emulator_log=${BUSTER_ANDROID_EMULATOR_LOG:-${log_dir%/}/buster-android-emulator.log}
started_marker=${BUSTER_ANDROID_EMULATOR_STARTED_MARKER:-${log_dir%/}/buster-android-emulator.started}
mkdir -p "$(dirname "$started_marker")" "$(dirname "$emulator_log")"
mkdir -p "$ANDROID_USER_HOME" "$ANDROID_AVD_HOME"

adb_devices_output=
adb_has_device() {
    if timed_capture adb_devices_output "$adb_timeout_seconds" "adb devices probe" adb devices; then
        if awk 'NR > 1 && $2 == "device" { found = 1 } END { exit found ? 0 : 1 }' <<<"$adb_devices_output"; then
            return 0
        fi
        return 1
    fi
    # A failed/timed-out adb command is different from a successful probe with
    # no connected device. Callers must fail instead of treating an adb outage
    # as a device that is merely still booting.
    return 2
}

print_diagnostics() {
    echo "----- Android emulator log tail -----" >&2
    if [[ -f $emulator_log ]]; then
        tail -n 200 "$emulator_log" >&2 || true
    else
        echo "(emulator log not found: $emulator_log)" >&2
    fi
    echo "----- adb devices -----" >&2
    timeout --kill-after=1s "${adb_timeout_seconds}s" adb devices >&2 || true
    echo "----- adb logcat tail -----" >&2
    timeout --kill-after=1s "${adb_timeout_seconds}s" adb logcat -d -v time -t 200 >&2 || true
}

read_emulator_pid() {
    emulator_pid=
    if [[ ! -f $started_marker ]]; then
        return 1
    fi
    if ! IFS= read -r emulator_pid <"$started_marker"; then
        echo "error: could not read Android emulator PID marker '$started_marker'" >&2
        return 1
    fi
    if [[ ! $emulator_pid =~ ^[1-9][0-9]*$ ]]; then
        echo "error: Android emulator PID marker '$started_marker' is invalid" >&2
        return 1
    fi
    return 0
}

stop_owned_emulator() {
    local status=0
    local pid=
    local fallback_pid=${emulator_pid:-}
    local stop_deadline

    if read_emulator_pid; then
        pid=$emulator_pid
    elif [[ $fallback_pid =~ ^[1-9][0-9]*$ ]]; then
        # Cover the small window between launching the process and publishing
        # the marker when an interrupt arrives.
        pid=$fallback_pid
    fi

    # Ask the emulator through adb first so it can release its device state.
    # The PID fallback handles adb outages and fake/test emulators.
    if [[ -n $pid ]] && kill -0 "$pid" >/dev/null 2>&1; then
        if timed_command "$cleanup_timeout_seconds" "adb emulator shutdown" adb emu kill; then
            :
        else
            echo "warning: adb emulator shutdown failed; terminating owned PID $pid" >&2
            status=1
        fi

        stop_deadline=$((SECONDS + cleanup_timeout_seconds))
        while kill -0 "$pid" >/dev/null 2>&1 && (( SECONDS < stop_deadline )); do
            sleep 1
        done
        if kill -0 "$pid" >/dev/null 2>&1; then
            echo "warning: Android emulator PID $pid did not exit; sending SIGTERM" >&2
            kill "$pid" >/dev/null 2>&1 || true
            sleep 1
        fi
        if kill -0 "$pid" >/dev/null 2>&1; then
            echo "warning: Android emulator PID $pid still exists; sending SIGKILL" >&2
            kill -KILL "$pid" >/dev/null 2>&1 || true
            status=1
        fi
    elif [[ -n $pid ]]; then
        echo "Android emulator PID $pid is no longer running"
    fi

    rm -f "$started_marker"
    return "$status"
}

emulator_pid=
emulator_started=0
cleanup_after_start_failure() {
    local status=$?
    trap - EXIT INT TERM
    if [[ ${emulator_started:-0} == 1 ]]; then
        if ! stop_owned_emulator; then
            echo "warning: failed to clean up owned Android emulator after lifecycle failure" >&2
        fi
    fi
    exit "$status"
}
trap cleanup_after_start_failure EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ $lifecycle == stop ]]; then
    if stop_owned_emulator; then
        exit 0
    else
        echo "warning: Android emulator cleanup reported a failure" >&2
        exit 1
    fi
fi

wait_for_ready() {
    local connect_deadline=$((SECONDS + boot_timeout_seconds))
    local boot_deadline
    local boot_probe_output
    local boot_completed
    local adb_status

    echo "Waiting up to ${boot_timeout_seconds}s for the emulator to connect..."
    while true; do
        if adb_has_device; then
            break
        else
            adb_status=$?
            if [[ $adb_status -ne 1 ]]; then
                echo "error: adb device discovery failed while waiting for the emulator" >&2
                print_diagnostics
                return 1
            fi
        fi
        if read_emulator_pid && ! kill -0 "$emulator_pid" >/dev/null 2>&1; then
            echo "error: emulator exited before connecting to adb" >&2
            print_diagnostics
            return 1
        fi
        if (( SECONDS >= connect_deadline )); then
            echo "error: emulator did not connect within ${boot_timeout_seconds}s" >&2
            print_diagnostics
            return 1
        fi
        sleep 2
    done

    echo "Waiting up to ${boot_timeout_seconds}s for Android boot to complete..."
    boot_deadline=$((SECONDS + boot_timeout_seconds))
    while true; do
        boot_probe_output=
        if timed_capture boot_probe_output "$adb_timeout_seconds" "adb boot-completion probe" adb shell getprop sys.boot_completed; then
            boot_completed=$(printf '%s' "$boot_probe_output" | tr -d '\r')
        else
            echo "warning: unable to query Android boot state; retrying" >&2
            boot_completed=
        fi
        if [[ $boot_completed == 1 ]]; then
            break
        fi
        if read_emulator_pid && ! kill -0 "$emulator_pid" >/dev/null 2>&1; then
            echo "error: emulator exited before Android finished booting" >&2
            print_diagnostics
            return 1
        fi
        if (( SECONDS >= boot_deadline )); then
            echo "error: Android did not finish booting within ${boot_timeout_seconds}s" >&2
            print_diagnostics
            return 1
        fi
        sleep 2
    done

    optional_timed_command "$adb_timeout_seconds" "disable window animations" adb shell settings put global window_animation_scale 0
    optional_timed_command "$adb_timeout_seconds" "disable transition animations" adb shell settings put global transition_animation_scale 0
    optional_timed_command "$adb_timeout_seconds" "disable animator duration" adb shell settings put global animator_duration_scale 0
    optional_timed_command "$adb_timeout_seconds" "unlock Android input" adb shell input keyevent 82

    if adb_has_device; then
        printf '%s\n' "$adb_devices_output"
    else
        adb_status=$?
        if [[ $adb_status -eq 1 ]]; then
            echo "error: final adb device probe found no connected Android device" >&2
        else
            echo "error: final adb device probe failed (status $adb_status)" >&2
        fi
        print_diagnostics
        return 1
    fi
    return 0
}

if [[ $lifecycle == wait ]]; then
    # A failed `wait` must clean up a process owned by a preceding `start`
    # invocation even when the caller is not wrapping both lifecycle calls in
    # its own EXIT trap. A successful wait disarms this helper's trap so the
    # caller retains ownership through install/run and can stop it afterward.
    if read_emulator_pid; then
        emulator_started=1
    fi
    if ! timed_command "$adb_timeout_seconds" "adb start-server" adb start-server; then
        exit 1
    fi
    wait_started=$SECONDS
    if wait_for_ready; then
        echo "TIMING_ANDROID boot_wait_seconds=$((SECONDS - wait_started))"
        emulator_started=0
        trap - EXIT INT TERM
        exit 0
    else
        exit 1
    fi
fi

if ! timed_command "$adb_timeout_seconds" "adb start-server" adb start-server; then
    exit 1
fi

if read_emulator_pid && kill -0 "$emulator_pid" >/dev/null 2>&1; then
    echo "Reusing owned Android emulator PID $emulator_pid"
    if [[ $lifecycle == start ]]; then
        trap - EXIT INT TERM
        exit 0
    fi
    emulator_started=1
    wait_started=$SECONDS
    if wait_for_ready; then
        echo "TIMING_ANDROID boot_wait_seconds=$((SECONDS - wait_started))"
        emulator_started=0
        trap - EXIT INT TERM
        exit 0
    else
        exit 1
    fi
fi
rm -f "$started_marker"

if adb_has_device; then
    echo "Android device is already available; not starting an emulator."
    printf '%s\n' "$adb_devices_output"
    if [[ $lifecycle == start ]]; then
        exit 0
    fi
    wait_started=$SECONDS
    if wait_for_ready; then
        echo "TIMING_ANDROID boot_wait_seconds=$((SECONDS - wait_started))"
        exit 0
    else
        exit 1
    fi
else
    adb_status=$?
    if [[ $adb_status -ne 1 ]]; then
        echo "error: unable to discover Android devices; refusing to continue with AVD setup" >&2
        exit 1
    fi
fi

emulator_avds_output=
emulator_list_avds() {
    timed_capture emulator_avds_output "$tool_timeout_seconds" "emulator -list-avds" emulator -list-avds
}

if ! emulator_list_avds; then
    echo "error: unable to list Android AVDs" >&2
    exit 1
fi

if ! grep -Fx -- "$avd_name" <<<"$emulator_avds_output" >/dev/null; then
    echo "Android AVD '$avd_name' was not found for user $(id -un) with HOME=${HOME:-<unset>}" >&2
    echo "ANDROID_USER_HOME=$ANDROID_USER_HOME" >&2
    echo "ANDROID_AVD_HOME=$ANDROID_AVD_HOME" >&2
    echo "Available AVDs for this user:" >&2
    if [[ -n $emulator_avds_output ]]; then
        printf '%s\n' "$emulator_avds_output" >&2
    else
        echo "  (none)" >&2
    fi
    if [[ $create_avd == 0 || $create_avd == false || $create_avd == FALSE ]]; then
        exit 1
    fi
    if ! command -v avdmanager >/dev/null 2>&1; then
        echo "error: avdmanager was not found; install Android SDK cmdline-tools or pre-create '$avd_name' for the runner user" >&2
        exit 1
    fi

    system_image_directory="$ANDROID_HOME/system-images/$system_image_platform/$system_image_tag/$system_image_abi"
    if [[ ! -d $system_image_directory ]]; then
        echo "error: Android system image '$system_image' is not installed at '$system_image_directory'" >&2
        echo "Install it into the SDK used by the runner, for example:" >&2
        echo "  sudo env ANDROID_HOME=\"$ANDROID_HOME\" ANDROID_SDK_ROOT=\"$ANDROID_SDK_ROOT\" sdkmanager --install \"$system_image\"" >&2
        exit 1
    fi

    echo "Creating Android AVD '$avd_name' from '$system_image' with device '$device_profile'..."
    if timed_command "$avd_create_timeout_seconds" "avdmanager create avd '$avd_name'" avdmanager create avd --force --name "$avd_name" --package "$system_image" --device "$device_profile" <<< no; then
        :
    else
        echo "error: failed to create Android AVD '$avd_name'" >&2
        exit 1
    fi

    if ! emulator_list_avds; then
        echo "error: unable to verify the Android AVD list after creation" >&2
        exit 1
    fi
    if ! grep -Fx -- "$avd_name" <<<"$emulator_avds_output" >/dev/null; then
        echo "error: avdmanager reported success, but emulator still cannot see Android AVD '$avd_name'" >&2
        echo "ANDROID_USER_HOME=$ANDROID_USER_HOME" >&2
        echo "ANDROID_AVD_HOME=$ANDROID_AVD_HOME" >&2
        echo "AVD directory contents:" >&2
        timeout --kill-after=1s "${tool_timeout_seconds}s" find "$ANDROID_AVD_HOME" -maxdepth 2 \( -type f -o -type d \) >&2 || true
        exit 1
    fi
fi

echo "Android emulator acceleration check:"
accel_output=
if timed_capture accel_output "$tool_timeout_seconds" "emulator -accel-check" emulator -accel-check; then
    accel_status=0
else
    accel_status=1
fi
printf '%s\n' "$accel_output"
if [[ $accel_status -ne 0 && $system_image == *x86* ]]; then
    echo "error: Android x86/x86_64 emulators require hardware acceleration, but it is not available" >&2
    echo "Use a runner with KVM/VMX/SVM enabled, a physical Android device, or set BUSTER_ANDROID_SYSTEM_IMAGE to a non-x86 image." >&2
    exit 1
elif [[ $accel_status -ne 0 ]]; then
    echo "warning: Android acceleration check failed for non-x86 image; continuing" >&2
fi

emulator_args=(-avd "$avd_name" -no-snapshot -no-boot-anim -no-audio -no-metrics -gpu "$gpu_mode")
if [[ $headless != 0 && $headless != false && $headless != FALSE ]]; then
    emulator_args+=(-no-window)
fi

echo "Starting Android emulator '$avd_name' with gpu=$gpu_mode headless=$headless"
emulator "${emulator_args[@]}" >"$emulator_log" 2>&1 < /dev/null &
emulator_pid=$!
if [[ ! $emulator_pid =~ ^[1-9][0-9]*$ ]]; then
    echo "error: emulator did not provide a valid process ID" >&2
    exit 1
fi
emulator_started=1
marker_tmp="${started_marker}.tmp.$$"
if ! printf '%s\n' "$emulator_pid" >"$marker_tmp" || ! mv -f "$marker_tmp" "$started_marker"; then
    rm -f "$marker_tmp"
    echo "error: could not publish Android emulator PID marker '$started_marker'" >&2
    kill "$emulator_pid" >/dev/null 2>&1 || true
    exit 1
fi
echo "Android emulator started asynchronously (pid=$emulator_pid, log=$emulator_log)"

if [[ $lifecycle == start ]]; then
    trap - EXIT INT TERM
    exit 0
fi

wait_started=$SECONDS
if ! wait_for_ready; then
    exit 1
fi
echo "TIMING_ANDROID boot_wait_seconds=$((SECONDS - wait_started))"
emulator_started=0
trap - EXIT INT TERM
