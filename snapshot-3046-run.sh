#!/usr/bin/env bash
set -euo pipefail
root="$RUNNER_TEMP/snapshot-3046-independent"
mkdir "$root"
status=0
if ! command -v strace >/dev/null; then sudo apt-get update -qq && sudo apt-get install -y strace; fi
cc -std=c17 -O2 -Werror snapshot-3046/reject-advice.c -o "$root/reject-advice"
for variant in base head; do
    git restore --source=HEAD -- src/buster/compiler/snapshot.c src/buster/compiler/snapshot_test.c
    if test "$variant" = head; then
        git apply snapshot-3046/head.patch
        git apply snapshot-3046/populated.patch
        ./build.sh --config Release
        cp build/compiler "$root/plain-head"
        python3 -u snapshot-3046/reject.py "$root" || status=1
        "$root/reject-advice" 0 "$root/plain-head" test-snapshot > "$root/forced-advice-test.txt" 2>&1 || status=1
        echo "SUITE forced-EINVAL"
        grep -E 'checks, [0-9]+ failed' "$root/forced-advice-test.txt" || status=1
    fi
    python3 snapshot-3046/phase.py instrument
    ./build.sh --config Release
    cp build/compiler "$root/$variant"
done
python3 -u snapshot-3046/phase.py measure "$root"
exit "$status"
