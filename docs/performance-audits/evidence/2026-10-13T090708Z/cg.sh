#!/usr/bin/env bash
# cg.sh IDE OUTDIR VARIANT CPU : callgrind with a Zen 5-like cache geometry
S=$2; v=$3
extra=(); [[ $v == b ]] && extra=(-fcanonical-inline)
cd /home/user/buster
exec taskset -c "$4" valgrind --tool=callgrind --cache-sim=yes --D1=49152,12,64 --I1=32768,8,64 --LL=33554432,16,64 \
  --callgrind-out-file="$S/cg-$v.out" "$1" cc -march=x86-64-v3 -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 \
  -g src/buster/apps/ide/ide.c -lm "${extra[@]}" -o "$S/bin-$v" > "$S/log-$v.txt" 2>&1
