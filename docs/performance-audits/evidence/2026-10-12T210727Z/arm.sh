#!/bin/bash
# arm.sh <name> <binary> <endpoint syn|c> [pilot]
S=/tmp/claude-0/-home-user-buster/da6de460-0b83-5c94-a74c-ffa150b86945/scratchpad
name=$1; bin=$2; ep=$3; pilot=$4
cd $S/base
if [ "$ep" = syn ]; then mode="-fsyntax-only"; else mode="-c -o $S/cg/arm-$name.o"; fi
valgrind --tool=callgrind --cache-sim=no --branch-sim=no --callgrind-out-file=$S/cg/arm-$name-$ep.out $bin cc -Isrc -Ibuild/v3/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5 -g0 $mode $pilot src/buster/apps/ide/ide.c > $S/cg/arm-$name-$ep.log 2>&1
echo "exit=$?" >> $S/cg/arm-$name-$ep.log
