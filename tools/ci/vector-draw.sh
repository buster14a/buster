#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The vector row's draws (#2033; docs/ci.md, "The vector row"): which hosted candidate runs which item, decided by one
# job per draw, the collector, by rank; and the one product that each configuration's items share (#2032). Artifacts'
# names carry every fact, so that a job reads them all with one listing of the run's artifacts, and nothing needs write
# access but the run's own artifacts:
#
#   PREFIX-ATTEMPT-probe-LEVEL-CANDIDATE-yes|no    a candidate's probe verdict, which it publishes first
#   PREFIX-ATTEMPT-assign-LEVEL[-ITEMxCANDIDATE]... the collector's assignment of the draw's items, the only one
#   PREFIX-ATTEMPT-draw-LEVEL-CANDIDATE            a candidate's record: its CPU, probe verdict and decision
#   PREFIX-ATTEMPT-evidence-ITEM                   an item's passing execution: the record, and its build's and run's
#   vector-product-OOPTIMIZATION-ATTEMPT-CONFIGURATION-SHA256
#                                                  a configuration's compiler, which its item 1's holder builds once
#                                                  and publishes, packed by tools/ci/hosted-product.sh, with the
#                                                  archive's SHA-256; the configuration's other items install it
#
# PREFIX is vector-OOPTIMIZATION. The row is one pool of items across its configurations (its sanitizers,
# BUSTER_VECTOR_CONFIGURATIONS, whose initials differ): an item is a configuration's initial and one of its shards, `m2`
# for memory's shard 2 of its items' count. BUSTER_VECTOR_ITEMS lists every item the costliest first, and the row's
# items are those of its configurations, in that order. Artifact names are not claims: of 16 jobs that upload one name
# at once, all 16 succeed (vector-claim-race.yml's control round, 2026-11-11). So no candidate claims anything; the
# collector, one job, decides. Run with the row's settings in the environment (BUSTER_VECTOR_CONFIGURATIONS,
# BUSTER_VECTOR_ITEMS, BUSTER_SANITIZER_OPTIMIZATION) and the job's GITHUB_* variables, GH_TOKEN with `actions: read`:
#
#   vector-draw.sh assign LEVEL CANDIDATES NEXT
#       The collector of draw LEVEL, whose CANDIDATES candidates it waits for: until as many have qualified as items
#       are missing, or every candidate has published its verdict or ended, at most 25 minutes. The qualifying
#       candidates by rank (their numbers) get the missing items, the costliest first. Writes the assignment's
#       artifact name (`name`), and the next draw's size, NEXT candidates per item still missing (`count`,
#       `candidates`), to GITHUB_OUTPUT, and the assignment's table to RUNNER_TEMP/vector-assignment. The first draw
#       has at least 3 candidates per item (the row's 3; the race fixture's 4).
#   vector-draw.sh take LEVEL CANDIDATE VERDICT RECORD
#       A candidate, after its probe (VERDICT `qualified` or not) and its verdict's artifact: the item its draw's
#       collector assigned it, or nothing (a redraw). The item's settings go to GITHUB_ENV: BUSTER_VECTOR_ITEM, its
#       configuration as BUSTER_SANITIZER, its shard as BUSTER_SHARD of BUSTER_VECTOR_SHARDS, and whether it builds
#       the configuration's product (BUSTER_VECTOR_BUILDER, 1 for shard 1). Appends its decision to RECORD.
#       In a rerun of failed jobs, whose collectors don't run again, a candidate that held an item retakes it, and
#       fails, as no test does, if its new runner doesn't qualify: rerunning the failed jobs draws again.
#   vector-draw.sh product CONFIGURATION
#       An item other than its configuration's first, after its setup: waits for the product that the holder of the
#       configuration's item 1 publishes (in the attempt that holder last ran in), and writes its artifact's name and
#       SHA-256 to GITHUB_OUTPUT (`name`, `digest`). Polls every 30 s. Fails, without waiting further, when no candidate
#       can hold item 1 any more, when its holder ended without one, or when the attempt has two; at most 90 minutes.
#   vector-draw.sh verdict RECORDS
#       The row's verdict over the downloaded artifacts (a directory per artifact): exactly one passing qualified
#       execution of each item, by the candidate its latest assignment names, the configuration's item 1 with the
#       product it built and the others with one they installed. An item no candidate held fails it as incomplete,
#       which no test caused.
set -euo pipefail
shopt -s inherit_errexit

optimization=${BUSTER_SANITIZER_OPTIMIZATION:?}
prefix="vector-O$optimization"
product_prefix="vector-product-O$optimization"
attempt=${GITHUB_RUN_ATTEMPT:?}
read -r -a configurations <<< "${BUSTER_VECTOR_CONFIGURATIONS:?}"
read -r -a every_item <<< "${BUSTER_VECTOR_ITEMS:?}"
row="(Clang 23, ${configurations[*]}, O$optimization)"
# The interval between polls, and how long a collector waits for its candidates, a candidate for its collector and an
# item for its configuration's product. The draws the row unrolls (draw-1 to draw-4).
# Artifact polling uses the same-run Results service, as upload/download-artifact do, not the repository REST quota
# (#2309). The few job-state reads below have a fixed request allowance, including every page and failed request.
# A failed artifact listing doubles the pause, up to poll_limit, until a listing succeeds again.
poll=${VECTOR_DRAW_POLL:-15}
poll_limit=${VECTOR_DRAW_POLL_LIMIT:-60}

# The pause after a listing that succeeded ($1 = 0) or failed: $poll, or the last pause doubled, at most $poll_limit.
pause=$poll
next_pause()
{
    if [ "$1" -eq 0 ]; then
        pause=$poll
    else
        pause=$((pause * 2))
        [ "$pause" -le "$poll_limit" ] || pause=$poll_limit
        [ "$pause" -ge "$poll" ] || pause=$poll
    fi
}
collect_limit=${VECTOR_DRAW_COLLECT_LIMIT:-1500}
take_limit=${VECTOR_DRAW_TAKE_LIMIT:-1800}
product_limit=${VECTOR_DRAW_PRODUCT_LIMIT:-5400}
# An item polls for its product less often than a draw for its assignment: it waits for a build, minutes. A failed
# listing backs off as a draw's does.
product_poll=${VECTOR_DRAW_PRODUCT_POLL:-30}
levels=${VECTOR_DRAW_LEVELS:-4}

# On standard error, which the runner reads workflow commands from too.
die()
{
    printf '::error title=Vector draw::%s\n' "$*" >&2
    exit 1
}

summary()
{
    printf 'vector-draw: %s\n' "$*"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        printf -- '- %s\n' "$*" >> "$GITHUB_STEP_SUMMARY"
    fi
}

# REST accounting must survive command substitutions and pipelines. Each command has a private, persisted ledger;
# the workflow uploads its TSV even after failure, and the verdict totals this attempt's ledgers. First attempts:
# four collectors * two job pages + three PR product waiters * two pages + two quota samples = at most 16 requests.
# No candidate reads jobs on attempt 1. Reruns need job identity; they get six requests per invocation, never an
# unbounded poll. Failure to establish that identity is fatal, not permission to reuse an earlier assignment/product.
rest_command=${1:-invalid}
rest_directory="${RUNNER_TEMP:?}/vector-rest"
mkdir -p "$rest_directory"
rest_ledger="$rest_directory/$rest_command.tsv"
rest_state="$rest_directory/.$rest_command-state"
rest_budget=2
if [ "$attempt" -gt 1 ]; then rest_budget=6; fi
if [ "$rest_command" = take ] && [ "$attempt" -eq 1 ]; then rest_budget=0; fi
if [ "$rest_command" = assign ] && [ "${2:-}" = 1 ]; then rest_budget=$((rest_budget + 1)); fi
if [ "$rest_command" = verdict ]; then rest_budget=1; fi
printf 'request\tstatus\tremaining\treset\tendpoint\n' > "$rest_ledger"
printf '0 -1 0\n' > "$rest_state"
rest_reserve=100

# One charged REST request. No --paginate or implicit retry: callers charge pages separately, discard partial
# snapshots, and never retry past their allowance. Header observations describe the repository's shared quota,
# whereas the ledger counts this command's own requests, including HTTP/transport failures.
rest_get()
{
    local endpoint=$1 count remaining reset now raw status result=0 body
    read -r count remaining reset < "$rest_state"
    now=$(date +%s)
    if [ "$count" -ge "$rest_budget" ]; then
        printf 'vector-draw: REST allowance exhausted (%s requests); no further job reads.\n' "$rest_budget" >&2
        return 1
    fi
    if [ "$remaining" -ge 0 ] && [ "$remaining" -le "$rest_reserve" ] && [ "$now" -lt "$reset" ]; then
        printf 'vector-draw: preserving the REST reserve (%s remaining, reset %s).\n' "$remaining" "$reset" >&2
        return 1
    fi
    count=$((count + 1))
    # If no trustworthy headers come back, stop further reads in this invocation.
    printf '%s 0 9223372036854775807\n' "$count" > "$rest_state"
    raw=$(mktemp "$rest_directory/.response.XXXXXX")
    gh api --include -H 'Accept: application/vnd.github+json' "$endpoint" > "$raw" || result=$?
    status=$(awk '/^HTTP\// { gsub("\r", "", $2); print $2; exit }' "$raw")
    remaining=$(awk 'tolower($1) == "x-ratelimit-remaining:" { gsub("\r", "", $2); print $2; exit }' "$raw")
    reset=$(awk 'tolower($1) == "x-ratelimit-reset:" { gsub("\r", "", $2); print $2; exit }' "$raw")
    printf '%s\t%s\t%s\t%s\t%s\n' "$count" "${status:-unknown}" "${remaining:-unknown}" "${reset:-unknown}" "$endpoint" >> "$rest_ledger"
    if [[ "$remaining" =~ ^[0-9]+$ && "$reset" =~ ^[0-9]+$ ]]; then
        printf '%s %s %s\n' "$count" "$remaining" "$reset" > "$rest_state"
    else
        result=1
        printf 'vector-draw: REST response lacks valid quota headers; refusing further reads.\n' >&2
    fi
    body=$(awk 'body { print } /^\r?$/ { body=1 }' "$raw")
    rm -f -- "$raw"
    if [ "$result" -ne 0 ] || [[ ! "$status" =~ ^2[0-9][0-9]$ ]]; then return 1; fi
    printf '%s\n' "$body"
}

rest_summary()
{
    local count remaining reset
    read -r count remaining reset < "$rest_state"
    summary "REST $rest_command: $count/$rest_budget requests; last shared remaining=$remaining, reset=$reset"
}
trap 'rest_summary >&2' EXIT

# A full jobs snapshot or nothing. A list exceeding this invocation's remaining page allowance is optional on an
# initial draw, but cannot establish rerun identity. A changing or malformed total likewise cannot prove absence.
jobs_snapshot()
{
    local page=1 response total=-1 expected size combined='[]'
    while true; do
        response=$(rest_get "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt/jobs?per_page=100&page=$page") || return 1
        jq -e --argjson attempt "$attempt" '(.total_count | type == "number") and (.total_count >= 0) and (.total_count == (.total_count | floor)) and
            (.jobs | type == "array") and (.jobs | length <= 100) and
            all(.jobs[]; (.id | type == "number") and (.id > 0) and (.id == (.id | floor)) and
                (.name | type == "string") and (.name | length > 0) and (.name | test("[\\r\\n\\t]") | not) and
                (.status | type == "string") and (.status | IN("queued", "waiting", "pending", "requested", "in_progress", "completed")) and
                (.run_attempt | type == "number") and (.run_attempt >= 1) and (.run_attempt <= $attempt) and (.run_attempt == (.run_attempt | floor)) and
                (.conclusion == null or (.conclusion | type == "string")) and
                ((.started_at == null and .status != "completed") or
                 ((.started_at | type == "string") and (.started_at | test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z$")))))' <<< "$response" > /dev/null || return 1
        expected=$(jq -r '.total_count' <<< "$response")
        if [ "$total" -ge 0 ] && [ "$total" -ne "$expected" ]; then return 1; fi
        total=$expected
        size=$(jq '.jobs | length' <<< "$response")
        combined=$(jq -cs '.[0] + .[1].jobs' <(printf '%s\n' "$combined") <(printf '%s\n' "$response")) || return 1
        if [ "$(jq length <<< "$combined")" -eq "$total" ]; then break; fi
        [ "$size" -eq 100 ] && [ "$(jq length <<< "$combined")" -lt "$total" ] || return 1
        page=$((page + 1))
    done
    jq -e 'length == (map(.id) | unique | length)' <<< "$combined" > /dev/null || return 1
    jq -c '{jobs: .}' <<< "$combined"
}

# The configuration whose initial item $1 has.
item_configuration()
{
    local configuration
    for configuration in "${configurations[@]}"; do
        if [ "${configuration:0:1}" = "${1:0:1}" ]; then
            printf '%s\n' "$configuration"
        fi
    done
}

# The row's items, the costliest first: those of BUSTER_VECTOR_ITEMS whose initial is one of its configurations'. Each
# configuration's are its shards 1 to N, each once.
items=()
initials=''
for configuration in "${configurations[@]}"; do
    [[ "$configuration" =~ ^[a-z]+$ ]] || die "a configuration that isn't a name: '$configuration'"
    [[ "$initials" != *"${configuration:0:1}"* ]] || die "two configurations with the initial ${configuration:0:1}: ${configurations[*]}"
    initials+=${configuration:0:1}
done
for item in "${every_item[@]}"; do
    [[ "$item" =~ ^[a-z][1-9][0-9]*$ ]] || die "an item that isn't a configuration's initial and a shard: '$item'"
    if [ -n "$(item_configuration "$item")" ]; then
        items+=("$item")
    fi
done
[ "${#items[@]}" -gt 0 ] || die "no item of the configurations ${configurations[*]} in '${every_item[*]}'"
for configuration in "${configurations[@]}"; do
    count=0
    for item in "${items[@]}"; do
        [ "${item:0:1}" != "${configuration:0:1}" ] || count=$((count + 1))
    done
    for ((shard = 1; shard <= count; shard += 1)); do
        [ "$(printf '%s\n' "${items[@]}" | grep -cx "${configuration:0:1}$shard")" -eq 1 ] ||
            die "the items of $configuration aren't its shards 1 to $count, each once: ${items[*]}"
    done
done

# Item $1's shard count: its configuration's items.
item_shards()
{
    local item count=0
    for item in "${items[@]}"; do
        [ "${item:0:1}" != "${1:0:1}" ] || count=$((count + 1))
    done
    printf '%s\n' "$count"
}

# The run's artifacts' names, every attempt's; nothing (a failed listing, which the caller retries) on failure.
artifact_names()
{
    "${VECTOR_ARTIFACT_NODE:?run through the vector-runtime action}" "${VECTOR_ARTIFACT_HELPER:?}" list
}

# The pairs `ITEM CANDIDATE` of attempt $2's assignment of draw $3 in the names $1, a line each; status 1 without one.
# Two assignments of one draw in one attempt can't be: one collector uploads it once.
assignment()
{
    local names=$1 at=$2 level=$3 found pair
    found=$(grep -E "^$prefix-$at-assign-$level(-[a-z][0-9]+x[0-9]+)*\$" <<< "$names" || true)
    [ -n "$found" ] || return 1
    [ "$(wc -l <<< "$found")" -eq 1 ] || die "attempt $at has $(wc -l <<< "$found") assignments of draw $level"
    for pair in $(tr '-' ' ' <<< "${found#"$prefix-$at-assign-$level"}"); do
        printf '%s %s\n' "${pair%x*}" "${pair#*x}"
    done
}

# The latest attempt, up to this one, whose collectors ran, in the names $1 (a directory's entries or the run's
# artifacts): its assignments stand, and later attempts (reruns of failed jobs) only rerun their holders. 0 for none.
assigning_attempt()
{
    local name at latest=0
    while IFS= read -r name; do
        at=${name#"$prefix"-}
        at=${at%%-*}
        if [[ "$name" == "$prefix"-*-assign-1* && "$at" =~ ^[0-9]+$ ]] && [ "$at" -gt "$latest" ] && [ "$at" -le "$attempt" ]; then
            latest=$at
        fi
    done <<< "$1"
    printf '%s\n' "$latest"
}

# This attempt's jobs, `RUN_ATTEMPT STATUS CONCLUSION NAME` each (tab-separated, `none` for a job not yet concluded):
# those it reran with its number, the others with `earlier`. The API lists a job the attempt didn't rerun with the
# attempt's number too, and only its start, before the attempt's, tells it apart (collector_runs, #2276).
attempt_jobs()
{
    local start='0' response
    if [ "$attempt" -gt 1 ]; then
        response=$(rest_get "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt") || return 1
        start=$(jq -r '.run_started_at' <<< "$response") || return 1
        [[ "$start" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z$ ]] || return 1
    fi
    response=$(jobs_snapshot) || return 1
    jq -r ".jobs[] | \"\\(if (.started_at // \"9\") >= \"$start\" then .run_attempt else \"earlier\" end)\\t\\(.status)\\t\\(if (.conclusion // \"\") == \"\" then \"none\" else .conclusion end)\\t\\(.name)\"" <<< "$response"
}

# Whether draw $1's collector runs in this attempt: always in the first; in a later one, only when the attempt reran
# every job, not the failed ones alone. A job the attempt didn't rerun lists in it too, with the attempt's number but
# its own start, before the attempt's (run 43048124249's attempt 2, 2026-11-11).
collector_runs()
{
    local level=$1 jobs run_attempt status conclusion name found=''
    [ "$attempt" -gt 1 ] || return 0
    jobs=$(attempt_jobs) || return 2
    while IFS=$'\t' read -r run_attempt status conclusion name; do
        if [ "$name" = "Vector draw $level's assignment $row" ] || [[ "$name" == *" / Vector draw $level's assignment $row" ]]; then
            [ -z "$found" ] || return 2
            found=$run_attempt
        fi
    done <<< "$jobs"
    [ -n "$found" ] || return 2
    if [ "$found" = "$attempt" ]; then return 0; fi
    if [ "$found" = earlier ]; then return 1; fi
    return 2
}

# The numbers of draw $1's candidates in the jobs of this attempt that have ended (a candidate that couldn't publish its
# verdict still decides nothing).
ended_candidates()
{
    local level=$1 response
    response=$(jobs_snapshot) || return 1
    jq -r '.jobs[] | select(.status == "completed") | .name' <<< "$response" |
        sed -n "s/.*Vector draw $level, candidate \([0-9][0-9]*\) ${row//[()]/[()]}\$/\1/p"
}

assign()
{
    local level=$1 candidates=$2 next=$3 start=$SECONDS polls=0 names k item pair taken decided count i list='' name
    local jobs_read=0 ended='' complete assignments
    local -a missing qualified pairs
    [[ "$level" =~ ^[1-9]$ && "$candidates" =~ ^[0-9]+$ && "$next" =~ ^[0-9]+$ ]] || die "assign: bad arguments: $*"
    [ "$level" -ne 1 ] || [ "$candidates" -ge $((3 * ${#items[@]})) ] ||
        die "the first draw has $candidates candidates for ${#items[@]} items: at least 3 per item (${items[*]})"
    : "${GITHUB_OUTPUT:?}" "${RUNNER_TEMP:?}"
    if [ "$level" -eq 1 ]; then
        # A charged, recorded quota observation before the draw. Failure cannot stop artifact-only progress.
        rest_get "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt" > /dev/null || true
    fi
    while true; do
        polls=$((polls + 1))
        if names=$(artifact_names); then
            next_pause 0
        else
            next_pause 1
            [ $((SECONDS - start)) -lt "$collect_limit" ] || die "draw $level: no complete artifact listing before the deadline; no assignment published"
            sleep "$pause"
            continue
        fi
        # The items no earlier draw of this attempt assigned, the costliest first.
        taken=''
        complete=1
        for ((k = 1; k < level; k += 1)); do
            if assignments=$(assignment "$names" "$attempt" "$k"); then
                taken+=" $(cut -d ' ' -f 1 <<< "$assignments" | tr '\n' ' ')"
            else
                complete=0
            fi
        done
        if [ "$complete" -ne 1 ]; then
            [ $((SECONDS - start)) -lt "$collect_limit" ] || die "draw $level: an earlier assignment is missing or ambiguous; no assignment published"
            sleep "$pause"
            continue
        fi
        missing=()
        for item in "${items[@]}"; do
            if ! grep -qw -- "$item" <<< "$taken"; then
                missing+=("$item")
            fi
        done
        mapfile -t qualified < <(sed -n "s/^$prefix-$attempt-probe-$level-\([0-9][0-9]*\)-yes\$/\1/p" <<< "$names" | sort -n -u)
        decided=$(sed -n "s/^$prefix-$attempt-probe-$level-\([0-9][0-9]*\)-\(yes\|no\)\$/\1/p" <<< "$names" | sort -n -u | grep -c . || true)
        if [ "${#qualified[@]}" -ge "${#missing[@]}" ] || [ "$decided" -ge "$candidates" ]; then
            break
        fi
        # Only one optional jobs snapshot, and only when artifacts did not already decide. The allowance counts
        # pages, not snapshots. An incomplete/error response means unknown; the artifact deadline still applies.
        if [ "$jobs_read" -eq 0 ]; then
            ended=$(ended_candidates "$level") || ended=''
            jobs_read=1
        fi
        decided=$( { sed -n "s/^$prefix-$attempt-probe-$level-\([0-9][0-9]*\)-\(yes\|no\)\$/\1/p" <<< "$names"
            printf '%s\n' "$ended"; } | sort -n -u | grep -c . || true)
        if [ "$decided" -ge "$candidates" ]; then break; fi
        if [ $((SECONDS - start)) -ge "$collect_limit" ]; then
            printf '::warning title=Vector draw::Draw %s: %s of %s candidates decided in %s s; assigning what qualified.\n' \
                "$level" "$decided" "$candidates" "$((SECONDS - start))"
            break
        fi
        sleep "$pause"
    done
    # By rank: the i-th qualifying candidate gets the i-th missing item.
    pairs=()
    name="$prefix-$attempt-assign-$level"
    for ((i = 0; i < ${#missing[@]} && i < ${#qualified[@]}; i += 1)); do
        pairs+=("${missing[i]}x${qualified[i]}")
        name+="-${missing[i]}x${qualified[i]}"
    done
    count=$(((${#missing[@]} - ${#pairs[@]}) * next))
    for ((i = 1; i <= count; i += 1)); do
        list+="${list:+, }$i"
    done
    {
        printf 'name=%s\n' "$name"
        printf 'count=%s\n' "$count"
        printf 'candidates=[%s]\n' "$list"
    } >> "$GITHUB_OUTPUT"
    {
        printf 'level\t%s\n' "$level"
        printf 'candidates\t%s\n' "$candidates"
        printf 'decided\t%s\n' "$decided"
        printf 'qualified\t%s\n' "${qualified[*]:-none}"
        printf 'missing\t%s\n' "${missing[*]:-none}"
        for pair in "${pairs[@]}"; do
            printf 'item %s\tcandidate %s\n' "${pair%x*}" "${pair#*x}"
        done
    } > "$RUNNER_TEMP/vector-assignment"
    summary "draw $level: $decided of $candidates candidates decided in $((SECONDS - start)) s ($polls polls); qualified: ${qualified[*]:-none}; assigned: ${pairs[*]:-none}; next draw: $count candidates"
}

take()
{
    local level=$1 candidate=$2 verdict=$3 record=$4 start=$SECONDS item='' decision held names at found configuration shard shards collector=0
    [[ "$level" =~ ^[1-9]$ && "$candidate" =~ ^[1-9][0-9]*$ ]] || die "take: bad arguments: $*"
    test -f "$record" || die "take: no record $record"
    collector_runs "$level" || collector=$?
    [ "$collector" -ne 2 ] || die "cannot establish whether draw $level's collector reran within the REST allowance/reserve; refusing an earlier assignment"
    if [ "$verdict" != qualified ] && [ "$attempt" -eq 1 ]; then
        decision='redraw: the CPU does not qualify'
    elif [ "$collector" -eq 0 ]; then
        if [ "$verdict" != qualified ]; then
            decision='redraw: the CPU does not qualify'
        else
            # The collector decides once enough candidates qualified, or all decided.
            found=''
            while [ -z "$found" ] && [ $((SECONDS - start)) -lt "$take_limit" ]; do
                if names=$(artifact_names); then
                    next_pause 0
                else
                    names=''
                    next_pause 1
                fi
                if found=$(assignment "$names" "$attempt" "$level"); then
                    found=${found:-none}
                else
                    found=''
                    sleep "$pause"
                fi
            done
            item=$(awk -v candidate="$candidate" '$2 == candidate { print $1 }' <<< "$found")
            if [ -n "$item" ]; then
                decision="took $item"
            elif [ -n "$found" ]; then
                decision='redraw: candidates of a higher rank took the items'
            else
                printf '::warning title=Vector draw::Draw %s: no assignment in %s s; ending as a redraw.\n' "$level" "$take_limit"
                decision='redraw: no assignment came'
            fi
        fi
    else
        # A rerun of the failed jobs: the latest earlier attempt's assignment stands, whose holder reruns its item.
        names=$(artifact_names) || die "the run's artifacts couldn't be listed"
        held=''
        at=$((attempt - 1))
        while [ -z "$held" ] && [ "$at" -ge 1 ]; do
            if found=$(assignment "$names" "$at" "$level"); then
                held="$at $(awk -v candidate="$candidate" '$2 == candidate { print $1 }' <<< "$found")"
            fi
            at=$((at - 1))
        done
        item=${held#* }
        if [ -z "$held" ] || [ -z "$item" ]; then
            item=''
            decision='redraw: it held no item'
        elif [ "$verdict" = qualified ]; then
            decision="took $item again (assigned in attempt ${held%% *})"
        else
            printf 'decision\tfailed: its rerun of item %s drew a CPU that does not qualify\n' "$item" >> "$record"
            die "Candidate $candidate of draw $level holds item $item (assigned in attempt ${held%% *}), and its rerun drew a CPU that doesn't qualify ($(sed -n 's/^cpu\t//p' "$record")). No test failed: rerun the failed jobs to draw again."
        fi
    fi
    printf 'decision\t%s\n' "$decision" >> "$record"
    if [ -n "$item" ]; then
        configuration=$(item_configuration "$item")
        shard=${item:1}
        shards=$(item_shards "$item")
        [ -n "$configuration" ] && [ "$shard" -le "$shards" ] || die "item $item is none of the row's: ${items[*]}"
        {
            printf 'BUSTER_VECTOR_ITEM=%s\n' "$item"
            printf 'BUSTER_SANITIZER=%s\n' "$configuration"
            printf 'BUSTER_SHARD=%s\n' "$shard"
            printf 'BUSTER_VECTOR_SHARDS=%s\n' "$shards"
            printf 'BUSTER_VECTOR_BUILDER=%s\n' "$([ "$shard" -eq 1 ] && echo 1 || echo 0)"
        } >> "${GITHUB_ENV:?}"
        # The line tools/ci/costs.c reads for the row of the candidate's log.
        printf 'Vector item %s: %s, shard %s of %s\n' "$item" "$configuration" "$shard" "$shards"
    fi
    summary "draw $level, candidate $candidate: $decision"
}

product()
{
    local configuration=$1 builder start=$SECONDS polls=0 names latest holder level job jobs run_attempt=1 status=unknown conclusion=unknown name found
    local identity_holder='' jobs_read=0 selected_attempt=0 listed_attempt listed_status listed_conclusion
    [ -n "$configuration" ] && [ "$(item_configuration "${configuration:0:1}1")" = "$configuration" ] || die "product: no configuration $configuration in ${configurations[*]}"
    builder="${configuration:0:1}1"
    : "${GITHUB_OUTPUT:?}"
    while true; do
        polls=$((polls + 1))
        if names=$(artifact_names); then
            next_pause 0
        else
            next_pause 1
            [ $((SECONDS - start)) -lt "$product_limit" ] || die "no complete artifact listing for $configuration's product before the deadline"
            sleep "$((pause > product_poll ? pause : product_poll))"
            continue
        fi
        latest=$(assigning_attempt "$names")
        holder=''
        for ((level = 1; level <= levels; level += 1)); do
            if [ -z "$holder" ]; then
                holder=$( { assignment "$names" "$latest" "$level" || true; } | awk -v item="$builder" -v level="$level" '$1 == item { print level, $2 }')
            fi
        done
        if [ -n "$holder" ]; then
            job="Vector draw ${holder% *}, candidate ${holder#* } $row"
            if [ "$attempt" -gt 1 ] && [ "$identity_holder" != "$latest $holder" ]; then
                # Identity, once per holder: queued/rerun jobs must not reuse a carried-over product. Unknown,
                # incomplete or rate-limited jobs data cannot establish identity and therefore fails closed.
                jobs=$(attempt_jobs) || die "cannot establish $configuration's builder attempt within the REST allowance/reserve; refusing an earlier product"
                found=''
                while IFS=$'\t' read -r listed_attempt listed_status listed_conclusion name; do
                    if [ "$name" = "$job" ] || [[ "$name" == *" / $job" ]]; then
                        found+="$listed_attempt $listed_status $listed_conclusion"$'\n'
                    fi
                done <<< "$jobs"
                [ "$(grep -c . <<< "$found" || true)" -eq 1 ] || die "cannot identify exactly one builder of $configuration in this attempt's jobs"
                read -r run_attempt status conclusion <<< "$found"
                [[ "$run_attempt" = "$attempt" || "$run_attempt" = earlier ]] || die "invalid builder attempt $run_attempt"
                identity_holder="$latest $holder"
                # Its uploads end before it does. Always refresh artifacts after learning it completed.
                continue
            fi
            selected_attempt=$run_attempt
            if [ "$run_attempt" = earlier ]; then
                # Carried over from an earlier attempt: its product is the latest attempt's before this one.
                selected_attempt=$( { grep -oE "^$product_prefix-[0-9]+-$configuration-[0-9a-f]{64}\$" <<< "$names" || true; } | sed -E "s/^$product_prefix-([0-9]+)-.*/\\1/" |
                    awk -v now="$attempt" '$1 < now && $1 > best { best = $1 } END { print best + 0 }')
            fi
            found=$(grep -E "^$product_prefix-$selected_attempt-$configuration-[0-9a-f]{64}\$" <<< "$names" || true)
            [ "$(grep -c . <<< "$found" || true)" -le 1 ] || die "attempt $selected_attempt has $(grep -c . <<< "$found") products of $configuration: $(tr '\n' ' ' <<< "$found")"
            if [ "$selected_attempt" -gt 0 ] && [ -n "$found" ]; then
                printf 'name=%s\ndigest=%s\n' "$found" "${found##*-}" >> "$GITHUB_OUTPUT"
                summary "the product of $configuration: $found, built by draw ${holder% *}'s candidate ${holder#* } in attempt $selected_attempt, after $((SECONDS - start)) s ($polls polls)"
                return 0
            fi
            if grep -qx "$prefix-$selected_attempt-builder-$configuration-ended" <<< "$names"; then
                status=completed
                conclusion='publication finished'
            fi
            [ "$status" != completed ] || die "The holder of item $builder (draw ${holder% *}, candidate ${holder#* }) ended ($conclusion) in attempt $selected_attempt without publishing $configuration's product, which this item installs: see its job. Rerun the failed jobs: it reruns first."
            if [ "$attempt" -eq 1 ] && [ "$jobs_read" -eq 0 ]; then
                # One optional early-failure snapshot, only if the product/ended artifacts have not decided.
                # A builder normally publishes its ended marker even after setup/build/upload failure; a lost
                # runner without a marker is covered by this snapshot and the existing product deadline.
                jobs_read=1
                if jobs=$(attempt_jobs); then
                    found=''
                    while IFS=$'\t' read -r listed_attempt listed_status listed_conclusion name; do
                        if [ "$name" = "$job" ] || [[ "$name" == *" / $job" ]]; then
                            found+="$listed_status $listed_conclusion"$'\n'
                        fi
                    done <<< "$jobs"
                    if [ "$(grep -c . <<< "$found" || true)" -eq 1 ]; then
                        read -r status conclusion <<< "$found"
                    fi
                fi
                continue
            fi
        elif [ "$latest" -gt 0 ] && assignment "$names" "$latest" "$levels" > /dev/null; then
            die "No candidate holds item $builder, which builds $configuration's product: its last draw assigned it to none. No test failed: re-run all jobs to draw again."
        fi
        [ $((SECONDS - start)) -lt "$product_limit" ] || die "no product of $configuration in $product_limit s (its builder: item $builder, ${holder:-not yet assigned})"
        sleep "$((pause > product_poll ? pause : product_poll))"
    done
}

# The local charged-request count, separately from the quota deltas shared with other runs. The two samples are
# ordinary metadata requests and are inside the bound, not extra rate_limit queries hidden from the accounting.
rest_report()
{
    local records=$1 ledger requests total=0 bound=$((10 + 2 * (${#items[@]} - ${#configurations[@]})))
    local before='' after='' first_reset='' last_reset='' directory name expected missing=0
    printf '\n### Repository REST requests (attempt %s)\n\n' "$attempt"
    printf '| Caller | Requests |\n| --- | ---: |\n'
    for ledger in "$records"/"$prefix-$attempt-rest-"*/*.tsv "$rest_ledger"; do
        [ -f "$ledger" ] || continue
        requests=$(awk -F '\t' 'NR == 1 { if ($0 != "request\tstatus\tremaining\treset\tendpoint") exit 1; next }
            NF != 5 || $1 !~ /^[0-9]+$/ || $1 != NR - 1 { exit 1 } END { if (NR < 1) exit 1; print NR - 1 }' "$ledger") || die "invalid REST ledger: $ledger"
        total=$((total + requests))
        directory=${ledger%/*}
        printf '| %s/%s | %s |\n' "${directory##*/}" "${ledger##*/}" "$requests"
    done
    # Every candidate that recorded its decision and every collector that published an assignment must retain its
    # ledger. Failed commands can also have ledgers without a completed draw, and are included by the loop above.
    for directory in "$records"/"$prefix-$attempt-draw-"* "$records"/"$prefix-$attempt-assign-"*; do
        [ -d "$directory" ] || continue
        name=${directory##*/}
        if [[ "$name" == "$prefix-$attempt-draw-"* ]]; then
            expected="$records/$prefix-$attempt-rest-draw-${name#"$prefix-$attempt-draw-"}/take.tsv"
        else
            name=${name#"$prefix-$attempt-assign-"}
            expected="$records/$prefix-$attempt-rest-collect-${name%%-*}/assign.tsv"
        fi
        if [ ! -f "$expected" ]; then
            printf '\nMissing request ledger: `%s`.\n' "${expected#"$records"/}"
            missing=$((missing + 1))
        fi
    done
    for directory in "$records"/"$prefix-$attempt-evidence-"*; do
        [ -f "$directory/record" ] || continue
        if [[ "$(field product "$directory/record")" == installed\ * ]]; then
            expected="$records/$prefix-$attempt-rest-draw-$(field level "$directory/record")-$(field candidate "$directory/record")/product.tsv"
            if [ ! -f "$expected" ]; then
                printf '\nMissing product request ledger: `%s`.\n' "${expected#"$records"/}"
                missing=$((missing + 1))
            fi
        fi
    done
    ledger="$records/$prefix-$attempt-rest-collect-1/assign.tsv"
    if [ -f "$ledger" ]; then
        before=$(awk -F '\t' 'NR == 2 { print $3 }' "$ledger")
        first_reset=$(awk -F '\t' 'NR == 2 { print $4 }' "$ledger")
    fi
    after=$(awk -F '\t' 'NR == 2 { print $3 }' "$rest_ledger")
    last_reset=$(awk -F '\t' 'NR == 2 { print $4 }' "$rest_ledger")
    printf '\nCounted **%s REST requests** from this attempt. ' "$total"
    if [ "$attempt" -eq 1 ]; then
        printf 'Hard first-attempt ceiling: **%s** (%s at 30 runs/hour).\n' "$bound" "$((30 * bound))"
    else
        printf 'Rerun identity reads have a separate limit of six per command (seven for collector 1 including its sample).\n'
    fi
    printf '\nShared quota remaining: %s before / %s after; reset epochs: %s / %s.\n' "${before:-unavailable}" "${after:-unavailable}" "${first_reset:-unavailable}" "${last_reset:-unavailable}"
    if [[ "$before" =~ ^[0-9]+$ && "$after" =~ ^[0-9]+$ ]] && [ "$first_reset" = "$last_reset" ]; then
        printf 'Observed shared-quota delta: %s. This includes concurrent workflows and excludes the first sample itself.\n' "$((before - after))"
    else
        printf 'No comparable header delta: a sample is unavailable or the quota window changed.\n'
    fi
    [ "$missing" -eq 0 ] || die "REST accounting is incomplete: $missing request ledgers are missing"
    [ "$attempt" -ne 1 ] || [ "$total" -le "$bound" ] || die "REST request ceiling exceeded: $total > $bound"
}

verdict()
{
    local records=$1 latest at name names level holder holders evidence passing item report product built configuration incomplete=() failed=()
    test -d "$records" || die "verdict: no directory $records"
    rest_get "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt" > /dev/null || true
    names=$(find "$records" -mindepth 1 -maxdepth 1 -printf '%f\n')
    latest=$(assigning_attempt "$names")
    holders=''
    if [ "$latest" -gt 0 ]; then
        for level in 1 2 3 4 5 6 7 8 9; do
            holders+=$(assignment "$names" "$latest" "$level" | sed "s/\$/ $level/" || true)
            holders+=$'\n'
        done
    fi
    report="${RUNNER_TEMP:-/tmp}/vector-verdict.md"
    {
        printf '## Vector row (%s, O%s), attempt %s\n\n' "${configurations[*]}" "$optimization" "$attempt"
        printf 'Assignments from attempt %s.\n\n' "$latest"
        printf '| Attempt | Draw | Candidate | CPU | Probe | Decision |\n| ---: | ---: | ---: | --- | --- | --- |\n'
        for name in "$records"/"$prefix"-*-draw-*/record; do
            [ -e "$name" ] || continue
            at=${name#"$records"/"$prefix"-}
            at=${at%%-*}
            [ "$at" -ge "$latest" ] || continue
            printf '| %s | %s | %s | %s | %s | %s |\n' "$at" "$(field level "$name")" "$(field candidate "$name")" \
                "$(field cpu "$name")" "$(field probe "$name")" "$(field decision "$name")"
        done | sort -t '|' -k 2,2n -k 3,3n -k 4,4n
    } > "$report"
    rest_report "$records" >> "$report"
    {
        printf '\n### Item results\n\n'
        printf '| Item | Configuration | Shard | Draw | Candidate | Attempt | CPU | Product | Result |\n'
        printf '| --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- |\n'
    } >> "$report"
    for item in "${items[@]}"; do
        configuration=$(item_configuration "$item")
        holder=$(awk -v item="$item" '$1 == item { print $3, $2 }' <<< "$holders")
        if [ -z "$holder" ]; then
            incomplete+=("$item")
            printf '| %s | %s | %s | | | | | | no qualifying CPU drawn |\n' "$item" "$configuration" "${item:1}" >> "$report"
            continue
        fi
        if [ "$(wc -l <<< "$holder")" -ne 1 ]; then
            failed+=("$item")
            printf '| %s | %s | %s | | | | | | assigned %s times |\n' "$item" "$configuration" "${item:1}" "$(wc -l <<< "$holder")" >> "$report"
            continue
        fi
        # The holder's passing executions in the assignments' attempt and the reruns after it: item 1 of a configuration
        # with the product it built, the others with one they installed.
        built=installed
        [ "${item:1}" != 1 ] || built=built
        passing=0
        evidence=''
        for ((at = latest; at <= attempt; at += 1)); do
            name="$records/$prefix-$at-evidence-$item/record"
            if [ -e "$name" ] && [ "$(field level "$name") $(field candidate "$name")" = "$holder" ] &&
                [ "$(field probe "$name")" = qualified ] && [ "$(field host-vector "$name")" = 1 ] &&
                [ -n "$(field kernels "$name")" ] && [ "$(field suite "$name")" = passed ] && [ "$(field item "$name")" = "$item" ] &&
                [ "$(field configuration "$name")" = "$configuration" ] && [ "$(field shard "$name")" = "${item:1}" ] &&
                [[ "$(field product "$name")" =~ ^$built\ [0-9a-f]{64}$ ]] &&
                [[ "$(field decision "$name")" == "took $item"* ]]; then
                passing=$((passing + 1))
                evidence=$name
            fi
        done
        if [ "$passing" -eq 1 ]; then
            product=$(field product "$evidence")
            printf '| %s | %s | %s | %s | %s | %s | %s | %s %.12s | passed |\n' "$item" "$configuration" "${item:1}" "${holder% *}" "${holder#* }" \
                "$(evidence_attempt "$evidence")" "$(field cpu "$evidence")" "${product%% *}" "${product#* }" >> "$report"
        else
            failed+=("$item")
            printf '| %s | %s | %s | %s | %s | | | | %s passing executions |\n' "$item" "$configuration" "${item:1}" "${holder% *}" "${holder#* }" "$passing" >> "$report"
        fi
    done
    cat "$report"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        cat "$report" >> "$GITHUB_STEP_SUMMARY"
    fi
    if [ "${#failed[@]}" -ne 0 ]; then
        printf '::error title=Vector row::Item(s) %s have no single passing, complete execution by their holder: see its jobs.\n' "${failed[*]}"
    fi
    if [ "${#incomplete[@]}" -ne 0 ]; then
        printf '::error title=Vector row incomplete::incomplete: no qualifying CPU drawn for item(s) %s. No test failed: re-run all jobs to draw again.\n' "${incomplete[*]}"
    fi
    [ "${#failed[@]}" -eq 0 ] && [ "${#incomplete[@]}" -eq 0 ]
}

# A record's field $1 in the file $2.
field()
{
    sed -n "s/^$1\t//p" "$2" | head -n 1
}

# The attempt in an evidence record's artifact directory's name.
evidence_attempt()
{
    local at=${1%/record}
    at=${at##*/"$prefix"-}
    printf '%s\n' "${at%%-*}"
}

case "${1:-}" in
    assign)
        [ $# -eq 4 ] || { echo 'usage: vector-draw.sh assign LEVEL CANDIDATES NEXT' >&2; exit 2; }
        assign "$2" "$3" "$4"
        ;;
    take)
        [ $# -eq 5 ] || { echo 'usage: vector-draw.sh take LEVEL CANDIDATE VERDICT RECORD' >&2; exit 2; }
        take "$2" "$3" "$4" "$5"
        ;;
    product)
        [ $# -eq 2 ] || { echo 'usage: vector-draw.sh product CONFIGURATION' >&2; exit 2; }
        product "$2"
        ;;
    verdict)
        [ $# -eq 2 ] || { echo 'usage: vector-draw.sh verdict RECORDS' >&2; exit 2; }
        verdict "$2"
        ;;
    *)
        echo 'usage: vector-draw.sh assign LEVEL CANDIDATES NEXT | take LEVEL CANDIDATE VERDICT RECORD | product CONFIGURATION | verdict RECORDS' >&2
        exit 2
        ;;
esac
