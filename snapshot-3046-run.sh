#!/usr/bin/env bash
set -euo pipefail
root="$RUNNER_TEMP/snapshot-3046-layout"
mkdir "$root"
build_variant()
{
    local variant=$1 mode=$2 product=$3
    git restore --source=HEAD -- src/buster/compiler/snapshot.c src/buster/compiler/snapshot_test.c
    if test "$variant" != base; then
        git apply snapshot-3046/head.patch
        git apply "snapshot-3046/$variant.patch"
    fi
    case "$mode" in
        phase) python3 snapshot-3046/phase2.py instrument ;;
        trace) python3 snapshot-3046/instrument.py src/buster/compiler/snapshot.c ;;
    esac
    ./build.sh --config Release
    cp build/compiler "$root/$product"
}
for variant in base ordered populated; do
    build_variant "$variant" phase "$variant"
    "$root/$variant" test-snapshot > "$root/test-$variant.log" 2>&1 || { tail -100 "$root/test-$variant.log"; exit 1; }
    echo "SUITE $variant"
    grep -E 'checks, [0-9]+ failed' "$root/test-$variant.log"
done
for variant in ordered populated; do
    ln -sfn "$root/$variant" "$root/head"
    echo "PHASE COMPARISON $variant"
    python3 -u snapshot-3046/phase2.py measure "$root" | tee "$root/$variant.tsv"
done
choice=$(python3 - "$root" <<'PY'
from pathlib import Path
import sys
candidates=[]
for v in ('ordered','populated'):
 rows=[s.split() for s in (Path(sys.argv[1])/(v+'.tsv')).read_text().splitlines() if s.startswith('PHASE_TIME')]
 assert len(rows)==10
 means={op:sum(float(r[10]) for r in rows if r[2]==op)/5 for op in ('image','write')}
 if min(means.values())>0 and all(float(r[13])>=0 for r in rows):
  candidates.append((means['image'],v))
print(max(candidates)[1] if candidates else 'none')
PY
)
echo "VERIFICATION_CANDIDATE $choice"
if test "$choice" = none; then exit 0; fi
for mode in plain trace; do
    build_variant base "$mode" "$mode-base"
    build_variant "$choice" "$mode" "$mode-head"
    if test "$mode" = plain; then
        "$root/plain-base" test-snapshot
        "$root/plain-head" test-snapshot
        if test "$choice" = populated && command -v strace > /dev/null; then
            mkdir "$root/strace-cache"
            XDG_CACHE_HOME="$root/strace-cache" strace -e trace=madvise -o "$root/madvise.log" "$root/plain-head" tools/ci/qualification.c -o "$root/qualification"
            grep 'MADV_POPULATE_WRITE' "$root/madvise.log"
        fi
    fi
    for kind in dynamic static static-pie no-pie shared; do
        python3 -u snapshot-3046/qualify.py "$mode" "$root" "$kind"
    done
done
