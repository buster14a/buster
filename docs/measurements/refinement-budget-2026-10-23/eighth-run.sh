#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 compact spans and record prefetch screen'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
"$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 \
    -I"$root/fifth-overlays/span32" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
    "$root/fifth-main.c" -o "$root/eighth-span32"
sha256sum "$root/eighth-span32"
for executable in "$root/eighth-span32" "$root/fifth-earlyrecords"; do
    test -x "$executable"
    "$lock" taskset -c 2 "$executable" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
done
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" \
    "$root/fifth-chunksnopref" "$root/seventh-window128" "$root/fifth-earlyrecords" "$root/eighth-span32" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
