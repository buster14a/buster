#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Audits a #109 campaign root from its slices' own outputs, independently of the runner's ledger: every seed's screen
# exited 0, and each of the 5 slices per seed (first 0, 200000, ..., 800000) exited 0 with exactly one strict summary per
# mode naming the seed, 200,000 cases and 200,000 compared with clang, 0 unverified, 0 differ, 0 without a result.
# Usage: audit.sh ROOT FIRST_SEED LAST_SEED
set -u
root=$1
first_seed=$2
last_seed=$3
pattern="^fuzz: (c17|c23), seed ([0-9]+): 200000 cases, [0-9]+ accepted; the kernels differ on 0; 200000 compared with clang \\([0-9]+ with clang's tokens, 0 unverified\\), 0 differ \\([0-9]+ read alone\\), 0 batches differ from the case alone, [0-9]+ batches read case by case; 0 without a result; [0-9]+ whose text can't lex back, as clang's can't$"
bad=0
slices=0
for ((seed=first_seed; seed<=last_seed; seed++)); do
    if [[ $(cat "$root/screen_$seed/exit.txt" 2>/dev/null) != 0 ]]; then
        echo "seed $seed: screen did not exit 0"
        bad=1
    fi
    for ((first=0; first<1000000; first+=200000)); do
        directory=$root/seed_${seed}_${first}
        c17=0
        c23=0
        other=0
        while IFS= read -r line; do
            if [[ $line == 'fuzz: '* ]]; then
                if [[ $line =~ $pattern && ${BASH_REMATCH[2]} == "$seed" ]]; then
                    [[ ${BASH_REMATCH[1]} == c17 ]] && ((c17+=1)) || ((c23+=1))
                else
                    ((other+=1))
                fi
            fi
        done < "$directory/out.txt"
        if [[ $(cat "$directory/exit.txt" 2>/dev/null) != 0 || $c17 != 1 || $c23 != 1 || $other != 0 ]]; then
            echo "seed $seed first $first: exit=$(cat "$directory/exit.txt" 2>/dev/null) c17=$c17 c23=$c23 other=$other"
            bad=1
        else
            ((slices+=1))
        fi
    done
done
echo "audit: $slices verified slices, $((slices * 200000)) fully verified cases per mode in each of C17 and C23, seeds $first_seed-$last_seed"
[[ $bad == 0 && $slices == $(((last_seed - first_seed + 1) * 5)) ]] && echo 'audit: PASS' || { echo 'audit: FAIL'; exit 1; }
