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
#       SHA-256 to GITHUB_OUTPUT (`name`, `digest`). Polls every 10 s. Fails, without waiting further, when no candidate
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
# Each poll lists the run's artifacts (and a collector's, its jobs) through the REST API, whose hourly limit the whole
# repository shares: polls every 5 s exhausted it with many pull requests open (#2309). A failed listing (the limit,
# for one) doubles the pause, up to poll_limit, until a listing succeeds again.
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
# An item polls for its product less often than a draw for its assignment: it waits for a build, minutes, and a full
# run's twelve waiting items share the repository's API rate. A failed listing backs off as a draw's does.
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
    gh api --paginate "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/artifacts?per_page=100" --jq '.artifacts[].name'
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
# those it reran with it, the others with the attempt they last ran in.
attempt_jobs()
{
    gh api --paginate "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt/jobs?per_page=100" \
        --jq '.jobs[] | "\(.run_attempt)\t\(.status)\t\(if (.conclusion // "") == "" then "none" else .conclusion end)\t\(.name)"'
}

# Whether draw $1's collector runs in this attempt: always in the first; in a later one, only when the attempt reran
# every job, not the failed ones alone. A job the attempt didn't rerun lists with its own earlier attempt.
collector_runs()
{
    local level=$1 jobs tries
    [ "$attempt" -gt 1 ] || return 0
    for tries in 1 2 3; do
        if jobs=$(gh api --paginate "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt/jobs?per_page=100" \
            --jq ".jobs[] | select(.run_attempt == $attempt) | .name"); then
            grep -qF "Vector draw $level's assignment $row" <<< "$jobs" && return 0
            # A collector the attempt runs is listed from its start, with its draw's candidates; look twice more.
        fi
        [ "$tries" -eq 3 ] || sleep "$poll"
    done
    return 1
}

# The numbers of draw $1's candidates in the jobs of this attempt that have ended (a candidate that couldn't publish its
# verdict still decides nothing).
ended_candidates()
{
    local level=$1
    gh api --paginate "repos/${GITHUB_REPOSITORY:?}/actions/runs/${GITHUB_RUN_ID:?}/attempts/$attempt/jobs?per_page=100" \
        --jq ".jobs[] | select(.status == \"completed\") | .name" |
        sed -n "s/.*Vector draw $level, candidate \([0-9][0-9]*\) ${row//[()]/[()]}\$/\1/p"
}

assign()
{
    local level=$1 candidates=$2 next=$3 start=$SECONDS polls=0 names k item pair taken decided count i list='' name
    local -a missing qualified pairs
    [[ "$level" =~ ^[1-9]$ && "$candidates" =~ ^[0-9]+$ && "$next" =~ ^[0-9]+$ ]] || die "assign: bad arguments: $*"
    [ "$level" -ne 1 ] || [ "$candidates" -ge $((3 * ${#items[@]})) ] ||
        die "the first draw has $candidates candidates for ${#items[@]} items: at least 3 per item (${items[*]})"
    : "${GITHUB_OUTPUT:?}" "${RUNNER_TEMP:?}"
    while true; do
        polls=$((polls + 1))
        if names=$(artifact_names); then
            next_pause 0
        else
            names=''
            next_pause 1
        fi
        # The items no earlier draw of this attempt assigned, the costliest first.
        taken=''
        for ((k = 1; k < level; k += 1)); do
            taken+=" $(assignment "$names" "$attempt" "$k" | cut -d ' ' -f 1 | tr '\n' ' ' || true)"
        done
        missing=()
        for item in "${items[@]}"; do
            if ! grep -qw -- "$item" <<< "$taken"; then
                missing+=("$item")
            fi
        done
        mapfile -t qualified < <(sed -n "s/^$prefix-$attempt-probe-$level-\([0-9][0-9]*\)-yes\$/\1/p" <<< "$names" | sort -n -u)
        decided=$( { sed -n "s/^$prefix-$attempt-probe-$level-\([0-9][0-9]*\)-\(yes\|no\)\$/\1/p" <<< "$names"
            ended_candidates "$level" || true; } | sort -n -u | grep -c . || true)
        if [ "${#qualified[@]}" -ge "${#missing[@]}" ] || [ "$decided" -ge "$candidates" ]; then
            break
        fi
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
    local level=$1 candidate=$2 verdict=$3 record=$4 start=$SECONDS item='' decision held names at found configuration shard shards
    [[ "$level" =~ ^[1-9]$ && "$candidate" =~ ^[1-9][0-9]*$ ]] || die "take: bad arguments: $*"
    test -f "$record" || die "take: no record $record"
    if [ "$verdict" != qualified ] && [ "$attempt" -eq 1 ]; then
        decision='redraw: the CPU does not qualify'
    elif collector_runs "$level"; then
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
    local configuration=$1 builder start=$SECONDS polls=0 names latest holder level job jobs run_attempt status conclusion name found
    [ -n "$configuration" ] && [ "$(item_configuration "${configuration:0:1}1")" = "$configuration" ] || die "product: no configuration $configuration in ${configurations[*]}"
    builder="${configuration:0:1}1"
    : "${GITHUB_OUTPUT:?}"
    while true; do
        polls=$((polls + 1))
        if names=$(artifact_names); then
            next_pause 0
        else
            names=''
            next_pause 1
        fi
        latest=$(assigning_attempt "$names")
        holder=''
        for ((level = 1; level <= levels; level += 1)); do
            if [ -z "$holder" ]; then
                holder=$( { assignment "$names" "$latest" "$level" || true; } | awk -v item="$builder" -v level="$level" '$1 == item { print level, $2 }')
            fi
        done
        if [ -n "$holder" ]; then
            # The attempt the holder last ran in, as this attempt's jobs list it, whose product it is: the first's in the
            # first attempt, whose jobs are read every 6th poll alone, for the holder's end; a rerun's, every poll.
            job="Vector draw ${holder% *}, candidate ${holder#* } $row"
            run_attempt=1
            status=unknown
            conclusion=unknown
            if [ "$attempt" -gt 1 ] || [ $((polls % 6)) -eq 1 ]; then
                jobs=$(attempt_jobs) || jobs=''
                found=''
                while IFS=$'\t' read -r run_attempt status conclusion name; do
                    if [ "$name" = "$job" ] || [[ "$name" == *" / $job" ]]; then
                        found+="$run_attempt $status $conclusion"$'\n'
                    fi
                done <<< "$jobs"
                run_attempt=0
                if [ "$(grep -c . <<< "$found" || true)" -eq 1 ]; then
                    read -r run_attempt status conclusion <<< "$found"
                    # Its uploads end before it does: list them again after its status.
                    names=$(artifact_names) || names=''
                fi
            fi
            found=$(grep -E "^$product_prefix-$run_attempt-$configuration-[0-9a-f]{64}\$" <<< "$names" || true)
            [ "$(grep -c . <<< "$found" || true)" -le 1 ] || die "attempt $run_attempt has $(grep -c . <<< "$found") products of $configuration: $(tr '\n' ' ' <<< "$found")"
            if [ "$run_attempt" -gt 0 ] && [ -n "$found" ]; then
                printf 'name=%s\ndigest=%s\n' "$found" "${found##*-}" >> "$GITHUB_OUTPUT"
                summary "the product of $configuration: $found, built by draw ${holder% *}'s candidate ${holder#* } in attempt $run_attempt, after $((SECONDS - start)) s ($polls polls)"
                return 0
            fi
            [ "$status" != completed ] || die "The holder of item $builder (draw ${holder% *}, candidate ${holder#* }) ended ($conclusion) in attempt $run_attempt without publishing $configuration's product, which this item installs: see its job. Rerun the failed jobs: it reruns first."
        elif [ "$latest" -gt 0 ] && assignment "$names" "$latest" "$levels" > /dev/null; then
            die "No candidate holds item $builder, which builds $configuration's product: its last draw assigned it to none. No test failed: re-run all jobs to draw again."
        fi
        [ $((SECONDS - start)) -lt "$product_limit" ] || die "no product of $configuration in $product_limit s (its builder: item $builder, ${holder:-not yet assigned})"
        sleep "$((pause > product_poll ? pause : product_poll))"
    done
}

verdict()
{
    local records=$1 latest at name names level holder holders evidence passing item report product built configuration incomplete=() failed=()
    test -d "$records" || die "verdict: no directory $records"
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
        printf '\n| Item | Configuration | Shard | Draw | Candidate | Attempt | CPU | Product | Result |\n'
        printf '| --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- |\n'
    } > "$report"
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
