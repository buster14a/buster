#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The Actions API and Git glue of main's nightly measurements (#1207; docs/ci.md, "Nightly main measurements"). jq
# turns API responses into the facts tools/ci/nightly.c reads; every decision is the C tool's. Hosted runners only.
#
#   nightly.sh evidence DIRECTORY     9700x.yml's evidence job: reuse and hardware admission, to GITHUB_OUTPUT
#   nightly.sh coverage DIRECTORY     9700x.yml's aggregate, for a merge-queue candidate: its full coverage, job by job
#   nightly.sh stale DIRECTORY        9700x.yml's stale job, for a merge-queue candidate: cancel invalidated candidates' runs
#   nightly.sh record DIRECTORY       9700x.yml's record job: DIRECTORY/record.tsv and the outcome
#   nightly.sh coordinate DIRECTORY --phase prepare|window --mode run|dry-run
#                                     nightly-backfill.yml: harvest, plan and dispatch; DIRECTORY/ledger.tsv and report.md
#
# NIGHTLY_GH replaces `gh` and NIGHTLY_NOW the clock for tests/ci/nightly.sh, which runs this against fixtures;
# NIGHTLY_ITERATIONS bounds the coordinator's steps (one with NIGHTLY_NOW), NIGHTLY_SLEEP their pause, and
# NIGHTLY_PREPARATIONS the hosted preparations in flight.
set -euo pipefail
shopt -s inherit_errexit
repository=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}
[[ "$repository" == buster14a/buster ]]
command=${1:?usage: nightly.sh evidence|coverage|stale|record|coordinate DIRECTORY [OPTIONS]}
work=${2:?usage: nightly.sh evidence|coverage|stale|record|coordinate DIRECTORY [OPTIONS]}
shift 2
mkdir -p "$work"
gh=${NIGHTLY_GH:-gh}
tool="$work/nightly"
# The tool beside this script: the evidence and record jobs run in the checkout, tests elsewhere.
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing "$(dirname "${BASH_SOURCE[0]}")/nightly.c" -o "$tool"

now() { printf '%s\n' "${NIGHTLY_NOW:-$(date +%s)}"; }
# A failed `gh api` still prints the response's body on standard output: read an answer only from a call that succeeded.
api() { local answer; if answer=$("$gh" api "$@"); then printf '%s\n' "$answer"; fi; }
sha() { [[ "$1" =~ ^[0-9a-f]{40}$ ]]; }
# Whether a line of standard input is exactly the argument. It reads the whole input (unlike grep -q, which would end
# the writer with SIGPIPE, a failure under pipefail).
has_line() { grep -Fx -- "$1" > /dev/null; }
output() { printf '%s=%s\n' "$1" "$2" >> "${GITHUB_OUTPUT:?GITHUB_OUTPUT is required}"; }
summary() { cat >> "${GITHUB_STEP_SUMMARY:-/dev/null}"; }

# Facts: runs and jobs as nightly.c reads them (tools/ci/nightly.c, nightly_facts_parse).
# Titles and branches are bounded (a commit's subject can be long): a nightly run's name is kept whole, others cut.
runs_facts()
{
    jq -r '.workflow_runs[]? | ["run", .id, .run_attempt, .event, .status, (.conclusion // "-"), .head_sha, (.path // "-"),
        ((.head_branch // "-")[0:60]), (.created_at | fromdateiso8601), (.updated_at | fromdateiso8601), (.head_repository.id // 0),
        ((.display_title // "-") | if startswith("Nightly ") then .[0:200] else .[0:40] end)] | @tsv'
}
jobs_facts()
{
    jq -r --arg run "$1" '.jobs[]? | ["job", $run, .status, (.conclusion // "-"), .name] | @tsv'
}
# The jobs of the run's latest attempt, every page.
fetch_jobs()
{
    "$gh" api --paginate "repos/$repository/actions/runs/$1/jobs?per_page=100&filter=latest" | jobs_facts "$1"
}
# The run's coverage record for the attempt, at most 64 KiB of its one file, or nothing when it has none: an archive is
# data, never extracted to paths it names. A failed API call fails, so that a harvest retries it rather than record
# the run without its record.
fetch_record()
{
    local run=$1 attempt=$2 destination=$3 listing id
    rm -f "$destination"
    listing=$("$gh" api "repos/$repository/actions/runs/$run/artifacts?name=coverage-record-$attempt&per_page=10") || return 1
    id=$(jq -r '[.artifacts[]? | select(.expired == false)] | first | .id // empty' <<< "$listing")
    if [[ "$id" =~ ^[1-9][0-9]*$ ]]; then
        "$gh" api "repos/$repository/actions/artifacts/$id/zip" > "$work/record.zip" || return 1
        unzip -p "$work/record.zip" record.tsv 2> /dev/null | head -c 65536 > "$destination" || true
        if [ ! -s "$destination" ]; then
            rm -f "$destination"
        fi
    fi
}

# main's history into a bare repository, partially: commits (and trees, for blob:none) alone. The remote is named, as
# a partial fetch's promisor must be.
fetch_main()
{
    local history=$1 filter=$2
    if [ ! -d "$history" ]; then
        git init -q --bare "$history"
        git -C "$history" remote add origin "${NIGHTLY_REMOTE:-https://github.com/$repository.git}"
    fi
    git -C "$history" fetch -q --no-tags --filter="$filter" origin '+refs/heads/main:refs/heads/main'
}

# --- 9700x.yml's evidence job ---

evidence()
{
    local event=${GITHUB_EVENT_NAME:?} commit=${GITHUB_SHA:?} run=${GITHUB_RUN_ID:?}
    local scope=full hosted=run hosted_source='' native=run native_source='' measurement=run measurement_source=''
    local hardware=admit reason=admitted retired=false notes='' window_start='' window_deadline='' window_override=false
    case "$event" in
        pull_request)
            # A pull request, draft or ready, never uses the 9700X (#2055, the owner's choice on 2026-11-10): its hosted
            # plain rows qualify it, and native correctness and measurements are its merged first-parent state's, run
            # in the 02:00-08:00 window. Its 9700X job is skipped as soon as its tests finish, so nothing waits on it.
            scope=plain
            native=-
            measurement=-
            hardware=skip
            reason=pull-request
            ;;
        push)
            # A push to main runs its own hosted rows here and reuses nothing from its pull request. In the nightly mode
            # (the owner's choice on 2026-11-05) it keeps off the 9700X: its native correctness and targeted
            # measurements are its first-parent state's, run in the 02:00-08:00 window. It also runs only the plain
            # rows (#2057, the owner's choice on 2026-11-10, until the merge queue carries the full suite): the sanitizer
            # rows and fuzz campaigns run on the nightly snapshot's daily run. Before the nightly mode, which has no
            # snapshot to cover them, a push runs every hosted row and the 9700X job here.
            if [ "${EVIDENCE_NIGHTLY:-}" = on ]; then
                scope=plain
                native=-
                measurement=-
                hardware=skip
                reason=deferred
                notes='the nightly mode runs a push'"'"'s plain rows only; its sanitizer rows and fuzzing run on the nightly snapshot, and its native correctness and measurements in the 02:00-08:00 Europe/Madrid window'
            fi
            ;;
        merge_group)
            # A merge-queue candidate (#1430): the commit main is fast-forwarded to if it passes, and the only
            # per-merge correctness run. It runs the full hosted contract, its own whatever its pull request ran, and
            # never the 9700X: its first-parent state's native correctness and measurements are the window's, as a
            # push's are in the nightly mode. Reads no API.
            scope=full
            native=-
            measurement=-
            hardware=skip
            reason=merge-group
            notes='a merge-queue candidate runs every hosted row, the vector pool and the fuzz campaigns; its native correctness and measurements are its first-parent state'"'"'s, in the 02:00-08:00 Europe/Madrid window'
            ;;
        schedule)
            if [ "${EVIDENCE_NIGHTLY:-}" = on ]; then
                retired=true
                hosted=none
                hardware=skip
                reason=retired
                notes='the nightly mode retired the daily run: its comprehensive measurements are the nightly snapshot'"'"'s'
            fi
            ;;
        workflow_dispatch)
            local purpose=${EVIDENCE_PURPOSE:-full}
            case "$purpose" in
                full) ;;
                backlog|snapshot)
                    # The commit must be a first-parent state of main, at or before the cycle's pinned snapshot.
                    local history="$work/history.git" snapshot=${EVIDENCE_SNAPSHOT:-}
                    fetch_main "$history" tree:0
                    if [ -z "$snapshot" ]; then
                        snapshot=$(git -C "$history" rev-parse main)
                    fi
                    if ! sha "$snapshot" || ! git -C "$history" rev-list --first-parent main | has_line "$snapshot" ||
                        ! git -C "$history" rev-list --first-parent "$snapshot" | has_line "$commit"; then
                        printf '::error title=Nightly dispatch::%s is not a first-parent state of main at or before %s\n' "$commit" "${snapshot:-main}"
                        exit 1
                    fi
                    scope=plain
                    # The cycle's window as instants, which the 9700X job compares with its clock before any setup.
                    local times
                    times=$("$tool" window-times --now "$(now)")
                    window_start=$(awk -F '\t' '$1 == "start" { print $2 }' <<< "$times")
                    window_deadline=$(awk -F '\t' '$1 == "deadline" { print $2 }' <<< "$times")
                    if [ "${EVIDENCE_HARDWARE:-admit}" = defer ]; then
                        # The snapshot's deferred run is the day's diagnostic run: every hosted row and fuzzing.
                        if [ "$purpose" = snapshot ]; then
                            scope=full
                        fi
                        hardware=skip
                        reason=deferred
                    else
                        local facts="$work/hosted.tsv" found
                        : > "$facts"
                        if "$gh" api "repos/$repository/actions/workflows/9700x.yml/runs?head_sha=$commit&per_page=50" > "$work/runs.json"; then
                            runs_facts < "$work/runs.json" >> "$facts"
                            for found in $(jq -r '.workflow_runs[]? | select(.status == "completed" and (.event == "push" or .event == "workflow_dispatch" or .event == "merge_group")) | .id' "$work/runs.json"); do
                                if [ "$found" != "$run" ]; then
                                    fetch_jobs "$found" >> "$facts" || true
                                fi
                            done
                        fi
                        local verdict
                        verdict=$("$tool" hosted --commit "$commit" --exclude "$run" < "$facts" || printf 'hosted\tnone\t0\n')
                        if [ "$(cut -f 2 <<< "$verdict")" = reused ]; then
                            hosted=reused
                            hosted_source=$(cut -f 3 <<< "$verdict")
                        fi
                        local bound=30 window
                        if [ "$purpose" = backlog ]; then
                            bound=25
                        fi
                        # A person's reason holds after the lock too: the 9700X job then admits whatever the time.
                        if [ -n "${EVIDENCE_OVERRIDE:-}" ]; then
                            window_override=true
                        fi
                        if window=$("$tool" window --now "$(now)" --bound "$((bound * 60))" --override "${EVIDENCE_OVERRIDE:-}"); then
                            notes="window: $(cut -f 2 <<< "$window")"
                        else
                            hardware=skip
                            reason=window
                            notes="window: $(cut -f 2 <<< "$window")"
                        fi
                    fi
                    ;;
                *)
                    printf '::error::unknown dispatch purpose %s\n' "$purpose"
                    exit 1
                    ;;
            esac
            ;;
        *)
            printf '::error::unexpected event %s\n' "$event"
            exit 1
            ;;
    esac
    output scope "$scope"
    output hosted "$hosted"
    output hosted_source "$hosted_source"
    output native "$native"
    output native_source "$native_source"
    output measurement "$measurement"
    output measurement_source "$measurement_source"
    output hardware "$hardware"
    output hardware_reason "$reason"
    output retired "$retired"
    output window_start "$window_start"
    output window_deadline "$window_deadline"
    output window_override "$window_override"
    {
        printf '### Reuse evidence and hardware admission\n\n| | |\n| --- | --- |\n'
        printf '| scope | %s |\n| hosted rows | %s %s |\n| native correctness | %s %s |\n| measurements | %s %s |\n| 9700X | %s (%s) |\n' \
            "$scope" "$hosted" "$hosted_source" "$native" "$native_source" "$measurement" "$measurement_source" "$hardware" "$reason"
        if [ -n "$notes" ]; then
            printf '\n%s\n' "$notes"
        fi
    } | summary
    printf 'scope=%s hosted=%s native=%s measurement=%s hardware=%s (%s) %s\n' "$scope" "$hosted" "$native" "$measurement" "$hardware" "$reason" "$notes"
}

# --- 9700x.yml's aggregate, for a merge-queue candidate ---

# This run's jobs (its latest attempt's), and nightly.c's check of them. A job the API still shows in progress, a page
# that failed, or an incomplete set is asked again, NIGHTLY_COVERAGE_TRIES times in all (3), NIGHTLY_SLEEP apart (15 s),
# before the check fails: the aggregate starts as its last need ends, before the API may show it.
coverage()
{
    local run=${GITHUB_RUN_ID:?} tries=${NIGHTLY_COVERAGE_TRIES:-3} try verdict=1
    for (( try = 1; try <= tries; try += 1 )); do
        : > "$work/coverage.tsv"
        if fetch_jobs "$run" > "$work/coverage-facts.tsv" && "$tool" coverage --run "$run" --auxiliary "${AUXILIARY:-}" < "$work/coverage-facts.tsv" > "$work/coverage.tsv"; then
            verdict=0
            break
        fi
        if [ "$try" -lt "$tries" ]; then
            sleep "${NIGHTLY_SLEEP:-15}"
        fi
    done
    {
        printf '### Merge-queue coverage\n\n```text\n'
        cat "$work/coverage.tsv"
        printf '```\n'
    } | summary
    cat "$work/coverage.tsv"
    if [ "$verdict" -ne 0 ]; then
        printf '::error title=Merge-queue coverage::the candidate'"'"'s hosted rows, vector verdict, fuzz campaigns or auxiliary workflows are incomplete (above)\n'
    fi
    return "$verdict"
}

# --- 9700x.yml's stale job: invalidated merge-queue candidates (#1430) ---

# GitHub never cancels a candidate's run: when it drops a candidate or rebuilds it, it deletes the candidate's branch or
# points it at the rebuild. Each candidate, as it starts, lists the workflow's unfinished merge-queue runs and, for each
# other one, whether its branch still names its commit and, if not, whether that commit is on main; nightly.c's `stale`
# decides and this cancels what it names. A failed read is an unknown fact, which cancels nothing.
stale()
{
    local own=${GITHUB_RUN_ID:?} status listing run branch sha present merged answer verdict why cancelled=0 failed=0
    : > "$work/stale-runs.tsv"
    for status in queued in_progress waiting pending requested; do
        listing=$(api "repos/$repository/actions/workflows/9700x.yml/runs?event=merge_group&status=$status&per_page=100")
        jq -r '.workflow_runs[]? | [.id, (.head_branch // "-"), .head_sha] | @tsv' <<< "${listing:-"{}"}" >> "$work/stale-runs.tsv"
    done
    sort -u "$work/stale-runs.tsv" | while IFS=$'\t' read -r run branch sha; do
        present=unknown
        merged=unknown
        if [ "$run" != "$own" ] && [[ "$branch" =~ ^gh-readonly-queue/main/pr-[0-9]+-[0-9a-f]{40}$ ]] && sha "$sha"; then
            if answer=$("$gh" api "repos/$repository/git/ref/heads/$branch" --jq .object.sha 2> "$work/stale-error"); then
                present=moved
                if [ "$answer" = "$sha" ]; then
                    present=present
                elif ! sha "$answer"; then
                    present=unknown
                fi
            elif grep -q 'HTTP 404' "$work/stale-error"; then
                present=gone
            fi
            if [ "$present" = gone ] || [ "$present" = moved ]; then
                case "$(api "repos/$repository/compare/$sha...main" --jq .status)" in
                    identical | ahead) merged=merged ;;
                    behind | diverged) merged=unmerged ;;
                esac
            fi
        fi
        printf 'candidate\t%s\t%s\t%s\n' "$run" "$present" "$merged"
    done > "$work/stale-facts.tsv"
    "$tool" stale --run "$own" < "$work/stale-facts.tsv" > "$work/stale.tsv"
    while IFS=$'\t' read -r verdict run why; do
        if [ "$verdict" = cancel ]; then
            if "$gh" api -X POST "repos/$repository/actions/runs/$run/cancel" > /dev/null; then
                cancelled=$((cancelled + 1))
                printf 'cancelled\t%s\n' "$run" >> "$work/stale.log"
            else
                failed=$((failed + 1))
                printf 'not cancelled\t%s\n' "$run" >> "$work/stale.log"
            fi
        else
            printf 'kept\t%s\t%s\n' "$run" "$why" >> "$work/stale.log"
        fi
    done < "$work/stale.tsv"
    touch "$work/stale.log"
    {
        printf '### Invalidated merge-queue candidates\n\n```text\n'
        cat "$work/stale.log"
        printf '```\n'
    } | summary
    cat "$work/stale.log"
    printf 'Cancelled %s invalidated candidate runs; %s cancels failed\n' "$cancelled" "$failed"
    [ "$failed" -eq 0 ]
}

# --- 9700x.yml's record job ---

record()
{
    export RECORD_COMMIT RECORD_TREE RECORD_PARENT RECORD_PARENT_TREE RECORD_PR_HEAD RECORD_WORKFLOW RECORD_SELECTOR RECORD_INVENTORY RECORD_TOOLCHAIN RECORD_CORPUS RECORD_GATES
    RECORD_COMMIT=$(git rev-parse HEAD)
    RECORD_TREE=$(git rev-parse 'HEAD^{tree}')
    RECORD_PARENT=$(git rev-parse -q --verify 'HEAD^1' || true)
    RECORD_PARENT_TREE=$(git rev-parse -q --verify 'HEAD^1^{tree}' || true)
    # A merge-queue candidate (#1430) is GitHub's merge of its base (main, or the entry ahead of it: the parent above)
    # and its pull request's head, its second parent; the event names no head, so the commit does.
    if [ "${RECORD_EVENT:-}" = merge_group ]; then
        RECORD_PR_HEAD=$(git rev-parse -q --verify 'HEAD^2' || true)
    fi
    RECORD_WORKFLOW=$(git rev-parse HEAD:.github/workflows/9700x.yml)
    RECORD_SELECTOR=$(git rev-parse HEAD:tools/ci/changes.c)
    RECORD_INVENTORY=$(git rev-parse HEAD:tools/ci/correctness.tests)
    RECORD_TOOLCHAIN=$(git rev-parse HEAD:tools/ci/setup-benchpress.sh)
    RECORD_CORPUS=$(git rev-parse HEAD:tools/ci/fetch-corpus.sh)
    local outputs=${CHANGES_OUTPUTS:-}
    if [ -z "$outputs" ]; then
        outputs='{}'
    fi
    RECORD_GATES=$(jq -r '[to_entries[]? | select(.value == "true" and .key != "code") | .key] | join(",")' <<< "$outputs")
    "$tool" record --output "$work/record.tsv" > "$work/outcome.tsv"
    local outcome
    outcome=$(awk -F '\t' '$1 == "outcome" { print $2 }' "$work/outcome.tsv")
    output outcome "$outcome"
    {
        printf '### Coverage record\n\n```text\n'
        cat "$work/record.tsv"
        printf '```\n'
    } | summary
    printf 'Coverage %s, outcome %s\n' "$(awk -F '\t' '$1 == "coverage" { print $2 }' "$work/outcome.tsv")" "$outcome"
}

# --- nightly-backfill.yml's coordinator ---

# The ledger this run's prepare job kept, or the newest the previous coordinator runs on main kept (a window job's
# before a prepare job's), or the one published to buster-performance, or a new one. Artifacts are read only from this
# workflow's own runs; an invalid ledger is replaced by a new one, which harvest refills from the runs' records. Every
# fallback is safe: the rows are the records', which harvest reads again, and the cycle's pin reaches the window job
# as a job output (NIGHTLY_SNAPSHOT), as the prepare job's pending dispatches do (pending_handoff).
ledger_artifact()
{
    local run=$1 attempt
    for attempt in window prepare; do
        api "repos/$repository/actions/runs/$run/artifacts?per_page=50" \
            --jq "[.artifacts[]? | select(.expired == false and (.name | startswith(\"nightly-ledger-$attempt-\")))] | max_by(.id) | .id // empty"
    done | grep -E '^[1-9][0-9]*$' | head -n 1 || true
}

load_ledger()
{
    local ledger=$1 id='' run
    printf '# buster-nightly-ledger 1\n' > "$ledger"
    : > "$work/ledger.previous"
    id=$(ledger_artifact "${GITHUB_RUN_ID:?}")
    if ! [[ "$id" =~ ^[1-9][0-9]*$ ]]; then
        for run in $(api "repos/$repository/actions/workflows/nightly-backfill.yml/runs?branch=main&status=completed&per_page=10" \
            --jq '.workflow_runs[]? | select(.event == "schedule" or .event == "workflow_dispatch") | .id' | grep -E '^[1-9][0-9]*$' || true); do
            id=$(ledger_artifact "$run")
            if [[ "$id" =~ ^[1-9][0-9]*$ ]]; then
                break
            fi
        done
    fi
    if [[ "$id" =~ ^[1-9][0-9]*$ ]] && "$gh" api "repos/$repository/actions/artifacts/$id/zip" > "$work/ledger.zip"; then
        unzip -p "$work/ledger.zip" ledger.tsv 2> /dev/null | head -c 67108864 > "$work/ledger.previous" || true
    elif git -c advice.detachedHead=false clone -q --depth 1 --branch nightly-ledger \
        "${NIGHTLY_PERFORMANCE_REMOTE:-https://github.com/buster14a/buster-performance.git}" "$work/published" 2> /dev/null &&
        [ -f "$work/published/nightly/main-coverage.tsv" ]; then
        head -c 67108864 "$work/published/nightly/main-coverage.tsv" > "$work/ledger.previous"
    fi
    if [ -s "$work/ledger.previous" ]; then
        # The C reader validates it: harvest's list reads the ledger whole and refuses any invalid line.
        if "$tool" harvest --ledger "$work/ledger.previous" --facts /dev/null --states "$work/states.tsv" --list > /dev/null; then
            cp "$work/ledger.previous" "$ledger"
        else
            printf '::warning title=Nightly ledger::the previous ledger is invalid; starting a new one from the runs'"'"' records\n'
        fi
    fi
}

# main's first-parent states from the epoch (the first-parent commit that added tools/ci/nightly.c, or NIGHTLY_EPOCH),
# oldest first, each with its worker's declared window and first-parent change's paths; main_head is main's head.
main_head=''
earlier_states=0
load_states()
{
    local history="$work/history.git" states=$1 epoch
    fetch_main "$history" blob:none
    epoch=${NIGHTLY_EPOCH:-$(git -C "$history" log --first-parent --diff-filter=A --format=%H main -- tools/ci/nightly.c | tail -n 1)}
    # Before tools/ci/nightly.c reaches main (a pull request's dry run of it), NIGHTLY_EPOCH_SINCE bounds the states.
    if [ -z "$epoch" ] && [ -n "${NIGHTLY_EPOCH_SINCE:-}" ]; then
        epoch=$(git -C "$history" log --first-parent --since="$NIGHTLY_EPOCH_SINCE" --format=%H main | tail -n 1)
    fi
    if ! sha "$epoch" || ! git -C "$history" rev-list --first-parent main | has_line "$epoch"; then
        printf '::error title=Nightly coordinator::no epoch: main has no first-parent commit that added tools/ci/nightly.c (%s)\n' "${epoch:-none}"
        return 1
    fi
    : > "$states"
    # Historical workers execute their own policy. Read its literal hour declarations as data, cached once per source
    # blob, never by compiling or running historical code. Rebuilt at each job: no handoff can lose this constraint.
    local workers="$work/worker-windows"
    mkdir -p "$workers"
    local commit parent committed blob started=false
    while read -r commit parent committed; do
        if [ "$commit" = "$epoch" ]; then
            started=true
        fi
        if [ "$started" = true ]; then
            printf 'state\t%s\t%s\t%s\n' "$commit" "$parent" "$committed" >> "$states"
            blob=$(git -C "$history" rev-parse "$commit:tools/ci/nightly.c" 2> /dev/null) || blob=''
            if sha "$blob"; then
                if [ ! -f "$workers/$blob.tsv" ]; then
                    git -C "$history" cat-file blob "$blob" > "$workers/source.c"
                    "$tool" worker-window < "$workers/source.c" > "$workers/$blob.tsv"
                fi
                cat "$workers/$blob.tsv" >> "$states"
            else
                printf 'worker-window\t-\t-\n' >> "$states"
            fi
            if [ "$parent" != - ]; then
                git -C "$history" diff --no-renames --name-only "$parent" "$commit" | sed 's/^/path\t/' >> "$states"
            fi
        fi
    done < <(git -C "$history" log --first-parent --reverse --format='%H %P %ct' main | awk '{ if (NF == 2) print $1, "-", $2; else print $1, $2, $NF }')
    main_head=$(git -C "$history" rev-parse main)
    # Out of scope, reported: the fortnight's first-parent states before the epoch, whose workflow has no nightly worker.
    earlier_states=0
    if git -C "$history" rev-parse -q --verify "$epoch^1" > /dev/null; then
        earlier_states=$(git -C "$history" rev-list --first-parent --count --since="$(( $(now) - 14 * 86400 ))" "$epoch^1")
    fi
}

# The workflow's runs of the last DAYS days: pushes, the daily run and dispatches, every page, as facts. The first
# step of a job reads a fortnight's (harvest's horizon), later ones a day's: what is in flight or just completed.
# Any listing that fails fails the whole: a partial list would hide a run in flight, and the planner would dispatch a
# second one. Each failure returns explicitly, since a caller's condition suspends errexit here.
load_runs()
{
    local facts=$1 days=$2 since event
    since=$(date -u -d "@$(( $(now) - days * 86400 ))" +%Y-%m-%d)
    : > "$facts"
    for event in push schedule workflow_dispatch merge_group; do
        if ! "$gh" api --paginate "repos/$repository/actions/workflows/9700x.yml/runs?event=$event&created=%3E%3D$since&per_page=100" > "$work/runs-$event.json"; then
            return 1
        fi
        if ! runs_facts < "$work/runs-$event.json" >> "$facts"; then
            return 1
        fi
    done
    # A run that changed state between pages appears twice; keep its first line.
    if ! awk -F '\t' '!seen[$2]++' "$facts" > "$facts.unique" || ! mv "$facts.unique" "$facts"; then
        return 1
    fi
}

# The completed runs the ledger lacks, with their jobs and records. A run whose jobs or record the API failed to give
# is left for the next step: its row, once written, is never revised.
harvest()
{
    local ledger=$1 states=$2 facts=$3 records="$work/records" run attempt
    mkdir -p "$records"
    "$tool" harvest --ledger "$ledger" --facts "$facts" --states "$states" --list > "$work/fetch.tsv"
    : > "$work/jobs.tsv"
    : > "$work/unfetched"
    while IFS=$'\t' read -r _ run attempt; do
        if fetch_jobs "$run" > "$work/run-jobs.tsv" && fetch_record "$run" "$attempt" "$records/$run.tsv"; then
            cat "$work/run-jobs.tsv" >> "$work/jobs.tsv"
        else
            printf '%s\n' "$run" >> "$work/unfetched"
            printf '::warning title=Nightly harvest::run %s could not be read; it is harvested later\n' "$run"
        fi
    done < "$work/fetch.tsv"
    awk -F '\t' 'FILENAME == ARGV[1] { skip[$1] = 1; next } !($1 == "run" && skip[$2])' "$work/unfetched" "$facts" > "$work/harvest.tsv"
    cat "$work/jobs.tsv" >> "$work/harvest.tsv"
    "$tool" harvest --ledger "$ledger" --facts "$work/harvest.tsv" --states "$states" --records "$records" >> "$ledger"
}

# The nightly/SHA branches' writes (create, delete). With the nightly-ledger environment's BUSTER_NIGHTLY_BRANCH_TOKEN
# (contents and workflows write on this repository alone, docs/ci.md), GitHub doesn't refuse the merges the workflow
# token may not have; without it, the workflow token, and those states are skipped. Nothing else uses that token:
# dispatches, cancellations and reads stay the workflow's. A branch token GitHub rejects as a credential (expired or
# revoked: HTTP 401; without access to this repository: "not accessible by personal access token", or a 404 when the
# token can't read the repository itself, since a branch another actor deleted is a 404 to a valid token too) is
# dropped for the rest of the job, which then writes with the workflow token as if there were none, and says so once
# (branch_token_warning). Its output is gh's, standard output and error together.
branch_api()
{
    local answer status=0 fallback=true
    if [ -n "${NIGHTLY_BRANCH_TOKEN:-}" ] && [ ! -e "$work/branch-token-rejected" ]; then
        answer=$(GH_TOKEN=$NIGHTLY_BRANCH_TOKEN "$gh" api "$@" 2>&1) || status=$?
        fallback=false
        local rejected=false
        if [ "$status" -ne 0 ] && grep -E -q 'HTTP 401|Bad credentials|not accessible by personal access token' <<< "$answer"; then
            rejected=true
        elif [ "$status" -ne 0 ] && grep -F -q 'HTTP 404' <<< "$answer" && ! GH_TOKEN=$NIGHTLY_BRANCH_TOKEN "$gh" api "repos/$repository" --jq .id > /dev/null 2>&1; then
            rejected=true
        fi
        if [ "$rejected" = true ]; then
            printf '::warning title=Nightly branch token::GitHub rejected BUSTER_NIGHTLY_BRANCH_TOKEN (%s), so this job makes the nightly branches with the workflow token (docs/ci.md, "Refused branches")\n' "$(tail -n 1 <<< "$answer")" > "$work/branch-token-rejected"
            fallback=true
            status=0
        else
            printf '%s\n' "$answer"
        fi
    fi
    if [ "$fallback" = true ]; then
        "$gh" api "$@" 2>&1 || status=$?
    fi
    return "$status"
}

# Says once a job that the branch token was rejected (branch_api runs inside command substitutions, which can't).
branch_token_warning()
{
    if [ -s "$work/branch-token-rejected" ] && [ ! -e "$work/branch-token-warned" ]; then
        cat "$work/branch-token-rejected"
        : > "$work/branch-token-warned"
    fi
}

# nightly/SHA at SHA: 0 when it is there. 2 when it can't be had for the rest of the job, which then stops asking for
# it: GitHub refuses the workflow token the branch (HTTP 403, "Resource not accessible by integration", as it did on
# 2026-11-05 for merges whose pull request's head lacked main's workflow changes), or the branch exists and points
# elsewhere (until a person fixes it, never dispatched). 1 for anything else (a timeout, a 429 or 5xx, a failed
# lookup), which a later step asks for again.
ensure_branch()
{
    local commit=$1 found answer result=0
    found=$(api "repos/$repository/git/ref/heads/nightly/$commit" --jq '.object.sha' 2> /dev/null)
    if [ -z "$found" ]; then
        if ! answer=$(branch_api -X POST "repos/$repository/git/refs" -f "ref=refs/heads/nightly/$commit" -f "sha=$commit" 2>&1); then
            # Made meanwhile (a 422 for a branch another step created): there now, at the commit, is success.
            found=$(api "repos/$repository/git/ref/heads/nightly/$commit" --jq '.object.sha' 2> /dev/null)
            if [ "$found" = "$commit" ]; then
                result=0
            elif grep -F 'Resource not accessible by integration' <<< "$answer" | grep -F -q 'HTTP 403'; then
                result=2
                printf '::warning title=Nightly branch::GitHub refused the workflow token nightly/%s, so nothing is dispatched for it in this job; later states are planned instead (docs/ci.md)\n' "$commit"
            else
                result=1
                printf '::warning title=Nightly branch::nightly/%s could not be created (%s); a later step tries again\n' "$commit" "$(tail -n 1 <<< "$answer")"
            fi
        fi
    elif [ "$found" != "$commit" ]; then
        printf '::error title=Nightly branch::nightly/%s points at %s; refusing to dispatch it in this job, and planning later states instead\n' "$commit" "$found"
        result=2
    fi
    return "$result"
}

publish()
{
    local cycle=$1
    if [ -z "${NIGHTLY_PUBLICATION_TOKEN:-}" ]; then
        printf 'The ledger is not published: no BUSTER_PERFORMANCE_LEDGER_TOKEN in the nightly-ledger environment (docs/ci.md).\n'
        return 0
    fi
    # Best effort: a publication that fails (a token refused, the network, the branch moved on meanwhile) warns and
    # changes nothing else. The coordinator's artifacts carry the ledger from run to run, and the next run publishes it
    # again; a fresh history is only ever pushed to a branch that doesn't exist yet, never forced over one that does.
    local target="$work/publication" header remote=${NIGHTLY_PERFORMANCE_REMOTE:-https://github.com/buster14a/buster-performance.git}
    header=$(printf 'x-access-token:%s' "$NIGHTLY_PUBLICATION_TOKEN" | base64 -w 0)
    printf '::add-mask::%s\n' "$header"
    local auth=(-c "http.https://github.com/.extraheader=AUTHORIZATION: basic $header")
    local published=true
    : > "$work/publication.log"
    rm -rf "$target"
    if ! GIT_TERMINAL_PROMPT=0 git "${auth[@]}" clone -q --depth 1 --branch nightly-ledger "$remote" "$target" 2>> "$work/publication.log"; then
        rm -rf "$target"
        git init -q -b nightly-ledger "$target"
    fi
    mkdir -p "$target/nightly/reports"
    cp "$work/ledger.tsv" "$target/nightly/main-coverage.tsv"
    cp "$work/report.md" "$target/nightly/reports/$cycle.md"
    git -C "$target" add nightly
    if ! git -C "$target" diff --cached --quiet; then
        if ! git -C "$target" -c user.name='github-actions[bot]' -c user.email='41898282+github-actions[bot]@users.noreply.github.com' \
            commit -q -m "Nightly ledger: cycle $cycle (buster run ${GITHUB_RUN_ID:-local})" 2>> "$work/publication.log" ||
            ! GIT_TERMINAL_PROMPT=0 git -C "$target" "${auth[@]}" push -q "$remote" HEAD:nightly-ledger 2>> "$work/publication.log"; then
            published=false
        fi
    fi
    if [ "$published" = true ]; then
        printf 'The ledger is published: buster-performance, branch nightly-ledger, nightly/reports/%s.md.\n' "$cycle"
    else
        # git's own reasons (`remote:` and `fatal:` lines) say why better than its last line.
        local reason
        reason=$(awk '/^(remote|fatal): / && n < 3 { r = r (n ? " " : "") $0; n += 1 } { last = $0 } END { print n ? r : last }' "$work/publication.log")
        printf '::warning title=Nightly ledger::the ledger could not be published to buster-performance (%s); this run'"'"'s artifact keeps it, and the next run publishes it again\n' "$reason"
    fi
}

# The prepare job's dispatches that the runs may not show yet, which the window job must not repeat. They are no
# record's and no ledger's, so only the prepare job can hand them over: as a job output, which the window job (that
# needs the prepare job's success) always gets, never an artifact the API may fail to list or serve. The form is
# "buster-nightly-pending-1 COUNT", then each dispatch as REQUEST,PURPOSE,HARDWARE,SHA,TIME. No dispatches is a valid
# list; an absent or malformed one, or one whose count doesn't match, is no list at all.
pending_handoff()
{
    awk -F '\t' '$1 == "pending" { count += 1; entries = entries sprintf(" %s,%s,%s,%s,%s", $2, $3, $4, $5, $6) }
        END { printf "buster-nightly-pending-1 %d%s\n", count, entries }' "$1"
}
pending_received()
{
    local words entry valid=true
    read -r -a words <<< "$1"
    if [ "${words[0]:-}" != buster-nightly-pending-1 ] || ! [[ "${words[1]:-}" =~ ^(0|[1-9][0-9]{0,3})$ ]] ||
        [ "${#words[@]}" -ne $((${words[1]:-0} + 2)) ]; then
        valid=false
    fi
    : > "$2"
    if [ "$valid" = true ]; then
        for entry in "${words[@]:2}"; do
            if [[ "$entry" =~ ^[^,]+,[^,]+,[^,]+,[^,]+,[^,]+$ ]]; then
                printf 'pending\t%s\n' "${entry//,/$'\t'}" >> "$2"
            else
                valid=false
            fi
        done
    fi
    [ "$valid" = true ]
}

coordinate()
{
    local phase='' mode=''
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --phase) phase=$2; shift 2 ;;
            --mode) mode=$2; shift 2 ;;
            *) printf 'nightly.sh: unknown option %s\n' "$1" >&2; exit 2 ;;
        esac
    done
    [[ "$phase" == prepare || "$phase" == window ]]
    [[ "$mode" == run || "$mode" == dry-run ]]
    local coordinator=${GITHUB_RUN_ID:?} states="$work/states.tsv" facts="$work/facts.tsv" ledger="$work/ledger.tsv"
    local refs="$work/refs.tsv" pending="$work/pending.tsv" refused="$work/refused.tsv" main sequence=1 times cycle start deadline started
    # The job's start; NIGHTLY_STARTED stands in for it in tests, as NIGHTLY_NOW does for the clock.
    started=${NIGHTLY_STARTED:-$(now)}
    # A window job hands over to the continuation job once it has run this long (345 minutes, inside GitHub's 360 and
    # the job's 355-minute timeout) with the window still open: the night the clocks go back, the window is 7 hours.
    local job_limit=${NIGHTLY_JOB_LIMIT:-20700} handoff=false
    : > "$pending"
    # The states whose nightly/SHA branch this job couldn't create: the plan skips them for the rest of the job.
    : > "$refused"
    if [ "$phase" = window ]; then
        # The window job's requests from 10001, its continuation's from 20001 (NIGHTLY_SEQUENCE): never the same token.
        sequence=${NIGHTLY_SEQUENCE:-10001}
        # Without the prepare job's pending dispatches, a dispatch the runs don't show yet could be dispatched again:
        # nothing is planned. The next coordinator run plans when the runs show them.
        if ! pending_received "${NIGHTLY_PENDING:-}" "$pending"; then
            printf '::error title=Nightly coordinator::the prepare job'"'"'s pending dispatches did not reach this job, so nothing is planned (a dispatch the runs do not show yet could be repeated): %s\n' "${NIGHTLY_PENDING:-none}"
            exit 1
        fi
    fi
    load_states "$states"
    main=$main_head
    load_ledger "$ledger"
    times=$("$tool" window-times --now "$(now)")
    cycle=$(awk -F '\t' '$1 == "cycle" { print $2 }' <<< "$times")
    start=$(awk -F '\t' '$1 == "start" { print $2 }' <<< "$times")
    deadline=$(awk -F '\t' '$1 == "deadline" { print $2 }' <<< "$times")
    # The prepare job's pin, when its ledger didn't reach this job: the cycle keeps the snapshot it started with.
    if [ "$phase" = window ] && sha "${NIGHTLY_SNAPSHOT:-}" && ! cut -f 1,2 "$ledger" | has_line "cycle"$'\t'"$cycle"; then
        printf 'cycle\t%s\t%s\t%s\t%s\n' "$cycle" "$NIGHTLY_SNAPSHOT" "$coordinator" "$(now)" >> "$ledger"
    fi
    local action verdict line iterations=0 limit=${NIGHTLY_ITERATIONS:-} pause=${NIGHTLY_SLEEP:-90}
    if [ -z "$limit" ] && [ -n "${NIGHTLY_NOW:-}" ]; then
        limit=1
    fi
    # A prepare job that will wait for the window keeps preparing while it waits, as hosted runs complete and free their
    # slots (the planner admits no hardware before 02:00): every five minutes until the window opens.
    local waits=false
    if [ "$phase" = prepare ] && [ "$mode" = run ] && [ -z "${NIGHTLY_OVERRIDE:-}" ] && [ "$(now)" -lt "$start" ] && [ "$((start - started))" -le 21300 ]; then
        waits=true
    fi
    while :; do
        iterations=$((iterations + 1))
        # A step's API failure ends a first step, which can't plan without the runs; later, it waits and tries again,
        # planning nothing on a partial list.
        local days=1
        if [ "$iterations" -eq 1 ]; then
            days=14
        fi
        if ! load_runs "$facts" "$days"; then
            if [ "$iterations" -eq 1 ]; then
                printf '::error title=Nightly coordinator::the runs could not be listed\n'
                exit 1
            fi
            printf '::warning title=Nightly coordinator::the runs could not be listed; nothing is planned until they are\n'
            if [ -n "$limit" ] && [ "$iterations" -ge "$limit" ]; then
                break
            fi
            sleep "$pause"
            continue
        fi
        harvest "$ledger" "$states" "$facts"
        api --paginate "repos/$repository/git/matching-refs/heads/nightly/" --jq '.[].ref' 2> /dev/null |
            sed -n 's|^refs/heads/nightly/\([0-9a-f]\{40\}\)$|\1|p' > "$refs" || : > "$refs"
        "$tool" plan --states "$states" --ledger "$ledger" --facts "$facts" --now "$(now)" --main "$main" --mode "$mode" \
            --coordinator "$coordinator" --sequence "$sequence" --pending "$pending" --refs "$refs" --report "$work/report.md" \
            --override "${NIGHTLY_OVERRIDE:-}" --earlier "$earlier_states" --preparations "${NIGHTLY_PREPARATIONS:-3}" --refused "$refused" \
            $( [ "$phase" = prepare ] && printf -- '--prepare-only' ) > "$work/plan.tsv"
        cat "$work/plan.tsv"
        verdict=''
        # A new refusal: nothing was launched for that state, so the next step plans at once, without the wait.
        local replan=false
        while IFS= read -r line; do
            IFS=$'\t' read -r -a action <<< "$line"
            case "${action[0]}" in
                pin)
                    printf '%s\n' "$(cut -f 2- <<< "$line")" >> "$ledger"
                    ;;
                dispatch)
                    local purpose=${action[1]} hardware=${action[2]} commit=${action[3]} request=${action[4]} pinned=${action[5]:-}
                    sha "$commit"
                    # The cycle's pinned snapshot, the planner's: main's head may have moved on since the pin.
                    sha "$pinned"
                    local branch=0
                    ensure_branch "$commit" || branch=$?
                    if [ "$branch" -eq 0 ]; then
                        local override_arguments=()
                        if [ -n "${NIGHTLY_OVERRIDE:-}" ]; then
                            override_arguments=(-f "override=$NIGHTLY_OVERRIDE")
                        fi
                        "$gh" workflow run 9700x.yml --repo "$repository" --ref "nightly/$commit" -f "purpose=$purpose" \
                            -f "hardware=$hardware" -f "snapshot=$pinned" -f "request=$request" "${override_arguments[@]}"
                        printf 'pending\t%s\t%s\t%s\t%s\t%s\n' "$request" "$purpose" "$hardware" "$commit" "$(now)" >> "$pending"
                    elif [ "$branch" -eq 2 ]; then
                        printf '%s\n' "$commit" >> "$refused"
                        replan=true
                    fi
                    sequence=$((sequence + 1))
                    ;;
                cancel)
                    "$gh" api -X POST "repos/$repository/actions/runs/${action[1]}/cancel" > /dev/null || true
                    ;;
                prune)
                    branch_api -X DELETE "repos/$repository/git/refs/heads/nightly/${action[1]}" > /dev/null || true
                    ;;
                alert)
                    printf '::warning title=Nightly backlog::%s\n' "${action[1]}"
                    ;;
                would-dispatch)
                    sequence=$((sequence + 1))
                    ;;
                wait|done)
                    verdict=${action[0]}
                    ;;
            esac
        done < "$work/plan.tsv"
        branch_token_warning
        # Still waiting inside the window, but out of job time: the continuation job takes over from here, with this
        # job's pending dispatches as its output.
        if [ "$phase" = window ] && [ "$verdict" = wait ] && [ "$(( $(now) - started ))" -ge "$job_limit" ] &&
            { [ -n "${NIGHTLY_OVERRIDE:-}" ] || [ "$(now)" -lt "$deadline" ]; }; then
            handoff=true
            printf 'The window is still open after %s minutes of this job: the continuation job takes over.\n' "$(( ($(now) - started) / 60 ))"
            break
        fi
        if [ -n "$limit" ] && [ "$iterations" -ge "$limit" ]; then
            break
        fi
        if [ "$replan" = true ]; then
            continue
        fi
        if [ "$phase" = prepare ]; then
            # Preparing until the window opens, five minutes at a time; otherwise one step.
            if [ "$waits" = false ] || [ "$(now)" -ge "$start" ]; then
                break
            fi
            local remaining=$((start - $(now)))
            sleep "${NIGHTLY_SLEEP:-$(( remaining < 300 ? remaining : 300 ))}"
            continue
        fi
        # An override (a manual run's reason) admits past 08:00: the loop then ends when nothing more is admissible.
        if [ "$verdict" != wait ] || { [ -z "${NIGHTLY_OVERRIDE:-}" ] && [ "$(now)" -ge "$deadline" ]; }; then
            break
        fi
        sleep "$pause"
    done
    summary < "$work/report.md"
    # The prepare job hands over to the window job, and a window job out of time to its continuation, the same way.
    if [ "$phase" = prepare ] || [ "$handoff" = true ]; then
        output snapshot "$(awk -F '\t' -v cycle="$cycle" '$1 == "cycle" && $2 == cycle { print $3 }' "$ledger" | tail -n 1)"
        output pending "$(pending_handoff "$pending")"
    fi
    if [ "$phase" = window ]; then
        output continue "$([ "$handoff" = true ] && echo reachable || echo closed)"
    fi
    # After the handoff's outputs, and never failing the job: the window job's night doesn't depend on buster-performance.
    publish "$cycle"
    if [ "$phase" = prepare ]; then
        # The window job runs when the window is open, or opens before this job's 6 hours end (5 hours 55 minutes from
        # its start, to leave the job a margin).
        local current
        current=$(now)
        if [ "$mode" = run ] && [ -n "${NIGHTLY_OVERRIDE:-}" ]; then
            printf 'Override: %s; the window job admits hardware now.\n' "$NIGHTLY_OVERRIDE"
            output window reachable
        elif [ "$waits" = true ]; then
            # The loop prepared until the window opened (or its steps' bound, in tests).
            if [ "$current" -lt "$start" ] && [ -z "${NIGHTLY_NOW:-}" ]; then
                sleep "$((start - current))"
            fi
            output window reachable
        elif [ "$mode" = run ] && [ "$current" -ge "$start" ] && [ "$current" -lt "$deadline" ]; then
            output window reachable
        else
            output window closed
        fi
    fi
}

case "$command" in
    evidence) evidence ;;
    coverage) coverage ;;
    stale) stale ;;
    record) record ;;
    coordinate) coordinate "$@" ;;
    *) printf 'nightly.sh: unknown command %s\n' "$command" >&2; exit 2 ;;
esac
