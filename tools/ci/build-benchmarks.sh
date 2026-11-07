#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Git/toolchain glue for the budgeted build barrier. Measurements stay in 9700x.yml; see docs/ci-builds.md.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
script="$root/tools/ci/build-benchmarks.sh"

cleanup()
{
    local directory=$1
    [[ "$directory" == "${RUNNER_TEMP:?}/benchmark-builds."* && -f "$directory/owner" ]]
    [[ "$(cat "$directory/owner")" == "$root" ]]
    if [ -f "$directory/base/.git" ]; then
        git -C "$root" worktree remove --force "$directory/base"
    fi
    rm -rf -- "$directory"
}

case "${1:-prepare}" in
    cleanup)
        [ "$#" -eq 2 ]
        # A failed preparation may already have removed its directory.
        if [ -e "$2" ]; then cleanup "$2"; fi
        exit 0
        ;;
    compiler)
        [ "$#" -eq 4 ]
        cd -- "$2"
        if [ "$4" = true ]; then
            ./build.sh shifts --config Release --jobs "${BUSTER_BUILD_JOBS:?}" "$3" main_unit_checker.c 0 1088 2112 3136
        else
            ./build.sh --config Release --jobs "${BUSTER_BUILD_JOBS:?}"
        fi
        exit 0
        ;;
    oracle)
        [ "$#" -eq 2 ]
        cd -- "$root"
        # The official libclang-cpp includes LLVM's dependencies.
        flags=$(llvm-config --cxxflags --ldflags)
        read -r -a flags_array <<< "${flags//$'\n'/ }"
        clang++ -O2 -Wall -Werror tools/preprocessor_pragma_oracle.cpp "${flags_array[@]}" -lclang-cpp -o "$2"
        "$2" --version
        exit 0
        ;;
    prepare) [ "$#" -le 1 ] ;;
    *) echo "usage: build-benchmarks.sh [prepare|cleanup DIRECTORY]" >&2; exit 2 ;;
esac

cd -- "$root"
: "${RUNNER_TEMP:?}" "${GITHUB_ENV:?}" "${GITHUB_OUTPUT:?}"
case "${AB_CHECKER:?}:${AB_TIER1:?}" in
    true:true|true:false|false:true|false:false) ;;
    *) echo "build-benchmarks: invalid selection" >&2; exit 2 ;;
esac
base=false
if [ "$AB_CHECKER" = true ] || [ "$AB_TIER1" = true ]; then
    [ "${GITHUB_EVENT_NAME:?}" = pull_request ]
    [[ "${AB_BASE_SHA:-}" =~ ^[0-9a-f]{40}$ ]]
    base=true
fi
directory=$(mktemp -d "$RUNNER_TEMP/benchmark-builds.XXXXXX")
[[ "$directory" != *$'\n'* && "$directory" != *$'\r'* ]]
printf '%s\n' "$root" > "$directory/owner"
# Publish the cleanup handle before doing anything that can fail.
printf 'directory=%s\n' "$directory" >> "$GITHUB_OUTPUT"
finish()
{
    local status=$?
    trap - EXIT
    if [ "$status" -ne 0 ]; then cleanup "$directory" || true; fi
    exit "$status"
}
trap finish EXIT
interrupted()
{
    local status=$1 pid
    trap '' INT TERM HUP
    # Includes the launch/assignment interval: Bash already tracks the background supervisor as a job.
    for pid in $(jobs -pr); do kill -TERM "$pid" 2>/dev/null || true; done
    wait || true
    exit "$status"
}
trap 'interrupted 130' INT
trap 'interrupted 143' TERM
trap 'interrupted 129' HUP
# Bash waits for the supervisor as a tracked job; the supervisor forwards cancellation and reaps its build groups.
# Do not remove their worktree from a signal trap before the supervisor has finished.
cc -std=c17 -O2 -Wall -Wextra -Werror "$root/tools/ci/build-budget.c" -o "$directory/build-budget"
commands=(build bash "$script" compiler "$root" "$directory/head" "$AB_CHECKER")
if [ "$base" = true ]; then
    if ! git cat-file -e "${AB_BASE_SHA}^{commit}" 2>/dev/null; then
        GIT_TERMINAL_PROMPT=0 timeout 60s git -c credential.helper= -c http.extraHeader= fetch \
            --no-tags --no-recurse-submodules --depth=1 https://github.com/buster14a/buster.git "$AB_BASE_SHA"
    fi
    git worktree add --detach "$directory/base" "$AB_BASE_SHA"
    [ "$(git -C "$directory/base" rev-parse HEAD)" = "$AB_BASE_SHA" ]
    commands+=(::: build bash "$script" compiler "$directory/base" "$directory/main" "$AB_CHECKER")
fi
oracle="$directory/preprocessor-pragma-oracle"
commands+=(::: single bash "$script" oracle "$oracle")
"$directory/build-budget" "${commands[@]}" &
wait "$!"
# Nothing is published until every compiler/oracle process has been reaped successfully.
compiler=$(./build.sh print-output --config Release)
test -x "$compiler"
test build/compiler -ef "$compiler"
if [ "$base" = true ]; then test -x "$directory/base/build/compiler"; fi
if [ "$AB_CHECKER" = true ]; then
    for side in main head; do
        for shift in 0 1088 2112 3136; do test -x "$directory/$side/compiler-$shift"; done
    done
fi
clang -dM -E -march="$BUSTER_HOST_ARCH" -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
{
    printf 'BUSTER_COMPILER=%s\n' "$compiler"
    printf 'BUSTER_PPF_PRAGMA_ORACLE=%s\n' "$oracle"
    printf 'BUSTER_BENCHMARK_BUILDS=%s\n' "$directory"
} >> "$GITHUB_ENV"
