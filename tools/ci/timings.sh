#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Read-only, bounded API collection and JSON normalization. C owns accounting.
# Run on a disposable hosted runner; never on the benchmark machine.
set -euo pipefail
output=${1:?usage: timings.sh OUTPUT_DIRECTORY}
repository=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}
[[ "$repository" == buster14a/buster ]]
mkdir -p "$output"
output=$(cd "$output" && pwd)
replay=${CI_TIMINGS_REPLAY:-}
if [[ -n "$replay" ]]; then
    replay=$(cd "$replay" && pwd)
    [[ "$replay" != "$output" ]] || { echo 'timings: replay source must differ from output' >&2; exit 1; }
    [[ -f "$replay/collection.json" ]] || { echo 'timings: replay needs collection.json' >&2; exit 1; }
fi
# A directory may be reused, but one capture must never inherit CPU markers or
# raw responses from another. Remove only this collector's named artifacts,
# and only where its manifest establishes that it previously owned them.
if [[ -f "$output/collection.json" ]]; then
    rm -f "$output"/runs-*.json "$output"/jobs-*.json "$output"/attempt-*.json "$output"/compare-*.json "$output"/branch-*.json "$output"/job-*-evidence.json "$output"/nightly-report.json "$output"/nightly-reports.json "$output"/nightly-report.md
fi
work=$(mktemp -d "$output/.timings-XXXXXX")
trap 'rm -rf "$work"' EXIT

# Replays inherit the original bounds and snapshot instant, unless explicitly
# overridden. These are report inputs, not account-level scheduling controls.
setting()
{
    local name=$1 fallback=$2 key=$3 value=${!1-}
    if [[ -z "$value" && -n "$replay" ]]; then
        value=$(jq -r --arg key "$key" '.[$key] // empty' "$replay/collection.json")
    fi
    printf '%s' "${value:-$fallback}"
}
run_limit=$(setting CI_TIMINGS_RUN_LIMIT 10 run_limit)
log_limit=$(setting CI_TIMINGS_LOG_LIMIT 24 log_limit)
request_limit=$(setting CI_TIMINGS_MAX_REQUESTS 600 request_limit)
time_limit=$(setting CI_TIMINGS_MAX_SECONDS 600 time_limit)
attempt_limit=$(setting CI_TIMINGS_ATTEMPT_LIMIT 16 attempt_limit)
snapshot=$(setting CI_TIMINGS_NOW "$(date -u +%s)" snapshot)
split_at=$(setting CI_TIMINGS_SPLIT_AT - split_at)
since=$(setting CI_TIMINGS_SINCE - since)
until=$(setting CI_TIMINGS_UNTIL - until)
capacity=$(setting CI_TIMINGS_HOSTED_CAPACITY - capacity)
capacity_source=$(setting CI_TIMINGS_CAPACITY_SOURCE - capacity_source)
workflows=$(setting CI_TIMINGS_WORKFLOWS '9700x.yml build.yml build-pgo.yml selfhost.yml tier1-problem.yml publish-hosted-inputs.yml prepare-hosted-tools.yml nightly-backfill.yml ci-timings.yml vector-claim-race.yml hosted-inputs-drill.yml hotfix.yml rerun.yml' workflows)
for value in "$run_limit" "$log_limit" "$request_limit" "$time_limit" "$attempt_limit" "$snapshot"; do
    [[ "$value" =~ ^(0|[1-9][0-9]{0,11})$ ]] || { echo 'timings: invalid numeric collection bound' >&2; exit 1; }
done
(( run_limit >= 1 && run_limit <= 100 && log_limit <= 1000 && request_limit >= 1 && request_limit <= 10000 && time_limit >= 1 && time_limit <= 3600 && attempt_limit >= 1 && attempt_limit <= 100 )) || { echo 'timings: collection bound out of range' >&2; exit 1; }
if [[ "$capacity" != - ]]; then
    [[ "$capacity" =~ ^[1-9][0-9]{0,5}$ && "$capacity_source" != - ]] || { echo 'timings: actual capacity needs a positive limit and an administrative evidence source' >&2; exit 1; }
fi
for stamp in "$split_at" "$since" "$until"; do
    if [[ "$stamp" != - ]]; then
        [[ "$stamp" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$ ]] && date -u -d "$stamp" +%s > /dev/null || { echo 'timings: use UTC timestamps YYYY-MM-DDTHH:MM:SSZ' >&2; exit 1; }
    fi
done
split=null
[[ "$split_at" == - ]] || split=$(date -u -d "$split_at" +%s)
[[ "$since" == - || "$until" == - || "$since" < "$until" ]] || { echo 'timings: since must precede until' >&2; exit 1; }
for workflow in $workflows; do
    case "$workflow" in
        9700x.yml|build.yml|build-pgo.yml|selfhost.yml|tier1-problem.yml|publish-hosted-inputs.yml|prepare-hosted-tools.yml|nightly-backfill.yml|ci-timings.yml|vector-claim-race.yml|hosted-inputs-drill.yml|hotfix.yml|rerun.yml) ;;
        *) echo 'timings: unknown workflow in CI_TIMINGS_WORKFLOWS' >&2; exit 1 ;;
    esac
done
jq -n --argjson snapshot "$snapshot" --argjson run_limit "$run_limit" --argjson log_limit "$log_limit" --argjson request_limit "$request_limit" --argjson time_limit "$time_limit" --argjson attempt_limit "$attempt_limit" --arg split_at "$split_at" --arg since "$since" --arg until "$until" --arg capacity "$capacity" --arg capacity_source "$capacity_source" --arg workflows "$workflows" \
    '{snapshot:$snapshot,run_limit:$run_limit,log_limit:$log_limit,request_limit:$request_limit,time_limit:$time_limit,attempt_limit:$attempt_limit,split_at:$split_at,since:$since,until:$until,capacity:$capacity,capacity_source:$capacity_source,workflows:$workflows}' > "$output/collection.json"
: > "$output/requests.tsv"
: > "$output/notes.tsv"
: > "$output/run-links.tsv"
: > "$work/selected.jsonl"
: > "$work/evidence.jsonl"
printf '[]\n' > "$work/seen-jobs.json"
printf 'meta\t%s\t%s\t%s\t%s\n' "$snapshot" "${split/null/-}" "$capacity" "$(printf '%s' "$capacity_source" | tr '\t\r\n' '   ')" > "$output/facts.tsv"
request_count=0
request_failed=0
log_count=0
normalized_runs=0
normalized_jobs=0
started=$SECONDS

note()
{
    printf 'note\t%s\t%s\n' "$1" "$2" >> "$output/notes.tsv"
}
# Every call, including retries and every jobs page, spends the same budget.
# A failed endpoint is an explicit incomplete observation, never a fake zero.
request()
{
    local key=$1 endpoint=$2 kind=${3:-json} destination="$output/$1.json" attempt result size
    if [[ "$kind" == log ]]; then destination="$work/job.log"; fi
    rm -f "$destination"
    if [[ -n "$replay" ]]; then
        if [[ -f "$replay/$key.json" ]]; then
            cp "$replay/$key.json" "$destination"
            printf '%s\t0\treplay\t%s\n' "$key" "$endpoint" >> "$output/requests.tsv"
            jq -e '(type == "object" and .message? == null and .collection_error? == null) or type == "array"' "$destination" > /dev/null 2>&1
            return
        fi
        note "$key" 'No saved response was available during replay.'
        printf '{"collection_error":"missing replay response"}\n' > "$destination"
        request_failed=$((request_failed + 1))
        return 1
    fi
    for attempt in 1 2; do
        if (( request_count >= request_limit || SECONDS - started >= time_limit )); then
            note "$key" 'Collection request/time budget exhausted; observation is incomplete.'
            printf '{"collection_error":"collection budget exhausted"}\n' > "$destination"
            request_failed=$((request_failed + 1))
            return 1
        fi
        request_count=$((request_count + 1))
        result=0
        timeout --kill-after=5 30 gh api "$endpoint" > "$destination" 2> "$work/request-error" || result=$?
        size=$(wc -c < "$destination")
        if [[ "$result" -eq 0 && "$size" -le 33554432 ]] && { [[ "$kind" == log ]] || jq -e 'type == "object" or type == "array"' "$destination" > /dev/null 2>&1; }; then
            printf '%s\t%s\tok\t%s\n' "$key" "$attempt" "$endpoint" >> "$output/requests.tsv"
            return 0
        fi
        printf '%s\t%s\tfailed\t%s\n' "$key" "$attempt" "$endpoint" >> "$output/requests.tsv"
        # A confirmed 404 is useful branch-deletion evidence, and retrying a
        # missing workflow or expired artifact cannot improve this snapshot.
        if jq -e '.status == "404" or .status == 404' "$destination" > /dev/null 2>&1; then break; fi
        [[ "$attempt" == 2 ]] || sleep 1
    done
    if ! jq -e 'type == "object"' "$destination" > /dev/null 2>&1; then
        printf '{"collection_error":"request failed or exceeded response limit"}\n' > "$destination"
    fi
    note "$key" 'API response unavailable after bounded collection; observation is incomplete.'
    request_failed=$((request_failed + 1))
    return 1
}

# Each workflow has its own bounded sample. The integration workflow also has
# one per event, so a burst of nightly admits cannot hide PR or queue demand.
# A requested split queries both time windows; each side remains bounded.
list_runs()
{
    local workflow=$1 event=$2 window=$3 lower=$since upper=$until key endpoint count total
    if [[ "$window" == before ]]; then
        boundary=$(date -u -d "@$((split - 1))" +%Y-%m-%dT%H:%M:%SZ)
        [[ "$upper" != - && "$upper" < "$boundary" ]] || upper=$boundary
    elif [[ "$window" == after ]]; then
        [[ "$lower" != - && "$lower" > "$split_at" ]] || lower=$split_at
    fi
    if [[ "$lower" != - && "$upper" != - && "$lower" > "$upper" ]]; then
        note "$workflow/$event/$window" 'The requested cohort is outside the collection time bounds.'
        return
    fi
    key="runs-$workflow-$event-$window"
    endpoint="repos/$repository/actions/workflows/$workflow/runs?per_page=$run_limit"
    [[ "$event" == all ]] || endpoint+="&event=$event"
    if [[ "$lower" != - && "$upper" != - ]]; then
        endpoint+="&created=$lower..$upper"
    elif [[ "$lower" != - ]]; then
        endpoint+="&created=>=$lower"
    elif [[ "$upper" != - ]]; then
        endpoint+="&created=<=$upper"
    fi
    if request "$key" "$endpoint" && jq -e '.workflow_runs | type == "array"' "$output/$key.json" > /dev/null; then
        jq -c --arg workflow "$workflow" --argjson limit "$run_limit" '.workflow_runs[:$limit][] | . + {collection_workflow:$workflow}' "$output/$key.json" >> "$work/selected.jsonl"
        count=$(jq '.workflow_runs | length' "$output/$key.json")
        total=$(jq -r '.total_count // "unknown"' "$output/$key.json")
        note "$workflow/$event/$window" "Sample: latest $count of $total runs returned, limit $run_limit, created filter $lower to $upper; jobs and attempts have their own completeness flags."
    else
        note "$workflow/$event/$window" 'Workflow run list unavailable or malformed; no zero-latency sample is implied.'
    fi
}
for workflow in $workflows; do
    events=all
    [[ "$workflow" != 9700x.yml ]] || events='pull_request merge_group workflow_dispatch schedule push'
    windows=all
    [[ "$split" == null ]] || windows='before after'
    for event in $events; do
        for window in $windows; do list_runs "$workflow" "$event" "$window"; done
    done
done
# Duplicate run IDs from malformed/repeated lists are not silently weighted.
jq -s 'group_by(.id) | map(max_by(.run_attempt, .updated_at)) | sort_by(.collection_workflow, .created_at, .id)' "$work/selected.jsonl" > "$output/selected-runs.json"
listed=$(wc -l < "$work/selected.jsonl")
unique=$(jq 'length' "$output/selected-runs.json")
[[ "$listed" -eq "$unique" ]] || note run-list 'Repeated run IDs were de-duplicated before collection.'

instant()
{
    local stamp=$1
    if [[ "$stamp" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,9})?Z$ ]]; then
        date -u -d "$stamp" +%s 2> /dev/null || printf -- '-'
    else
        printf -- '-'
    fi
}
log_evidence()
{
    local job=$1 cpu start acquired released evidence="$output/job-$1-evidence.json"
    if [[ -n "$replay" ]]; then
        if [[ -f "$replay/job-$job-evidence.json" ]] && jq -e --argjson job "$job" '.id == $job' "$replay/job-$job-evidence.json" > /dev/null; then
            cp "$replay/job-$job-evidence.json" "$evidence"
            cat "$evidence" >> "$work/evidence.jsonl"
        fi
    elif (( log_count < log_limit )); then
        log_count=$((log_count + 1))
        if request "job-$job-log" "repos/$repository/actions/jobs/$job/logs" log; then
            # Extract only the explicit CPU and lock markers. Shell snippets,
            # arbitrary other output and full job logs are never retained.
            awk '
                { stamp = $1; sub(/^[^0-9]*/, "", stamp); line = $0; sub(/^[^ ]+ /, "", line) }
                cpu == "" && line ~ /^(cpu model|model name[[:space:]]*):/ { cpu = line; sub(/^[^:]*:[[:space:]]*/, "", cpu) }
                cpu == "" && line ~ /^Draw [0-9]+, candidate [0-9]+:/ { cpu = line; sub(/^Draw [0-9]+, candidate [0-9]+: /, "", cpu); sub(/, (un)?qualified.*$/, "", cpu) }
                start == "" && /##\[group\]Run .*\/job-started\.sh/ { start = stamp }
                acquired == "" && /buster-9700x: this job has the machine/ { acquired = stamp }
                /buster-9700x: gave the machine back/ { released = stamp }
                END { gsub(/[\t\r\n]/, " ", cpu); printf "%s\n%s\n%s\n%s\n", cpu == "" ? "-" : cpu, start == "" ? "-" : start, acquired == "" ? "-" : acquired, released == "" ? "-" : released }
            ' "$work/job.log" > "$work/log-fields"
            { IFS= read -r cpu; IFS= read -r start; IFS= read -r acquired; IFS= read -r released; } < "$work/log-fields"
            start=$(instant "$start") acquired=$(instant "$acquired") released=$(instant "$released")
            jq -n --argjson id "$job" --arg cpu "$cpu" --arg start "$start" --arg acquired "$acquired" --arg released "$released" \
                'def number: if . == "-" then null else tonumber end; {id:$id,cpu:(if $cpu == "-" then null else $cpu end),lock_start:($start|number),lock_acquired:($acquired|number),lock_released:($released|number)}' > "$evidence"
            cat "$evidence" >> "$work/evidence.jsonl"
        fi
        rm -f "$work/job.log"
    fi
}

while IFS= read -r selected; do
    printf '%s\n' "$selected" > "$work/run.json"
    run=$(jq -r '.id' "$work/run.json")
    [[ "$run" =~ ^[1-9][0-9]*$ ]] || { note run-list 'Invalid run identity omitted.'; continue; }
    workflow=$(jq -r '.collection_workflow' "$work/run.json")
    attempts=$(jq -r '.run_attempt // 1' "$work/run.json")
    [[ "$attempts" =~ ^[1-9][0-9]*$ ]] || { note "$run" 'Invalid attempt count; run cannot be normalized.'; continue; }
    event=$(jq -r '.event // "unknown"' "$work/run.json")
    sha=$(jq -r '.head_sha // "-"' "$work/run.json")
    branch=$(jq -r '.head_branch // "-"' "$work/run.json")
    fate=na
    if [[ "$event" == merge_group && "$branch" == gh-readonly-queue/main/* && "$sha" =~ ^[0-9a-f]{40}$ ]]; then
        fate=unknown
        if request "compare-$run" "repos/$repository/compare/$sha...main"; then
            relation=$(jq -r '.status // "unknown"' "$output/compare-$run.json")
            case "$relation" in
                ahead|identical) fate=merged ;;
                behind|diverged)
                    encoded=$(jq -nr --arg branch "$branch" '$branch | @uri')
                    if request "branch-$run" "repos/$repository/branches/$encoded"; then
                        actual=$(jq -r '.commit.sha // "-"' "$output/branch-$run.json")
                        if [[ "$actual" == "$sha" ]]; then fate=live; elif [[ "$actual" =~ ^[0-9a-f]{40}$ ]]; then fate=invalidated; fi
                    elif jq -e '.status == "404" or .status == 404' "$output/branch-$run.json" > /dev/null; then
                        fate=invalidated
                    fi
                    # A merge can delete the branch between these two reads.
                    # Recheck ancestry before labelling its work invalidated.
                    if [[ "$fate" == invalidated ]]; then
                        fate=unknown
                        if request "compare-confirm-$run" "repos/$repository/compare/$sha...main"; then
                            confirmation=$(jq -r '.status // "unknown"' "$output/compare-confirm-$run.json")
                            case "$confirmation" in
                                ahead|identical) fate=merged ;;
                                behind|diverged) fate=invalidated ;;
                            esac
                        fi
                    fi
                    ;;
            esac
        fi
    elif [[ "$event" == merge_group ]]; then
        fate=unknown
    elif [[ "$event" == pull_request ]] && jq -e --argjson run "$run" '
        . as $all | .[] | select(.id == $run and .conclusion == "cancelled") as $old |
        [$all[] | select(.id != $old.id and .event == "pull_request" and .collection_workflow == $old.collection_workflow and .created_at > $old.created_at)
            | select(any(.pull_requests[]?; .number as $n | any($old.pull_requests[]?; .number == $n))
                or ((.pull_requests // [] | length) == 0 and ($old.pull_requests // [] | length) == 0
                    and ($old.head_repository.id // null) != null and .head_repository.id == $old.head_repository.id
                    and ($old.head_branch // "") != "" and .head_branch == $old.head_branch and .head_sha != $old.head_sha))] | length > 0' "$output/selected-runs.json" > /dev/null; then
        fate=superseded-possible
    fi
    if (( attempts > attempt_limit )); then
        note "$run" "Attempt count $attempts exceeds collection limit $attempt_limit; older attempts not fully observed."
    fi
    attempt=1
    while (( attempt <= attempts )); do
        if (( normalized_runs >= 4096 )); then
            note collection 'Normalized attempt limit 4096 reached; remaining selected runs/attempts are not observed by this report. Their run-list metadata is retained.'
            break 2
        fi
        if (( attempt == attempt_limit && attempts > attempt_limit )); then attempt=$attempts; fi
        complete=1
        if (( attempts > attempt_limit )); then complete=0; fi
        if (( attempt == attempts )); then
            cp "$work/run.json" "$work/attempt.json"
        elif request "attempt-$run-$attempt" "repos/$repository/actions/runs/$run/attempts/$attempt" && jq -e --argjson run "$run" '.id == $run' "$output/attempt-$run-$attempt.json" > /dev/null; then
            # Creation belongs to the revision's original run, not a retry.
            jq --slurpfile original "$work/run.json" '.created_at=$original[0].created_at' "$output/attempt-$run-$attempt.json" > "$work/attempt.json"
        else
            jq '.status="unknown" | .conclusion=null | .run_started_at=null | .updated_at=null' "$work/run.json" > "$work/attempt.json"
            complete=0
        fi
        : > "$work/jobs.jsonl"
        expected=-1
        page=1
        while :; do
            key="jobs-$run-$attempt-$page"
            if ! request "$key" "repos/$repository/actions/runs/$run/attempts/$attempt/jobs?per_page=100&page=$page" || ! jq -e '.jobs | type == "array"' "$output/$key.json" > /dev/null; then complete=0; break; fi
            count=$(jq '.jobs | length' "$output/$key.json")
            total=$(jq -r '.total_count // -1' "$output/$key.json")
            [[ "$total" =~ ^[0-9]+$ ]] || { complete=0; total=-1; }
            if (( expected >= 0 && expected != total )); then complete=0; fi
            expected=$total
            jq -c '.jobs[]' "$output/$key.json" >> "$work/jobs.jsonl"
            if (( count < 100 || page * 100 >= expected && expected >= 0 )); then break; fi
            page=$((page + 1))
            if (( page > 100 )); then complete=0; note "$run/$attempt" 'Jobs pagination exceeded 100 pages; attempt incomplete.'; break; fi
        done
        jq -s --argjson run "$run" --argjson attempt "$attempt" --argjson remaining "$((65536 - normalized_jobs))" --slurpfile seen "$work/seen-jobs.json" '
            group_by(.id) | map(.[0]) | map(select((.run_id == null or .run_id == $run) and (.run_attempt == null or .run_attempt == $attempt) and (.id as $id | $seen[0] | index($id) | not)))
            | sort_by(.started_at, .id) | .[:$remaining]' "$work/jobs.jsonl" > "$work/jobs.json"
        count=$(wc -l < "$work/jobs.jsonl")
        unique=$(jq 'length' "$work/jobs.json")
        if (( count != unique || count != expected )); then complete=0; fi
        if (( count > 65536 - normalized_jobs )); then note "$run/$attempt" 'Normalized job limit 65536 reached; runner consumption is incomplete.'; fi
        jq --slurpfile jobs "$work/jobs.json" '. + [$jobs[0][].id]' "$work/seen-jobs.json" > "$work/seen-jobs-next.json"
        mv "$work/seen-jobs-next.json" "$work/seen-jobs.json"
        if [[ "$complete" == 0 ]]; then note "$run/$attempt" 'Attempt metadata or jobs pagination incomplete; available jobs retained once.'; fi
        : > "$work/evidence.jsonl"
        # Hardware lock evidence first; then eligible allocated jobs, explicitly
        # bounded. Unread CPU models remain unknown and are counted as such.
        jq -r 'sort_by(if (.labels // [] | index("self-hosted")) != null then 0 else 1 end)[]
            | select((.runner_id // 0) > 0)
            | select((.labels // [] | index("self-hosted")) != null
                or any(.steps[]?; (.name // "") | test("^(Report (host CPU|sanitizer host)|Probe this runner.s CPU|Record the host)")))
            | .id' "$work/jobs.json" > "$work/log-jobs"
        while IFS= read -r job; do
            [[ "$job" =~ ^[1-9][0-9]*$ ]] || continue
            log_evidence "$job"
        done < "$work/log-jobs"
        jq -nr --slurpfile run "$work/attempt.json" --slurpfile jobs "$work/jobs.json" --slurpfile evidence "$work/evidence.jsonl" --argjson attempt "$attempt" --arg workflow "$workflow" --argjson split "$split" --arg fate "$fate" --argjson complete "$complete" -f tools/ci/timings-facts.jq >> "$output/facts.tsv"
        normalized_runs=$((normalized_runs + 1))
        normalized_jobs=$((normalized_jobs + unique))
        printf '%s\t%s\t%s\t%s\n' "$run" "$attempt" "$workflow" "https://github.com/$repository/actions/runs/$run/attempts/$attempt" >> "$output/run-links.tsv"
        attempt=$((attempt + 1))
    done
done < <(jq -c '.[]' "$output/selected-runs.json")

# Reuse #1439's published ledger report. Its age is explicitly as of its own
# cycle, not silently advanced to this report's collection instant.
if request nightly-reports 'repos/buster14a/buster-performance/contents/nightly/reports?ref=nightly-ledger' && jq -e 'type == "array"' "$output/nightly-reports.json" > /dev/null; then
    report=$(jq -r '[.[] | select(.type == "file" and (.name | test("^[0-9]{4}-[0-9]{2}-[0-9]{2}\\.md$")))] | sort_by(.name) | last | .name // "-"' "$output/nightly-reports.json")
    if [[ "$report" != - ]] && request nightly-report "repos/buster14a/buster-performance/contents/nightly/reports/$report?ref=nightly-ledger" && jq -e '.encoding == "base64" and (.content | type) == "string"' "$output/nightly-report.json" > /dev/null; then
        if jq -r '.content' "$output/nightly-report.json" | base64 -d > "$output/nightly-report.md"; then
            note nightly "Existing #1439 report, as of cycle ${report%.md}: https://github.com/buster14a/buster-performance/blob/nightly-ledger/nightly/reports/$report"
            while IFS= read -r line; do note nightly "${line//$'\t'/ }"; done < <(sed -n '/^Carried over: /p; /^Capacity: /p' "$output/nightly-report.md")
        else
            note nightly 'Existing nightly report could not be decoded; oldest-state backlog age is unknown here.'
        fi
    else
        note nightly 'No readable dated #1439 report; oldest-state backlog age is unknown here.'
    fi
else
    note nightly 'Main backlog age unavailable here; use the existing #1439 report: https://github.com/buster14a/buster/actions/workflows/nightly-backfill.yml. This report does not create another ledger.'
fi
note collection "At most $run_limit runs per workflow/event/time window; all reported conclusions retained. API attempts $request_count/$request_limit, unavailable responses $request_failed; log reads $log_count/$log_limit. Unread CPU/lock values remain unknown."
awk 'NR <= 500 {print} END {if (NR > 500) printf "note\tcollection\t%d additional collection notes omitted from the summary; all remain in notes.tsv.\n", NR - 500}' "$output/notes.tsv" >> "$output/facts.tsv"
if [[ "$(wc -l < "$output/notes.tsv")" -gt 500 ]]; then tail -n 1 "$output/notes.tsv" >> "$output/facts.tsv"; fi
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/timings.c -o "$work/timings"
"$work/timings" --accounting < "$output/facts.tsv" > "$output/summary.md"
cat "$output/summary.md"
