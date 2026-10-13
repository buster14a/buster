S=/tmp/claude-0/-home-user-buster/7c7ded3d-d8c6-5ea6-a99a-fbc58892467b/scratchpad
cd $S/base
run() { # name binary flags
  valgrind --tool=callgrind --cache-sim=no --branch-sim=no --callgrind-out-file=$S/out/$1.cg $2 cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5 -g0 -c $3 src/buster/apps/ide/ide.c -o $S/out/$1.g0.o > $S/out/$1.log 2>&1
}
B=$S/base/build/Release/ide; C=$S/cand2/build/Release/ide
run C2 $C -fc-ast-pilot & run D2 $C "" & wait
for arm in C2 D2; do
  bin=$B; true && bin=$C
  flag=""; [ $arm = C2 ] && flag=-fc-ast-pilot
  $bin cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -target x86_64-unknown-linux -march=znver5 -g -c $flag src/buster/apps/ide/ide.c -o $S/out/$arm.g.o &
done; wait
cd $S/out; grep -h refs C2.log D2.log; sha256sum C2* D2*
