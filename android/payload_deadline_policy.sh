#!/usr/bin/env bash

# Select the Android CI payload watchdog from measured whole-suite baselines.
# Debug uses about 42 seconds on a good hosted emulator and Release 16-18
# seconds. The defaults leave a hung-payload watchdog well beyond 1.5x ordinary
# host variance without turning Release's deadline into an unexamined copy of
# Debug's. See docs/android-payload-deadlines.md.
buster_android_payload_deadline_policy() {
    local build_config=$1
    local config_timeout_seconds=
    local default_timeout_seconds

    case "$build_config" in
        Debug)
            default_timeout_seconds=180
            config_timeout_seconds=${BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS:-}
            ;;
        Release)
            default_timeout_seconds=60
            config_timeout_seconds=${BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS:-}
            ;;
        *)
            # Preserve the standalone monitor default for callers that do not
            # participate in the Debug/Release Android CI batch.
            default_timeout_seconds=180
            ;;
    esac

    if [[ -n ${BUSTER_ANDROID_TEST_TIMEOUT_SECONDS:-} ]]; then
        android_payload_timeout_seconds=$BUSTER_ANDROID_TEST_TIMEOUT_SECONDS
    elif [[ -n $config_timeout_seconds ]]; then
        android_payload_timeout_seconds=$config_timeout_seconds
    else
        android_payload_timeout_seconds=$default_timeout_seconds
    fi

    # A 1.5x slowdown consumes one third of the deadline's original headroom.
    # Warn at 40% so the green 42s-of-60s observation from #728 (30% left)
    # would have signalled before the next slow host crossed the old deadline.
    android_payload_headroom_warning_percent=${BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT:-40}
}
