#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its fourteenth round: where Tier 0's time goes when a function makes
# thousands of overflow or bit-count builtin calls (flat profiles and the stages, at two sizes each). Everything goes
# to standard output between "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/inputs"
compiler="$root/build/compiler"
perf="${PERF:-perf}"
cd "$work"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep -E 'Model name'
cc -std=c17 -O2 -o "$work/generator" "$root/.github/perf-probe/gen.c"
for entry in overflow:2000 overflow:4000 popcount:4000 popcount:8000 expect:8000; do
    builtin="${entry%%:*}"
    n="${entry#*:}"
    file="$work/inputs/builtin-$builtin-$n.c"
    "$work/generator" "builtin-$builtin" "$n" > "$file"
    section "builtin-$builtin $n"
    "$compiler" --time-stages -c "$file" -o out.o 2>&1 > /dev/null | awk -F'\t' '$1 == "time-stage" && $2 != "total" && $2 != "stage" { print "  " $2, $3 " ms" }' |
        sort -k2 -n -r | head -n 4
    "$perf" record -q -o "$builtin-$n.data" -F 9999 -- "$compiler" -c "$file" -o out.o > /dev/null 2>&1
    "$perf" report -q -i "$builtin-$n.data" --no-children --sort symbol --percent-limit 1 --stdio 2> /dev/null | grep -v '^$' | head -n 14 | cut -c 1-110
    objdump -d --no-show-raw-insn out.o | grep -c '^ ' | sed 's/^/  instructions in the object: /'
done
section "done"
