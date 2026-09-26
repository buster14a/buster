#!/bin/bash
set -u
S=$SCRATCH
for pair in base:ade6ac4b6ecb21f30b61b656439bac476c145e2f a:343ed8df4ff37065e22e1b8317a2d63ef372440e b:d3a8d872cb1016d6d554b18fbdd5e6bab655046b ab:207ea447810d89b1b71e24263a1787bda2e8c670; do
  v=${pair%%:*}; c=${pair#*:}
  git -C $S/wv checkout -q --detach $c || exit 1
  cd $S/wv
  echo "variant=$v commit=$(git rev-parse HEAD) status=$(git status --porcelain | wc -l)"
  F="-Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v"
  $S/vbin/ide-$v-checked-native cc $F -fsource-metrics=$S/sh/$v-stage1.metrics src/buster/apps/ide/ide.c -lm -o $S/sh/$v-stage1 > $S/sh/$v-stage1.log 2>&1; r1=$?
  $S/sh/$v-stage1 cc $F -fsource-metrics=$S/sh/$v-stage2.metrics src/buster/apps/ide/ide.c -lm -o $S/sh/$v-stage2 > $S/sh/$v-stage2.log 2>&1; r2=$?
  if cmp -s $S/sh/$v-stage1 $S/sh/$v-stage2; then fp=identical; else fp=DIFFERENT; fi
  tok1=$(grep '^preprocessed.tokens=' $S/sh/$v-stage1.metrics); tok2=$(grep '^preprocessed.tokens=' $S/sh/$v-stage2.metrics)
  $S/sh/$v-stage2 bench > $S/sh/$v-bench.log 2>&1; rb=$?
  echo "SELF_HOST variant=$v stage1_rc=$r1 stage2_rc=$r2 fixed_point=$fp bytes=$(stat -c %s $S/sh/$v-stage1) stage1=$(sha256sum < $S/sh/$v-stage1 | cut -c1-16) stage2=$(sha256sum < $S/sh/$v-stage2 | cut -c1-16) $tok1 stage2_$tok2 bench_rc=$rb"
done
