#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# API/JSON glue only. The C tool validates observations and computes quantiles.
# Run on a disposable hosted runner; never on the benchmark machine.
set -euo pipefail
output=${1:?usage: timings.sh OUTPUT_DIRECTORY}
repository=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}
[[ "$repository" == buster14a/buster ]]
mkdir -p "$output"
# Every run starts its observations and lock lines afresh, in a directory used before too.
: > "$output/observations.tsv"
: > "$output/locks.tsv"
gh api "repos/$repository/actions/workflows/9700x.yml/runs?per_page=100" > "$output/runs.json"
jq -e '.workflow_runs | type == "array"' "$output/runs.json" > /dev/null
# A nightly backlog dispatch's run name says it is one (9700x.yml's run-name): its coverage is targeted.
jq -r '.workflow_runs[] | select(.event == "pull_request" or .event == "push" or .event == "schedule" or .event == "workflow_dispatch")
    | [.id, .event, (if ((.display_title // "") | startswith("Nightly backlog ")) then "backlog" else "other" end)] | @tsv' "$output/runs.json" > "$output/run-list.tsv"
while IFS=$'\t' read -r run event kind; do
    [[ "$run" =~ ^[1-9][0-9]*$ ]]
    case "$event:$kind" in
        pull_request:*) group=pr ;;
        push:*) group=main ;;
        workflow_dispatch:backlog) group=targeted ;;
        schedule:*|workflow_dispatch:*) group=full ;;
        *) exit 1 ;;
    esac
    gh api --paginate "repos/$repository/actions/runs/$run/jobs?per_page=100&filter=latest" > "$output/jobs-$run.json"
    jq -r --arg group "$group" '
        def epoch: if type == "string" then try fromdateiso8601 catch null else null end;
        def elapsed($start; $end): ($start | epoch) as $a | ($end | epoch) as $b | if $a != null and $b != null and $b >= $a then $b - $a else null end;
        def row($job; $metric; $seconds): if $seconds != null then [$job.id, $group, $metric, $seconds] | @tsv else empty end;
        .jobs[] | select(.name == "Native tests and benchmarks on Benchpress (Ryzen 7 9700X)" and .status == "completed" and .conclusion == "success") as $job |
        row($job; "queue"; elapsed($job.created_at; $job.started_at)),
        row($job; "execution"; elapsed($job.started_at; $job.completed_at)),
        ($job.steps[]? | select(.name == "Set up runner" and .conclusion == "success") | row($job; "runner-hook"; elapsed(.started_at; .completed_at))),
        (([$job.steps[]? | select(.name == "Verify benchmark host")] | .[0]) as $verify |
        ([$job.steps[]? | select(.name == "Run complete native vector correctness before benchmarks" and .conclusion == "success")] | .[0]) as $native |
        row($job; "setup-build"; elapsed($verify.started_at; $native.started_at)),
        row($job; "native-tests"; elapsed($native.started_at; $native.completed_at)),
        row($job; "host-occupation"; elapsed($verify.started_at; $job.completed_at))),
        (([$job.steps[]? | select(.conclusion == "success" and (.name | startswith("Benchmark ") or startswith("Measure ") or startswith("Attribute "))) | elapsed(.started_at; .completed_at)]) as $measured |
        if ($measured | length) > 0 and ($measured | all(. != null)) then row($job; "benchmark-steps"; ($measured | add)) else empty end)
    ' "$output/jobs-$run.json" >> "$output/observations.tsv"
    # The shared lock, from the runner hooks' own lines in the job's log (data, never run): the job-started hook's
    # start, its `this job has the machine` when it holds the lock, and the job-completed hook's `gave the machine
    # back`. A log that can't be read, or lacks a line, gives that metric no sample.
    jq -r '.jobs[] | select(.name == "Native tests and benchmarks on Benchpress (Ryzen 7 9700X)" and .status == "completed" and .conclusion == "success") | .id' \
        "$output/jobs-$run.json" > "$output/job-list.tsv"
    while IFS= read -r job; do
        [[ "$job" =~ ^[1-9][0-9]*$ ]]
        if gh api "repos/$repository/actions/jobs/$job/logs" > "$output/job.log" 2> /dev/null; then
            awk -v job="$job" -v group="$group" '
                { stamp = $1; sub(/^[^0-9]*/, "", stamp) }
                start == "" && /##\[group\]Run .*\/job-started\.sh/ { start = stamp }
                acquired == "" && /buster-9700x: this job has the machine/ { acquired = stamp }
                /buster-9700x: gave the machine back/ { released = stamp }
                END { printf "%s\t%s\t%s\t%s\t%s\n", job, group, start == "" ? "-" : start, acquired == "" ? "-" : acquired, released == "" ? "-" : released }
            ' "$output/job.log" >> "$output/locks.tsv"
        fi
        rm -f "$output/job.log"
    done < "$output/job-list.tsv"
    rm -f "$output/job-list.tsv"
done < "$output/run-list.tsv"
# Each lock line's instant, then the wait and the hold in whole seconds; a malformed or reversed pair gives no sample.
instant()
{
    if [[ "$1" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,9})?Z$ ]]; then
        date -u -d "$1" +%s.%N
    fi
}
if [ -s "$output/locks.tsv" ]; then
    while IFS=$'\t' read -r job group start acquired released; do
        a=$(instant "$start") b=$(instant "$acquired") c=$(instant "$released")
        awk -v job="$job" -v group="$group" -v a="$a" -v b="$b" -v c="$c" 'BEGIN {
            if (a != "" && b != "" && b + 0 >= a + 0) printf "%s\t%s\tlock-wait\t%.0f\n", job, group, b - a
            if (b != "" && c != "" && c + 0 >= b + 0) printf "%s\t%s\tlock-hold\t%.0f\n", job, group, c - b
        }' >> "$output/observations.tsv"
    done < "$output/locks.tsv"
fi
# The merge queue's candidates (#1430): each completed merge_group run of main's queue (not another branch's, such as
# the drill's, whose candidates never land on main), what became of its candidate (merged when its
# commit is on main; otherwise its own conclusion: passed but invalidated, failed or cancelled; a cancelled run in which
# a job besides the aggregates failed is a failure, which its own stop job cancelled, #2703), its wall time from its
# start to its last update, and the sum of its jobs' own times. Then its hosted jobs' waits for a runner (#2031), each
# from its creation to its start, of the jobs a runner took (a skipped job, or one cancelled before it started, has
# none): the longest, their sum and their count. A candidate whose fate can't be read gives no line.
: > "$output/candidates.tsv"
gh api "repos/$repository/actions/workflows/9700x.yml/runs?event=merge_group&status=completed&per_page=100" > "$output/queue-runs.json"
jq -e '.workflow_runs | type == "array"' "$output/queue-runs.json" > /dev/null
jq -r '
    def epoch: if type == "string" then try fromdateiso8601 catch null else null end;
    .workflow_runs[] | select(.event == "merge_group" and .status == "completed" and ((.head_branch // "") | startswith("gh-readonly-queue/main/")))
    | ((.run_started_at // .created_at) | epoch) as $start | (.updated_at | epoch) as $end
    | select($start != null and $end != null and $end >= $start)
    | [.id, .head_sha, (.conclusion // "-"), ($end - $start)] | @tsv' "$output/queue-runs.json" > "$output/queue-list.tsv"
while IFS=$'\t' read -r run sha conclusion wall; do
    [[ "$run" =~ ^[1-9][0-9]*$ && "$sha" =~ ^[0-9a-f]{40}$ ]]
    fate=$(gh api --jq .status "repos/$repository/compare/$sha...main" 2> /dev/null) || fate=unknown
    case "$fate" in
        identical | ahead | behind | diverged) ;;
        *) continue ;;
    esac
    gh api --paginate "repos/$repository/actions/runs/$run/jobs?per_page=100&filter=latest" > "$output/queue-jobs-$run.json"
    # A job that failed of its own, besides the aggregates, which fail in every cancelled run.
    own_failure=$(jq -s '[.[].jobs[]? | select((.conclusion == "failure" or .conclusion == "timed_out")
        and .name != "Integration qualification" and .name != "Coverage record" and .name != "Build and execute on Ryzen 7 9700X")] | length' \
        "$output/queue-jobs-$run.json")
    case "$fate:$conclusion" in
        identical:* | ahead:*) outcome=merged ;;
        behind:success | diverged:success) outcome=passed ;;
        behind:cancelled | diverged:cancelled) outcome=cancelled; [ "$own_failure" -eq 0 ] || outcome=failed ;;
        *) outcome=failed ;;
    esac
    runner=$(jq -s '
        def epoch: if type == "string" then try fromdateiso8601 catch null else null end;
        [.[].jobs[]? | (.started_at | epoch) as $a | (.completed_at | epoch) as $b | select($a != null and $b != null and $b >= $a) | $b - $a] | add // 0
    ' "$output/queue-jobs-$run.json")
    waits=$(jq -rs '
        def epoch: if type == "string" then try fromdateiso8601 catch null else null end;
        [.[].jobs[]? | select((.runner_id // 0) > 0 and ((.labels // []) | index("self-hosted") | not))
            | (.created_at | epoch) as $a | (.started_at | epoch) as $b | select($a != null and $b != null and $b >= $a) | $b - $a]
        | [(max // 0), (add // 0), length] | @tsv
    ' "$output/queue-jobs-$run.json")
    printf '%s\t%s\t%s\t%s\t%s\n' "$run" "$outcome" "$wall" "$runner" "$waits" >> "$output/candidates.tsv"
done < "$output/queue-list.tsv"
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/timings.c -o "$output/timings"
"$output/timings" < "$output/observations.tsv" > "$output/summary.md"
printf '\nSample: up to the latest 100 runs of `9700x.yml`; job pagination is complete and only the latest attempt is used. Raw API timestamps are retained with this report.\n' >> "$output/summary.md"
"$output/timings" --queue < "$output/candidates.tsv" >> "$output/summary.md"
printf '\nSample: up to the latest 100 completed merge-queue runs; a candidate whose fate the compare API did not give is left out.\n' >> "$output/summary.md"
cat "$output/summary.md"
