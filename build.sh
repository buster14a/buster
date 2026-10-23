#!/usr/bin/env bash
# ./build.sh builds build/compiler, the only thing it writes under build/ besides compile_commands.json. It compiles the
# translation units at once, each in a clang process of its own (main.c, which includes most of the compiler, takes
# most of the time, and the others compile beside it), then links their objects in the sources' order: the same
# executable as one clang command for all of them.
# ./build.sh check-call-graph checks the no-recursion rule on the built compiler's call graph: it writes the LLVM IR of
# the same translation units into a temporary directory, at once too, and runs build/compiler check-call-graph on it.
# The IR is clang's before any LLVM pass, so a call in it is a call in the source, at the build's own optimization
# level, so it holds every body the build may inline (at -O0, clang doesn't emit an extern inline body at all). First it
# captures src/buster/compiler/call_graph_capture the same way, whose cycle runs through such a body, and fails if the
# check doesn't report it.
# ./build.sh lexer-variant NAME=SOURCE... builds build/compiler with other lexers in it, for benchmark-lexer to check
# and time beside c_tokenize in one process: each SOURCE is a copy of c_lexer.c (compiled through
# src/buster/compiler/c_lexer_variant.c into c_lexer_variant_NAME), or that file's assembly (.s), which
# ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s writes with debug information, and label-spills may label.
set -e
sources=(src/buster/compiler/main.c src/buster/compiler/main_unit_x86.c src/buster/compiler/main_unit_frontend.c src/buster/compiler/main_unit_assembler.c src/buster/compiler/main_unit_link.c src/buster/compiler/main_unit_executable.c src/buster/compiler/main_unit_dynamic.c src/buster/compiler/main_unit_types.c src/buster/compiler/main_unit_backend.c src/buster/compiler/c_lexer_benchmark.c src/buster/compiler/x86_64_encoder_benchmark.c src/buster/compiler/lane_benchmark.c src/buster/compiler/dataflow_benchmark.c src/buster/compiler/object_writer_benchmark.c src/buster/compiler/executable_benchmark.c src/buster/compiler/import_benchmark.c src/buster/compiler/branch_sizing_benchmark.c src/buster/compiler/bind_benchmark.c src/buster/compiler/c_refine_benchmark.c src/buster/compiler/c_preprocessor_benchmark.c src/buster/compiler/c_files_benchmark.c src/buster/compiler/c_parser_benchmark.c src/buster/compiler/c_types_benchmark.c src/buster/compiler/c_builtin_benchmark.c src/buster/compiler/calling_convention_benchmark.c src/buster/compiler/snapshot_benchmark.c src/buster/compiler/link_benchmark.c src/buster/compiler/foreign_benchmark.c src/buster/compiler/floors_benchmark.c src/buster/compiler/x86_64_disassembler_benchmark.c src/buster/compiler/x86_semantics_benchmark.c src/buster/compiler/tier0_benchmark.c src/buster/compiler/tier1_schedule_benchmark.c)
flags=(-march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror)
optimization=-O3
capture()
{
    clang -S -emit-llvm "$optimization" -Xclang -disable-llvm-passes "${flags[@]}" "$1" -o "$2"
}
# The temporary directories, removed on exit, and the processes each_source runs, which an interrupt stops too.
temporary=()
pids=()
trap 'rm -rf "${temporary[@]}"' EXIT
trap 'kill "${pids[@]}" 2> /dev/null; wait; exit 130' INT
trap 'kill "${pids[@]}" 2> /dev/null; wait; exit 143' TERM
# Runs "$2 INDEX" for each source's index at once, each in a process of its own whose standard error goes to the
# directory $1, then prints what each wrote there in the sources' order. Fails if any of them failed.
each_source()
{
    local failed=0
    pids=()
    for i in "${!sources[@]}"; do
        "$2" "$i" 2> "$1/$i.log" &
        pids+=("$!")
    done
    for i in "${!sources[@]}"; do
        wait "${pids[$i]}" || failed=1
        cat "$1/$i.log" >&2
    done
    pids=()
    return "$failed"
}
# The source of index $1 compiled, with the flags in `defines` besides the build's, into the directory
# `object_directory` names, with its compile command beside it.
compile_source()
{
    clang -c "${sources[$1]}" -o "$object_directory/$1.o" -MJ "$object_directory/$1.json" -g "${flags[@]}" \
        "$optimization" "${defines[@]}"
}
# The source of index $1's IR, in the directory `ir` names.
capture_source()
{
    capture "${sources[$1]}" "$ir/$(basename "${sources[$1]%.c}").ll"
}
# Builds build/compiler from the sources, with the flags in `defines`, and the objects in "$@" after theirs. The sources'
# objects go in a directory of their own, so that build/ keeps one file besides compile_commands.json, which gets their
# compile commands in the sources' order.
build_compiler()
{
    object_directory="$(mktemp -d)"
    temporary+=("$object_directory")
    each_source "$object_directory" compile_source
    local linked=()
    for i in "${!sources[@]}"; do
        linked+=("$object_directory/$i.o")
    done
    for i in "${!sources[@]}"; do
        cat "$object_directory/$i.json"
    done > build/compile_commands.json
    clang "${linker_flags[@]}" "${linked[@]}" "$@" -o build/compiler
}
# Choose the linker for build/compiler only; compilation and IR capture keep their existing flags.
select_linker()
{
    local linker="${BUSTER_LINKER-auto}"
    linker_flags=()
    case "$linker" in
        auto) linker="$(command -v wild || command -v mold || true)" ;;
        system) linker="" ;;
        lld|bfd) linker="ld.$linker" ;;
        wild|mold|/*) ;;
        *) echo "build.sh: BUSTER_LINKER must be auto, system, wild, mold, lld, bfd or an absolute path" >&2; return 1 ;;
    esac
    if [ -n "$linker" ]; then
        linker="$(command -v "$linker")" && [ -f "$linker" ] && [ -x "$linker" ] || { echo "build.sh: requested linker is not an executable file on PATH or at the specified path" >&2; return 1; }
        linker_flags=("--ld-path=$linker")
    fi
    echo "build.sh: stage 1 linker: ${linker:-system default}" >&2
}
defines=()
if [ "$1" = check-call-graph ]; then
    test -x build/compiler || { echo "build.sh check-call-graph: build/compiler is missing; run ./build.sh first" >&2; exit 1; }
    root="$(pwd)"
    ir="$(mktemp -d)"
    temporary+=("$ir")
    mkdir "$ir/capture"
    capture src/buster/compiler/call_graph_capture/inline.c "$ir/capture/inline.ll"
    capture src/buster/compiler/call_graph_capture/strong.c "$ir/capture/strong.ll"
    expected="cycle: inline.ll:recurse -> inline.ll:again -> inline.ll:recurse"
    if report="$(cd "$ir/capture" && "$root/build/compiler" check-call-graph inline.ll strong.ll)" || ! grep -qxF "$expected" <<< "$report"; then
        printf 'build.sh check-call-graph: the capture lost the cycle through an inline body; expected "%s", got:\n%s\n' "$expected" "$report" >&2
        exit 1
    fi
    mkdir "$ir/logs"
    each_source "$ir/logs" capture_source
    names=()
    for source in "${sources[@]}"; do
        names+=("$(basename "${source%.c}").ll")
    done
    (cd "$ir" && "$root/build/compiler" check-call-graph "${names[@]}")
elif [ "$1" = lexer-variant-assembly ]; then
    [ $# -eq 4 ] && [[ "$2" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || { echo "usage: ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s" >&2; exit 1; }
    clang -S src/buster/compiler/c_lexer_variant.c -o "$4" -g "${flags[@]}" "$optimization" \
        -DBUSTER_LEXER_VARIANT_SOURCE="\"$(realpath "$3")\"" -DBUSTER_LEXER_VARIANT_NAME="c_lexer_variant_$2"
elif [ "$1" = lexer-variant ]; then
    shift
    [ $# -ne 0 ] || { echo "usage: ./build.sh lexer-variant NAME=SOURCE..." >&2; exit 1; }
    select_linker
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
    defines=(-DBUSTER_LEXER_VARIANTS="$variants")
    build_compiler "${objects[@]}"
    for object in "${objects[@]}"; do
        rm -rf "$(dirname "$object")"
    done
else
    select_linker
    rm -rf build || true
    mkdir -p build
    build_compiler
fi
