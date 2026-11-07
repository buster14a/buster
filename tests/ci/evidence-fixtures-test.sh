#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Exercise the actual Bash/Git entry-point glue with disposable histories, not GitHub or the compiler.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/tests/ci/evidence-fixtures.sh"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/scratch"
checks=0
fail() { printf 'evidence fixture selection: %s\n' "$*" >&2; exit 1; }
paths_case()
{
    local expected=$1 status=0
    shift
    : > "$work/paths"
    if [ "$#" -gt 0 ]; then printf '%s\0' "$@" > "$work/paths"; fi
    fixture_paths_required "$work/paths" || status=$?
    [ "$status" -eq "$expected" ] || fail "paths: expected $expected, got $status ($*)"
    checks=$((checks + 1))
}
paths_case 1 src/buster/compiler/c_checker.c src/buster/include/stdint.h docs/c-checker.md README.md
paths_case 1 'src/space name.c' $'src/line\nbreak.h'
paths_case 0
for path in tools/ci/nightly.c tools/ci/cached-download.sh tests/ci/nightly.sh tests/ci/evidence-fixtures.sh \
    .github/workflows/9700x.yml .github/workflows/nightly-backfill.yml build.sh CMakeLists.txt cmake/Build.cmake \
    .githooks/commit-msg docs/ci.md docs/ci-builds.md docs/commands/ci.md generated/new.inc src/generator.sh unknown; do
    paths_case 0 src/compiler.c "$path"
done
printf 'src/compiler.c' > "$work/paths"
if ! fixture_paths_required "$work/paths"; then fail 'an unterminated path list was accepted'; fi
checks=$((checks + 1))

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
repo="$work/repo"
git -c init.templateDir= init -q -b main "$repo"
git -C "$repo" config user.name Fixture
git -C "$repo" config user.email fixture@example.invalid
mkdir -p "$repo/src" "$repo/tools/ci"
printf 'base\n' > "$repo/src/compiler.c"
printf 'infrastructure\n' > "$repo/tools/ci/helper.sh"
git -C "$repo" add .
git -C "$repo" commit -qm base
base=$(git -C "$repo" rev-parse HEAD)
git -C "$repo" checkout -qb feature
printf 'changed compiler\n' > "$repo/src/compiler.c"
git -C "$repo" commit -qam compiler
head=$(git -C "$repo" rev-parse HEAD)
git -C "$repo" checkout -q main
git -C "$repo" merge --no-ff -qm merge feature
merge=$(git -C "$repo" rev-parse HEAD)
export GITHUB_ACTIONS=true GITHUB_EVENT_NAME=pull_request GITHUB_SHA="$merge" GITHUB_EVENT_PATH="$work/event.json"
event()
{
    jq -n --arg base "$1" --arg head "$2" '{pull_request:{base:{sha:$base},head:{sha:$head}}}' > "$GITHUB_EVENT_PATH"
}
required_case()
{
    local expected=$1 status=0
    shift
    fixture_required "$work/scratch" "$@" || status=$?
    [ "$status" -eq "$expected" ] || fail "expected $expected, got $status: $fixture_reason"
    checks=$((checks + 1))
}
cd "$repo"
event "$base" "$head"
required_case 1
required_case 0 true
GITHUB_ACTIONS='' required_case 0
GITHUB_EVENT_NAME='' required_case 0
for event_name in push schedule workflow_dispatch merge_group pull_request_target; do GITHUB_EVENT_NAME=$event_name required_case 0; done
GITHUB_EVENT_PATH="$work/missing" required_case 0
GITHUB_SHA="$base" required_case 0
printf '{}\n' > "$GITHUB_EVENT_PATH"
required_case 0
printf 'not json\n' > "$GITHUB_EVENT_PATH"
required_case 0
event "$head" "$base"
required_case 0
event "$base" "$head"
printf 'uncommitted\n' >> src/compiler.c
required_case 0
git checkout -q -- src/compiler.c
required_case 1
git checkout -q --detach "$head"
GITHUB_SHA="$head" required_case 0
git checkout -q --detach "$merge"
# A checkout too shallow to prove both parents must not skip.
git -c init.templateDir= clone -q --depth 1 "file://$repo" "$work/shallow"
(cd "$work/shallow" && required_case 0)
# Git failures must not be mistaken for an empty/unrelated diff.
mkdir "$work/bin"
printf '#!/bin/sh\nexit 1\n' > "$work/bin/git"
chmod +x "$work/bin/git"
PATH="$work/bin:$PATH" required_case 0
# A rename out of infrastructure records both sides because the real diff disables rename detection.
git checkout -q feature
git mv tools/ci/helper.sh src/moved.c
git commit -qm rename
head=$(git rev-parse HEAD)
git checkout -q --detach "$base"
git merge --no-ff -qm rename-merge feature
export GITHUB_SHA=$(git rev-parse HEAD)
event "$base" "$head"
required_case 0

# Timing waits for the child's cleanup and preserves its failure, rather than masking it with a log command.
status=0
fixture_run failure bash -c 'trap '\''printf cleaned > "$1"'\'' EXIT; exit 7' _ "$work/cleaned" > "$work/run.log" || status=$?
[ "$status" -eq 7 ] || fail 'the reporter lost the child exit status'
[ "$(cat "$work/cleaned")" = cleaned ] || fail 'the reporter returned before cleanup'
grep -Eq 'completed in [0-9]+s including cleanup \(status=7\)' "$work/run.log" || fail 'missing failure timing'
fixture_run success bash -c 'exit 0' > "$work/run.log"
grep -q 'status=0' "$work/run.log" || fail 'missing success timing'
printf 'evidence fixture selection: %s cases and reporter cleanup/status checks passed\n' "$checks"
