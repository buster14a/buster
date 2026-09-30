#!/usr/bin/env bash
# Compile every frozen tests/*.c with two compilers; compare exit codes and objects.
set -u
SP=$1; A=$2; B=$3; TARGET=${4:-}; OUT=$SP/corpus-out/${TARGET:-native}
mkdir -p $OUT
cd $SP/frozen
same=0; diff=0; bothfail=0; total=0
for f in tests/*.c; do
  n=$(basename $f .c); total=$((total+1))
  targ=(); [ -n "$TARGET" ] && targ=(--target=$TARGET)
  $A cc "${targ[@]}" -c -g0 -Isrc -Itests $f -o $OUT/$n.a.o > $OUT/$n.a.log 2>&1; ea=$?
  $B cc "${targ[@]}" -c -g0 -Isrc -Itests $f -o $OUT/$n.b.o > $OUT/$n.b.log 2>&1; eb=$?
  if [ $ea -ne $eb ]; then echo "EXIT-DIFF $n $ea $eb"; diff=$((diff+1)); continue; fi
  if [ $ea -ne 0 ]; then bothfail=$((bothfail+1)); continue; fi
  if cmp -s $OUT/$n.a.o $OUT/$n.b.o; then same=$((same+1)); else echo "OBJ-DIFF $n"; diff=$((diff+1)); fi
done
echo "target=${TARGET:-native} total=$total identical_objects=$same both_failed=$bothfail differences=$diff"
