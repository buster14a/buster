#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 hash finalization and bucket mapping'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
variants=(prefix28scaled foldxor hashflags)
executables=("$root/seventh-window128")
for variant in "${variants[@]}"; do
    "$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 \
        -I"$root/fifth-overlays/$variant" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
        "$root/fifth-main.c" -o "$root/tenth-$variant"
    sha256sum "$root/tenth-$variant"
    executables+=("$root/tenth-$variant")
done
for variant in "${variants[@]}"; do
    "$lock" taskset -c 2 "$root/tenth-$variant" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
done
"$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 -I"$root/fifth-overlays/prefix28scaled" \
    -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/files-test-main.c" -o "$root/tenth-files-scaled"
"$lock" taskset -c 2 "$root/tenth-files-scaled" test-files --seed 116 --id-order 19
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "${executables[@]}" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/seventh-window128" "$root/tenth-prefix28scaled" \
    -- --setup "$root/inputs/sqlite3.c"
