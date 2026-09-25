#!/bin/bash
# usage: timeit.sh <rounds> variants...  (interleaved ABAB order, perf stat per run)
N=$1; shift
cd <repo>
for r in $(seq 1 $N); do
  for v in "$@"; do
    perf stat -x, -e task-clock,cycles,instructions -o <census>/t/$v.$r.csv \
      <census>/bin/$v-native cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o <census>/t/out-$v >/dev/null 2>&1
  done
done
