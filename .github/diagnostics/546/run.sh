#!/usr/bin/env bash
# Temporary ordinary-task experiment. No self-hosted runner, service or policy mutation.
set -euo pipefail
recipe=$(realpath "$1")
e=$(realpath "$2")
mkdir -p "$e" "$e/binaries" "$e/builds" "$e/results" "$e/census"
exec > >(tee "$e/execution.log") 2>&1
set -x
base=2e942e80a87666409cf29d3e24a68d322b9e71fd
test "$(git rev-parse HEAD)" = "$base"
git rev-parse HEAD 'HEAD^{tree}' > "$e/pinned-base.txt"
clang --version > "$e/clang.txt"
cmake --version > "$e/cmake.txt"
ninja --version > "$e/ninja.txt"
uname -a > "$e/kernel.txt"
lscpu > "$e/cpu.txt"
free -b > "$e/memory.txt"
clang -print-resource-dir > "$e/clang-resource.txt"
dpkg-query -W > "$e/packages.txt"
printf 'label\tstatus\texit\n' > "$e/outcomes.tsv"
record() {
  local label=$1
  shift
  local code=0
  "$@" > "$e/$label.log" 2>&1 || code=$?
  if test "$code" = 0; then
    printf '%s\tPASS\t0\n' "$label" | tee -a "$e/outcomes.tsv"
  else
    printf '%s\tFAIL\t%s\n' "$label" "$code" | tee -a "$e/outcomes.tsv"
    tail -60 "$e/$label.log"
  fi
  return "$code"
}
driver="$RUNNER_TEMP/buster-546-build"
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable build.c -o "$driver"
record configure "$driver" generate --config Release --cc clang --ci -DBUSTER_UNITY_BUILD=OFF -DBUSTER_INCLUDE_TESTS=ON
record baseline-original-build "$driver" build --config Release -t ide -- -j2
record baseline-original-self-host timeout 360 "$driver" test_self_host --config Release || true
mkdir -p "$e/frozen"
git archive --format=tar.gz HEAD > "$e/frozen-source.tar.gz"
tar -xzf "$e/frozen-source.tar.gz" -C "$e/frozen"
mkdir -p "$e/frozen/build"
cp -a build/generated "$e/frozen/build/generated"
python3 "$recipe/variants.py" "$PWD" "$e/variants" > "$e/variant-source-sha256.json"
cp "$recipe/regressions.c" "$e/regressions.c"
# The same registered regressions are in every timed compiler. The baseline
# implementation is unchanged; its additional test-only tree is recorded.
python3 - "$recipe/regressions.c" <<'PY'
from pathlib import Path
import sys
p=Path('src/buster/tests/compiler/ir/ir_cfg_test.c')
s=p.read_text()
a='BUSTER_GLOBAL_LOCAL UnitTestResult ir_cfg_publication_tests(UnitTestArguments* arguments)'
assert s.count(a)==1
s=s.replace(a,Path(sys.argv[1]).read_text()+a)
a='    UnitTestResult result = ir_cfg_pool_tests(arguments);'
assert s.count(a)==1
s=s.replace(a,a+'''
    UnitTestResult permutations = ir_cfg_permutation_ownership_tests(arguments);
    result.test_count += permutations.test_count;
    result.succeeded_test_count += permutations.succeeded_test_count;
    UnitTestResult failures = ir_cfg_permutation_failure_tests(arguments);
    result.test_count += failures.test_count;
    result.succeeded_test_count += failures.succeeded_test_count;''')
p.write_text(s)
PY
for variant in baseline delayed forward; do
  cp "$e/variants/$variant.c" src/buster/lib/compiler/ir/ir_cfg.c
  git add src/buster/lib/compiler/ir/ir_cfg.c src/buster/tests/compiler/ir/ir_cfg_test.c
  git write-tree > "$e/builds/$variant.tree"
  git diff "$base" -- src/buster/lib/compiler/ir/ir_cfg.c src/buster/tests/compiler/ir/ir_cfg_test.c > "$e/builds/$variant.patch"
  record "$variant-build" "$driver" build --config Release -t ide -- -j2
  cp build/Release/ide "$e/binaries/$variant"
  sha256sum "$e/binaries/$variant" > "$e/builds/$variant.sha256"
  cp build/CMakeCache.txt "$e/builds/$variant.CMakeCache.txt"
  cp build/compile_commands.json "$e/builds/$variant.compile_commands.json"
  record "$variant-tests" timeout 360 "$driver" build --config Release -t test_all -- -j2 || true
  if test "$variant" = forward; then
    record forward-self-host timeout 360 "$driver" test_self_host --config Release || true
    record forward-modes timeout 180 "$driver" test_mode_matrix --config Release || true
  fi
done
# Same-source, same-root rebuild control, not just an immutable A/A run.
cp "$e/variants/baseline.c" src/buster/lib/compiler/ir/ir_cfg.c
record baseline-rebuild "$driver" build --config Release -t ide -- -j2
cp build/Release/ide "$e/binaries/baseline-rebuild"
sha256sum "$e/binaries/baseline-rebuild" > "$e/builds/baseline-rebuild.sha256"
record baseline-rebuild-identity cmp "$e/binaries/baseline" "$e/binaries/baseline-rebuild" || true
record harness-tests "$driver" bench_throughput self-test
# Fixed two-round experiments; no optional sampling or rerun-to-green.
for variant in baseline delayed forward; do
  record "$variant-synthetic" "$driver" bench_throughput run --baseline "$e/binaries/baseline" --candidate "$e/binaries/$variant" --baseline-id "$(cat "$e/builds/baseline.tree")" --candidate-id "$(cat "$e/builds/$variant.tree")" --output "$e/results/$variant-synthetic" --profile ci --mode fast --pairs 20 --warmups 2 --cpu auto --no-guard --require-identical-output || true
  # Two pairs per round are a complete-artifact diagnostic/fixed-point check,
  # explicitly not a statistically powered throughput acceptance experiment.
  record "$variant-frozen" "$driver" bench_throughput run --baseline "$e/binaries/baseline" --candidate "$e/binaries/$variant" --baseline-id "$(cat "$e/builds/baseline.tree")" --candidate-id "$(cat "$e/builds/$variant.tree")" --output "$e/results/$variant-frozen" --profile smoke --workload tiny_startup --mode fast --pairs 2 --warmups 1 --cpu auto --no-guard --require-identical-output --self-host-root "$e/frozen" --self-host-generated "$e/frozen/build/generated" || true
done
for frontend in -ffrontend-ssa -fno-frontend-ssa; do
  record "forward-matrix-$frontend" "$driver" bench_throughput run --baseline "$e/binaries/baseline" --candidate "$e/binaries/forward" --baseline-id "$(cat "$e/builds/baseline.tree")" --candidate-id "$(cat "$e/builds/forward.tree")" --output "$e/results/forward-matrix-$frontend" --profile smoke --mode all --pairs 2 --warmups 1 --cpu auto --no-guard --require-identical-output --flag "$frontend" || true
done
# Restore baseline code before configuring a separate instrumented build.
cp "$e/variants/baseline-diagnostic.c" src/buster/lib/compiler/ir/ir_cfg.c
record census-configure "$driver" generate --build-directory build-census --config Release --cc clang --ci -DBUSTER_UNITY_BUILD=OFF -DBUSTER_INCLUDE_TESTS=OFF -DBUSTER_BENCH_ALLOCATIONS=ON
record census-inputs "$driver" bench_throughput generate --output "$e/census-inputs" --profile ci
for variant in baseline forward; do
  cp "$e/variants/$variant-diagnostic.c" src/buster/lib/compiler/ir/ir_cfg.c
  record "$variant-census-build" "$driver" build --build-directory build-census --config Release -t ide -- -j2
  compiler=$(realpath build-census/Release/ide)
  cp "$compiler" "$e/binaries/$variant-diagnostic"
  sha256sum "$compiler" > "$e/builds/$variant-diagnostic.sha256"
  cp build-census/CMakeCache.txt "$e/builds/$variant-diagnostic.CMakeCache.txt"
  cp build-census/compile_commands.json "$e/builds/$variant-diagnostic.compile_commands.json"
  for frontend in -ffrontend-ssa -fno-frontend-ssa; do
    label="$variant-unity-$frontend"
    (cd "$e/frozen" && BUSTER_ALLOCATION_CENSUS=1 "$compiler" cc -g -O0 -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 "$frontend" -fregister-allocator=fast -fsource-metrics="$e/census/$label.metrics" src/buster/apps/ide/ide.c -lm -o "$e/census/$label.exe") > "$e/census/$label.stdout" 2> "$e/census/$label.stderr" && printf '%s\tPASS\t0\n' "$label" >> "$e/outcomes.tsv" || printf '%s\tFAIL\t1\n' "$label" >> "$e/outcomes.tsv"
    if test -s "$e/census/$label.exe"; then sha256sum "$e/census/$label.exe" >> "$e/census/artifacts.sha256"; fi
    python3 tools/allocation_census.py --input "$e/census/$label.stderr" --output "$e/census/$label-report" --label "$label diagnostic only" || true
  done
  while IFS= read -r source; do
    label="$variant-$(basename "$source" .c)"
    BUSTER_ALLOCATION_CENSUS=1 "$compiler" cc -g0 -O0 -c -fregister-allocator=fast "$source" -o "$e/census/$label.o" > "$e/census/$label.stdout" 2> "$e/census/$label.stderr"
    sha256sum "$e/census/$label.o" >> "$e/census/artifacts.sha256"
  done < <(find "$e/census-inputs" -name '*.c' -type f | sort)
done
cp "$e/variants/forward.c" src/buster/lib/compiler/ir/ir_cfg.c
git add src/buster/lib/compiler/ir/ir_cfg.c src/buster/tests/compiler/ir/ir_cfg_test.c
git write-tree > "$e/final-candidate.tree"
git diff --check "$base"
cat "$e/outcomes.tsv"
# Keep all raw metadata, commands, logs, counts and hashes. Large repeated
# sample binaries are not uploaded; immutable compared host binaries are.
find "$e/results" -type f \( -name '*.exe' -o -name '*.o' -o -name '*.s' \) -delete
find "$e/census" -type f \( -name '*.exe' -o -name '*.o' \) -delete
rm -rf "$e/frozen"
