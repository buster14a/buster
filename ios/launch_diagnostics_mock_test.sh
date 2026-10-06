#!/usr/bin/env bash
# Exercise process ownership without Xcode, using real timeout/process groups.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
python3 "$repo_root/ios/lifecycle_capture_test.py" -v
python3 "$repo_root/ios/lifecycle_capture_bridge_test.py" -v
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-ios-monitor.XXXXXX")
runner=
cleanup() {
    local status=$?
    local path kind pid
    trap - EXIT INT TERM
    if [[ -n $runner ]]; then
        kill -TERM "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
    fi
    # Clean up the known fake processes even when testing a broken baseline.
    for path in "$test_root"/*/pids; do
        [[ -f $path ]] || continue
        while read -r kind pid; do
            if [[ $kind == timeout ]]; then
                kill -TERM -- -"$pid" 2>/dev/null || true
            else
                kill -TERM "$pid" 2>/dev/null || true
            fi
        done <"$path"
    done
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$test_root/bin"
export REAL_TEE
REAL_TEE=$(command -v tee)
cat >"$test_root/bin/tee" <<'TOOL'
#!/usr/bin/env bash
printf 'reader %s\n' "$$" >>"$FAKE_PIDS"
exec "$REAL_TEE" "$@"
TOOL
cat >"$test_root/bin/codesign" <<'TOOL'
#!/usr/bin/env bash
exit 0
TOOL
cat >"$test_root/bin/xcrun" <<'TOOL'
#!/usr/bin/env bash
set -eu
if [[ ${1:-} == simctl && ${2:-} == spawn ]]; then
    printf 'fake diagnostic %s: simulator command output\n' "${4:-}" >&2
    case "${FAKE_DIAGNOSTIC_MODE:-success}" in
        reject) exit 70 ;;
        native-124) exit 124 ;;
        timeout) exec sleep 60 ;;
        large)
            python3 -c 'import sys; sys.stdout.write("X" * 131072)'
            exit 71
            ;;
    esac
    exit 0
fi
if [[ ${1:-} == simctl && ${2:-} == launch ]]; then
    if [[ $# -ne 8 || ${6:-} != test || ${7:-} != --verbose=1 || ${8:-} != --ci=1 ]]; then
        echo "incorrect iOS payload arguments: $*" >&2
        exit 97
    fi
    printf 'timeout %s\nproducer %s\n' "$(ps -p "$$" -o ppid= | tr -d ' ')" "$$" >>"$FAKE_PIDS"
    if [[ $FAKE_RESULT != empty ]]; then
        printf 'TEST_MODULE_TIMING index=28 module=x86_64_forwarding_tests duration_ns=1 passed=1 failed=0 assertions=1 status=pass\n'
        printf 'TEST_ARENA_V1 kind=fixture module=x86_64_metadata_tests fixture=first index=0\n'
    fi
    case "$FAKE_RESULT" in
        success) printf 'BUSTER_IOS_RESULT: SUCCESS\n' ;;
        failure) printf 'BUSTER_IOS_RESULT: FAILURE\n' ;;
        hang|empty) : ;;
    esac
    # The same process stays attached after its terminal marker.
    exec sleep 60
fi
exit 0
TOOL
chmod +x "$test_root/bin/tee" "$test_root/bin/codesign" "$test_root/bin/xcrun"
export PATH="$test_root/bin:$PATH"
run_case() {
    local label=$1 outcome=$2 expected=$3 interrupt=$4 bundles=$5
    local diagnostic_mode=${6:-success}
    local state="$test_root/$label" status=0 deadline kind pid probe status_log output_log expected_probe expected_progress
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    export RUNNER_TEMP="$state" FAKE_PIDS="$state/pids" FAKE_RESULT="$outcome"
    export FAKE_DIAGNOSTIC_MODE="$diagnostic_mode"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    local arguments=(--batch Debug "$state/Debug/ide.app")
    if [[ $bundles == 2 ]]; then
        arguments+=(Release "$state/Release/ide.app")
    fi
    /bin/bash "$launcher" "${arguments[@]}" >"$state/output" 2>&1 &
    runner=$!
    if [[ $interrupt == 1 ]]; then
        deadline=$((SECONDS + 10))
        while ! grep -q '^producer ' "$state/pids" 2>/dev/null; do
            if (( SECONDS >= deadline )); then
                echo "launcher did not start for interrupt test" >&2
                exit 1
            fi
            sleep 0.1
        done
        kill -TERM "$runner"
    fi
    wait "$runner" || status=$?
    runner=
    if [[ $status -ne $expected ]]; then
        cat "$state/output" >&2
        echo "unexpected status for $label: $status, expected $expected" >&2
        exit 1
    fi
    [[ -f $state/pids ]]
    [[ $(grep -c '^producer ' "$state/pids") -eq $bundles ]]
    [[ $(grep -c '^reader ' "$state/pids") -eq $bundles ]]
    while read -r kind pid; do
        if kill -0 "$pid" 2>/dev/null; then
            cat "$state/output" >&2
            echo "$label leaked $kind PID $pid" >&2
            exit 1
        fi
    done <"$state/pids"
    if find "$state" -name 'buster-ios-stream.*' | grep -q .; then
        echo "$label leaked its FIFO directory" >&2
        exit 1
    fi
    if [[ $expected == 1 ]]; then
        expected_progress='last_completed_module=x86_64_forwarding_tests last_completed_index=28 last_observed_module=x86_64_metadata_tests'
        if [[ $outcome == empty ]]; then
            expected_progress='last_completed_module=unavailable last_completed_index=unavailable last_observed_module=unavailable'
        fi
        if ! grep -qF "$expected_progress" "$state/output"; then
            cat "$state/output" >&2
            echo "$label did not report completed/observed module progress" >&2
            exit 1
        fi
        case "$diagnostic_mode" in
            success) expected_probe='outcome=success status=0 native_status=0 capture_status=0' ;;
            reject) expected_probe='outcome=command-failure status=70 native_status=70 capture_status=0' ;;
            native-124) expected_probe='outcome=command-failure status=124 native_status=124 capture_status=0' ;;
            timeout) expected_probe='outcome=timeout status=124 native_status=143 capture_status=0' ;;
            large) expected_probe='outcome=command-failure status=71 native_status=71 capture_status=0' ;;
        esac
        for probe in process-table unified-log crash-reports; do
            output_log="$state/buster-ios-console.Debug.log.diagnostic-$probe.log"
            status_log="$state/buster-ios-console.Debug.log.diagnostic-$probe.status.log"
            grep -qF "$expected_probe" "$status_log"
            grep -qF 'deadline_seconds=1 output_limit_bytes=65536' "$status_log"
            grep -qF 'capture_receipt=complete' "$status_log"
            [[ $(wc -c <"$output_log") -le 65536 ]]
            if [[ $diagnostic_mode != success ]]; then
                grep -qF "warning: iOS diagnostic unavailable name=$probe" "$state/output"
                grep -qF 'fake diagnostic' "$output_log"
            fi
            if [[ $diagnostic_mode == large ]]; then
                grep -qF 'retained_bytes=65536 truncated=1' "$status_log"
            fi
        done
    fi
    rm -f "$state/pids"
    printf 'iOS monitor cleanup passed: %s\n' "$label"
}
run_case success success 0 0 1
run_case failure failure 1 0 1
run_case timeout hang 1 0 1
run_case no-progress empty 1 0 1
run_case rejected-probes hang 1 0 1 reject
run_case native-124-probes hang 1 0 1 native-124
run_case timed-out-probes hang 1 0 1 timeout
run_case large-probes hang 1 0 1 large
run_case interrupted hang 143 1 1
run_case batch success 0 0 2
