#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The front end of the CMake build (CMakeLists.txt, CMakePresets.json; docs/commands/build.md has the details):
#
#   ./build.sh [build] [--config Debug|Release|Develop] [--target TARGET]... [--jobs N] [--verbose]
#   ./build.sh test [--config Debug|Release|Develop] [--jobs N] [-- CTEST_ARGUMENT...]
#   ./build.sh clean [--config Debug|Release|Develop | --all]
#   ./build.sh configure
#   ./build.sh print-sources | print-compile-flags [--config C] | print-sanitizer-flags | print-host-flags | print-output [--config C]
#   ./build.sh check-call-graph [--config C] [--jobs N]
#   ./build.sh lexer-variant [--config C] [--jobs N] NAME=SOURCE...
#   ./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s
#   ./build.sh shifts [--config C] [--jobs N] DIRECTORY SOURCE SHIFT...
#
# It builds with CI's toolchain, the official LLVM release, CMake and Ninja that tools/ci/hosted-tools.lock pins: PATH's
# when its clang is the release's, or else tools/ci/session-toolchain.sh's, staged once in the user's cache (#2628);
# BUSTER_TOOLCHAIN=path builds with PATH's as found.
#
# A build configures build/cmake/VARIANT, Ninja Multi-Config's tree for the BUSTER_* environment's host ISA,
# runtime, sanitizer, fuzzing and debug information, only when the settings, PATH or the toolchain changed since it
# last did (a changed clang starts the tree afresh), then has Ninja build what changed: an unchanged build compiles,
# links and bakes nothing. It builds Develop (Release's optimization in parts, for the edit loop; the owner's default
# since #2559) unless --config says Release or Debug; Release is the configuration every timing and benchmark measures,
# and the default of the commands that exist to measure (shifts, lexer-variant) and of a BUSTER_PGO tree, which has it
# alone. It runs at most one compile per core of the CPUs it may run on, and when it builds the executable,
# build/cmake/VARIANT/CONFIG/compiler, it publishes that file as build/compiler (a hard link, the same file) and its
# configuration's compile commands as build/compile_commands.json, once the build has succeeded. Builds of one tree,
# and publications, wait for each other.
#
# print-compile-flags, print-sanitizer-flags, print-host-flags and print-sources print expanded arguments and source
# paths as NUL-delimited argv, without building (cmake/BusterPrint.cmake); consumers read arrays rather than reevaluating
# shell text from this script. print-host-flags is the host target alone: -march=BUSTER_HOST_ARCH, or the explicit
# vector target's flags for BUSTER_HOST_ARCH=vector, which commands that compile for the build's host pass to clang and
# to the compiler. print-output prints the configuration's executable's path.
set -euo pipefail
# Command substitutions fail as the commands in them do: a failed description stops the script.
shopt -s inherit_errexit

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
    build|test|clean|configure|print-sources|print-compile-flags|print-sanitizer-flags|print-host-flags|print-output|check-call-graph|lexer-variant|lexer-variant-assembly|shifts)
        command="$1"
        shift
        ;;
    help|-h|--help)
        usage
        ;;
esac
config=""
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
        -j?*) jobs="${1#-j}"; shift ;;
        --verbose|-v) verbose=1; shift ;;
        --all) clean_all=1; shift ;;
        --) shift; arguments+=("$@"); break ;;
        -*) die "unknown option $1; ./build.sh --help lists the commands" ;;
        *) arguments+=("$1"); shift ;;
    esac
done
# The default configuration: Develop for the edit loop; Release for the commands that measure and for BUSTER_PGO.
if [ "$config_given" = 0 ]; then
    case "$command" in
        shifts|lexer-variant) config=Release ;;
        *) [ "${BUSTER_PGO:-0}" = 1 ] && config=Release || config=Develop ;;
    esac
fi
case "$config" in
    Debug|debug) config=Debug ;;
    Release|release) config=Release ;;
    Develop|develop) config=Develop ;;
    *) die "--config must be Debug, Release or Develop" ;;
esac
[ -z "$jobs" ] || [[ "$jobs" =~ ^[1-9][0-9]{0,3}$ ]] || die "--jobs must be a positive number"
# Each command's own options: the others would be ignored silently.
case "$command" in
    build) [ "${#arguments[@]}" -eq 0 ] || die "build takes no arguments: ${arguments[*]}" ;;
    test) ;;
    clean) [ "${#arguments[@]}" -eq 0 ] && { [ "$clean_all" = 0 ] || [ "$config_given" = 0 ]; } || die "usage: ./build.sh clean [--config Debug|Release|Develop | --all]" ;;
    *) [ "$clean_all" = 0 ] || die "--all is clean's option" ;;
esac
case "$command" in
    build) ;;
    *) [ "${#targets[@]}" -eq 0 ] && [ "$verbose" = 0 ] || die "--target and --verbose are build's options" ;;
esac

# The toolchain (#2628): CI's unless BUSTER_TOOLCHAIN=path, which takes PATH's as found. CI's is the official LLVM
# release, CMake and Ninja that tools/ci/hosted-tools.lock pins: PATH's when its clang is the release's (CI's own setup
# puts them first), or else tools/ci/session-toolchain.sh's, staged once in the user's cache and put first on PATH.
toolchain_identity="$(bash "$root/tools/ci/session-toolchain.sh" identity)" || die "can't read CI's toolchain from tools/ci/hosted-tools.lock"
# The first line of $1's --version, or nothing.
first_line()
{
    local text
    text="$("$1" --version 2> /dev/null)" || return 0
    printf '%s\n' "${text%%$'\n'*}"
}
case "${BUSTER_TOOLCHAIN:-ci}" in
    ci)
        if [ "$(first_line clang)" != "$toolchain_identity" ]; then
            directories="$(bash "$root/tools/ci/session-toolchain.sh" stage)" || die "can't stage CI's toolchain; BUSTER_TOOLCHAIN=path builds with PATH's"
            PATH="$directories:$PATH"
            [ "$(first_line clang)" = "$toolchain_identity" ] || die "the staged clang isn't CI's ($toolchain_identity)"
        fi
        ;;
    path) ;;
    *) die "BUSTER_TOOLCHAIN must be ci or path" ;;
esac

# The tools. CMake 3.25 has every feature CMakeLists.txt and CMakePresets.json use; Ninja Multi-Config needs Ninja 1.10.
clang="$(command -v clang)" || die "clang isn't on PATH"
cmake="$(command -v cmake)" || die "cmake (3.25 or later) isn't on PATH"
version_at_least()
{
    local major minor minimum_major="${2%%.*}" minimum_minor="${2#*.}"
    [[ "$1" =~ ^([0-9]+)\.([0-9]+) ]] || return 1
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    ((10#$major > 10#$minimum_major || (10#$major == 10#$minimum_major && 10#$minor >= 10#$minimum_minor)))
}
cmake_version="$("$cmake" --version)"
cmake_version="${cmake_version%%$'\n'*}"
[[ "$cmake_version" =~ ^cmake\ version\ ([0-9][0-9.]*) ]] || die "couldn't read cmake's version: $cmake_version"
cmake_version="${BASH_REMATCH[1]}"
version_at_least "$cmake_version" 3.25 || die "cmake $cmake_version is older than 3.25"

# The settings: the environment's BUSTER_* variables, each passed explicitly, so that unsetting one restores its
# default rather than leaving a tree's cached value. An unset or empty one is its default, but BUSTER_DEBUG_INFO and
# BUSTER_LINKER, for which an empty value is an error (cmake/BusterSettings.cmake checks them all).
fuzz_entry="${BUSTER_FUZZ_ENTRY:-}"
[ -z "$fuzz_entry" ] || fuzz_entry="$(absolute "$fuzz_entry")"
host_runtime="${BUSTER_HOST_RUNTIME:-}"
[ -z "$host_runtime" ] || host_runtime="$(absolute "$host_runtime")"
pgo_train="${BUSTER_PGO_TRAIN:-}"
[ -z "$pgo_train" ] || pgo_train="$(absolute "$pgo_train")"
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
    "-DBUSTER_PGO=${BUSTER_PGO:-0}"
    "-DBUSTER_PGO_TRAIN=$pgo_train"
    "-DBUSTER_PARALLEL_CODEGEN=${BUSTER_PARALLEL_CODEGEN:-auto}"
)

# The long commands (configuring, building, testing, checking) run in a process group of their own, `child`, which an
# interrupt or termination of this script goes on to: a signal for this script alone, as `timeout` sends, stops them at
# once rather than once they end. Ninja stops its commands when it stops.
child=""
stop()
{
    if [ -n "$child" ]; then
        kill "-$1" -- "-$child" 2> /dev/null || true
        wait "$child" 2> /dev/null || true
    fi
    exit "$2"
}
trap 'stop INT 130' INT
trap 'stop TERM 143' TERM
run()
{
    local status=0
    set -m
    "$@" &
    child=$!
    set +m
    wait "$child" || status=$?
    child=""
    return "$status"
}

# The description's $1 (cmake/BusterPrint.cmake), one argument per line. Every caller captures a pipe, so the script
# can write straight to stdout, once after successful validation, without a temporary file and another copy.
describe()
{
    "$cmake" "-DBUSTER_PRINT=$1" -DBUSTER_PRINT_OUTPUT=/dev/stdout "-DBUSTER_PRINT_CONFIG=$config" \
        "-DBUSTER_C_COMPILER=$clang" "${settings[@]}" -P "$root/cmake/BusterPrint.cmake"
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
    print-sources|print-compile-flags|print-sanitizer-flags|print-host-flags)
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
        # The variants change main.c's code, which the profile of the compiler without them would no longer match.
        [ "${BUSTER_PGO:-0}" = 0 ] || die "lexer-variant: BUSTER_PGO builds build/compiler and the shifts' executables alone"
        # Other lexers in the executable, for benchmark-lexer to check and time beside c_tokenize in one process: each
        # SOURCE is a copy of c_lexer.c (compiled through src/buster/compiler/c_lexer_variant.c into
        # c_lexer_variant_NAME), or that file's assembly (.s), which lexer-variant-assembly writes, and label-spills
        # may label. They have a tree of their own, whose units all take the list of variants in place of
        # BUSTER_BUILD_DEFINES.
        [ "${#arguments[@]}" -gt 0 ] || die "usage: ./build.sh lexer-variant NAME=SOURCE..."
        tags=""
        # CMakeLists.txt checks the names and paths; this resolves each path from the caller's directory and checks an
        # assembly's labels, which can change between builds.
        for entry in "${arguments[@]}"; do
            name="${entry%%=*}"
            source="${entry#*=}"
            [[ "$entry" == *=* && -f "$(absolute "$source")" ]] || die "lexer-variant: $entry isn't NAME=SOURCE"
            source="$(realpath -e -- "$(absolute "$source")")"
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
        # CMakeLists.txt checks the source's name and the shifts; a shift given twice is linked once.
        [ "${#arguments[@]}" -ge 3 ] || die "usage: ./build.sh shifts DIRECTORY SOURCE SHIFT..."
        shift_directory="$(absolute "${arguments[0]}")"
        shift_source="${arguments[1]}"
        for bytes in "${arguments[@]:2}"; do
            [[ ";$shift_bytes;" == *";$bytes;"* ]] || shift_bytes="$shift_bytes${shift_bytes:+;}$bytes"
        done
        mkdir -p -- "$shift_directory"
        ;;
    *)
        [ "$command" = test ] || [ "$command" = clean ] || [ "${#arguments[@]}" -eq 0 ] || die "$command takes no arguments"
        ;;
esac

# BUSTER_PGO's tree has the Release configuration alone, which its profile is for; the others' default is Develop.
configurations="Debug;Release;Develop"
default_config=Develop
if [ "${BUSTER_PGO:-0}" = 1 ]; then
    [ "$config" = Release ] || die "BUSTER_PGO builds the Release configuration alone"
    configurations=Release
    default_config=Release
fi
tree="$root/build/cmake/$variant"
binary="$tree/$config/compiler"

if [ "$command" = print-output ]; then
    printf '%s\n' "$binary"
    exit 0
fi

# Takes descriptor $1's lock with flock's option $2 (-s shared, -x exclusive), at once when it is free, or else after
# saying it waits for $3, in the background, so that a signal stops the wait at once; the lock is the descriptor's.
hold()
{
    if ! flock -n "$2" "$1"; then
        echo "build.sh: waiting for $3" >&2
        run flock "$2" "$1"
    fi
}

# The locks, always taken in this order, so that no two commands wait for each other: build/.trees.lock, which every
# command on one tree holds shared as long as it holds the tree's and clean --all exclusively; the tree's,
# build/cmake/VARIANT.lock, which its builds and cleans hold in turn; and build/.publish.lock, around a publication or a
# clean of the published files. So clean --all removes no tree a command is using, nor a lock file one has open.
mkdir -p "$root/build"
exec {trees}> "$root/build/.trees.lock"
if [ "$command" = clean ] && [ "$clean_all" = 1 ]; then
    # Every tree and the published files: nothing else can be publishing, since a publication needs a tree's lock.
    hold "$trees" -x "the other builds and cleans to end"
    rm -rf -- "$root/build/cmake" "$root/build/compiler" "$root/build/compile_commands.json"
    echo "build.sh: removed every tree under build/cmake and the published build/compiler" >&2
    exit 0
fi
hold "$trees" -s "a clean --all to end"
mkdir -p "$root/build/cmake"
exec {lock}> "$tree.lock"
hold "$lock" -x "another build or clean of ${tree#"$root/"}"

if [ "$command" = clean ]; then
    # Explicit cleaning: --config removes that configuration's outputs in this settings' tree; else, the whole tree.
    if [ -d "$tree" ]; then
        exec {publication}> "$root/build/.publish.lock"
        flock "$publication"
        for configuration in Debug Release Develop; do
            if [ "$config_given" = 0 ] || [ "$config" = "$configuration" ]; then
                if [ "$root/build/compiler" -ef "$tree/$configuration/compiler" ]; then
                    rm -f -- "$root/build/compiler" "$root/build/compile_commands.json"
                fi
            fi
        done
        if [ "$config_given" = 1 ]; then
            # The configuration's objects and outputs alone: Ninja's clean of one configuration would take the outputs
            # both share too (the call graph's IR, the lexer variants' assembly), whose edges are in each one's file.
            rm -rf -- "$tree/$config" "$tree"/CMakeFiles/*.dir/"$config"
            echo "build.sh: cleaned ${tree#"$root/"}'s $config outputs" >&2
        else
            rm -rf -- "$tree"
            echo "build.sh: removed ${tree#"$root/"}" >&2
        fi
    fi
    exit 0
fi

# The tree's configuration, which CMake writes again only when it changed: the arguments, the tools and linkers PATH
# finds for it, and the toolchain's identity. A different clang (its path, its file or its version) can't reuse the
# tree's objects, nor CMake its cache, so the tree starts afresh.
configure_arguments=(
    -S "$root" -B "$tree" -G "Ninja Multi-Config"
    "-DCMAKE_MAKE_PROGRAM=$ninja"
    "-DCMAKE_C_COMPILER=$clang"
    "-DCMAKE_CONFIGURATION_TYPES=$configurations"
    "-DCMAKE_DEFAULT_BUILD_TYPE=$default_config"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    # CMake would take these from the environment once, at the first configure, and keep them.
    -DCMAKE_C_COMPILER_LAUNCHER=
    -DCMAKE_C_LINKER_LAUNCHER=
    "${settings[@]}"
    "-DBUSTER_LEXER_VARIANTS=$lexer_variants"
    "-DBUSTER_SHIFT_SOURCE=$shift_source"
    "-DBUSTER_SHIFTS=$shift_bytes"
)
clang_version="$("$clang" --version)"
toolchain="clang $clang ($(realpath "$clang")): ${clang_version%%$'\n'*}"
tools="$(command -v nm llvm-nm llvm-split llvm-objcopy readelf llvm-readelf wild mold ld.lld ld.bfd | tr '\n' ' ' || true)"
stamp="$tree/build.sh.configuration"
wanted="$(printf '%s\n' "$toolchain" "cmake $cmake $cmake_version" "ninja $ninja $ninja_version" "tools $tools" "${configure_arguments[@]}")"

recorded=""
if [ -d "$tree" ]; then
    previous=""
    if [ -f "$stamp" ]; then
        recorded="$(<"$stamp")"
        previous="${recorded%%$'\n'*}"
    elif [ -f "$tree/buster-toolchain.txt" ]; then
        # A tree a preset configured: CMakeLists.txt records the same identity of the clang it configured with.
        previous="$(<"$tree/buster-toolchain.txt")"
        previous="${previous%%$'\n'*}"
    elif [ -e "$tree" ]; then
        previous="an unknown one"
    fi
    if [ -n "$previous" ] && [ "$previous" != "$toolchain" ]; then
        echo "build.sh: ${tree#"$root/"} was configured for another toolchain ($previous); configuring it afresh" >&2
        rm -rf -- "$tree"
    fi
fi
if [ "$command" = configure ] || [ ! -f "$stamp" ] || [ ! -f "$tree/build.ninja" ] || [ "$recorded" != "$wanted" ]; then
    if [ "${clang_version%%$'\n'*}" = "$toolchain_identity" ]; then
        echo "build.sh: configuring ${tree#"$root/"} with CI's toolchain: $toolchain" >&2
    else
        echo "build.sh: configuring ${tree#"$root/"} with PATH's toolchain (BUSTER_TOOLCHAIN=path), not CI's: $toolchain" >&2
    fi
    rm -f -- "$stamp"
    run "$cmake" "${configure_arguments[@]}" >&2
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
    run "$cmake" "${options[@]}" >&2
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
    # Both files are made first, so that a failure leaves the published ones as they were.
    local commands="$root/build/.compile_commands.json.$$" next="$root/build/.compiler.$$" changed=0
    # Ninja updates this configuration's database when its inputs change. Copy, never hard-link: the next build
    # may rewrite its cached database before a later compile fails, which must leave the published files intact.
    if ! cmp -s -- "$tree/$config/compile_commands.json" "$root/build/compile_commands.json"; then
        cp -f -- "$tree/$config/compile_commands.json" "$commands" || { rm -f -- "$commands"; return 1; }
        changed=1
    fi
    if [ ! "$root/build/compiler" -ef "$binary" ]; then
        ln -f -- "$binary" "$next" 2> /dev/null || cp -f -- "$binary" "$next" || { rm -f -- "$commands" "$next"; return 1; }
        mv -f -T -- "$next" "$root/build/compiler"
    fi
    if [ "$changed" = 1 ]; then
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
        flock -u "$trees"
        ctest="${cmake%/*}/ctest"
        [ -x "$ctest" ] || ctest="$(command -v ctest)" || die "ctest isn't beside cmake or on PATH"
        run "$ctest" --test-dir "$tree" -C "$config" --output-on-failure "${arguments[@]}"
        ;;
    check-call-graph)
        # The no-recursion rule on the call graph of the units' IR (CMakeLists.txt's call-graph-ir, captured at the
        # Release configuration's optimization whatever the configuration), checked by BUSTER_COMPILER, which the
        # parallel runner gives its commands, or else by the configuration's built executable, which must be there first.
        checker="${BUSTER_COMPILER:-$binary}"
        [ -x "$checker" ] || die "check-call-graph: $checker is missing; run ./build.sh first"
        build call-graph-ir
        flock -u "$lock"
        flock -u "$trees"
        run "$cmake" "-DCOMPILER=$checker" "-DIR=$tree/call-graph" -P "$root/cmake/BusterCallGraph.cmake"
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
