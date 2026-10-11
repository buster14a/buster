#!/usr/bin/env bash
# Hermetic controls for android/runtime_order.sh: fake NDK clang, host ide and
# adb stand in for the toolchain and the emulator, so the build/run plumbing,
# the anchored result record, the exit-status check and the result lines are
# exercised without a device. The real probe is run by the Android CI job.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
script=$repo_root/android/runtime_order.sh
test_root=$(mktemp -d "${TMPDIR:-/tmp}/buster-android-runtime-order.XXXXXX")
cleanup() {
    local status=$?
    trap - EXIT INT TERM
    rm -rf "$test_root"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

fake_bin=$test_root/bin
mkdir -p "$fake_bin"
# Writes the named output and records the invocation.
cat >"$fake_bin/clang" <<'FAKE_CLANG'
#!/usr/bin/env bash
printf 'clang %s\n' "$*" >>"$FAKE_RUNTIME_ORDER_LOG"
while [[ $# -gt 0 ]]; do
    if [[ $1 == -o ]]; then
        printf 'fake\n' >"$2"
        shift 2
    else
        shift
    fi
done
FAKE_CLANG
cat >"$fake_bin/ide" <<'FAKE_IDE'
#!/usr/bin/env bash
printf 'ide %s\n' "$*" >>"$FAKE_RUNTIME_ORDER_LOG"
while [[ $# -gt 0 ]]; do
    if [[ $1 == -o ]]; then
        printf 'probe\n' >"$2"
        shift 2
    else
        shift
    fi
done
FAKE_IDE
# FAKE_RUNTIME_ORDER_OUTPUT is what the probe prints; FAKE_RUNTIME_ORDER_EXIT its
# status, and FAKE_RUNTIME_ORDER_ADB_FAIL makes the named adb command fail.
cat >"$fake_bin/adb" <<'FAKE_ADB'
#!/usr/bin/env bash
printf 'adb %s\n' "$*" >>"$FAKE_RUNTIME_ORDER_LOG"
if [[ $1 == -s ]]; then
    shift 2
fi
if [[ $1 == "${FAKE_RUNTIME_ORDER_ADB_FAIL:-}" ]]; then
    exit 1
fi
if [[ $1 == shell && $2 == cd\ * ]]; then
    printf '%s\r\n' "$FAKE_RUNTIME_ORDER_OUTPUT"
    printf 'BUSTER_ANDROID_RUNTIME_ORDER_EXIT=%s\r\n' "${FAKE_RUNTIME_ORDER_EXIT:-0}"
fi
exit 0
FAKE_ADB
chmod +x "$fake_bin/clang" "$fake_bin/ide" "$fake_bin/adb"

ok_record='BUSTER_ANDROID_RUNTIME_ORDER preinit=1,2 dependency=3 program=4 arguments=1 ifunc=21,21 resolutions=1 status=ok'
work=$test_root/work
export FAKE_RUNTIME_ORDER_LOG=$test_root/tools.log
export BUSTER_ANDROID_RUNTIME_ORDER_CLANG=$fake_bin/clang
export BUSTER_ANDROID_HOST_IDE=$fake_bin/ide

expect_contains() {
    local needle=$1 path=$2
    if ! grep -qF -- "$needle" "$path"; then
        echo "assertion failed: '$needle' not found in $path" >&2
        cat "$path" >&2
        exit 1
    fi
}

bash "$script" build "$work" >"$test_root/build.out" 2>&1
expect_contains 'ANDROID_RUNTIME_ORDER_RESULT mode=build phase=done status=0' "$test_root/build.out"
expect_contains '--target=x86_64-linux-android35 -O2 -fPIC -shared -nostdlib' "$FAKE_RUNTIME_ORDER_LOG"
expect_contains "ide cc -target x86_64-linux-android -nostdinc -g0 $repo_root/android/runtime_order/probe.c $work/indirect.o" "$FAKE_RUNTIME_ORDER_LOG"
[[ -f $work/runtime_order_probe && -f $work/libruntime_order_dependency.so ]]

# Without a supplied ide, the selected native driver generates and builds one
# under the work directory; this script runs no CMake itself.
cat >"$fake_bin/driver" <<'FAKE_DRIVER'
#!/usr/bin/env bash
printf 'driver %s\n' "$*" >>"$FAKE_RUNTIME_ORDER_LOG"
if [[ $1 == build ]]; then
    mkdir -p "$3/Debug"
    cp "$(dirname "$0")/ide" "$3/Debug/ide"
fi
FAKE_DRIVER
chmod +x "$fake_bin/driver"
BUSTER_ANDROID_HOST_IDE= BUSTER_ANDROID_HOST_DRIVER=$fake_bin/driver bash "$script" build "$test_root/driven" >"$test_root/driven.out" 2>&1
expect_contains 'ANDROID_RUNTIME_ORDER_RESULT mode=build phase=done status=0' "$test_root/driven.out"
expect_contains "driver generate --build-directory $test_root/driven/host-ide --linker DEFAULT --no-include-tests" "$FAKE_RUNTIME_ORDER_LOG"
expect_contains "driver build --build-directory $test_root/driven/host-ide --config Debug -t ide" "$FAKE_RUNTIME_ORDER_LOG"
[[ -f $test_root/driven/runtime_order_probe ]]

# A missing NDK clang fails before any build.
if BUSTER_ANDROID_RUNTIME_ORDER_CLANG=$test_root/missing-clang bash "$script" build "$test_root/no-clang" >"$test_root/no-clang.out" 2>&1; then
    echo "assertion failed: a missing NDK clang built the probe" >&2
    exit 1
fi
expect_contains 'ANDROID_RUNTIME_ORDER_RESULT mode=build phase=arguments status=1' "$test_root/no-clang.out"

run_case() {
    local name=$1 expected=$2 phase=$3 output=$4 exit_status=$5 adb_fail=${6:-}
    local status=0
    FAKE_RUNTIME_ORDER_OUTPUT=$output FAKE_RUNTIME_ORDER_EXIT=$exit_status FAKE_RUNTIME_ORDER_ADB_FAIL=$adb_fail \
        BUSTER_ANDROID_SERIAL=emulator-5554 \
        bash "$script" run "$fake_bin/adb" "$work" >"$test_root/$name.out" 2>&1 || status=$?
    if [[ $status -ne $expected ]]; then
        echo "assertion failed: $name returned $status, expected $expected" >&2
        cat "$test_root/$name.out" >&2
        exit 1
    fi
    expect_contains "ANDROID_RUNTIME_ORDER_RESULT mode=run phase=$phase status=$expected" "$test_root/$name.out"
}

run_case pass 0 done "$ok_record" 0
expect_contains 'adb -s emulator-5554 push' "$FAKE_RUNTIME_ORDER_LOG"
expect_contains 'LD_LIBRARY_PATH=/data/local/tmp/buster-runtime-order ./runtime_order_probe' "$FAKE_RUNTIME_ORDER_LOG"
# Library constructors before the preinit entries, as an entry-stub call of
# them would order them; the record decides even with a zero exit status.
run_case library-first 1 run "${ok_record/preinit=1,2 dependency=3/preinit=2,3 dependency=1}" 0
expect_contains 'out of the expected order' "$test_root/library-first.out"
# A correct record with a failing exit status, and a record that only embeds
# the expected text, both fail.
run_case exit-status 1 run "$ok_record" 139
run_case forged 1 run "prefix $ok_record" 0
run_case push-failure 1 push "$ok_record" 0 push
rm -f "$work/runtime_order_probe"
run_case missing-input 1 arguments "$ok_record" 0
expect_contains 'run the build mode first' "$test_root/missing-input.out"

echo "Android runtime-order script tests passed"
