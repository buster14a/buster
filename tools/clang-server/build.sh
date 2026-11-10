#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Builds the clang server (clang_server.cpp, #2071) against the libraries of the clang on PATH, whose release llvm-config
# must be: the server's runs stand for that clang's. bash tools/clang-server/build.sh [OUTPUT], by default
# build/clang-server/clang-server; CXX and LLVM_CONFIG choose the tools.
#
# The server must hold one copy of LLVM. Where LLVM is a shared library (llvm-config --shared-mode: a distribution's),
# libclang-cpp links to it, and so does the server. Where it is static (the official release), libclang-cpp bundles
# LLVM without exporting the targets' initializers, and LLVM's static archives would bring a second copy of its core:
# two copies' globals, constructed and destroyed twice ("double free" at exit, run 42911952930). There the server links
# libclang-cpp alone if that suffices, and otherwise exits with status 77, which a caller reads as no server for this
# toolchain. Any other status is a failure.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
output=${1:-"$root/build/clang-server/clang-server"}
compiler=${CXX:-clang++}
config=${LLVM_CONFIG:-llvm-config}
version=$(clang -dumpversion)
if [ "$("$config" --version)" != "$version" ]; then
    echo "clang-server: $config is LLVM $("$config" --version), clang is $version" >&2
    exit 1
fi
words()
{
    local line
    local part
    while IFS= read -r line; do
        read -r -a part <<< "$line"
        printf '%s\n' "${part[@]}"
    done <<< "$1"
}
mapfile -t flags < <(words "$("$config" --cxxflags --ldflags)")
mapfile -t system < <(words "$("$config" --system-libs)")
libraries=()
if [ "$("$config" --shared-mode)" = shared ]; then
    mapfile -t libraries < <(words "$("$config" --libs)")
fi
mkdir -p "$(dirname "$output")"
log=$(mktemp)
trap 'rm -f "$log"' EXIT
status=0
"$compiler" -O2 -Wall -Werror "$root/tools/clang-server/clang_server.cpp" "${flags[@]}" -lclang-cpp "${libraries[@]}" "${system[@]}" -o "$output" 2> "$log" || status=$?
if [ "$status" -ne 0 ] && [ "${#libraries[@]}" -eq 0 ] && grep -qE 'undefined (reference|symbol)' "$log"; then
    echo "clang-server: this toolchain's libclang-cpp bundles LLVM without what the server needs (static LLVM): no server" >&2
    grep -m 3 -E 'undefined (reference|symbol)' "$log" >&2
    exit 77
fi
cat "$log" >&2
[ "$status" -eq 0 ]
test "$("$output" --version)" = "$version"
