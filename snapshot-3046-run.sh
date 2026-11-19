#!/usr/bin/env bash
set -euo pipefail
root="$RUNNER_TEMP/snapshot-3046-independent"
mkdir "$root"
cc -std=c17 -O2 -Werror -fPIC -shared snapshot-3046/reject-advice.c -o "$root/reject.so"
for variant in base head; do
    git restore --source=HEAD -- src/buster/compiler/snapshot.c src/buster/compiler/snapshot_test.c
    if test "$variant" = head; then
        git apply snapshot-3046/head.patch
        git apply snapshot-3046/populated.patch
        ./build.sh --config Release
        cp build/compiler "$root/plain-head"
        python3 -u snapshot-3046/reject.py "$root"
        LD_PRELOAD="$root/reject.so" "$root/plain-head" test-snapshot > "$root/forced-advice-test.txt" 2>&1
        echo "SUITE forced-EINVAL"
        grep -E 'checks, [0-9]+ failed' "$root/forced-advice-test.txt"
    fi
    python3 snapshot-3046/phase.py instrument
    ./build.sh --config Release
    cp build/compiler "$root/$variant"
done
python3 -u snapshot-3046/phase.py measure "$root"
