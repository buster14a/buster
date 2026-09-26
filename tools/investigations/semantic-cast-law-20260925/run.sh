#!/usr/bin/env bash
set -euo pipefail
ide=$(realpath "$1")
out=$(realpath -m "$2")
probe=$(cd -- "$(dirname -- "$0")" && pwd)
read -r -a compilers <<< "${PROBE_COMPILERS:-clang gcc buster}"
optimization=${PROBE_OPTIMIZATION:--O0}
allocator=${PROBE_ALLOCATOR:-fast}
frontend=${PROBE_FRONTEND:--ffrontend-ssa}
mkdir -p "$out/sources" "$out/results"
cp "$probe/probe.c" "$probe/observer.c" "$out/sources/"
expressions=(
 '0'
 '((signed char)-1 + 1)'
 '((short)-1 + 1)'
 '((long long)(int)-1 + 1)'
 '((signed char)-7 + 7)'
 '((short)-127 + 127)'
 '(0 - (int)(signed char)-1 - 1)'
 '((long long)(unsigned int)-1 - 4294967295LL)'
 '((int)(unsigned char)255 - 255)'
 '((int)(signed char)0)'
 '((long long)(signed char)-128 + 128)'
 '((int)(short)-32767 + 32767)'
 '((long long)(int)(signed char)-1 + 1)'
 '((unsigned long long)(signed char)-1 + 1ULL)'
 '((unsigned int)(signed char)-1 + 1u)'
 '((unsigned int)(short)-2 + 2u)'
 '((long long)(int)-2147483647 + 2147483647LL)'
)
printf 'expression\tkind\tcompiler\tcompile\tlink\texecute\n' > "$out/results.tsv"
printf '%s\n' "${expressions[@]}" > "$out/expressions.txt"
clang --version > "$out/clang-version.txt"
gcc --version > "$out/gcc-version.txt"
uname -a > "$out/host.txt"
lscpu >> "$out/host.txt"
sha256sum "$ide" > "$out/compiler.sha256"
clang -std=c17 -O0 -Wall -Wextra -Werror -pedantic-errors -c "$probe/observer.c" -o "$out/observer.o"
for e in "${!expressions[@]}"; do
 for k in {0..8}; do
  src="$out/sources/e${e}-k${k}.c"
  printf '#define Z %s\n#define CASE_KIND %s\n#include "probe.c"\n' "${expressions[$e]}" "$k" > "$src"
  for compiler in "${compilers[@]}"; do
   dir="$out/results/e${e}-k${k}-${compiler}"; mkdir -p "$dir"
   args=(-std=gnu17 "$optimization" -g0 -c "$src" -o "$dir/subject.o")
   command=("$compiler")
   if [[ $compiler == buster ]]; then
    command=("$ide" cc)
    args+=(-target x86_64-linux -fverify-codegen "-fregister-allocator=$allocator" "$frontend")
    if [[ $allocator != none ]]; then args+=(-fno-machine-fallback); fi
   else
    args+=(-Wall -Wextra -Werror)
    # Preserve these diagnostics, but do not classify style warnings about
    # independently proved C integer-zero expressions as semantic failures.
    if [[ $compiler == clang ]]; then args+=(-Wno-error=non-literal-null-conversion); fi
    if [[ $compiler == gcc ]]; then args+=(-Wno-error=pointer-compare); fi
   fi
   printf '%q ' "${command[@]}" "${args[@]}" > "$dir/compile.argv"; printf '\n' >> "$dir/compile.argv"
   status=0; timeout 40 "${command[@]}" "${args[@]}" > "$dir/compile.stdout" 2> "$dir/compile.stderr" || status=$?
   link=NA; execute=NA
   if [[ $status == 0 ]]; then
    link=0; clang -no-pie "$dir/subject.o" "$out/observer.o" -o "$dir/check" > "$dir/link.stdout" 2> "$dir/link.stderr" || link=$?
    if [[ $link == 0 ]]; then
     execute=0; timeout 5 "$dir/check" > "$dir/execute.stdout" 2> "$dir/execute.stderr" || execute=$?
    fi
   fi
   printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$e" "$k" "$compiler" "$status" "$link" "$execute" | tee -a "$out/results.tsv"
  done
 done
done
find "$out" -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > "$out/SHA256SUMS"
