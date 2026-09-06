#!/usr/bin/env bash
# Exercise process ownership without Xcode, using real timeout/process groups.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
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
if [[ ${1:-} == simctl && ${2:-} == launch ]]; then
    printf 'timeout %s\nproducer %s\n' "$(ps -p "$$" -o ppid= | tr -d ' ')" "$$" >>"$FAKE_PIDS"
    case "$FAKE_RESULT" in
        success) printf 'BUSTER_IOS_RESULT: SUCCESS\n' ;;
        failure) printf 'BUSTER_IOS_RESULT: FAILURE\n' ;;
        hang) : ;;
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
    local state="$test_root/$label" status=0 deadline kind pid
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    export RUNNER_TEMP="$state" FAKE_PIDS="$state/pids" FAKE_RESULT="$outcome"
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
    rm -f "$state/pids"
    printf 'iOS monitor cleanup passed: %s\n' "$label"
}
run_case success success 0 0 1
run_case failure failure 1 0 1
run_case timeout hang 1 0 1
run_case interrupted hang 143 1 1
run_case batch success 0 0 2
