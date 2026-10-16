#!/usr/bin/env bash
rm -rf build || true
mkdir -p build
clang src/buster/compiler/main.c -o build/compiler -O3 -march=native -g -Isrc
