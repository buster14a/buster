#!/usr/bin/env bash
# Observe delayed app/fixture receipt without accepting traces as test results.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
launcher=${BUSTER_IOS_TEST_LAUNCHER:-$repo_root/ios/launch_simulator.sh}
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-ios-monitor.XXXXXX")
runner=
collector=
baseline_launcher=
active_state=
evidence_root=${BUSTER_MOBILE_TEST_EVIDENCE_DIR:-}
collector_timeout_bin=$(python3 "$repo_root/ios/gnu_timeout.py")
cleanup() {
    local status=$?
    local path kind pid file
    local files=()
    trap - EXIT INT TERM
    if [[ -n $active_state && $status -ne 0 ]]; then
        printf 'BUSTER_IOS_MONITOR_CASE event=failed status=%s runner=%s collector=%s elapsed_seconds=%s\n' "$status" "${runner:-none}" "${collector:-none}" "$SECONDS" >>"$active_state/case.log"
        for pid in "$runner" "$collector"; do
            [[ -n $pid ]] || continue
            ps -p "$pid" -o pid=,ppid=,pgid=,stat=,comm= >>"$active_state/children.log" 2>&1 || true
        done
        if [[ -f $active_state/pids ]]; then
            while read -r kind pid; do
                printf 'role=%s pid=%s\n' "$kind" "$pid" >>"$active_state/children.log"
                ps -p "$pid" -o pid=,ppid=,pgid=,stat=,comm= >>"$active_state/children.log" 2>&1 || true
            done <"$active_state/pids"
        fi
    fi
    if [[ -n $active_state && $status -ne 0 ]]; then
        for path in "$active_state/case.log" "$active_state/collector.log" "$active_state/children.log" "$active_state"/*.status.log; do
            [[ -f $path ]] || continue
            printf '%s\n' "----- ${path##*/} -----" >&2
            cat "$path" >&2 || true
        done
    fi
    if [[ -n $runner ]]; then
        kill -TERM "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
    fi
    if [[ -n $collector ]]; then
        kill -TERM -- -"$collector" 2>/dev/null || kill -TERM "$collector" 2>/dev/null || true
        kill -KILL -- -"$collector" 2>/dev/null || true
        wait "$collector" 2>/dev/null || true
    fi
    if [[ -n $baseline_launcher ]]; then rm -f "$baseline_launcher"; fi
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
    if [[ -n $evidence_root ]]; then
        mkdir -p "$evidence_root"
        for path in "$test_root"/*; do
            [[ -d $path && ${path##*/} != bin ]] || continue
            mkdir -p "$evidence_root/${path##*/}"
            # Retain ordinary evidence only; never open a stalled FIFO to copy it.
            files=()
            for file in "$path"/*; do
                [[ -f $file ]] || continue
                files+=("$file")
            done
            if [[ ${#files[@]} -gt 0 ]]; then
                if ! cp "${files[@]}" "$evidence_root/${path##*/}/"; then
                    if [[ $status -eq 0 ]]; then status=1; fi
                fi
            fi
        done
    fi
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$test_root/bin"
export REAL_TEE REAL_GREP
REAL_TEE=$(command -v tee)
REAL_GREP=$(command -v grep)
cat >"$test_root/bin/grep" <<'TOOL'
#!/usr/bin/env bash
set -eu
printf 'grep\n' >>"$RUNNER_TEMP/grep-invocations.log"
# Finite process-start delay control for receipt verification overhead. Current
# builtin receipt checks never enter this shim for these files.
receipt_path=${!#}
if [[ ${BUSTER_IOS_TEST_RECEIPT_DELAY:-0} == 1 && ( $receipt_path == *.caller-status.log || $receipt_path == *.supervisor-status.log ) ]]; then
    sleep 0.02
fi
# Model bytes arriving between the launcher's empty size check and its first
# app-record scan. The fake producer owns the FIFO but emits its marker later.
if [[ $FAKE_RESULT == receipt-race && ${1:-} == -q && ${2:-} == '^BUSTER_IOS_LAUNCH_V1 ' && ! -e $RUNNER_TEMP/trace-written ]]; then
    sleep 2
    printf 'BUSTER_IOS_LAUNCH_V1 stage=main pid=1 monotonic_us=1 wall_us=1 process_cpu_us=1 monotonic_status=0 wall_status=0 cpu_status=0\nTEST_FIXTURE_START_V1 kind=module module=probe fixture=body index=0\n' >>"$3"
    : >"$RUNNER_TEMP/trace-written"
fi
exec "$REAL_GREP" "$@"
TOOL
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
        success|delayed-success|trace-only|backpressure)
            [[ ${SIMCTL_CHILD_BUSTER_IOS_LAUNCH_TRACE:-} == 1 ]]
            [[ ${SIMCTL_CHILD_BUSTER_TEST_FIXTURE_TIMING:-} == all ]]
            if [[ $FAKE_RESULT == delayed-success ]]; then sleep 1; fi
            printf 'BUSTER_IOS_LAUNCH_V1 stage=main pid=1 monotonic_us=1 wall_us=1 process_cpu_us=1 monotonic_status=0 wall_status=0 cpu_status=0\n'
            if [[ $FAKE_RESULT == delayed-success ]]; then sleep 2; fi
            printf 'TEST_FIXTURE_START_V1 kind=module module=probe fixture=body index=0\n'
            if [[ $FAKE_RESULT == backpressure ]]; then
                # Much more than either Linux or Darwin pipe capacity. Preserve
                # the existing real tee; the downstream collector stalls below.
                dd if=/dev/zero bs=65536 count=32 2>/dev/null | tr '\0' x
                printf '\n'
            fi
            if [[ $FAKE_RESULT != trace-only ]]; then printf 'BUSTER_IOS_RESULT: SUCCESS\n'; fi ;;
        failure) printf 'BUSTER_IOS_RESULT: FAILURE\n' ;;
        receipt-race) sleep 4; printf 'BUSTER_IOS_RESULT: SUCCESS\n' ;;
        hang) : ;;
    esac
    # The same process stays attached after its terminal marker.
    exec sleep 60
fi
exit 0
TOOL
chmod +x "$test_root/bin/tee" "$test_root/bin/codesign" "$test_root/bin/xcrun" "$test_root/bin/grep"
export PATH="$test_root/bin:$PATH"
run_case() {
    local label=$1 outcome=$2 expected=$3 interrupt=$4 bundles=$5
    local state="$test_root/$label" status=0 deadline kind pid
    active_state=$state
    printf 'BUSTER_IOS_MONITOR_CASE event=start label=%s elapsed_seconds=%s\n' "$label" "$SECONDS" >&2
    mkdir -p "$state/Debug/ide.app" "$state/Release/ide.app"
    export RUNNER_TEMP="$state" FAKE_PIDS="$state/pids" FAKE_RESULT="$outcome"
    export BUSTER_IOS_SIMULATOR_UDID=FAKE-UDID
    export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3
    if [[ $outcome == delayed-success || $outcome == receipt-race ]]; then
        export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=7
    fi
    export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=1
    local arguments=(--batch Debug "$state/Debug/ide.app")
    if [[ $bundles == 2 ]]; then
        arguments+=(Release "$state/Release/ide.app")
    fi
    printf 'BUSTER_IOS_MONITOR_CASE event=start label=%s elapsed_seconds=%s\n' "$label" "$SECONDS" >"$state/case.log"
    local output_path="$state/output"
    if [[ $outcome == backpressure ]]; then
        mkfifo "$state/actions-output"
        # Drain setup records, then stall the pipe as launch starts. Waiting
        # for producer PID evidence before draining can instead block setup on
        # Darwin's smaller pipe. Exercise actual app-output backpressure.
        # shellcheck disable=SC2016
        "$collector_timeout_bin" --kill-after=1s 20s /bin/bash -c '
            set -eu
            printf "event=collector-start elapsed_seconds=%s pid=%s\n" "$SECONDS" "$$" >>"$RUNNER_TEMP/collector.log"
            while IFS= read -r line; do
                printf "%s\n" "$line"
                if [[ $line == Launching\ * ]]; then break; fi
            done
            printf "event=stall-start elapsed_seconds=%s\n" "$SECONDS" >>"$RUNNER_TEMP/collector.log"
            sleep 6
            printf "event=drain-start elapsed_seconds=%s\n" "$SECONDS" >>"$RUNNER_TEMP/collector.log"
            cat
            printf "event=eof elapsed_seconds=%s\n" "$SECONDS" >>"$RUNNER_TEMP/collector.log"
        ' <"$state/actions-output" >"$state/output" &
        collector=$!
        output_path="$state/actions-output"
        export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=4
    fi
    /bin/bash "$launcher" "${arguments[@]}" >"$output_path" 2>&1 &
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
    printf 'BUSTER_IOS_MONITOR_CASE event=launcher-exit status=%s elapsed_seconds=%s\n' "$status" "$SECONDS" >>"$state/case.log"
    runner=
    if [[ -n $collector ]]; then
        local collector_status=0
        wait "$collector" || collector_status=$?
        printf 'BUSTER_IOS_MONITOR_CASE event=collector-exit status=%s elapsed_seconds=%s\n' "$collector_status" "$SECONDS" >>"$state/case.log"
        collector=
        if [[ $collector_status -ne 0 ]]; then
            cat "$state/case.log" "$state/collector.log" >&2
            exit "$collector_status"
        fi
    fi
    if [[ $outcome == backpressure ]]; then
        grep -Eq '^event=collector-start elapsed_seconds=[0-9]+ pid=[1-9][0-9]*$' "$state/collector.log"
        if [[ $expected == 0 ]]; then
            [[ $(grep -c '^BUSTER_IOS_RESULT: SUCCESS' "$state/buster-ios-console.Debug.log") -eq 1 ]]
            [[ $(wc -c <"$state/buster-ios-console.Debug.log") -gt 2097152 ]]
            grep -q 'iOS Debug tests passed.' "$state/output"
            ! grep -q 'this is a real launch timeout' "$state/output"
        else
            grep -q 'this is a real launch timeout' "$state/output"
            ! grep -q 'iOS Debug tests passed.' "$state/output"
        fi
    fi
    if [[ $status -ne $expected ]]; then
        cat "$state/output" >&2
        echo "unexpected status for $label: $status, expected $expected" >&2
        exit 1
    fi
    if [[ $outcome == success || $outcome == delayed-success || $outcome == trace-only || $outcome == receipt-race ]]; then
        [[ $(grep -c 'event=first-console ' "$state/output") -eq $bundles ]]
        [[ $(grep -c 'event=first-app-trace ' "$state/output") -eq $bundles ]]
        [[ $(grep -c 'event=first-fixture ' "$state/output") -eq $bundles ]]
        awk '
            /BUSTER_IOS_LAUNCH_OBSERVATION / {
                for (i=1; i<=NF; i++) if ($i ~ /^observed_after_seconds=/) elapsed=substr($i,24)+0
                if ($0 ~ /event=first-console /) { console=elapsed; seen=1 }
                else if (!seen || console > elapsed) exit 1
            }
        ' "$state/output"
        if [[ $outcome == trace-only ]]; then
            grep -q 'this is a real launch timeout' "$state/output"
            ! grep -q 'iOS Debug tests passed.' "$state/output"
        fi
        if [[ $outcome == delayed-success ]]; then
            awk '
                /event=first-app-trace / { for (i=1; i<=NF; i++) if ($i ~ /^observed_after_seconds=/) trace=substr($i,24)+0 }
                /event=first-fixture / { for (i=1; i<=NF; i++) if ($i ~ /^observed_after_seconds=/) fixture=substr($i,24)+0 }
                END { exit !(trace >= 1 && fixture >= trace + 1) }
            ' "$state/output"
        fi
    elif [[ $outcome == hang ]]; then
        ! grep -q 'BUSTER_IOS_LAUNCH_OBSERVATION ' "$state/output"
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
    cp "$state/pids" "$state/children-pids.log"
    rm -f "$state/pids"
    cat "$state/case.log"
    if [[ -f $state/collector.log ]]; then cat "$state/collector.log"; fi
    printf 'BUSTER_IOS_MONITOR_CASE event=verified label=%s grep_invocations=%s elapsed_seconds=%s\n' "$label" "$(wc -l <"$state/grep-invocations.log" | tr -d ' ')" "$SECONDS"
    printf 'iOS monitor cleanup passed: %s\n' "$label"
    active_state=
}
if [[ ${1:-} == --receipt-race-only ]]; then
    run_case receipt-race receipt-race 0 0 1
else
    if [[ ${1:-} != --backpressure-only ]]; then
        run_case success success 0 0 1
        run_case batch success 0 0 2
        run_case delayed-success delayed-success 0 0 1
        run_case trace-only trace-only 1 0 1
        run_case receipt-race receipt-race 0 0 1
    fi
    # Sensitivity control: restore only the old stdout coupling in a private
    # script beside its unchanged helpers, then require a real deadline failure.
    baseline_launcher=$(mktemp "$repo_root/ios/launcher-backpressure.XXXXXX")
    # shellcheck disable=SC2016
    sed 's/tee "$console_log" >\/dev\/null/tee "$console_log"/' "$launcher" >"$baseline_launcher"
    if cmp -s "$launcher" "$baseline_launcher"; then
        echo "backpressure control did not restore the baseline transport" >&2
        exit 1
    fi
    corrected_launcher=$launcher
    launcher=$baseline_launcher
    run_case backpressure-baseline backpressure 1 0 1
    launcher=$corrected_launcher
    run_case backpressure-corrected backpressure 0 0 1
    rm -f "$baseline_launcher"
    baseline_launcher=
fi
