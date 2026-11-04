#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Stage the CMake and Ninja that build.sh drives (docs/commands/build.md) under PARENT, in a directory named by their
# versions, from their official release archives verified by SHA-256, and print its bin directory, which holds cmake,
# ctest and ninja. A directory that already holds them is reused; staging is serialized on a lock beside it, and a
# failed one is never used.
set -euo pipefail
[ $# -eq 1 ] || { echo "usage: setup-build-tools.sh PARENT" >&2; exit 2; }
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
cmake_version=4.4.3
# Kitware's cmake-4.4.3-SHA-256.txt for the release asset.
cmake_digest=d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc
ninja_version=1.13.2
# The release asset ninja-linux.zip, which has no published digest; recorded when it was pinned.
ninja_digest=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6
destination="$1/cmake-$cmake_version-ninja-$ninja_version"
expected="cmake $cmake_version $cmake_digest, ninja $ninja_version $ninja_digest"
mkdir -p "$(dirname "$destination")"
exec 9> "$destination.lock"
flock 9
if [ ! -f "$destination/.complete" ] || [ "$(cat "$destination/.complete")" != "$expected" ]; then
    temporary=$(mktemp -d "$destination.XXXXXXXX")
    trap 'rm -rf "$temporary"' EXIT
    archive="cmake-$cmake_version-linux-x86_64"
    curl --fail --location --retry 3 --silent --show-error \
        "https://github.com/Kitware/CMake/releases/download/v$cmake_version/$archive.tar.gz" \
        --output "$temporary/cmake.tar.gz"
    printf '%s  %s\n' "$cmake_digest" "$temporary/cmake.tar.gz" | sha256sum --check --status
    mkdir "$temporary/tree"
    # The programs, their modules and the license; not the GUI or the documentation.
    tar -xzf "$temporary/cmake.tar.gz" -C "$temporary/tree" --strip-components=1 --no-same-owner \
        "$archive/bin/cmake" "$archive/bin/ctest" "$archive/share/cmake-${cmake_version%.*}" \
        "$archive/doc/cmake/LICENSE.rst"
    rm "$temporary/cmake.tar.gz"
    curl --fail --location --retry 3 --silent --show-error \
        "https://github.com/ninja-build/ninja/releases/download/v$ninja_version/ninja-linux.zip" \
        --output "$temporary/ninja.zip"
    printf '%s  %s\n' "$ninja_digest" "$temporary/ninja.zip" | sha256sum --check --status
    # The CMake just staged extracts it: nothing else this needs may be on the host (unzip, Python).
    (cd "$temporary/tree/bin" && ../bin/cmake -E tar xf "$temporary/ninja.zip")
    chmod 755 "$temporary/tree/bin/ninja"
    rm "$temporary/ninja.zip"
    test "$("$temporary/tree/bin/cmake" --version | head -n 1)" = "cmake version $cmake_version"
    test "$("$temporary/tree/bin/ctest" --version | head -n 1)" = "ctest version $cmake_version"
    test "$("$temporary/tree/bin/ninja" --version)" = "$ninja_version"
    printf '%s\n' "$expected" > "$temporary/tree/.complete"
    if [ -e "$destination" ]; then rm -rf "$destination"; fi
    mv "$temporary/tree" "$destination"
    rm -rf "$temporary"
    trap - EXIT
fi
test "$("$destination/bin/cmake" --version | head -n 1)" = "cmake version $cmake_version"
test "$("$destination/bin/ninja" --version)" = "$ninja_version"
printf '%s\n' "$destination/bin"
