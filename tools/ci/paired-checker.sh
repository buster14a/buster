#!/usr/bin/env bash
# Collect one bounded #509 session under the existing job lock. The hosted
# record job evaluates the sealed resource, including partial captures.
set -euo pipefail
plan=tools/performance/plans/checker-sqlite.json
out="${RUNNER_TEMP:?}/paired-checker"
mkdir "$out"
ab=${BUSTER_BENCHMARK_BUILDS:?}
base=$(git -C "$ab/base" rev-parse HEAD)
head=$(git rev-parse HEAD)
[[ "$base" == "${PAIRED_BASE:?}" && "$head" == "${GITHUB_SHA:?}" ]]
hash() { sha256sum | cut -d' ' -f1; }
manifest="$out/manifest.bmf.json"
corpus=${BUSTER_CORPUS:?}
corpus_hash=391e729af6587355050881752558e790299cfe638c7dc7803bd028a8535ebcbd
printf '%s  %s\n' "$corpus_hash" "$corpus/MANIFESTS.sha256" | sha256sum --check --status
sqlite="$corpus/sqlite"
printf '%s  %s\n' 0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d "$sqlite/sqlite3.c" | sha256sum --check --status
options=(-fsyntax-only --predefines "$corpus/provenance/gnu17-pinned-predefines.txt" --clang-features --target=x86_64-linux-gnu -march=znver5 -std=gnu17
         -nostdinc -isystem "$corpus/headers/usr/lib/clang/23/include" -isystem "$corpus/headers/usr/local/include" -isystem "$corpus/headers/usr/include"
         -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE "-I$sqlite" "$sqlite/sqlite3.c")
# The builds barrier verifies the relative checker offsets. A legacy padding
# sweep cannot supply these units.
[[ "${BUSTER_CHECKER_PLACEMENTS:?}" == checker ]]
builds=()
for role in main head main-again; do
    side=$role
    [[ $role != main-again ]] || side=main
    for offset in 0 16 32 48; do
        builds+=(--build "$role" "$ab/$side/compiler-$offset")
    done
done
# Record immutable per-side receipts before launching either executable.
bins() {
    for offset in 0 16 32 48; do hash < "$ab/$1/compiler-$offset"; done | jq -R . | jq -s .
}
toolchain=$( { sha256sum < "$(command -v clang)"; sha256sum < "$(command -v ld)"; clang --version; } | hash)
host=$( { printf '%s\n' "${PAIRED_HOST:?}"; cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor /sys/kernel/mm/transparent_hugepage/enabled; } | hash)
harness=$(sha256sum src/buster/compiler/profile.c src/buster/compiler/profile_order.h | hash)
config_id() {
    git ls-tree -r "$1" -- build.sh CMakeLists.txt CMakePresets.json cmake/BusterSettings.cmake tools/ci/setup-build-tools.sh | hash
}
base_config=$(config_id "$base")
head_config=$(config_id "$head")
recipe=$( { printf '%s\n' "$head_config" "$BUSTER_HOST_ARCH" "$BUSTER_DEBUG_INFO"; sha256sum tools/ci/paired-checker.sh tools/ci/build-benchmarks.sh; } | hash)
jq -n --arg id "paired-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT" --arg date "$(date -u +%FT%TZ)" \
   --arg base "$base" --arg head "$head" --argjson a "$(bins main)" --argjson b "$(bins head)" \
   --arg plan "$(hash < "$plan")" --arg toolchain "$toolchain" --arg corpus "$corpus_hash" \
   --arg host "$host" --arg host_label "$PAIRED_HOST" --arg harness "$harness" --arg recipe "$recipe" \
   --arg base_config "$base_config" --arg head_config "$head_config" \
   '{format:"buster-benchmark",version:1,id:$id,kind:"measured",status:"partial",recorded_at:$date,
     producer:{name:"time-runs",version:"1",revision:$head},resources:[],limitations:["One CPU and four verified checker offsets"],
     extensions:{paired:{plan_sha256:$plan,toolchain:$toolchain,corpus:$corpus,host:$host,harness:$harness,recipe:$recipe,
       host_label:$host_label,build_config:{main:$base_config,head:$head_config},
       main:{source:$base,binaries:$a},head:{source:$head,binaries:$b},"main-again":{source:$base,binaries:$a}}}}' > "$manifest"
[[ "$base_config" == "$head_config" ]] || { echo "incompatible build recipes" >&2; exit 1; }
status=complete
rounds=$(jq -er .rounds "$plan")
copies=$(jq -er .copies "$plan")
warmups=$(jq -er .warmups "$plan")
timeout --kill-after=5s 90s taskset -c 2 ./build/compiler time-runs --runs "$rounds" --warmups "$warmups" --copies "$copies" "${builds[@]}" --list "${options[@]}" > "$out/observations.txt" || status=partial
jq --arg status "$status" --arg sha "$(hash < "$out/observations.txt")" --arg bytes "$(stat -c %s "$out/observations.txt")" \
   '.status=$status | .resources=[{id:"observations",format:"time-runs-v1",path:"observations.txt",sha256:$sha,bytes:$bytes}]' "$manifest" > "$manifest.tmp"
mv "$manifest.tmp" "$manifest"
