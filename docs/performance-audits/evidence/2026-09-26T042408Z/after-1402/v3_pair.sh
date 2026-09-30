#!/usr/bin/env bash
# Build base then candidate at one path ($M/v3) with -march=x86-64-v3 (Valgrind cannot decode EVEX).
set -euo pipefail
M=$1
for v in base cand; do
  rm -rf $M/v3 && mkdir -p $M/v3/build && cp -r $M/$v/src $M/v3/src && cp -r $M/$v/build/generated $M/v3/build/
  $M/build_variant.sh $M/v3 $M/bin/ide-v3-$v 0 x86-64-v3
done
