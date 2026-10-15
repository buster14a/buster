# The four Callgrind arms and the -g object builds, as run. Workload runs from
# the frozen base tree; S is this session's scratchpad.
S=${S:?}
cd $S/base
run() { # name binary flags
  valgrind --tool=callgrind --cache-sim=no --branch-sim=no --callgrind-out-file=$S/out/$1.cg $2 cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5 -g0 -c $3 src/buster/apps/ide/ide.c -o $S/out/$1.g0.o > $S/out/$1.log 2>&1
}
B=$S/base/build/Release/ide; C=$S/cand/build/Release/ide
run A $B "" & run B $B -fc-ast-pilot & run C $C -fc-ast-pilot & run D $C "" & wait
for arm in A B C D; do
  bin=$B; [ $arm = C -o $arm = D ] && bin=$C
  flag=""; [ $arm = B -o $arm = C ] && flag=-fc-ast-pilot
  $bin cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5 -g -c $flag src/buster/apps/ide/ide.c -o $S/out/$arm.g.o &
done; wait
cd $S/out; grep -h "refs" A.log B.log C.log D.log; sha256sum *.o; ls -l *.o
