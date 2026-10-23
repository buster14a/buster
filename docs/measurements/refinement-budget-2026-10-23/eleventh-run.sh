#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 splice reuse and capacity screen'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
variants=(probe16 lexsplices prefix28compact compactsplices)
executables=("$root/seventh-window128" "$root/tenth-prefix28scaled")
for variant in "${variants[@]}"; do
    "$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 \
        -I"$root/fifth-overlays/$variant" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
        "$root/fifth-main.c" -o "$root/eleventh-$variant"
    sha256sum "$root/eleventh-$variant"
    executables+=("$root/eleventh-$variant")
done
for variant in "${variants[@]}"; do
    "$lock" taskset -c 2 "$root/eleventh-$variant" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
done
"$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 -I"$root/fifth-overlays/compactsplices" \
    -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/files-test-main.c" -o "$root/eleventh-files-compactsplices"
"$lock" taskset -c 2 "$root/eleventh-files-compactsplices" test-files --seed 116 --id-order 19
"$lock" clang -O2 -g -march=native -I"$root/fifth-overlays/compactsplices" -Isrc -Isrc/buster/compiler \
    -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/chunk-prepare-test.c" -o "$root/eleventh-prepare-compactsplices"
"$lock" taskset -c 2 "$root/eleventh-prepare-compactsplices" "$root/inputs/sqlite3.c" "$root/inputs/sqlite-new.c" src/buster/compiler/main.c
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "${executables[@]}" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/seventh-window128" "$root/eleventh-prefix28compact" \
    -- --setup "$root/inputs/sqlite3.c"
