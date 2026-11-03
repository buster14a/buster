#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The 9700X's external Clang pragma oracle at OUTPUT, built from tools/preprocessor_pragma_oracle.cpp against the
# staged official libclang-cpp, which includes its LLVM dependencies (avoiding llvm-config's Ubuntu-specific static
# system-library paths). With BUSTER_CI_CACHE, the executable is kept under a key of everything that makes it: the
# source's SHA-256, the compiler's resolved path (the staged release's directory, named by its archive's digest) and
# version, llvm-config's flags and the command. A hit is checked against the SHA-256 recorded at its publication and
# must answer --version; a mismatch or failure deletes the entry and builds afresh. Publication is atomic.
set -euo pipefail
output=${1:?usage: pragma-oracle-cache.sh OUTPUT}
root=$(cd "$(dirname "$0")/../.." && pwd)
source="$root/tools/preprocessor_pragma_oracle.cpp"
compiler=${CXX:-clang++}
config=${LLVM_CONFIG:-llvm-config}
flags=$("$config" --cxxflags --ldflags)
read -r -a flags_array <<< "${flags//$'\n'/ }"
command=("$compiler" -O2 -Wall -Werror "$source" "${flags_array[@]}" -lclang-cpp)
build() { "${command[@]}" -o "$1"; }
cache=${BUSTER_CI_CACHE:-}
if [ -z "$cache" ]; then
    build "$output"
else
    key=$({
        sha256sum < "$source"
        realpath "$(command -v "$compiler")"
        "$compiler" --version
        printf '%s\n' "$flags" "${command[*]//$source/SOURCE}"
    } | sha256sum | cut -c 1-64)
    directory="$cache/pragma-oracle"
    entry="$directory/$key"
    mkdir -p "$directory"
    exec 8> "$directory/$key.lock"
    flock 8
    hit=false
    if [ -f "$entry/oracle" ] && [ -f "$entry/oracle.sha256" ] && (cd "$entry" && sha256sum --check --status oracle.sha256) && "$entry/oracle" --version > /dev/null 2>&1; then
        hit=true
        printf 'pragma-oracle-cache.sh: reusing %s\n' "$key"
    fi
    if [ "$hit" = false ]; then
        rm -rf "$entry"
        temporary=$(mktemp -d "$directory/.oracle.XXXXXXXX")
        trap 'rm -rf "$temporary"' EXIT
        build "$temporary/oracle"
        (cd "$temporary" && sha256sum oracle > oracle.sha256)
        mv "$temporary" "$entry"
        trap - EXIT
    fi
    cp "$entry/oracle" "$output"
    (cd "$entry" && sha256sum --check --status oracle.sha256)
fi
"$output" --version
