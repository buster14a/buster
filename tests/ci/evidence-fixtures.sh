#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Bash entry-point glue for the existing Bash/Git suites; docs/ci-evidence-fixtures.md.
set -euo pipefail

# 0: run, 1: skip. Only a nonempty, completely read list of known unrelated paths may skip.
fixture_paths_required()
{
    local path='' count=0
    while IFS= read -r -d '' path; do
        count=$((count + 1))
        case "$path" in
            docs/ci*|docs/commands/ci*) return 0 ;;
            src/*.c|src/*.h|docs/*.md|README.md) ;;
            *) return 0 ;;
        esac
    done < "$1"
    [ "$count" -gt 0 ] && [ -z "$path" ] || return 0
    return 1
}

# Missing or ambiguous inputs run everything. This selects fixture work only: the real evidence
# command still runs on every event and decides all admission and coverage outputs itself.
fixture_required()
{
    local scratch=$1 force=${2:-false} metadata base head actual parents
    fixture_reason='full fixtures requested, or not a verified pull-request checkout'
    [ "$force" = false ] || return 0
    [ "${GITHUB_ACTIONS:-}" = true ] && [ "${GITHUB_EVENT_NAME:-}" = pull_request ] || return 0
    [ -r "${GITHUB_EVENT_PATH:-}" ] || return 0
    metadata=$(jq -er '[.pull_request.base.sha, .pull_request.head.sha] |
        if all(.[]; type == "string" and test("^[0-9a-f]{40}$")) then join(" ") else error("invalid PR commits") end' \
        "$GITHUB_EVENT_PATH" 2>/dev/null) || return 0
    read -r base head <<< "$metadata"
    actual=$(git rev-parse --verify HEAD 2>/dev/null) || return 0
    [ "$actual" = "${GITHUB_SHA:-}" ] || return 0
    parents=$(git show -s --format=%P HEAD 2>/dev/null) || return 0
    [ "$parents" = "$base $head" ] || return 0
    git cat-file -e "$base^{commit}" 2>/dev/null && git cat-file -e "$head^{commit}" 2>/dev/null || return 0
    git diff --quiet --no-ext-diff --no-textconv HEAD -- 2>/dev/null || return 0
    # No rename detection: moving an infrastructure file into src must retain the old path's deletion.
    # NUL records keep spaces, tabs and newlines from hiding another changed path.
    git diff --no-ext-diff --no-textconv --no-renames --name-only -z "$base" "$actual" -- > "$scratch/paths" 2>/dev/null || return 0
    fixture_reason='infrastructure, unknown, empty or incomplete change list'
    if fixture_paths_required "$scratch/paths"; then return 0; fi
    fixture_reason='verified PR changes only compiler C/headers or unrelated Markdown; infrastructure is unchanged'
    return 1
}

fixture_run()
{
    local name=$1 started=$SECONDS status=0
    shift
    if [ "${GITHUB_ACTIONS:-}" = true ]; then printf '::group::%s fixtures\n' "$name"; fi
    printf 'fixtures: %s: starting\n' "$name"
    "$@" || status=$?
    printf 'fixtures: %s: completed in %ss including cleanup (status=%s)\n' "$name" "$((SECONDS - started))" "$status"
    if [ "${GITHUB_ACTIONS:-}" = true ]; then printf '::endgroup::\n'; fi
    return "$status"
}

fixture_main()
{
    local suite=${1:-} force=false
    case "$suite" in nightly|caches|build-benchmarks) ;; *) printf 'usage: evidence-fixtures.sh nightly|caches|build-benchmarks [--all]\n' >&2; return 2 ;; esac
    shift
    if [ "${1:-}" = --all ]; then force=true; shift; fi
    [ "$#" -eq 0 ] || { printf 'unexpected fixture argument\n' >&2; return 2; }
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.."
    fixture_scratch=$(mktemp -d)
    trap 'rm -rf -- "$fixture_scratch"' EXIT
    # This cheap regression suite is never selected away, even on a compiler-only PR.
    if [ "$suite" = nightly ]; then fixture_run 'fixture selection' bash tests/ci/evidence-fixtures-test.sh; fi
    if fixture_required "$fixture_scratch" "$force"; then
        printf 'fixtures: %s: %s\n' "$suite" "$fixture_reason"
        if [ "$suite" = caches ]; then fixture_run 'download retry policy' bash tests/ci/cached-download-retry.sh; fi
        fixture_run "$suite" bash "tests/ci/$suite-fixtures.sh"
    else
        printf 'fixtures: %s: skipped: %s\n' "$suite" "$fixture_reason"
    fi
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then fixture_main "$@"; fi
