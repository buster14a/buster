#!/usr/bin/env bash
# ./build.sh builds build/compiler, the only thing it writes under build/.
# ./build.sh check-call-graph checks the no-recursion rule on the built compiler's call graph: it writes the LLVM IR of
# the same translation units into a temporary directory and runs build/compiler check-call-graph on it. The IR is
# clang's before any LLVM pass, so a call in it is a call in the source, at the build's own optimization level, so it
# holds every body the build may inline (at -O0, clang doesn't emit an extern inline body at all). First it captures
# src/buster/compiler/call_graph_capture the same way, whose cycle runs through such a body, and fails if the check
# doesn't report it.
# ./build.sh lexer-variant NAME=SOURCE... builds build/compiler with other lexers in it, for benchmark-lexer to check
# and time beside c_tokenize in one process: each SOURCE is a copy of c_lexer.c (compiled through
# src/buster/compiler/c_lexer_variant.c into c_lexer_variant_NAME), or that file's assembly (.s), which
# ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s writes with debug information, and label-spills may label.
set -e
sources=(src/buster/compiler/main.c src/buster/compiler/c_lexer_benchmark.c src/buster/compiler/x86_64_encoder_benchmark.c src/buster/compiler/lane_benchmark.c src/buster/compiler/dataflow_benchmark.c src/buster/compiler/preprocessor_fuzz_benchmark.c src/buster/compiler/object_writer_benchmark.c src/buster/compiler/executable_benchmark.c src/buster/compiler/import_benchmark.c src/buster/compiler/branch_sizing_benchmark.c src/buster/compiler/bind_benchmark.c src/buster/compiler/c_refine_benchmark.c src/buster/compiler/c_preprocessor_benchmark.c src/buster/compiler/c_files_benchmark.c src/buster/compiler/c_types_benchmark.c src/buster/compiler/calling_convention_benchmark.c src/buster/compiler/snapshot_benchmark.c src/buster/compiler/link_benchmark.c src/buster/compiler/foreign_benchmark.c src/buster/compiler/floors_benchmark.c src/buster/compiler/x86_64_disassembler_benchmark.c)
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
elif [ "$1" = lexer-variant-assembly ]; then
    [ $# -eq 4 ] && [[ "$2" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || { echo "usage: ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s" >&2; exit 1; }
    clang -S src/buster/compiler/c_lexer_variant.c -o "$4" -g "${flags[@]}" "$optimization" \
        -DBUSTER_LEXER_VARIANT_SOURCE="\"$(realpath "$3")\"" -DBUSTER_LEXER_VARIANT_NAME="c_lexer_variant_$2"
elif [ "$1" = lexer-variant ]; then
    shift
    [ $# -ne 0 ] || { echo "usage: ./build.sh lexer-variant NAME=SOURCE..." >&2; exit 1; }
    rm -rf build || true
    mkdir -p build
    variants=""
    tags=""
    objects=()
    for variant in "$@"; do
        name="${variant%%=*}"
        source="${variant#*=}"
        [[ "$variant" == *=* && "$name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ && -f "$source" ]] || { echo "build.sh lexer-variant: $variant isn't NAME=SOURCE" >&2; exit 1; }
        # The objects go in a directory of their own, so that build/ keeps one file besides compile_commands.json.
        object="$(mktemp -d)/$name.o"
        if [[ "$source" == *.s ]]; then
            grep -q "^c_lexer_variant_$name:" "$source" || { echo "build.sh lexer-variant: $source doesn't define c_lexer_variant_$name; write it with ./build.sh lexer-variant-assembly $name" >&2; exit 1; }
            # Two labeled variants of one tag would share their symbols, and spill-report would mix their samples.
            tag="$(grep -m1 -o '^buster_spill\.[A-Za-z0-9_]*\.' "$source" || true)"
            if [ -n "$tag" ]; then
                [[ " $tags " != *" $tag "* ]] || { echo "build.sh lexer-variant: $source is labeled with the tag of another variant (${tag:13:-1}); give label-spills each variant's own" >&2; exit 1; }
                tags="$tags $tag"
            fi
            clang -c "$source" -o "$object"
        else
            clang -c src/buster/compiler/c_lexer_variant.c -o "$object" -g "${flags[@]}" "$optimization" \
                -DBUSTER_LEXER_VARIANT_SOURCE="\"$(realpath "$source")\"" -DBUSTER_LEXER_VARIANT_NAME="c_lexer_variant_$name"
        fi
        objects+=("$object")
        variants="$variants BUSTER_LEXER_VARIANT($name)"
    done
    clang "${sources[@]}" "${objects[@]}" -o build/compiler -MJ build/compile_commands.json -g "${flags[@]}" "$optimization" \
        -DBUSTER_LEXER_VARIANTS="$variants"
    for object in "${objects[@]}"; do
        rm -rf "$(dirname "$object")"
    done
else
    rm -rf build || true
    mkdir -p build
    clang "${sources[@]}" -o build/compiler -MJ build/compile_commands.json -g "${flags[@]}" "$optimization"
fi
