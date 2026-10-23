#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
cd "$root/fifth"
variant=${1:?variant}
clang -O3 -g -march=native -DBUSTER_REFINE_SOURCE_CHUNK=262144 -I"$root/fifth-overlays/$variant" -Isrc -Isrc/buster/compiler \
    -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror "$root/fifth-main.c" -o "$root/fifth-$variant"
sha256sum "$root/fifth-$variant"
