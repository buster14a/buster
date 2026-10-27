#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its eighth round: system calls. How many writes each diagnostic costs
# (the preprocessor's, a redefinition's with its note, the checker's), with standard error on a file and on a pipe, and
# strace -c summaries of whole compiles against clang -O0 and tcc. Everything goes to standard output between "=== "
# headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real"
compiler="$root/build/compiler"
tcc="$(command -v tcc)"
clang_bin="$(command -v clang-23)"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep 'Model name'
sudo apt-get install -y -q strace > /dev/null 2>&1
cd "$work"
n=4000
for i in $(seq 0 $((n - 1))); do printf '#warning "warning number %u"\n' "$i"; done > warnings.c
printf 'int main(void)\n{\n    return 0;\n}\n' >> warnings.c
for i in $(seq 0 $((n - 1))); do printf '#define R %u\n' "$i"; done > redefinitions.c
printf 'int get(void)\n{\n    return R;\n}\n' >> redefinitions.c
for i in $(seq 0 $((n - 1))); do printf 'int v%u = 2147483647 + %u;\n' "$i" "$((i + 1))"; done > overflow.c

for kind in warnings redefinitions overflow; do
    for tool in buster clang tcc; do
        case "$tool" in
            buster) command=("$compiler" -c "$kind.c" -o "$kind-$tool.o") ;;
            clang) command=("$clang_bin" -O0 -c "$kind.c" -o "$kind-$tool.o") ;;
            tcc) command=("$tcc" -c "$kind.c" -o "$kind-$tool.o") ;;
        esac
        section "$kind $n $tool"
        strace -f -c -o "$kind-$tool.strace" -e trace=write,writev,pwrite64 "${command[@]}" 2> "$kind-$tool.err"
        echo "status $?, stderr $(wc -l < "$kind-$tool.err") lines, $(wc -c < "$kind-$tool.err") bytes"
        awk '$NF == "write" || $NF == "writev" || $NF == "pwrite64" { print "  " $NF ": " $4 " calls, " $2 " s" }' "$kind-$tool.strace"
        head -n 3 "$kind-$tool.err" | cut -c 1-160
        for sink in file pipe; do
            best=""
            for run in 1 2 3 4 5; do
                if [ "$sink" = file ]; then
                    /usr/bin/time -f '%e %U %S' -o time.txt "${command[@]}" 2> sink.err > /dev/null
                else
                    /usr/bin/time -f '%e %U %S' -o time.txt bash -c '"$@" 2>&1 | cat > /dev/null' _ "${command[@]}"
                fi
                read -r wall user system < time.txt
                echo "  $sink run $run: wall $wall s, user $user s, system $system s"
            done
        done
    done
done

section "whole compiles"
curl -sSL --retry 3 -o real/zlib.tar.gz https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz && tar -xzf real/zlib.tar.gz -C real
curl -sSL --retry 3 -o real/sqlite.zip https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' real/sqlite.zip real
for file in real/zlib-1.3.1/deflate.c real/zlib-1.3.1/uncompr.c real/sqlite-amalgamation-3530200/sqlite3.c; do
    directory="$(dirname "$file")"
    for tool in buster clang tcc; do
        case "$tool" in
            buster) command=("$compiler" -c "-I$directory" "$file" -o whole.o) ;;
            clang) command=("$clang_bin" -O0 -c "-I$directory" "$file" -o whole.o) ;;
            tcc) command=("$tcc" -c "-I$directory" "$file" -o whole.o) ;;
        esac
        section "strace $file $tool"
        strace -f -c -o whole.strace "${command[@]}" > /dev/null 2> whole.err
        echo "status $?"
        sed -n '1,40p' whole.strace
    done
    section "strace -E $file buster"
    strace -f -c -o whole.strace "$compiler" -E "-I$directory" "$file" -o whole.i > /dev/null 2> whole.err
    echo "status $?, output $(wc -c < whole.i) bytes"
    sed -n '1,25p' whole.strace
done
section "done"
