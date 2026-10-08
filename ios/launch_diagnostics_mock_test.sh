#!/usr/bin/env bash
# Exercise process ownership without Xcode using real launcher timeouts.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
python3 "$repo_root/ios/lifecycle_capture_test.py" -v
python3 "$repo_root/ios/lifecycle_capture_bridge_test.py" -v
timeout_bin=$(python3 "$repo_root/ios/gnu_timeout.py")
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-ios-monitor.XXXXXX")
runner=
MOCK_ACTIVE_ACK_STATE=
mock_report_owner_state() {
    local state=$1 role token directory done_state received_state lifetime_state trace_line trace_count
    if [[ ! -f $state/processes ]]; then
        printf 'mock owner state: no process registry at %s\n' "$state" >&2
        return
    fi
    while read -r role token; do
        if [[ -n $token && $token == owner.* && $token != */* ]]; then
            directory="$state/control/$token"
            done_state=missing
            received_state=missing
            if [[ -f $directory/done && ! -L $directory/done ]]; then
                done_state=present
            fi
            if [[ -f $directory/received && ! -L $directory/received ]]; then
                received_state=present
            fi
            lifetime_state=missing
            if [[ -p $directory/lifetime && ! -L $directory/lifetime ]]; then
                lifetime_state=present
            fi
            printf 'mock owner state: role=%s token=%s done=%s received=%s lifetime_fifo=%s\n' \
                "$role" "$token" "$done_state" "$received_state" "$lifetime_state" >&2
            if [[ -f $directory/trace && ! -L $directory/trace ]]; then
                trace_count=0
                while IFS= read -r trace_line; do
                    trace_count=$((trace_count + 1))
                    if (( trace_count <= 67 )); then
                        printf 'mock owner trace: %s\n' "$trace_line" >&2
                    else
                        printf 'mock owner trace: truncated after 67 events\n' >&2
                        break
                    fi
                done <"$directory/trace"
            else
                printf 'mock owner trace: missing\n' >&2
            fi
        else
            printf 'mock owner state: invalid registry entry role=%s token=%s\n' \
                "$role" "$token" >&2
        fi
    done <"$state/processes"
}
mock_lifetime_probe() {
    local state=$1 role=$2 token=$3 timeout=$4 directory path response read_status
    local registered_role registered_token registration_count=0
    [[ -n $token && $token == owner.* && $token != */* ]] || return 2
    [[ -d $state/control && ! -L $state/control ]] || return 2
    [[ -f $state/processes && ! -L $state/processes ]] || return 2
    directory="$state/control/$token"
    [[ -d $directory && ! -L $directory ]] || return 2
    path="$directory/lifetime"
    [[ -p $path && ! -L $path ]] || return 2
    while read -r registered_role registered_token; do
        if [[ $registered_token == "$token" ]]; then
            [[ $registered_role == "$role" ]] || return 2
            registration_count=$((registration_count + 1))
        fi
    done <"$state/processes"
    [[ $registration_count == 1 ]] || return 2

    # Keep read-only open nonblocking even when the owner has already exited.
    # Parent FD 3 is the temporary keeper; FD 8 is the read-only observer.
    exec 3<> "$path" || return 2
    if [[ ! $path -ef /dev/fd/3 ]]; then
        exec 3>&-
        return 2
    fi
    if ! exec 8< "$path"; then
        exec 3>&-
        return 2
    fi
    if [[ ! $path -ef /dev/fd/8 ]]; then
        exec 8<&-
        exec 3>&-
        return 2
    fi
    exec 3>&-
    response=
    if IFS= read -r -t "$timeout" -u 8 response; then
        read_status=0
    else
        read_status=$?
    fi
    exec 8<&-
    if (( read_status == 1 )) && [[ -z $response ]]; then
        return 0
    fi
    if (( read_status > 128 )) && [[ -z $response ]]; then
        return 1
    fi
    return 2
}
mock_wait_owner_exits() {
    local state=$1 deadline=$2 role token remaining probe_status
    [[ -f $state/processes && ! -L $state/processes ]] || return 2
    while read -r role token; do
        remaining=$((deadline - SECONDS))
        (( remaining > 0 )) || return 1
        if mock_lifetime_probe "$state" "$role" "$token" "$remaining"; then
            :
        else
            probe_status=$?
            if (( probe_status == 1 )); then
                return 1
            fi
            return 2
        fi
    done <"$state/processes"
}
mock_send_release() {
    local directory=$1
    [[ -p $directory/release ]] || return 1
    exec 3<> "$directory/release" || return 1
    if ! printf 'release\n' >&3; then
        exec 3>&-
        return 1
    fi
    exec 3>&-
}
mock_wait_acknowledgments() {
    local state=$1 deadline=$2 pending=0 role token response remaining directory found recorded_role recorded_token
    [[ -f $state/processes ]] || return 0
    while read -r role token; do
        [[ -n $token && $token == owner.* && $token != */* ]] || return 1
        directory="$state/control/$token"
        if [[ -f $directory/received && ! -L $directory/received ]]; then
            continue
        fi
        pending=$((pending + 1))
    done <"$state/processes"
    while (( pending > 0 )); do
        remaining=$((deadline - SECONDS))
        (( remaining > 0 )) || return 1
        response=
        if ! IFS= read -r -t "$remaining" -u 9 response; then
            return 1
        fi
        [[ -n $response && $response == owner.* && $response != */* ]] || return 1
        directory="$state/control/$response"
        [[ -d $directory && ! -L $directory ]] || return 1
        [[ -f $directory/done && ! -L $directory/done ]] || return 1
        [[ ! -e $directory/received && ! -L $directory/received ]] || return 1
        found=0
        while read -r recorded_role recorded_token; do
            [[ $recorded_token == "$response" ]] && found=1
        done <"$state/processes"
        [[ $found == 1 ]] || return 1
        : >"$directory/received" || return 1
        pending=$((pending - 1))
    done
}
mock_release_all() {
    local path role token directory deadline
    MOCK_CLEANUP_INCOMPLETE=0
    for path in "$test_root"/*/processes; do
        [[ -f $path ]] || continue
        [[ $path == "$MOCK_ACTIVE_ACK_STATE/processes" ]] || {
            MOCK_CLEANUP_INCOMPLETE=1
            continue
        }
        while read -r role token; do
            [[ -n $token && $token == owner.* && $token != */* ]] || {
                MOCK_CLEANUP_INCOMPLETE=1
                continue
            }
            directory="$MOCK_ACTIVE_ACK_STATE/control/$token"
            [[ -d $directory && ! -L $directory && -p $directory/release && ! -L $directory/release ]] || {
                MOCK_CLEANUP_INCOMPLETE=1
                continue
            }
            if [[ ! -f $directory/done || -L $directory/done ]]; then
                mock_send_release "$directory" || MOCK_CLEANUP_INCOMPLETE=1
            fi
        done <"$path"
    done
    if [[ -n $MOCK_ACTIVE_ACK_STATE && -f $MOCK_ACTIVE_ACK_STATE/processes ]]; then
        deadline=$((SECONDS + 3))
        if ! mock_wait_owner_exits "$MOCK_ACTIVE_ACK_STATE" "$deadline"; then
            MOCK_CLEANUP_INCOMPLETE=1
        fi
    fi
}
cleanup() {
    local status=$? runner_status=0
    trap - EXIT INT TERM
    mock_release_all
    if [[ -n $runner ]]; then
        # This job is owned by the verified GNU timeout helper; wait never signals it.
        wait "$runner" || runner_status=$?
        runner=
        [[ $status -ne 0 || $runner_status -eq 0 ]] || status=$runner_status
    fi
    if [[ $MOCK_CLEANUP_INCOMPLETE == 1 ]]; then
        echo "mock cleanup did not observe bounded lifetime EOF for every owner; retaining $test_root" >&2
        if [[ -n $MOCK_ACTIVE_ACK_STATE ]]; then
            mock_report_owner_state "$MOCK_ACTIVE_ACK_STATE"
        fi
        [[ $status -ne 0 ]] || status=1
    else
        rm -rf "$test_root"
    fi
    exit "$status"
}
# Keep parent channels below Bash's >=10 saved-redirection descriptor range:
# Parent FD 3 releases owners/keeps lifetime FIFOs open, FD 4 observes reader
# copies, FD 5 registers fixtures, FD 6 holds reader input, FD 8 observes owner
# lifetime EOF, and FD 9 consumes cooperative-release acknowledgments.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$test_root/bin"
cat >"$test_root/bin/mock-control.sh" <<'TOOL'
#!/usr/bin/env bash
mock_trace_event() {
    local event=$1
    if [[ $event != finalizer-* ]]; then
        MOCK_TRACE_COUNT=$((MOCK_TRACE_COUNT + 1))
        (( MOCK_TRACE_COUNT <= 64 )) || return 0
    fi
    printf 'bash=%s seconds=%s role=%s event=%s\n' \
        "$BASH_VERSION" "$SECONDS" "$MOCK_ROLE" "$event" \
        >>"$MOCK_TOKEN_DIR/trace" || true
}
mock_acknowledge_owner() {
    trap '' INT TERM
    trap - EXIT
    local status=$1
    mock_trace_event "finalizer-enter status=$status"
    if : >"$MOCK_TOKEN_DIR/done"; then
        mock_trace_event "finalizer-done-write=ok"
    else
        status=1
        mock_trace_event "finalizer-done-write=failed"
    fi
    if printf '%s\n' "$MOCK_TOKEN" >&7; then
        mock_trace_event "finalizer-ack-write=ok"
    else
        status=1
        mock_trace_event "finalizer-ack-write=failed"
    fi
    exit "$status"
}
mock_register() {
    local role=$1 token_dir
    token_dir=$(mktemp -d "$FAKE_CONTROL_DIR/owner.XXXXXX")
    MOCK_TOKEN_DIR=$token_dir
    MOCK_TOKEN=${token_dir##*/}
    MOCK_ROLE=$role
    MOCK_TRACE_COUNT=0
    mkfifo "$MOCK_TOKEN_DIR/release" "$MOCK_TOKEN_DIR/lifetime"
    exec 8<> "$MOCK_TOKEN_DIR/release"
    exec 7<> "$FAKE_ACK_FIFO"
    # No child may be started after this lifetime handle is opened: a child
    # inheriting FD 4 would postpone EOF after this owner exits.
    exec 4<> "$MOCK_TOKEN_DIR/lifetime"
    trap 'mock_acknowledge_owner "$?"' EXIT
    trap 'mock_acknowledge_owner 143' TERM
    trap 'mock_acknowledge_owner 130' INT
    printf '%s %s\n' "$role" "$MOCK_TOKEN" >>"$FAKE_PROCESSES"
    mock_trace_event "owner-register"
    if [[ -n ${FAKE_REGISTRATION_FIFO:-} ]]; then
        exec 5<> "$FAKE_REGISTRATION_FIFO"
        printf '%s %s\n' "$role" "$MOCK_TOKEN" >&5
        exec 5>&-
    fi
}
mock_wait_for_release() {
    local response status
    while :; do
        response=
        if IFS= read -r -t 1 -u 8 response; then
            [[ $response == release ]] || return 97
            return 0
        else
            status=$?
            (( status > 128 )) || return "$status"
        fi
    done
}
mock_run_long_owner() {
    local role=$1
    mock_register "$role"
    mock_wait_for_release
}
TOOL
chmod +x "$test_root/bin/mock-control.sh"
cat >"$test_root/bin/tee" <<'TOOL'
#!/usr/bin/env bash
set -euo pipefail
. "$MOCK_CONTROL_HELPER"
mock_register reader
[[ $# -eq 1 ]] || exit 97
output_file=$1
exec 9>"$output_file"
if [[ -n ${FAKE_COPY_FIFO:-} ]]; then
    exec 3>"$FAKE_COPY_FIFO"
fi
pending=
while :; do
    part=
    if IFS= read -r -t 0.1 part; then
        line=$pending$part
        printf '%s\n' "$line" >&9
        printf '%s\n' "$line"
        mock_trace_event "stdin-line-copied"
        if [[ -n ${FAKE_COPY_FIFO:-} ]]; then
            printf '%s\n' "$line" >&3
        fi
        pending=
    else
        read_status=$?
        pending+=$part
        if (( read_status > 128 )); then
            mock_trace_event "stdin-timeout status=$read_status"
            release_ready=
            if IFS= read -r -t 0 -u 8 release_ready; then
                mock_trace_event "release-ready status=0"
                release=
                if IFS= read -r -t 0.1 -u 8 release; then
                    if [[ $release == release ]]; then
                        mock_trace_event "release-read status=0 value=release"
                        break
                    fi
                    mock_trace_event "release-read status=0 value=unexpected"
                else
                    release_read_status=$?
                    mock_trace_event "release-read status=$release_read_status"
                fi
            else
                release_ready_status=$?
                mock_trace_event "release-ready status=$release_ready_status"
            fi
            continue
        fi
        mock_trace_event "stdin-eof status=$read_status"
        if [[ -n $pending ]]; then
            printf '%s' "$pending" >&9
            printf '%s' "$pending"
        fi
        break
    fi
done
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
    mock_register producer
    if [[ $FAKE_RESULT != empty ]]; then
        printf 'TEST_MODULE_TIMING index=28 module=x86_64_forwarding_tests duration_ns=1 passed=1 failed=0 assertions=1 status=pass\n'
        printf 'TEST_ARENA_V1 kind=fixture module=x86_64_metadata_tests fixture=first index=0\n'
    fi
    case "$FAKE_RESULT" in
        success) printf 'BUSTER_IOS_RESULT: SUCCESS\n' ;;
        failure) printf 'BUSTER_IOS_RESULT: FAILURE\n' ;;
        hang|empty) : ;;
    esac
    # The registered process stays attached after its terminal marker.
    mock_wait_for_release
fi
exit 0
TOOL
chmod +x "$test_root/bin/tee" "$test_root/bin/codesign" "$test_root/bin/xcrun"
export PATH="$test_root/bin:$PATH"
run_case() {
    local label=$1 outcome=$2 expected=$3 interrupt=$4 bundles=$5
    local diagnostic_mode=${6:-success}
    local state="$test_root/$label" status=0 role token registration runner_timeout probe status_log output_log expected_probe expected_progress owner_deadline owner_wait_status
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app" "$state/control"
    mkfifo "$state/acknowledgments"
    exec 9<> "$state/acknowledgments"
    MOCK_ACTIVE_ACK_STATE=$state
    export FAKE_ACK_FIFO="$state/acknowledgments"
    export RUNNER_TEMP="$state" FAKE_PROCESSES="$state/processes" FAKE_RESULT="$outcome"
    export FAKE_CONTROL_DIR="$state/control" MOCK_CONTROL_HELPER="$test_root/bin/mock-control.sh"
    export FAKE_DIAGNOSTIC_MODE="$diagnostic_mode"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    unset FAKE_REGISTRATION_FIFO
    local arguments=(--batch Debug "$state/Debug/ide.app")
    if [[ $bundles == 2 ]]; then
        arguments+=(Release "$state/Release/ide.app")
    fi
    runner_timeout=15s
    if [[ $interrupt == 1 ]]; then
        runner_timeout=2s
        mkfifo "$state/registration"
        exec 5<> "$state/registration"
        export FAKE_REGISTRATION_FIFO="$state/registration"
    fi
    "$timeout_bin" --preserve-status --signal=TERM --kill-after=3s "$runner_timeout" /bin/bash "$launcher" "${arguments[@]}" >"$state/output" 2>&1 &
    runner=$!
    if [[ $interrupt == 1 ]]; then
        deadline=2
        while :; do
            registration=
            if ! IFS= read -r -t "$deadline" -u 5 registration; then
                echo "launcher did not register its producer before interrupt" >&2
                exit 1
            fi
            IFS=' ' read -r role token <<<"$registration"
            [[ $token == owner.* && $token != */* ]]
            [[ $role == reader ]] && continue
            [[ $role == producer ]] && break
            echo "unexpected iOS mock registration role=$role" >&2
            exit 1
        done
        # Accept status 143 only after the producer registration proves that
        # the native two-second GNU timeout interrupted an attached owner.
        exec 5>&-
    fi
    wait "$runner" || status=$?
    runner=
    if [[ $status -ne $expected ]]; then
        cat "$state/output" >&2
        echo "unexpected status for $label: $status, expected $expected" >&2
        exit 1
    fi
    if [[ ! -f $state/processes ]]; then
        echo "$label did not create its owner registry" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    producer_count=$(grep -c '^producer ' "$state/processes" || true)
    reader_count=$(grep -c '^reader ' "$state/processes" || true)
    if [[ $producer_count -ne $bundles || $reader_count -ne $bundles ]]; then
        echo "$label registered producer=$producer_count reader=$reader_count; expected $bundles of each" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    while read -r role token; do
        case "$role" in
            producer|reader) ;;
            *)
                echo "$label has an unexpected registered owner role=$role" >&2
                mock_report_owner_state "$state"
                exit 1
                ;;
        esac
    done <"$state/processes"
    owner_deadline=$((SECONDS + 3))
    if mock_wait_owner_exits "$state" "$owner_deadline"; then
        :
    else
        owner_wait_status=$?
        if (( owner_wait_status == 1 )); then
            echo "$label still has an owner after the bounded lifetime wait" >&2
        else
            echo "$label failed lifetime path, inode, or registry validation" >&2
        fi
        mock_report_owner_state "$state"
        exit 1
    fi
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
    rm -f "$state/processes"
    exec 9>&-
    MOCK_ACTIVE_ACK_STATE=
    unset FAKE_ACK_FIFO FAKE_REGISTRATION_FIFO
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
run_reader_release_control() {
    local state="$test_root/reader-release" registration role token status=0 deadline
    local fresh_line copied_line copied_in_output=0 response copy_wait_status ack_probe_status lifetime_status
    mkdir -p "$state/control"
    mkfifo "$state/acknowledgments" "$state/registration" "$state/input" "$state/copied"
    exec 9<> "$state/acknowledgments"
    exec 5<> "$state/registration"
    exec 6<> "$state/input"
    exec 4<> "$state/copied"
    MOCK_ACTIVE_ACK_STATE=$state
    export FAKE_ACK_FIFO="$state/acknowledgments"
    export FAKE_CONTROL_DIR="$state/control" FAKE_PROCESSES="$state/processes"
    export MOCK_CONTROL_HELPER="$test_root/bin/mock-control.sh"
    export FAKE_REGISTRATION_FIFO="$state/registration"
    export FAKE_COPY_FIFO="$state/copied"
    "$timeout_bin" --preserve-status --signal=TERM --kill-after=3s 10s "$test_root/bin/tee" "$state/output" <"$state/input" >"$state/stdout" 2>&1 &
    runner=$!
    registration=
    if ! IFS= read -r -t 5 -u 5 registration; then
        echo "fake reader did not register before release" >&2
        exit 1
    fi
    IFS=' ' read -r role token <<<"$registration"
    [[ $role == reader && $token == owner.* && $token != */* ]]

    # Keep the input writer open, but send neither a line nor a release. This
    # spans at least one 100ms stdin poll while the release FIFO is empty.
    copied_line=
    if IFS= read -r -t 0.25 -u 4 copied_line; then
        echo "fake reader copied data before receiving the fresh control line" >&2
        mock_report_owner_state "$state"
        exit 1
    else
        copy_wait_status=$?
        if (( copy_wait_status <= 128 )); then
            echo "fake reader copy observer failed during the idle control window" >&2
            mock_report_owner_state "$state"
            exit 1
        fi
    fi
    if mock_lifetime_probe "$state" "$role" "$token" 0.25; then
        echo "fake reader lifetime closed before release" >&2
        mock_report_owner_state "$state"
        exit 1
    else
        lifetime_status=$?
        if (( lifetime_status != 1 )); then
            echo "fake reader lifetime observer did not time out for a live owner" >&2
            mock_report_owner_state "$state"
            exit 1
        fi
    fi
    response=
    if IFS= read -r -t 0 -u 9 response; then
        echo "fake reader acknowledged before receiving release during idle control" >&2
        mock_report_owner_state "$state"
        exit 1
    else
        ack_probe_status=$?
        if (( ack_probe_status != 1 && ack_probe_status <= 128 )); then
            echo "fake reader acknowledgment observer failed during the idle control window" >&2
            mock_report_owner_state "$state"
            exit 1
        fi
    fi

    # With the release FIFO still empty, a fresh line must be copied within
    # half of the launcher's one-second reader grace.
    fresh_line="reader-open-input-$token"
    if ! printf '%s\n' "$fresh_line" >&6; then
        echo "could not write the fresh open-input reader control line" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    copied_line=
    if ! IFS= read -r -t 0.5 -u 4 copied_line; then
        echo "fake reader did not copy fresh open-input line within 0.5 seconds before release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    if [[ $copied_line != "$fresh_line" ]]; then
        echo "fake reader copied an unexpected line before release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    while IFS= read -r copied_line; do
        [[ $copied_line == "$fresh_line" ]] && copied_in_output=1
    done <"$state/output"
    if [[ $copied_in_output != 1 ]]; then
        echo "fresh open-input control line was not present in the reader output" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    response=
    if IFS= read -r -t 0 -u 9 response; then
        echo "fake reader acknowledged before receiving release" >&2
        mock_report_owner_state "$state"
        exit 1
    else
        ack_probe_status=$?
        if (( ack_probe_status != 1 && ack_probe_status <= 128 )); then
            echo "fake reader acknowledgment observer failed unexpectedly before release" >&2
            mock_report_owner_state "$state"
            exit 1
        fi
    fi

    mock_send_release "$state/control/$token"
    wait "$runner" || status=$?
    runner=
    [[ $status -eq 0 ]]
    if ! mock_lifetime_probe "$state" "$role" "$token" 3; then
        echo "fake reader lifetime did not reach EOF after cooperative release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    deadline=$((SECONDS + 3))
    if ! mock_wait_acknowledgments "$state" "$deadline"; then
        echo "fake reader did not acknowledge release with its input writer still open" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    if [[ ! -f $state/control/$token/done || -L $state/control/$token/done ]]; then
        echo "fake reader completion marker is missing after acknowledgment" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    rm -f "$state/processes"
    exec 4>&-
    exec 6>&-
    exec 5>&-
    exec 9>&-
    MOCK_ACTIVE_ACK_STATE=
    unset FAKE_ACK_FIFO FAKE_REGISTRATION_FIFO FAKE_COPY_FIFO
}
run_mock_release_control() {
    local state="$test_root/release-control" registration role token response status=0
    mkdir -p "$state/control"
    mkfifo "$state/acknowledgments" "$state/registration"
    exec 9<> "$state/acknowledgments"
    exec 5<> "$state/registration"
    MOCK_ACTIVE_ACK_STATE=$state
    export FAKE_ACK_FIFO="$state/acknowledgments"
    export FAKE_CONTROL_DIR="$state/control" FAKE_PROCESSES="$state/processes"
    export MOCK_CONTROL_HELPER="$test_root/bin/mock-control.sh"
    export FAKE_REGISTRATION_FIFO="$state/registration" FAKE_RESULT=hang
    "$timeout_bin" --preserve-status --signal=TERM --kill-after=3s 10s "$test_root/bin/xcrun" simctl launch --console-pty FAKE-UDID org.buster.fixture test --verbose=1 --ci=1 >"$state/output" 2>&1 &
    runner=$!
    registration=
    if ! IFS= read -r -t 5 -u 5 registration; then
        echo "finite mock fixture did not register an owner token" >&2
        exit 1
    fi
    exec 5>&-
    IFS=' ' read -r role token <<<"$registration"
    [[ $role == producer && $token == owner.* && $token != */* ]]
    exec 3<> "$state/control/$token/release"
    printf 'release\n' >&3
    exec 3>&-
    wait "$runner" || status=$?
    runner=
    [[ $status -eq 0 ]]
    if ! mock_lifetime_probe "$state" "$role" "$token" 3; then
        echo "finite mock fixture lifetime did not reach EOF after cooperative release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    deadline=$((SECONDS + 3))
    if ! mock_wait_acknowledgments "$state" "$deadline"; then
        echo "finite mock fixture did not acknowledge its release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    if [[ ! -f $state/control/$token/done || -L $state/control/$token/done ]]; then
        echo "finite mock fixture completion marker is missing after acknowledgment" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    rm -f "$state/processes"
    exec 9>&-
    MOCK_ACTIVE_ACK_STATE=
    unset FAKE_ACK_FIFO FAKE_REGISTRATION_FIFO
}
assert_cleanup_ignores_stale_ids
run_mock_release_control
run_reader_release_control
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
