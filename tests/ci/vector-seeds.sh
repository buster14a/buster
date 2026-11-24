#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# A one-item draw on an already qualified CPU, with the real kernel evidence producer and aggregate.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
export BUSTER_HOST_ARCH=vector BUSTER_VECTOR_CONFIGURATIONS=none BUSTER_VECTOR_ITEMS=n1 BUSTER_SANITIZER_OPTIMIZATION=3
export GITHUB_RUN_ATTEMPT=1 GITHUB_RUN_ID=1 GITHUB_REPOSITORY=buster14a/buster RUNNER_TEMP="$work"
unset GITHUB_STEP_SUMMARY BUSTER_DRAW_ROW
export BUSTER_COMPILER="$work/compiler"
mapfile -d '' -t flags < <(./build.sh print-host-flags)
test "${#flags[@]}" -gt 0
clang -std=gnu17 -O2 -funsigned-char -fwrapv "${flags[@]}" -Isrc tests/ci/vector-seeds.c -o "$BUSTER_COMPILER"
digest=$(sha256sum "$BUSTER_COMPILER" | cut -d ' ' -f 1)
records="$work/records"
prefix="$records/vector-O3-1"
draw="$prefix-evidence-n1"
mkdir -p "$draw" "$prefix-assign-1-n1x1" "$prefix-rest-collect-1" "$work/bin"
printf 'request\tstatus\tremaining\treset\tendpoint\n' > "$prefix-rest-collect-1/assign.tsv"
# The verdict's quota sample is optional. Keep this fixture entirely offline.
printf '#!/bin/sh\nexit 1\n' > "$work/bin/gh"
chmod +x "$work/bin/gh"
export PATH="$work/bin:$PATH"
for seed in token tail; do
    # A short partial lane for '+', or one live byte after a complete lane for ';'.
    file="$work/x86_64_encoder.c"
    if [ "$seed" = tail ]; then printf '%64s;' '' > "$file"; else printf 'x+y;' > "$file"; fi
    for mutation in control "$seed"; do
        printf 'level\t1\ncandidate\t1\ndecision\ttook n1\nprobe\tqualified\ncpu\tfixture\n' > "$draw/record"
        printf 'suite\tpassed\nitem\tn1\nconfiguration\tnone\nshard\t1\nproduct\tbuilt %s\n' "$digest" >> "$draw/record"
        status=0
        BUSTER_TEST_VECTOR_SEED=$mutation bash tools/ci/vector-kernels.sh "$file" "$draw" > "$work/kernels.log" 2>&1 || status=$?
        result=0
        bash tools/ci/vector-draw.sh verdict "$records" > "$work/verdict.log" 2>&1 || result=$?
        if [ "$mutation" = control ]; then
            [ "$status" = 0 ] && [ "$result" = 0 ] || { cat "$work/kernels.log" "$work/verdict.log"; exit 1; }
        else
            [ "$status" = 1 ] && [ "$result" = 1 ] &&
                grep -Fq "$file: token " "$work/kernels.log" &&
                grep -Fq 'corpus: 1 files differ, 0 files (0.00% of bytes) need the scalar lexer for a part' "$work/kernels.log" &&
                grep -Fq 'Item(s) n1 have no single passing' "$work/verdict.log" ||
                { cat "$work/kernels.log" "$work/verdict.log"; exit 1; }
            printf 'Detected the %s seed in the vector lexer and failed assigned item n1.\n' "$mutation"
        fi
    done
done
