#!/usr/bin/env bash
set -e
rm -rf build || true
mkdir -p build
clang src/buster/compiler/main.c src/buster/compiler/c_lexer_benchmark.c src/buster/compiler/x86_64_encoder_benchmark.c src/buster/compiler/lane_benchmark.c src/buster/compiler/object_writer_benchmark.c -o build/compiler -MJ build/compile_commands.json -march=native -g -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -O3
