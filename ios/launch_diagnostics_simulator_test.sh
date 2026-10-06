#!/usr/bin/env bash
# Hosted ARM64 control: synthetic payload, real simulator boot/probes/shutdown.
# No compiler build or installed test app is needed to check probe availability.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
check_probe_receipt() {
    local status_log=$1 output_log=$2 run_log=$3 probe=$4 seconds=$5 native_commands=$6 command
    case "$probe" in
        process-table) command=ps ;;
        unified-log) command=log ;;
        crash-reports) command=sh ;;
        *) return 1 ;;
    esac
    grep -qE "^command: xcrun simctl spawn [[:xdigit:]-]+ $command " "$status_log" || return 1
    grep -qE "^BUSTER_IOS_PHASE phase=diagnostic-$probe label=probe-control outcome=[a-z-]+ status=[0-9]+ native_status=([0-9]+|unavailable) capture_status=(0|124) " "$status_log" || return 1
    grep -qF "deadline_seconds=$seconds output_limit_bytes=65536" "$status_log" || return 1
    if grep -qF 'capture_receipt=complete' "$status_log"; then
        grep -qE 'status=[0-9]+ native_status=([0-9]+|unavailable) capture_status=0 ' "$status_log" || return 1
        [[ -f $output_log ]] || return 1
    else
        # Only the actual caller capture-clock expiry can substitute for a
        # complete snapshot. Missing/malformed protocol evidence still fails.
        grep -qF 'outcome=evidence-failure status=125 native_status=unavailable capture_status=124 ' "$status_log" || return 1
        grep -qF 'capture_receipt=incomplete' "$status_log" || return 1
        grep -qE '^BUSTER_IOS_CALLER_GATE monitor_status=(124|137) admission=0 invocation_status=unavailable generation=[A-Za-z0-9]{8,64} reason=starting$' "$status_log" || return 1
        grep -qF 'BUSTER_IOS_CAPTURE incomplete=1 reason=missing-or-empty-receipt' "$status_log" || return 1
    fi
    if ! grep -qE "^simctl spawn [[:xdigit:]-]+ $command " "$native_commands" 2>/dev/null; then
        # A command/capture clock may expire before argv admission. Require the
        # explicit expiry receipt; a missing native attempt is never probe success.
        if grep -qF 'capture_receipt=complete' "$status_log"; then
            grep -qF 'outcome=timeout status=124 native_status=unavailable capture_status=0 ' "$status_log" || return 1
            grep -qF 'supervisor_valid=1 deadline_reached=1 cleanup_status=0' "$status_log" || return 1
            grep -qE '(^| )native_launch=0( |$)' "$status_log" || return 1
        fi
    fi
    if ! grep -qF 'outcome=success status=0 native_status=0 capture_status=0 ' "$status_log"; then
        grep -qF "warning: iOS diagnostic unavailable name=$probe " "$run_log" || return 1
    fi
    if grep -qF 'Traceback (most recent call last):' "$run_log"; then
        return 1
    fi
    if [[ -f $output_log ]]; then
        [[ $(wc -c <"$output_log") -le 65536 ]] || return 1
    fi
}

# Receipt controls run on POSIX hosts without booting a simulator.
if [[ ${1:-} == --check-probe-receipt ]]; then
    shift
    check_probe_receipt "$@"
    exit $?
fi

if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
    echo "error: native iOS launch-probe control requires macOS ARM64" >&2
    exit 1
fi
timeout_bin=
if command -v timeout >/dev/null 2>&1; then
    timeout_bin=timeout
elif command -v gtimeout >/dev/null 2>&1; then
    timeout_bin=gtimeout
else
    echo "error: timeout or gtimeout is required" >&2
    exit 1
fi
export BUSTER_IOS_NATIVE_XCRUN
BUSTER_IOS_NATIVE_XCRUN=$(command -v xcrun)
evidence_dir=${BUSTER_MOBILE_TEST_EVIDENCE_DIR:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}}
mkdir -p "$evidence_dir"
export BUSTER_IOS_NATIVE_CONTROL_ROOT
BUSTER_IOS_NATIVE_CONTROL_ROOT=$(mktemp -d "$evidence_dir/ios-native-probes.XXXXXX")
runner=
cleanup() {
    local status=$? udid=
    trap - EXIT INT TERM
    if [[ -n $runner ]]; then
        kill -TERM "$runner" 2>/dev/null || true
        wait "$runner" 2>/dev/null || true
    fi
    if [[ -f $BUSTER_IOS_NATIVE_CONTROL_ROOT/udid ]]; then
        read -r udid <"$BUSTER_IOS_NATIVE_CONTROL_ROOT/udid" || true
        if [[ $udid =~ ^[[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12}$ ]]; then
            "$timeout_bin" --kill-after=10s 30s "$BUSTER_IOS_NATIVE_XCRUN" simctl shutdown "$udid" \
                >"$BUSTER_IOS_NATIVE_CONTROL_ROOT/control-shutdown.log" 2>&1 || true
            if ! "$timeout_bin" --kill-after=10s 30s "$BUSTER_IOS_NATIVE_XCRUN" simctl delete "$udid" \
                >"$BUSTER_IOS_NATIVE_CONTROL_ROOT/control-delete.log" 2>&1; then
                echo "error: could not delete native probe control simulator $udid" >&2
                status=1
            fi
        else
            echo "error: native probe control has no canonical cleanup identity" >&2
            status=1
        fi
    fi
    echo "Native iOS probe evidence: $BUSTER_IOS_NATIVE_CONTROL_ROOT"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$BUSTER_IOS_NATIVE_CONTROL_ROOT/bin" "$BUSTER_IOS_NATIVE_CONTROL_ROOT/ide.app"
cat >"$BUSTER_IOS_NATIVE_CONTROL_ROOT/bin/codesign" <<'TOOL'
#!/usr/bin/env bash
exit 0
TOOL
cat >"$BUSTER_IOS_NATIVE_CONTROL_ROOT/bin/xcrun" <<'TOOL'
#!/usr/bin/env bash
set -euo pipefail
case "${1:-}/${2:-}" in
    simctl/install) exit 0 ;;
    simctl/launch)
        [[ $# -eq 8 && $6 == test && $7 == --verbose=1 && $8 == --ci=1 ]]
        printf 'TEST_MODULE_TIMING index=0 module=native_probe_control duration_ns=1 passed=1 failed=0 assertions=1 status=pass\n'
        exec sleep 60
        ;;
    simctl/create)
        udid=$("$BUSTER_IOS_NATIVE_XCRUN" "$@")
        printf '%s\n' "$udid" >"$BUSTER_IOS_NATIVE_CONTROL_ROOT/udid"
        printf '%s\n' "$udid"
        ;;
    *)
        # Every diagnostic, readiness, and lifecycle command uses native Xcode.
        printf '%s\n' "$*" >>"$BUSTER_IOS_NATIVE_CONTROL_ROOT/native-commands.log"
        exec "$BUSTER_IOS_NATIVE_XCRUN" "$@"
        ;;
esac
TOOL
chmod +x "$BUSTER_IOS_NATIVE_CONTROL_ROOT/bin/codesign" "$BUSTER_IOS_NATIVE_CONTROL_ROOT/bin/xcrun"
export PATH="$BUSTER_IOS_NATIVE_CONTROL_ROOT/bin:$PATH"
unset BUSTER_IOS_SIMULATOR_UDID
export BUSTER_IOS_SIMULATOR_DEVICE="buster-probe-control-$$"
export BUSTER_IOS_CONSOLE_LOG="$BUSTER_IOS_NATIVE_CONTROL_ROOT/console.log"
export BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS=3
# Probes provide optional evidence, not readiness. #2742 observed a 42-second
# hosted phase with no snapshot, beyond this 10-second command budget and its
# separate 30-second caller cap (command + 10-second grace + capture allowance).
# Keep those bounds; record an expired optional probe instead of waiting longer.
export BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS=10

/bin/bash "$repo_root/ios/launch_simulator.sh" --batch probe-control \
    "$BUSTER_IOS_NATIVE_CONTROL_ROOT/ide.app" >"$BUSTER_IOS_NATIVE_CONTROL_ROOT/run.log" 2>&1 &
runner=$!
status=0
wait "$runner" || status=$?
runner=
cat "$BUSTER_IOS_NATIVE_CONTROL_ROOT/run.log"
[[ $status -eq 1 ]]
grep -qF 'this is a real launch timeout' "$BUSTER_IOS_NATIVE_CONTROL_ROOT/run.log"
grep -qF 'last_completed_module=native_probe_control last_completed_index=0' "$BUSTER_IOS_NATIVE_CONTROL_ROOT/run.log"
grep -qE 'shutdown_disposition=(direct-success|verified-shutdown-after-timeout) result_status=1' "$BUSTER_IOS_CONSOLE_LOG.shutdown.status.log"
for probe in process-table unified-log crash-reports; do
    status_log="${BUSTER_IOS_CONSOLE_LOG%.log}.probe-control.log.diagnostic-$probe.status.log"
    output_log="${BUSTER_IOS_CONSOLE_LOG%.log}.probe-control.log.diagnostic-$probe.log"
    check_probe_receipt "$status_log" "$output_log" "$BUSTER_IOS_NATIVE_CONTROL_ROOT/run.log" \
        "$probe" "$BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS" "$BUSTER_IOS_NATIVE_CONTROL_ROOT/native-commands.log"
    # The retained receipt and raw stderr state availability, rather than
    # requiring simulator images to ship optional ps/log/sh tools.
    cat "$status_log"
done
echo "Native simulator launch-probe control passed; availability is recorded per probe."
