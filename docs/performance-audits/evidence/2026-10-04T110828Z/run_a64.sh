#!/bin/sh
# usage: run_a64.sh IDE -- prints "K M FAR instructions:u text_bytes" for the
# AArch64 switch-relaxation family (aarch64-linux object output, -O0).
IDE=${1:?ide path}
cd "$(dirname "$0")"
row() { ./gen_a64sw "$1" "$2" "$3" > /tmp/ra.c; perf stat -x, -e instructions:u "$IDE" cc -target aarch64-linux -c -o /tmp/ra.o /tmp/ra.c 2> /tmp/ra.stat || exit 1; echo "$1 $2 $3 $(grep instructions /tmp/ra.stat | cut -d, -f1) $(llvm-objdump-18 -h /tmp/ra.o | awk '$2==".text"{print strtonum("0x"$3)}')"; }
for far in 1 0; do for k in 500 1000 2000 4000 8000; do row $k 40000 $far; done; done
for far in 1 0; do for m in 20000 60000; do for k in 1000 4000; do row $k $m $far; done; done; done
