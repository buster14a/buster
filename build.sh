#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
clang -Isrc -Wall -Werror -Wno-unused-function -g build.c -o build/build
build/build $@
