#!/usr/bin/env bash
# Optional external Clang API adapter; the ordinary compiler build does not depend on it.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
output=${1:-"$root/build/preprocessor-pragma-oracle"}
compiler=${CXX:-clang++}
config=${LLVM_CONFIG:-llvm-config}
flags=()
configuration_flags=$("$config" --cxxflags --ldflags --libs --system-libs)
while IFS= read -r line; do
    read -r -a part <<< "$line"
    flags+=("${part[@]}")
done <<< "$configuration_flags"
mkdir -p "$(dirname "$output")"
"$compiler" -O2 -Wall -Werror "$root/tools/preprocessor_pragma_oracle.cpp" "${flags[@]}" -lclang-cpp -o "$output"
"$output" --version
