#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Records a fixture of tests/ci/rerun.sh from GitHub's API (#2276): run RUN's attempt ATTEMPT, as tools/ci/rerun.sh
# fetches it. Its jobs' pages, PER_PAGE jobs a page (100 by default), one document a page and a job a line, each job
# cut to the fields tools/ci/rerun.c reads: a job that passed or was skipped to its id, name, status and conclusion,
# any other to those, its link, its labels and its steps' names and conclusions. Then its cancelled jobs' annotations,
# a document each, as tools/ci/rerun.sh writes them. cases.tsv lists each
# fixture's run and what it shows. Writes NAME.json here; regenerate NAME.expected and NAME.md with
# `bash tests/ci/rerun.sh --update`, and read their difference before keeping it.
#
#   bash tests/ci/rerun/record.sh RUN ATTEMPT NAME [PER_PAGE]
set -euo pipefail
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo 'usage: bash tests/ci/rerun/record.sh RUN ATTEMPT NAME [PER_PAGE]' >&2
    exit 2
fi
run=$1
attempt=$2
name=$3
per_page=${4:-100}
repository=${GITHUB_REPOSITORY:-buster14a/buster}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
output="$here/$name.json"
# Page by page, as tools/ci/rerun.sh does, to total_count.
page=1
pages=1
: > "$output.tmp"
while [ "$page" -le "$pages" ]; do
    gh api "repos/$repository/actions/runs/$run/attempts/$attempt/jobs?per_page=$per_page&page=$page" |
        jq -r '"{\"total_count\": \(.total_count), \"jobs\": [",
            ([.jobs[] | if .conclusion == "success" or .conclusion == "skipped" then {id, name, status, conclusion}
                else {id, name, status, conclusion, html_url, labels, steps: [.steps[]? | {name, conclusion}]} end
                | tojson] | join(",\n")),
            "]}"' >> "$output.tmp"
    total=$(jq -s '.[0].total_count' "$output.tmp")
    pages=$(( (total + per_page - 1) / per_page ))
    page=$((page + 1))
done
jq -r '.jobs[] | select(.conclusion == "cancelled") | .id' "$output.tmp" | while read -r id; do
    gh api --paginate "repos/$repository/check-runs/$id/annotations?per_page=100" --jq ".[] | {job_id: $id, message: .message}"
done >> "$output.tmp"
mv "$output.tmp" "$output"
printf '%s: %s bytes\n' "$output" "$(wc -c < "$output")"
