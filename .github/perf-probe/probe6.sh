#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its sixth round: X-macro headers, diagnostics in bulk, local array
# initializers, one huge literal, #embed and token pastes, each at four sizes against tcc or clang. Everything goes to
# standard output between "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
clang_bin="$(command -v clang-23)"

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
modes=(x-macros:50:tcc: warnings:2000:tcc: redefinitions:5000:tcc: local-initializer:5000:tcc: huge-literal:524288:tcc:
    embed:1048576:clang:-std=c23 pastes:5000:tcc:)
for entry in "${modes[@]}"; do
    IFS=: read -r mode base reference standard <<< "$entry"
    [ "$reference" = clang ] && reference="$clang_bin" || reference="$tcc"
    for scale in 1 2 4 8; do
        n=$((base * scale))
        directory="$work/inputs/$mode-$n"
        mkdir -p "$directory"
        "$work/generator" "$mode" "$n" "$directory" > "$directory/in.c"
        section "gen $mode $n $(wc -c < "$directory/in.c") bytes"
        measure 3 "$reference" ${standard:+"$standard"} -c "-I$directory" "$directory/in.c" | grep -v '^run \|^warm-up'
        "$compiler" ${standard:+"$standard"} -c "-I$directory" "$directory/in.c" -o "$work/check.o" 2>&1 | grep -v 'warning' | head -n 3
        if [ "$mode" = local-initializer ] || [ "$mode" = embed ] || [ "$mode" = huge-literal ]; then
            "$clang_bin" ${standard:+"$standard"} -O0 -c "-I$directory" "$directory/in.c" -o "$work/clang.o" 2> /dev/null
            echo "text bytes: buster $(size -A "$work/check.o" | awk '$1 ~ /^\.text/ { t += $2 } END { print t }'), clang -O0 $(size -A "$work/clang.o" | awk '$1 ~ /^\.text/ { t += $2 } END { print t }')"
        fi
    done
done
section "done"
