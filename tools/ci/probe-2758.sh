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
    for test in test-interpreter test-ir-interpreter test-interpreter-native test-interpreter-link; do
        "$bins/$side" "$test" > "$out/$side-$test.log" 2>&1 || { cat "$out/$side-$test.log"; exit 1; }
        tail -1 "$out/$side-$test.log"
    done
done
sha256sum "$bins/base" "$bins/head" > "$out/binaries.sha256"
time_run()
{
    local side="$1" round="$2" size="$3" log="$out/$1-$3-$2" work record
    local args=(test-ssa-source --typed --tier1)
    if [[ $size = one ]]; then
        args+=(--keep "$one")
    else
        args+=("${files[@]}")
    fi
    if ! /usr/bin/time -f '%e %U %S' -o "$log.time" "$bins/$side" "${args[@]}" > "$log.log" 2>&1; then
        cat "$log.log"; return 1
    fi
    echo "TIMING $side $size $round $(cat "$log.time")"
    sed -En '/^test-ssa-source: (file=|totals )/{s@ \([^()]*\)$@@;p;}' "$log.log" | sort > "$log.results"
    if [[ $size = one ]]; then
        work=$(sed -n 's/^test-ssa-source: files kept in //p' "$log.log")
        for record in "$work"/run-*/ssa-source.record; do
            local form="${record%/*}"
            form="${form##*-}"
            # The child creates its result file before interpreting and writes it once at completion.
            # Filesystems without birth time can use the empty capture it created just before that file.
            test ! -s "${record%/*}/ssa-source.out"
            local capture_time
            capture_time=$(stat --printf='%.9Y' "${record%/*}/ssa-source.out")
            stat --printf='%.9W %.9Y %s\n' "$record" | awk -v f="$form" -v start="$capture_time" '{if ($1 > 0) start=$1; if (start <= 0 || $2 < start || !$3) exit 1; printf "%s %.6f\n", f, $2 - start}'
        done | sort > "$log.forms"
        echo "FORM $side $round $(awk '$1=="memory" {print $2}' "$log.forms")"
    fi
}
for round in 1 2 3; do
    order=(base head)
    if [[ $round = 2 ]]; then order=(head base); fi
    for side in "${order[@]}"; do
        time_run "$side" "$round" one
        time_run "$side" "$round" all
    done
    for size in one all; do
        cmp "$out/base-$size-1.results" "$out/base-$size-$round.results"
        cmp "$out/base-$size-1.results" "$out/head-$size-$round.results"
    done
done
for side in base head; do
    log="$out/$side-kept.log"
    "$bins/$side" test-ssa-source --typed --tier1 --keep "${files[@]}" > "$log" 2>&1 || { cat "$log"; exit 1; }
    work=$(sed -n 's/^test-ssa-source: files kept in //p' "$log")
    (cd "$work"; sha256sum run-*/ssa-source.out run-*-tier1-*/output | sort -k2) > "$out/$side-output.sha256"
    test "$(wc -l < "$out/$side-output.sha256")" = 1320
    sed -En '/^test-ssa-source: (file=|totals )/{s@ \([^()]*\)$@@;p;}' "$log" | sort > "$out/$side-kept.results"
    cmp "$out/base-all-1.results" "$out/$side-kept.results"
done
cmp "$out/base-output.sha256" "$out/head-output.sha256"
sha256sum "$out/base-output.sha256" "$out/base-all-1.results"
grep '^test-ssa-source: totals ' "$out/base-all-1.log"
grep '^test-ssa-source: file=' "$out/base-one-1.log"
echo "EVIDENCE_BEGIN"
tar -C "$out" -czf - setup.log binaries.sha256 candidate.patch {base,head}-{one,all}-{1,2,3}.time {base,head}-one-{1,2,3}.forms base-all-1.results base-output.sha256 {base,head}-test-{interpreter,ir-interpreter,interpreter-native,interpreter-link}.log | base64 -w 100
echo "EVIDENCE_END"
