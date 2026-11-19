#!/usr/bin/env bash
set -euo pipefail
root="$RUNNER_TEMP/snapshot-3046-phases"
mkdir "$root"
for v in base head; do
    git restore --source=HEAD -- src/buster/compiler/snapshot.c
    if test "$v" = head; then git apply snapshot-3046/head.patch; fi
    python3 snapshot-3046/phase.py instrument
    ./build.sh --config Release
    cp build/compiler "$root/$v"
done
python3 -u snapshot-3046/phase.py measure "$root"
