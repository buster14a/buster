#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=android/payload_deadline_policy.sh
source "$repo_root/android/payload_deadline_policy.sh"

assert_policy() (
    set -euo pipefail
    local build_config=$1
    local expected_timeout_seconds=$2
    local expected_warning_percent=$3

    buster_android_payload_deadline_policy "$build_config"
    if [[ $android_payload_timeout_seconds != "$expected_timeout_seconds" ]]; then
        printf 'error: %s timeout was %s, expected %s\n' \
            "$build_config" "$android_payload_timeout_seconds" "$expected_timeout_seconds" >&2
        exit 1
    fi
    if [[ $android_payload_headroom_warning_percent != "$expected_warning_percent" ]]; then
        printf 'error: %s warning margin was %s%%, expected %s%%\n' \
            "$build_config" "$android_payload_headroom_warning_percent" "$expected_warning_percent" >&2
        exit 1
    fi
)

unset BUSTER_ANDROID_TEST_TIMEOUT_SECONDS
unset BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS
unset BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS
unset BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT
assert_policy Debug 240 40
assert_policy Release 60 40
assert_policy standalone 180 40

BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS=90 assert_policy Debug 90 40
BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS=45 assert_policy Release 45 40
BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS=90 assert_policy Release 60 40
BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS=45 assert_policy Debug 240 40

BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=12 \
    BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS=90 \
    BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS=45 \
    assert_policy Debug 12 40
BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=12 \
    BUSTER_ANDROID_DEBUG_TEST_TIMEOUT_SECONDS=90 \
    BUSTER_ANDROID_RELEASE_TEST_TIMEOUT_SECONDS=45 \
    assert_policy Release 12 40
BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT=35 assert_policy Debug 240 35

# #3258: the exact-source fast Debug payload took 75s; completed modules
# slowed by 2.55x on the failed host. Round up to 2.6x for watchdog sizing.
# This 195s projection is diagnostic, not an observed completed slow run.
debug_baseline_seconds=75
debug_slow_seconds=$((debug_baseline_seconds * 26 / 10))
release_slow_seconds=52
buster_android_payload_deadline_policy Debug
debug_deadline_seconds=$android_payload_timeout_seconds
warning_seconds=$((debug_deadline_seconds * android_payload_headroom_warning_percent / 100))
if (( debug_deadline_seconds != debug_baseline_seconds * 16 / 5 ||
      debug_slow_seconds <= 180 || debug_slow_seconds >= debug_deadline_seconds )); then
    echo "error: measured Debug host variance does not fit the sized watchdog" >&2
    exit 1
fi
if (( debug_deadline_seconds - debug_slow_seconds >= warning_seconds )); then
    echo "error: projected slow Debug payload would not warn" >&2
    exit 1
fi
buster_android_payload_deadline_policy Release
if (( release_slow_seconds >= android_payload_timeout_seconds )); then
    echo "error: recorded slow Release payload no longer fits its watchdog" >&2
    exit 1
fi
warning_seconds=$((android_payload_timeout_seconds * android_payload_headroom_warning_percent / 100))
if (( android_payload_timeout_seconds - release_slow_seconds >= warning_seconds )); then
    echo "error: recorded slow Release payload would not warn" >&2
    exit 1
fi

echo "Android payload deadline policy passed"

# Exercise the production batch wrapper with fake build/device tools so the
# policy cannot pass in isolation while test_ci.sh forgets to propagate it.
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-android-deadline-policy.XXXXXX")
cleanup() {
    local status=$?
    trap - EXIT INT TERM
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$test_root/bin" "$test_root/sdk/ndk/fake/build/cmake"
: >"$test_root/sdk/ndk/fake/build/cmake/android.toolchain.cmake"
cat >"$test_root/bin/cmake" <<'FAKE_CMAKE'
#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} == --version ]]; then
    echo "cmake version deadline-policy-fake"
elif [[ ${1:-} == --build ]]; then
    build_directory=$2
    shift 2
    build_config=
    while [[ $# -gt 0 ]]; do
        if [[ $1 == --config ]]; then
            build_config=$2
            shift 2
        else
            shift
        fi
    done
    mkdir -p "$build_directory"
    printf '%s apk\n' "$build_config" >"$build_directory/buster.apk"
fi
FAKE_CMAKE
cat >"$test_root/bin/ninja" <<'FAKE_NINJA'
#!/usr/bin/env bash
echo "1.13.0 deadline-policy-fake"
FAKE_NINJA
cat >"$test_root/bin/adb" <<'FAKE_ADB'
#!/usr/bin/env bash
exit 0
FAKE_ADB
cat >"$test_root/start.sh" <<'FAKE_START'
#!/usr/bin/env bash
exit 0
FAKE_START
cat >"$test_root/payload.sh" <<'FAKE_PAYLOAD'
#!/usr/bin/env bash
set -euo pipefail
printf '%s %s %s\n' \
    "$BUSTER_ANDROID_TEST_CONFIG" \
    "$BUSTER_ANDROID_TEST_TIMEOUT_SECONDS" \
    "$BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT" >>"$BUSTER_ANDROID_POLICY_LOG"
FAKE_PAYLOAD
chmod +x "$test_root/bin/"* "$test_root/start.sh" "$test_root/payload.sh"

run_batch_policy_case() {
    local case_name=$1
    local configs=$2
    local expected_log=$3
    local timeout_override=${4:-}
    local warning_override=${5:-}
    local output="$test_root/$case_name.out"
    local policy_log="$test_root/$case_name.log"
    local build_directory="$test_root/build-$case_name"
    local config_args=()
    read -r -a config_args <<<"$configs"

    (
        unset BUSTER_ANDROID_TEST_TIMEOUT_SECONDS
        unset BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT
        if [[ -n $timeout_override ]]; then
            export BUSTER_ANDROID_TEST_TIMEOUT_SECONDS=$timeout_override
        fi
        if [[ -n $warning_override ]]; then
            export BUSTER_ANDROID_TEST_HEADROOM_WARNING_PERCENT=$warning_override
        fi
        export PATH="$test_root/bin:$PATH"
        export ANDROID_HOME="$test_root/sdk"
        export ANDROID_NDK_HOME="$test_root/sdk/ndk/fake"
        export BUSTER_ANDROID_BUILD_DIRECTORY="$build_directory"
        export BUSTER_ANDROID_START_SCRIPT="$test_root/start.sh"
        export BUSTER_ANDROID_RUN_TESTS_SCRIPT="$test_root/payload.sh"
        export BUSTER_ANDROID_ADB="$test_root/bin/adb"
        export BUSTER_ANDROID_POLICY_LOG="$policy_log"
        bash "$repo_root/android/test_ci.sh" "${config_args[@]}" >"$output" 2>&1
    )

    if [[ $(cat "$policy_log") != "$expected_log" ]]; then
        echo "error: $case_name propagated the wrong policy" >&2
        cat "$policy_log" >&2
        exit 1
    fi
}

run_batch_policy_case defaults "Debug Release" $'Debug 240 40\nRelease 60 40'
run_batch_policy_case override "Release" 'Release 12 35' 12 35
if ! grep -qF 'ANDROID_DEADLINE_POLICY config=Debug timeout_seconds=240 headroom_warning_percent=40' "$test_root/defaults.out" ||
   ! grep -qF 'ANDROID_DEADLINE_POLICY config=Release timeout_seconds=60 headroom_warning_percent=40' "$test_root/defaults.out" ||
   ! grep -qF 'ANDROID_DEADLINE_POLICY config=Release timeout_seconds=12 headroom_warning_percent=35' "$test_root/override.out"; then
    echo "error: Android batch policy diagnostics are incomplete" >&2
    cat "$test_root/defaults.out" >&2
    cat "$test_root/override.out" >&2
    exit 1
fi

echo "Android payload deadline batch integration passed"
