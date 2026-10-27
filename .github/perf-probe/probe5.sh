#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its fifth round: more generated constructs at four sizes against tcc,
# -g's growth, and the assembler's against GNU as. Everything goes to standard output between "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"

section()
{
    echo
    echo "=== $*"
}

measure()
{
    local runs="$1" reference="$2"
    shift 2
    if [ -n "$reference" ] && "$reference" "$@" -o "$work/reference.o" > "$work/reference.log" 2>&1; then
        "$compiler" measure-compile --runs "$runs" --warmups 1 --reference "$reference" -- "$@"
    else
        echo "reference: none ($(head -c 300 "$work/reference.log" 2> /dev/null | tr '\n' ' '))"
        "$compiler" measure-compile --runs "$runs" --warmups 1 -- "$@"
    fi
}

section "cpu"
lscpu | grep 'Model name'
cc -std=c17 -O2 -Wall -Wextra -Werror -o "$work/generator" "$root/.github/perf-probe/gen.c"

section "generated inputs"
modes=(string-calls:10000 string-declarations:10000 char-arrays:10000 wide-strings:10000 nested-arrays:20000
    nested-designators:5000 compound-literals:5000 static-asserts:10000 generics:5000 parameters:500 variadic-calls:5000
    pragma-packs:2000 conditionals:5000 line-directives:10000 function-table:5000 shadowing:5000 casts:10000)
for entry in "${modes[@]}"; do
    mode="${entry%%:*}"
    base="${entry#*:}"
    for scale in 1 2 4 8; do
        n=$((base * scale))
        directory="$work/inputs/$mode-$n"
        mkdir -p "$directory"
        "$work/generator" "$mode" "$n" "$directory" > "$directory/in.c"
        section "gen $mode $n $(wc -c < "$directory/in.c") bytes"
        measure 3 "$tcc" -c "-I$directory" "$directory/in.c" | grep -v '^run \|^warm-up'
        "$compiler" -c "-I$directory" "$directory/in.c" -o "$work/check.o" 2>&1 | head -n 3
    done
done

for mode in functions:2000 statements:10000; do
    for scale in 1 2 4 8; do
        n=$((${mode#*:} * scale))
        "$work/generator" "${mode%%:*}" "$n" > "$work/debug.c"
        section "gen debug-${mode%%:*} $n $(wc -c < "$work/debug.c") bytes"
        measure 3 "" -g -c "$work/debug.c" | grep -E 'median of|^  (debug|lanes|lower|tier0|check-input|span) '
    done
done

for scale in 1 2 4 8; do
    n=$((5000 * scale))
    "$work/generator" assembly "$n" > "$work/assembly.s"
    section "assembly $n $(wc -c < "$work/assembly.s") bytes"
    for tool in buster gnu; do
        best=""
        for run in 1 2 3; do
            start="${EPOCHREALTIME//[!0-9]/}"
            if [ "$tool" = buster ]; then
                "$compiler" assemble -o "$work/assembly.o" "$work/assembly.s" > "$work/assembly.log" 2>&1 || { echo "buster assemble failed: $(head -c 300 "$work/assembly.log")"; }
            else
                as "$work/assembly.s" -o "$work/assembly-gnu.o"
            fi
            end="${EPOCHREALTIME//[!0-9]/}"
            echo "$tool run $run: $(((end - start) / 1000)) ms"
        done
    done
done
section "done"
