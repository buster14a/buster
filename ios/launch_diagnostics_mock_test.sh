#!/usr/bin/env bash
# Exercise process ownership without Xcode using real launcher timeouts.
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
if (( BASH_VERSINFO[0] < 4 )); then
    if [[ ${BUSTER_IOS_MOCK_BASH_REEXEC:-0} == 1 ]]; then
        printf 'iOS mock lifecycle requires Bash 4+; re-exec still ran %s\n' "$BASH_VERSION" >&2
        exit 1
    fi
    modern_bash=$(type -P bash 2>/dev/null || true)
    if [[ -z $modern_bash || ! -x $modern_bash ]]; then
        printf 'iOS mock lifecycle requires Bash 4+ for fractional read timeouts; no executable bash was found on PATH\n' >&2
        exit 1
    fi
    if ! timeout_bin=$(python3 "$repo_root/ios/gnu_timeout.py" 2>/dev/null); then
        printf 'iOS mock lifecycle requires a verified GNU timeout to bound its Bash version check\n' >&2
        exit 1
    fi
    if ! "$timeout_bin" --preserve-status --signal=TERM --kill-after=1s 2s "$modern_bash" -c '(( BASH_VERSINFO[0] >= 4 ))' >/dev/null 2>&1; then
        printf 'iOS mock lifecycle requires Bash 4+ for fractional read timeouts; could not verify %s within the bounded version check\n' "$modern_bash" >&2
        exit 1
    fi
    BUSTER_IOS_MOCK_BASH_REEXEC=1
    export BUSTER_IOS_MOCK_BASH_REEXEC
    if exec "$modern_bash" "$0" "$@"; then
        :
    else
        printf 'iOS mock lifecycle could not re-exec with verified modern Bash: %s\n' "$modern_bash" >&2
        exit 1
    fi
fi
unset BUSTER_IOS_MOCK_BASH_REEXEC 2>/dev/null || true
set -euo pipefail
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
python3 "$repo_root/ios/lifecycle_capture_test.py" -v
python3 "$repo_root/ios/lifecycle_capture_bridge_test.py" -v
timeout_bin=$(python3 "$repo_root/ios/gnu_timeout.py")
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-ios-monitor.XXXXXX")
runner=
MOCK_ACTIVE_ACK_STATE=
MOCK_CLEANUP_INCOMPLETE=0
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
mock_lifetime_refusal() {
    local role=$1 token=$2 phase=$3
    printf 'mock lifetime probe failure: role=%s token=%s phase=%s\n' \
        "$role" "$token" "$phase" >&2
    return 2
}
mock_lifetime_descriptors_match() {
    local path=$1
    # Bash -ef on /dev/fd/N can see Darwin's descriptorfs inode instead of the
    # underlying FIFO. Compare the private path with real inherited fstat data.
    python3 -c 'import os, sys
try:
    path_stat = os.lstat(sys.argv[1])
    keeper_stat = os.fstat(3)
    observer_stat = os.fstat(8)
except OSError:
    sys.exit(3)
if (path_stat.st_dev, path_stat.st_ino) != (keeper_stat.st_dev, keeper_stat.st_ino):
    sys.exit(1)
if (path_stat.st_dev, path_stat.st_ino) != (observer_stat.st_dev, observer_stat.st_ino):
    sys.exit(2)
sys.exit(0)
' "$path" 3<&3 8<&8
}
mock_lifetime_probe() {
    local state=$1 role=$2 token=$3 timeout=$4 directory path response read_status
    local registered_role registered_token registration_count=0 inode_status inode_phase
    if [[ -n $token && $token == owner.* && $token != */* ]]; then
        :
    else
        mock_lifetime_refusal "$role" invalid-token token
        return 2
    fi
    if [[ -d $state/control && ! -L $state/control ]]; then
        :
    else
        mock_lifetime_refusal "$role" "$token" control-directory
        return 2
    fi
    if [[ -f $state/processes && ! -L $state/processes ]]; then
        :
    else
        mock_lifetime_refusal "$role" "$token" process-registry
        return 2
    fi
    directory="$state/control/$token"
    if [[ -d $directory && ! -L $directory ]]; then
        :
    else
        mock_lifetime_refusal "$role" "$token" owner-directory
        return 2
    fi
    path="$directory/lifetime"
    if [[ -p $path && ! -L $path ]]; then
        :
    else
        mock_lifetime_refusal "$role" "$token" lifetime-fifo
        return 2
    fi
    while read -r registered_role registered_token; do
        if [[ $registered_token == "$token" ]]; then
            if [[ $registered_role == "$role" ]]; then
                registration_count=$((registration_count + 1))
            else
                mock_lifetime_refusal "$role" "$token" registry-role
                return 2
            fi
        fi
    done <"$state/processes"
    if [[ $registration_count == 1 ]]; then
        :
    else
        mock_lifetime_refusal "$role" "$token" "registry-count-$registration_count"
        return 2
    fi

    # Keep read-only open nonblocking even when the owner has already exited.
    # Parent FD 3 is the temporary keeper; FD 8 is the read-only observer.
    if exec 3<> "$path"; then
        :
    else
        mock_lifetime_refusal "$role" "$token" keeper-open
        return 2
    fi
    if exec 8< "$path"; then
        :
    else
        exec 3>&-
        mock_lifetime_refusal "$role" "$token" observer-open
        return 2
    fi
    if mock_lifetime_descriptors_match "$path"; then
        :
    else
        inode_status=$?
        exec 8<&-
        exec 3>&-
        case "$inode_status" in
            1) inode_phase=keeper-inode-mismatch ;;
            2) inode_phase=observer-inode-mismatch ;;
            *) inode_phase="fstat-helper-status-$inode_status" ;;
        esac
        mock_lifetime_refusal "$role" "$token" "$inode_phase"
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
    mock_lifetime_refusal "$role" "$token" "read-status-$read_status-bytes-${#response}"
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
    # The launcher can register another owner while the first bounded cleanup
    # observes an earlier registry. Recheck after its owned runner has stopped.
    mock_release_all
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
    # Terminal-marker fixtures are gone; hang/empty fixtures stay alive so
    # their cases exercise the launcher's real deadline and timeout report.
    if [[ ${4:-} == ps && $# -eq 8 && ${5:-} == -p && ${7:-} == -o && ${8:-} == pid=,stat=,comm= ]]; then
        if [[ ${FAKE_RESULT:-success} == hang || ${FAKE_RESULT:-success} == empty ]]; then
            printf '%s\n' "${6}"
        fi
        exit 0
    fi
    if [[ ${4:-} == ps && $# -eq 7 && ${5:-} == -A && ${6:-} == -o && ${7:-} == pid=,stat=,comm=,args= ]]; then
        case "${FAKE_RESULT:-success}" in
            hang|empty) printf '%s\n' "${BUSTER_IOS_BUNDLE_ID:-dev.buster.ide}" ;;
        esac
        exit 0
    fi

    diagnostic_kind=
    if [[ ${4:-} == ps && $# -eq 7 && ${5:-} == -A && ${6:-} == -o && ${7:-} == pid=,ppid=,stat=,comm=,args= ]]; then
        diagnostic_kind=process-table
    elif [[ ${4:-} == log && $# -eq 11 && ${5:-} == show && ${6:-} == --style && ${7:-} == compact && ${8:-} == --last && ${9:-} == 5m && ${10:-} == --predicate ]]; then
        diagnostic_kind=unified-log
    elif [[ ${4:-} == sh && $# -eq 6 && ${5:-} == -c && ${6:-} == 'find /var/mobile/Library/Logs/CrashReporter -type f -mmin -10 -print -exec tail -n 80 {} \;' ]]; then
        diagnostic_kind=crash-reports
    fi
    if [[ -z $diagnostic_kind ]]; then
        echo "unrecognized fake simctl spawn command: $*" >&2
        exit 97
    fi
    printf 'fake diagnostic %s: simulator command output\n' "$diagnostic_kind" >&2
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
    local producer_result cancel_result monitor_result helper_result bridge_result producer_token stream_dirs capture_receipt
    local producer_count reader_count diagnostic_count expected_diagnostic_count=0
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
        # This fixture sends an authenticated lifecycle CANCEL after exact
        # producer registration so the launcher can run its owned TERM cleanup.
        # Direct bridge-shell SIGTERM is exercised by lifecycle_capture_bridge_test.py.
        # One absolute controller deadline shares the unchanged 15s outer cap;
        # launcher and monitor deadlines remain 3s/1s.
        mkfifo "$state/registration"
        export FAKE_REGISTRATION_FIFO="$state/registration"
        "$timeout_bin" --preserve-status --signal=TERM --kill-after=3s "$runner_timeout" \
            python3 - "$timeout_bin" "$repo_root/ios/lifecycle_capture_bridge.sh" \
            "$state/interrupted-run" "$state/registration" "$state/processes" \
            "$state/interruption-result" 3 14 15 -- \
            /bin/bash "$launcher" "${arguments[@]}" <<'PY' >"$state/output" 2>&1 &
import errno
import os
import re
import select
import signal
import stat
import subprocess
import sys
import time

def read_regular_line(path):
    if not stat.S_ISREG(os.lstat(path).st_mode):
        raise ValueError("receipt is not a non-symlink regular file: " + path)
    with open(path, "rb") as source:
        data = source.read(4097)
    if len(data) > 4096 or not data.endswith(b"\n") or data.count(b"\n") != 1:
        raise ValueError("receipt is missing, oversized, or not one line: " + path)
    return data[:-1].decode("ascii")

def read_named_receipt(path, header):
    words = read_regular_line(path).split()
    if not words or words[0] != header:
        raise ValueError("receipt header is missing or invalid: " + path)
    values = {}
    for word in words[1:]:
        key, separator, value = word.partition("=")
        if not separator or not key or key in values:
            raise ValueError("receipt has an invalid or duplicate field: " + path)
        values[key] = value
    return values

def private_lifetime_fifo(prefix):
    private_dir = read_regular_line(prefix + ".caller-private-directory.log")
    absolute_prefix = os.path.abspath(prefix)
    if (not os.path.isabs(private_dir) or os.path.normpath(private_dir) != private_dir
            or os.path.dirname(private_dir) != os.path.dirname(absolute_prefix)):
        raise ValueError("caller private directory is outside the expected prefix")
    base = os.path.basename(absolute_prefix) + ".capture."
    name = os.path.basename(private_dir)
    generation = name[len(base):] if name.startswith(base) else ""
    if re.fullmatch(r"[A-Za-z0-9]{8,64}", generation) is None:
        raise ValueError("caller private directory has an invalid generation")
    if not stat.S_ISDIR(os.lstat(private_dir).st_mode):
        raise ValueError("caller private generation is not a non-symlink directory")
    lifetime = os.path.join(private_dir, "lifetime")
    if not stat.S_ISFIFO(os.lstat(lifetime).st_mode):
        raise ValueError("caller private lifetime endpoint is not a non-symlink FIFO")
    return generation, lifetime

def send_cancel(lifetime, generation, deadline):
    frame = ("CANCEL %s 15\n" % generation).encode("ascii")
    while time.monotonic() < deadline:
        try:
            descriptor = os.open(lifetime, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as error:
            if error.errno not in (errno.ENXIO, errno.ENOENT):
                raise
            time.sleep(min(0.01, deadline - time.monotonic()))
            continue
        try:
            try:
                if os.write(descriptor, frame) != len(frame):
                    raise RuntimeError("private lifecycle cancellation frame was only partly written")
                return
            except BlockingIOError:
                time.sleep(min(0.01, deadline - time.monotonic()))
        finally:
            os.close(descriptor)
    raise TimeoutError("private lifecycle cancellation write exceeded the shared cap")

def replay_capture(prefix):
    path = prefix + ".log"
    if os.path.isfile(path) and not os.path.islink(path):
        with open(path, "rb") as capture:
            sys.stdout.buffer.write(capture.read())
            sys.stdout.buffer.flush()

def main():
    separator = sys.argv.index("--", 1)
    options, command = sys.argv[1:separator], sys.argv[separator + 1:]
    if len(options) != 9 or not command:
        raise ValueError("interruption controller received incorrect arguments")
    timeout_bin, bridge_script, prefix, fifo, registry, result, command_seconds, capture_seconds, outer_seconds = options
    if (command_seconds, capture_seconds, outer_seconds) != ("3", "14", "15"):
        raise ValueError("interruption controller received changed deadline policy")
    controller_started = time.monotonic()
    outer_deadline = controller_started + int(outer_seconds)
    registration_deadline = min(outer_deadline, controller_started + 10)
    if not stat.S_ISFIFO(os.lstat(fifo).st_mode):
        raise ValueError("producer registration endpoint is not a FIFO")
    fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
    bridge = None
    cancel_sent = False
    try:
        bridge = subprocess.Popen([
            "/bin/bash", bridge_script, timeout_bin, prefix,
            command_seconds, capture_seconds, "--", *command,
        ])
        pending = bytearray()
        token = None
        while time.monotonic() < registration_deadline and token is None:
            if bridge.poll() is not None:
                raise RuntimeError("lifecycle bridge exited before producer registration")
            remaining = registration_deadline - time.monotonic()
            ready, _, _ = select.select([fd], [], [], min(0.05, remaining))
            if not ready:
                continue
            chunk = os.read(fd, 256)
            if not chunk:
                time.sleep(min(0.05, remaining))
                continue
            pending.extend(chunk)
            if len(pending) > 1024:
                raise ValueError("producer registration data exceeded its bound")
            while b"\n" in pending:
                raw, _, rest = pending.partition(b"\n")
                pending = bytearray(rest)
                fields = raw.decode("ascii").split(" ")
                if len(fields) != 2:
                    raise ValueError("producer registration row is malformed")
                role, candidate = fields
                if not candidate.startswith("owner.") or "/" in candidate:
                    raise ValueError("producer registration token is invalid")
                if role in ("reader", "diagnostic"):
                    continue
                if role != "producer":
                    raise ValueError("unexpected mock owner role: " + role)
                if not stat.S_ISREG(os.lstat(registry).st_mode):
                    raise ValueError("mock process registry is not a regular file")
                with open(registry, "r", encoding="ascii") as rows:
                    if rows.read().splitlines().count("producer " + candidate) != 1:
                        raise ValueError("producer notification has no exact owner row")
                token = candidate
                break
        if token is None:
            raise TimeoutError("producer registration handshake expired within the shared cap")
        if bridge.poll() is not None:
            raise RuntimeError("lifecycle bridge exited before authenticated producer cancellation")
        generation, lifetime = private_lifetime_fifo(prefix)
        send_cancel(lifetime, generation, outer_deadline)
        cancel_sent = True
        remaining = outer_deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("no shared outer-cap time remained for helper completion")
        try:
            bridge_status = bridge.wait(timeout=remaining)
        except subprocess.TimeoutExpired as error:
            raise TimeoutError("authenticated lifecycle completion exceeded the unchanged 15s outer cap") from error
        if bridge_status != 0:
            raise RuntimeError("lifecycle bridge did not complete helper receipts (status %d)" % bridge_status)
        caller = read_regular_line(prefix + ".caller-fields.log").split()
        caller_receipt = read_named_receipt(prefix + ".caller-status.log", "BUSTER_IOS_CALLER")
        caller_keys = {"version", "generation", "admission", "helper_status", "invocation_status",
                       "command_monitor_status", "reason"}
        if set(caller_receipt) != caller_keys or caller_receipt["version"] != "1":
            raise RuntimeError("lifecycle caller receipt has an invalid field set or version")
        if caller_receipt["generation"] != generation:
            raise RuntimeError("caller receipt generation does not match the private lifetime FIFO")
        monitor = caller_receipt["command_monitor_status"]
        expected_caller = ["1", "143", "143", generation, monitor, "complete"]
        if (caller != expected_caller or caller_receipt["admission"] != "1"
                or caller_receipt["helper_status"] != "143"
                or caller_receipt["invocation_status"] != "143"
                or monitor not in ("0", "124", "137") or caller_receipt["reason"] != "complete"):
            raise RuntimeError("lifecycle bridge has no matching completed helper receipt: " + repr(caller))
        supervisor = read_named_receipt(prefix + ".supervisor-status.log", "BUSTER_IOS_SUPERVISOR")
        # Expiry is monitor evidence, never monitor success. The production
        # launcher pairs 124/137 with deadline_reached=1 and authentication=0.
        monitor_authenticated = "1" if monitor == "0" else "0"
        monitor_deadline = "0" if monitor == "0" else "1"
        expected_supervisor = {
            "bridge_generation": generation,
            "command_monitor_status": monitor,
            "command_authenticated": monitor_authenticated,
            "deadline_reached": monitor_deadline,
            "caller_lost": "0",
            "command_status": "143",
            "native_status": "143",
            "native_launch": "1",
            "native_reaped": "1",
            "capture_status": "0",
            "capture_eof": "1",
            "cleanup_status": "0",
            "keeper_reaped": "1",
            "group_authority_released": "1",
            "cancellation_signal": "15",
            "helper_error": "none",
        }
        if supervisor.get("version") != "1" or any(
                supervisor.get(key) != value for key, value in expected_supervisor.items()):
            raise RuntimeError("lifecycle supervisor cancellation/cleanup receipt is incomplete: "
                               + repr({key: supervisor.get(key) for key in expected_supervisor}))
        helper_status = int(caller_receipt["helper_status"])
        with open(result, "x", encoding="ascii", newline="\n") as marker:
            marker.write("producer=%s cancellation_signal=15 command_monitor_status=%s "
                         "helper_status=%d bridge_status=%d\n" %
                         (token, monitor, helper_status, bridge_status))
        return helper_status
    finally:
        os.close(fd)
        if bridge is not None and bridge.poll() is None:
            try:
                if not cancel_sent:
                    bridge.send_signal(signal.SIGTERM)
                remaining = max(0.0, outer_deadline - time.monotonic())
                if remaining:
                    bridge.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                try:
                    bridge.send_signal(signal.SIGTERM)
                except ProcessLookupError:
                    pass
            except ProcessLookupError:
                pass
        replay_capture(prefix)

try:
    sys.exit(main())
except (OSError, ValueError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
    print("mock interruption controller failed: %s" % error, file=sys.stderr)
    sys.exit(1)
PY
    else
        "$timeout_bin" --preserve-status --signal=TERM --kill-after=3s "$runner_timeout" \
            /bin/bash "$launcher" "${arguments[@]}" >"$state/output" 2>&1 &
    fi
    runner=$!
    wait "$runner" || status=$?
    runner=
    if [[ $status -ne $expected ]]; then
        cat "$state/output" >&2
        echo "unexpected status for $label: $status, expected $expected" >&2
        exit 1
    fi
    if [[ $interrupt == 1 ]]; then
        if [[ ! -f $state/interruption-result ]] || [[ $(wc -l <"$state/interruption-result") -ne 1 ]]; then
            cat "$state/output" >&2
            echo "$label did not record the producer-registered interruption result" >&2
            exit 1
        fi
        read -r producer_result cancel_result monitor_result helper_result bridge_result <"$state/interruption-result"
        producer_token=${producer_result#producer=}
        case "$monitor_result" in
            command_monitor_status=0|command_monitor_status=124|command_monitor_status=137) ;;
            *)
                echo "$label reported an invalid lifecycle monitor result: $monitor_result" >&2
                exit 1
                ;;
        esac
        if [[ $producer_token != owner.* || $producer_token == */* \\
            || $cancel_result != cancellation_signal=15 \\
            || $helper_result != helper_status=143 || $bridge_result != bridge_status=0 \\
            || ! -f $state/processes ]] || ! grep -Fxq "producer $producer_token" "$state/processes"; then
            cat "$state/output" >&2
            cat "$state/interruption-result" >&2
            echo "$label did not complete authenticated native cancellation and helper cleanup after producer registration" >&2
            exit 1
        fi
    fi
    if [[ $expected == 1 && $interrupt == 0 && ( $outcome == hang || $outcome == empty ) ]]; then
        if ! grep -qF 'did not produce a buster test result marker before the 3s launch deadline' "$state/output" ||
            ! grep -qF 'this is a real launch timeout; simctl/console attachment status was' "$state/output"; then
            cat "$state/output" >&2
            echo "$label did not report the real launch deadline timeout" >&2
            exit 1
        fi
    fi
    if [[ ! -f $state/processes ]]; then
        echo "$label did not create its owner registry" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    producer_count=$(grep -c '^producer ' "$state/processes" || true)
    reader_count=$(grep -c '^reader ' "$state/processes" || true)
    diagnostic_count=$(grep -c '^diagnostic ' "$state/processes" || true)
    if [[ $diagnostic_mode == timeout ]]; then
        expected_diagnostic_count=3
    fi
    if [[ $producer_count -ne $bundles || $reader_count -ne $bundles || $diagnostic_count -ne $expected_diagnostic_count ]]; then
        echo "$label registered producer=$producer_count reader=$reader_count diagnostic=$diagnostic_count; expected producer=$bundles reader=$bundles diagnostic=$expected_diagnostic_count" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    while read -r role token; do
        case "$role" in
            producer|reader|diagnostic) ;;
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
    # Reuse the same bounded lifetime deadline for the launcher's cleanup.
    # Owner EOF can precede the shell's EXIT-trap removal of its private stream FIFO.
    while (( SECONDS < owner_deadline )); do
        stream_dirs=$(find "$state" -name 'buster-ios-stream.*' -print)
        if [[ -z $stream_dirs ]]; then
            break
        fi
        sleep 0.05
    done
    stream_dirs=$(find "$state" -name 'buster-ios-stream.*' -print)
    if [[ -n $stream_dirs ]]; then
        printf '%s leaked its FIFO path by the shared lifetime deadline:\n%s\n' "$label" "$stream_dirs" >&2
        mock_report_owner_state "$state"
        if [[ $interrupt == 1 ]]; then
            for capture_receipt in \
                "$state/interrupted-run.caller-status.log" \
                "$state/interrupted-run.caller-fields.log" \
                "$state/interrupted-run.caller-private-directory.log" \
                "$state/interrupted-run.supervisor-fields.log" \
                "$state/interrupted-run.supervisor-status.log" \
                "$state/interrupted-run.log"; do
                if [[ -f $capture_receipt && ! -L $capture_receipt ]]; then
                    printf 'mock capture receipt: %s\n' "$capture_receipt" >&2
                    head -c 4096 "$capture_receipt" >&2 || true
                    printf '\n' >&2
                else
                    printf 'mock capture receipt: missing %s\n' "$capture_receipt" >&2
                fi
            done
        fi
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
    if [[ $role == reader && $token == owner.* && $token != */* ]]; then
        :
    else
        echo "fake reader registered an invalid owner role/token" >&2
        mock_report_owner_state "$state"
        exit 1
    fi

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
    if [[ $status -eq 0 ]]; then
        :
    else
        echo "fake reader runner exited status=$status after cooperative release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    if mock_lifetime_probe "$state" "$role" "$token" 3; then
        :
    else
        lifetime_status=$?
        if (( lifetime_status == 1 )); then
            echo "fake reader remained live after the cooperative-release EOF deadline" >&2
        else
            echo "fake reader lifetime observer validation failed status=$lifetime_status" >&2
        fi
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
    local state="$test_root/release-control" registration role token response status=0 probe_status
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
    if [[ $role == producer && $token == owner.* && $token != */* ]]; then
        :
    else
        echo "finite mock fixture registered an invalid owner role/token" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    exec 3<> "$state/control/$token/release"
    printf 'release\n' >&3
    exec 3>&-
    wait "$runner" || status=$?
    runner=
    if [[ $status -eq 0 ]]; then
        :
    else
        echo "finite mock fixture runner exited status=$status after cooperative release" >&2
        mock_report_owner_state "$state"
        exit 1
    fi
    if mock_lifetime_probe "$state" "$role" "$token" 3; then
        :
    else
        probe_status=$?
        if (( probe_status == 1 )); then
            echo "finite mock fixture remained live after the cooperative-release EOF deadline" >&2
        else
            echo "finite mock fixture lifetime observer validation failed status=$probe_status" >&2
        fi
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
