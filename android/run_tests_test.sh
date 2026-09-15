#!/usr/bin/env bash
set -euo pipefail

# The harness doubles as a deterministic adb replacement. This keeps the
# production wrapper's five-argument contract intact without requiring an
# emulator.
if [[ ${BUSTER_ANDROID_FAKE_ADB:-0} == 1 ]]; then
    state=${BUSTER_ANDROID_FAKE_STATE:?BUSTER_ANDROID_FAKE_STATE is required}
    if [[ ${1:-} == -s ]]; then
        shift 2
    fi
    command=${1:-}
    shift || true

    case "$command" in
        devices)
            printf 'List of devices attached\n'
            printf 'fake-emulator\tdevice\n'
            exit 0
            ;;
        uninstall|install)
            if [[ $command == install && ${BUSTER_ANDROID_FAKE_INSTALL_STATUS:-0} -ne 0 ]]; then
                exit "$BUSTER_ANDROID_FAKE_INSTALL_STATUS"
            fi
            exit 0
            ;;
        shell)
            shell_command=${1:-}
            shift || true
            case "$shell_command" in
                getprop)
                    case ${1:-} in
                        sys.boot_completed) printf '1\n' ;;
                        ro.product.cpu.abi) printf 'x86_64\n' ;;
                        ro.build.version.sdk) printf '35\n' ;;
                        *)
                            printf 'fake adb: unsupported getprop %s\n' "${1:-}" >&2
                            exit 1
                            ;;
                    esac
                    ;;
                am)
                    case ${1:-} in
                        start) exit "${BUSTER_ANDROID_FAKE_LAUNCH_STATUS:-0}" ;;
                        force-stop) ;;
                        *)
                            printf 'fake adb: unsupported am command %s\n' "${1:-}" >&2
                            exit 1
                            ;;
                    esac
                    ;;
                'am start '*)
                    : >"$state/am-start.ready"
                    exit "${BUSTER_ANDROID_FAKE_LAUNCH_STATUS:-0}"
                    ;;
                'am force-stop '*) ;;
                *)
                    printf 'fake adb: unsupported shell command %s\n' "$shell_command" >&2
                    exit 1
                    ;;
            esac
            exit 0
            ;;
        logcat)
            if [[ ${1:-} == -d ]]; then
                printf 'fake Android logcat diagnostics\n'
                exit 0
            fi
            if [[ ${1:-} == -c ]]; then
                exit 0
            fi

            printf '%s\n' "$$" >"$state/producer.pid"
            case ${BUSTER_ANDROID_FAKE_SCENARIO:-success} in
                success)
                    printf 'unrelated Android log line\n'
                    printf '%s\n' "$(date +%s%N)" >"$state/marker.ns"
                    printf 'BUSTER_ANDROID_TEST_RESULT:0\n'
                    ;;
                failure)
                    printf 'BUSTER_ANDROID_TEST_RESULT:1\n'
                    ;;
                launch_failure|install_failure)
                    printf 'test still running\n'
                    ;;
                malformed)
                    printf 'BUSTER_ANDROID_TEST_RESULT:0 trailing\n'
                    printf 'BUSTER_ANDROID_TEST_RESULT:0x\n'
                    printf 'unrelated Android log line\n'
                    ;;
                missing_marker)
                    printf 'unrelated Android log line\n'
                    exit 0
                    ;;
                timeout)
                    printf 'test still running\n'
                    ;;
                *)
                    printf 'unknown fake scenario\n' >&2
                    exit 2
                    ;;
            esac
            while :; do
                sleep 1
            done
            ;;
        *)
            printf 'fake adb: unsupported command %s\n' "$command" >&2
            exit 1
            ;;
    esac
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
run_tests_script=$repo_root/android/run_tests.sh
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-android-run-tests.XXXXXX")
apk=$test_root/fake.apk
: >"$apk"

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

assert_log_contains() {
    local needle=$1
    local state=$2
    if ! grep -qF "$needle" "$state/run.log"; then
        echo "assertion failed: '$needle' not found in $state/run.log" >&2
        cat "$state/run.log" >&2
        return 1
    fi
}

assert_no_owned_producer() {
    local state=$1
    local pid
    local process_state
    if [[ ! -f $state/producer.pid ]]; then
        return 0
    fi
    pid=$(<"$state/producer.pid")
    if kill -0 "$pid" >/dev/null 2>&1; then
        process_state=$(ps -o stat= -p "$pid" 2>/dev/null | tr -d ' ' || true)
        if [[ $process_state != Z* ]]; then
            echo "assertion failed: fake adb producer $pid is still alive" >&2
            ps -o pid,ppid,pgid,stat,comm,args -p "$pid" >&2 || true
            return 1
        fi
    fi
}

run_case() {
    local scenario=$1
    local expected_status=$2
    local max_elapsed_ms=$3
    local state=$test_root/$scenario
    local start_ns
    local end_ns
    local elapsed_ms
    local status
    local marker_ns
    local marker_tail_ms
    local launch_status=0
    local install_status=0
    mkdir -p "$state"

    if [[ $scenario == launch_failure ]]; then
        launch_status=23
    elif [[ $scenario == install_failure ]]; then
        install_status=24
    fi

    start_ns=$(date +%s%N)
    set +e
    BUSTER_ANDROID_FAKE_ADB=1 \
        BUSTER_ANDROID_FAKE_STATE="$state" \
        BUSTER_ANDROID_FAKE_SCENARIO="$scenario" \
        BUSTER_ANDROID_FAKE_LAUNCH_STATUS="$launch_status" \
        BUSTER_ANDROID_FAKE_INSTALL_STATUS="$install_status" \
        BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=3 \
        BUSTER_ANDROID_ADB_WAIT_TIMEOUT_SECONDS=2 \
        BUSTER_ANDROID_ADB_COMMAND_TIMEOUT_SECONDS=2 \
        BUSTER_ANDROID_ADB_INSTALL_TIMEOUT_SECONDS=2 \
        bash "$run_tests_script" \
            "$BASH_SOURCE" \
            "$apk" \
            dev.buster.ide \
            dev.buster.ide/android.app.NativeActivity \
            'test --verbose=1 --ci=1' >"$state/run.log" 2>&1
    status=$?
    set -e
    end_ns=$(date +%s%N)
    elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))

    printf 'CASE %s status=%s elapsed_ms=%s\n' "$scenario" "$status" "$elapsed_ms"
    if [[ $status -ne $expected_status ]]; then
        echo "assertion failed: $scenario returned $status, expected $expected_status" >&2
        return 1
    fi
    if (( elapsed_ms > max_elapsed_ms )); then
        echo "assertion failed: $scenario took ${elapsed_ms}ms, expected <= ${max_elapsed_ms}ms" >&2
        return 1
    fi
    assert_no_owned_producer "$state"

    case "$scenario" in
        success)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=10' "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=0' "$state"
            ;;
        failure)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=11' "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=1' "$state"
            ;;
        timeout|malformed)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=0 producer_status=124 timeout_seconds=3' "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=1' "$state"
            ;;
        missing_marker)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=0 producer_status=0 timeout_seconds=3' "$state"
            assert_log_contains 'ended without a terminal result' "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=1' "$state"
            ;;
        launch_failure)
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=launch status=23' "$state"
            ;;
        install_failure)
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=install status=24' "$state"
            ;;
    esac

    if [[ $scenario == success ]]; then
        marker_ns=$(<"$state/marker.ns")
        marker_tail_ms=$(( (end_ns - marker_ns) / 1000000 ))
        printf 'CASE %s marker_to_wrapper_ms=%s\n' "$scenario" "$marker_tail_ms"
        if (( marker_tail_ms >= 2000 )); then
            echo "assertion failed: success marker tail was ${marker_tail_ms}ms" >&2
            return 1
        fi
    fi
}

run_case success 0 2000
run_case failure 1 2000
run_case timeout 1 6000
run_case malformed 1 6000
run_case missing_marker 1 2000
run_case launch_failure 23 2000
run_case install_failure 24 2000

state=$test_root/interrupt
mkdir -p "$state"
cat >"$state/wrapper.sh" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$$" >"$BUSTER_ANDROID_TEST_WRAPPER_PID_FILE"
exec bash "$@"
EOF
BUSTER_ANDROID_TEST_WRAPPER_PID_FILE="$state/wrapper.pid" \
    BUSTER_ANDROID_FAKE_ADB=1 \
    BUSTER_ANDROID_FAKE_STATE="$state" \
    BUSTER_ANDROID_FAKE_SCENARIO=timeout \
    BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=30 \
    BUSTER_ANDROID_ADB_WAIT_TIMEOUT_SECONDS=2 \
    BUSTER_ANDROID_ADB_COMMAND_TIMEOUT_SECONDS=2 \
    BUSTER_ANDROID_ADB_INSTALL_TIMEOUT_SECONDS=2 \
    timeout --kill-after=1s 15s bash "$state/wrapper.sh" \
        "$run_tests_script" \
        "$BASH_SOURCE" \
        "$apk" \
        dev.buster.ide \
        dev.buster.ide/android.app.NativeActivity \
        'test --verbose=1 --ci=1' >"$state/run.log" 2>&1 &
timeout_pid=$!
deadline=$((SECONDS + 5))
while (( SECONDS < deadline )) && [[ ! -f $state/wrapper.pid || ! -f $state/producer.pid || ! -f $state/am-start.ready ]]; do
    sleep 1
done
if [[ ! -f $state/wrapper.pid || ! -f $state/producer.pid || ! -f $state/am-start.ready ]]; then
    echo "assertion failed: interrupt wrapper did not reach the monitor phase" >&2
    kill "$timeout_pid" >/dev/null 2>&1 || true
    exit 1
fi
kill -TERM "$(<"$state/wrapper.pid")"
set +e
wait "$timeout_pid"
interrupt_status=$?
set -e
printf 'CASE interrupt status=%s\n' "$interrupt_status"
if [[ $interrupt_status -ne 143 ]]; then
    echo "assertion failed: interrupt returned $interrupt_status, expected 143" >&2
    cat "$state/run.log" >&2
    exit 1
fi
assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=143' "$state"
assert_no_owned_producer "$state"

echo "Android run_tests harness passed"
