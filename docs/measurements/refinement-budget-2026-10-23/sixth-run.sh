#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 sixth screen'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
for variant in probe4 chunks256 chunksnopref chunksl2; do
    "$lock" bash "$root/fifth-build.sh" "$variant"
done
for variant in packed packedloop hash8 probe4 chunks256 chunksnopref chunksl2; do
    printf 'Test: %s\n' "$variant"
    "$lock" taskset -c 2 "$root/fifth-$variant" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
done
date -u
sha256sum "$root"/fifth-{maskprobe,packed,packedloop,hash8,probe4,chunks256,chunksnopref,chunksl2}
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" \
    "$root"/fifth-{maskprobe,packed,packedloop,hash8,probe4,chunks256,chunksnopref,chunksl2} \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
