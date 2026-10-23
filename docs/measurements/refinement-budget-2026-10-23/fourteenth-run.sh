#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
export EXCLUSIVE_OWNER=codex-116
export EXCLUSIVE_TITLE='Issue 116 direct whitespace storage and full-file prefetch'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/pull/334
lock=/home/david/buster-fast-bench/exclusive.sh
cd "$root/fifth"
executables=()
for variant in blockgap coldpref directspace directcold; do
    "$lock" clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=131072 \
        -I"$root/fifth-overlays/$variant" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
        "$root/fifth-main.c" -o "$root/fourteenth-$variant"
    sha256sum "$root/fourteenth-$variant"
    executables+=("$root/fourteenth-$variant")
done
for variant in blockgap coldpref directspace directcold; do
    "$lock" taskset -c 2 "$root/fourteenth-$variant" --test --seed 116 --count 2000 src "$root/inputs/sqlite3.c" /home/david/azp-bench/c-tokenizer/corpus/libc
done
"$lock" clang -O2 -g -march=native -I"$root/fifth-overlays/directcold" -Isrc -Isrc/buster/compiler \
    -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/chunk-prepare-boundaries.c" -o "$root/fourteenth-prepare-directcold"
"$lock" taskset -c 2 "$root/fourteenth-prepare-directcold" "$root/inputs/sqlite3.c" "$root/inputs/sqlite-new.c" src/buster/compiler/main.c
for variant in fixed coldpref; do
    "$lock" clang -O3 -g -march=native -I"$root/fifth-overlays/$variant" -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
        "$root/fifth/docs/measurements/refinement-budget-2026-10-23/refinement-main.c" -o "$root/fourteenth-kernel-$variant"
done
sha256sum --check "$root/evidence/sqlite-system-headers.sha256"
date -u
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "${executables[@]}" \
    -- --only vectorized "$root/inputs/sqlite3.c" \
    -- --only vectorized "$root/inputs/sqlite-new.c" \
    -- --only vectorized -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/fourteenth-blockgap" "$root/fourteenth-directcold" \
    -- --setup "$root/inputs/sqlite3.c"
"$root/base/build/compiler" ab --rounds 3 --cpu 2 --lock "$lock" "$root/fourteenth-kernel-fixed" "$root/fourteenth-kernel-coldpref" \
    -- --only refine_tokens_64 "$root/inputs/sqlite3.c" \
    -- --only refine_tokens_64 --after-scalar 100 "$root/inputs/sqlite3.c" \
    -- --only refine_tokens_64 /home/david/azp-bench/c-tokenizer/corpus/libc
sha256sum --check "$root/evidence/sqlite-system-headers.sha256"
