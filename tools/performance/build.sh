#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/performance
"${CC:-clang}" -std=c17 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror \
    ${BUSTER_PERFORMANCE_CFLAGS:-} tools/performance/main.c -o build/performance/bmf
"${CC:-clang}" -std=c17 -O2 -g -Wall -Wextra -Werror \
    ${BUSTER_PERFORMANCE_CFLAGS:-} tools/performance/serve.c -o build/performance/serve
if [[ $(uname -s) == Linux ]]; then
    for tool in capture profile; do
        "${CC:-clang}" -std=c17 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror \
            ${BUSTER_PERFORMANCE_CFLAGS:-} "tools/performance/$tool.c" -o "build/performance/$tool"
    done
fi
