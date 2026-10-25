#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/performance
bash tools/performance/prepare-deps.sh > /dev/null
source tools/performance/dependency-flags.sh
source_file=tools/performance/bmf.c
output_file=build/performance/bmf
if [[ ${1:-} == --tests ]]; then
    source_file=tools/performance/bmf_test.c
    output_file=build/performance/bmf-test
elif [[ $# != 0 ]]; then
    exit 2
fi
read -r -a cflags <<< "$(pkg-config --cflags openssl libpcre2-8 libcurl)"
read -r -a libs <<< "$(pkg-config --libs openssl libpcre2-8 libcurl)"
"${CC:-clang}" -std=c17 -O2 -g -Wall -Wextra -Werror -D_GNU_SOURCE \
    "${performance_dependency_includes[@]}" "${cflags[@]}" \
    "$source_file" "${performance_dependency_sources[@]}" \
    "${libs[@]}" -lm ${BUSTER_PERFORMANCE_CFLAGS:-} -o "$output_file"
