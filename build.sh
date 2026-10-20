#!/usr/bin/env bash
# ./build.sh builds build/compiler, the only thing it writes under build/.
# ./build.sh check-call-graph checks the no-recursion rule on the built compiler's call graph: it writes the LLVM IR of
# the same translation units into a temporary directory and runs build/compiler check-call-graph on it. The IR is
# clang's before any LLVM pass, so a call in it is a call in the source, at the build's own optimization level, so it
# holds every body the build may inline (at -O0, clang doesn't emit an extern inline body at all). First it captures
# src/buster/compiler/call_graph_capture the same way, whose cycle runs through such a body, and fails if the check
# doesn't report it.
set -e
sources=(src/buster/compiler/main.c src/buster/compiler/c_lexer_benchmark.c src/buster/compiler/x86_64_encoder_benchmark.c src/buster/compiler/lane_benchmark.c src/buster/compiler/object_writer_benchmark.c src/buster/compiler/executable_benchmark.c)
flags=(-march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror)
optimization=-O3
capture()
{
    clang -S -emit-llvm "$optimization" -Xclang -disable-llvm-passes "${flags[@]}" "$1" -o "$2"
}
if [ "$1" = check-call-graph ]; then
    test -x build/compiler || { echo "build.sh check-call-graph: build/compiler is missing; run ./build.sh first" >&2; exit 1; }
    root="$(pwd)"
    ir="$(mktemp -d)"
    trap 'rm -rf "$ir"' EXIT
    mkdir "$ir/capture"
    capture src/buster/compiler/call_graph_capture/inline.c "$ir/capture/inline.ll"
    capture src/buster/compiler/call_graph_capture/strong.c "$ir/capture/strong.ll"
    expected="cycle: inline.ll:recurse -> inline.ll:again -> inline.ll:recurse"
    if report="$(cd "$ir/capture" && "$root/build/compiler" check-call-graph inline.ll strong.ll)" || ! grep -qxF "$expected" <<< "$report"; then
        printf 'build.sh check-call-graph: the capture lost the cycle through an inline body; expected "%s", got:\n%s\n' "$expected" "$report" >&2
        exit 1
    fi
    names=()
    for source in "${sources[@]}"; do
        name="$(basename "${source%.c}").ll"
        capture "$source" "$ir/$name"
        names+=("$name")
    done
    (cd "$ir" && "$root/build/compiler" check-call-graph "${names[@]}")
else
    rm -rf build || true
    mkdir -p build
    clang "${sources[@]}" -o build/compiler -MJ build/compile_commands.json -g "${flags[@]}" "$optimization"
fi
