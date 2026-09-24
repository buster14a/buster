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
# A hosted ARM64 device that times out during its first readiness check can
# finish migration without starting over. One bounded continuation precedes
# the existing one-device replacement; neither path accepts a Booted state as
# readiness, and the replacement retains its original single readiness check.
boot_continuation_seconds=${BUSTER_IOS_BOOT_CONTINUATION_SECONDS:-120}
install_timeout_seconds=${BUSTER_IOS_INSTALL_TIMEOUT_SECONDS:-120}
codesign_timeout_seconds=${BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS:-60}
shutdown_timeout_seconds=${BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS:-30}
# GitHub's macOS 26 Apple-Silicon CoreSimulator can need substantially
# longer than 30 seconds to quiesce after the full Debug+Release batch.
# Preserve the tighter local/self-hosted budget and every explicit caller
# override, but give the hosted arm64 lane enough time to complete an
# orderly shutdown before the postcondition recovery path is needed.
if [[ -z ${BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS:-} \
    && ${GITHUB_ACTIONS:-false} == true \
    && ${RUNNER_ENVIRONMENT:-} == github-hosted \
    && ${RUNNER_OS:-} == macOS \
    && ${RUNNER_ARCH:-} == ARM64 \
    && ${BUSTER_IOS_ARCH:-arm64} == arm64 ]]; then
    shutdown_timeout_seconds=90
fi
monitor_command_timeout_seconds=${BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS:-10}
result_marker_success="BUSTER_IOS_RESULT: SUCCESS"
result_marker_failure="BUSTER_IOS_RESULT: FAILURE"

for timeout_value in \
    "$launch_timeout_seconds" \
    "$boot_timeout_seconds" \
    "$boot_continuation_seconds" \
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

# Keep at most 64 KiB, but drain the producer so a full evidence file cannot
# change the command's status via SIGPIPE. The reader has its own outer bound
# in case a descendant retains the output descriptor after the command exits.
capture_lifecycle_output() {
    local seconds=$1
    local path=$2
    run_with_timeout "$seconds" python3 -c '
import sys
remaining = 65536
total = 0
with open(sys.argv[1], "wb", buffering=0) as output:
    while True:
        chunk = sys.stdin.buffer.read1(4096)
        if not chunk:
            break
        output.write(chunk[:remaining])
        remaining = max(0, remaining - len(chunk))
        total += len(chunk)
with open(sys.argv[1] + ".capture-status.log", "w") as status:
    status.write("BUSTER_IOS_CAPTURE total_bytes=%d retained_bytes=%d truncated=%d\n" %
                 (total, min(total, 65536), total > 65536))
' "$path"
}

observe_lifecycle_context() {
    local name=$1
    shift
    local started=$SECONDS status
    printf '%s\n' "----- iOS context $name -----"
    if run_with_timeout "$monitor_command_timeout_seconds" "$@"; then
        status=0
    else
        status=$?
    fi
    printf 'BUSTER_IOS_CONTEXT_PROBE name=%s status=%s elapsed_seconds=%s deadline_seconds=%s\n' \
        "$name" "$status" "$((SECONDS - started))" "$monitor_command_timeout_seconds"
}

collect_lifecycle_context() {
    local context_base=$1 attempt=$2
    local context_log="${context_base}.lifecycle-context.log"
    local context_status="${context_base}.lifecycle-context.status.log"
    local capture_status=0 receipt=incomplete
    if [[ ! -e $context_status ]]; then
        {
            printf 'GITHUB_SHA=%s\nDEVELOPER_DIR=%s\nSIMULATOR_UDID=%s\nSIMULATOR_RUNTIME=%s\nSIMULATOR_DEVICE_TYPE=%s\nATTEMPT=%s\n' \
                "${GITHUB_SHA:-unavailable}" "${DEVELOPER_DIR:-default}" "${udid:-unavailable}" \
                "${runtime:-unavailable}" "${device_type:-unavailable}" "$attempt"
            observe_lifecycle_context source git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse HEAD
            observe_lifecycle_context xcode xcodebuild -version
            observe_lifecycle_context sdk xcrun --sdk iphonesimulator --show-sdk-version
            observe_lifecycle_context devices xcrun simctl list devices
            observe_lifecycle_context runtimes xcrun simctl list runtimes
            # These small host probes distinguish a busy or full host from a
            # stalled simulator without changing the simulator service.
            observe_lifecycle_context memory vm_stat
            observe_lifecycle_context disk df -h "$log_dir"
        } 2>&1 | capture_lifecycle_output "$((7 * monitor_command_timeout_seconds + 10))" "$context_log" || capture_status=$?
        if [[ $capture_status -eq 0 && -s ${context_log}.capture-status.log ]]; then
            receipt=complete
        fi
        printf 'BUSTER_IOS_CONTEXT attempt=%s udid=%s runtime=%s source=%s capture_status=%s capture_receipt=%s output_log=%s\n' \
            "$attempt" "${udid:-unavailable}" "${runtime:-unavailable}" "${GITHUB_SHA:-unavailable}" \
            "$capture_status" "$receipt" "$context_log" >"$context_status" || true
        cat "$context_status" >&2 || true
        echo "iOS lifecycle context: $context_log" >&2
    fi
}

run_lifecycle_phase() {
    local phase=$1 label=$2 evidence_base=$3 seconds=$4
    shift 4
    local output_log="${evidence_base}.${phase}.log"
    local status_log="${evidence_base}.${phase}.status.log"
    local native_log="${evidence_base}.${phase}.native-status.log"
    local started=$SECONDS
    local statuses native_status=unavailable outcome result
    local capture_receipt=incomplete command_elapsed=unavailable capture_elapsed=unavailable
    local command_elapsed_log="${evidence_base}.${phase}.command-elapsed.log"
    local capture_elapsed_log="${evidence_base}.${phase}.capture-elapsed.log"
    mkdir -p "$(dirname "$evidence_base")"
    rm -f "$native_log" "${output_log}.capture-status.log" "$command_elapsed_log" "$capture_elapsed_log"
    # Record the command's own status before the timeout helper returns. This
    # distinguishes an ordinary exit 124 from the helper's deadline status.
    if (
        command_started=$SECONDS
        if run_with_timeout "$seconds" "$BASH" -c '
        status_path=$1
        shift
        if "$@"; then status=0; else status=$?; fi
        printf "%s\n" "$status" >"$status_path"
        exit "$status"
    ' bash "$native_log" "$@"; then command_status=0; else command_status=$?; fi
        printf '%s\n' "$((SECONDS - command_started))" >"$command_elapsed_log"
        exit "$command_status"
    ) 2>&1 | (
        capture_started=$SECONDS
        if capture_lifecycle_output "$((seconds + 10 + monitor_command_timeout_seconds))" "$output_log"; then
            capture_status=0
        else
            capture_status=$?
        fi
        printf '%s\n' "$((SECONDS - capture_started))" >"$capture_elapsed_log"
        exit "$capture_status"
    ); then
        statuses=("${PIPESTATUS[@]}")
    else
        statuses=("${PIPESTATUS[@]}")
    fi
    if [[ -f $native_log ]]; then
        read -r native_status <"$native_log" || native_status=unavailable
    fi
    if [[ -s $command_elapsed_log ]]; then
        read -r command_elapsed <"$command_elapsed_log" || command_elapsed=unavailable
    fi
    if [[ -s $capture_elapsed_log ]]; then
        read -r capture_elapsed <"$capture_elapsed_log" || capture_elapsed=unavailable
    fi
    if [[ ${statuses[1]} -eq 0 && -s ${output_log}.capture-status.log ]] \
        && grep -Eq '^BUSTER_IOS_CAPTURE total_bytes=[0-9]+ retained_bytes=[0-9]+ truncated=[01]$' \
            "${output_log}.capture-status.log"; then
        capture_receipt=complete
    fi
    result=${statuses[0]}
    outcome=command-failure
    if [[ $result -eq 0 ]]; then
        outcome=success
    elif [[ $native_status == unavailable ]]; then
        case "$result" in
            124) outcome=timeout ;;
            125) outcome=timeout-helper-failure ;;
            126|127) outcome=launch-failure ;;
            137) outcome=timeout-or-signal ;;
            *) outcome=signal-or-command-failure ;;
        esac
    fi
    if [[ ( ${statuses[1]} -ne 0 || $capture_receipt != complete ) && $result -eq 0 ]]; then
        outcome=evidence-failure
        result=1
    fi
    if ! {
        printf 'BUSTER_IOS_PHASE phase=%s label=%s outcome=%s status=%s native_status=%s capture_status=%s elapsed_seconds=%s deadline_seconds=%s output_limit_bytes=65536 command_elapsed_seconds=%s capture_elapsed_seconds=%s capture_receipt=%s\n' \
            "$phase" "$label" "$outcome" "${statuses[0]}" "$native_status" "${statuses[1]}" "$((SECONDS - started))" "$seconds" \
            "$command_elapsed" "$capture_elapsed" "$capture_receipt"
        printf 'command:'
        printf ' %q' "$@"
        printf '\noutput_log=%s\n' "$output_log"
        if [[ -s ${output_log}.capture-status.log ]]; then
            cat "${output_log}.capture-status.log"
        else
            printf 'BUSTER_IOS_CAPTURE incomplete=1 reason=missing-or-empty-receipt\n'
        fi
    } >"$status_log"; then
        echo "error: could not retain iOS $phase status at $status_log" >&2
        result=1
        outcome=evidence-failure
    fi
    last_lifecycle_outcome=$outcome
    cat "$status_log" >&2 || true
    if [[ $result -ne 0 ]]; then
        # Full bounded raw output remains in the artifact, independent of the
        # console/result-marker logs that are reset for each app launch.
        tail -c 4096 "$output_log" >&2 || true
        if [[ $phase == boot || $phase == bootstatus || $phase == bootstatus-continue ]]; then
            collect_lifecycle_context "${evidence_base}.${phase}" "$label"
        else
            collect_lifecycle_context "$console_log_base" general
        fi
    fi
    return "$result"
}

simulator_udid_is_valid() {
    local value=$1
    [[ $value =~ ^[[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12}$ ]]
}

# simctl create writes one canonical UUID on stdout. Since lifecycle evidence
# combines stdout and stderr, reject every other nonempty line instead of
# treating an arbitrary diagnostic or device name as an owned identity.
read_simulator_create_identity() {
    local path=$1
    local line candidate= valid_count=0 invalid_count=0
    if [[ ! -f $path ]]; then
        return 1
    fi
    while IFS= read -r line || [[ -n $line ]]; do
        line=${line%$'\r'}
        if [[ -z $line ]]; then
            continue
        fi
        if simulator_udid_is_valid "$line"; then
            candidate=$line
            valid_count=$((valid_count + 1))
        else
            invalid_count=$((invalid_count + 1))
        fi
    done <"$path"
    if [[ $valid_count -eq 1 && $invalid_count -eq 0 ]]; then
        printf '%s\n' "$candidate"
        return 0
    fi
    return 1
}

adopt_pending_create_identity() {
    local candidate=
    if [[ ${pending_create_owned:-0} -eq 1 && -z ${udid:-} \
        && -n ${pending_create_log:-} ]]; then
        if candidate=$(read_simulator_create_identity "$pending_create_log"); then
            udid=$candidate
            simulator_owned=1
            pending_create_owned=0
            pending_create_log=
            echo "Recovered pending invocation-owned replacement simulator identity $udid for cleanup" >&2
        fi
    fi
}

print_simulator_diagnostics() {
    echo "----- iOS simulator devices -----" >&2
    run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl list devices >&2 || true
    echo "----- iOS simulator runtimes -----" >&2
    run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl list runtimes >&2 || true
}

verify_shutdown_postcondition() {
    local probe_log="${console_log_base}.shutdown-postcondition.log"
    local result_log="${console_log_base}.shutdown-postcondition.result.log"
    local parsed_state=
    local summary

    shutdown_postcondition_probe_status=not-run
    shutdown_postcondition_parser_status=not-run
    shutdown_postcondition_state=unavailable
    shutdown_disposition=unresolved-failure
    if run_lifecycle_phase shutdown-postcondition batch "$console_log_base" \
        "$monitor_command_timeout_seconds" xcrun simctl list devices -j; then
        shutdown_postcondition_probe_status=0
    else
        shutdown_postcondition_probe_status=$?
    fi
    if [[ $shutdown_postcondition_probe_status -eq 0 ]]; then
        if parsed_state=$(run_with_timeout "$monitor_command_timeout_seconds" \
            python3 - "$udid" "$probe_log" <<'PY_STATE'
import json
import sys

target, path = sys.argv[1:]
try:
    with open(path, "r", encoding="utf-8") as stream:
        data = json.load(stream)
except (OSError, UnicodeError, json.JSONDecodeError):
    sys.exit(2)
devices = data.get("devices") if isinstance(data, dict) else None
if not isinstance(devices, dict):
    sys.exit(2)
matches = []
for runtime_devices in devices.values():
    if not isinstance(runtime_devices, list):
        sys.exit(2)
    for device in runtime_devices:
        if not isinstance(device, dict):
            sys.exit(2)
        if device.get("udid") == target:
            matches.append(device)
if len(matches) != 1:
    sys.exit(3)
state = matches[0].get("state")
if not isinstance(state, str):
    sys.exit(2)
if state == "Shutdown":
    print("Shutdown")
    sys.exit(0)
print("non-shutdown")
sys.exit(4)
PY_STATE
        ); then
            shutdown_postcondition_parser_status=0
        else
            shutdown_postcondition_parser_status=$?
        fi
        if [[ -n $parsed_state ]]; then
            shutdown_postcondition_state=$parsed_state
        fi
        if [[ $shutdown_postcondition_parser_status -eq 0 \
            && $shutdown_postcondition_state == Shutdown ]]; then
            shutdown_disposition=verified-shutdown-after-timeout
        fi
    fi
    summary="BUSTER_IOS_SHUTDOWN_POSTCONDITION simulator_udid=$udid eligibility=$shutdown_postcondition_eligible probe_status=$shutdown_postcondition_probe_status parser_status=$shutdown_postcondition_parser_status state=$shutdown_postcondition_state disposition=$shutdown_disposition"
    if ! printf '%s\n' "$summary" >"$result_log"; then
        shutdown_disposition=unresolved-evidence-failure
        summary="BUSTER_IOS_SHUTDOWN_POSTCONDITION simulator_udid=$udid eligibility=$shutdown_postcondition_eligible probe_status=$shutdown_postcondition_probe_status parser_status=$shutdown_postcondition_parser_status state=$shutdown_postcondition_state disposition=$shutdown_disposition"
        echo "error: could not retain iOS shutdown postcondition result at $result_log" >&2
    fi
    printf '%s\n' "$summary" >&2
    [[ $shutdown_disposition == verified-shutdown-after-timeout ]]
}

verify_shutdown_recovery_postcondition() {
    local probe_log="${console_log_base}.shutdown-recovery-postcondition.log"
    local result_log="${console_log_base}.shutdown-recovery-postcondition.result.log"
    local parsed_state=
    local summary

    shutdown_recovery_probe_status=not-run
    shutdown_recovery_parser_status=not-run
    shutdown_recovery_state=unavailable
    shutdown_recovery_disposition=unresolved-failure
    if run_lifecycle_phase shutdown-recovery-postcondition batch "$console_log_base" \
        "$monitor_command_timeout_seconds" xcrun simctl list devices -j; then
        shutdown_recovery_probe_status=0
    else
        shutdown_recovery_probe_status=$?
    fi
    if [[ $shutdown_recovery_probe_status == 0 ]]; then
        if parsed_state=$(run_with_timeout "$monitor_command_timeout_seconds" \
            python3 - "$udid" "$probe_log" <<'PY_STATE'
import json
import sys

target, path = sys.argv[1:]
try:
    with open(path, "r", encoding="utf-8") as stream:
        data = json.load(stream)
except (OSError, UnicodeError, json.JSONDecodeError):
    sys.exit(2)
devices = data.get("devices") if isinstance(data, dict) else None
if not isinstance(devices, dict):
    sys.exit(2)
matches = []
for runtime_devices in devices.values():
    if not isinstance(runtime_devices, list):
        sys.exit(2)
    for device in runtime_devices:
        if not isinstance(device, dict):
            sys.exit(2)
        if device.get("udid") == target:
            matches.append(device)
if len(matches) != 1:
    sys.exit(3)
state = matches[0].get("state")
if not isinstance(state, str):
    sys.exit(2)
if state == "Shutdown":
    print("Shutdown")
    sys.exit(0)
print("non-shutdown")
sys.exit(4)
PY_STATE
        ); then
            shutdown_recovery_parser_status=0
        else
            shutdown_recovery_parser_status=$?
        fi
        if [[ -n $parsed_state ]]; then
            shutdown_recovery_state=$parsed_state
        fi
        if [[ $shutdown_recovery_parser_status == 0 \
            && $shutdown_recovery_state == Shutdown ]]; then
            shutdown_recovery_disposition=verified-shutdown-after-retry
        fi
    fi
    summary="BUSTER_IOS_SHUTDOWN_RECOVERY_POSTCONDITION simulator_udid=$udid probe_status=$shutdown_recovery_probe_status parser_status=$shutdown_recovery_parser_status state=$shutdown_recovery_state disposition=$shutdown_recovery_disposition"
    if ! printf '%s\n' "$summary" >"$result_log"; then
        shutdown_recovery_disposition=unresolved-evidence-failure
        summary="BUSTER_IOS_SHUTDOWN_RECOVERY_POSTCONDITION simulator_udid=$udid probe_status=$shutdown_recovery_probe_status parser_status=$shutdown_recovery_parser_status state=$shutdown_recovery_state disposition=$shutdown_recovery_disposition"
        echo "error: could not retain iOS shutdown recovery postcondition result at $result_log" >&2
    fi
    printf '%s\n' "$summary" >&2
    [[ $shutdown_recovery_disposition == verified-shutdown-after-retry ]]
}

recover_shutdown_after_non_shutdown_postcondition() {
    local result_log="${console_log_base}.shutdown-recovery.result.log"
    local initial_probe_status=$shutdown_postcondition_probe_status
    local initial_parser_status=$shutdown_postcondition_parser_status
    local initial_state=$shutdown_postcondition_state
    local final_verified=0
    local evidence_status=0
    local summary

    shutdown_recovery_status=not-run
    shutdown_recovery_outcome=not-run
    shutdown_recovery_probe_status=not-run
    shutdown_recovery_parser_status=not-run
    shutdown_recovery_state=unavailable
    shutdown_recovery_disposition=unresolved-failure
    echo "Retrying shutdown once for invocation-owned iOS simulator $udid" >&2
    if run_lifecycle_phase shutdown-recovery batch "$console_log_base" \
        "$shutdown_timeout_seconds" xcrun simctl shutdown "$udid"; then
        shutdown_recovery_status=0
        shutdown_recovery_outcome=$last_lifecycle_outcome
    else
        shutdown_recovery_status=$?
        shutdown_recovery_outcome=$last_lifecycle_outcome
    fi
    if [[ $shutdown_recovery_status == 0 || $shutdown_recovery_outcome == timeout ]]; then
        if verify_shutdown_recovery_postcondition; then
            final_verified=1
        fi
        shutdown_postcondition_probe_status=$shutdown_recovery_probe_status
        shutdown_postcondition_parser_status=$shutdown_recovery_parser_status
        shutdown_postcondition_state=$shutdown_recovery_state
        if [[ $final_verified -eq 1 ]]; then
            shutdown_disposition=recovered-shutdown-after-timeout
        fi
    fi
    summary="BUSTER_IOS_SHUTDOWN_RECOVERY simulator_udid=$udid eligibility=$shutdown_postcondition_eligible initial_probe_status=$initial_probe_status initial_parser_status=$initial_parser_status initial_state=$initial_state retry_status=$shutdown_recovery_status retry_outcome=$shutdown_recovery_outcome final_probe_status=$shutdown_recovery_probe_status final_parser_status=$shutdown_recovery_parser_status final_state=$shutdown_recovery_state disposition=$shutdown_disposition"
    if ! printf '%s\n' "$summary" >"$result_log"; then
        evidence_status=1
        shutdown_disposition=unresolved-evidence-failure
        shutdown_recovery_disposition=unresolved-evidence-failure
        summary="BUSTER_IOS_SHUTDOWN_RECOVERY simulator_udid=$udid eligibility=$shutdown_postcondition_eligible initial_probe_status=$initial_probe_status initial_parser_status=$initial_parser_status initial_state=$initial_state retry_status=$shutdown_recovery_status retry_outcome=$shutdown_recovery_outcome final_probe_status=$shutdown_recovery_probe_status final_parser_status=$shutdown_recovery_parser_status final_state=$shutdown_recovery_state disposition=$shutdown_disposition"
        echo "error: could not retain iOS shutdown recovery result at $result_log" >&2
    fi
    printf '%s\n' "$summary" >&2
    [[ $evidence_status -eq 0 && $shutdown_disposition == recovered-shutdown-after-timeout ]]
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
explicit_simulator_udid=0
if [[ -n $udid ]]; then
    explicit_simulator_udid=1
fi
simulator_owned=0
runtime=
device_type=
boot_recovery_eligible=0
shutdown_postcondition_eligible=0
shutdown_postcondition_probe_status=not-run
shutdown_postcondition_parser_status=not-run
shutdown_postcondition_state=unavailable
shutdown_disposition=not-attempted
shutdown_recovery_status=not-run
shutdown_recovery_outcome=not-run
shutdown_recovery_probe_status=not-run
shutdown_recovery_parser_status=not-run
shutdown_recovery_state=unavailable
shutdown_recovery_disposition=not-attempted
boot_disposition=unresolved
last_lifecycle_outcome=unavailable
pending_create_log=
pending_create_owned=0
active_launch_stream_pid=
active_launch_reader_pid=
active_launch_pipe_dir=

cleanup() {
    local status=$?
    local prior_status=$status
    local shutdown_status
    local shutdown_outcome=unavailable
    local cleanup_summary
    trap - EXIT INT TERM
    if [[ -n ${active_launch_stream_pid:-}${active_launch_reader_pid:-}${active_launch_pipe_dir:-} ]]; then
        stop_launch_stream "$active_launch_stream_pid"
        active_launch_stream_pid=
    fi
    # A cancellation can interrupt the bounded create phase after simctl has
    # emitted the replacement UUID but before the phase function returns.
    # Adopt only a canonical identity from that invocation-owned evidence.
    adopt_pending_create_identity
    # Always shut down the selected device. Batch mode reaches this trap once,
    # after both app bundles have had independent install/launch checks.
    if [[ -n ${udid:-} ]]; then
        echo "Shutting down iOS simulator $udid"
        if run_lifecycle_phase shutdown batch "$console_log_base" "$shutdown_timeout_seconds" xcrun simctl shutdown "$udid"; then
            shutdown_status=0
            shutdown_outcome=$last_lifecycle_outcome
            shutdown_disposition=direct-success
        else
            shutdown_status=$?
            shutdown_outcome=$last_lifecycle_outcome
            if [[ $shutdown_outcome == timeout && $shutdown_postcondition_eligible -eq 1 ]]; then
                if verify_shutdown_postcondition; then
                    echo "warning: iOS simulator shutdown command timed out, but the exact invocation-owned device reached Shutdown" >&2
                elif [[ $prior_status -eq 0 \
                    && $shutdown_postcondition_probe_status == 0 \
                    && $shutdown_postcondition_parser_status == 4 \
                    && $shutdown_postcondition_state == non-shutdown ]] \
                    && recover_shutdown_after_non_shutdown_postcondition; then
                    echo "warning: iOS simulator remained non-Shutdown after the first timeout, but reached Shutdown after one bounded exact-device retry" >&2
                else
                    echo "warning: failed to verify or recover the exact iOS simulator shutdown postcondition after timeout" >&2
                    if [[ $status -eq 0 ]]; then
                        status=1
                    fi
                fi
            else
                shutdown_disposition=unresolved-failure
                echo "warning: failed to shut down iOS simulator $udid" >&2
                if [[ $status -eq 0 ]]; then
                    status=1
                fi
            fi
        fi
        cleanup_summary="BUSTER_IOS_CLEANUP simulator_udid=$udid prior_status=$prior_status shutdown_status=$shutdown_status shutdown_outcome=$shutdown_outcome postcondition_eligibility=$shutdown_postcondition_eligible postcondition_probe_status=$shutdown_postcondition_probe_status postcondition_parser_status=$shutdown_postcondition_parser_status postcondition_state=$shutdown_postcondition_state shutdown_disposition=$shutdown_disposition result_status=$status"
        printf '%s\n' "$cleanup_summary" >>"${console_log_base}.shutdown.status.log" || true
        printf '%s\n' "$cleanup_summary" >&2
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
    simulator_owned=1
fi

echo "Using simulator $device_name ($udid)"

# Keep the existing cleanup policy for stale unavailable devices. Do not shut
# down all simulators here: the selected simulator is shut down once by the EXIT
# trap after the batch, and an already-booted selected device can be reused.
run_with_timeout "$monitor_command_timeout_seconds" xcrun simctl delete unavailable 2>/dev/null || true

run_boot_readiness_attempt() {
    local attempt=$1
    local evidence_base="${console_log_base}.boot.attempt-${attempt}"
    local started=$SECONDS
    local boot_status=0
    local readiness_status=0
    local readiness_outcome=unavailable
    local continued=0 first_readiness_status=unavailable first_readiness_outcome=unavailable

    # Keep boot and readiness evidence attempt-qualified. Recovery is decided
    # only from the readiness helper's proven deadline outcome below.
    if run_lifecycle_phase boot "$attempt" "$evidence_base" "$boot_timeout_seconds" \
        xcrun simctl boot "$udid"; then
        boot_status=0
    else
        boot_status=$?
        echo "iOS simulator boot attempt ${attempt} returned status ${boot_status}; continuing to bootstatus (it may already be booted)" >&2
    fi
    if run_lifecycle_phase bootstatus "$attempt" "$evidence_base" "$boot_timeout_seconds" \
        xcrun simctl bootstatus "$udid" -b; then
        readiness_status=0
    else
        readiness_status=$?
        readiness_outcome=$last_lifecycle_outcome
    fi
    # A caller that shortened the first readiness deadline keeps its chosen
    # recovery budget unless it explicitly opts into a continuation as well.
    if [[ $attempt -eq 1 && $boot_recovery_eligible -eq 1 && $readiness_outcome == timeout \
        && ( $boot_timeout_seconds -ge 180 || -n ${BUSTER_IOS_BOOT_CONTINUATION_SECONDS:-} ) ]]; then
        first_readiness_status=$readiness_status
        first_readiness_outcome=$readiness_outcome
        continued=1
        echo "Continuing readiness on the same owned simulator $udid for at most ${boot_continuation_seconds}s" >&2
        if run_lifecycle_phase bootstatus-continue "$attempt" "$evidence_base" "$boot_continuation_seconds" \
            xcrun simctl bootstatus "$udid" -b; then
            readiness_status=0
            readiness_outcome=success-after-continuation
        else
            readiness_status=$?
            readiness_outcome=$last_lifecycle_outcome
        fi
        printf 'BUSTER_IOS_BOOT_CONTINUATION attempt=%s udid=%s initial_status=%s initial_outcome=%s status=%s outcome=%s deadline_seconds=%s\n' \
            "$attempt" "$udid" "$first_readiness_status" "$first_readiness_outcome" \
            "$readiness_status" "$readiness_outcome" "$boot_continuation_seconds" >&2
    fi
    if [[ $readiness_status -eq 0 && $continued -eq 0 ]]; then
        readiness_outcome=success
    fi
    boot_attempt_boot_status=$boot_status
    boot_attempt_readiness_status=$readiness_status
    boot_attempt_readiness_outcome=$readiness_outcome
    boot_attempt_continued=$continued
    boot_attempt_elapsed=$((SECONDS - started))
    printf 'BUSTER_IOS_BOOT_ATTEMPT attempt=%s udid=%s boot_status=%s readiness_status=%s readiness_outcome=%s elapsed_seconds=%s evidence_base=%s\n' \
        "$attempt" "$udid" "$boot_status" "$readiness_status" "$readiness_outcome" \
        "$boot_attempt_elapsed" "$evidence_base" >&2
    echo "TIMING_IOS boot_seconds=$boot_attempt_elapsed attempt=$attempt" >&2
    return "$readiness_status"
}

recover_boot_readiness() {
    local old_udid=$udid
    local recovery_evidence_base="${console_log_base}.boot-recovery"
    local shutdown_status=0
    local delete_status=0
    local create_status=0
    local replacement_udid=
    local recovery_status=1
    local create_log="${recovery_evidence_base}.recovery-create.log"

    # This path is reached only for an invocation-created hosted ARM64 device;
    # each destructive/device-creation command has its own lifecycle deadline.
    echo "iOS simulator readiness timed out on owned hosted ARM64 device $old_udid; attempting one bounded replacement" >&2
    if run_lifecycle_phase recovery-shutdown attempt-1 "$recovery_evidence_base" "$shutdown_timeout_seconds" \
        xcrun simctl shutdown "$old_udid"; then
        shutdown_status=0
    else
        shutdown_status=$?
        echo "error: could not shut down the first iOS simulator before recovery" >&2
    fi
    if [[ $shutdown_status -eq 0 ]]; then
        if run_lifecycle_phase recovery-delete attempt-1 "$recovery_evidence_base" "$shutdown_timeout_seconds" \
            xcrun simctl delete "$old_udid"; then
            delete_status=0
            udid=
        else
            delete_status=$?
            echo "error: could not delete the first iOS simulator during recovery" >&2
        fi
    fi
    if [[ $shutdown_status -eq 0 && $delete_status -eq 0 ]]; then
        pending_create_log=$create_log
        pending_create_owned=1
        if run_lifecycle_phase recovery-create attempt-1 "$recovery_evidence_base" "$boot_timeout_seconds" \
            xcrun simctl create "$device_name" "$device_type" "$runtime"; then
            create_status=0
        else
            create_status=$?
            echo "error: could not create a replacement iOS simulator during recovery" >&2
        fi
    fi
    # Also run this after a normal phase return; cleanup runs the same strict
    # parser if cancellation interrupts the phase before reaching this point.
    adopt_pending_create_identity
    if [[ -n $udid && $udid != "$old_udid" ]]; then
        replacement_udid=$udid
    fi
    if [[ $shutdown_status -eq 0 && $delete_status -eq 0 && $create_status -eq 0 ]]; then
        if [[ -z $replacement_udid ]]; then
            echo "error: replacement simulator creation did not return a valid device identity" >&2
            create_status=1
        else
            echo "Created replacement iOS simulator $udid using runtime=$runtime device_type=$device_type" >&2
            if run_boot_readiness_attempt 2; then
                recovery_status=0
            else
                echo "error: replacement iOS simulator did not become ready on the one permitted retry" >&2
            fi
        fi
    fi
    printf 'BUSTER_IOS_BOOT_RECOVERY attempt=1 old_udid=%s shutdown_status=%s delete_status=%s create_status=%s replacement_udid=%s readiness_status=%s result=%s\n' \
        "$old_udid" "$shutdown_status" "$delete_status" "$create_status" \
        "${replacement_udid:-unavailable}" "${boot_attempt_readiness_status:-unavailable}" \
        "$recovery_status" >&2
    return "$recovery_status"
}

if [[ $simulator_owned -eq 1 && $explicit_simulator_udid -eq 0 \
    && ${GITHUB_ACTIONS:-false} == true && ${RUNNER_ENVIRONMENT:-} == github-hosted \
    && ${RUNNER_OS:-} == macOS \
    && ${RUNNER_ARCH:-} == ARM64 && ${BUSTER_IOS_ARCH:-arm64} == arm64 ]]; then
    boot_recovery_eligible=1
    shutdown_postcondition_eligible=1
fi
printf 'BUSTER_IOS_BOOT_RECOVERY eligibility=%s owned=%s explicit_udid=%s github_actions=%s runner_environment=%s runner_os=%s runner_arch=%s ios_arch=%s\n' \
    "$boot_recovery_eligible" "$simulator_owned" "$explicit_simulator_udid" \
    "${GITHUB_ACTIONS:-false}" "${RUNNER_ENVIRONMENT:-unavailable}" \
    "${RUNNER_OS:-unavailable}" "${RUNNER_ARCH:-unavailable}" \
    "${BUSTER_IOS_ARCH:-default}" >&2

if run_boot_readiness_attempt 1; then
    if [[ $boot_attempt_continued -eq 1 ]]; then
        boot_disposition=continued-original-pending-tests
    else
        boot_disposition=first-attempt-success
    fi
else
    first_readiness_status=$?
    if [[ $boot_recovery_eligible -eq 1 && $boot_attempt_readiness_outcome == timeout ]]; then
        if recover_boot_readiness; then
            boot_disposition=recovered-infrastructure-pending-tests
        else
            boot_disposition=unrecovered-failure
            echo "error: iOS simulator boot recovery failed; both readiness attempts and recovery statuses are retained" >&2
            echo "BUSTER_IOS_BOOT_DISPOSITION=$boot_disposition" >&2
            exit 1
        fi
    else
        if [[ $boot_recovery_eligible -eq 0 ]]; then
            echo "iOS simulator boot recovery not eligible for this device/invocation; preserving established failure behavior" >&2
        elif [[ $boot_attempt_readiness_outcome != timeout ]]; then
            echo "iOS simulator readiness failed with outcome=$boot_attempt_readiness_outcome; no recovery is permitted" >&2
        fi
        if [[ $boot_attempt_readiness_outcome == timeout ]]; then
            echo "error: iOS simulator did not become ready within ${boot_timeout_seconds}s" >&2
        else
            echo "error: iOS simulator readiness command failed with outcome=$boot_attempt_readiness_outcome" >&2
        fi
        print_simulator_diagnostics
        boot_disposition=unrecovered-failure
        echo "BUSTER_IOS_BOOT_DISPOSITION=$boot_disposition" >&2
        exit "$first_readiness_status"
    fi
fi

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
    : >"$console_log"

    echo "Codesigning iOS ${label} app bundle"
    if ! run_lifecycle_phase codesign "$label" "$console_log" "$codesign_timeout_seconds" \
        codesign --force --sign - --timestamp=none "$app_bundle"; then
        echo "error: codesign failed for iOS ${label} app bundle" >&2
        return 1
    fi

    echo "Installing iOS ${label} app bundle: $app_bundle"
    install_started=$SECONDS
    if ! run_lifecycle_phase install "$label" "$console_log" "$install_timeout_seconds" \
        xcrun simctl install "$udid" "$app_bundle"; then
        echo "error: failed to install iOS ${label} app bundle (outcome=$last_lifecycle_outcome)" >&2
        print_simulator_diagnostics
        return 1
    fi
    echo "TIMING_IOS install_seconds label=$label value=$((SECONDS - install_started))"

    echo "Launching $bundle_id for iOS ${label} (timeout ${launch_timeout_seconds}s)"
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

if [[ $boot_disposition == recovered-infrastructure-pending-tests ]]; then
    if [[ $overall_status -eq 0 ]]; then
        boot_disposition=recovered-infrastructure-failure
    else
        boot_disposition=recovered-boot-but-test-failure
    fi
elif [[ $boot_disposition == first-attempt-success && $overall_status -ne 0 ]]; then
    boot_disposition=first-attempt-boot-success-but-test-failure
elif [[ $boot_disposition == continued-original-pending-tests ]]; then
    if [[ $overall_status -eq 0 ]]; then
        boot_disposition=continued-original-boot-success
    else
        boot_disposition=continued-boot-but-test-failure
    fi
fi
printf 'BUSTER_IOS_BOOT_DISPOSITION=%s\n' "$boot_disposition" >&2

exit "$overall_status"
