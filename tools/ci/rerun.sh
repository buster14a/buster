#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The API glue of the hosted correctness runs' setup re-run (#2276; docs/commands/ci.md, "Setup failures re-run once"),
# which .github/workflows/rerun.yml runs when a run fails. The decision is tools/ci/rerun.c's: this script fetches the
# run's first attempt's jobs, a page at a time, and the annotations (which tell fail-fast from a timeout) of the
# cancelled jobs the classifier names; classifies them; requests rerun-failed-jobs once, when the run is still at its first attempt and every
# stopped job stopped in setup; and writes the run's summary and, after a re-run, a note on the pull request or the
# commit with the steps the jobs stopped in. No name read from the API reaches the shell but as data.
#
#   bash tools/ci/rerun.sh RUN DIRECTORY
#
# Environment: GITHUB_REPOSITORY; RERUN_TOOL, the built classifier; RERUN_EVENT (push or pull_request), RERUN_SHA,
# RERUN_URL and RERUN_NUMBER, the run's; RERUN_PULL_REQUEST, its pull request's number, if any; GITHUB_STEP_SUMMARY and
# the run's own link (GITHUB_SERVER_URL, GITHUB_RUN_ID), where they are set. GH is the `gh` to call and RERUN_PER_PAGE
# the jobs a page (100), for tests/ci/rerun.sh.
set -euo pipefail
if [ "$#" -ne 2 ]; then
    echo 'usage: bash tools/ci/rerun.sh RUN DIRECTORY' >&2
    exit 2
fi
run=$1
directory=$2
gh=${GH:-gh}
repository=$GITHUB_REPOSITORY
per_page=${RERUN_PER_PAGE:-100}
tool=$RERUN_TOOL
[[ "$run" =~ ^[0-9]+$ ]] || { echo "rerun: not a run ID: $run" >&2; exit 2; }
mkdir -p "$directory"
summary="$directory/summary.md"
link="[run $RERUN_NUMBER]($RERUN_URL)"
self="${GITHUB_SERVER_URL:-https://github.com}/$repository/actions/runs/${GITHUB_RUN_ID:-0}"

publish_summary()
{
    cat "$summary"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        cat "$summary" >> "$GITHUB_STEP_SUMMARY"
    fi
}

# The first attempt's jobs, page by page to total_count. Every run shares the Actions token's hourly API quota (the
# vector draws' calls exhausted it on 2026-11-11), so this asks for little more: the annotations the classifier says
# could change its decision, and, before a re-run, the run's state once.
page=1
pages=1
while [ "$page" -le "$pages" ]; do
    printf -v jobs '%s/jobs-%02d.json' "$directory" "$page"
    "$gh" api "repos/$repository/actions/runs/$run/attempts/1/jobs?per_page=$per_page&page=$page" > "$jobs"
    total=$(jq -e '.total_count' "$jobs")
    pages=$(( (total + per_page - 1) / per_page ))
    if [ "$pages" -gt 99 ]; then
        echo "rerun: $total jobs, more than the classifier reads" >&2
        exit 1
    fi
    page=$((page + 1))
done
: > "$directory/annotations.json"
"$tool" annotations "$directory"/jobs-*.json > "$directory/annotate.txt"
while read -r id; do
    [[ "$id" =~ ^[0-9]+$ ]] || { echo "rerun: not a job ID: $id" >&2; exit 1; }
    annotation_page=1
    count=100
    while [ "$count" -eq 100 ]; do
        "$gh" api "repos/$repository/check-runs/$id/annotations?per_page=100&page=$annotation_page" > "$directory/annotation-page.json"
        count=$(jq -e 'length' "$directory/annotation-page.json")
        jq -c --argjson id "$id" '.[] | {job_id: $id, message: .message}' "$directory/annotation-page.json" >> "$directory/annotations.json"
        annotation_page=$((annotation_page + 1))
    done
done < "$directory/annotate.txt"

"$tool" classify --markdown "$directory/jobs.md" "$directory"/jobs-*.json "$directory/annotations.json" > "$directory/report.tsv"
decision=$(awk -F '\t' '$1 == "decision" { print $2 }' "$directory/report.tsv")
read -r stopped setup keep < <(awk -F '\t' '$1 == "jobs" { print $4, $6, $8 }' "$directory/report.tsv")

if [ "$decision" != rerun ]; then
    {
        printf '### Kept %s as it is\n\n' "$link"
        printf 'Of its %s stopped jobs, %s stopped in setup and %s elsewhere: a run is re-run automatically only when every job that stopped did so in setup, before any test ran (#2276).\n\n' "$stopped" "$setup" "$keep"
        cat "$directory/jobs.md"
    } > "$summary"
    publish_summary
    exit 0
fi

# Still the completed, failed first attempt, just before the request: no one re-ran it meanwhile.
current=$("$gh" api "repos/$repository/actions/runs/$run" | jq -r '[.run_attempt, .status, .conclusion // "-"] | @tsv')
IFS=$'\t' read -r attempt status conclusion <<< "$current"
if [ "$attempt" != 1 ] || [ "$status" != completed ] || [ "$conclusion" != failure ]; then
    printf '### No automatic re-run of %s\n\nIts jobs stopped in setup, but it is at attempt %s now (%s, %s): only a failed first attempt is re-run, once.\n' "$link" "$attempt" "$status" "$conclusion" > "$summary"
    publish_summary
    exit 0
fi
"$gh" api -X POST "repos/$repository/actions/runs/$run/rerun-failed-jobs" > /dev/null
{
    printf '### Re-ran the failed jobs of %s, once\n\n' "$link"
    printf 'Every job that stopped did so in setup, before any test ran (#2276; docs/commands/ci.md, "Setup failures re-run once"). The %s stopped jobs, by the step they stopped in:\n\n' "$stopped"
    cat "$directory/jobs.md"
} > "$summary"
publish_summary

# The note, where the people who follow the change read it.
{
    printf 'Hosted CI re-ran the failed jobs of %s once, automatically: every job that stopped did so in setup, before any test ran (#2276). The %s stopped jobs, by the step they stopped in:\n\n' "$link" "$stopped"
    cat "$directory/jobs.md"
    printf '\nFrom [Re-run hosted setup failures](%s). A failure in this second attempt stays red.\n' "$self"
} > "$directory/note.md"
if [ "$RERUN_EVENT" = pull_request ] && [[ "${RERUN_PULL_REQUEST:-}" =~ ^[0-9]+$ ]]; then
    "$gh" api -X POST "repos/$repository/issues/$RERUN_PULL_REQUEST/comments" -F "body=@$directory/note.md" > /dev/null
elif [[ "$RERUN_SHA" =~ ^[0-9a-f]{40}$ ]]; then
    "$gh" api -X POST "repos/$repository/commits/$RERUN_SHA/comments" -F "body=@$directory/note.md" > /dev/null
fi
