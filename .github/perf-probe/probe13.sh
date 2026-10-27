#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its thirteenth round: one builtin at a time at four sizes (which of
# them makes builtins-heavy code cost 18 us a call), and popular single-file C libraries against tcc (or clang -O0 where
# tcc refuses them): their times, their stages and their failures. Everything goes to standard output between "=== "
# headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real" "$work/inputs"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
clang_bin="$(command -v clang-23)"
cd "$work"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep -E 'Model name'
cc -std=c17 -O2 -o "$work/generator" "$root/.github/perf-probe/gen.c"

wall_ms()
{
    local times=()
    local status=0
    for run in 1 2 3; do
        local start="${EPOCHREALTIME/./}"
        "$@" > /dev/null 2>&1
        status=$?
        local end="${EPOCHREALTIME/./}"
        times+=($(((end - start) / 1000)))
    done
    printf '%s\n' "${times[@]}" | sort -n | sed -n 2p | tr '\n' ' '
    echo "status $status"
}

stages()
{
    "$compiler" --time-stages "$@" 2>&1 > /dev/null | awk -F'\t' '$1 == "time-stage" && $2 != "total" && $2 != "stage" { print "  " $2, $3 " ms", $4 "%" }' |
        sort -k2 -n -r | head -n "${STAGES:-4}"
}

section "one builtin at a time, buster and tcc wall ms (median of 3)"
for builtin in expect popcount clz ctz bswap overflow constant abs; do
    for n in 1000 2000 4000 8000; do
        file="$work/inputs/builtin-$builtin-$n.c"
        "$work/generator" "builtin-$builtin" "$n" > "$file"
        echo "builtin-$builtin $n: buster $(wall_ms "$compiler" -c "$file" -o out.o) | tcc $(wall_ms "$tcc" -c "$file" -o ref.o)"
    done
    "$compiler" -c "$work/inputs/builtin-$builtin-8000.c" -o out.o 2>&1 | grep -m1 -E 'error|fatal' | cut -c 1-200
    STAGES=3 stages -c "$work/inputs/builtin-$builtin-4000.c" -o out.o
done
"$work/generator" builtins-heavy 8000 > "$work/inputs/builtins-heavy-8000.c"
echo "builtins-heavy 8000: $("$compiler" -c "$work/inputs/builtins-heavy-8000.c" -o out.o 2>&1 | grep -m1 -E 'error|fatal' | cut -c 1-200)"
"$work/generator" builtins-heavy 4000 > "$work/inputs/builtins-heavy-4000.c"
STAGES=5 stages -c "$work/inputs/builtins-heavy-4000.c" -o out.o

fetch()
{
    curl -sSL --retry 3 -o "$1" "$2" || echo "fetch failed: $2"
}

section "single-file libraries"
cd "$work/real"
fetch lz4.c https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.c
fetch lz4.h https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.h
fetch yyjson.c https://raw.githubusercontent.com/ibireme/yyjson/0.10.0/src/yyjson.c
fetch yyjson.h https://raw.githubusercontent.com/ibireme/yyjson/0.10.0/src/yyjson.h
fetch miniz.zip https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' miniz.zip miniz
fetch duktape.tar.xz https://github.com/svaarala/duktape/releases/download/v2.7.0/duktape-2.7.0.tar.xz
tar -xJf duktape.tar.xz
fetch nuklear.h https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/master/nuklear.h
printf '#define NK_IMPLEMENTATION\n#define NK_INCLUDE_DEFAULT_ALLOCATOR\n#define NK_INCLUDE_STANDARD_IO\n#define NK_INCLUDE_STANDARD_VARARGS\n#include "nuklear.h"\n' > unit_nuklear.c
fetch xxhash.h https://raw.githubusercontent.com/Cyan4973/xxHash/v0.8.2/xxhash.h
printf '#define XXH_IMPLEMENTATION\n#define XXH_STATIC_LINKING_ONLY\n#include "xxhash.h"\n' > unit_xxhash.c
for file in lz4.c yyjson.c miniz/miniz.c duktape-2.7.0/src/duktape.c unit_nuklear.c unit_xxhash.c; do
    [ -f "$file" ] || { echo "$file: missing"; continue; }
    directory="$(dirname "$file")"
    reference="$tcc"
    if ! "$tcc" -c "-I$directory" "$file" -o ref.o > ref.log 2>&1; then
        echo "$file: tcc refused ($(head -c 100 ref.log | tr '\n' ' ')), clang -O0 instead"
        reference="$clang_bin"
        "$clang_bin" -O0 -c "-I$directory" "$file" -o ref.o > ref.log 2>&1 || { echo "  clang refused too: $(head -c 160 ref.log | tr '\n' ' ')"; reference=""; }
    fi
    if ! "$compiler" -c "-I$directory" "$file" -o out.o > buster.log 2>&1; then
        echo "$file ($(wc -c < "$file") bytes): buster failed: $(grep -m1 -E 'error|fatal' buster.log | cut -c 1-220)"
        continue
    fi
    if [ -n "$reference" ]; then
        echo "$file ($(wc -c < "$file") bytes): buster $(wall_ms "$compiler" -c "-I$directory" "$file" -o out.o) | $(basename "$reference") $(wall_ms "$reference" $([ "$reference" = "$clang_bin" ] && echo -O0) -c "-I$directory" "$file" -o ref.o)"
    else
        echo "$file: buster $(wall_ms "$compiler" -c "-I$directory" "$file" -o out.o)"
    fi
    STAGES=5 stages -c "-I$directory" "$file" -o out.o
done
section "done"
