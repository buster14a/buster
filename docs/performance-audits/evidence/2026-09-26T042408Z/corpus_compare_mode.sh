#!/usr/bin/env bash
# Compare base/candidate artifacts for every frozen tests/*.c under extra flags.
set -u
SP=$1; A=$2; B=$3; LABEL=$4; shift 4
OUT=$SP/corpus-out/$LABEL; mkdir -p $OUT; cd $SP/frozen
same=0; diff=0; bothfail=0; total=0
for f in tests/*.c; do
  n=$(basename $f .c); total=$((total+1))
  $A cc "$@" -g0 -Isrc -Itests $f -o $OUT/$n.a.out > $OUT/$n.a.log 2>&1; ea=$?
  $B cc "$@" -g0 -Isrc -Itests $f -o $OUT/$n.b.out > $OUT/$n.b.log 2>&1; eb=$?
  if [ $ea -ne $eb ]; then echo "EXIT-DIFF $n $ea $eb"; diff=$((diff+1)); continue; fi
  if [ $ea -ne 0 ]; then bothfail=$((bothfail+1)); continue; fi
  if cmp -s $OUT/$n.a.out $OUT/$n.b.out; then same=$((same+1)); else echo "OUT-DIFF $n"; diff=$((diff+1)); fi
done
echo "mode=$LABEL total=$total identical=$same both_failed=$bothfail differences=$diff"
