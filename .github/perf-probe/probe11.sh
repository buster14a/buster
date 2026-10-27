#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its eleventh round: 29 more generated constructs at four sizes against
# tcc (deep nesting, long chains, the preprocessor's edges, diagnostics in bulk, the backend's rarer classes), Lua
# 5.4.7's files against tcc, and the source lines that the hottest functions of -c sqlite3.c spend their samples on.
# Everything goes to standard output between "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real" "$work/inputs"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
perf="${PERF:-perf}"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep -E 'Model name'
cc -std=c17 -O2 -o "$work/generator" "$root/.github/perf-probe/gen.c"

# The median of three runs' wall milliseconds of "$@", and its exit status.
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

section "generated inputs, buster and tcc wall ms (median of 3)"
modes=(enum-chain:2000 right-deep:500 left-deep:2000 nested-blocks:1000 nested-ifs:1000 nested-loops:1000 case-ranges:2000
    if-expression:1000 one-line:50000 splice-macro:10000 utf8-identifiers:5000 syntax-errors:2000 semantic-errors:2000
    nested-calls:1000 unary-chain:2000 cast-chain:2000 index-chain:1000 assign-chain:1000 many-args:1000
    long-double-ops:2000 int128-ops:2000 double-ops:2000 atomic-ops:2000 struct-returns:1000 kr-functions:2000
    pointer-depth:500 function-pointer-parameters:500 nested-switches:500 stringify:10000)
for entry in "${modes[@]}"; do
    mode="${entry%%:*}"
    base="${entry#*:}"
    for scale in 1 2 4 8; do
        n=$((base * scale))
        file="$work/inputs/$mode-$n.c"
        "$work/generator" "$mode" "$n" > "$file"
        buster="$(wall_ms "$compiler" -c "$file" -o "$work/out.o")"
        reference="$(wall_ms "$tcc" -c "$file" -o "$work/ref.o")"
        echo "gen $mode $n ($(wc -c < "$file") bytes): buster $buster | tcc $reference"
    done
    "$compiler" -c "$work/inputs/$mode-$base.c" -o "$work/out.o" 2>&1 | grep -m1 -E 'error|fatal' | cut -c 1-160
done

section "stages of the largest of each superlinear-looking mode"
for entry in "${modes[@]}"; do
    mode="${entry%%:*}"
    n=$((${entry#*:} * 8))
    file="$work/inputs/$mode-$n.c"
    if "$compiler" -c "$file" -o "$work/out.o" > /dev/null 2>&1; then
        echo "--- $mode $n"
        "$compiler" --time-stages -c "$file" -o "$work/out.o" 2>&1 > /dev/null | awk -F'\t' '$1 == "time-stage" && $2 != "total" && $2 != "stage" { print $2, $3, $4 "%" }' |
            sort -k2 -n -r | head -n 4
    fi
done

section "Lua 5.4.7 against tcc"
curl -sSL --retry 3 -o real/lua.tar.gz https://www.lua.org/ftp/lua-5.4.7.tar.gz && tar -xzf real/lua.tar.gz -C real
for file in real/lua-5.4.7/src/*.c; do
    if ! "$tcc" -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file" -o ref.o > tcc.log 2>&1; then
        echo "$(basename "$file"): tcc failed"
        continue
    fi
    if ! "$compiler" -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file" -o out.o > buster.log 2>&1; then
        echo "$(basename "$file"): buster failed: $(grep -m1 error buster.log | cut -c 1-140)"
        continue
    fi
    line="$("$compiler" measure-compile --runs 5 --warmups 1 --reference "$tcc" -- -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file")"
    echo "$(basename "$file"): $(grep 'against reference' <<< "$line" | sed 's/default against reference: //')"
    awk '$2 ~ /^[0-9.]+$/ && $1 != "span" && $1 != "outside" { print }' <<< "$line" | sort -k2 -n -r | head -n 3
done

section "the source lines of -c sqlite3.c's hottest functions"
curl -sSL --retry 3 -o real/sqlite.zip https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' real/sqlite.zip real
sqlite="$work/real/sqlite-amalgamation-3530200/sqlite3.c"
"$perf" record -q -o "$work/compile.data" -F 19999 -- taskset -c 1 "$compiler" --lanes 1 -c "$sqlite" -o "$work/sqlite.o" > /dev/null 2>&1
"$perf" report -q -i "$work/compile.data" --no-children --sort symbol --percent-limit 1 --stdio 2> /dev/null | grep -v '^$' | head -n 30 | cut -c 1-120
for symbol in check_input_expressions check_program check_input_run check_input_resolve_range check_lower_part tier0_generate \
    check_graph_run check_control_drain check_lower_drain check_driver_reports check_input_declarator_step check_data_prepare; do
    echo "--- $symbol"
    timeout 300 "$perf" annotate -q -i "$work/compile.data" --stdio -l -s "$symbol" 2> /dev/null |
        awk '/Sorted summary/ { p = 1; next } p && NF == 0 { exit } p' | head -n 14 | cut -c 1-160
done
section "done"
