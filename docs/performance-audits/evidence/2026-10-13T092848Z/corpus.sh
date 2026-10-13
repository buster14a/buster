S=/tmp/claude-0/-home-user-buster/7c7ded3d-d8c6-5ea6-a99a-fbc58892467b/scratchpad
B=$S/base/build/Release/ide; C=$S/cand2/build/Release/ide
cd $S/base; mkdir -p $S/corpus
n=0; same=0; diff=0; fail=0
for f in $(find tests -name '*.c' | sort); do
  for g in -g0 -g; do for p in "" -fc-ast-pilot; do
    rm -f $S/corpus/a.o $S/corpus/b.o $S/corpus/c.o
    $B cc -Isrc -target x86_64-unknown-linux -march=znver5 $g -c $p "$f" -o $S/corpus/a.o >/dev/null 2>&1; ra=$?
    $C cc -Isrc -target x86_64-unknown-linux -march=znver5 $g -c $p "$f" -o $S/corpus/c.o >/dev/null 2>&1; rc=$?
    n=$((n+1))
    if [ $ra != $rc ]; then diff=$((diff+1)); echo "STATUS $f $g $p $ra $rc"
    elif [ $ra != 0 ]; then fail=$((fail+1))
    elif cmp -s $S/corpus/a.o $S/corpus/c.o; then same=$((same+1))
    else diff=$((diff+1)); echo "DIFF $f $g $p"; fi
  done; done
done
echo "compiles=$n identical=$same differ=$diff both_failed=$fail"
