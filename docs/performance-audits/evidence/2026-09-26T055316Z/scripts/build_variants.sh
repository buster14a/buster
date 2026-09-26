#!/bin/bash
# Serial matched builds: one variant at a time, same worktree path, same command.
set -u
S=$SCRATCH
for pair in base:ade6ac4b6ecb21f30b61b656439bac476c145e2f a:343ed8df4ff37065e22e1b8317a2d63ef372440e b:d3a8d872cb1016d6d554b18fbdd5e6bab655046b ab:207ea447810d89b1b71e24263a1787bda2e8c670; do
  v=${pair%%:*}; c=${pair#*:}
  git -C $S/wv checkout -q --detach $c || exit 1
  echo "variant=$v commit=$(git -C $S/wv rev-parse HEAD) tree=$(git -C $S/wv rev-parse HEAD^{tree}) status=$(git -C $S/wv status --porcelain | wc -l)"
  pids=()
  for cfg in checked:1 unchecked:0; do for march in native x86-64-v3; do
    name=ide-$v-${cfg%%:*}-$march
    (cd $S && ROOT=$S/wv MARCH=$march TESTS=${cfg#*:} ./build_cfg.sh $S/vbin/$name > $S/vbin/$name.log 2>&1; echo "$name rc=$?") &
    pids+=($!)
  done; done
  wait
done
sha256sum $S/vbin/ide-* | grep -v "\.o$\|\.log$"
