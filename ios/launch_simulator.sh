#!/usr/bin/env bash
set -euo pipefail

# Standalone mode:
#   BUSTER_IOS_APP_BUNDLE=/path/to/ide.app bash ios/launch_simulator.sh
#
# Batch mode boots one simulator and runs multiple labeled app bundles in order:
#   bash ios/launch_simulator.sh --batch Debug /path/Debug/ide.app Release /path/Release/ide.app
#
# The batch interface is deliberately explicit about labels so each run gets an
# independent console log and result-marker check while the simulator is booted
# and shut down exactly once for the whole batch.
mode=single
bundle_labels=()
bundle_paths=()
if [[ ${1:-} == --batch ]]; then
    mode=batch
    shift
    if [[ $# -eq 0 || $(( $# % 2 )) -ne 0 ]]; then
        echo "usage: $0 --batch <label> <app-bundle> [<label> <app-bundle> ...]" >&2
        exit 2
    fi
    while [[ $# -gt 0 ]]; do
        bundle_labels+=("$1")
        bundle_paths+=("$2")
        shift 2
    done
else
    if [[ $# -ne 0 ]]; then
        echo "usage: $0 [--batch <label> <app-bundle> ...]" >&2
        exit 2
    fi
    app_bundle=${BUSTER_IOS_APP_BUNDLE:-}
    if [[ -z $app_bundle ]]; then
        echo "error: BUSTER_IOS_APP_BUNDLE must point at the built .app bundle" >&2
        exit 1
    fi
    bundle_labels+=(single)
    bundle_paths+=("$app_bundle")
fi

for app_bundle in "${bundle_paths[@]}"; do
    if [[ ! -d $app_bundle ]]; then
        echo "error: iOS app bundle not found at '$app_bundle'" >&2
        exit 1
    fi
done

bundle_id=${BUSTER_IOS_BUNDLE_ID:-dev.buster.ide}
device_name=${BUSTER_IOS_SIMULATOR_DEVICE:-buster-ci}
launch_timeout_seconds=${BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS:-180}
boot_timeout_seconds=${BUSTER_IOS_BOOT_TIMEOUT_SECONDS:-180}
install_timeout_seconds=${BUSTER_IOS_INSTALL_TIMEOUT_SECONDS:-120}
codesign_timeout_seconds=${BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS:-60}
shutdown_timeout_seconds=${BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS:-30}
monitor_command_timeout_seconds=${BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS:-10}
result_marker_success="BUSTER_IOS_RESULT: SUCCESS"
result_marker_failure="BUSTER_IOS_RESULT: FAILURE"

for timeout_value in \
    "$launch_timeout_seconds" \
    "$boot_timeout_seconds" \
    "$install_timeout_seconds" \
    "$codesign_timeout_seconds" \
    "$shutdown_timeout_seconds" \
    "$monitor_command_timeout_seconds"; do
    if [[ ! $timeout_value =~ ^[1-9][0-9]*$ ]]; then
        echo "error: iOS simulator timeouts must be positive integers; got '$timeout_value'" >&2
        exit 1
    fi
done

if ! command -v xcrun >/dev/null 2>&1; then
    echo "error: xcrun was not found; Xcode command line tools are required" >&2
    exit 1
fi
if ! command -v codesign >/dev/null 2>&1; then
    echo "error: codesign was not found; Xcode command line tools are required" >&2
    exit 1
fi

log_dir=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
console_log_base=${BUSTER_IOS_CONSOLE_LOG:-${log_dir%/}/buster-ios-console.log}

# macOS runners do not always ship GNU `timeout`; prefer it (or coreutils
# `gtimeout`) when present so a stuck simulator fails fast instead of hanging
# silently under Ninja's output buffering.
timeout_bin=
if command -v timeout >/dev/null 2>&1; then
    timeout_bin=timeout
elif command -v gtimeout >/dev/null 2>&1; then
    timeout_bin=gtimeout
else
    echo "error: timeout or gtimeout is required for bounded iOS simulator CI" >&2
    exit 1
fi

run_with_timeout() {
    local seconds=$1
    shift
    "$timeout_bin" --kill-after=10s "${seconds}s" "$@"
}

print_simulator_diagnostics() {
    echo "----- iOS simulator devices -----" >&2
    run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl list devices >&2 || true
    echo "----- iOS simulator runtimes -----" >&2
    run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl list runtimes >&2 || true
}

collect_launch_diagnostics() {
    local label=$1
    local console_log=$2
    local app_pid=$3
    local outcome=${4:-missing-marker}
    local process_output
    local unified_log_output
    local crash_output
    local process_probe_status

    echo "----- iOS ${label} launch diagnostics -----" >&2
    if [[ $outcome == failure-marker ]]; then
        echo "the app emitted a failure marker; collecting simulator diagnostics." >&2
    else
        echo "simctl launch --console-pty returned before a successful buster result marker;" >&2
        echo "the following probes distinguish an app exit/crash from an attachment that is still alive." >&2
    fi
    if [[ -n $app_pid ]]; then
        echo "App PID reported by simctl: $app_pid" >&2
        if process_output=$(run_with_timeout "$monitor_command_timeout_seconds" \
            xcrun simctl spawn "$udid" ps -p "$app_pid" -o pid=,ppid=,stat=,comm=,args= 2>&1); then
            printf '%s\n' "$process_output" >&2
        else
            process_probe_status=$?
            if [[ $process_probe_status -eq 124 || $process_probe_status -eq 137 ]]; then
                echo "App PID probe timed out; simulator process state is unknown." >&2
            else
                echo "App PID $app_pid is no longer visible to the simulator process table." >&2
                echo "Interpretation: the app likely terminated or crashed before producing the marker; this is not a launch-timeout classification." >&2
            fi
        fi
    fi
    if process_output=$(run_with_timeout "$monitor_command_timeout_seconds" \
        xcrun simctl spawn "$udid" ps -A -o pid=,ppid=,stat=,comm=,args= 2>&1); then
        echo "----- simulator process table (buster matches) -----" >&2
        printf '%s\n' "$process_output" | grep -iE "${bundle_id}|ide" >&2 || true
    else
        echo "warning: could not read the simulator process table" >&2
    fi
    if unified_log_output=$(run_with_timeout "$monitor_command_timeout_seconds" \
        xcrun simctl spawn "$udid" log show --style compact --last 5m \
        --predicate "process == '${bundle_id}' OR eventMessage CONTAINS[c] '${bundle_id}'" 2>&1); then
        echo "----- simulator unified log (last 5m) -----" >&2
        printf '%s\n' "$unified_log_output" | tail -n 160 >&2
    else
        echo "warning: could not read the simulator unified log" >&2
    fi
    if crash_output=$(run_with_timeout "$monitor_command_timeout_seconds" \
        xcrun simctl spawn "$udid" sh -c \
        'find /var/mobile/Library/Logs/CrashReporter -type f -mmin -10 -print -exec tail -n 80 {} \; 2>/dev/null' 2>&1); then
        echo "----- recent simulator crash reports and tails -----" >&2
        if [[ -n $crash_output ]]; then
            printf '%s\n' "$crash_output" >&2
        else
            echo "(no recent crash-report paths found)" >&2
        fi
    else
        echo "warning: could not inspect simulator crash-report paths" >&2
    fi
    echo "----- ${label} console log tail -----" >&2
    tail -n 80 "$console_log" >&2 || true
    print_simulator_diagnostics
}

extract_app_pid() {
    local console_log=$1
    awk -v prefix="${bundle_id}: " '
        index($0, prefix) == 1 {
            value = substr($0, length(prefix) + 1)
            if (value ~ /^[0-9][0-9]*$/) {
                print value
            }
        }
    ' "$console_log" | tail -n 1
}

# Return 0 when the app is running, 1 when it is definitely gone, and 2 when
# the simulator probe itself failed. `simctl launch --console-pty` on recent
# Xcode versions can return status 0 after only printing "bundle: pid", so this
# probe is the authority for early app termination.
probe_app_process() {
    local app_pid=$1
    local process_output
    local probe_status
    if [[ -n $app_pid ]]; then
        if process_output=$(run_with_timeout "$monitor_command_timeout_seconds" \
            xcrun simctl spawn "$udid" ps -p "$app_pid" -o pid=,stat=,comm= 2>/dev/null); then
            probe_status=0
        else
            probe_status=$?
        fi
        if [[ $probe_status -eq 0 ]]; then
            if printf '%s\n' "$process_output" | awk -v pid="$app_pid" '$1 == pid { found = 1 } END { exit found ? 0 : 1 }'; then
                return 0
            fi
            return 1
        fi
        # `simctl spawn` propagates the child `ps -p` status. A status of 1 is
        # therefore the normal, definitive "PID is gone" result; timeout and
        # simulator-transport failures remain indeterminate.
        if [[ $probe_status -eq 1 ]]; then
            return 1
        fi
        return 2
    fi

    if process_output=$(run_with_timeout "$monitor_command_timeout_seconds" \
        xcrun simctl spawn "$udid" ps -A -o pid=,stat=,comm=,args= 2>/dev/null); then
        if printf '%s\n' "$process_output" | grep -qiF "$bundle_id"; then
            return 0
        fi
        return 1
    fi
    return 2
}

stop_launch_stream() {
    local stream_pid=$1
    local stop_deadline
    # Launch GNU timeout itself, not a shell around a pipeline: its PID owns
    # the producer's private process group, including the attached xcrun.
    if [[ -n $stream_pid ]]; then
        kill -TERM -- -"$stream_pid" 2>/dev/null || kill -TERM "$stream_pid" 2>/dev/null || true
        stop_deadline=$((SECONDS + monitor_command_timeout_seconds))
        while kill -0 -- -"$stream_pid" 2>/dev/null && (( SECONDS < stop_deadline )); do
            sleep 1
        done
        kill -KILL -- -"$stream_pid" 2>/dev/null || true
        wait "$stream_pid" 2>/dev/null || true
    fi
    # Let tee drain to EOF after the producer closes its descriptor, but never
    # let a stuck reader hold CI output or the next batch configuration open.
    if [[ -n ${active_launch_reader_pid:-} ]]; then
        stop_deadline=$((SECONDS + monitor_command_timeout_seconds))
        while kill -0 "$active_launch_reader_pid" 2>/dev/null && (( SECONDS < stop_deadline )); do
            sleep 1
        done
        kill -TERM "$active_launch_reader_pid" 2>/dev/null || true
        kill -KILL "$active_launch_reader_pid" 2>/dev/null || true
        wait "$active_launch_reader_pid" 2>/dev/null || true
        active_launch_reader_pid=
    fi
    if [[ -n ${active_launch_pipe_dir:-} ]]; then
        rm -f "$active_launch_pipe_dir/console"
        rmdir "$active_launch_pipe_dir"
        active_launch_pipe_dir=
    fi
}

launch_stream_is_running() {
    local stream_pid=$1
    local pid
    local stream_state
    # Both sides must finish before the final marker read. This avoids racing
    # tee's last buffered output when simctl exits immediately.
    for pid in "$stream_pid" "${active_launch_reader_pid:-}"; do
        if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then
            stream_state=$(ps -p "$pid" -o stat= 2>/dev/null || true)
            case "$stream_state" in
                ''|Z*|*Z*) ;;
                *) return 0 ;;
            esac
        fi
    done
    return 1
}

udid=${BUSTER_IOS_SIMULATOR_UDID:-}
active_launch_stream_pid=
active_launch_reader_pid=
active_launch_pipe_dir=

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n ${active_launch_stream_pid:-}${active_launch_reader_pid:-}${active_launch_pipe_dir:-} ]]; then
        stop_launch_stream "$active_launch_stream_pid"
        active_launch_stream_pid=
    fi
    # Always shut down the selected device. Batch mode reaches this trap once,
    # after both app bundles have had independent install/launch checks.
    if [[ -n ${udid:-} ]]; then
        echo "Shutting down iOS simulator $udid"
        if ! run_with_timeout "$shutdown_timeout_seconds" xcrun simctl shutdown "$udid" >/dev/null 2>&1; then
            echo "warning: failed to shut down iOS simulator $udid" >&2
            if [[ $status -eq 0 ]]; then
                status=1
            fi
        fi
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Reuse a device by name, otherwise create one from the newest iOS runtime and an
# available iPhone device type. BUSTER_IOS_SIMULATOR_UDID is useful for local
# runs and deterministic fake-tool tests, while name-based discovery remains the
# default CI behavior.
if [[ -z $udid ]]; then
    if udid_output=$(run_with_timeout "$boot_timeout_seconds" \
        xcrun simctl list devices available -j 2>/dev/null | python3 -c '
import json, sys
name = sys.argv[1]
data = json.load(sys.stdin)["devices"]
for runtime_devices in data.values():
    for device in runtime_devices:
        if device.get("name") == name and device.get("isAvailable", True):
            print(device["udid"]); sys.exit(0)
sys.exit(1)
' "$device_name"); then
        udid=$udid_output
    else
        udid=
    fi
fi

# Newest available iOS runtime plus a compatible iPhone device type, as
# "<runtime-id>\t<devicetype-id>". The device type is taken from the runtime's
# own supportedDeviceTypes so it cannot be an incompatible (too-old) model.
find_runtime_and_device() {
    run_with_timeout "$boot_timeout_seconds" xcrun simctl list runtimes -j | python3 -c '
import json, re, sys
runtimes = [r for r in json.load(sys.stdin)["runtimes"]
            if r.get("isAvailable") and "iOS" in (r.get("identifier", "") + r.get("name", ""))]
def ver(r):
    return tuple(int(x) for x in re.findall(r"\d+", r.get("version", "0")))
runtimes.sort(key=ver)
for r in reversed(runtimes):
    iphones = [d for d in r.get("supportedDeviceTypes", []) if d.get("productFamily") == "iPhone"]
    if not iphones:
        continue
    def model(d):
        m = re.search(r"iPhone[- ](\d+)", d.get("name", ""))
        return int(m.group(1)) if m else -1
    best = max(iphones, key=model)
    print(r["identifier"] + "\t" + best["identifier"])
    sys.exit(0)
sys.exit(1)
'
}

if [[ -z $udid ]]; then
    runtime_and_device=
    if runtime_and_device=$(find_runtime_and_device); then
        :
    else
        runtime_and_device=
    fi
    # Xcode 16+/26 ships simulator runtimes on demand, so a fresh runner may have
    # none even though the iphonesimulator SDK is present. Downloading one is
    # multi-GB and silent under Ninja, so it is opt-in (off by default) to avoid
    # masking a misconfigured runner as a long hang.
    if [[ -z $runtime_and_device && ${BUSTER_IOS_DOWNLOAD_RUNTIME:-0} != 0 ]]; then
        echo "No iOS simulator runtime installed; downloading one via xcodebuild..."
        if ! xcodebuild -downloadPlatform iOS && ! xcrun simctl runtime add "iOS"; then
            echo "error: failed to download an iOS simulator runtime" >&2
            exit 1
        fi
        if runtime_and_device=$(find_runtime_and_device); then
            :
        else
            runtime_and_device=
        fi
    fi
    if [[ -z $runtime_and_device ]]; then
        echo "error: no available iOS simulator runtime found." >&2
        echo "Install one on the runner (e.g. 'xcodebuild -downloadPlatform iOS')," >&2
        echo "or set BUSTER_IOS_DOWNLOAD_RUNTIME=1 to let this script download it." >&2
        run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl list runtimes >&2 || true
        exit 1
    fi
    runtime=${runtime_and_device%%$'\t'*}
    device_type=${runtime_and_device##*$'\t'}
    if [[ -z $device_type ]]; then
        echo "error: no compatible iPhone simulator device type found" >&2
        exit 1
    fi

    echo "Creating simulator '$device_name' (type=$device_type, runtime=$runtime)"
    if ! udid=$(run_with_timeout "$boot_timeout_seconds" \
        xcrun simctl create "$device_name" "$device_type" "$runtime"); then
        echo "error: failed to create iOS simulator '$device_name'" >&2
        exit 1
    fi
fi

echo "Using simulator $device_name ($udid)"

# Keep the existing cleanup policy for stale unavailable devices. Do not shut
# down all simulators here: the selected simulator is shut down once by the EXIT
# trap after the batch, and an already-booted selected device can be reused.
run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl delete unavailable 2>/dev/null || true

boot_started=$SECONDS
if run_with_timeout "$boot_timeout_seconds" xcrun simctl boot "$udid" 2>/dev/null; then
    :
else
    boot_status=$?
    echo "iOS simulator boot returned status $boot_status; continuing to bootstatus (it may already be booted)" >&2
fi
if ! run_with_timeout "$boot_timeout_seconds" xcrun simctl bootstatus "$udid" -b; then
    echo "error: iOS simulator did not become ready within ${boot_timeout_seconds}s" >&2
    print_simulator_diagnostics
    exit 1
fi
echo "TIMING_IOS boot_seconds=$((SECONDS - boot_started))"

console_log_for_label() {
    local label=$1
    if [[ $mode == single ]]; then
        printf '%s\n' "$console_log_base"
    elif [[ $console_log_base == *.log ]]; then
        printf '%s.%s.log\n' "${console_log_base%.log}" "$label"
    else
        printf '%s.%s.log\n' "$console_log_base" "$label"
    fi
}

run_one_bundle() {
    local label=$1
    local app_bundle=$2
    local console_log
    local install_started
    local launch_started
    local launch_stream_pid
    local launch_stream_status
    local launch_stream_done
    local app_pid
    local process_status
    local deadline
    local result
    local early_exit_reported

    console_log=$(console_log_for_label "$label")
    mkdir -p "$(dirname "$console_log")"

    echo "Codesigning iOS ${label} app bundle"
    if ! run_with_timeout "$codesign_timeout_seconds" \
        codesign --force --sign - --timestamp=none "$app_bundle" >/dev/null 2>&1; then
        echo "error: codesign failed for iOS ${label} app bundle" >&2
        return 1
    fi

    echo "Installing iOS ${label} app bundle: $app_bundle"
    install_started=$SECONDS
    if ! run_with_timeout "$install_timeout_seconds" xcrun simctl install "$udid" "$app_bundle"; then
        echo "error: failed to install iOS ${label} app bundle" >&2
        print_simulator_diagnostics
        return 1
    fi
    echo "TIMING_IOS install_seconds label=$label value=$((SECONDS - install_started))"

    echo "Launching $bundle_id for iOS ${label} (timeout ${launch_timeout_seconds}s)"
    : >"$console_log"
    launch_started=$SECONDS
    deadline=$((launch_started + launch_timeout_seconds))
    app_pid=
    launch_stream_done=0
    launch_stream_status=0
    result=
    early_exit_reported=0

    # On older Xcode versions this command remained attached until the app
    # exited. On current Xcode (as observed in CI) it can print
    # "dev.buster.ide: <pid>" and return 0 immediately. Keep the command in the
    # background and watch both its console file and the launched app instead of
    # treating its status as the test result.
    # A private FIFO works with macOS Bash 3.2 and gives the parent direct
    # ownership of both children; $! from a background pipeline is not enough.
    active_launch_pipe_dir=$(mktemp -d "${log_dir%/}/buster-ios-stream.XXXXXX")
    mkfifo "$active_launch_pipe_dir/console"
    tee "$console_log" <"$active_launch_pipe_dir/console" &
    active_launch_reader_pid=$!
    "$timeout_bin" --kill-after=10s "${launch_timeout_seconds}s" \
        xcrun simctl launch --console-pty "$udid" "$bundle_id" test \
        >"$active_launch_pipe_dir/console" 2>&1 &
    launch_stream_pid=$!
    active_launch_stream_pid=$launch_stream_pid

    while true; do
        if grep -qF "$result_marker_success" "$console_log"; then
            result=success
            break
        fi
        if grep -qF "$result_marker_failure" "$console_log"; then
            result=failure
            break
        fi

        if [[ -z $app_pid ]]; then
            app_pid=$(extract_app_pid "$console_log")
        fi

        if [[ $launch_stream_done -eq 0 ]] && ! launch_stream_is_running "$launch_stream_pid"; then
            if wait "$launch_stream_pid"; then
                launch_stream_status=0
            else
                launch_stream_status=$?
            fi
            launch_stream_done=1
            if [[ -z $app_pid ]]; then
                app_pid=$(extract_app_pid "$console_log")
            fi
            # The stream can finish between the marker check at the top of the
            # loop and this wait. Re-read the completed file before probing the
            # process, otherwise a valid last line can be mistaken for an early
            # app exit.
            if grep -qF "$result_marker_success" "$console_log"; then
                result=success
                break
            fi
            if grep -qF "$result_marker_failure" "$console_log"; then
                result=failure
                break
            fi
            if [[ $launch_stream_status -eq 0 ]]; then
                echo "warning: simctl launch --console-pty returned before the ${label} result marker; continuing app monitoring" >&2
            else
                echo "warning: simctl launch --console-pty returned status ${launch_stream_status} before the ${label} result marker; continuing diagnostics" >&2
            fi
        fi

        if [[ $launch_stream_done -eq 1 ]]; then
            if probe_app_process "$app_pid"; then
                :
            else
                process_status=$?
                if [[ $process_status -eq 1 ]]; then
                    if [[ $early_exit_reported -eq 0 ]]; then
                        echo "error: iOS ${label} app terminated before emitting a buster result marker; this is an early app/console exit, not a timeout" >&2
                        collect_launch_diagnostics "$label" "$console_log" "$app_pid"
                        early_exit_reported=1
                    fi
                    stop_launch_stream "$launch_stream_pid"
                    active_launch_stream_pid=
                    echo "TIMING_IOS test_seconds label=$label value=$((SECONDS - launch_started))"
                    return 1
                fi
            fi
        fi

        if (( SECONDS >= deadline )); then
            echo "error: iOS ${label} did not produce a buster test result marker before the ${launch_timeout_seconds}s launch deadline" >&2
            echo "error: this is a real launch timeout; simctl/console attachment status was ${launch_stream_status}" >&2
            if [[ $launch_stream_done -eq 1 ]]; then
                echo "warning: simctl launch --console-pty ended before the deadline; if the app process remains alive, the console attachment returned early or is not forwarding stdout" >&2
            fi
            stop_launch_stream "$launch_stream_pid"
            active_launch_stream_pid=
            collect_launch_diagnostics "$label" "$console_log" "$app_pid"
            echo "TIMING_IOS test_seconds label=$label value=$((SECONDS - launch_started))"
            return 1
        fi
        sleep 1
    done

    # A marker is authoritative for the in-app test result. Stop only the
    # console attachment, not the simulator lifecycle; the EXIT trap handles
    # shutdown after the full batch.
    stop_launch_stream "$launch_stream_pid"
    active_launch_stream_pid=
    echo "TIMING_IOS test_seconds label=$label value=$((SECONDS - launch_started))"
    if [[ $result == success ]]; then
        echo "iOS ${label} tests passed."
        return 0
    fi
    echo "error: iOS ${label} tests reported failure." >&2
    collect_launch_diagnostics "$label" "$console_log" "$app_pid" failure-marker
    return 1
}

overall_status=0
for index in "${!bundle_paths[@]}"; do
    if run_one_bundle "${bundle_labels[$index]}" "${bundle_paths[$index]}"; then
        :
    else
        overall_status=1
    fi
done

exit "$overall_status"
