#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# #109's qualification: test-preprocessor --clang in slices of 200,000 cases per mode, 1,000,000 per seed.
# Adapted from the Codex handoff's campaign.sh: same strict summary regex and per-slice receipts, plus a --fuzz 0
# pre-screen of each seed's other --clang checks, resume from completed receipts, the pragma oracle, and SCHED_IDLE
# locally. Usage: campaign.sh run local|9700 ROOT COMPILER ORACLE FIRST_SEED LAST_SEED
set -u
action=$1
shift
if [[ $action == slice ]]; then
    compiler=$1
    directory=$2
    jobs=$3
    seed=$4
    first=$5
    count=$6
    idle=$7
    cd "$directory" || exit 1
    start=$SECONDS
    launch=()
    if [[ $idle == 1 ]]; then
        launch=(chrt --idle 0 ionice -c 3 nice -n 19)
    fi
    "${launch[@]}" "$compiler" test-preprocessor --clang --fuzz "$count" --first "$first" --seed "$seed" --jobs "$jobs" > out.txt 2> err.txt
    status=$?
    printf '%s\n' "$status" > exit.txt
    printf '%s\n' "$((SECONDS - start))" > held-seconds.txt
    exit "$status"
fi
if [[ $action != run || $# != 6 ]]; then
    echo 'usage: campaign.sh run local|9700 ROOT COMPILER ORACLE FIRST_SEED LAST_SEED' >&2
    exit 2
fi
host=$1
root=$2
compiler=$3
oracle=$4
first_seed=$5
last_seed=$6
script=$(realpath "$0")
mkdir -p "$root/tmp" || exit 1
export TMPDIR=$root/tmp
export BUSTER_PPF_PRAGMA_ORACLE=$oracle
export EXCLUSIVE_OWNER=claude-109-qualification
export EXCLUSIVE_TITLE='Issue 109: final differential qualification, 200K-per-mode slices'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/109
if [[ $host == local ]]; then
    jobs=8
    idle=1
else
    jobs=15
    idle=0
fi
run_slice()
{
    if [[ $host == local ]]; then
        bash "$script" slice "$@" "$idle"
    else
        "$HOME/buster-fast-bench/exclusive.sh" bash "$script" slice "$@" "$idle"
    fi
}
pattern="^fuzz: (c17|c23), seed ([0-9]+): ([0-9]+) cases, ([0-9]+) accepted; the kernels differ on 0; ([0-9]+) compared with clang \\([0-9]+ with clang's tokens, 0 unverified\\), 0 differ \\([0-9]+ read alone\\), 0 batches differ from the case alone, [0-9]+ batches read case by case; 0 without a result; [0-9]+ whose text can't lex back, as clang's can't$"
count=200000
total=0
for ((seed=first_seed; seed<=last_seed; seed++)); do
    screen=$root/screen_${seed}
    if [[ ! -e $screen/exit.txt || $(cat "$screen/exit.txt") != 0 ]]; then
        rm -rf "$screen"
        mkdir "$screen" || exit 1
        printf '%s SCREEN seed=%s\n' "$(date -Is)" "$seed" >> "$root/campaign.log"
        run_slice "$compiler" "$screen" "$jobs" "$seed" 0 0
        if [[ $(cat "$screen/exit.txt") != 0 ]]; then
            printf '%s SCREEN-FAILED seed=%s\n' "$(date -Is)" "$seed" >> "$root/campaign.log"
            echo "SCREEN-FAILED seed=$seed" > "$root/STATE"
            exit 1
        fi
    fi
    for ((first=0; first<1000000; first+=count)); do
        directory=$root/seed_${seed}_${first}
        if grep -q "^${seed}	${first}	${count}	" "$root/receipts.tsv" 2>/dev/null; then
            ((total+=count))
            continue
        fi
        if [[ -e $root/STOP ]]; then
            printf '%s STOPPED after %s cases per mode\n' "$(date -Is)" "$total" >> "$root/campaign.log"
            echo STOPPED > "$root/STATE"
            exit 0
        fi
        rm -rf "$directory"
        mkdir "$directory" || exit 1
        printf '%s RUNNING seed=%s first=%s count=%s\n' "$(date -Is)" "$seed" "$first" "$count" >> "$root/campaign.log"
        echo "RUNNING seed=$seed first=$first total-per-mode=$total" > "$root/STATE"
        run_slice "$compiler" "$directory" "$jobs" "$seed" "$first" "$count"
        status=$(cat "$directory/exit.txt" 2>/dev/null || echo missing)
        seen17=0
        seen23=0
        valid=1
        while IFS= read -r line; do
            if [[ $line == 'fuzz: c17, seed '* || $line == 'fuzz: c23, seed '* ]]; then
                printf '%s\n' "$line" >> "$root/campaign.log"
                if [[ $line =~ $pattern && ${BASH_REMATCH[2]} == "$seed" && ${BASH_REMATCH[3]} == "$count" && ${BASH_REMATCH[5]} == "$count" ]]; then
                    if [[ ${BASH_REMATCH[1]} == c17 ]]; then
                        ((seen17+=1))
                    else
                        ((seen23+=1))
                    fi
                else
                    valid=0
                fi
            fi
        done < "$directory/out.txt"
        if [[ $status != 0 || $valid != 1 || $seen17 != 1 || $seen23 != 1 ]]; then
            printf '%s FAILED seed=%s first=%s exit=%s summaries=%s,%s valid=%s\n' "$(date -Is)" "$seed" "$first" "$status" "$seen17" "$seen23" "$valid" >> "$root/campaign.log"
            echo "FAILED seed=$seed first=$first exit=$status" > "$root/STATE"
            exit 1
        fi
        ((total+=count))
        printf '%s PASS seed=%s first=%s held-seconds=%s total-per-mode=%s\n' "$(date -Is)" "$seed" "$first" "$(cat "$directory/held-seconds.txt")" "$total" >> "$root/campaign.log"
        printf '%s\t%s\t%s\t%s\t%s\n' "$seed" "$first" "$count" "$(cat "$directory/held-seconds.txt")" "$total" >> "$root/receipts.tsv"
        if [[ $host == 9700 ]]; then
            sleep 60
        fi
    done
done
printf '%s COMPLETE %s cases per mode\n' "$(date -Is)" "$total" >> "$root/campaign.log"
echo "COMPLETE total-per-mode=$total" > "$root/STATE"
