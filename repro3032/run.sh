#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$1
mkdir -p "$out"
compiler=./build/compiler

# The Link fixture goes through the command's normal materialization API.
# One copy of each interpreter process also executes the C units' real main.
run_case() {
    local name=$1
    shift
    local status=0
    "$@" >"$out/$name.out" 2>"$out/$name.err" || status=$?
    printf '%s exit=%s\n' "$name" "$status"
    cat "$out/$name.out"
    awk '/^COUNT /' "$out/$name.err" | tail -14
    if (( status != 0 )); then tail -30 "$out/$name.err"; return "$status"; fi
    awk '/^(WIN|MAP|FIX|DIAG) /' "$out/$name.err" > "$out/$name.trace"
    sha256sum "$out/$name.trace"
    for key in WIN MAP FIX DIAG; do printf '%s ' "$key"; awk -v k="$key" '$1==k {n++} END {print n+0}' "$out/$name.trace"; done
}
run_case link "$compiler" test-interpreter-link
run_case units "$compiler" interpret-c tests/interpreter/units-main.c tests/interpreter/units-library.c
"$CC" tests/interpreter/units-main.c tests/interpreter/units-library.c -o "$out/units-native"
"$out/units-native" > "$out/units.clang"
cmp "$out/units.clang" "$out/units.out"

p="$RUNNER_TEMP/rand48"
mkdir -p "$p"
for f in rand48.h __rand48_step.c __seed48.c lrand48.c mrand48.c seed48.c; do
    if [[ ! -f "$p/$f" ]]; then
        curl -fsSL --retry 3 "https://raw.githubusercontent.com/ziglang/zig/d03a147ea0a590ca711b3db07106effc559b0fc6/lib/libc/musl/src/prng/$f" -o "$p/$f"
    fi
done
(cd "$p" && sha256sum -c "$here/musl.sha256")
sources=("$here/musl-main.c" "$p/__rand48_step.c" "$p/__seed48.c" "$p/lrand48.c" "$p/mrand48.c" "$p/seed48.c")
"$CC" -Dhidden= "${sources[@]}" -o "$out/musl-native"
"$out/musl-native" > "$out/musl.clang"
run_case musl "$compiler" interpret-c -Dhidden= "${sources[@]}"
cmp "$out/musl.clang" "$out/musl.out"
for source in "${sources[@]}"; do
    name=$(basename "$source" .c)
    "$compiler" -Dhidden= -c "$source" -o "$out/$name.o"
    sha256sum "$out/$name.o"
done

"$CC" -O2 "$here/generate.c" -o "$out/generate"
for n in 8 32 128; do
    dir="$RUNNER_TEMP/units-$n"
    mkdir -p "$dir"
    "$out/generate" "$n" "$dir"
    run_case "c$n" "$compiler" interpret-c "$dir"/unit-*.c
    "$CC" "$dir"/unit-*.c -o "$out/c$n-native"
    "$out/c$n-native" > "$out/c$n.clang"
    cmp "$out/c$n.clang" "$out/c$n.out"
done
