#!/usr/bin/env bash
# Exercise process ownership without Xcode using real launcher timeouts.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
python3 "$repo_root/ios/lifecycle_capture_test.py" -v
python3 "$repo_root/ios/lifecycle_capture_bridge_test.py" -v
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-ios-monitor.XXXXXX")
runner=
MOCK_CLEANUP_INCOMPLETE=0
mock_release_all() {
    local path role token directory all_done deadline
    MOCK_CLEANUP_INCOMPLETE=0
    for path in "$test_root"/*/processes; do
        [[ -f $path ]] || continue
        while read -r role token; do
            [[ -n ${token:-} && ${token:-} == owner.* && ${token:-} != */* ]] || {
                MOCK_CLEANUP_INCOMPLETE=1
                continue
            }
            directory="${path%/processes}/control/$token"
            [[ -d $directory && ! -L $directory ]] || {
                MOCK_CLEANUP_INCOMPLETE=1
                continue
            }
            if [[ ! -f $directory/done ]]; then
                : >"$directory/release"
            fi
        done <"$path"
    done
    deadline=$((SECONDS + 3))
    while :; do
        all_done=1
        for path in "$test_root"/*/processes; do
            [[ -f $path ]] || continue
            while read -r role token; do
                [[ -n ${token:-} && $token == owner.* && $token != */* ]] || {
                    all_done=0
                    continue
                }
                directory="${path%/processes}/control/$token"
                if [[ ! -f $directory/done || -L $directory/done ]]; then
                    all_done=0
                fi
            done <"$path"
        done
        [[ $all_done == 1 ]] && break
        if (( SECONDS >= deadline )); then
            MOCK_CLEANUP_INCOMPLETE=1
            break
        fi
        sleep 1
    done
}
owned_child_state() {
    local state
    state=$(ps -p "$1" -o stat= 2>/dev/null || true)
    [[ -n $state && $state != Z* ]]
}
wait_owned_child() {
    local child=$1 status=0 deadline state
    deadline=$((SECONDS + 10))
    while :; do
        if ! owned_child_state "$child"; then
            wait "$child" || status=$?
            return "$status"
        fi
        if (( SECONDS >= deadline )); then
            echo "mock harness timed out waiting for its direct child $child" >&2
            stop_owned_child "$child" || true
            return 124
        fi
        sleep 1
    done
}
stop_owned_child() {
    local child=$1 status=0 deadline
    if owned_child_state "$child"; then
        kill -TERM "$child" 2>/dev/null || true
        deadline=$((SECONDS + 3))
        while owned_child_state "$child"; do
            if (( SECONDS >= deadline )); then
                if owned_child_state "$child"; then
                    kill -KILL "$child" 2>/dev/null || true
                fi
                break
            fi
            sleep 1
        done
    fi
    wait "$child" || status=$?
    return "$status"
}
cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n $runner ]]; then
        # runner remains an unreaped direct child until stop_owned_child returns.
        stop_owned_child "$runner" || true
        runner=
    fi
    mock_release_all
    if [[ $MOCK_CLEANUP_INCOMPLETE == 1 ]]; then
        echo "mock cleanup did not receive every bounded owner acknowledgment; retaining $test_root" >&2
        [[ $status -ne 0 ]] || status=1
    else
        rm -rf "$test_root"
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$test_root/bin"
cat >"$test_root/bin/mock-control.sh" <<'TOOL'
#!/usr/bin/env bash
mock_register() {
    local role=$1 token_dir
    token_dir=$(mktemp -d "$FAKE_CONTROL_DIR/owner.XXXXXX")
    MOCK_TOKEN_DIR=$token_dir
    MOCK_TOKEN=${token_dir##*/}
    printf '%s %s\n' "$role" "$MOCK_TOKEN" >>"$FAKE_PROCESSES"
}
mock_kill_owned_child() {
    local child=$1 state
    state=$(ps -p "$child" -o stat= 2>/dev/null || true)
    [[ -n $state && $state != Z* ]] || return 0
    kill -TERM "$child" 2>/dev/null || true
}
mock_cleanup_owner() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n ${MOCK_TIMER:-} ]]; then
        mock_kill_owned_child "$MOCK_TIMER"
        wait "$MOCK_TIMER" 2>/dev/null || true
        MOCK_TIMER=
    fi
    if [[ -n ${MOCK_CHILD:-} ]]; then
        mock_kill_owned_child "$MOCK_CHILD"
        wait "$MOCK_CHILD" 2>/dev/null || true
        MOCK_CHILD=
    fi
    : >"$MOCK_TOKEN_DIR/done"
    exit "$status"
}
mock_wait_child_or_release() {
    local state
    while :; do
        state=$(ps -p "$MOCK_CHILD" -o stat= 2>/dev/null || true)
        [[ -n $state && $state != Z* ]] || return 0
        if [[ -f $MOCK_TOKEN_DIR/release ]]; then
            mock_kill_owned_child "$MOCK_CHILD"
            return 0
        fi
        sleep 1 &
        MOCK_TIMER=$!
        wait "$MOCK_TIMER" || true
        MOCK_TIMER=
    done
}
mock_run_long_owner() {
    local role=$1
    MOCK_CHILD=
    MOCK_TIMER=
    mock_register "$role"
    trap mock_cleanup_owner EXIT
    trap 'exit 143' TERM
    trap 'exit 130' INT
    sleep 60 &
    MOCK_CHILD=$!
    while [[ ! -f $MOCK_TOKEN_DIR/release ]]; do
        sleep 1 &
        MOCK_TIMER=$!
        wait "$MOCK_TIMER" || true
        MOCK_TIMER=
    done
}
TOOL
chmod +x "$test_root/bin/mock-control.sh"
export REAL_TEE
REAL_TEE=$(command -v tee)
cat >"$test_root/bin/tee" <<'TOOL'
#!/usr/bin/env bash
set -eu
. "$MOCK_CONTROL_HELPER"
MOCK_CHILD=
MOCK_TIMER=
mock_register reader
trap mock_cleanup_owner EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
"$REAL_TEE" "$@" &
MOCK_CHILD=$!
status=0
mock_wait_child_or_release
wait "$MOCK_CHILD" || status=$?
MOCK_CHILD=
exit "$status"
TOOL
cat >"$test_root/bin/codesign" <<'TOOL'
#!/usr/bin/env bash
exit 0
TOOL
cat >"$test_root/bin/xcrun" <<'TOOL'
#!/usr/bin/env bash
set -eu
. "$MOCK_CONTROL_HELPER"
if [[ ${1:-} == simctl && ${2:-} == spawn ]]; then
    printf 'fake diagnostic %s: simulator command output\n' "${4:-}" >&2
    case "${FAKE_DIAGNOSTIC_MODE:-success}" in
        reject) exit 70 ;;
        native-124) exit 124 ;;
        timeout) mock_run_long_owner diagnostic ;;
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
    mock_run_long_owner producer
fi
exit 0
TOOL
chmod +x "$test_root/bin/tee" "$test_root/bin/codesign" "$test_root/bin/xcrun"
export PATH="$test_root/bin:$PATH"
run_case() {
    local label=$1 outcome=$2 expected=$3 interrupt=$4 bundles=$5
    local diagnostic_mode=${6:-success}
    local state="$test_root/$label" status=0 deadline role token probe status_log output_log expected_probe expected_progress
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app" "$state/control"
    export RUNNER_TEMP="$state" FAKE_PROCESSES="$state/processes" FAKE_RESULT="$outcome"
    export FAKE_CONTROL_DIR="$state/control" MOCK_CONTROL_HELPER="$test_root/bin/mock-control.sh"
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
        while ! grep -q '^producer ' "$state/processes" 2>/dev/null; do
            if (( SECONDS >= deadline )); then
                echo "launcher did not start for interrupt test" >&2
                exit 1
            fi
            sleep 0.1
        done
        if ! owned_child_state "$runner"; then
            echo "launcher exited before the interrupt control could signal its owned child" >&2
            exit 1
        fi
        kill -TERM "$runner"
    fi
    wait_owned_child "$runner" || status=$?
    runner=
    if [[ $status -ne $expected ]]; then
        cat "$state/output" >&2
        echo "unexpected status for $label: $status, expected $expected" >&2
        exit 1
    fi
    [[ -f $state/processes ]]
    [[ $(grep -c '^producer ' "$state/processes") -eq $bundles ]]
    [[ $(grep -c '^reader ' "$state/processes") -eq $bundles ]]
    while read -r role token; do
        [[ $token == owner.* && $token != */* ]]
        if [[ ! -f $state/control/$token/done || -L $state/control/$token/done ]]; then
            cat "$state/output" >&2
            echo "$label did not receive the $role owner acknowledgment" >&2
            exit 1
        fi
    done <"$state/processes"
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
    printf 'iOS monitor cleanup passed: %s\n' "$label"
}
assert_cleanup_ignores_stale_ids() {
    local legacy_root="$test_root/stale-cleanup" signal_log="$test_root/stale-cleanup-signals"
    mkdir -p "$legacy_root/old"
    printf 'timeout 424243\nproducer 424241\nreader 424242\n' >"$legacy_root/old/pids"
    (
        test_root=$legacy_root
        runner=
        SIGNAL_LOG=$signal_log
        kill() {
            printf '%s\n' "$*" >>"$SIGNAL_LOG"
        }
        cleanup
    )
    if [[ -e $signal_log ]]; then
        echo "cleanup attempted to signal stale numeric IDs" >&2
        exit 1
    fi
}
run_mock_release_control() {
    local state="$test_root/release-control" deadline role token status=0
    mkdir -p "$state/control"
    export FAKE_CONTROL_DIR="$state/control" FAKE_PROCESSES="$state/processes"
    export MOCK_CONTROL_HELPER="$test_root/bin/mock-control.sh" FAKE_RESULT=hang
    "$test_root/bin/xcrun" simctl launch --console-pty FAKE-UDID org.buster.fixture test --verbose=1 --ci=1 >"$state/output" 2>&1 &
    runner=$!
    deadline=$((SECONDS + 5))
    while [[ ! -s $state/processes ]]; do
        if (( SECONDS >= deadline )); then
            echo "finite mock fixture did not register an owner token" >&2
            exit 1
        fi
        sleep 1
    done
    read -r role token <"$state/processes"
    [[ $role == producer && $token == owner.* && $token != */* ]]
    : >"$state/control/$token/release"
    deadline=$((SECONDS + 5))
    while [[ ! -f $state/control/$token/done ]]; do
        if (( SECONDS >= deadline )); then
            echo "finite mock fixture did not acknowledge its release" >&2
            exit 1
        fi
        sleep 1
    done
    wait_owned_child "$runner" || status=$?
    runner=
    [[ $status -eq 0 ]]
    [[ -f $state/control/$token/done && ! -L $state/control/$token/done ]]
}
assert_cleanup_ignores_stale_ids
run_mock_release_control
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
