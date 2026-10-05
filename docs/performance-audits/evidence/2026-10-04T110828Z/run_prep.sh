#!/bin/sh
# usage: run_prep.sh IDE  -- prints "P C instructions:u" rows for the
# frontend prepared-control-expression family (x86_64-linux object output).
IDE=${1:?ide path}
cd "$(dirname "$0")"
row() { ./gen_prep "$1" "$2" > /tmp/rp.c; perf stat -x, -e instructions:u "$IDE" cc -target x86_64-linux -c -o /tmp/rp.o /tmp/rp.c 2> /tmp/rp.stat || exit 1; echo "$1 $2 $(grep instructions /tmp/rp.stat | cut -d, -f1)"; }
for p in 1000 2000 4000 8000 16000; do row $p 0; done
for c in 1000 2000 4000 8000 16000; do row 0 $c; done
for p in 2000 4000 8000; do for c in 1000 4000; do row $p $c; done; done
