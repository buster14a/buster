#!/usr/bin/env bash
rm -rf build || true
mkdir -p build
clang src/buster/compiler/main.c -o build/compiler -march=native -g -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror
