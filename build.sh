#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The front end of the CMake build (CMakeLists.txt, CMakePresets.json; docs/commands/build.md has the details):
#
#   ./build.sh [build] [--config Debug|Release] [--target TARGET]... [--jobs N] [--verbose]
#   ./build.sh test [--config Debug|Release] [--jobs N] [-- CTEST_ARGUMENT...]
#   ./build.sh clean [--config Debug|Release | --all]
#   ./build.sh configure
#   ./build.sh print-sources | print-compile-flags [--config C] | print-sanitizer-flags | print-output [--config C]
#   ./build.sh check-call-graph [--config C] [--jobs N]
#   ./build.sh lexer-variant [--config C] [--jobs N] NAME=SOURCE...
#   ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s
#   ./build.sh shifts [--config C] [--jobs N] DIRECTORY SOURCE SHIFT...
#
# A build configures build/cmake/VARIANT, Ninja Multi-Config's tree for the BUSTER_* environment's host ISA,
# runtime, sanitizer, fuzzing and debug information, only when the settings, PATH or the toolchain changed since it
# last did (a changed clang starts the tree afresh), then has Ninja build what changed: an unchanged build compiles,
# links and bakes nothing. It builds Release unless --config says Debug, at most one compile per core of the CPUs it may
# run on, and when it builds the executable, build/cmake/VARIANT/CONFIG/compiler, it publishes that file as
# build/compiler (a hard link, the same file) and its configuration's compile commands as build/compile_commands.json,
# once the build has succeeded. Builds of one tree, and publications, wait for each other.
#
# print-compile-flags, print-sanitizer-flags and print-sources print expanded arguments and source paths as
# NUL-delimited argv, without building (cmake/BusterPrint.cmake); consumers read arrays rather than reevaluating shell
# text from this script. print-output prints the configuration's executable's path.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
caller="$PWD"

usage()
{
    sed -n '/^#   \.\/build\.sh/s/^#   //p' "$root/build.sh" >&2
    exit 2
}

die()
{
    printf 'build.sh: %s\n' "$*" >&2
    exit 1
}

# $1 as an absolute path, a relative one being relative to the directory build.sh started in.
absolute()
{
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$caller/$1" ;;
    esac
}

command=build
case "${1-}" in
    build|test|clean|configure|print-sources|print-compile-flags|print-sanitizer-flags|print-output|check-call-graph|lexer-variant|lexer-variant-assembly|shifts)
        command="$1"
        shift
        ;;
    help|-h|--help)
        usage
        ;;
esac
config=Release
config_given=0
jobs=""
targets=()
verbose=0
clean_all=0
arguments=()
while [ $# -gt 0 ]; do
    case "$1" in
        --config|--target|--jobs|-j)
            [ $# -ge 2 ] || die "$1 needs a value"
            case "$1" in
                --config) config="$2"; config_given=1 ;;
                --target) targets+=("$2") ;;
                *) jobs="$2" ;;
            esac
            shift 2
            ;;
        --config=*) config="${1#*=}"; config_given=1; shift ;;
        --target=*) targets+=("${1#*=}"); shift ;;
        --jobs=*) jobs="${1#*=}"; shift ;;
        --verbose|-v) verbose=1; shift ;;
        --all) clean_all=1; shift ;;
        --) shift; arguments+=("$@"); break ;;
        -*) die "unknown option $1; ./build.sh --help lists the commands" ;;
        *) arguments+=("$1"); shift ;;
    esac
done
case "$config" in
    Debug|debug) config=Debug ;;
    Release|release) config=Release ;;
    *) die "--config must be Debug or Release" ;;
esac
[ -z "$jobs" ] || [[ "$jobs" =~ ^[1-9][0-9]{0,3}$ ]] || die "--jobs must be a positive number"
# Each command's own options: the others would be ignored silently.
case "$command" in
    build) [ "${#arguments[@]}" -eq 0 ] || die "build takes no arguments: ${arguments[*]}" ;;
    test) ;;
    clean) [ "${#arguments[@]}" -eq 0 ] && { [ "$clean_all" = 0 ] || [ "$config_given" = 0 ]; } || die "usage: ./build.sh clean [--config Debug|Release | --all]" ;;
    *) [ "$clean_all" = 0 ] || die "--all is clean's option" ;;
esac
case "$command" in
    build) ;;
    *) [ "${#targets[@]}" -eq 0 ] && [ "$verbose" = 0 ] || die "--target and --verbose are build's options" ;;
esac

# The tools. CMake 3.25 has every feature CMakeLists.txt and CMakePresets.json use; Ninja Multi-Config needs Ninja 1.10.
clang="$(command -v clang)" || die "clang isn't on PATH"
cmake="$(command -v cmake)" || die "cmake (3.25 or later) isn't on PATH"
version_at_least()
{
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | sed -n 1p)" = "$2" ]
}
cmake_version="$("$cmake" --version | sed -n '1s/^cmake version \([0-9][0-9.]*\).*/\1/p')"
version_at_least "$cmake_version" 3.25 || die "cmake $cmake_version is older than 3.25"

# The settings: the environment's BUSTER_* variables, each passed explicitly, so that unsetting one restores its
# default rather than leaving a tree's cached value. An unset or empty one is its default, but BUSTER_DEBUG_INFO and
# BUSTER_LINKER, for which an empty value is an error (cmake/BusterSettings.cmake checks them all).
fuzz_entry="${BUSTER_FUZZ_ENTRY:-}"
[ -z "$fuzz_entry" ] || fuzz_entry="$(absolute "$fuzz_entry")"
host_runtime="${BUSTER_HOST_RUNTIME:-}"
[ -z "$host_runtime" ] || host_runtime="$(absolute "$host_runtime")"
settings=(
    "-DBUSTER_HOST_ARCH=${BUSTER_HOST_ARCH:-native}"
    "-DBUSTER_HOST_RUNTIME=$host_runtime"
    "-DBUSTER_DEBUG_INFO=${BUSTER_DEBUG_INFO-1}"
    "-DBUSTER_LINKER=${BUSTER_LINKER-auto}"
    "-DBUSTER_BUILD_DEFINES=${BUSTER_BUILD_DEFINES:-}"
    "-DBUSTER_SANITIZER=${BUSTER_SANITIZER:-}"
    "-DBUSTER_SANITIZER_OPTIMIZATION=${BUSTER_SANITIZER_OPTIMIZATION:-3}"
    "-DBUSTER_FUZZ_AVAILABLE=${BUSTER_FUZZ_AVAILABLE:-0}"
    "-DBUSTER_FUZZ_ENTRY=$fuzz_entry"
    "-DBUSTER_BAKE=${BUSTER_BAKE:-1}"
)

temporary=()
cleanup()
{
    [ "${#temporary[@]}" -eq 0 ] || rm -rf "${temporary[@]}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# The description's $1 (cmake/BusterPrint.cmake) for the settings, one argument per line, to standard output.
describe()
{
    local output
    output="$(mktemp)"
    temporary+=("$output")
    "$cmake" "-DBUSTER_PRINT=$1" "-DBUSTER_PRINT_OUTPUT=$output" "-DBUSTER_PRINT_CONFIG=$config" \
        "-DBUSTER_C_COMPILER=$clang" "${settings[@]}" -P "$root/cmake/BusterPrint.cmake" >&2
    cat "$output"
}

# The number of cores of the CPUs this process may run on, by their core_cpus_list, or 0 if /sys doesn't say: a build
# runs at most one compile per core, since two clang processes on a core's two threads slow each other down, the
# longest among them. On the 9700X before main.c's homes, its 15 threads compiling at once took 40.0 s and 331 s of
# CPU, and its 8 cores one compile each, the longest first, 32.3 s and 233 s; with them, 25.3 s and 200 s (#270).
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

# Sets the variable $1 to the microseconds since the epoch. Bash writes $EPOCHREALTIME's seconds and microseconds (six
# digits) apart by the locale's decimal point, a comma in es_ES, so they are its digits alone. This is the script's only
# reader of $EPOCHREALTIME, as .github/workflows/build.yml checks, with now() under such locales.
now()
{
    printf -v "$1" %s "${EPOCHREALTIME//[!0-9]/}"
}

case "$command" in
    print-sources|print-compile-flags|print-sanitizer-flags)
        [ "${#arguments[@]}" -eq 0 ] || die "$command takes no arguments"
        describe "${command#print-}" | tr '\n' '\0'
        exit 0
        ;;
    lexer-variant-assembly)
        # One lexer's assembly, with debug information, for label-spills to label and lexer-variant to build in.
        [ "${#arguments[@]}" -eq 3 ] && [[ "${arguments[0]}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || die "usage: ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s"
        source="$(realpath -e -- "$(absolute "${arguments[1]}")")" || die "lexer-variant-assembly: ${arguments[1]} doesn't exist"
        [[ "$source" != *[\"\\]* && "$source" != *$'\n'* ]] || die "lexer-variant-assembly: $source has a quote, backslash or newline"
        output="$(absolute "${arguments[2]}")"
        text="$(describe assembly-flags)"
        mapfile -t flags <<< "$text"
        cd "$root"
        "$clang" -S src/buster/compiler/c_lexer_variant.c -o "$output" "${flags[@]}" \
            -DBUSTER_LEXER_VARIANT_SOURCE="\"$source\"" -DBUSTER_LEXER_VARIANT_NAME="c_lexer_variant_${arguments[0]}"
        exit 0
        ;;
esac

ninja="$(command -v ninja)" || die "ninja (1.10 or later) isn't on PATH"
ninja_version="$("$ninja" --version)"
version_at_least "$ninja_version" 1.10 || die "ninja $ninja_version is older than 1.10"

variant="$(describe variant)"
lexer_variants=""
shift_source=""
shift_bytes=""
case "$command" in
    lexer-variant)
        # Other lexers in the executable, for benchmark-lexer to check and time beside c_tokenize in one process: each
        # SOURCE is a copy of c_lexer.c (compiled through src/buster/compiler/c_lexer_variant.c into
        # c_lexer_variant_NAME), or that file's assembly (.s), which lexer-variant-assembly writes, and label-spills
        # may label. They have a tree of their own, whose units all take the list of variants in place of
        # BUSTER_BUILD_DEFINES.
        [ "${#arguments[@]}" -gt 0 ] || die "usage: ./build.sh lexer-variant NAME=SOURCE..."
        tags=""
        for entry in "${arguments[@]}"; do
            name="${entry%%=*}"
            source="${entry#*=}"
            [[ "$entry" == *=* && "$name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ && -f "$(absolute "$source")" ]] || die "lexer-variant: $entry isn't NAME=SOURCE"
            source="$(realpath -e -- "$(absolute "$source")")"
            [[ "$source" != *[\"\\\;]* && "$source" != *$'\n'* ]] || die "lexer-variant: $source has a quote, backslash, semicolon or newline"
            if [[ "$source" == *.s ]]; then
                grep -q "^c_lexer_variant_$name:" "$source" || die "lexer-variant: $source doesn't define c_lexer_variant_$name; write it with ./build.sh lexer-variant-assembly $name"
                # Two labeled variants of one tag would share their symbols, and spill-report would mix their samples.
                tag="$(grep -m1 -o '^buster_spill\.[A-Za-z0-9_]*\.' "$source" || true)"
                if [ -n "$tag" ]; then
                    [[ " $tags " != *" $tag "* ]] || die "lexer-variant: $source is labeled with the tag of another variant (${tag:13:-1}); give label-spills each variant's own"
                    tags="$tags $tag"
                fi
            fi
            lexer_variants="$lexer_variants${lexer_variants:+;}$name=$source"
        done
        variant="$variant-lexer"
        ;;
    shifts)
        # The executable, then the same objects linked again into DIRECTORY/compiler-SHIFT for each SHIFT, a number of
        # bytes, with that much padding in .text before the objects of SOURCE (a source's file name), a multiple of its
        # .text's alignment: its code and what the linker places after it move by SHIFT, the same instructions at
        # other addresses. The same code ran up to 2% faster or slower so on the 9700X (#509), so time-runs compares
        # builds over several placements.
        [ "${#arguments[@]}" -ge 3 ] || die "usage: ./build.sh shifts DIRECTORY SOURCE SHIFT..."
        shift_directory="$(absolute "${arguments[0]}")"
        shift_source="${arguments[1]}"
        text="$(describe sources)"
        mapfile -t sources <<< "$text"
        found=0
        for source in "${sources[@]}"; do
            [ "${source##*/}" != "$shift_source" ] || found=1
        done
        [ "$found" = 1 ] || die "shifts: $shift_source isn't a source's file name"
        for bytes in "${arguments[@]:2}"; do
            [[ "$bytes" =~ ^(0|[1-9][0-9]{0,6})$ ]] || die "shifts: $bytes isn't a shift of 0 to 9,999,999 bytes"
            shift_bytes="$shift_bytes${shift_bytes:+;}$bytes"
        done
        mkdir -p -- "$shift_directory"
        ;;
    *)
        [ "$command" = test ] || [ "$command" = clean ] || [ "${#arguments[@]}" -eq 0 ] || die "$command takes no arguments"
        ;;
esac

mkdir -p "$root/build/cmake"
tree="$root/build/cmake/$variant"
binary="$tree/$config/compiler"

if [ "$command" = print-output ]; then
    printf '%s\n' "$binary"
    exit 0
fi

if [ "$command" = clean ]; then
    # Explicit cleaning: --all removes every tree and the published files; --config, that configuration's outputs in
    # this settings' tree; neither, this settings' tree.
    exec {publication}> "$root/build/.publish.lock"
    flock "$publication"
    if [ "$clean_all" = 1 ]; then
        rm -rf -- "$root/build/cmake" "$root/build/compiler" "$root/build/compile_commands.json"
        echo "build.sh: removed every tree under build/cmake and the published build/compiler" >&2
    elif [ -d "$tree" ]; then
        exec {lock}> "$tree.lock"
        flock "$lock"
        for configuration in Debug Release; do
            if [ "$config_given" = 0 ] || [ "$config" = "$configuration" ]; then
                if [ "$root/build/compiler" -ef "$tree/$configuration/compiler" ]; then
                    rm -f -- "$root/build/compiler" "$root/build/compile_commands.json"
                fi
            fi
        done
        if [ "$config_given" = 1 ]; then
            [ ! -f "$tree/build.ninja" ] || "$cmake" --build "$tree" --config "$config" --target clean
            echo "build.sh: cleaned ${tree#"$root/"}'s $config outputs" >&2
        else
            rm -rf -- "$tree"
            echo "build.sh: removed ${tree#"$root/"}" >&2
        fi
    fi
    exit 0
fi

# The tree's configuration, which CMake writes again only when it changed: the arguments, PATH (which finds the tools
# and linkers) and the toolchain's identity. A different clang can't reuse the tree's objects, nor CMake its cache, so
# the tree starts afresh.
configure_arguments=(
    -S "$root" -B "$tree" -G "Ninja Multi-Config"
    "-DCMAKE_MAKE_PROGRAM=$ninja"
    "-DCMAKE_C_COMPILER=$clang"
    "-DCMAKE_CONFIGURATION_TYPES=Debug;Release"
    -DCMAKE_DEFAULT_BUILD_TYPE=Release
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    # CMake would take these from the environment once, at the first configure, and keep them.
    -DCMAKE_C_COMPILER_LAUNCHER=
    -DCMAKE_C_LINKER_LAUNCHER=
    "${settings[@]}"
    "-DBUSTER_LEXER_VARIANTS=$lexer_variants"
    "-DBUSTER_SHIFT_SOURCE=$shift_source"
    "-DBUSTER_SHIFTS=$shift_bytes"
)
toolchain="clang $clang ($(realpath "$clang")): $("$clang" --version | sed -n 1p)"
stamp="$tree/build.sh.configuration"
wanted="$(printf '%s\n' "$toolchain" "cmake $cmake $cmake_version" "ninja $ninja $ninja_version" "PATH=$PATH" "${configure_arguments[@]}")"

exec {lock}> "$tree.lock"
if ! flock -n "$lock"; then
    echo "build.sh: waiting for another build of ${tree#"$root/"}" >&2
    flock "$lock"
fi
if [ -d "$tree" ]; then
    previous=""
    if [ -f "$stamp" ]; then
        previous="$(head -n 1 "$stamp")"
    elif [ -f "$tree/CMakeCache.txt" ]; then
        # A tree a preset configured: its compiler is the cache's.
        previous="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$tree/CMakeCache.txt")"
        [ "$previous" != "$clang" ] || previous="$toolchain"
    fi
    if [ -n "$previous" ] && [ "$previous" != "$toolchain" ]; then
        echo "build.sh: ${tree#"$root/"} was configured for another toolchain ($previous); configuring it afresh" >&2
        rm -rf -- "$tree"
    fi
fi
if [ "$command" = configure ] || [ ! -f "$stamp" ] || [ ! -f "$tree/build.ninja" ] || [ "$(cat "$stamp")" != "$wanted" ]; then
    rm -f -- "$stamp"
    "$cmake" "${configure_arguments[@]}" >&2
    printf '%s\n' "$wanted" > "$stamp"
fi
[ "$command" != configure ] || exit 0

if [ -z "$jobs" ]; then
    jobs="$(cores)"
    [ "$jobs" -ne 0 ] || jobs="$(nproc)"
fi

# Builds the targets "$@" of the configuration, at most `jobs` compiles at once, and reports the units it compiled and
# the longest, from Ninja's log of this run's commands.
build()
{
    local log="$tree/.ninja_log" offset=0 inode="" start end
    [ ! -f "$log" ] || { offset="$(stat -c %s "$log")"; inode="$(stat -c %i "$log")"; }
    local options=(--build "$tree" --config "$config" --parallel "$jobs")
    [ "$verbose" = 0 ] || options+=(--verbose)
    local target
    for target in "$@"; do
        options+=(--target "$target")
    done
    now start
    "$cmake" "${options[@]}" >&2
    now end
    # Ninja appends a line for each command it ran; when it rewrites its log to compact it, the report is skipped.
    if [ -f "$log" ] && { [ -z "$inode" ] || [ "$(stat -c %i "$log")" = "$inode" ]; }; then
        tail -c "+$((offset + 1))" "$log" | awk -F '\t' -v total="$((end - start))" -v config="$config" '
            $4 ~ /\.c\.o$/ { count += 1; name = $4; sub(/^.*\//, "", name); sub(/\.o$/, "", name); time[name] = $2 - $1 }
            END {
                line = sprintf("build.sh: %s: %u units compiled, the build took %.1f s", config, count, total / 1e6)
                n = 0
                for (name in time) { names[++n] = name }
                for (i = 1; i <= n && i <= 3; i += 1) {
                    best = i
                    for (j = i + 1; j <= n; j += 1) { if (time[names[j]] > time[names[best]]) { best = j } }
                    swap = names[i]; names[i] = names[best]; names[best] = swap
                    line = line sprintf("%s %s %.1f s", i == 1 ? "; the longest:" : ",", names[i], time[names[i]] / 1e3)
                }
                print line
            }' >&2
    fi
}

# Publishes the configuration's executable as build/compiler, a hard link to it (or a copy where the file system has
# none), renamed into place, and its compile commands as build/compile_commands.json, each only when it changed.
# Publications wait for each other.
publish()
{
    exec {publication}> "$root/build/.publish.lock"
    flock "$publication"
    if [ ! "$root/build/compiler" -ef "$binary" ]; then
        local next="$root/build/.compiler.$$"
        ln -f -- "$binary" "$next" 2> /dev/null || cp -f -- "$binary" "$next"
        mv -f -T -- "$next" "$root/build/compiler"
    fi
    local commands="$root/build/.compile_commands.json.$$"
    temporary+=("$commands")
    "$cmake" "-DINPUT=$tree/compile_commands.json" "-DOUTPUT=$commands" "-DCONFIG=$config" \
        -P "$root/cmake/BusterCompileCommands.cmake"
    if cmp -s -- "$commands" "$root/build/compile_commands.json"; then
        rm -f -- "$commands"
    else
        mv -f -T -- "$commands" "$root/build/compile_commands.json"
    fi
    flock -u "$publication"
    echo "build.sh: $config: ${binary#"$root/"}, published as build/compiler" >&2
}

case "$command" in
    build)
        if [ "${#targets[@]}" -eq 0 ]; then
            build
            publish
        else
            build "${targets[@]}"
            for target in "${targets[@]}"; do
                if [ "$target" = compiler ] || [ "$target" = all ]; then
                    publish
                    break
                fi
            done
        fi
        ;;
    test)
        build
        publish
        flock -u "$lock"
        ctest="${cmake%/*}/ctest"
        [ -x "$ctest" ] || ctest="$(command -v ctest)" || die "ctest isn't beside cmake or on PATH"
        "$ctest" --test-dir "$tree" -C "$config" --output-on-failure "${arguments[@]}"
        ;;
    check-call-graph)
        # The no-recursion rule on the call graph of the units' IR (CMakeLists.txt's call-graph-ir, captured at the
        # Release configuration's optimization whatever the configuration), checked by BUSTER_COMPILER, which the
        # parallel runner gives its commands, or else by the configuration's built executable.
        build call-graph-ir
        flock -u "$lock"
        "$cmake" "-DCOMPILER=${BUSTER_COMPILER:-$binary}" "-DIR=$tree/call-graph" -P "$root/cmake/BusterCallGraph.cmake"
        ;;
    lexer-variant)
        build compiler
        publish
        ;;
    shifts)
        build compiler shifts
        publish
        IFS=';' read -r -a shifted <<< "$shift_bytes"
        for bytes in "${shifted[@]}"; do
            output="$shift_directory/compiler-$bytes"
            ln -f -- "$tree/$config/shifts/compiler-$bytes" "$output" 2> /dev/null || cp -f -- "$tree/$config/shifts/compiler-$bytes" "$output"
            echo "build.sh shifts: $output: $shift_source's code $bytes bytes later" >&2
        done
        ;;
esac
