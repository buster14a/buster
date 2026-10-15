#!/bin/bash
# Four Callgrind arms (A base default, B base pilot, C candidate pilot,
# D candidate default) on the base tree's unity self-host source.
S=/tmp/claude-0/-home-user-buster/750697fc-e6e0-5a2a-a7c5-db10f289a2b6/scratchpad
cd "$S/base"
B=$S/base/build/Release/ide
C=$S/cand/build/Release/ide
common="-Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5"
run() { # name binary mode flag
    valgrind --tool=callgrind --cache-sim=no --branch-sim=no --callgrind-out-file="$S/out/$1.cg" \
        "$2" cc $common -g0 $3 $4 src/buster/apps/ide/ide.c > "$S/out/$1.log" 2>&1
}
run A.s "$B" -fsyntax-only "" &
run B.s "$B" -fsyntax-only -fc-ast-pilot &
run C.s "$C" -fsyntax-only -fc-ast-pilot &
run D.s "$C" -fsyntax-only "" &
wait
run A.c "$B" "-c -o $S/out/A.g0.o" "" &
run B.c "$B" "-c -o $S/out/B.g0.o" -fc-ast-pilot &
run C.c "$C" "-c -o $S/out/C.g0.o" -fc-ast-pilot &
run D.c "$C" "-c -o $S/out/D.g0.o" "" &
wait
for arm in A B C D; do
    bin=$B; if [ $arm = C ] || [ $arm = D ]; then bin=$C; fi
    flag=""; if [ $arm = B ] || [ $arm = C ]; then flag=-fc-ast-pilot; fi
    "$bin" cc $common -g -c $flag src/buster/apps/ide/ide.c -o "$S/out/$arm.g.o" &
done
wait
"$B" cc $common -g0 -fsyntax-only -fc-ast-pilot -v src/buster/apps/ide/ide.c 2>&1 | grep -E '^C_AST' > "$S/out/B.stats"
"$C" cc $common -g0 -fsyntax-only -fc-ast-pilot -v src/buster/apps/ide/ide.c 2>&1 | grep -E '^C_AST' > "$S/out/C.stats"
cd "$S/out"
grep -H "refs" *.log
sha256sum *.o
diff <(sed 's/build_ns=[0-9]*//; s/walk_ns=[0-9]*//; s/scan_ns=[0-9]*//; s/children_ns=[0-9]*//' B.stats) \
     <(sed 's/build_ns=[0-9]*//; s/walk_ns=[0-9]*//; s/scan_ns=[0-9]*//; s/children_ns=[0-9]*//' C.stats) && echo STATS-IDENTICAL
