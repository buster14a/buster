#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its tenth round: what a process costs before and after its stages (exec,
# the dynamic loader, exit) against /bin/true and tcc, and a flat profile of the expression checker on sqlite3.c.
# Everything goes to standard output between "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
perf="${PERF:-perf}"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep -E 'Model name'
cd "$work"
: > empty.c
printf 'int main(void)\n{\n    return 0;\n}\n' > tiny.c
ls -l "$compiler"
size -A "$compiler" | awk '$2 > 100000'
ldd "$compiler"

section "dynamic loader statistics"
LD_DEBUG=statistics "$compiler" 2>&1 | grep -v '^usage' | head -n 20

stat_of()
{
    local label="$1"
    shift
    echo "--- $label"
    "$perf" stat -r 101 -e task-clock,page-faults,context-switches -x, -o stat.txt -- "$@" > /dev/null 2>&1
    awk -F, '!/^#/ && NF > 2 { printf "  %-16s %12s %s (+-%s)\n", $3, $1, $2, $4 }' stat.txt
    "$perf" stat -r 101 -- "$@" 2>&1 > /dev/null | grep -E 'seconds time elapsed|seconds user|seconds sys'
}

section "fixed costs, 101 runs each"
stat_of "/bin/true" /bin/true
stat_of "compiler, usage" "$compiler"
stat_of "compiler -E empty.c" "$compiler" -E empty.c -o /dev/null
stat_of "compiler -fsyntax-only empty.c" "$compiler" -fsyntax-only empty.c
stat_of "compiler -c empty.c" "$compiler" -c empty.c -o "$work/empty.o"
stat_of "compiler -c tiny.c" "$compiler" -c tiny.c -o "$work/tiny.o"
stat_of "compiler tiny.c -o tiny" "$compiler" tiny.c -o "$work/tiny"
stat_of "tcc -c tiny.c" "$tcc" -c tiny.c -o "$work/tiny-tcc.o"
stat_of "tcc tiny.c -o tiny" "$tcc" tiny.c -o "$work/tiny-tcc"

section "measure-compile -c tiny.c against tcc"
"$compiler" measure-compile --runs 41 --warmups 3 --reference "$tcc" -- -c tiny.c | grep -E 'median of|against reference|^  (setup|preprocess|backend-setup|release|span|outside|object-build|emit-setup) '

section "the expression checker on sqlite3.c"
curl -sSL --retry 3 -o real/sqlite.zip https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' real/sqlite.zip real
sqlite="$work/real/sqlite-amalgamation-3530200/sqlite3.c"
taskset -c 1 "$compiler" benchmark-checker-source --only scalar "$sqlite" 2>&1 | cut -c 1-400 | head -n 8
"$perf" record -q -o "$work/checker.data" -F 4999 -- taskset -c 1 "$compiler" benchmark-checker-source --only scalar "$sqlite" > /dev/null 2>&1
"$perf" report -q -i "$work/checker.data" --no-children --sort symbol --percent-limit 0.5 --stdio 2> /dev/null | grep -v '^$' | head -n 45 | cut -c 1-150
section "done"
