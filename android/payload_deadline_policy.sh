#!/usr/bin/env bash

# Select the Android CI payload watchdog from measured whole-suite baselines.
# After #3225's intended deep-chain coverage and #3265's overlap-scan fix,
# #3258 measured 75s Debug on a fast host and a 180s cutoff at the same
# checkout on a slow host (about 190s estimated completion). Debug's 240s
# watchdog is 3.2x that fast baseline / about 1.26x the slow estimate.
# Release remains 60s. See docs/android-payload-deadlines.md for the receipts.
buster_android_payload_deadline_policy() {
    local build_config=$1
    local config_timeout_seconds=
    local default_timeout_seconds

    case "$build_config" in
        Debug)
            default_timeout_seconds=240
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
