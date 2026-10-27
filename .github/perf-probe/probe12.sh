#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its twelfth round: six more generated constructs; nested switches up to
# depth 16,000 and an xxd-style byte table, each with its stages and its hottest source lines; Lua 5.4.7's files against
# tcc; and the source lines of -c sqlite3.c's hottest functions. Everything goes to standard output between "=== "
# headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real" "$work/inputs"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
perf="${PERF:-perf}"
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
    "$compiler" --time-stages "$@" 2>&1 > /dev/null | awk -F'\t' '$1 == "time-stage" && $2 != "total" && $2 != "stage" { print "  " $2, $3 " ms", $4 "%", ($7 != "" ? $7 " KB added" : "") }' |
        sort -k2 -n -r | head -n "${STAGES:-5}"
    "$compiler" --time-stages "$@" 2>&1 > /dev/null | awk -F'\t' '$1 == "time-process" && ($2 == "peak-rss-kb" || $2 == "user-ms" || $2 == "system-ms") { print "  " $2, $3 }'
}

# The hottest symbols of a recorded run, and the source lines of the first few.
hot()
{
    local data="$1"
    local symbols="$2"
    "$perf" report -q -i "$data" --no-children --sort symbol --percent-limit 1.5 --stdio 2> /dev/null | grep -v '^$' | head -n 16 | cut -c 1-110
    for symbol in $symbols; do
        echo "  --- lines of $symbol"
        timeout 300 "$perf" annotate -q -i "$data" --stdio -l -s "$symbol" 2> /dev/null |
            awk '/Sorted summary/ { p = 1; next } p && NF == 0 { exit } p' | head -n 12 | cut -c 1-150
    done
}

section "six more generated inputs, buster and tcc wall ms (median of 3)"
for entry in declarator-depth:1000 typedef-chain:2000 diagnostic-pragmas:1000 inline-asm:1000 offsetofs:2000 builtins-heavy:1000; do
    mode="${entry%%:*}"
    base="${entry#*:}"
    for scale in 1 2 4 8; do
        n=$((base * scale))
        file="$work/inputs/$mode-$n.c"
        "$work/generator" "$mode" "$n" > "$file"
        echo "gen $mode $n: buster $(wall_ms "$compiler" -c "$file" -o out.o) | tcc $(wall_ms "$tcc" -c "$file" -o ref.o)"
    done
    "$compiler" -c "$work/inputs/$mode-$base.c" -o out.o 2>&1 | grep -m1 -E 'error|fatal' | cut -c 1-160
    STAGES=3 stages -c "$work/inputs/$mode-$((base * 8)).c" -o out.o
done

section "nested switches"
for n in 1000 2000 4000 8000 16000; do
    file="$work/inputs/nested-switches-$n.c"
    "$work/generator" nested-switches "$n" > "$file"
    echo "nested-switches $n: buster $(wall_ms "$compiler" -c "$file" -o out.o) | tcc $(wall_ms "$tcc" -c "$file" -o ref.o)"
    STAGES=2 stages -c "$file" -o out.o
done
"$perf" record -q -o switches.data -F 9999 -- "$compiler" -c "$work/inputs/nested-switches-16000.c" -o out.o > /dev/null 2>&1
hot switches.data "check_input_run check_input_controls_mode"

section "an xxd-style byte table of 800,000 bytes"
"$work/generator" bytes 800000 > "$work/inputs/bytes.c"
echo "bytes 800000: buster $(wall_ms "$compiler" -c "$work/inputs/bytes.c" -o out.o) | tcc $(wall_ms "$tcc" -c "$work/inputs/bytes.c" -o ref.o)"
STAGES=8 stages -c "$work/inputs/bytes.c" -o out.o
"$perf" record -q -o bytes.data -F 9999 -- "$compiler" -c "$work/inputs/bytes.c" -o out.o > /dev/null 2>&1
hot bytes.data "check_input_run check_input_expressions check_init_walk check_graph_run"

section "Lua 5.4.7 against tcc"
curl -sSL --retry 3 -o "$work/real/lua.tar.gz" https://www.lua.org/ftp/lua-5.4.7.tar.gz && tar -xzf "$work/real/lua.tar.gz" -C "$work/real"
for file in "$work"/real/lua-5.4.7/src/*.c; do
    name="$(basename "$file")"
    if ! "$tcc" -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file" -o ref.o > tcc.log 2>&1; then
        echo "$name: tcc failed: $(head -c 120 tcc.log)"
        continue
    fi
    if ! "$compiler" -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file" -o out.o > buster.log 2>&1; then
        echo "$name: buster failed: $(grep -m1 error buster.log | cut -c 1-140)"
        continue
    fi
    echo "$name: buster $(wall_ms "$compiler" -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file" -o out.o) | tcc $(wall_ms "$tcc" -c -DLUA_COMPAT_5_3 -DLUA_USE_LINUX "$file" -o ref.o)"
done

section "the source lines of -c sqlite3.c's hottest functions"
curl -sSL --retry 3 -o "$work/real/sqlite.zip" https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$work/real/sqlite.zip" "$work/real"
sqlite="$work/real/sqlite-amalgamation-3530200/sqlite3.c"
"$perf" record -q -o compile.data -F 19999 -- taskset -c 1 "$compiler" --lanes 1 -c "$sqlite" -o sqlite.o > /dev/null 2>&1
hot compile.data "check_input_expressions check_program check_input_run check_input_resolve_range check_lower_part tier0_generate check_graph_run check_control_drain"
section "done"
