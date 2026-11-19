#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
export BUSTER_HOST_ARCH=vector BUSTER_SANITIZER="$BUSTER_SAMPLE_SANITIZER"
case "$BUSTER_SANITIZER" in memory|address) ;; *) exit 2 ;; esac
out="$RUNNER_TEMP/lookup-results"
bins="$RUNNER_TEMP/lookup-compilers"
export TMPDIR="$RUNNER_TEMP/lookup-comparison"
mkdir -p "$out" "$bins" "$TMPDIR" build
{
    git rev-parse HEAD
    clang --version
    uname -srmo
    nproc
    lscpu | grep -E 'Model name|CPU\(s\)'
    ./build.sh print-sanitizer-flags --config Release | tr '\0' ' '
    echo
    ./build.sh print-host-flags --config Release | tr '\0' ' '
    echo
} | tee "$out/setup.log"
git init -q --template= build/c-testsuite
git -C build/c-testsuite fetch -q --depth 1 https://github.com/c-testsuite/c-testsuite 5c7275656d751de0e68b2d340a95b5681858ed07
git -C build/c-testsuite checkout -q --detach FETCH_HEAD
mapfile -t files < <(sed -n 's|^\([0-9][0-9]*\)$|build/c-testsuite/tests/single-exec/\1.c|p' tests/c-testsuite/subset.txt)
test "${#files[@]}" = 220
one=build/c-testsuite/tests/single-exec/00040.c
cp tools/ci/2758-candidate.patch "$out/candidate.patch"
build_compiler()
{
    local label="$1"
    if ! ./build.sh --config Release --jobs 4 > "$out/build-$label.log" 2>&1; then
        tail -80 "$out/build-$label.log" >&2
        return 1
    fi
    cp --reflink=auto build/compiler "$bins/$label"
}
for side in base head; do
    if [[ $side = head ]]; then git apply "$out/candidate.patch"; fi
    build_compiler "$side"
done
sha256sum "$bins/base" "$bins/head" > "$out/binaries.sha256"

cc -std=c17 -O2 -Wall -Wextra -Werror tools/ci/2758-memory-clock.c -o "$bins/memory-clock"
: > "$out/clocks.tsv"
for round in 1 2 3 4 5 6; do
    order=(base head)
    if (( round % 2 == 0 )); then order=(head base); fi
    for side in "${order[@]}"; do
        label="$side-$round"
        "$bins/memory-clock" "$out/$label.clock" "$bins/$side" test-ssa-source --typed --tier1 "$one" > "$out/$label.log" 2>&1 || { cat "$out/$label.log"; exit 1; }
        grep -E '^test-ssa-source: file=.* run=as expected steps=' "$out/$label.log" > "$out/$label.steps"
        cmp "$out/base-1.steps" "$out/$label.steps"
        echo "CLOCK $side $round $(cat "$out/$label.clock")" | tee -a "$out/clocks.tsv"
    done
done
sha256sum "$out/base-1.steps"
