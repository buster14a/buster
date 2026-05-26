#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
tcc -Isrc -Wall -Werror -g build.c -o build/build
build/build $@
