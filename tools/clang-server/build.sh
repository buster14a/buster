#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Builds the clang server (clang_server.cpp, #2071) against the libraries of the clang on PATH, as
# tools/build-preprocessor-pragma-oracle.sh builds its adapter: llvm-config's flags and libclang-cpp, which the official
# release bundles LLVM into and a distribution's links to libLLVM. llvm-config must be that clang's release, since the
# server's runs stand for that clang's. bash tools/clang-server/build.sh [OUTPUT], by default build/clang-server/clang-server;
# CXX and LLVM_CONFIG choose the tools.
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
flags=()
configuration_flags=$("$config" --cxxflags --ldflags --libs --system-libs)
while IFS= read -r line; do
    read -r -a part <<< "$line"
    flags+=("${part[@]}")
done <<< "$configuration_flags"
mkdir -p "$(dirname "$output")"
"$compiler" -O2 -Wall -Werror "$root/tools/clang-server/clang_server.cpp" "${flags[@]}" -lclang-cpp -o "$output"
test "$("$output" --version)" = "$version"
