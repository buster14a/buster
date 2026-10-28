#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Stage the official release compiler in the runner's private cache. The job
# hook already holds the shared 9700X lock; do not acquire it again here.
set -euo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
version=23.1.4
archive=LLVM-${version}-Linux-X64
# GitHub's release asset digest, for the unmodified official zstd archive.
digest=efdd1dbd089b99bfcfcde7b8eb1236163f2e88df5a981cca4f783ec9cb5e0378
cache=${XDG_CACHE_HOME:-$HOME/.cache}/buster/benchpress-clang
destination="$cache/$digest"
mkdir -p "$cache"
# Serialize cache publication even if another consumer does not hold the host lock.
exec 9>"$cache/$digest.lock"
flock 9
if [ ! -f "$destination/.complete" ] || [ ! -f "$destination/LICENSE.TXT" ]; then
    temporary=$(mktemp -d "$cache/.clang-${version}.XXXXXXXX")
    trap 'rm -rf "$temporary"' EXIT
    curl --fail --location --retry 3 \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-${version}/${archive}.tar.zst" \
        --output "$temporary/archive.tar.zst"
    printf '%s  %s\n' "$digest" "$temporary/archive.tar.zst" | sha256sum --check
    mkdir "$temporary/tree"
    # The release uses a 1 GiB zstd window. Clang is a self-contained binary
    # linked only to ordinary host system libraries; its builtin headers are
    # required for intrinsics. Benchmark jobs do not need LLVM development files.
    tar -I 'zstd -d --long=30' --extract --file "$temporary/archive.tar.zst" \
        --directory "$temporary/tree" --strip-components=1 --no-same-owner \
        "$archive/bin/clang-23" "$archive/bin/clang" "$archive/lib/clang/23/include"
    # The binary release omits the project license. Retain the LLVM license
    # from the exact release commit alongside the staged executable.
    curl --fail --location --retry 3 \
        https://raw.githubusercontent.com/llvm/llvm-project/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/llvm/LICENSE.TXT \
        --output "$temporary/tree/LICENSE.TXT"
    printf '%s  %s\n' 8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee \
        "$temporary/tree/LICENSE.TXT" | sha256sum --check
    ln -s clang "$temporary/tree/bin/cc"
    test "$("$temporary/tree/bin/clang" -dumpversion)" = "$version"
    test -f "$temporary/tree/lib/clang/23/include/immintrin.h"
    printf '%s\n' "$digest" > "$temporary/tree/.complete"
    # A failed earlier publication is incomplete and never used.
    if [ -e "$destination" ]; then rm -rf "$destination"; fi
    mv "$temporary/tree" "$destination"
    rm -rf "$temporary"
    trap - EXIT
fi
test "$(cat "$destination/.complete")" = "$digest"
printf '%s  %s\n' 8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee \
    "$destination/LICENSE.TXT" | sha256sum --check --status
test "$("$destination/bin/clang" -dumpversion)" = "$version"
test -f "$destination/lib/clang/23/include/immintrin.h"
"$destination/bin/clang" --version
printf '%s\n' "$destination/bin" >> "${GITHUB_PATH:?GITHUB_PATH is required}"
