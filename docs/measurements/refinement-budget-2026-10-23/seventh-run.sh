#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 seventh screen'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
variants=(window64 window128 block512 block2048 batch32 ahead1 prefix28hot)
executables=("$root/fifth-chunksnopref")
for variant in "${variants[@]}"; do
    "$lock" bash "$root/seventh-build.sh" "$variant"
    executables+=("$root/seventh-$variant")
done
for variant in "${variants[@]}"; do
    printf 'Test: %s\n' "$variant"
    "$lock" taskset -c 2 "$root/seventh-$variant" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
done
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "${executables[@]}" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/fifth-maskprobe" "$root/seventh-prefix28hot" \
    -- --setup "$root/inputs/sqlite3.c"
