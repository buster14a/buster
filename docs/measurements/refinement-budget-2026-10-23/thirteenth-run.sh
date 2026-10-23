#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 skip empty splice blocks'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
"$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 \
    -I"$root/fifth-overlays/blockgap" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
    "$root/fifth-main.c" -o "$root/thirteenth-blockgap"
sha256sum "$root/thirteenth-blockgap"
"$lock" taskset -c 2 "$root/thirteenth-blockgap" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
"$lock" clang -O2 -g -march=native -I"$root/fifth-overlays/blockgap" -Isrc -Isrc/buster/compiler \
    -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/chunk-prepare-boundaries.c" -o "$root/thirteenth-prepare-blockgap"
"$lock" taskset -c 2 "$root/thirteenth-prepare-blockgap" "$root/inputs/sqlite3.c" "$root/inputs/sqlite-new.c" src/buster/compiler/main.c
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/twelfth-precisearena" "$root/thirteenth-blockgap" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
