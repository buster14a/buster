#!/usr/bin/env bash
# ./build.sh builds build/compiler, the only thing it writes under build/.
# ./build.sh check-call-graph checks the no-recursion rule on the built compiler's call graph: it writes the LLVM IR of
# the same translation units, with the same flags, into a temporary directory (at -O0 and without LLVM's passes, so a
# call in the IR is a call in the source) and runs build/compiler check-call-graph on it.
set -e
sources=(src/buster/compiler/main.c src/buster/compiler/c_lexer_benchmark.c src/buster/compiler/x86_64_encoder_benchmark.c src/buster/compiler/lane_benchmark.c src/buster/compiler/object_writer_benchmark.c src/buster/compiler/executable_benchmark.c)
flags=(-march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror)
if [ "$1" = check-call-graph ]; then
    test -x build/compiler || { echo "build.sh check-call-graph: build/compiler is missing; run ./build.sh first" >&2; exit 1; }
    root="$(pwd)"
    ir="$(mktemp -d)"
    trap 'rm -rf "$ir"' EXIT
    names=()
    for source in "${sources[@]}"; do
        name="$(basename "${source%.c}").ll"
        clang -S -emit-llvm -O0 -Xclang -disable-llvm-passes "${flags[@]}" "$source" -o "$ir/$name"
        names+=("$name")
    done
    (cd "$ir" && "$root/build/compiler" check-call-graph "${names[@]}")
else
    rm -rf build || true
    mkdir -p build
    clang "${sources[@]}" -o build/compiler -MJ build/compile_commands.json -g "${flags[@]}" -O3
fi
