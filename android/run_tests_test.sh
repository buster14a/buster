#!/usr/bin/env bash
set -euo pipefail

# Batch/workflow-only extensions live here rather than changing the frozen
# tests/mobile_ci_fake_tool.sh support input. Delegate ordinary adb commands,
# CMake/Ninja, and the emulator lifecycle to that unchanged shared fake.
if [[ -n ${BUSTER_ANDROID_STATUS_FAKE_TOOL:-} ]]; then
    tool=$BUSTER_ANDROID_STATUS_FAKE_TOOL
    state=${FAKE_ANDROID_STATE_DIR:?FAKE_ANDROID_STATE_DIR is required}
    shared_fake=${BUSTER_ANDROID_SHARED_FAKE:?BUSTER_ANDROID_SHARED_FAKE is required}
    arguments=("$@")
    if [[ $tool == emulator ]]; then
        if [[ ${1:-} != -list-avds && ${1:-} != -accel-check ]]; then
            printf '%s\n' "$$" >"$state/emulator.pid"
        fi
    elif [[ $tool == adb ]]; then
        printf '%s\n' "$*" >>"$state/adb.log"
        if [[ ${1:-} == -s ]]; then
            shift 2
        fi
        command=${1:-}
        shift || true
        case "$command" in
            install)
                # install -r supplies the APK last, not as its first argument.
                printf '%s\n' "${!#}" >"$state/installed_apk"
                exit 0
                ;;
            logcat)
                if [[ ${1:-} == -c ]]; then
                    exit 0
                fi
                if [[ ${1:-} != -d ]]; then
                    installed_path=$(<"$state/installed_apk")
                    installed_config=${installed_path%/*}
                    installed_config=${installed_config##*/}
                    printf '%s\n' "$$" >"$state/monitor-$installed_config.pid"
                    if [[ $installed_config == "${FAKE_ANDROID_TIMEOUT_CONFIG:-}" ]]; then
                        printf 'test still running\n'
                        while :; do sleep 1; done
                    elif [[ $installed_config == "${FAKE_ANDROID_MISSING_MARKER_CONFIG:-}" ]]; then
                        exit 0
                    elif [[ $installed_config == "${FAKE_ANDROID_FAIL_CONFIG:-}" ]]; then
                        printf 'BUSTER_ANDROID_TEST_RESULT:1\n'
                        exit 0
                    elif [[ $installed_config == "${FAKE_ANDROID_STOP_AFTER_CONFIG:-}" ]]; then
                        : >"$state/kill"
                        emulator_pid=$(<"$state/emulator.pid")
                        stopped_deadline=$((SECONDS + 3))
                        while kill -0 "$emulator_pid" >/dev/null 2>&1; do
                            emulator_state=$(ps -o stat= -p "$emulator_pid" 2>/dev/null | tr -d ' ' || true)
                            if [[ -z $emulator_state || $emulator_state == Z* || $emulator_state == X* ]]; then
                                break
                            fi
                            if [[ $SECONDS -ge $stopped_deadline ]]; then
                                echo "fake emulator $emulator_pid did not stop" >&2
                                exit 1
                            fi
                            sleep 0.1
                        done
                        printf 'BUSTER_ANDROID_TEST_RESULT:0\n'
                        exit 0
                    fi
                fi
                ;;
            emu)
                sleep "${FAKE_ANDROID_ADB_KILL_SLEEP_SECONDS:-0}"
                ;;
        esac
    else
        echo "unsupported Android status fake: $tool" >&2
        exit 1
    fi
    FAKE_TOOL_NAME="$tool" exec bash "$shared_fake" "${arguments[@]}"
fi

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
                success|slow_success)
                    printf 'unrelated Android log line\n'
                    # slow_success reports late enough to leave only thin
                    # headroom inside its deadline, without crossing it.
                    sleep "${BUSTER_ANDROID_FAKE_MARKER_DELAY_SECONDS:-0}"
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
                    # Oversized payload text must be reported bounded, not in full.
                    printf 'x%.0s' {1..500}
                    printf '\n'
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
if [[ -n ${BUSTER_MOBILE_TEST_EVIDENCE_DIR:-} ]]; then
    mkdir -p "$BUSTER_MOBILE_TEST_EVIDENCE_DIR"
    test_root=$(mktemp -d "$BUSTER_MOBILE_TEST_EVIDENCE_DIR/android-cases.XXXXXX")
else
    test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-android-run-tests.XXXXXX")
fi
apk=$test_root/fake.apk
: >"$apk"

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -z ${BUSTER_MOBILE_TEST_EVIDENCE_DIR:-} ]]; then
        rm -rf "$test_root"
    else
        echo "Android lifecycle evidence: $test_root"
    fi
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

assert_log_matches() {
    local expression=$1
    local state=$2
    if ! grep -qE "$expression" "$state/run.log"; then
        echo "assertion failed: '$expression' did not match $state/run.log" >&2
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
    local case_name=${5:-$scenario}
    local state=$test_root/$case_name
    local start_ns
    local end_ns
    local elapsed_ms
    local status
    local marker_ns
    local marker_tail_ms
    local bounded_tail
    local launch_status=0
    local install_status=0
    local timeout_seconds=3
    local headroom_percent=50
    local marker_delay_seconds=0
    mkdir -p "$state"

    case "$scenario" in
        launch_failure) launch_status=23 ;;
        install_failure) install_status=24 ;;
        slow_success)
            timeout_seconds=6
            headroom_percent=75
            marker_delay_seconds=3
            ;;
    esac
    # Preserve per-scenario defaults, explicit overrides, and an empty value
    # that exercises the production wrapper's normal whole-suite deadline.
    timeout_seconds=${4-$timeout_seconds}
    local monitor_timeout_seconds=${timeout_seconds:-180}

    start_ns=$(date +%s%N)
    set +e
    BUSTER_ANDROID_FAKE_ADB=1 \
        BUSTER_ANDROID_FAKE_STATE="$state" \
        BUSTER_ANDROID_FAKE_SCENARIO="$scenario" \
        BUSTER_ANDROID_FAKE_LAUNCH_STATUS="$launch_status" \
        BUSTER_ANDROID_FAKE_INSTALL_STATUS="$install_status" \
        BUSTER_ANDROID_FAKE_MARKER_DELAY_SECONDS="$marker_delay_seconds" \
        BUSTER_ANDROID_TEST_TIMEOUT_SECONDS="$timeout_seconds" \
        BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT="$headroom_percent" \
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

    printf 'CASE %s status=%s elapsed_ms=%s\n' "$case_name" "$status" "$elapsed_ms"
    if [[ $status -ne $expected_status ]]; then
        echo "assertion failed: $scenario returned $status, expected $expected_status" >&2
        return 1
    fi
    if (( elapsed_ms > max_elapsed_ms )); then
        echo "assertion failed: $scenario took ${elapsed_ms}ms, expected <= ${max_elapsed_ms}ms" >&2
        return 1
    fi
    assert_no_owned_producer "$state"
    if [[ $scenario != install_failure && $scenario != launch_failure ]]; then
        assert_log_contains "ANDROID_MONITOR_START config=standalone timeout_seconds=$monitor_timeout_seconds" "$state"
    fi

    case "$scenario" in
        success)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=10' "$state"
            assert_log_matches "ANDROID_MONITOR_RESULT config=standalone reader_status=10 producer_status=[0-9]+ timeout_seconds=$monitor_timeout_seconds elapsed_seconds=[0-9]+ headroom_seconds=[0-9]+ headroom_warning=no$" "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=0' "$state"
            if grep -qF "of its ${monitor_timeout_seconds}s deadline" "$state/run.log"; then
                echo "assertion failed: comfortable headroom reported a deadline warning" >&2
                return 1
            fi
            ;;
        slow_success)
            assert_log_matches "ANDROID_MONITOR_RESULT config=standalone reader_status=10 producer_status=[0-9]+ timeout_seconds=$monitor_timeout_seconds elapsed_seconds=[0-9]+ headroom_seconds=[0-9]+ headroom_warning=yes$" "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=0' "$state"
            assert_log_contains 'Android compiler tests passed' "$state"
            assert_log_matches "warning: Android standalone payload used [0-9]+s of its ${monitor_timeout_seconds}s deadline; [0-9]+s of headroom remain \(warning margin $((monitor_timeout_seconds * headroom_percent / 100))s at ${headroom_percent}%\)" "$state"
            ;;
        failure)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=11' "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=1' "$state"
            ;;
        timeout|malformed)
            assert_log_contains 'ANDROID_MONITOR_RESULT config=standalone reader_status=0 producer_status=124 timeout_seconds=3' "$state"
            assert_log_matches 'ANDROID_MONITOR_RESULT config=standalone reader_status=0 producer_status=124 timeout_seconds=3 elapsed_seconds=[0-9]+ headroom_seconds=0 headroom_warning=no$' "$state"
            assert_log_contains 'ANDROID_PAYLOAD_RESULT config=standalone phase=monitor status=1' "$state"
            assert_log_contains 'exhausted its own deadline; emulator cleanup has not run yet' "$state"
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

    case "$scenario" in
        timeout)
            assert_log_contains 'Android payload emitted 1 log line(s) before the monitor ended; last line: test still running' "$state"
            ;;
        malformed)
            assert_log_contains 'Android payload emitted 3 log line(s) before the monitor ended; last line: unrelated Android log line' "$state"
            ;;
        missing_marker)
            bounded_tail=$(printf 'x%.0s' {1..200})
            assert_log_contains "Android payload emitted 2 log line(s) before the monitor ended; last line: $bounded_tail" "$state"
            if grep -qF "last line: ${bounded_tail}x" "$state/run.log"; then
                echo "assertion failed: the reported payload tail was not bounded" >&2
                return 1
            fi
            ;;
    esac

    if [[ $scenario == success || $scenario == slow_success ]]; then
        marker_ns=$(<"$state/marker.ns")
        marker_tail_ms=$(( (end_ns - marker_ns) / 1000000 ))
        printf 'CASE %s marker_to_wrapper_ms=%s\n' "$case_name" "$marker_tail_ms"
        if (( marker_tail_ms >= 2000 )); then
            echo "assertion failed: success marker tail was ${marker_tail_ms}ms" >&2
            return 1
        fi
    fi
}

run_case success 0 2000
run_case slow_success 0 6000
run_case failure 1 2000
run_case timeout 1 6000
run_case malformed 1 6000
run_case missing_marker 1 2000
run_case launch_failure 23 2000
run_case install_failure 24 2000
# The normal whole-suite budget must not delay either terminal result. Keep
# the short explicit timeout/malformed-marker controls above as real failures.
run_case success 0 2000 '' default_deadline_success
run_case failure 1 2000 '' default_deadline_failure
run_case success 0 2000 5 override_deadline_success
run_case slow_success 0 6000 8 override_deadline_slow_success

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
while (( SECONDS < deadline )); do
    if [[ -f $state/wrapper.pid && -f $state/producer.pid ]] &&
       grep -qF 'ANDROID_MONITOR_START config=standalone timeout_seconds=30' "$state/run.log" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if [[ ! -f $state/wrapper.pid || ! -f $state/producer.pid ]] ||
   ! grep -qF 'ANDROID_MONITOR_START config=standalone timeout_seconds=30' "$state/run.log" 2>/dev/null; then
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

# Batch and real workflow-body regressions are kept outside the frozen shared
# mobile support files. Each invocation owns its SDK, AVD, outputs, and PIDs.
export BUSTER_ANDROID_STATUS_HARNESS="$repo_root/android/run_tests_test.sh"
export BUSTER_ANDROID_SHARED_FAKE="$repo_root/tests/mobile_ci_fake_tool.sh"
fake_bin="$test_root/bin"
mkdir -p "$fake_bin"
for tool in adb emulator; do
    cat >"$fake_bin/$tool" <<EOF
#!/usr/bin/env bash
export BUSTER_ANDROID_STATUS_FAKE_TOOL=$tool
exec bash "\$BUSTER_ANDROID_STATUS_HARNESS" "\$@"
EOF
    chmod +x "$fake_bin/$tool"
done
for tool in cmake ninja; do
    ln -s "$BUSTER_ANDROID_SHARED_FAKE" "$fake_bin/$tool"
done

assert_file_contains() {
    local needle=$1
    local path=$2
    if ! grep -qF "$needle" "$path"; then
        echo "assertion failed: '$needle' not found in $path" >&2
        cat "$path" >&2
        exit 1
    fi
}

assert_android_owned_process_stopped() {
    local pid_file=$1
    local pid process_state
    if [[ ! -f $pid_file ]]; then
        echo "assertion failed: expected owned process pid file $pid_file" >&2
        exit 1
    fi
    pid=$(<"$pid_file")
    if kill -0 "$pid" >/dev/null 2>&1; then
        process_state=$(ps -o stat= -p "$pid" 2>/dev/null | tr -d ' ' || true)
        if [[ -n $process_state && $process_state != Z* && $process_state != X* ]]; then
            echo "assertion failed: owned process $pid ($pid_file) is still alive" >&2
            exit 1
        fi
    fi
}

android_fixture_teardown() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n ${control_pid:-} ]]; then
        kill "$control_pid" >/dev/null 2>&1 || true
        wait "$control_pid" 2>/dev/null || true
    fi
    if [[ -f ${BUSTER_ANDROID_EMULATOR_STARTED_MARKER:-} ]] &&
       ! bash "$repo_root/android/start_emulator_ci.sh" stop; then
        echo "error: Android fixture teardown could not stop the owned emulator" >&2
        if [[ $status -eq 0 ]]; then status=1; fi
    fi
    exit "$status"
}

setup_android_fixture() {
    local state=$1
    local sdk="$state/sdk"
    local ndk="$sdk/ndk/fake"
    mkdir -p "$ndk/build/cmake" "$state/runner-temp/buster-ci" \
        "$sdk/system-images/android-35/google_apis/x86_64"
    : >"$ndk/build/cmake/android.toolchain.cmake"
    export PATH="$fake_bin:$PATH"
    export ANDROID_HOME="$sdk"
    export ANDROID_NDK_HOME="$ndk"
    export ANDROID_USER_HOME="$state/android-home"
    export ANDROID_AVD_HOME="$state/avd"
    export FAKE_ANDROID_STATE_DIR="$state"
    export FAKE_ANDROID_BOOT_DELAY_SECONDS=0
    export FAKE_CMAKE_LOG="$state/cmake.log"
    export BUSTER_ANDROID_BUILD_DIRECTORY="$state/build"
    export RUNNER_TEMP="$state/runner-temp"
    export BUSTER_ANDROID_EMULATOR_STARTED_MARKER="$RUNNER_TEMP/buster-ci/android-emulator.started"
    export BUSTER_ANDROID_EMULATOR_LOG="$RUNNER_TEMP/buster-ci/android-emulator.log"
    export BUSTER_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS=8
    export BUSTER_ANDROID_COMMAND_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_ADB_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_TOOL_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS=2
    export BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=4
    trap android_fixture_teardown EXIT
}

extract_android_workflow_body() {
    local destination=$1
    local trailer=${2:-}
    python3 - "$repo_root/.github/workflows/ci.yml" "$destination" "$trailer" <<'PY'
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text().splitlines()
step = next(i for i, line in enumerate(lines) if line.strip() == '- name: Test (Android)')
run = next(i for i in range(step, len(lines)) if lines[i] == '        run: |')
body = []
for line in lines[run + 1:]:
    if line and not line.startswith('          '):
        break
    body.append(line[10:] if line else '')
assert body.count('bash ./android/start_emulator_ci.sh start') == 1
assert body.count('./android/test_ci.sh --all') == 1
if sys.argv[3]:
    body = body[:body.index('bash ./android/start_emulator_ci.sh start') + 1]
    body.extend(['android_phase=tests', sys.argv[3]])
Path(sys.argv[2]).write_text('\n'.join(body) + '\n')
PY
}

assert_case_status() {
    local case_name=$1 actual=$2 expected=$3 log=$4
    if [[ $actual -eq 124 || $actual -eq 137 || $actual -ne $expected ]]; then
        echo "assertion failed: $case_name returned $actual, expected $expected (deadline statuses are never accepted)" >&2
        cat "$log" >&2
        exit 1
    fi
}

test_android_batch_status() (
    set -euo pipefail
    local case_name=$1
    local expected_status=$2
    local expected_debug_status=$3
    local expected_release_status=$4
    local state="$test_root/android-batch-$case_name"
    local batch_status
    setup_android_fixture "$state"
    export BUSTER_ANDROID_COMMAND_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_ADB_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_TOOL_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS=1
    export BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=1
    case "$case_name" in
        payload-failure) export FAKE_ANDROID_FAIL_CONFIG=Debug ;;
        debug-timeout) export FAKE_ANDROID_TIMEOUT_CONFIG=Debug ;;
        missing-marker) export FAKE_ANDROID_MISSING_MARKER_CONFIG=Debug ;;
        all-pass) ;;
        *) echo "unknown batch case: $case_name" >&2; exit 1 ;;
    esac
    bash "$repo_root/android/start_emulator_ci.sh" start
    [[ -f $BUSTER_ANDROID_EMULATOR_STARTED_MARKER ]]
    set +e
    timeout --kill-after=1s 15s bash "$repo_root/android/test_ci.sh" --all >"$state/run.log" 2>&1
    batch_status=$?
    set -e
    assert_case_status "$case_name" "$batch_status" "$expected_status" "$state/run.log"
    assert_file_contains "ANDROID_CONFIG_RESULT config=Debug status=$expected_debug_status" "$state/run.log"
    assert_file_contains "ANDROID_CONFIG_RESULT config=Release status=$expected_release_status" "$state/run.log"
    if [[ $expected_status -eq 0 ]]; then
        assert_file_contains 'ANDROID_BATCH_RESULT phase=tests config=none status=0 cleanup_status=not-run' "$state/run.log"
    else
        assert_file_contains "ANDROID_BATCH_RESULT phase=tests config=Debug status=$expected_status cleanup_status=0" "$state/run.log"
        if grep -qE 'ANDROID_BATCH_RESULT .* status=0 ' "$state/run.log"; then
            echo "assertion failed: $case_name reported a successful batch after failure" >&2
            exit 1
        fi
        assert_file_contains 'a later configuration success does not clear an earlier failure' "$state/run.log"
    fi
    case "$case_name" in
        payload-failure)
            assert_file_contains 'Android compiler tests failed' "$state/run.log"
            assert_file_contains 'ANDROID_PAYLOAD_RESULT config=Debug phase=monitor status=1' "$state/run.log"
            ;;
        debug-timeout)
            assert_file_contains 'Android compiler tests timed out after 1s' "$state/run.log"
            assert_file_contains 'the Android Debug payload exhausted its own deadline' "$state/run.log"
            assert_file_contains 'Android payload emitted 1 log line(s) before the monitor ended; last line: test still running' "$state/run.log"
            assert_file_contains 'ANDROID_PAYLOAD_RESULT config=Debug phase=monitor status=1' "$state/run.log"
            assert_file_contains 'ANDROID_PAYLOAD_RESULT config=Release phase=monitor status=0' "$state/run.log"
            if grep -qF 'ANDROID_PAYLOAD_RESULT config=Debug phase=monitor status=0' "$state/run.log"; then
                echo "assertion failed: $case_name reported a successful Debug payload after timeout" >&2
                exit 1
            fi
            ;;
        missing-marker)
            assert_file_contains 'ended without a terminal result' "$state/run.log"
            assert_file_contains 'ANDROID_MONITOR_RESULT config=Debug reader_status=0 producer_status=0' "$state/run.log"
            ;;
    esac
    assert_file_contains Debug "$state/build/Debug/buster.apk"
    assert_file_contains Release "$state/build/Release/buster.apk"
    assert_file_contains 'Debug apk' "$state/cmake.log"
    assert_file_contains 'Release apk' "$state/cmake.log"
    if [[ -f $BUSTER_ANDROID_EMULATOR_STARTED_MARKER ]]; then
        bash "$repo_root/android/start_emulator_ci.sh" stop
    fi
    [[ ! -f $BUSTER_ANDROID_EMULATOR_STARTED_MARKER ]]
    assert_android_owned_process_stopped "$state/emulator.pid"
    assert_android_owned_process_stopped "$state/monitor-Debug.pid"
    assert_android_owned_process_stopped "$state/monitor-Release.pid"
    echo "Android batch status evidence passed: $case_name"
)

test_android_workflow_body() (
    set -euo pipefail
    local case_name=$1
    local state="$test_root/android-workflow-$case_name"
    local body=$android_workflow_body
    local ran_tests=1
    local body_status control_pid=
    local expected_status expected_result
    setup_android_fixture "$state"
    case "$case_name" in
        all-pass)
            expected_status=0
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=0 cleanup_status=0 status=0'
            ;;
        debug-failure|debug-timeout|missing-marker)
            case "$case_name" in
                debug-failure) export FAKE_ANDROID_FAIL_CONFIG=Debug ;;
                debug-timeout) export FAKE_ANDROID_TIMEOUT_CONFIG=Debug ;;
                missing-marker) export FAKE_ANDROID_MISSING_MARKER_CONFIG=Debug ;;
            esac
            expected_status=1
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=1 cleanup_status=not-run status=1'
            ;;
        cleanup-exit7|cleanup-timeout)
            if [[ $case_name == cleanup-exit7 ]]; then
                export FAKE_ANDROID_ADB_KILL_STATUS=7
            else
                export FAKE_ANDROID_ADB_KILL_SLEEP_SECONDS=30
                export BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS=1
            fi
            expected_status=1
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=0 cleanup_status=1 status=1'
            ;;
        failure-and-cleanup7)
            export FAKE_ANDROID_FAIL_CONFIG=Debug
            export FAKE_ANDROID_ADB_KILL_STATUS=7
            expected_status=1
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=1 cleanup_status=not-run status=1'
            ;;
        already-stopped)
            export FAKE_ANDROID_STOP_AFTER_CONFIG=Release
            expected_status=0
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=0 cleanup_status=0 status=0'
            ;;
        interruption)
            body=$android_workflow_interrupt_body
            ran_tests=0
            expected_status=143
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=143 cleanup_status=0 status=143'
            ;;
        payload-23-cleanup7)
            body=$android_workflow_exit23_body
            export FAKE_ANDROID_ADB_KILL_STATUS=7
            ran_tests=0
            expected_status=23
            expected_result='ANDROID_CI_RESULT phase=tests payload_status=23 cleanup_status=1 status=23'
            ;;
        *) echo "unknown workflow case: $case_name" >&2; exit 1 ;;
    esac
    sleep 60 &
    control_pid=$!
    set +e
    (cd "$repo_root" && timeout --kill-after=1s 15s bash "$body") >"$state/body.log" 2>&1
    body_status=$?
    set -e
    assert_case_status "$case_name" "$body_status" "$expected_status" "$state/body.log"
    assert_file_contains "$expected_result" "$state/body.log"
    if [[ $expected_status -ne 0 ]] && grep -qE 'ANDROID_CI_RESULT .* status=0$' "$state/body.log"; then
        echo "assertion failed: $case_name reported a successful workflow after failure" >&2
        exit 1
    fi
    case "$case_name" in
        debug-failure)
            assert_file_contains 'ANDROID_BATCH_RESULT phase=tests config=Debug status=1 cleanup_status=0' "$state/body.log"
            assert_file_contains 'Android CI failed before final emulator cleanup (phase=tests status=1)' "$state/body.log"
            assert_file_contains 'Android emulator log follows an already-failed phase=tests status=1' "$state/body.log"
            ;;
        debug-timeout)
            assert_file_contains 'Android compiler tests timed out after 4s' "$state/body.log"
            assert_file_contains 'the Android Debug payload exhausted its own deadline' "$state/body.log"
            ;;
        missing-marker)
            assert_file_contains 'ended without a terminal result' "$state/body.log"
            ;;
        cleanup-exit7)
            assert_file_contains 'adb emulator shutdown failed with exit status 7' "$state/body.log"
            assert_file_contains 'Android CI required emulator cleanup failed (status=1)' "$state/body.log"
            ;;
        cleanup-timeout)
            assert_file_contains 'Android CI required emulator cleanup failed (status=1)' "$state/body.log"
            ;;
        failure-and-cleanup7)
            assert_file_contains 'ANDROID_BATCH_RESULT phase=tests config=Debug status=1 cleanup_status=1' "$state/body.log"
            assert_file_contains 'adb emulator shutdown failed with exit status 7' "$state/body.log"
            assert_file_contains 'Android CI failed before final emulator cleanup (phase=tests status=1)' "$state/body.log"
            ;;
        already-stopped)
            assert_file_contains 'is no longer running' "$state/body.log"
            ;;
        interruption)
            assert_file_contains 'Android CI failed before final emulator cleanup (phase=tests status=143)' "$state/body.log"
            assert_file_contains 'Android emulator log follows an already-failed phase=tests status=143' "$state/body.log"
            ;;
        payload-23-cleanup7)
            assert_file_contains 'adb emulator shutdown failed with exit status 7' "$state/body.log"
            assert_file_contains 'Android CI failed before final emulator cleanup (phase=tests status=23)' "$state/body.log"
            ;;
    esac
    [[ ! -f $BUSTER_ANDROID_EMULATOR_STARTED_MARKER ]]
    assert_android_owned_process_stopped "$state/emulator.pid"
    if [[ $ran_tests == 1 ]]; then
        local debug_status=0
        case "$case_name" in
            debug-failure|debug-timeout|missing-marker|failure-and-cleanup7) debug_status=1 ;;
        esac
        assert_file_contains "ANDROID_CONFIG_RESULT config=Debug status=$debug_status" "$state/body.log"
        assert_file_contains 'ANDROID_CONFIG_RESULT config=Release status=0' "$state/body.log"
        assert_android_owned_process_stopped "$state/monitor-Debug.pid"
        assert_android_owned_process_stopped "$state/monitor-Release.pid"
    fi
    if ! kill -0 "$control_pid" >/dev/null 2>&1; then
        echo "assertion failed: $case_name process handling killed an unrelated process" >&2
        exit 1
    fi
    kill "$control_pid" >/dev/null 2>&1 || true
    wait "$control_pid" 2>/dev/null || true
    control_pid=
    echo "Android workflow body evidence passed: $case_name"
)


# Exercise cancellation through the actual workflow and batch, not just the
# workflow's EXIT trap. The existing payload seam supplies signal-style statuses;
# the standalone monitor case above covers a real TERM during log monitoring.
test_android_batch_interruption() (
    set -euo pipefail
    local case_name=$1 debug_status=$2 release_status=$3 expected_status=$4
    local first_failed_config=$5 cleanup_status=$6
    local state="$test_root/android-interruption-$case_name"
    local actual_status expected_release=not-run
    setup_android_fixture "$state"
    export BUSTER_ANDROID_RUN_TESTS_SCRIPT="$state/payload.sh"
    export FAKE_ANDROID_DEBUG_PAYLOAD_STATUS=$debug_status
    export FAKE_ANDROID_RELEASE_PAYLOAD_STATUS=$release_status
    if [[ $cleanup_status -ne 0 ]]; then export FAKE_ANDROID_ADB_KILL_STATUS=7; fi
    cat >"$state/payload.sh" <<'PAYLOAD'
#!/usr/bin/env bash
set -euo pipefail
[[ $# -eq 5 ]]
printf '%s\n' "$BUSTER_ANDROID_TEST_CONFIG" >>"$FAKE_ANDROID_STATE_DIR/configs-ran"
if [[ $BUSTER_ANDROID_TEST_CONFIG == Debug ]]; then
    exit "$FAKE_ANDROID_DEBUG_PAYLOAD_STATUS"
fi
exit "$FAKE_ANDROID_RELEASE_PAYLOAD_STATUS"
PAYLOAD
    set +e
    (cd "$repo_root" && timeout --kill-after=1s 15s bash "$android_workflow_body") >"$state/run.log" 2>&1
    actual_status=$?
    set -e
    assert_case_status "$case_name" "$actual_status" "$expected_status" "$state/run.log"
    if [[ $debug_status -eq 130 || $debug_status -eq 143 ]]; then
        [[ $(cat "$state/configs-ran") == Debug ]]
    else
        [[ $(cat "$state/configs-ran") == $'Debug\nRelease' ]]
        expected_release=$release_status
    fi
    [[ $(grep -c '^ANDROID_CONFIG_RESULT ' "$state/run.log") -eq 2 ]]
    assert_file_contains "ANDROID_CONFIG_RESULT config=Debug status=$debug_status" "$state/run.log"
    assert_file_contains "ANDROID_CONFIG_RESULT config=Release status=$expected_release" "$state/run.log"
    assert_file_contains "ANDROID_BATCH_RESULT phase=tests config=$first_failed_config status=$expected_status cleanup_status=$cleanup_status" "$state/run.log"
    assert_file_contains "ANDROID_CI_RESULT phase=tests payload_status=$expected_status cleanup_status=not-run status=$expected_status" "$state/run.log"
    [[ ! -f $BUSTER_ANDROID_EMULATOR_STARTED_MARKER ]]
    assert_android_owned_process_stopped "$state/emulator.pid"
    echo "Android batch interruption evidence passed: $case_name"
)


# Keep the terminated child unreaped for the entire helper call so this tests
# the zombie path deterministically instead of racing the host's PID reaper.
test_android_zombie_cleanup() (
    set -euo pipefail
    local state="$test_root/android-zombie-cleanup"
    setup_android_fixture "$state"
    python3 -S - "$repo_root/android/start_emulator_ci.sh" <<'PYTHON'
import os
from pathlib import Path
import subprocess
import sys
import time

marker = Path(os.environ['BUSTER_ANDROID_EMULATOR_STARTED_MARKER'])
state = Path(os.environ['FAKE_ANDROID_STATE_DIR'])
child = os.fork()
if child == 0:
    os._exit(0)
try:
    deadline = time.monotonic() + 5
    while True:
        probe = subprocess.run(['ps', '-o', 'stat=', '-p', str(child)],
                               capture_output=True, text=True, check=True)
        if probe.stdout.strip().startswith('Z'):
            break
        if time.monotonic() >= deadline:
            raise AssertionError('owned child did not reach the zombie state')
        time.sleep(0.01)
    marker.write_text(str(child) + '\n')
    result = subprocess.run(['bash', sys.argv[1], 'stop'],
                            capture_output=True, text=True, timeout=10)
    log = result.stdout + result.stderr
    (state / 'run.log').write_text(log)
    print(log, end='')
    assert result.returncode == 0, result.returncode
    assert 'is no longer running' in log
    assert not marker.exists()
    adb_log = state / 'adb.log'
    assert not adb_log.exists() or 'emu kill' not in adb_log.read_text()
finally:
    marker.unlink(missing_ok=True)
    os.waitpid(child, 0)
PYTHON
    echo "Android zombie cleanup evidence passed"
)

# Reproduce #917 deterministically: after adb shutdown the owned emulator is a
# zombie. Two terminal-state probes succeed, then ps becomes unavailable while
# kill -0 still sees the unreaped child. The stop path must treat the first
# stopped observation as terminal instead of probing again and manufacturing a
# live PID from the later ambiguous query.
test_android_terminal_state_is_monotonic() (
    set -euo pipefail
    local state="$test_root/android-terminal-state-monotonic"
    setup_android_fixture "$state"
    python3 -S - "$repo_root/android/start_emulator_ci.sh" <<'PYTHON'
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

marker = Path(os.environ['BUSTER_ANDROID_EMULATOR_STARTED_MARKER'])
state = Path(os.environ['FAKE_ANDROID_STATE_DIR'])
fake_bin = state / 'race-bin'
fake_bin.mkdir(parents=True, exist_ok=True)
real_ps = shutil.which('ps')
assert real_ps is not None

child = os.fork()
if child == 0:
    while True:
        time.sleep(60)

try:
    marker.write_text(str(child) + '\n')
    env = os.environ.copy()
    env['FAKE_ANDROID_RACE_PID'] = str(child)
    env['FAKE_ANDROID_RACE_STATE'] = str(state)
    env['FAKE_ANDROID_REAL_PS'] = real_ps
    env['BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS'] = '2'
    env['PATH'] = str(fake_bin) + os.pathsep + env['PATH']

    (fake_bin / 'adb').write_text(r'''#!/usr/bin/env bash
set -euo pipefail
state=${FAKE_ANDROID_RACE_STATE:?}
pid=${FAKE_ANDROID_RACE_PID:?}
real_ps=${FAKE_ANDROID_REAL_PS:?}
if [[ ${1:-} == emu && ${2:-} == kill ]]; then
    : >"$state/shutdown"
    kill -TERM "$pid"
    deadline=$((SECONDS + 5))
    while :; do
        process_state=$("$real_ps" -o stat= -p "$pid" 2>/dev/null | tr -d ' ' || true)
        if [[ $process_state == Z* || $process_state == X* ]]; then
            break
        fi
        if (( SECONDS >= deadline )); then
            echo "fake adb could not observe the emulator zombie" >&2
            exit 1
        fi
        sleep 0.1
    done
    printf 'OK: killing emulator, bye bye\nOK\n'
    exit 0
fi
exit 0
''')
    (fake_bin / 'ps').write_text(r'''#!/usr/bin/env bash
set -euo pipefail
state=${FAKE_ANDROID_RACE_STATE:?}
pid=${FAKE_ANDROID_RACE_PID:?}
real_ps=${FAKE_ANDROID_REAL_PS:?}
if [[ -f "$state/shutdown" && " $* " == *" -p $pid "* ]]; then
    count=0
    if [[ -f "$state/post-shutdown-ps-count" ]]; then
        count=$(<"$state/post-shutdown-ps-count")
    fi
    count=$((count + 1))
    printf '%s\n' "$count" >"$state/post-shutdown-ps-count"
    if (( count >= 3 )); then
        exit 1
    fi
fi
exec "$real_ps" "$@"
''')
    (fake_bin / 'adb').chmod(0o755)
    (fake_bin / 'ps').chmod(0o755)

    result = subprocess.run(['bash', sys.argv[1], 'stop'], env=env,
                            capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr
    (state / 'run.log').write_text(log)
    print(log, end='')
    assert result.returncode == 0, result.returncode
    assert 'still exists; sending SIGKILL' not in log
    assert not marker.exists()
    count_path = state / 'post-shutdown-ps-count'
    assert count_path.read_text().strip() == '1', count_path.read_text()
finally:
    marker.unlink(missing_ok=True)
    try:
        os.kill(child, 9)
    except ProcessLookupError:
        pass
    os.waitpid(child, 0)
PYTHON
    echo "Android monotonic terminal-state evidence passed"
)

# Requiring SIGKILL is not itself a cleanup failure. Verify the final owned
# process state after the forced signal and fail only if the process survives.
test_android_sigkill_final_verification() (
    set -euo pipefail
    local state="$test_root/android-sigkill-final-verification"
    setup_android_fixture "$state"
    python3 -S - "$repo_root/android/start_emulator_ci.sh" <<'PYTHON'
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

marker = Path(os.environ['BUSTER_ANDROID_EMULATOR_STARTED_MARKER'])
state = Path(os.environ['FAKE_ANDROID_STATE_DIR'])
fake_bin = state / 'sigkill-bin'
fake_bin.mkdir(parents=True, exist_ok=True)
read_fd, write_fd = os.pipe()
child = os.fork()
if child == 0:
    os.close(read_fd)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    os.write(write_fd, b'1')
    os.close(write_fd)
    while True:
        time.sleep(60)

os.close(write_fd)
try:
    assert os.read(read_fd, 1) == b'1'
    os.close(read_fd)
    marker.write_text(str(child) + '\n')
    (fake_bin / 'adb').write_text(r'''#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} == emu && ${2:-} == kill ]]; then
    printf 'OK: killing emulator, bye bye\nOK\n'
fi
exit 0
''')
    (fake_bin / 'adb').chmod(0o755)
    env = os.environ.copy()
    env['BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS'] = '1'
    env['PATH'] = str(fake_bin) + os.pathsep + env['PATH']

    result = subprocess.run(['bash', sys.argv[1], 'stop'], env=env,
                            capture_output=True, text=True, timeout=15)
    log = result.stdout + result.stderr
    (state / 'run.log').write_text(log)
    print(log, end='')
    assert result.returncode == 0, result.returncode
    assert 'did not exit; sending SIGTERM' in log
    assert 'still exists; sending SIGKILL' in log
    assert 'survived SIGKILL' not in log
    assert not marker.exists()
finally:
    marker.unlink(missing_ok=True)
    try:
        os.kill(child, 9)
    except ProcessLookupError:
        pass
    os.waitpid(child, 0)
PYTHON
    echo "Android SIGKILL final verification evidence passed"
)

android_workflow_body="$test_root/android-workflow.sh"
android_workflow_interrupt_body="$test_root/android-workflow-interrupt.sh"
android_workflow_exit23_body="$test_root/android-workflow-exit23.sh"
extract_android_workflow_body "$android_workflow_body"
extract_android_workflow_body "$android_workflow_interrupt_body" 'kill -TERM "$$"'
extract_android_workflow_body "$android_workflow_exit23_body" 'exit 23'
test_android_batch_status all-pass 0 0 0
test_android_batch_status payload-failure 1 1 0
test_android_batch_status debug-timeout 1 1 0
test_android_batch_status missing-marker 1 1 0
for workflow_case in all-pass debug-failure debug-timeout missing-marker cleanup-exit7 \
    cleanup-timeout failure-and-cleanup7 already-stopped interruption payload-23-cleanup7; do
    test_android_workflow_body "$workflow_case"
done

test_android_zombie_cleanup
test_android_terminal_state_is_monotonic
test_android_sigkill_final_verification
test_android_batch_interruption debug-int 130 0 130 Debug 0
test_android_batch_interruption debug-term 143 0 143 Debug 0
test_android_batch_interruption debug-term-cleanup-failure 143 0 143 Debug 1
test_android_batch_interruption release-term-after-failure 1 143 143 Debug 0

echo "Android run_tests and CI status harness passed"
