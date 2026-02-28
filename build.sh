#!/usr/bin/env bash
set -euox pipefail

BUSTER_CI=0
EXTRA_FLAGS=""

for arg in "$@"; do
    case "$arg" in
        --ci=0) BUSTER_CI=0 ;;
        --ci=1) BUSTER_CI=1 ;;
    esac
done

if [[ "$BUSTER_CI" == "1" ]]; then
    echo "USER=$USER"
    export DISPLAY=":99"
    echo "DISPLAY=$DISPLAY"
    echo "TERM=$TERM"
    id

    export DEBUGINFOD_URLS="https://debuginfod.ubuntu.com"
    export DEBUGINFOD_PROGRESS=1
    export DEBUGINFOD_CACHE_PATH="${RUNNER_TEMP:-/tmp}/debuginfod_cache"
    export ASAN_OPTIONS="symbolize=1:fast_unwind_on_malloc=0"
    export UBSAN_OPTIONS="symbolize=1:print_stacktrace=1"
    mkdir -p "$DEBUGINFOD_CACHE_PATH"
fi

BUSTER_BUILD_DIRECTORY=build
mkdir -p $BUSTER_BUILD_DIRECTORY
clang preamble.c -o $BUSTER_BUILD_DIRECTORY/preamble -Isrc -Wall -Werror -std=gnu2x -Wno-unused-function -funsigned-char -DBUSTER_CI=$BUSTER_CI -g
$BUSTER_BUILD_DIRECTORY/preamble $@
exit $?
