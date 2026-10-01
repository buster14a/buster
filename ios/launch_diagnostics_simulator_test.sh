#!/usr/bin/env bash
# Hosted ARM64 control: synthetic payload, real simulator boot/probes/shutdown.
# No compiler build or installed test app is needed to check probe availability.
set -euo pipefail
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
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
    grep -qF 'capture_receipt=complete' "$status_log"
    grep -qF 'deadline_seconds=10 output_limit_bytes=65536' "$status_log"
    grep -qE 'status=[0-9]+ native_status=([0-9]+|unavailable) capture_status=0' "$status_log"
    [[ $(wc -c <"$output_log") -le 65536 ]]
    # The retained receipt and raw stderr state availability, rather than
    # requiring simulator images to ship optional ps/log/sh tools.
    cat "$status_log"
done
for command in ps log sh; do
    grep -qE "^simctl spawn [[:xdigit:]-]+ $command " "$BUSTER_IOS_NATIVE_CONTROL_ROOT/native-commands.log"
done
echo "Native simulator launch-probe control passed; availability is recorded per probe."
