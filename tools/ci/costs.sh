#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Regenerates the hosted rows' shard costs, tools/ci/costs/ (#2031; docs/commands/ci.md, "Shard costs"), from the job
# logs of the given runs of 9700x.yml: API glue only. tools/ci/costs.c reads and checks every log, and rejects one that
# lacks a line it needs, or the same job twice. Pass full runs (the nightly snapshot's, or a dispatched `full` run) for
# the sanitizer rows, and merge-queue candidates for more samples. With --peaks it writes only the commands' peak memory
# and the runners' (#2031; `costs peaks`) into the existing tables, keeping their costs: for runs whose logs carry
# `MiB peak`. Needs `gh` with read access to the repository; run it from the repository's root.
#
#   bash tools/ci/costs.sh [--peaks] LOGS_DIRECTORY RUN...
set -euo pipefail
step=collect
if [ "${1:-}" = --peaks ]; then
    step=peaks
    shift
fi
logs=${1:?usage: costs.sh [--peaks] LOGS_DIRECTORY RUN...}
shift
(( $# > 0 )) || { echo 'usage: costs.sh [--peaks] LOGS_DIRECTORY RUN...' >&2; exit 2; }
repository=buster14a/buster
mkdir -p "$logs"
# An API call, again after 2, 4 and 8 s when GitHub fails it (a 502 or 503 now and then over many runs' logs).
fetch()
{
    local attempt
    for attempt in 1 2 3; do
        gh api "$@" && return 0
        sleep $((2 ** attempt))
    done
    gh api "$@"
}
# The given runs' logs alone, whatever else the directory keeps from earlier fetches.
fetched=()
for run in "$@"; do
    [[ "$run" =~ ^[1-9][0-9]*$ ]] || { echo "costs.sh: not a run id: $run" >&2; exit 2; }
    # The run's latest attempt's shard jobs, and the vector row's draws (#2033), whose test step ran (passed or failed):
    # one that stopped before it, such as a draw's candidate that took no item, has no report, which costs.c would reject.
    fetch --paginate "repos/$repository/actions/runs/$run/jobs?per_page=100&filter=latest" \
        --jq '.jobs[] | select(.name | test(", shard [0-9]+ of [0-9]+\\)$| / Vector draw [0-9]+, candidate [0-9]+ \\("))
              | select(any(.steps[]?; .name == "Run correctness tests within the hosted CPU budget" and (.conclusion == "success" or .conclusion == "failure")))
              | .id' > "$logs/jobs-$run.txt"
    while IFS= read -r job; do
        [[ "$job" =~ ^[1-9][0-9]*$ ]]
        if [ ! -s "$logs/$run-$job.log" ]; then
            fetch "repos/$repository/actions/jobs/$job/logs" > "$logs/$run-$job.log.part"
            mv "$logs/$run-$job.log.part" "$logs/$run-$job.log"
        fi
        fetched+=("$logs/$run-$job.log")
    done < "$logs/jobs-$run.txt"
    printf 'costs.sh: run %s: %s shard logs\n' "$run" "$(wc -l < "$logs/jobs-$run.txt")" >&2
done
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/costs.c -o "$logs/costs"
(( ${#fetched[@]} > 0 )) || { echo 'costs.sh: the runs have no shard logs' >&2; exit 1; }
if [ "$step" = peaks ]; then
    "$logs/costs" peaks --tests tools/ci/correctness.tests --costs tools/ci/costs "${fetched[@]}"
else
    "$logs/costs" collect --tests tools/ci/correctness.tests --output tools/ci/costs "${fetched[@]}"
fi
"$logs/costs" check --tests tools/ci/correctness.tests --costs tools/ci/costs
