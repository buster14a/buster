S=/tmp/claude-0/-home-user-buster/7c7ded3d-d8c6-5ea6-a99a-fbc58892467b/scratchpad
C=$S/cand2/build/Release/ide
cd $S/base; n=0; same=0; diff=0; fail=0
for f in $(find tests -name '*.c' | sort); do for g in -g0 -g; do
  rm -f $S/corpus/d.o $S/corpus/p.o
  $C cc -Isrc -target x86_64-unknown-linux -march=znver5 $g -c "$f" -o $S/corpus/d.o >/dev/null 2>&1; rd=$?
  $C cc -Isrc -target x86_64-unknown-linux -march=znver5 $g -c -fc-ast-pilot "$f" -o $S/corpus/p.o >/dev/null 2>&1; rp=$?
  n=$((n+1))
  if [ $rd != $rp ]; then diff=$((diff+1)); echo "STATUS $f $g $rd $rp"
  elif [ $rd != 0 ]; then fail=$((fail+1))
  elif cmp -s $S/corpus/d.o $S/corpus/p.o; then same=$((same+1)); else diff=$((diff+1)); echo "DIFF $f $g"; fi
done; done
echo "pairs=$n identical=$same differ=$diff both_failed=$fail"
