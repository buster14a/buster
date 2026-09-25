#!/usr/bin/env bash
# Exercise iOS simulator installation lifecycle evidence without Xcode.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-ios-install.XXXXXX")

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

assert_file_contains() {
    local needle=$1 path=$2
    if ! grep -qF "$needle" "$path"; then
        echo "assertion failed: '$needle' not found in $path" >&2
        [[ -f $path ]] && cat "$path" >&2
        exit 1
    fi
}

assert_count() {
    local expected=$1 needle=$2 path=$3 actual
    actual=$(grep -cF "$needle" "$path" 2>/dev/null || true)
    if [[ $actual -ne $expected ]]; then
        echo "assertion failed: expected $expected occurrences of '$needle' in $path, got $actual" >&2
        [[ -f $path ]] && cat "$path" >&2
        exit 1
    fi
}

mkdir -p "$test_root/bin"
cat >"$test_root/bin/codesign" <<'TOOL'
#!/usr/bin/env bash
exit 0
TOOL
cat >"$test_root/bin/xcrun" <<'TOOL'
#!/usr/bin/env bash
set -euo pipefail

state=${FAKE_IOS_INSTALL_STATE:?FAKE_IOS_INSTALL_STATE is required}
log="$state/xcrun.log"
printf '%s\n' "$*" >>"$log"

if [[ ${1:-} == --sdk ]]; then
    printf '26.5\n'
    exit 0
fi
if [[ ${1:-} != simctl ]]; then
    exit 1
fi
shift
command=${1:-}
shift || true
case "$command" in
    boot)
        exit 0
        ;;
    bootstatus)
        printf 'Device booted\n'
        exit 0
        ;;
    install)
        app_path=${2:-}
        printf 'fake install stdout: %s\n' "$app_path"
        printf 'fake install stderr: lifecycle diagnostic\n' >&2
        if [[ $app_path == *"/Debug/"* ]]; then
            case "${FAKE_IOS_INSTALL_CASE:-success}" in
                success) ;;
                reject) exit 8 ;;
                native-124) exit 124 ;;
                timeout) sleep 60 ;;
                *) exit 97 ;;
            esac
        fi
        printf '%s\n' "$app_path" >"$state/installed"
        exit 0
        ;;
    launch)
        printf 'BUSTER_IOS_RESULT: SUCCESS\n'
        exit 0
        ;;
    list)
        case "${1:-}" in
            devices) printf 'fake iOS simulator devices\n' ;;
            runtimes) printf 'fake iOS simulator runtimes\n' ;;
            *) printf 'fake iOS simulator list\n' ;;
        esac
        exit 0
        ;;
    spawn)
        exit 1
        ;;
    shutdown)
        exit 0
        ;;
    *)
        printf 'unsupported fake simctl command: %s\n' "$command" >&2
        exit 1
        ;;
esac
TOOL
chmod +x "$test_root/bin/codesign" "$test_root/bin/xcrun"
export PATH="$test_root/bin:$PATH"

run_case() {
    local case_name=$1 expected_status=$2 expected_outcome=$3 expected_helper_status=$4 expected_native_status=$5
    local state="$test_root/$case_name" status=0 expected_launches=2
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    : >"$state/xcrun.log"

    export FAKE_IOS_INSTALL_STATE="$state"
    export FAKE_IOS_INSTALL_CASE="$case_name"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_CONSOLE_LOG="$state/console.log"
    export BUSTER_IOS_BOOT_TIMEOUT_SECONDS=2
    export BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS=2
    export BUSTER_IOS_INSTALL_TIMEOUT_SECONDS=1
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=2
    export BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS=2
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1

    if /bin/bash "$launcher" --batch         Debug "$state/Debug/ide.app" Release "$state/Release/ide.app"         >"$state/run.log" 2>&1; then
        status=0
    else
        status=$?
    fi

    if [[ $status -ne $expected_status ]]; then
        cat "$state/run.log" >&2
        echo "unexpected launcher status for $case_name: $status, expected $expected_status" >&2
        exit 1
    fi

    assert_file_contains         "phase=install label=Debug outcome=$expected_outcome status=$expected_helper_status native_status=$expected_native_status capture_status=0"         "$state/console.Debug.log.install.status.log"
    assert_file_contains 'deadline_seconds=1 output_limit_bytes=65536'         "$state/console.Debug.log.install.status.log"
    assert_file_contains 'command: xcrun simctl install FAKE-UDID'         "$state/console.Debug.log.install.status.log"
    assert_file_contains 'fake install stdout:' "$state/console.Debug.log.install.log"
    assert_file_contains 'fake install stderr: lifecycle diagnostic' "$state/console.Debug.log.install.log"

    if [[ $case_name != success ]]; then
        expected_launches=1
        assert_file_contains "failed to install iOS Debug app bundle (outcome=$expected_outcome)" "$state/run.log"
        assert_file_contains 'phase=install label=Release outcome=success status=0 native_status=0 capture_status=0'             "$state/console.Release.log.install.status.log"
        assert_file_contains 'BUSTER_IOS_RESULT: SUCCESS' "$state/console.Release.log"
    else
        assert_file_contains 'phase=install label=Release outcome=success status=0 native_status=0 capture_status=0'             "$state/console.Release.log.install.status.log"
        assert_file_contains 'BUSTER_IOS_RESULT: SUCCESS' "$state/console.Debug.log"
        assert_file_contains 'BUSTER_IOS_RESULT: SUCCESS' "$state/console.Release.log"
    fi

    assert_count 2 'simctl install FAKE-UDID' "$state/xcrun.log"
    assert_count "$expected_launches" 'simctl launch --console-pty FAKE-UDID' "$state/xcrun.log"
    assert_count 1 'simctl shutdown FAKE-UDID' "$state/xcrun.log"
    printf 'iOS install lifecycle evidence passed: %s\n' "$case_name"
}

run_case success 0 success 0 0
run_case reject 1 command-failure 8 8
run_case native-124 1 command-failure 124 124
run_case timeout 1 timeout 124 unavailable
