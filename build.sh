#!/usr/bin/env bash
# ./build.sh builds build/compiler, the only thing it writes under build/ besides compile_commands.json. It compiles the
# translation units in a clang process each, one per core at a time, the longest first (main.c's units, which include
# most of the compiler, take most of the time), then links their objects in the sources' order: the same executable as
# one clang command for all of them. It reports how long the compiles took, and which took longest.
# ./build.sh check-call-graph checks the no-recursion rule on the built compiler's call graph: it writes the LLVM IR of
# the same translation units into a temporary directory, one per allowed core, and runs build/compiler check-call-graph on it.
# The IR is clang's before any LLVM pass, so a call in it is a call in the source, at the build's own optimization
# level, so it holds every body the build may inline (at -O0, clang doesn't emit an extern inline body at all). First it
# captures src/buster/compiler/call_graph_capture the same way, whose cycle runs through such a body, and fails if the
# check doesn't report it.
# ./build.sh lexer-variant NAME=SOURCE... builds build/compiler with other lexers in it, for benchmark-lexer to check
# and time beside c_tokenize in one process: each SOURCE is a copy of c_lexer.c (compiled through
# src/buster/compiler/c_lexer_variant.c into c_lexer_variant_NAME), or that file's assembly (.s), which
# ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s writes with debug information, and label-spills may label.
# ./build.sh shifts DIRECTORY SOURCE SHIFT... builds build/compiler, then links the same objects again into
# DIRECTORY/compiler-SHIFT for each SHIFT, a number of bytes, with that much padding in .text before the objects of
# SOURCE (a source's file name), a multiple of its .text's alignment: its code and what the linker places after it move
# by SHIFT, the same instructions at other addresses. The same code ran up to 2% faster or slower so on the 9700X
# (#509), so time-runs compares builds over several placements.
# print-compile-flags and print-sources print expanded compile arguments and source paths as NUL-delimited argv,
# without building. Consumers read arrays rather than reevaluating shell text from this script.
set -e
sources=(src/buster/compiler/main.c src/buster/compiler/main_unit_x86.c src/buster/compiler/main_unit_frontend.c src/buster/compiler/main_unit_assembler.c src/buster/compiler/main_unit_link.c src/buster/compiler/main_unit_executable.c src/buster/compiler/main_unit_dynamic.c src/buster/compiler/main_unit_types.c src/buster/compiler/main_unit_checker.c src/buster/compiler/main_unit_check_builtins.c src/buster/compiler/main_unit_backend.c src/buster/compiler/c_lexer_benchmark.c src/buster/compiler/x86_64_encoder_benchmark.c src/buster/compiler/lane_benchmark.c src/buster/compiler/dataflow_benchmark.c src/buster/compiler/dominators_benchmark.c src/buster/compiler/tier1_allocate_benchmark.c src/buster/compiler/object_writer_benchmark.c src/buster/compiler/executable_benchmark.c src/buster/compiler/import_benchmark.c src/buster/compiler/branch_sizing_benchmark.c src/buster/compiler/bind_benchmark.c src/buster/compiler/c_refine_benchmark.c src/buster/compiler/c_preprocessor_benchmark.c src/buster/compiler/c_files_benchmark.c src/buster/compiler/c_parser_benchmark.c src/buster/compiler/c_types_benchmark.c src/buster/compiler/c_builtin_benchmark.c src/buster/compiler/c_checker_benchmark.c src/buster/compiler/calling_convention_benchmark.c src/buster/compiler/tier1_frame_benchmark.c src/buster/compiler/snapshot_benchmark.c src/buster/compiler/link_benchmark.c src/buster/compiler/foreign_benchmark.c src/buster/compiler/floors_benchmark.c src/buster/compiler/x86_64_disassembler_benchmark.c src/buster/compiler/x86_semantics_benchmark.c src/buster/compiler/tier0_benchmark.c src/buster/compiler/tier1_schedule_benchmark.c src/buster/compiler/tier1_select_benchmark.c src/buster/compiler/tier1_model_measure.c)
flags=(-march="${BUSTER_HOST_ARCH:-native}" -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror)
# The target-query defaults follow the Clang driver used to build this compiler.
# Keep the triple as a single argument and reject text that cannot be a triple.
clang_default_target=$(clang -dumpmachine)
[[ "$clang_default_target" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "build.sh: invalid clang default target" >&2; exit 1; }
flags+=("-DBUSTER_CLANG_DEFAULT_TARGET=\"$clang_default_target\"")
# The scalar host build retains the vector kernels in source but does not emit
# them. Their unused helpers are expected in that build alone.
host_defines=$(clang "${flags[@]}" -dM -E -include src/buster/compiler/host_isa.h -x c /dev/null)
if ! grep -q '^#define BUSTER_HOST_VECTOR 1$' <<< "$host_defines"; then
    flags+=(-Wno-unused-function -Wno-psabi)
    echo "build.sh: host execution: scalar (${BUSTER_HOST_ARCH:-native})" >&2
fi
optimization=-O3
# The executable's debug information: -g, unless BUSTER_DEBUG_INFO is 0, which leaves it out of the sources' objects
# and takes a fifth of the build's time (the 9700X job builds so, since nothing there reads it). Clang's code with -g is
# its code without it but for a few functions' stack slots.
debug=(-g)
case "${BUSTER_DEBUG_INFO-1}" in
    1) ;;
    0) debug=() ;;
    *) echo "build.sh: BUSTER_DEBUG_INFO must be 0 or 1" >&2; exit 1 ;;
esac
# Sanitizer builds keep the production semantics (-fwrapv, unsigned char and
# aliasing) and instrument every unit and the final link with the same flags.
# TSan requires PIE; use it for all diagnostic builds. Keep the ordinary fixed
# address executable and its benchmark layout unchanged.
sanitizer_flags=()
link_mode=(-no-pie)
case "${BUSTER_SANITIZER:-none}" in
    none) ;;
    address) sanitizer_flags=(-fsanitize=address,undefined,bounds -fsanitize-address-use-after-scope -fsanitize-address-use-after-return=always) ;;
    undefined) sanitizer_flags=(-fsanitize=undefined,bounds) ;;
    thread) sanitizer_flags=(-fsanitize=thread) ;;
    memory) sanitizer_flags=(-fsanitize=memory -fsanitize-memory-track-origins=2) ;;
    *) echo 'build.sh: BUSTER_SANITIZER must be none, address, undefined, thread or memory' >&2; exit 1 ;;
esac
if [ "${BUSTER_SANITIZER:-none}" != none ]; then
    [ -z "${BUSTER_HOST_RUNTIME:-}" ] || { echo 'build.sh: sanitizers require the host runtime, not BUSTER_HOST_RUNTIME' >&2; exit 1; }
    sanitizer_flags+=(-fno-sanitize-recover=all -fno-sanitize-trap=all -fno-omit-frame-pointer -fno-optimize-sibling-calls -fPIE)
    flags+=("${sanitizer_flags[@]}")
    optimization=-O1
    debug=(-gline-tables-only)
    link_mode=(-pie)
fi
# The order the compiles start in, each source's file name once, the longest first by their times on the 9700X (#270):
# the build runs at most one compile per core (cores, below), so a long one that started late would make it wait for
# it. A new source goes where its time puts it.
order=(main_unit_checker.c main_unit_backend.c main_unit_frontend.c main_unit_executable.c main_unit_dynamic.c
    main_unit_link.c c_parser_benchmark.c main.c main_unit_x86.c main_unit_types.c main_unit_check_builtins.c
    c_files_benchmark.c c_preprocessor_benchmark.c link_benchmark.c executable_benchmark.c
    main_unit_assembler.c foreign_benchmark.c tier0_benchmark.c snapshot_benchmark.c x86_64_encoder_benchmark.c
    bind_benchmark.c c_refine_benchmark.c c_checker_benchmark.c import_benchmark.c tier1_select_benchmark.c
    c_lexer_benchmark.c branch_sizing_benchmark.c object_writer_benchmark.c x86_64_disassembler_benchmark.c
    calling_convention_benchmark.c tier1_schedule_benchmark.c x86_semantics_benchmark.c tier1_model_measure.c
    c_types_benchmark.c dominators_benchmark.c tier1_frame_benchmark.c tier1_allocate_benchmark.c lane_benchmark.c
    floors_benchmark.c dataflow_benchmark.c c_builtin_benchmark.c)
# The sources' indices in `order`'s order, into `indices`. Fails, naming it, for two sources of one file name, which
# `order` couldn't tell apart, for a file that `order` names twice or that isn't a source, and for a source that it
# doesn't name.
compile_order()
{
    local -A index=()
    local name
    indices=()
    for i in "${!sources[@]}"; do
        name="$(basename "${sources[$i]}")"
        [ -z "${index[$name]-}" ] || { echo "build.sh: two sources are named $name, which order names by file name" >&2; return 1; }
        index["$name"]="$i"
    done
    for name in "${order[@]}"; do
        [ -n "${index[$name]-}" ] || { echo "build.sh: order names $name, which isn't a source or is named twice" >&2; return 1; }
        indices+=("${index[$name]}")
        unset "index[$name]"
    done
    [ "${#index[@]}" -eq 0 ] || { echo "build.sh: order doesn't name ${!index[*]}" >&2; return 1; }
}
compile_order
# The number of cores of the CPUs this process may run on, by their core_cpus_list, or 0 if /sys doesn't say: the
# build runs at most one compile per core, since two clang processes on a core's two threads slow each other down, the
# longest among them. On the 9700X before main.c's homes, its 15 threads compiling at once took 40.0 s and 331 s of CPU,
# and its 8 cores one compile each, the longest first, 32.3 s and 233 s; with them, 25.3 s and 200 s (#270).
cores()
{
    local range cpu
    local -A seen=()
    local IFS=,
    for range in $(sed -n 's/^Cpus_allowed_list:[[:space:]]*//p' /proc/self/status); do
        for ((cpu = ${range%-*}; cpu <= ${range#*-}; cpu += 1)); do
            if [ -r "/sys/devices/system/cpu/cpu$cpu/topology/core_cpus_list" ]; then
                seen["$(< "/sys/devices/system/cpu/cpu$cpu/topology/core_cpus_list")"]=1
            fi
        done
    done
    echo "${#seen[@]}"
}
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
# Sets the variable $1 to the microseconds since the epoch. Bash writes $EPOCHREALTIME's seconds and microseconds (six
# digits) apart by the locale's decimal point, a comma in es_ES, so they are its digits alone. This is the script's only
# reader of $EPOCHREALTIME, as .github/workflows/build-locale.yml checks, with now() and timed() under such locales.
now()
{
    printf -v "$1" %s "${EPOCHREALTIME//[!0-9]/}"
}
# Runs "${@:2}" and writes the microseconds it took to the file $1, with its status.
timed()
{
    local start end status=0
    now start
    "${@:2}" || status=$?
    now end
    echo "$((end - start))" > "$1"
    return "$status"
}
# Runs "$2 INDEX" for each source's index, in `indices`' order and at most $3 at once, each in a process of its own whose
# standard error goes to the directory $1, with the microseconds it took, then prints what each wrote there in the
# sources' order. Fails if any of them failed.
each_source()
{
    local failed=0 running=0 finished status
    local -A statuses=()
    pids=()
    for i in "${indices[@]}"; do
        if [ "$running" -ge "$3" ]; then
            status=0
            wait -n -p finished || status=$?
            statuses["$finished"]="$status"
            running=$((running - 1))
        fi
        timed "$1/$i.time" "$2" "$i" 2> "$1/$i.log" &
        pids[$i]="$!"
        running=$((running + 1))
    done
    for i in "${!sources[@]}"; do
        status="${statuses[${pids[$i]}]-}"
        if [ -z "$status" ]; then
            status=0
            wait "${pids[$i]}" || status=$?
        fi
        [ "$status" -eq 0 ] || failed=1
        cat "$1/$i.log" >&2
    done
    pids=()
    return "$failed"
}
# The source of index $1 compiled, with the flags in `defines` besides the build's, into the directory
# `object_directory` names, with its compile command beside it.
compile_source()
{
    clang -c "${sources[$1]}" -o "$object_directory/$1.o" -MJ "$object_directory/$1.json" "${debug[@]}" "${flags[@]}" \
        "$optimization" "${defines[@]}"
}
# Prints how long the sources' compiles in the directory $1 took, from each_source's times: the whole, which waits for
# the longest (main.c's comment, #270), and the three longest.
report_times()
{
    for i in "${!sources[@]}"; do
        echo "$(< "$1/$i.time") $(basename "${sources[$i]}")"
    done | sort -rn | awk -v count="${#sources[@]}" -v total="$2" '
        NR <= 3 { longest = longest (NR > 1 ? ", " : "") sprintf("%s %.1f s", $2, $1 / 1e6) }
        END { printf "build.sh: %u sources compiled in %.1f s; the longest: %s\n", count, total / 1e6, longest }' >&2
}
# The source of index $1's IR, in the directory `ir` names.
capture_source()
{
    capture "${sources[$1]}" "$ir/$(basename "${sources[$1]%.c}").ll"
}
# Link a baseline runtime explicitly when CI emulates an old CPU. The host's
# startup objects and loader can require a newer ISA even with -march=x86-64.
link_program()
{
    local output="$1"
    shift
    if [ -n "${BUSTER_HOST_RUNTIME:-}" ]; then
        [ "${BUSTER_HOST_ARCH:-native}" = x86-64 ] || { echo 'build.sh: BUSTER_HOST_RUNTIME requires BUSTER_HOST_ARCH=x86-64' >&2; return 1; }
        local library="$BUSTER_HOST_RUNTIME/usr/lib/x86_64-linux-gnu"
        local support="$BUSTER_HOST_RUNTIME/usr/lib/gcc/x86_64-linux-gnu/15/libgcc.a"
        clang "${link_mode[@]}" -nostdlib "${linker_flags[@]}" "$library/crt1.o" "$library/crti.o" "$@" \
            "$library/libm.so.6" "$library/libc.so.6" "$support" "$library/crtn.o" -o "$output"
    else
        clang "${link_mode[@]}" "${sanitizer_flags[@]}" "${linker_flags[@]}" "$@" -lm -ldl -o "$output"
    fi
}

# Builds build/compiler from the sources, with the flags in `defines`, and the objects in "$@" after theirs. The sources'
# objects go in a directory of their own, so that build/ keeps one file besides compile_commands.json, which gets their
# compile commands in the sources' order. It is linked at a fixed address (-no-pie), from the same code: as a PIE, its
# 10.6 MB of pointer tables (.data.rel.ro) took 366,684 relative relocations at every start, whose 2,800 page faults
# made a run that does nothing take 8 to 13 ms, where linked at its address it takes 1.5 ms with 113 (#86: link-foreign
# and the tests run it as a process of its own, and a small link's work is 0.8 ms).
build_compiler()
{
    object_directory="$(mktemp -d)"
    temporary+=("$object_directory")
    local start end jobs
    jobs="$(cores)"
    now start
    each_source "$object_directory" compile_source "$((jobs != 0 ? jobs : ${#sources[@]}))"
    now end
    report_times "$object_directory" "$((end - start))"
    local linked=()
    for i in "${!sources[@]}"; do
        linked+=("$object_directory/$i.o")
    done
    for i in "${!sources[@]}"; do
        cat "$object_directory/$i.json"
    done > build/compile_commands.json
    link_program build/compiler "${linked[@]}" "$@"
}
# Links build_compiler's objects into $1 with $3 bytes of padding in .text before the objects of the source of index
# $2, an object of its own, in the order build_compiler links them. The padding is a section of its own,
# .text.buster_shift_padding, which the linkers put in .text in the objects' order, with the source's .text's alignment
# and SHF_GNU_RETAIN: wild collects sections nothing refers to (0.10 even what -u names, which 0.11 keeps), and places
# .text's by their alignment, the largest first, in the objects' order within each, so only padding of the same
# alignment goes right before the source's. Fails for a shift that isn't a multiple of that alignment, which would
# round it up, and if the linker dropped the padding all the same.
link_shifted()
{
    local object="$object_directory/$2.o" padding="$object_directory/padding-$3.o" alignment linked=()
    alignment="$(readelf -SW "$object" | awk '$0 ~ /\] \.text / { print $NF }')"
    [[ "$alignment" =~ ^[1-9][0-9]*$ ]] || { echo "build.sh shifts: can't read the alignment of ${sources[$2]}'s .text" >&2; return 1; }
    [ $(($3 % alignment)) -eq 0 ] || { echo "build.sh shifts: $3 isn't a multiple of ${sources[$2]}'s .text's alignment, $alignment" >&2; return 1; }
    printf '\t.section .text.buster_shift_padding,"axR",@progbits\n\t.balign %d\n\t.globl buster_shift_padding\nbuster_shift_padding:\n\t.skip %d\n' \
        "$alignment" "$3" | clang -c -x assembler - -o "$padding"
    for i in "${!sources[@]}"; do
        if [ "$i" -eq "$2" ]; then
            linked+=("$padding")
        fi
        linked+=("$object_directory/$i.o")
    done
    link_program "$1" "${linked[@]}"
    if ! nm "$1" | grep -q ' buster_shift_padding$' || { [ "$3" -ne 0 ] && cmp -s build/compiler "$1"; }; then
        echo "build.sh shifts: the linker dropped $1's padding" >&2
        return 1
    fi
    echo "build.sh shifts: $1: ${sources[$2]}'s code $3 bytes later (its .text is aligned to $alignment)" >&2
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
if [ "$1" = print-compile-flags ]; then
    printf '%s\0' "${debug[@]}" "${flags[@]}" "$optimization" "${defines[@]}"
elif [ "$1" = print-sanitizer-flags ]; then
    if [ "${#sanitizer_flags[@]}" -ne 0 ]; then
        printf '%s\0' -O1 "${debug[@]}" "${sanitizer_flags[@]}" "${link_mode[@]}"
    fi
elif [ "$1" = print-sources ]; then
    printf '%s\0' "${sources[@]}"
elif [ "$1" = check-call-graph ]; then
    test -x build/compiler || { echo "build.sh check-call-graph: build/compiler is missing; run ./build.sh first" >&2; exit 1; }
    jobs="$(cores)"
    [[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "build.sh check-call-graph: couldn't determine a positive CPU budget" >&2; exit 1; }
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
    each_source "$ir/logs" capture_source "$jobs"
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
            clang -c src/buster/compiler/c_lexer_variant.c -o "$object" "${debug[@]}" "${flags[@]}" "$optimization" \
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
elif [ "$1" = shifts ]; then
    [ $# -ge 4 ] || { echo "usage: ./build.sh shifts DIRECTORY SOURCE SHIFT..." >&2; exit 1; }
    directory="$2"
    unit=""
    for i in "${!sources[@]}"; do
        if [ "$(basename "${sources[$i]}")" = "$3" ]; then
            unit="$i"
        fi
    done
    [ -n "$unit" ] || { echo "build.sh shifts: $3 isn't a source's file name" >&2; exit 1; }
    shift 3
    for bytes in "$@"; do
        [[ "$bytes" =~ ^(0|[1-9][0-9]{0,6})$ ]] || { echo "build.sh shifts: $bytes isn't a shift of 0 to 9,999,999 bytes" >&2; exit 1; }
    done
    mkdir -p "$directory"
    select_linker
    rm -rf build || true
    mkdir -p build
    build_compiler
    for bytes in "$@"; do
        link_shifted "$directory/compiler-$bytes" "$unit" "$bytes"
    done
else
    select_linker
    rm -rf build || true
    mkdir -p build
    build_compiler
fi
