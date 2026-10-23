#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 lexer backslash handoff'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
"$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 \
    -I"$root/fifth-overlays/lexgap" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
    "$root/fifth-main.c" -o "$root/ninth-lexgap"
sha256sum "$root/ninth-lexgap"
"$lock" taskset -c 2 "$root/ninth-lexgap" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
"$lock" clang -O2 -g -march=native -I"$root/fifth-overlays/lexgap" -Isrc -Isrc/buster/compiler \
    -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/chunk-prepare-test.c" -o "$root/ninth-prepare-lexgap"
"$lock" taskset -c 2 "$root/ninth-prepare-lexgap" "$root/inputs/sqlite3.c" "$root/inputs/sqlite-new.c" src/buster/compiler/main.c
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/seventh-window128" "$root/ninth-lexgap" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
