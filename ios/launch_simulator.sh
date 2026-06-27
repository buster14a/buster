#!/usr/bin/env bash
set -euo pipefail

# Boots an iOS simulator, installs the buster .app, launches it with the console
# attached and parses the in-app test result from stdout. Invoked by the CMake
# `test_all` target on iOS (mirrors the Android device launch flow).

app_bundle=${BUSTER_IOS_APP_BUNDLE:-}
if [[ -z $app_bundle || ! -d $app_bundle ]]; then
    echo "error: BUSTER_IOS_APP_BUNDLE must point at the built .app bundle (got '${app_bundle}')" >&2
    exit 1
fi

bundle_id=${BUSTER_IOS_BUNDLE_ID:-dev.buster.ide}
device_name=${BUSTER_IOS_SIMULATOR_DEVICE:-buster-ci}
launch_timeout_seconds=${BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS:-180}
boot_timeout_seconds=${BUSTER_IOS_BOOT_TIMEOUT_SECONDS:-180}
install_timeout_seconds=${BUSTER_IOS_INSTALL_TIMEOUT_SECONDS:-120}
codesign_timeout_seconds=${BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS:-60}
result_marker_success="BUSTER_IOS_RESULT: SUCCESS"
result_marker_failure="BUSTER_IOS_RESULT: FAILURE"

log_dir=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
console_log=${BUSTER_IOS_CONSOLE_LOG:-${log_dir%/}/buster-ios-console.log}

# macOS runners do not always ship GNU `timeout`; prefer it (or coreutils
# `gtimeout`) when present so a stuck simulator fails fast instead of hanging
# silently under Ninja's output buffering, but fall back to running unbounded.
timeout_bin=
if command -v timeout >/dev/null 2>&1; then
    timeout_bin=timeout
elif command -v gtimeout >/dev/null 2>&1; then
    timeout_bin=gtimeout
fi

run_with_timeout() {
    local seconds=$1
    shift
    if [[ -n $timeout_bin ]]; then
        "$timeout_bin" --kill-after=10s "$seconds" "$@"
    else
        "$@"
    fi
}

cleanup() {
    # Always shut our device down (created or reused) so booted simulators do not
    # accumulate across CI runs and exhaust memory.
    if [[ -n ${udid:-} ]]; then
        xcrun simctl shutdown "$udid" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

# Reuse a device by name, otherwise create one from the newest iOS runtime and an
# available iPhone device type.
udid=$(xcrun simctl list devices available -j | python3 -c '
import json, sys
name = sys.argv[1]
data = json.load(sys.stdin)["devices"]
for runtime_devices in data.values():
    for device in runtime_devices:
        if device.get("name") == name and device.get("isAvailable", True):
            print(device["udid"]); sys.exit(0)
' "$device_name")

# Newest available iOS runtime plus a compatible iPhone device type, as
# "<runtime-id>\t<devicetype-id>". The device type is taken from the runtime's
# own supportedDeviceTypes so it cannot be an incompatible (too-old) model.
find_runtime_and_device() {
    xcrun simctl list runtimes -j | python3 -c '
import json, re, sys
runtimes = [r for r in json.load(sys.stdin)["runtimes"]
            if r.get("isAvailable") and "iOS" in (r.get("identifier", "") + r.get("name", ""))]
def ver(r):
    return tuple(int(x) for x in re.findall(r"\d+", r.get("version", "0")))
runtimes.sort(key=ver)
for r in reversed(runtimes):
    iphones = [d for d in r.get("supportedDeviceTypes", []) if d.get("productFamily") == "iPhone"]
    if not iphones:
        continue
    def model(d):
        m = re.search(r"iPhone[- ](\d+)", d.get("name", ""))
        return int(m.group(1)) if m else -1
    best = max(iphones, key=model)
    print(r["identifier"] + "\t" + best["identifier"])
    sys.exit(0)
'
}

created_device=
if [[ -z $udid ]]; then
    runtime_and_device=$(find_runtime_and_device)
    # Xcode 16+/26 ships simulator runtimes on demand, so a fresh runner may have
    # none even though the iphonesimulator SDK is present. Downloading one is
    # multi-GB and silent under Ninja, so it is opt-in (off by default) to avoid
    # masking a misconfigured runner as a long hang.
    if [[ -z $runtime_and_device && ${BUSTER_IOS_DOWNLOAD_RUNTIME:-0} != 0 ]]; then
        echo "No iOS simulator runtime installed; downloading one via xcodebuild..."
        xcodebuild -downloadPlatform iOS || xcrun simctl runtime add "iOS" || true
        runtime_and_device=$(find_runtime_and_device)
    fi
    if [[ -z $runtime_and_device ]]; then
        echo "error: no available iOS simulator runtime found." >&2
        echo "Install one on the runner (e.g. 'xcodebuild -downloadPlatform iOS')," >&2
        echo "or set BUSTER_IOS_DOWNLOAD_RUNTIME=1 to let this script download it." >&2
        xcrun simctl list runtimes >&2 || true
        exit 1
    fi
    runtime=${runtime_and_device%%$'\t'*}
    device_type=${runtime_and_device##*$'\t'}
    if [[ -z $device_type ]]; then
        echo "error: no compatible iPhone simulator device type found" >&2
        exit 1
    fi

    echo "Creating simulator '$device_name' (type=$device_type, runtime=$runtime)"
    udid=$(xcrun simctl create "$device_name" "$device_type" "$runtime")
    created_device=1
fi

echo "Using simulator $device_name ($udid)"

# Free memory before booting: leftover booted simulators from previous runs
# accumulate and cause "insufficient system memory / too many processes".
xcrun simctl shutdown all 2>/dev/null || true
xcrun simctl delete unavailable 2>/dev/null || true

xcrun simctl boot "$udid" 2>/dev/null || true
if ! run_with_timeout "$boot_timeout_seconds" xcrun simctl bootstatus "$udid" -b; then
    # One retry after forcing everything else down, in case memory was the cause.
    echo "Boot failed; shutting down all simulators and retrying once..." >&2
    xcrun simctl shutdown all 2>/dev/null || true
    sleep 5
    xcrun simctl boot "$udid" 2>/dev/null || true
    run_with_timeout "$boot_timeout_seconds" xcrun simctl bootstatus "$udid" -b
fi

# Recent simulators require at least an ad-hoc signature to launch the app.
run_with_timeout "$codesign_timeout_seconds" \
    codesign --force --sign - --timestamp=none "$app_bundle" >/dev/null 2>&1 || true

echo "Installing $app_bundle"
run_with_timeout "$install_timeout_seconds" xcrun simctl install "$udid" "$app_bundle"

echo "Launching $bundle_id (timeout ${launch_timeout_seconds}s)"
: >"$console_log"
# --console-pty streams the app's stdout/stderr until it exits; the app exits
# itself once the in-app tests finish. The trailing "test" argument is forwarded
# to the app's argv and is what puts the IDE into test mode (process_arguments ->
# ide_state.test): without it the GUI render loop runs forever (loop_times =
# UINT64_MAX) and the BUSTER_IOS_RESULT marker is never printed.
set +e
run_with_timeout "$launch_timeout_seconds" xcrun simctl launch --console-pty "$udid" "$bundle_id" test 2>&1 | tee "$console_log"
set -e

if grep -qF "$result_marker_success" "$console_log"; then
    echo "iOS tests passed."
    exit 0
fi
if grep -qF "$result_marker_failure" "$console_log"; then
    echo "error: iOS tests reported failure." >&2
    exit 1
fi

echo "error: did not observe a buster test result marker within ${launch_timeout_seconds}s." >&2
echo "----- console log tail -----" >&2
tail -n 60 "$console_log" >&2 || true
exit 1
