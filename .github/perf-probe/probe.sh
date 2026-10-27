#!/usr/bin/env bash
# A temporary probe (not part of the compiler): times build/compiler's stages on GitHub's hosted runners, against tcc,
# on generated inputs at four sizes each, with perf's profiles, and takes apart a small file's fixed costs. Everything
# goes to standard output between "=== " headers, for reading from the job's log.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
clang_bin="$(command -v clang-23)"
perf_bin="${PERF:-perf}"

section()
{
    echo
    echo "=== $*"
}

measure()
{
    # measure: RUNS REFERENCE ARGUMENT...; the reference is tcc where tcc compiles the arguments, otherwise none.
    local runs="$1" reference="$2"
    shift 2
    if [ -n "$reference" ] && "$reference" "$@" -o "$work/reference.o" > "$work/reference.log" 2>&1; then
        "$compiler" measure-compile --runs "$runs" --warmups 1 --reference "$reference" -- "$@"
    else
        echo "reference: none ($(head -c 300 "$work/reference.log" 2> /dev/null | tr '\n' ' '))"
        "$compiler" measure-compile --runs "$runs" --warmups 1 -- "$@"
    fi
}

profile()
{
    # profile: NAME ARGUMENT...: perf's samples of five compiles, by symbol.
    local name="$1"
    shift
    "$perf_bin" record -q -e cpu-clock -F 15000 -o "$work/perf-$name.data" -- \
        bash -c 'for run in 1 2 3 4 5; do "$@" > /dev/null 2>&1 || exit 1; done' compile "$compiler" "$@" -o "$work/perf.o" \
        || { echo "perf record failed"; return; }
    "$perf_bin" report -i "$work/perf-$name.data" --stdio --no-children --sort dso --percent-limit 0.5 2> /dev/null | grep -v '^#' | sed '/^$/d'
    "$perf_bin" report -i "$work/perf-$name.data" --stdio --no-children --sort symbol --percent-limit 0.3 2> /dev/null | grep -v '^#' | sed -n '/^$/d; 1,60p'
}

faults()
{
    # faults: NAME ARGUMENT...: every page fault of one run, by the function that took it and its callers.
    local name="$1"
    shift
    "$perf_bin" record -q -e page-faults -c 1 --call-graph dwarf,16384 -o "$work/faults-$name.data" -- "$@" > /dev/null 2>&1 \
        || { echo "perf record failed"; return; }
    "$perf_bin" report -i "$work/faults-$name.data" --stdio --no-children --sort dso,symbol --percent-limit 1 -g graph,0.5,callee,function,percent 2> /dev/null \
        | grep -v '^#' | sed -n '/^$/d; 1,260p'
}

section "cpu"
lscpu | sed -n '1,20p'
nproc
uname -r
cat /sys/kernel/mm/transparent_hugepage/enabled /sys/kernel/mm/transparent_hugepage/defrag
ls -la "$compiler"
size -A "$compiler" | sort -k2 -n -r | head -n 8

sqlite_dir="$work/sqlite"
mkdir -p "$sqlite_dir"
curl -sSL --retry 3 -o "$work/sqlite.zip" https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$work/sqlite.zip" "$sqlite_dir"
sqlite="$sqlite_dir/sqlite-amalgamation-3530200"
printf '%s  %s\n' 0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d "$sqlite/sqlite3.c" | sha256sum --check
options=(-c -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE "-I$sqlite" "$sqlite/sqlite3.c")

mkdir -p "$work/small"
printf '' > "$work/small/empty.c"
printf '#include <stdio.h>\nint main(void)\n{\n    printf("hello %%d\\n", 42);\n    return 0;\n}\n' > "$work/small/hello.c"
printf 'int main(void)\n{\n    return 0;\n}\n' > "$work/small/tiny.c"

section "fixed costs: whole processes"
cc -O2 -o "$work/nothing" -x c - <<< 'int main(void) { return 0; }'
for command in "$work/nothing" "$compiler" "$compiler -E $work/small/empty.c -o /dev/null" "$compiler -fsyntax-only $work/small/empty.c" \
    "$compiler -c $work/small/empty.c -o $work/empty.o" "$compiler -c $work/small/tiny.c -o $work/tiny.o" "$tcc -c $work/small/empty.c -o $work/tcc.o"; do
    echo "--- $command"
    # shellcheck disable=SC2086
    "$perf_bin" stat -r 100 -e task-clock,page-faults,context-switches -- $command 2>&1 > /dev/null | grep -E 'task-clock|page-faults|elapsed'
done
section "fixed costs: system calls of -c empty.c"
sudo apt-get install -y -q strace > /dev/null 2>&1
strace -c -f -o "$work/strace-summary.txt" "$compiler" -c "$work/small/empty.c" -o "$work/empty.o"
cat "$work/strace-summary.txt"
strace -f -o "$work/strace-calls.txt" -e trace=memory "$compiler" -c "$work/small/empty.c" -o "$work/empty.o"
awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^(mmap|munmap|mprotect|madvise|brk|mremap)\(/) { print $i; break } }' "$work/strace-calls.txt" | sed 's/(.*//' | sort | uniq -c
grep -E 'mmap\(NULL, [0-9]{9,}' "$work/strace-calls.txt" | head -n 20
section "fixed costs: page faults of -c empty.c by function"
faults empty "$compiler" -c "$work/small/empty.c" -o "$work/empty.o"
section "fixed costs: page faults of the bare executable by function"
faults bare "$compiler"
section "fixed costs: samples of 300 compiles of tiny.c, with callers"
"$perf_bin" record -q -e cpu-clock -F 20000 --call-graph dwarf,16384 -o "$work/perf-tiny.data" -- \
    bash -c 'for run in $(seq 300); do "$@" > /dev/null 2>&1 || exit 1; done' compile "$compiler" -c "$work/small/tiny.c" -o "$work/perf.o"
"$perf_bin" report -i "$work/perf-tiny.data" --stdio --comm compiler --no-children --sort symbol --percent-limit 0.8 -g graph,0.5,callee,function,percent 2> /dev/null \
    | grep -v '^#' | sed -n '/^$/d; 1,400p'

section "sqlite3.c page faults by function, one lane"
faults sqlite1 "$compiler" --lanes 1 "${options[@]}" -o "$work/sqlite.o"
section "sqlite3.c page faults by function, four lanes"
faults sqlite4 "$compiler" --lanes 4 "${options[@]}" -o "$work/sqlite.o"

section "generated inputs"
cc -std=c17 -O2 -Wall -Wextra -Werror -o "$work/generator" "$root/.github/perf-probe/gen.c"
modes=(typed-members:1000 members:1000 functions:2000 statements:10000 locals:1000 objects:500 elseif:1000 switch:2000
    sparse-switch:2000 labels:1000 forward-gotos:1000 label-values:1000 static-locals:2000 bitfields:500 enum:4000
    integers:40000 bytes:100000 floats:20000 designated:10000 records:5000 strings:20000 same-strings:20000
    concatenation:20000 macros:10000 macro-chain:1000 macro-calls:250 sums:10000 ternaries:1000 commas:10000 calls:2000
    globals:5000 pointers:5000 typedefs:5000 structs:2000 prototypes:5000 inlines:2000 loops:1000 includes:250
    identifiers:1000 oneline:2000)
for entry in "${modes[@]}"; do
    mode="${entry%%:*}"
    base="${entry#*:}"
    for scale in 1 2 4 8; do
        n=$((base * scale))
        directory="$work/inputs/$mode-$n"
        mkdir -p "$directory"
        if [ "$mode" = oneline ]; then
            "$work/generator" functions "$n" | tr '\n' ' ' > "$directory/in.c"
        else
            "$work/generator" "$mode" "$n" "$directory" > "$directory/in.c"
        fi
        section "gen $mode $n $(wc -c < "$directory/in.c") bytes"
        measure 3 "$tcc" -c "-I$directory" "$directory/in.c" | grep -v '^run \|^warm-up'
        "$compiler" -c "-I$directory" "$directory/in.c" -o "$work/check.o" 2>&1 | head -n 3
    done
    section "gen $mode profile at $n"
    profile "gen-$mode" -c "-I$directory" "$directory/in.c" | sed -n '1,25p'
done
section "done"
