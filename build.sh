#!/usr/bin/env bash
set -e
rm -rf build || true
mkdir -p build
clang src/buster/compiler/main.c -o build/compiler -MJ build/compile_commands.json -march=native -g -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -O3
