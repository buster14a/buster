#!/usr/bin/env bash
set -euo pipefail

BUSTER_CI=0
EXTRA_FLAGS=""

for arg in "$@"; do
    case "$arg" in
        --ci=0) BUSTER_CI=0 ;;
        --ci=1) BUSTER_CI=1 ;;
    esac
done

BUSTER_BUILD_DIRECTORY=build
mkdir -p $BUSTER_BUILD_DIRECTORY
clang preamble.c -o $BUSTER_BUILD_DIRECTORY/preamble -Isrc -Wall -Werror -std=gnu2x -Wno-unused-function -funsigned-char -DBUSTER_CI=$BUSTER_CI -g
$BUSTER_BUILD_DIRECTORY/preamble $@
exit $?
