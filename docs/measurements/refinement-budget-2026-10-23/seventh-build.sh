#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
cd "$root/fifth"
variant=${1:?variant}
overlay=tune
window=262144
block=1024
batch=64
ahead=2
case "$variant" in
    window64) window=65536 ;;
    window128) window=131072 ;;
    block512) block=512 ;;
    block2048) block=2048 ;;
    batch32) batch=32 ;;
    ahead1) ahead=1 ;;
    prefix28hot) overlay=prefix28hot ;;
    *) exit 2 ;;
esac
clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK="$window" -DBUSTER_REFINE_BLOCK_TOKENS="$block" \
    -DBUSTER_REFINE_HOT_BATCH="$batch" -DBUSTER_REFINE_PREFETCH_BATCHES="$ahead" \
    -I"$root/fifth-overlays/$overlay" -Isrc -Isrc/buster/compiler -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror \
    "$root/fifth-main.c" -o "$root/seventh-$variant"
sha256sum "$root/seventh-$variant"
