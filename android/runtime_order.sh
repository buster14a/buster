#!/usr/bin/env bash
# Executes a Buster-linked Android executable on the device and checks bionic's
# start-up order (#1243 runtime follow-up). The APK payload runs inside an app
# sandbox that cannot execute freshly written files, so this probe is linked on
# the host and run from /data/local/tmp through `adb shell` instead.
#
#   runtime_order.sh build <work-directory>
#       Builds a host `ide` (Debug, no tests) unless BUSTER_ANDROID_HOST_IDE
#       names one, then links android/runtime_order/probe.c with Buster against
#       a shared library and an IFUNC object built by the NDK's clang.
#   runtime_order.sh run <adb> <work-directory>
#       Pushes the probe and its library, runs it, and requires its
#       `status=ok` record and exit status 0.
#
# Both modes end with ANDROID_RUNTIME_ORDER_RESULT mode=<mode> phase=<phase>
# status=<status>. Every adb operation is bounded by the run_tests.sh adb
# deadlines, and the probe itself by BUSTER_ANDROID_RUNTIME_ORDER_TIMEOUT_SECONDS.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source_directory=$repo_root/android/runtime_order
# The probe's own lines; the result record is anchored so device output cannot
# forge a pass with a partial line.
probe_ok_pattern='^BUSTER_ANDROID_RUNTIME_ORDER preinit=1,2 dependency=3 program=4 arguments=1 ifunc=21,21 resolutions=1 status=ok$'
remote_directory=${BUSTER_ANDROID_RUNTIME_ORDER_REMOTE_DIRECTORY:-/data/local/tmp/buster-runtime-order}
adb_command_timeout_seconds=${BUSTER_ANDROID_ADB_COMMAND_TIMEOUT_SECONDS:-30}
probe_timeout_seconds=${BUSTER_ANDROID_RUNTIME_ORDER_TIMEOUT_SECONDS:-30}
android_serial=${BUSTER_ANDROID_SERIAL:-}

mode=${1:-}
phase=arguments
report() {
    local status=$?
    trap - EXIT INT TERM
    printf 'ANDROID_RUNTIME_ORDER_RESULT mode=%s phase=%s status=%s\n' "${mode:-none}" "$phase" "$status"
    exit "$status"
}
trap report EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for timeout_value in "$adb_command_timeout_seconds" "$probe_timeout_seconds"; do
    if [[ ! $timeout_value =~ ^[1-9][0-9]*$ ]]; then
        echo "error: Android runtime-order timeouts must be positive integers; got '$timeout_value'" >&2
        exit 1
    fi
done

build_probe() {
    local work_directory=$1
    local android_ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
    local api_level=${BUSTER_ANDROID_RUNTIME_ORDER_API:-35}
    local ndk_clang=${BUSTER_ANDROID_RUNTIME_ORDER_CLANG:-}
    local host_ide=${BUSTER_ANDROID_HOST_IDE:-}
    local host_build_directory=$work_directory/host-ide

    if [[ -z $ndk_clang ]]; then
        ndk_clang=$android_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
    fi
    if [[ ! -x $ndk_clang ]]; then
        echo "error: the Android runtime-order probe needs the NDK clang; '$ndk_clang' is not executable" >&2
        return 1
    fi
    mkdir -p "$work_directory"

    phase=host-ide
    if [[ -z $host_ide ]]; then
        # The probe only needs the linker, so a Debug tree without the test
        # suite keeps this inside the emulator boot it overlaps.
        cmake --warn-uninitialized -Werror=dev \
            -B "$host_build_directory" \
            -S "$repo_root" \
            -G "Ninja Multi-Config" \
            -DCMAKE_C_COMPILER="${BUSTER_ANDROID_HOST_CC:-clang}" \
            -DCMAKE_DEFAULT_BUILD_TYPE=Debug \
            -DCMAKE_CONFIGURATION_TYPES="Debug;Release" \
            -DCMAKE_LINKER_TYPE=DEFAULT \
            -DBUSTER_CI=ON \
            -DBUSTER_OPTIMIZE=OFF \
            -DBUSTER_SANITIZE=OFF \
            -DBUSTER_FUZZ_AVAILABLE=OFF \
            -DBUSTER_INCLUDE_TESTS=OFF \
            -DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF \
            -DBUSTER_DEVELOPER_TARGETS=OFF
        cmake --build "$host_build_directory" --config Debug --target ide
        host_ide=$host_build_directory/Debug/ide
    fi

    phase=probe-build
    "$ndk_clang" --target="x86_64-linux-android$api_level" -O2 -fPIC -shared -nostdlib \
        "$source_directory/dependency.c" -o "$work_directory/libruntime_order_dependency.so"
    "$ndk_clang" --target="x86_64-linux-android$api_level" -O2 -fPIC -c \
        "$source_directory/indirect.c" -o "$work_directory/indirect.o"
    "$host_ide" cc -target x86_64-linux-android -nostdinc -g0 \
        "$source_directory/probe.c" "$work_directory/indirect.o" \
        -L"$work_directory" -lruntime_order_dependency -o "$work_directory/runtime_order_probe"
    printf 'Android runtime-order probe linked by %s\n' "$host_ide"
}

adb_with_timeout() {
    local seconds=$1
    shift
    if [[ -n $android_serial ]]; then
        timeout --kill-after=10s "${seconds}s" "$adb" -s "$android_serial" "$@"
    else
        timeout --kill-after=10s "${seconds}s" "$adb" "$@"
    fi
}

run_probe() {
    local work_directory=$1
    local output
    local probe_status
    local file
    for file in runtime_order_probe libruntime_order_dependency.so; do
        if [[ ! -f $work_directory/$file ]]; then
            echo "error: Android runtime-order probe input '$work_directory/$file' is missing; run the build mode first" >&2
            return 1
        fi
    done

    phase=push
    adb_with_timeout "$adb_command_timeout_seconds" shell rm -rf "$remote_directory" >/dev/null 2>&1 || true
    adb_with_timeout "$adb_command_timeout_seconds" shell mkdir -p "$remote_directory"
    adb_with_timeout "$adb_command_timeout_seconds" push \
        "$work_directory/runtime_order_probe" "$work_directory/libruntime_order_dependency.so" "$remote_directory/"
    adb_with_timeout "$adb_command_timeout_seconds" shell chmod 755 "$remote_directory/runtime_order_probe"

    # /data/local/tmp is the shell user's executable scratch space; the app
    # data directory is not (W^X for app data on Android 10+).
    phase=run
    if output=$(adb_with_timeout "$probe_timeout_seconds" shell \
        "cd $remote_directory && LD_LIBRARY_PATH=$remote_directory ./runtime_order_probe; echo BUSTER_ANDROID_RUNTIME_ORDER_EXIT=\$?" 2>&1); then
        probe_status=0
    else
        probe_status=$?
    fi
    output=$(printf '%s\n' "$output" | tr -d '\r')
    printf '%s\n' "$output"
    adb_with_timeout "$adb_command_timeout_seconds" shell rm -rf "$remote_directory" >/dev/null 2>&1 || true
    if [[ $probe_status -ne 0 ]]; then
        echo "error: adb shell for the Android runtime-order probe exited with status $probe_status" >&2
        return 1
    fi
    if ! grep -qx 'BUSTER_ANDROID_RUNTIME_ORDER_EXIT=0' <<<"$output"; then
        echo "error: the Android runtime-order probe did not exit with status 0" >&2
        return 1
    fi
    if ! grep -qE "$probe_ok_pattern" <<<"$output"; then
        echo "error: bionic ran preinit entries, library constructors, program constructors or the IFUNC out of the expected order" >&2
        return 1
    fi
    echo "Android runtime-order probe passed"
}

case "$mode" in
    build)
        if [[ $# -ne 2 ]]; then
            echo "usage: $0 build <work-directory>" >&2
            exit 2
        fi
        build_probe "$2"
        ;;
    run)
        if [[ $# -ne 3 ]]; then
            echo "usage: $0 run <adb> <work-directory>" >&2
            exit 2
        fi
        adb=$2
        run_probe "$3"
        ;;
    *)
        echo "usage: $0 build <work-directory> | run <adb> <work-directory>" >&2
        exit 2
        ;;
esac
phase=done
