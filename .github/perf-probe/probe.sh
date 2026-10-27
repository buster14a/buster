#!/usr/bin/env bash
# A temporary probe (not part of the compiler): times build/compiler's stages on GitHub's hosted runners, against tcc,
# on sqlite3.c, small files, Lua's sources and generated inputs at four sizes each, with perf's profiles. Everything
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
    "$perf_bin" record -q -e "$PERF_EVENT" -F 15000 -o "$work/perf-$name.data" -- \
        bash -c 'for run in 1 2 3 4 5; do "$@" > /dev/null 2>&1 || exit 1; done' compile "$compiler" "$@" -o "$work/perf.o" \
        || { echo "perf record failed"; return; }
    "$perf_bin" report -i "$work/perf-$name.data" --stdio --no-children --sort dso --percent-limit 0.5 2> /dev/null | grep -v '^#' | sed '/^$/d'
    "$perf_bin" report -i "$work/perf-$name.data" --stdio --no-children --sort symbol --percent-limit 0.3 2> /dev/null | grep -v '^#' | sed -n '/^$/d; 1,60p'
}

section "cpu"
lscpu | sed -n '1,30p'
grep -m1 '^flags' /proc/cpuinfo
nproc
uname -a
"$clang_bin" --version | head -1
"$tcc" -v
ls -la "$compiler"

# The event perf can sample: the cycles where the hypervisor gives them, else the CPU clock.
if "$perf_bin" stat -e cycles:u -x, -- true 2>&1 | grep -q 'not supported\|not counted'; then
    PERF_EVENT=cpu-clock
else
    PERF_EVENT=cycles
fi
"$perf_bin" stat -e task-clock,cycles,instructions,cycles:u,instructions:u -- true 2>&1 | sed -n '1,20p'
echo "perf event: $PERF_EVENT"

sqlite_dir="$work/sqlite"
mkdir -p "$sqlite_dir"
curl -sSL --retry 3 -o "$work/sqlite.zip" https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$work/sqlite.zip" "$sqlite_dir"
sqlite="$sqlite_dir/sqlite-amalgamation-3530200"
printf '%s  %s\n' 0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d "$sqlite/sqlite3.c" | sha256sum --check
options=(-c -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE "-I$sqlite" "$sqlite/sqlite3.c")

section "sqlite3.c -c against tcc"
measure 7 "$tcc" "${options[@]}"
section "sqlite3.c -c against clang -O0"
"$compiler" measure-compile --runs 3 --warmups 1 --reference "$clang_bin" -- "${options[@]}"
section "sqlite3.c -c -g"
measure 5 "" -g "${options[@]}"
for lanes in 1 2 4; do
    section "sqlite3.c -c --lanes $lanes"
    measure 5 "" --lanes "$lanes" "${options[@]}"
done
section "sqlite3.c -fsyntax-only"
for run in 1 2 3; do
    /usr/bin/time -f "fsyntax-only %e s %U user %S system %M KB" "$compiler" -fsyntax-only "${options[@]:1}" 2>&1 | tail -1
done
section "sqlite3.c -E"
for run in 1 2 3; do
    /usr/bin/time -f "buster -E %e s %U user %S system %M KB" "$compiler" -E "${options[@]:1}" -o "$work/sqlite.i" 2>&1 | tail -1
    /usr/bin/time -f "clang -E %e s %U user %S system %M KB" "$clang_bin" -E "${options[@]:1}" -o "$work/sqlite-clang.i" 2>&1 | tail -1
    /usr/bin/time -f "tcc -E %e s %U user %S system %M KB" "$tcc" -E "${options[@]:1}" -o "$work/sqlite-tcc.i" 2>&1 | tail -1
done
section "sqlite3.c profile"
profile sqlite "${options[@]}"
section "sqlite3.c profile with call chains of libc and the kernel"
"$perf_bin" record -q -e "$PERF_EVENT" -F 5000 --call-graph dwarf,16384 -o "$work/perf-chains.data" -- "$compiler" "${options[@]}" -o "$work/perf.o" > /dev/null 2>&1 \
    && "$perf_bin" report -i "$work/perf-chains.data" --stdio --no-children --sort dso,symbol --percent-limit 1.5 -G 2> /dev/null | grep -v '^#' | sed -n '/^$/d; 1,400p'

section "small files"
mkdir -p "$work/small"
printf '' > "$work/small/empty.c"
printf 'int main(void)\n{\n    return 0;\n}\n' > "$work/small/tiny.c"
printf '#include <stdio.h>\nint main(void)\n{\n    printf("hello %%d\\n", 42);\n    return 0;\n}\n' > "$work/small/hello.c"
{
    for header in assert ctype errno float inttypes limits locale math setjmp signal stdarg stddef stdint stdio stdlib string time wchar pthread unistd fcntl sys/stat sys/types sys/mman dirent; do
        echo "#include <$header.h>"
    done
    printf 'int main(void)\n{\n    printf("%%f\\n", sin(1.0));\n    return 0;\n}\n'
} > "$work/small/headers.c"
for name in empty tiny hello headers; do
    section "small $name.c against tcc"
    measure 41 "$tcc" -c "$work/small/$name.c"
    section "small $name.c against clang -O0"
    "$compiler" measure-compile --runs 11 --warmups 1 --reference "$clang_bin" -- -c "$work/small/$name.c" | grep -E 'median|against|backend-setup|setup|outside|span'
done
section "small hello.c profile"
"$perf_bin" record -q -e "$PERF_EVENT" -F 20000 -o "$work/perf-hello.data" -- \
    bash -c 'for run in $(seq 200); do "$@" > /dev/null 2>&1 || exit 1; done' compile "$compiler" -c "$work/small/hello.c" -o "$work/perf.o"
"$perf_bin" report -i "$work/perf-hello.data" --stdio --no-children --sort comm,dso --percent-limit 0.5 2> /dev/null | grep -v '^#' | sed '/^$/d'
"$perf_bin" report -i "$work/perf-hello.data" --stdio --no-children --comm compiler --sort symbol --percent-limit 0.4 2> /dev/null | grep -v '^#' | sed -n '/^$/d; 1,60p'

section "lua"
curl -sSL --retry 3 -o "$work/lua.tar.gz" https://www.lua.org/ftp/lua-5.4.8.tar.gz
tar -xzf "$work/lua.tar.gz" -C "$work"
lua="$work/lua-5.4.8/src"
for source in "$lua"/*.c; do
    section "lua $(basename "$source")"
    measure 5 "$tcc" -c -DLUA_USE_LINUX "-I$lua" "$source" | grep -E 'median|against|FAIL|failures|^  [a-z]'
done

section "generated inputs"
cc -std=c17 -O2 -Wall -Wextra -Werror -o "$work/gen" "$root/.github/perf-probe/gen.c"
modes=(functions:2000 statements:10000 locals:1000 objects:500 elseif:1000 switch:2000 sparse-switch:2000 labels:1000
    forward-gotos:1000 members:1000 bitfields:500 enum:4000 integers:40000 bytes:100000 floats:20000 designated:10000
    records:5000 strings:20000 same-strings:20000 concatenation:20000 macros:10000 macro-chain:1000 macro-calls:250
    sums:10000 ternaries:1000 commas:10000 calls:2000 globals:5000 pointers:5000 typedefs:5000 structs:2000
    prototypes:5000 inlines:2000 loops:1000 includes:250 identifiers:1000 oneline:2000)
for entry in "${modes[@]}"; do
    mode="${entry%%:*}"
    base="${entry#*:}"
    for scale in 1 2 4 8; do
        n=$((base * scale))
        directory="$work/gen/$mode-$n"
        mkdir -p "$directory"
        if [ "$mode" = oneline ]; then
            "$work/gen" functions "$n" | tr '\n' ' ' > "$directory/in.c"
        else
            "$work/gen" "$mode" "$n" "$directory" > "$directory/in.c"
        fi
        section "gen $mode $n $(wc -c < "$directory/in.c") bytes"
        measure 3 "$tcc" -c "-I$directory" "$directory/in.c"
    done
    section "gen $mode profile at $n"
    profile "gen-$mode" -c "-I$directory" "$directory/in.c" | sed -n '1,30p'
done

section "direct path"
"$work/gen" functions 16000 > "$work/direct.c"
for run in 1 2 3; do
    "$compiler" --time-stages "$work/direct.c" -o "$work/direct" 2>&1 | grep -E 'time-stage|time-process' | tr '\t' ' ' | sed "s/^/run $run: /"
done
"$work/direct"; echo "direct exit: $?"
section "done"
