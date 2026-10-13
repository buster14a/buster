#!/bin/bash
# Build a Release ide at each #56 merge commit and its first parent, one worktree reused.
S=$(dirname "$(readlink -f "$0")")
export PATH=$S/tcc/bin:$PATH
mkdir -p $S/ides
cd /home/user/buster
[ -d $S/wt-attr ] || git worktree add -q --detach $S/wt-attr a67057643a40c0ab99931849eb30053a2b870fcc
cd $S/wt-attr
for c in 3601845c9 17ff6fbad; do
  [ -x $S/ides/ide-$c ] && continue
  git checkout -q --detach $c || exit 1
  if [ ! -f build/build.ninja ]; then ./build.sh generate > $S/ides/gen-$c.log 2>&1 || { echo "gen fail $c"; exit 1; }; fi
  ./build.sh build --config Release -t ide > $S/ides/build-$c.log 2>&1 || { ./build.sh generate > $S/ides/gen-$c.log 2>&1 && ./build.sh build --config Release -t ide > $S/ides/build-$c.log 2>&1; } || { echo "build fail $c"; exit 1; }
  cp build/Release/ide $S/ides/ide-$c && echo "built $c $(date -u +%T)"
done
echo done
