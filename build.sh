#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
tcc -Isrc -Wall -Werror -Wno-unused-function -g build.c -o build/build
build/build $@
