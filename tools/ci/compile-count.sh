#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The compile's count of a change against its base's (#2850; docs/commands/ci.md, "The compile's count before the
# merge"): each checkout's compiler built with clang's -fprofile-instr-generate, the frozen sqlite3.c compiled with
# each, and tools/ci/compile-count.c's verdict on the two raw profiles' counts. Hosted runners count no instructions,
# and no pull request reaches the 9700X; this count needs neither, and it is the same on any runner to a few parts in a
# million, so that a change is held to its own base on the runner it drew.
#
#   compile-count.sh check BASE HEAD CORPUS WORK
#       BASE and HEAD are checkouts of the change's base and of the change, CORPUS the frozen corpus
#       (tools/ci/fetch-corpus.sh), WORK a directory for the builds and the profiles. Prints the verdict, with
#       GitHub's annotation for a failure, and adds it to the step's summary. Exits as compile-count does: 0 passed,
#       1 the change moves the count and doesn't state it, or misstates it, 2 nothing could be judged.
#   compile-count.sh build TREE DIRECTORY
#       DIRECTORY/compiler, the instrumented executable of the checkout TREE: its own sources and Release flags
#       (./build.sh print-sources, print-compile-flags), compiled at -O0 without debug information. The counters are
#       the front end's, placed before any optimization, so -O0 counts what an optimized build counts (315,115,704 at
#       -O0 and at -O1, main 16ccdccd) and builds in an eighth of -O1's time. The executable is left unbaked, as
#       BUSTER_PGO's is: its counters are writable memory outside buster_baked.
#   compile-count.sh count EXECUTABLE CORPUS DIRECTORY NAME
#       DIRECTORY/NAME-1.profraw to NAME-3.profraw, three compiles' raw profiles, and NAME-one.profraw, a one-function
#       unit's. The unit is the corpus's sqlite3.c with the corpus's headers and predefines, on one lane: a compile's
#       parts on several threads lose counts to each other.
#
# BUSTER_COMPILE_COUNT_CLANG is the clang; otherwise CI's, as ./build.sh takes it (the reader refuses another raw
# profile version by its number). BUSTER_COMPILE_COUNT_JOBS is the compiles at once (the CPUs otherwise).
set -euo pipefail

die() { printf 'compile-count.sh: %s\n' "$*" >&2; exit 2; }
jobs=${BUSTER_COMPILE_COUNT_JOBS:-$(nproc)}
case "$jobs" in
    ''|*[!0-9]*|0*) die "BUSTER_COMPILE_COUNT_JOBS must be a positive number, not '$jobs'" ;;
esac
clang=${BUSTER_COMPILE_COUNT_CLANG:-}

# CI's clang where none is named, as ./build.sh takes it (#2628): PATH's where it is the release
# tools/ci/hosted-tools.lock pins, as on a hosted runner, or else the one tools/ci/session-toolchain.sh stages once in
# the user's cache.
toolchain()
{
    local tools identity version directories
    if [ -z "$clang" ]; then
        tools=$(cd -- "$(dirname -- "$0")" && pwd)
        identity=$(bash "$tools/session-toolchain.sh" identity) || die "can't read CI's toolchain from tools/ci/hosted-tools.lock"
        version=$(clang --version 2> /dev/null) || version=
        if [ "${version%%$'\n'*}" != "$identity" ]; then
            directories=$(bash "$tools/session-toolchain.sh" stage) || die "can't stage CI's toolchain; BUSTER_COMPILE_COUNT_CLANG names another clang"
            PATH="$directories:$PATH"
        fi
        clang=clang
    fi
}

build()
{
    local tree=$1 directory=$2 index=0 source object started=$SECONDS
    local -a sources flags objects=()
    [ -x "$tree/build.sh" ] || die "build: $tree is no checkout (no build.sh)"
    toolchain
    # Absolute: the compiles run in the checkout.
    tree=$(cd -- "$tree" && pwd)
    mkdir -p "$directory"
    directory=$(cd -- "$directory" && pwd)
    rm -rf -- "$directory/objects"
    mkdir -p "$directory/objects"
    # Written before they are read: a process substitution would hide the exporter's failure.
    (cd "$tree" && ./build.sh print-sources) > "$directory/sources.nul" || die "build: $tree's print-sources failed"
    (cd "$tree" && ./build.sh print-compile-flags --config Release) > "$directory/flags.nul" || die "build: $tree's print-compile-flags failed"
    mapfile -d '' -t sources < "$directory/sources.nul"
    mapfile -d '' -t flags < "$directory/flags.nul"
    [ "${#sources[@]}" -gt 0 ] && [ "${#flags[@]}" -gt 0 ] || die "build: $tree printed no sources or no flags"
    for source in "${sources[@]}"; do
        object="$directory/objects/$index.o"
        objects+=("$object")
        index=$((index + 1))
        # The last -O and -g decide: the Release flags' -O3 and -g give way.
        (cd "$tree" && exec "$clang" -c "$source" -o "$object" "${flags[@]}" -O0 -g0 -fprofile-instr-generate) > "$object.log" 2>&1 &
        while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do
            wait -n || true
        done
    done
    wait
    for object in "${objects[@]}"; do
        if [ ! -s "$object" ]; then
            cat "$object.log" >&2
            die "build: a unit of $tree didn't compile"
        fi
    done
    # The build's own link mode and libraries (cmake/BusterSettings.cmake's buster_link_mode, CMakeLists.txt): static
    # where the units were compiled as a static executable's, which their flags say (BUSTER_HOST_STATIC).
    local -a mode=(-no-pie)
    case " ${flags[*]} " in
        *' -DBUSTER_HOST_STATIC=1 '*) mode+=(-static -Wl,--wrap=__register_frame_info,--wrap=__deregister_frame_info) ;;
    esac
    if ! "$clang" -fprofile-instr-generate "${objects[@]}" -o "$directory/compiler" "${mode[@]}" \
        -Wl,--push-state,--as-needed -lm -Wl,--pop-state -ldl > "$directory/link.log" 2>&1; then
        cat "$directory/link.log" >&2
        die "build: $tree's executable didn't link"
    fi
    printf 'compile-count.sh: built %s from %s units in %s s\n' "$directory/compiler" "${#sources[@]}" "$((SECONDS - started))"
}

count()
{
    local executable=$1 corpus=$2 directory=$3 name=$4 run
    local -a unit=(--lanes 1 --predefines "$corpus/provenance/gnu17-pinned-predefines.txt" --target=x86_64-linux-gnu -march=znver5 -std=gnu17 -nostdinc
        -isystem "$corpus/headers/usr/lib/clang/23/include" -isystem "$corpus/headers/usr/local/include" -isystem "$corpus/headers/usr/include")
    local -a sqlite=(-c -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE "-I$corpus/sqlite" "$corpus/sqlite/sqlite3.c")
    [ -f "$corpus/sqlite/sqlite3.c" ] || die "count: $corpus has no sqlite/sqlite3.c"
    # The profile runtime expands a % in its file's name (%p, %h, %m): a directory that holds one names another file.
    case "$directory" in
        *%*) die "count: $directory holds a '%', which the raw profile's file name would expand" ;;
    esac
    mkdir -p "$directory"
    for run in 1 2 3; do
        rm -f -- "$directory/$name-$run.profraw"
        LLVM_PROFILE_FILE="$directory/$name-$run.profraw" "$executable" "${unit[@]}" "${sqlite[@]}" -o "$directory/$name.o" ||
            die "count: $executable didn't compile sqlite3.c"
        [ -s "$directory/$name-$run.profraw" ] || die "count: $executable wrote no raw profile (it isn't an instrumented build)"
    done
    # The process's start and the tables a real executable has baked, for a reader of a move to tell them from the
    # unit's work; nothing judges it.
    printf 'int main(void)\n{\n    return 0;\n}\n' > "$directory/one.c"
    rm -f -- "$directory/$name-one.profraw"
    LLVM_PROFILE_FILE="$directory/$name-one.profraw" "$executable" "${unit[@]}" -c "$directory/one.c" -o "$directory/$name-one.o" ||
        die "count: $executable didn't compile a one-function unit"
}

check()
{
    local base=$1 head=$2 corpus=$3 work=$4 status=0 one
    mkdir -p "$work"
    cc -std=c17 -O2 -Wall -Wextra -Werror -o "$work/compile-count" "$head/tools/ci/compile-count.c" || die "check: compile-count.c didn't build"
    build "$base" "$work/base"
    build "$head" "$work/head"
    count "$work/base/compiler" "$corpus" "$work" base
    count "$work/head/compiler" "$corpus" "$work" head
    "$work/compile-count" check --base-statement "$base/tools/ci/compile-count.txt" --head-statement "$head/tools/ci/compile-count.txt" \
        --base "$work/base-1.profraw" "$work/base-2.profraw" "$work/base-3.profraw" \
        --head "$work/head-1.profraw" "$work/head-2.profraw" "$work/head-3.profraw" > "$work/verdict.txt" || status=$?
    one=$("$work/compile-count" sum "$work/base-one.profraw" "$work/head-one.profraw" | cut -f 1 | paste -s -d ' ') || die "check: the one-function unit's profiles have no count"
    printf 'compile-count: a one-function unit counts %s at the base and %s at the change (the start and its tables, which the real executable has baked)\n' "${one% *}" "${one#* }" >> "$work/verdict.txt"
    cat "$work/verdict.txt"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        { printf '### The compile'"'"'s count against its base'"'"'s (#2850)\n\n```\n'; cat "$work/verdict.txt"; printf '```\n'; } >> "$GITHUB_STEP_SUMMARY"
    fi
    case "$status" in
        0) ;;
        1) printf '::error title=The compile'"'"'s count::%s\n' "$(grep -m1 '^compile-count: FAILED: ' "$work/verdict.txt" | cut -c 24-)" ;;
        *) printf '::error title=The compile'"'"'s count::It could not be judged (compile-count exited %s); see the step'"'"'s log.\n' "$status" ;;
    esac
    return "$status"
}

command=${1:-}
case "$command" in
    check) [ "$#" -eq 5 ] || die "usage: compile-count.sh check BASE HEAD CORPUS WORK"; check "$2" "$3" "$4" "$5" ;;
    build) [ "$#" -eq 3 ] || die "usage: compile-count.sh build TREE DIRECTORY"; build "$2" "$3" ;;
    count) [ "$#" -eq 5 ] || die "usage: compile-count.sh count EXECUTABLE CORPUS DIRECTORY NAME"; count "$2" "$3" "$4" "$5" ;;
    *) die "usage: compile-count.sh check BASE HEAD CORPUS WORK | build TREE DIRECTORY | count EXECUTABLE CORPUS DIRECTORY NAME" ;;
esac
