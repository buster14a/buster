#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Builds #2071's prototype clang server (clang_server.cpp) against the libraries of the clang on PATH, whose release
# llvm-config must be: the server's runs are that clang's. bash tools/clang-server/build.sh [OUTPUT], by default
# build/clang-server/clang-server.
set -euo pipefail
output=${1:-build/clang-server/clang-server}
config=$(command -v llvm-config)
version=$(clang -dumpversion)
if [ "$("$config" --version)" != "$version" ]; then
    echo "clang-server: llvm-config is LLVM $("$config" --version), clang is $version" >&2
    exit 1
fi
mkdir -p "$(dirname "$output")"
read -r -a cxxflags <<< "$("$config" --cxxflags)"
read -r -a ldflags <<< "$("$config" --ldflags)"
read -r -a libraries <<< "$("$config" --libs)"
clang++ -std=c++17 -O2 "${cxxflags[@]}" "$(dirname "$0")/clang_server.cpp" -o "$output" "${ldflags[@]}" -lclang-cpp "${libraries[@]}"
