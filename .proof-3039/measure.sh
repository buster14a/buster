#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# In a Buster source checkout, using its pinned hosted tools. Reuse INPUT for
# both compilers so their object files and their external linker inputs match.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
action=${1:?prepare, link or test}
input=${2:?input directory}
compiler=${3:-./build/compiler}
output=${4:-}

if [[ $action == prepare ]]; then
    mkdir -p "$input"
    clang -std=c17 -O2 -Wall -Wextra -Werror "$here/frames.c" -o "$input/frames"
    clang -std=c17 -O2 -Wall -Wextra -Werror "$here/verify.c" -o "$input/verify"
    clang -std=gnu17 -O2 -fasynchronous-unwind-tables -ffunction-sections -c tools/dco/dco.c -o "$input/dco.o"
    clang -std=gnu17 -O2 -fPIC -ffunction-sections -fasynchronous-unwind-tables -fno-optimize-sibling-calls -c "$here/runtime.c" -o "$input/runtime.o"
    for shape in sorted reverse ties; do
        for n in 0 1 2 17 257 5000; do
            "$input/frames" "$n" "$shape" > "$input/$shape-$n.s"
            clang -c "$input/$shape-$n.s" -o "$input/$shape-$n.o"
        done
    done
    sha256sum "$input"/*.o
elif [[ $action == link ]]; then
    mkdir -p "$output"
    record()
    {
        local name=$1
        shift
        FDE_TRACE=1 "$compiler" "$@" -o "$output/$name" 2> "$output/$name.trace" || { cat "$output/$name.trace"; return 1; }
        objcopy --dump-section ".eh_frame_hdr=$output/$name.hdr" "$output/$name" "$output/objcopy.tmp"
        rm "$output/objcopy.tmp"
        readelf -W -l "$output/$name" > "$output/$name.phdr"
        grep -q GNU_EH_FRAME "$output/$name.phdr"
        readelf --debug-dump=frames "$output/$name" > "$output/$name.frames"
        llvm-readobj --unwind "$output/$name" > "$output/$name.unwind"
        read -r hdr_addr frame_addr < <(objdump -h "$output/$name" | awk '$2==".eh_frame_hdr"{h=$4} $2==".eh_frame"{f=$4} END{print h, f}')
        "$input/verify" "$output/$name.hdr" "$output/$name.frames" "$hdr_addr" "$frame_addr"
        printf '%s ' "$name"
        cat "$output/$name.trace"
    }
    record dco --lanes 1 -static "$input/dco.o"
    "$output/dco" status > "$output/dco.stdout"
    grep -q 'DCO setup status' "$output/dco.stdout"
    record runtime --lanes 1 "$input/runtime.o" -lgcc_s
    "$output/runtime" | tee "$output/runtime.stdout"
    for shape in sorted reverse ties; do
        for n in 0 1 2 17 257 5000; do
            name=$shape-$n
            record "$name" link-foreign -static -e _start "$input/$name.o"
            test "$(wc -c < "$output/$name.hdr")" -eq "$((12 + 8 * n))"
            test "$(awk '/ FDE cie=/ { n++ } END { print n+0 }' "$output/$name.frames")" -eq "$n"
            "$output/$name"
        done
    done
elif [[ $action == test ]]; then
    mkdir -p "$output"
    tests=(test-executable test-unwind test-dynamic test-static test-foreign test-check-debug)
    result=0
    for test_name in "${tests[@]}"; do
        options=()
        if [[ $test_name != test-executable ]]; then options+=(--cross-check); fi
        if "$compiler" "$test_name" "${options[@]}" > "$output/$test_name.log" 2>&1; then
            echo "PASS $test_name ${options[*]}"
            grep -Ei 'unwind sort|unwind links|libgcc|backtrace|skipped' "$output/$test_name.log" | tail -12 || true
        else
            echo "FAILED $test_name ${options[*]}"
            tail -70 "$output/$test_name.log"
            result=1
        fi
    done
    exit "$result"
else
    exit 2
fi
