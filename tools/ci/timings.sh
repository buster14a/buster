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
: > "$output/observations.tsv"
gh api "repos/$repository/actions/workflows/9700x.yml/runs?per_page=100" > "$output/runs.json"
jq -e '.workflow_runs | type == "array"' "$output/runs.json" > /dev/null
jq -r '.workflow_runs[] | select(.event == "pull_request" or .event == "push" or .event == "schedule" or .event == "workflow_dispatch") | [.id, .event] | @tsv' "$output/runs.json" > "$output/run-list.tsv"
while IFS=$'\t' read -r run event; do
    [[ "$run" =~ ^[1-9][0-9]*$ ]]
    case "$event" in
        pull_request) group=pr ;;
        push) group=main ;;
        schedule|workflow_dispatch) group=periodic ;;
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
        row($job; "native-tests"; elapsed($native.started_at; $native.completed_at))),
        (([$job.steps[]? | select(.conclusion == "success" and (.name | startswith("Benchmark ") or startswith("Measure ") or startswith("Attribute "))) | elapsed(.started_at; .completed_at)]) as $measured |
        if ($measured | length) > 0 and ($measured | all(. != null)) then row($job; "benchmark-steps"; ($measured | add)) else empty end)
    ' "$output/jobs-$run.json" >> "$output/observations.tsv"
done < "$output/run-list.tsv"
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/timings.c -o "$output/timings"
"$output/timings" < "$output/observations.tsv" > "$output/summary.md"
printf '\nSample: up to the latest 100 runs of `9700x.yml`; job pagination is complete and only the latest attempt is used. Raw API timestamps are retained with this report.\n' >> "$output/summary.md"
cat "$output/summary.md"
