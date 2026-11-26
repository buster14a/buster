#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Read-only collection for nightly.c's calendar-cycle observer (#3627). Never
# dispatch, cancel, rerun, change a ref or publish a replacement ledger here.
set -Eeuo pipefail
shopt -s inherit_errexit
output=${1:?usage: nightly-observe.sh NEW_OUTPUT_DIRECTORY}
repository=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}
[[ "$repository" == buster14a/buster ]]
mkdir -p "$output"
output=$(cd "$output" && pwd)
# Refuse reuse so a previous capture's files cannot become fresh evidence.
mkdir "$output/raw" "$output/attempts" "$output/records" "$output/workers"
tool="$output/nightly"
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -n "${NIGHTLY_OBSERVE_TEST_TOOL:-}" ]]; then
    # Fixtures compile this source once and reuse that binary across API cases.
    cp "$NIGHTLY_OBSERVE_TEST_TOOL" "$tool"
else
    cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing "$root/tools/ci/nightly.c" -o "$tool"
fi
gh=${NIGHTLY_OBSERVE_GH:-gh}
since=${NIGHTLY_OBSERVE_SINCE:-2026-11-07}
snapshot=${NIGHTLY_OBSERVE_NOW:-$(date -u +%s)}
request_limit=${NIGHTLY_OBSERVE_MAX_REQUESTS:-2000}
time_limit=${NIGHTLY_OBSERVE_MAX_SECONDS:-720}
page_limit=${NIGHTLY_OBSERVE_MAX_PAGES:-10}
attempt_limit=${NIGHTLY_OBSERVE_MAX_ATTEMPTS:-100}
for number in "$snapshot" "$request_limit" "$time_limit" "$page_limit" "$attempt_limit"; do
    [[ "$number" =~ ^(0|[1-9][0-9]{0,11})$ ]] || { printf 'nightly-observe: invalid collection bound\n' >&2; exit 2; }
done
(( request_limit >= 1 && request_limit <= 10000 && time_limit >= 1 && time_limit <= 840 && page_limit >= 1 && page_limit <= 10 && attempt_limit >= 1 && attempt_limit <= 100 )) || { printf 'nightly-observe: collection bound out of range\n' >&2; exit 2; }
[[ "$since" =~ ^20[0-9]{2}-[0-9]{2}-[0-9]{2}$ ]] && [[ "$(date -u -d "$since" +%F)" == "$since" ]] || { printf 'nightly-observe: invalid first calendar date\n' >&2; exit 2; }
started=$SECONDS
collector_pid=$BASHPID
requests=0
complete=true
ledger_status=missing
ledger_published=0
ledger_fetched=$snapshot
printf '# buster-nightly-ledger 1\n' > "$output/ledger.tsv"
: > "$output/observations.tsv"
: > "$output/notes.tsv"
: > "$output/requests.tsv"
: > "$output/snapshot-runs.jsonl"
: > "$output/snapshot-runs.tsv"
jq -n --arg since "$since" --argjson snapshot "$snapshot" --argjson request_limit "$request_limit" --argjson time_limit "$time_limit" --argjson page_limit "$page_limit" --argjson attempt_limit "$attempt_limit" \
    '{since:$since,snapshot:$snapshot,request_limit:$request_limit,time_limit:$time_limit,page_limit:$page_limit,attempt_limit:$attempt_limit}' > "$output/collection.json"

unavailable()
{
    complete=false
    printf '%s\t%s\n' "$1" "$2" >> "$output/notes.tsv"
}

# Validate every dispatch before selecting snapshots. A malformed title must not
# hide a newer admission behind an older pass. C still owns all coverage credit.
run_schema='
def positive_integer: type == "number" and . > 0 and floor == .;
def instant: type == "string" and test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$") and (try (fromdateiso8601 | type == "number") catch false);
def nightly_name:
    . as $run |
    if (.display_title | startswith("Nightly ")) then
        ((.display_title | capture("^Nightly (?<purpose>snapshot|backlog) \\((?<flavor>admit|defer)\\) of (?<sha>[0-9a-f]{40}) \\[(?<date>[0-9]{8})\\.[0-9]+\\.[0-9]+\\.(?<request>admit|defer)\\]$")) // null) as $name |
        $name != null and $name.sha == $run.head_sha and $name.flavor == $name.request and $run.head_branch == ("nightly/" + $run.head_sha)
    else true end;
def valid_run:
    try ((.id | positive_integer) and (.run_attempt | positive_integer) and
        .head_repository.id == 1421805693 and .event == "workflow_dispatch" and
        .path == ".github/workflows/9700x.yml" and (.head_sha | type == "string" and test("^[0-9a-f]{40}$")) and
        (.head_branch | type == "string" and length > 0) and (.display_title | type == "string" and length > 0) and
        (.status | IN("completed", "queued", "in_progress", "waiting", "pending", "requested")) and
        (.conclusion == null or (.conclusion | type == "string" and length > 0)) and
        (.created_at | instant) and (.updated_at | instant) and nightly_name) catch false;
'
valid_runs()
{
    jq -e "$run_schema if \$collection then all(.workflow_runs[]; valid_run) else valid_run end" --argjson collection "$2" "$1" > /dev/null
}

# GET only, with at most two tries and shared request/time limits. Never consume
# a failed response. Reading a byte beyond the limit detects truncated downloads.
read_api()
{
    local key=$1 endpoint=$2 kind=${3:-json} destination try status size
    if [[ "$kind" == json ]]; then destination="$output/raw/$key.json"; else destination="$output/raw/$key.zip"; fi
    for try in 1 2; do
        if (( requests >= request_limit || SECONDS - started >= time_limit )); then
            unavailable "$key" 'Collection request/time budget exhausted.'
            return 1
        fi
        requests=$((requests + 1))
        status=0
        timeout --kill-after=2 25 "$gh" api --method GET "$endpoint" 2> "$output/raw/$key.error" | head -c 16777217 > "$destination.partial" || status=$?
        size=$(wc -c < "$destination.partial")
        if [[ "$status" -eq 0 && "$size" -le 16777216 ]] && { [[ "$kind" == zip ]] || jq -e 'type == "object" and .message? == null' "$destination.partial" > /dev/null 2>&1; }; then
            mv "$destination.partial" "$destination"
            printf '%s\t%s\tok\t%s\n' "$key" "$try" "$endpoint" >> "$output/requests.tsv"
            return 0
        fi
        printf '%s\t%s\tfailed\t%s\n' "$key" "$try" "$endpoint" >> "$output/requests.tsv"
        [[ "$try" == 2 ]] || sleep 1
    done
    unavailable "$key" 'API read failed, was malformed or exceeded the byte allowance.'
    return 1
}

# Explicit complete pagination. A changing total, GitHub's per-query 1000-item
# ceiling or a missing page poisons credit. Partial rows remain diagnostic data.
read_pages()
{
    local key=$1 endpoint=$2 array=$3 page total=-1 expected count read=0 valid=true
    : > "$output/raw/$key.jsonl"
    for (( page=1; page<=page_limit; page+=1 )); do
        if ! read_api "$key-$page" "$endpoint&per_page=100&page=$page" json; then valid=false; break; fi
        if ! jq -e '.total_count | type == "number" and . >= 0 and floor == .' "$output/raw/$key-$page.json" > /dev/null ||
           ! jq -e --arg array "$array" '.[$array] | type == "array" and length <= 100' "$output/raw/$key-$page.json" > /dev/null; then
            unavailable "$key" 'List response has no valid total_count and item array.'
            valid=false
            break
        fi
        expected=$(jq -r .total_count "$output/raw/$key-$page.json")
        count=$(jq -r --arg array "$array" '.[$array] | length' "$output/raw/$key-$page.json")
        if [[ "$total" -eq -1 ]]; then total=$expected; fi
        if [[ "$expected" -ne "$total" || "$total" -gt $((page_limit * 100)) ]]; then
            unavailable "$key" 'List changed during collection or exceeds the complete-pagination allowance.'
            valid=false
        fi
        jq -c --arg array "$array" '.[$array][]' "$output/raw/$key-$page.json" >> "$output/raw/$key.jsonl"
        read=$((read + count))
        if [[ "$valid" != true || "$read" -ge "$total" || "$count" -lt 100 ]]; then break; fi
    done
    if [[ "$total" -lt 0 || "$read" -ne "$total" ]]; then
        unavailable "$key" 'List is incomplete; missing rows are not evidence of absence.'
        valid=false
    fi
    jq -s --arg array "$array" '{($array):.}' "$output/raw/$key.jsonl" > "$output/raw/$key.json"
    if ! jq -e --arg array "$array" '.[$array] | all(.[]; .id | type == "number" and . > 0 and floor == .) and (length == (map(.id) | unique | length))' "$output/raw/$key.json" > /dev/null; then
        unavailable "$key" 'List has invalid or repeated item IDs.'
        valid=false
    fi
    [[ "$valid" == true ]]
}

# Archives are data: no extraction to their paths. Require one named member,
# its CRC check and a bounded complete output.
read_member()
{
    local archive=$1 member=$2 destination=$3 limit=$4 status=0 count
    count=$(unzip -Z1 "$archive" | awk -v member="$member" '$0 == member { n += 1 } END { print n + 0 }') || return 1
    [[ "$count" -eq 1 ]] || return 1
    timeout --kill-after=2 20 unzip -p "$archive" "$member" 2> "$destination.error" | head -c "$((limit + 1))" > "$destination.partial" || status=$?
    [[ "$status" -eq 0 && -s "$destination.partial" && "$(wc -c < "$destination.partial")" -le "$limit" ]] || return 1
    mv "$destination.partial" "$destination"
}

load_ledger()
{
    local run id published
    : > "$output/raw/ledger-candidates.jsonl"
    # Freshness search, not a coverage listing. Failed/cancelled coordinators may
    # have retained the newest valid ledger; do not filter by success.
    if ! read_api ledger-coordinators "repos/$repository/actions/workflows/nightly-backfill.yml/runs?branch=main&per_page=20" json; then ledger_status=unavailable; return; fi
    if ! jq -e '.workflow_runs | type == "array" and length <= 20' "$output/raw/ledger-coordinators.json" > /dev/null; then
        unavailable ledger 'Coordinator listing malformed.'
        ledger_status=unavailable
        return
    fi
    while read -r run; do
        if read_pages "ledger-artifacts-$run" "repos/$repository/actions/runs/$run/artifacts?" artifacts; then
            jq -c '.artifacts[] | select(.expired == false and (.name | test("^nightly-ledger-(prepare|window|window-continued)-[1-9][0-9]*$")))' "$output/raw/ledger-artifacts-$run.json" >> "$output/raw/ledger-candidates.jsonl"
        fi
    done < <(jq -r '.workflow_runs[] | select(.head_repository.id == 1421805693 and .head_branch == "main" and .path == ".github/workflows/nightly-backfill.yml" and (.event == "schedule" or .event == "workflow_dispatch")) | .id | select(type == "number" and . > 0 and floor == .)' "$output/raw/ledger-coordinators.json")
    if [[ -s "$output/raw/ledger-candidates.jsonl" ]]; then
        read -r id published < <(jq -sr 'sort_by(.created_at, .id) | last | [.id, (.created_at | fromdateiso8601)] | @tsv' "$output/raw/ledger-candidates.jsonl")
        if [[ "$id" =~ ^[1-9][0-9]*$ && "$published" =~ ^[1-9][0-9]*$ ]] &&
           read_api "ledger-$id" "repos/$repository/actions/artifacts/$id/zip" zip &&
           read_member "$output/raw/ledger-$id.zip" ledger.tsv "$output/ledger.tsv" 67108864; then
            ledger_status=present
            ledger_published=$published
        else
            unavailable ledger 'Newest retained ledger could not be read completely.'
            ledger_status=unavailable
        fi
    fi
}

load_runs()
{
    local day first last date end
    first=$(( $(date -u -d "$since 00:00:00" +%s) - 86400 ))
    last=$(( snapshot / 86400 * 86400 ))
    if (( last < first || (last - first) / 86400 > 1024 )); then
        unavailable runs 'Requested calendar span exceeds the bounded collection range.'
        return
    fi
    for (( day=first; day<=last; day+=86400 )); do
        date=$(date -u -d "@$day" +%F)
        end=$(date -u -d "@$((day + 86399 < snapshot ? day + 86399 : snapshot))" +%FT%TZ)
        read_pages "runs-$date" "repos/$repository/actions/workflows/9700x.yml/runs?event=workflow_dispatch&created=${date}T00:00:00Z..$end" workflow_runs || true
        if ! valid_runs "$output/raw/runs-$date.json" true ||
           ! jq -e --arg first "${date}T00:00:00Z" --arg last "$end" 'all(.workflow_runs[]; .created_at >= $first and .created_at <= $last)' "$output/raw/runs-$date.json" > /dev/null; then
            unavailable "runs-$date" 'Dispatch listing contains malformed, foreign or out-of-range run metadata.'
        fi
        jq -c '.workflow_runs[] | select(if (.display_title | type) == "string" then (.display_title | startswith("Nightly snapshot ")) else false end)' "$output/raw/runs-$date.json" >> "$output/snapshot-runs.jsonl"
        if (( requests >= request_limit || SECONDS - started >= time_limit )); then break; fi
    done
    if (( day < last )); then unavailable runs 'Calendar-run listing stopped before its final date.'; fi
    # Reruns retain original created_at. Refresh ledger snapshot IDs as well.
    local first_token=${since//-/} last_token
    last_token=$(TZ=Europe/Madrid date -d "@$snapshot" +%Y%m%d)
    {
        jq -r '.id | select(type == "number" and . > 0 and floor == .)' "$output/snapshot-runs.jsonl"
        awk -F '\t' -v first="$first_token" -v last="$last_token" '$1 == "run" && $6 == "snapshot" && $2 ~ /^[1-9][0-9]*$/ { split($7, request, "."); if (length(request[1]) == 8 && request[1] >= first && request[1] <= last) print $2 }' "$output/ledger.tsv"
    } | sort -nu > "$output/snapshot-runs.tsv"
}

worker_window()
{
    local sha=$1 source="$output/workers/$1.c" blob size declared
    worker_start=0
    worker_end=0
    if [[ ! -f "$output/workers/$sha.tsv" ]]; then
        printf 'worker-window\t-\t-\n' > "$output/workers/$sha.tsv"
        if read_api "worker-$sha" "repos/$repository/contents/tools/ci/nightly.c?ref=$sha" json &&
           jq -e '.type == "file" and .encoding == "base64" and .path == "tools/ci/nightly.c" and (.sha | test("^[0-9a-f]{40}$")) and (.size | type == "number" and . > 0 and . <= 1048576)' "$output/raw/worker-$sha.json" > /dev/null; then
            blob=$(jq -r .sha "$output/raw/worker-$sha.json")
            size=$(jq -r .size "$output/raw/worker-$sha.json")
            if jq -r .content "$output/raw/worker-$sha.json" | base64 --decode > "$source" &&
               [[ "$(wc -c < "$source")" -eq "$size" && "$(git hash-object "$source")" == "$blob" ]]; then
                "$tool" worker-window < "$source" > "$output/workers/$sha.tsv" || true
            fi
        fi
    fi
    read -r declared worker_start worker_end < "$output/workers/$sha.tsv"
    if [[ "$declared $worker_start $worker_end" != 'worker-window 2 8' && "$declared $worker_start $worker_end" != 'worker-window 4 7' ]]; then
        worker_start=0
        worker_end=0
        unavailable "worker-$sha" 'Immutable worker window is unavailable or unrecognized.'
    fi
}

run_facts()
{
    jq -er '["run", .id, .run_attempt, .event, .status, (.conclusion // "-"), .head_sha, .path, .head_branch,
        (.created_at | fromdateiso8601), (.updated_at | fromdateiso8601), .head_repository.id, .display_title] | @tsv' "$1"
}

load_attempts()
{
    local run=$1 latest sha attempt run_started jobs_status job_id native_started native_finished record_status published count id
    if ! read_api "run-$run" "repos/$repository/actions/runs/$run" json; then return; fi
    if ! valid_runs "$output/raw/run-$run.json" false ||
       ! jq -e --argjson run "$run" '.id == $run and (.display_title | startswith("Nightly snapshot ")) and .head_branch == ("nightly/" + .head_sha)' "$output/raw/run-$run.json" > /dev/null; then
        unavailable "run-$run" 'Snapshot run identity or latest attempt is invalid.'
        return
    fi
    latest=$(jq -r .run_attempt "$output/raw/run-$run.json")
    sha=$(jq -r .head_sha "$output/raw/run-$run.json")
    if (( latest > attempt_limit )); then unavailable "run-$run" 'Snapshot attempts exceed the collection allowance.'; return; fi
    worker_window "$sha"
    local artifacts_status=complete
    read_pages "records-$run" "repos/$repository/actions/runs/$run/artifacts?" artifacts || artifacts_status=unavailable
    for (( attempt=1; attempt<=latest; attempt+=1 )); do
        if ! read_api "run-$run-$attempt" "repos/$repository/actions/runs/$run/attempts/$attempt" json; then continue; fi
        if ! valid_runs "$output/raw/run-$run-$attempt.json" false ||
           ! jq -e "$run_schema .id == \$run and .run_attempt == \$attempt and .head_sha == \$sha and .display_title == \$original[0].display_title and .created_at == \$original[0].created_at and (.run_started_at | instant)" --argjson run "$run" --argjson attempt "$attempt" --arg sha "$sha" --slurpfile original "$output/raw/run-$run.json" "$output/raw/run-$run-$attempt.json" > /dev/null; then
            unavailable "run-$run-$attempt" 'Attempt metadata disagrees with its run.'
            continue
        fi
        run_started=$(jq -er '.run_started_at | fromdateiso8601' "$output/raw/run-$run-$attempt.json")
        if ! run_facts "$output/raw/run-$run-$attempt.json" > "$output/attempts/$run-$attempt.tsv"; then
            unavailable "run-$run-$attempt" 'Attempt facts could not be normalized.'
            continue
        fi
        jobs_status=complete
        read_pages "jobs-$run-$attempt" "repos/$repository/actions/runs/$run/attempts/$attempt/jobs?" jobs || jobs_status=unavailable
        if ! jq -e --argjson run "$run" --argjson attempt "$attempt" '.jobs | all(.[]; .run_id == $run and .run_attempt == $attempt)' "$output/raw/jobs-$run-$attempt.json" > /dev/null; then
            jobs_status=unavailable
            unavailable "jobs-$run-$attempt" 'Jobs do not belong to the exact run attempt.'
        fi
        jq -r --arg run "$run" '.jobs[] | ["job", $run, .status, (.conclusion // "-"), .name] | @tsv' "$output/raw/jobs-$run-$attempt.json" >> "$output/attempts/$run-$attempt.tsv"
        job_id=0
        native_started=0
        native_finished=0
        count=$(jq '[.jobs[] | select(.name == "Native tests and benchmarks on Benchpress (Ryzen 7 9700X)")] | length' "$output/raw/jobs-$run-$attempt.json")
        if [[ "$count" -eq 1 ]]; then
            if ! read -r job_id native_started native_finished < <(jq -er '.jobs[] | select(.name == "Native tests and benchmarks on Benchpress (Ryzen 7 9700X)") | [.id, (if .started_at == null then 0 else (.started_at | fromdateiso8601) end), (if .completed_at == null then 0 else (.completed_at | fromdateiso8601) end)] | @tsv' "$output/raw/jobs-$run-$attempt.json"); then
                jobs_status=unavailable
                unavailable "jobs-$run-$attempt" 'Native job timestamps are invalid.'
            fi
        elif [[ "$count" -gt 1 ]]; then
            jobs_status=unavailable
            unavailable "jobs-$run-$attempt" 'More than one native job in an attempt.'
        fi
        record_status=missing
        published=0
        if [[ "$artifacts_status" == unavailable ]]; then
            record_status=unavailable
        else
            count=$(jq --arg name "coverage-record-$attempt" '[.artifacts[] | select(.name == $name and .expired == false)] | length' "$output/raw/records-$run.json")
            if [[ "$count" -eq 1 ]]; then
                read -r id published < <(jq -r --arg name "coverage-record-$attempt" '.artifacts[] | select(.name == $name and .expired == false) | [.id, (.created_at | fromdateiso8601)] | @tsv' "$output/raw/records-$run.json")
                if read_api "record-$run-$attempt" "repos/$repository/actions/artifacts/$id/zip" zip &&
                   read_member "$output/raw/record-$run-$attempt.zip" record.tsv "$output/records/$run-$attempt.tsv" 65536; then
                    record_status=present
                else
                    record_status=unavailable
                    unavailable "record-$run-$attempt" 'Coverage record could not be read completely.'
                fi
            elif [[ "$count" -gt 1 ]]; then
                record_status=unavailable
                unavailable "record-$run-$attempt" 'More than one retained coverage record for an attempt.'
            elif jq -e --arg name "coverage-record-$attempt" '.artifacts | any(.[]; .name == $name and .expired == true)' "$output/raw/records-$run.json" > /dev/null; then
                record_status=expired
            fi
        fi
        printf 'attempt\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$run" "$attempt" "$run_started" "$job_id" "$native_started" "$native_finished" "$jobs_status" "$record_status" "$published" "$worker_start" "$worker_end" >> "$output/observations.tsv"
    done
    # A rerun that appeared while its earlier attempts were read is not hidden.
    if read_api "run-$run-final" "repos/$repository/actions/runs/$run" json; then
        if ! valid_runs "$output/raw/run-$run-final.json" false ||
           ! jq -e --slurpfile original "$output/raw/run-$run.json" '.id == $original[0].id and .head_sha == $original[0].head_sha and .created_at == $original[0].created_at and .display_title == $original[0].display_title and .run_attempt == $original[0].run_attempt' "$output/raw/run-$run-final.json" > /dev/null; then
            unavailable "run-$run" 'Run identity changed or a newer rerun appeared during collection; retry the observer.'
        fi
    fi
}

new_admissions()
{
    local first last
    first=$(date -u -d "@$snapshot" +%FT%TZ)
    last=$(date -u -d "@$((snapshot + SECONDS - started))" +%FT%TZ)
    # Initial listings stop at collection start. A new admission during capture
    # must not disappear behind an older pass, even if its cycle is already closed.
    read_pages final-dispatches "repos/$repository/actions/workflows/9700x.yml/runs?event=workflow_dispatch&created=$first..$last" workflow_runs || true
    if ! valid_runs "$output/raw/final-dispatches.json" true; then unavailable final-dispatches 'Final dispatch listing is malformed.'; fi
    jq -r '.workflow_runs[] | select(if (.display_title | type) == "string" then (.display_title | startswith("Nightly snapshot ")) else false end) | .id' "$output/raw/final-dispatches.json" | sort -u > "$output/raw/final-dispatches.tsv"
    sort -u "$output/snapshot-runs.tsv" > "$output/raw/collected-dispatches.tsv"
    if [[ -n "$(comm -23 "$output/raw/final-dispatches.tsv" "$output/raw/collected-dispatches.tsv")" ]]; then
        unavailable final-dispatches 'A new snapshot appeared during collection; retry the observer.'
    fi
}

finish_report()
{
    local finished=$((snapshot + SECONDS - started)) status=0
    printf 'collection\t%s\t%s\t%s\n' "$snapshot" "$finished" "$([[ "$complete" == true ]] && printf complete || printf unavailable)" >> "$output/observations.tsv"
    printf 'ledger\t%s\t%s\t%s\n' "$ledger_fetched" "$ledger_status" "$ledger_published" >> "$output/observations.tsv"
    "$tool" observe --since "$since" --now "$finished" --ledger "$output/ledger.tsv" --observations "$output/observations.tsv" --attempts "$output/attempts" --records "$output/records" --report "$output/report.md" > "$output/status.tsv" || status=$?
    if [[ -s "$output/report.md" ]]; then cat "$output/report.md"; fi
    if [[ "$status" -ne 0 ]]; then printf '::error title=Nightly full-cycle observer::A closed cycle is incomplete or its evidence is unavailable; inspect the retained report and exact attempts.\n'; fi
    return "$status"
}

collection_error()
{
    local status=$1
    trap - ERR
    # ERR is inherited by command/process substitutions. Only the main shell
    # may append the receipt; a child must not send report text into read/jq.
    if [[ "$BASHPID" != "$collector_pid" ]]; then exit "$status"; fi
    set +e
    unavailable collector "Collection aborted (status $status); uncollected evidence is unavailable."
    finish_report
    exit 2
}

# Unexpected malformed API fields still produce every expected date, unavailable.
trap 'collection_error "$?"' ERR
load_ledger
ledger_fetched=$((snapshot + SECONDS - started))
load_runs
while read -r run; do
    if (( requests >= request_limit || SECONDS - started >= time_limit )); then unavailable attempts 'Attempt collection budget exhausted.'; break; fi
    load_attempts "$run"
done < "$output/snapshot-runs.tsv"
new_admissions
trap - ERR
status=0
finish_report || status=$?
exit "$status"
