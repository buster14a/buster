#!/usr/bin/env bash
set -euo pipefail
export BUSTER_HOST_ARCH=vector BUSTER_SANITIZER=memory
export TMPDIR="$RUNNER_TEMP/lookup-tmp"
out="$RUNNER_TEMP/lookup-results"
mkdir -p "$TMPDIR" "$out" build
{
    git rev-parse HEAD
    clang --version
    nproc
    lscpu | grep -E 'Model name|CPU\(s\)'
    ./build.sh print-sanitizer-flags --config Release | tr '\0' ' '
    echo
} | tee "$out/setup.log"
git init -q --template= build/c-testsuite
git -C build/c-testsuite fetch -q --depth 1 https://github.com/c-testsuite/c-testsuite 5c7275656d751de0e68b2d340a95b5681858ed07
git -C build/c-testsuite checkout -q --detach FETCH_HEAD
git apply - <<'BUSTER_2758_PROBES'
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
BUSTER_2758_PROBES
git diff > "$out/probes.patch"
./build.sh --config Release --jobs 4 > "$out/build.log" 2>&1 || { tail -100 "$out/build.log"; exit 1; }
for round in 1 2 3; do
    export TMPDIR="$RUNNER_TEMP/lookup-tmp/r$round"
    mkdir -p "$TMPDIR"
    build/compiler test-ssa-source --typed --tier1 --keep build/c-testsuite/tests/single-exec/00040.c | tee "$out/00040-$round.log"
    mapfile -t records < <(find "$TMPDIR" -path '*/run-*-memory/lookup-probes.tsv')
    test "${#records[@]}" = 1
    cp "${records[0]}" "$out/all-memory-$round.tsv"
    cat "$out/all-memory-$round.tsv"
    sha256sum "$(dirname "${records[0]}")/ssa-source.out" | tee "$out/output-$round.sha256"
done
