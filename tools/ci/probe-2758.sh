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
cat > "$out/base-probes.patch" <<'BUSTER_BASE_PROBES'
--- a/src/buster/compiler/interpreter.c
+++ b/src/buster/compiler/interpreter.c
@@ -91,6 +91,19 @@
     // the search from the newest down finds first, and older memories (a global, the heap), which it finds last; one
     // hint for the last access sent each of those back to the search (#2758).
     uint32_t recent[1u << BUSTER_INTERPRETER_RECENT_LOG2];
+    uint64_t probe_hits;
+    uint64_t probe_misses;
+    uint64_t probe_hint_checks;
+    uint64_t probe_scans;
+    uint64_t probe_same_granule;
+    uint64_t probe_different_granule;
+    uint64_t probe_empty;
+    uint64_t probe_uncacheable;
+    uint64_t probe_absent;
+    uint32_t probe_max;
+    uint64_t probe_histogram[129];
+    uint64_t probe_granule[1u << BUSTER_INTERPRETER_RECENT_LOG2];
+    bool probe_known[1u << BUSTER_INTERPRETER_RECENT_LOG2];
     bool check_memory;
     bool check_undefined;
     uint64_t stack_serial;
@@ -357,13 +370,39 @@
     const uint32_t entry = interpreter_memory_recent(address);
     const uint32_t hint = runtime->recent[entry];
     uint32_t found = hint < prefix && interpreter_memory_holds(&memories[hint], address, bytes) ? hint : count;
+    const bool recent_hit = found != count;
+    uint32_t inspected = 0;
+    runtime->probe_hint_checks += hint < prefix;
     for (uint32_t i = prefix; found == count && i > 0; i -= 1)
     {
+        inspected += 1;
         found = interpreter_memory_holds(&memories[i - 1], address, bytes) ? i - 1 : count;
     }
     for (uint32_t i = prefix; found == count && i < count; i += 1)
     {
+        inspected += 1;
         found = interpreter_memory_holds(&memories[i], address, bytes) ? i : count;
+    }
+    if (recent_hit)
+    {
+        runtime->probe_hits += 1;
+    }
+    else
+    {
+        runtime->probe_misses += 1;
+        runtime->probe_scans += inspected;
+        runtime->probe_max = inspected > runtime->probe_max ? inspected : runtime->probe_max;
+        runtime->probe_histogram[inspected < 128 ? inspected : 128] += 1;
+        runtime->probe_empty += !runtime->probe_known[entry];
+        runtime->probe_same_granule += runtime->probe_known[entry] && runtime->probe_granule[entry] == (address >> 3);
+        runtime->probe_different_granule += runtime->probe_known[entry] && runtime->probe_granule[entry] != (address >> 3);
+        runtime->probe_uncacheable += found >= prefix && found < count;
+        runtime->probe_absent += found == count;
+    }
+    if (found != count)
+    {
+        runtime->probe_known[entry] = true;
+        runtime->probe_granule[entry] = address >> 3;
     }
     // The entry keeps its hint unless another memory held the range: a store only when it changes (#2758).
     if (found != count && found != hint)
--- a/src/buster/compiler/ir_interpreter.c
+++ b/src/buster/compiler/ir_interpreter.c
@@ -1788,6 +1788,22 @@
     }
     result.status = status;
     result.steps = steps;
+    if (steps > UINT64_C(1000000000))
+    {
+        FILE* probes = fopen("lookup-probes.tsv", "w");
+        if (probes != NULL)
+        {
+            fprintf(probes, "steps\t%" PRIu64 "\nhits\t%" PRIu64 "\nmisses\t%" PRIu64 "\nhint_checks\t%" PRIu64 "\nfallback_regions\t%" PRIu64 "\nmaximum\t%u\nempty_entry\t%" PRIu64 "\nsame_granule\t%" PRIu64 "\ndifferent_granule\t%" PRIu64 "\noutside_prefix\t%" PRIu64 "\nabsent\t%" PRIu64 "\n", steps, runtime.probe_hits, runtime.probe_misses, runtime.probe_hint_checks, runtime.probe_scans, runtime.probe_max, runtime.probe_empty, runtime.probe_same_granule, runtime.probe_different_granule, runtime.probe_uncacheable, runtime.probe_absent);
+            for (uint32_t bucket = 0; bucket <= 128; bucket += 1)
+            {
+                if (runtime.probe_histogram[bucket] != 0)
+                {
+                    fprintf(probes, "scan_%u\t%" PRIu64 "\n", bucket, runtime.probe_histogram[bucket]);
+                }
+            }
+            fclose(probes);
+        }
+    }
     interpreter_native_release(&runtime);
     arena_pop_to(arena, mark);
     return result;
BUSTER_BASE_PROBES
build_compiler()
{
    local label="$1"
    if ! ./build.sh --config Release --jobs 4 > "$out/build-$label.log" 2>&1; then
        tail -80 "$out/build-$label.log" >&2
        return 1
    fi
    cp --reflink=auto build/compiler "$bins/$label"
}
probes()
{
    local side="$1" work
    git apply "$out/$side-probes.patch"
    build_compiler "$side-probes"
    "$bins/$side-probes" test-ssa-source --typed --tier1 --keep "$one" > "$out/probe-$side.log" 2>&1 || { cat "$out/probe-$side.log"; return 1; }
    work=$(sed -n 's/^test-ssa-source: files kept in //p' "$out/probe-$side.log")
    local records=("$work"/run-*-memory/lookup-probes.tsv)
    test "${#records[@]}" = 1 && test -s "${records[0]}"
    cp "${records[0]}" "$out/probe-$side.tsv"
    echo "PROBES $side"
    cat "$out/probe-$side.tsv"
    git apply -R "$out/$side-probes.patch"
    rm -- "$bins/$side-probes"
}
read_candidate()
{
    # Base is already built and counted. The measured patch is published after the independent baseline probe.
    local ref=refs/heads/codex/2758-cloud-probes
    for attempt in {1..60}; do
        if git fetch -q --depth=1 --no-tags origin "$ref" &&
           git cat-file -e FETCH_HEAD:tools/ci/2758-head-probes.patch 2>/dev/null &&
           git cat-file -e FETCH_HEAD:tools/ci/2758-candidate.patch 2>/dev/null; then
            git show FETCH_HEAD:tools/ci/2758-candidate.patch > "$out/candidate.patch"
            git show FETCH_HEAD:tools/ci/2758-head-probes.patch > "$out/head-probes.patch"
            git rev-parse FETCH_HEAD | tee "$out/candidate-commit.txt"
            git apply --numstat "$out/candidate.patch" | awk -F '\t' '$3 !~ /^src\/buster\/compiler\/interpreter(_test)?\.c$/ {bad=1} END {exit (bad || NR==0)}'
            git apply --numstat "$out/head-probes.patch" | awk -F '\t' '$3 !~ /^src\/buster\/compiler\/(ir_)?interpreter\.c$/ {bad=1} END {exit (bad || NR==0)}'
            git apply --check "$out/candidate.patch"
            return 0
        fi
        sleep 20
    done
    echo "The candidate has not been published yet." >&2
    return 1
}
for side in base head; do
    if [[ $side = head ]]; then
        read_candidate
        git apply "$out/candidate.patch"
    fi
    build_compiler "$side"
    for test in test-interpreter test-ir-interpreter test-interpreter-native test-interpreter-link; do
        "$bins/$side" "$test" > "$out/$side-$test.log" 2>&1 || { cat "$out/$side-$test.log"; exit 1; }
        tail -1 "$out/$side-$test.log"
    done
    probes "$side"
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
    grep -E '^test-ssa-source: (file=|totals )' "$log.log" | sort > "$log.results"
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
    grep -E '^test-ssa-source: (file=|totals )' "$log" | sort > "$out/$side-kept.results"
    cmp "$out/base-all-1.results" "$out/$side-kept.results"
done
cmp "$out/base-output.sha256" "$out/head-output.sha256"
sha256sum "$out/base-output.sha256" "$out/base-all-1.results"
grep '^test-ssa-source: totals ' "$out/base-all-1.log"
grep '^test-ssa-source: file=' "$out/base-one-1.log"
echo "EVIDENCE_BEGIN"
tar -C "$out" -czf - setup.log binaries.sha256 candidate.patch base-probes.patch head-probes.patch probe-{base,head}.tsv {base,head}-{one,all}-{1,2,3}.time {base,head}-one-{1,2,3}.forms base-all-1.results base-output.sha256 {base,head}-test-{interpreter,ir-interpreter,interpreter-native,interpreter-link}.log | base64 -w 100
echo "EVIDENCE_END"
