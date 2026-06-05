#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
if [[ ${1-} == "test_all_combinations_ci" ]]; then
    if [[ "$(uname -s)" = "Linux" || "$(uname -s)" = "Darwin" ]]; then
        set -x
        source /opt/vulkan-sdk/current/setup-env.sh
    fi
fi
env
tcc -Isrc -Wall -Werror -Wno-unused-function -g build.c -o build/build
build/build $@
