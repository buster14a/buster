#!/usr/bin/env bash
set -euo pipefail
root="$RUNNER_TEMP/snapshot-evidence-3046"
mkdir "$root"
git rev-parse HEAD
git hash-object src/buster/compiler/snapshot.c
getconf GNU_LIBC_VERSION
lscpu | grep -E 'Model name|Virtualization|CPU\(s\)'
sha256sum tools/ci/hosted-tools.lock
bash tools/ci/session-toolchain.sh identity
build_witness()
{
    local label=$1
    ./build.sh --config Release
    cp build/compiler "$root/$label"
    grep -m1 -oE -- '-march=[^ "]+[^"]{0,180}' build/compile_commands.json || true
}
for v in base head; do
    if test "$v" = head; then git apply snapshot-3046/head.patch; fi
    build_witness plain-$v
    "$root/plain-$v" test-snapshot
done
status=0
for kind in dynamic static static-pie no-pie shared; do
    python3 -u snapshot-3046/qualify.py plain "$root" "$kind" || status=1
done
for v in head base; do
    if test "$v" = base; then
        git restore --source=HEAD -- src/buster/compiler/snapshot.c src/buster/compiler/snapshot_test.c
    fi
    python3 snapshot-3046/instrument.py src/buster/compiler/snapshot.c
    build_witness trace-$v
done
for kind in dynamic static static-pie no-pie shared; do
    python3 -u snapshot-3046/qualify.py trace "$root" "$kind" || status=1
done
exit "$status"
