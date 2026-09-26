#!/bin/bash
set -u
S=$SCRATCH
git -C $S/wv checkout -q --detach 8e0ee56101217c2b4b087cb7d162ada15ffb629d || exit 1
echo "variant=abp commit=$(git -C $S/wv rev-parse HEAD) tree=$(git -C $S/wv rev-parse HEAD^{tree}) status=$(git -C $S/wv status --porcelain | wc -l)"
for cell in checked-native:native:1 checked-x86-64-v3:x86-64-v3:1; do
  IFS=: read name march tests <<< "$cell"
  (cd $S && ROOT=$S/wv MARCH=$march TESTS=$tests ./build_cfg.sh $S/vbin/ide-abp-$name > $S/vbin/ide-abp-$name.log 2>&1; echo "ide-abp-$name rc=$?")
done
sha256sum $S/vbin/ide-abp-checked-native $S/vbin/ide-abp-checked-x86-64-v3
