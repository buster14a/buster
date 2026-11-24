#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Append the drawn item's runtime evidence only after the target check and the lexer comparison both pass.
set -euo pipefail
source_file=${1:?expected lexer input}
draw=${2:?expected draw directory}
mapfile -d '' -t host_flags < <(./build.sh print-host-flags)
test "${#host_flags[@]}" -gt 0
clang "${host_flags[@]}" -Isrc -E -include src/buster/compiler/host_isa.h -x c /dev/null -o /dev/null
size=$(stat -c %s -- "$source_file")
test "$size" -gt 0
"$BUSTER_COMPILER" benchmark-lexer "$source_file" 2>&1 | tee "$draw/kernels"
grep -Fxq "1 files, $size bytes" "$draw/kernels"
kernels=$(grep -Fx 'corpus: 0 files differ, 0 files (0.00% of bytes) need the scalar lexer for a part' "$draw/kernels")
{
    printf 'target\t%s\n' "${host_flags[*]}"
    printf 'host-vector\t1\n'
    printf 'kernels\tbenchmark-lexer %s: %s\n' "${source_file##*/}" "${kernels#corpus: }"
} >> "$draw/record"
