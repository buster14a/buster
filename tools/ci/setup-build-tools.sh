#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Stage the pinned CMake/Ninja under PARENT; hosted consumers read the verified
# handoff, while standalone/9700X callers retain bounded upstream acquisition.
set -euo pipefail
[ $# -eq 1 ] || { echo 'usage: setup-build-tools.sh PARENT' >&2; exit 2; }
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
release="$(dirname "$0")/hosted-release.sh"
cmake_version=$(bash "$release" version cmake)
cmake_digest=$(bash "$release" digest cmake)
ninja_version=$(bash "$release" version ninja)
ninja_digest=$(bash "$release" digest ninja)
destination="$1/cmake-$cmake_version-ninja-$ninja_version"
expected="cmake $cmake_version $cmake_digest, ninja $ninja_version $ninja_digest"
mkdir -p "$(dirname "$destination")"
exec 9> "$destination.lock"
flock 9
if [ ! -f "$destination/.complete" ] || [ "$(cat "$destination/.complete")" != "$expected" ]; then
    temporary=$(mktemp -d "$destination.XXXXXXXX")
    trap 'rm -rf "$temporary"' EXIT
    archive="cmake-$cmake_version-linux-x86_64"
    bash "$release" get cmake "$temporary/cmake.tar.gz"
    mkdir "$temporary/tree"
    tar -xzf "$temporary/cmake.tar.gz" -C "$temporary/tree" --strip-components=1 --no-same-owner \
        "$archive/bin/cmake" "$archive/bin/ctest" "$archive/share/cmake-${cmake_version%.*}" \
        "$archive/doc/cmake/LICENSE.rst"
    rm "$temporary/cmake.tar.gz"
    bash "$release" get ninja "$temporary/ninja.zip"
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
