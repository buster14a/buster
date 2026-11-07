#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Disposable repositories and stand-in toolchains; no compiler build, network or benchmark machine is needed.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
temporary=$(mktemp -d)
trap 'for job in $(jobs -pr); do kill -TERM "$job" 2>/dev/null || true; done; wait || true; rm -rf -- "$temporary"' EXIT
cc -std=c17 -O2 -Wall -Wextra -Werror "$root/tools/ci/build-budget.c" -o "$temporary/budget"
cc -std=c17 -O2 -Wall -Wextra -Werror "$root/tests/ci/build-budget.c" -o "$temporary/probe"
"$temporary/probe"
mapfile -t cpus < <("$temporary/probe" --cores)
[ "${#cpus[@]}" -gt 0 ]
probe="$temporary/probe"
budget="$temporary/budget"
mkdir "$temporary/serial"
taskset -c "${cpus[0]}" "$budget" build "$probe" --probe "$temporary/serial" head - 0 ::: build "$probe" --probe "$temporary/serial" base - 0 ::: single "$probe" --probe "$temporary/serial" oracle - 0
[ "$(find "$temporary/serial" -type f | wc -l)" -eq 3 ]
if [ "${#cpus[@]}" -ge 2 ]; then
    mkdir "$temporary/paired"
    # Both commands must start before either may finish: not a wall-time speedup assertion.
    taskset -c "${cpus[0]},${cpus[1]}" "$budget" build "$probe" --probe "$temporary/paired" head base 0 ::: build "$probe" --probe "$temporary/paired" base head 0
    head_mask=$(awk '{print $3}' "$temporary/paired/head")
    base_mask=$(awk '{print $3}' "$temporary/paired/base")
    [ "$head_mask" != "$base_mask" ]
fi
if "$budget" build true ::: > /dev/null 2>&1; then echo "accepted empty command" >&2; exit 1; fi
mkdir "$temporary/failure"
status=0
# This is bounded even on one allowed core, where it fails before the hold command is started.
"$budget" build "$probe" --probe "$temporary/failure" fail - 7 ::: build "$probe" --probe "$temporary/failure" sibling - hold || status=$?
[ "$status" -eq 7 ]
for file in "$temporary/failure/"*; do
    read -r pid rest < "$file"
    if kill -0 "$pid" 2>/dev/null; then echo "surviving failed-build process $pid" >&2; exit 1; fi
done
mkdir "$temporary/cancel"
"$budget" build "$probe" --probe "$temporary/cancel" parent - hold &
supervisor=$!
for ((try=0; try<500; try++)); do if [ -s "$temporary/cancel/parent-child" ]; then break; fi; sleep 0.01; done
test -f "$temporary/cancel/parent-child"
kill -TERM "$supervisor"
status=0
wait "$supervisor" || status=$?
[ "$status" -eq 143 ]
for file in "$temporary/cancel/"*; do
    read -r pid rest < "$file"
    if kill -0 "$pid" 2>/dev/null; then echo "surviving cancelled process $pid" >&2; exit 1; fi
done

mkdir "$temporary/orphan"
status=0
"$budget" build "$probe" --probe "$temporary/orphan" leader - orphan || status=$?
test "$status" -eq 125
for file in "$temporary/orphan/"*; do
    if [ -s "$file" ]; then
        read -r pid rest < "$file"
        if kill -0 "$pid" 2>/dev/null; then echo "surviving orphan $pid" >&2; exit 1; fi
    fi
done
status=0
"$budget" build /no-such-buster-build-command || status=$?
test "$status" -eq 127

# build.sh/Ninja use nested groups. Escaped descendants must be reaped too, both on cancellation and leader exit.
for mode in escape escape-orphan; do
    mkdir "$temporary/$mode"
    "$budget" build "$probe" --probe "$temporary/$mode" leader - "$mode" &
    supervisor=$!
    for ((try=0; try<500; try++)); do if [ -s "$temporary/$mode/leader-child" ]; then break; fi; sleep 0.01; done
    test -s "$temporary/$mode/leader-child"
    expected=125
    if [ "$mode" = escape ]; then kill -TERM "$supervisor"; expected=143; fi
    status=0
    wait "$supervisor" || status=$?
    test "$status" -eq "$expected"
    for file in "$temporary/$mode/"*; do
        read -r pid rest < "$file"
        if kill -0 "$pid" 2>/dev/null; then echo "surviving nested-group process $pid" >&2; exit 1; fi
    done
done

# Real Git worktree lifecycle, but tiny fake compiler/LLVM commands. The budget/probe remain the real C programs.
repo="$temporary/repo"
mkdir -p "$repo/tools/ci" "$temporary/bin" "$temporary/runner"
cp "$root/tools/ci/build-budget.c" "$root/tools/ci/build-benchmarks.sh" "$repo/tools/ci/"
cat > "$repo/build.sh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
if [ "${1:-}" = print-output ]; then printf '%s/build/compiler\n' "$PWD"; exit 0; fi
side=head
if [ -f .git ]; then side=base; fi
printf '%s %s\n' "$side" "$*" >> "$FIXTURE_LOG/calls"
outcome=0
if [ "${FIXTURE_FAIL:-}" = "$side" ]; then outcome=7; fi
if [ "${FIXTURE_HOLD:-}" = true ]; then outcome=hold; fi
"$FIXTURE_PROBE" --probe "$FIXTURE_LOG" "$side" - "$outcome"
shifted=false
if [ "${1:-}" = shifts ]; then shifted=true; shift; fi
test "$1" = --config && test "$2" = Release && test "$3" = --jobs && test "$4" = "$BUSTER_BUILD_JOBS"
shift 4
mkdir -p build
printf '#!/bin/sh\nexit 0\n' > build/compiler
chmod +x build/compiler
if [ "$shifted" = true ]; then
    output=$1; shift
    test "$1" = main_unit_checker.c; shift
    test "$*" = '0 1088 2112 3136'
    mkdir -p "$output"
    for shift in "$@"; do ln -f build/compiler "$output/compiler-$shift"; done
fi
SH
chmod +x "$repo/build.sh"
cat > "$temporary/bin/llvm-config" <<'SH'
#!/usr/bin/env bash
printf '%s\n' -Ifixture
SH
cat > "$temporary/bin/clang++" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
outcome=0
if [ "${FIXTURE_FAIL:-}" = oracle ]; then outcome=7; fi
"$FIXTURE_PROBE" --probe "$FIXTURE_LOG" oracle - "$outcome"
last=${!#}
printf '#!/bin/sh\nexit 0\n' > "$last"
chmod +x "$last"
SH
cat > "$temporary/bin/clang" <<'SH'
#!/usr/bin/env bash
printf '%s\n' '#define FIXTURE 1'
SH
chmod +x "$temporary/bin/"*
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
git -C "$repo" -c init.templateDir= init -q
git -C "$repo" add .
git -C "$repo" -c user.name=Fixture -c user.email=fixture@example.invalid commit -qm initial
base_sha=$(git -C "$repo" rev-parse HEAD)
export PATH="$temporary/bin:$PATH" RUNNER_TEMP="$temporary/runner" BUSTER_HOST_ARCH=znver5
export FIXTURE_PROBE="$probe" AB_BASE_SHA="$base_sha" GITHUB_EVENT_NAME=pull_request
export GITHUB_ENV="$temporary/environment" GITHUB_OUTPUT="$temporary/outputs"
helper="$repo/tools/ci/build-benchmarks.sh"

reset_case()
{
    export FIXTURE_LOG="$temporary/$1"
    mkdir "$FIXTURE_LOG"
    : > "$GITHUB_ENV"
    : > "$GITHUB_OUTPUT"
    unset FIXTURE_FAIL FIXTURE_HOLD
}

for selection in false:false true:false false:true true:true; do
    reset_case "select-${selection/:/-}"
    IFS=: read -r AB_CHECKER AB_TIER1 <<< "$selection"
    export AB_CHECKER AB_TIER1
    bash "$helper"
    directory=$(sed -n 's/^BUSTER_BENCHMARK_BUILDS=//p' "$GITHUB_ENV")
    test -n "$directory"
    test -x "$directory/preprocessor-pragma-oracle"
    test "$(grep -c '^head ' "$FIXTURE_LOG/calls")" -eq 1
    if [ "$selection" = false:false ]; then
        test "$(wc -l < "$FIXTURE_LOG/calls")" -eq 1
        test ! -d "$directory/base"
    else
        test "$(grep -c '^base ' "$FIXTURE_LOG/calls")" -eq 1
        test "$(git -C "$directory/base" rev-parse HEAD)" = "$base_sha"
        test -x "$directory/base/build/compiler"
    fi
    if [ "$AB_CHECKER" = true ]; then
        for side in main head; do
            for shift in 0 1088 2112 3136; do test -x "$directory/$side/compiler-$shift"; done
        done
    else
        test ! -e "$directory/main/compiler-0"
        test ! -e "$directory/head/compiler-0"
    fi
    # Successful publication is a process barrier; the programs can now be measured without these processes.
    for side in head base oracle; do
        if [ -f "$FIXTURE_LOG/$side" ]; then
            read -r pid rest < "$FIXTURE_LOG/$side"
            if kill -0 "$pid" 2>/dev/null; then echo "publication before build completion" >&2; exit 1; fi
        fi
    done
    bash "$helper" cleanup "$directory"
    bash "$helper" cleanup "$directory"
    test "$(git -C "$repo" worktree list --porcelain | grep -c '^worktree ')" -eq 1
done

for event in push workflow_dispatch; do
    reset_case "event-$event"
    export GITHUB_EVENT_NAME="$event" AB_CHECKER=false AB_TIER1=false
    bash "$helper"
    directory=$(sed -n 's/^BUSTER_BENCHMARK_BUILDS=//p' "$GITHUB_ENV")
    test "$(wc -l < "$FIXTURE_LOG/calls")" -eq 1
    test ! -d "$directory/base"
    bash "$helper" cleanup "$directory"
done
export GITHUB_EVENT_NAME=pull_request

for side in head base oracle; do
    reset_case "fail-$side"
    export AB_CHECKER=true AB_TIER1=true FIXTURE_FAIL="$side"
    status=0
    bash "$helper" || status=$?
    test "$status" -ne 0
    test ! -s "$GITHUB_ENV"
    directory=$(sed -n 's/^directory=//p' "$GITHUB_OUTPUT")
    test ! -e "$directory"
    test "$(git -C "$repo" worktree list --porcelain | grep -c '^worktree ')" -eq 1
done

reset_case wrong-event
export GITHUB_EVENT_NAME=push AB_CHECKER=true AB_TIER1=false
if bash "$helper"; then echo "non-PR base was built" >&2; exit 1; fi
test ! -s "$GITHUB_ENV"
reset_case bad-sha
export GITHUB_EVENT_NAME=pull_request AB_BASE_SHA=bad
if bash "$helper"; then echo "bad base SHA was accepted" >&2; exit 1; fi
test ! -s "$GITHUB_ENV"

reset_case cancel-helper
export AB_BASE_SHA="$base_sha" AB_CHECKER=true AB_TIER1=true FIXTURE_HOLD=true
bash "$helper" &
outer=$!
for ((try=0; try<500; try++)); do if [ -s "$FIXTURE_LOG/head-child" ]; then break; fi; sleep 0.01; done
test -f "$FIXTURE_LOG/head-child"
kill -TERM "$outer"
status=0
wait "$outer" || status=$?
test "$status" -eq 143
test ! -s "$GITHUB_ENV"
test "$(git -C "$repo" worktree list --porcelain | grep -c '^worktree ')" -eq 1
for side in head head-child base base-child oracle; do
    if [ -f "$FIXTURE_LOG/$side" ]; then
        read -r pid rest < "$FIXTURE_LOG/$side"
        if kill -0 "$pid" 2>/dev/null; then echo "surviving helper process $pid" >&2; exit 1; fi
    fi
done
echo "build budget and benchmark preparation fixtures: passed"
