#!/bin/bash
set -u
S=$SCRATCH
build() { (cd $S && ROOT=$S/wv MARCH=$2 TESTS=$3 ./build_cfg.sh $S/vbin/$1 > $S/vbin/$1.log 2>&1; echo "$1 rc=$?"); }
git -C $S/wv checkout -q --detach 343ed8df4ff37065e22e1b8317a2d63ef372440e && echo "variant=a $(git -C $S/wv rev-parse HEAD) status=$(git -C $S/wv status --porcelain | wc -l)"
build ide-a-unchecked-native native 0 & build ide-a-checked-x86-64-v3 x86-64-v3 1 & wait
git -C $S/wv checkout -q --detach d3a8d872cb1016d6d554b18fbdd5e6bab655046b && echo "variant=b $(git -C $S/wv rev-parse HEAD) status=$(git -C $S/wv status --porcelain | wc -l)"
build ide-b-checked-x86-64-v3 x86-64-v3 1
git -C $S/wv checkout -q --detach 207ea447810d89b1b71e24263a1787bda2e8c670 && echo "variant=ab $(git -C $S/wv rev-parse HEAD) status=$(git -C $S/wv status --porcelain | wc -l)"
build ide-ab-checked-x86-64-v3 x86-64-v3 1
