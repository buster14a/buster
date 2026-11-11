#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# tools/ci/vector-draw.sh's collector, candidates, product waits and verdict (#2033, #2032) against a fake `gh` that
# applies each call's own --jq filter to a fixture of the run's artifacts and jobs: an assignment by rank, early when
# enough qualified and late when every candidate decided or ended; a later draw's missing items; a pool of four
# configurations, the costliest items first; a candidate that takes its item and its configuration's settings, one
# ranked out, one that doesn't qualify; a rerun of the failed jobs, whose holder retakes its item or fails on a CPU that
# doesn't qualify; an item that waits for its configuration's product, from the attempt its builder last ran in, and
# fails when the builder ended without one, when no candidate can build it, or on two; and the verdict's passes, its
# incomplete and failed items, its products and its reruns. No network.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
checks=0
fail() { printf 'vector-draw fixture: %s\n' "$*" >&2; exit 1; }
pass() { checks=$((checks + 1)); }
command -v jq > /dev/null || fail 'jq is required'

# The row's items as hosted-correctness.yml lists them, the costliest first; a pull request's row is `none` alone.
every='m1 m2 m3 m4 a1 a2 a3 a4 t1 t2 t3 t4 n1 n2 n3 n4'
export LC_ALL=C GITHUB_REPOSITORY=buster14a/buster GITHUB_RUN_ID=7 BUSTER_VECTOR_CONFIGURATIONS=none BUSTER_SANITIZER_OPTIMIZATION=3
export BUSTER_VECTOR_ITEMS="$every" VECTOR_DRAW_POLL=0 VECTOR_DRAW_COLLECT_LIMIT=2 VECTOR_DRAW_TAKE_LIMIT=2 VECTOR_DRAW_PRODUCT_LIMIT=2 VECTOR_DRAW_PRODUCT_POLL=0 GH_TOKEN=fixture
export RUNNER_TEMP="$work/runner" FIXTURE="$work/api"
mkdir -p "$RUNNER_TEMP" "$FIXTURE" "$work/bin"
p=vector-O3
products=vector-product-O3
sha=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
other=fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210

# gh api [--paginate] [-H ...] PATH --jq FILTER: the filter over the fixture's JSON for that path.
cat > "$work/bin/gh" <<'GH'
#!/usr/bin/env bash
path='' filter='.'
while [ $# -gt 0 ]; do
    case "$1" in
        api|--paginate) shift ;;
        --jq) filter=$2; shift 2 ;;
        -H) shift 2 ;;
        *) path=$1; shift ;;
    esac
done
case "$path" in
    */artifacts\?*) file="$FIXTURE/artifacts.json" ;;
    */attempts/*/jobs\?*) attempt=${path#*/attempts/}; file="$FIXTURE/jobs-${attempt%%/*}.json" ;;
    *) echo "fake gh: unexpected $path" >&2; exit 1 ;;
esac
[ -e "$file" ] || { echo "fake gh: no $file" >&2; exit 1; }
jq -r "$filter" "$file"
GH
chmod +x "$work/bin/gh"
export PATH="$work/bin:$PATH"

# The run's artifacts, by name.
artifacts()
{
    printf '%s\n' "$@" | jq -R . | jq -s '{ artifacts: map({ name: . }) }' > "$FIXTURE/artifacts.json"
}
# Attempt $1's jobs: NAME|STATUS|ATTEMPT[|CONCLUSION] each.
jobs()
{
    local attempt=$1
    shift
    [ $# -gt 0 ] || { printf '{ "jobs": [] }\n' > "$FIXTURE/jobs-$attempt.json"; return 0; }
    printf '%s\n' "$@" | jq -R 'split("|") | { name: .[0], status: .[1], run_attempt: (.[2] | tonumber), conclusion: (if (.[3] // "") == "" then null else .[3] end) }' | jq -s '{ jobs: . }' > "$FIXTURE/jobs-$attempt.json"
}
label() { printf '(Clang 23, %s, O3)' "$BUSTER_VECTOR_CONFIGURATIONS"; }
draw() { printf 'Tests on Ubuntu 26.04 (vector, %s, O3) / Vector draw %s, candidate %s %s|%s|%s|%s' "$BUSTER_VECTOR_CONFIGURATIONS" "$1" "$2" "$(label)" "$3" "$4" "${5:-}"; }
collector() { printf "Tests on Ubuntu 26.04 (vector, %s, O3) / Vector draw %s's assignment %s|%s|%s" "$BUSTER_VECTOR_CONFIGURATIONS" "$1" "$(label)" "$2" "$3"; }
output() { sed -n "s/^$1=//p" "$GITHUB_OUTPUT" | tail -n 1; }
record()
{
    printf 'level\t%s\ncandidate\t%s\nrunner\tR%s-%s\ncpu\t%s\ncpus\t4\nprobe\t%s\nmissing\tnone\n' "$1" "$2" "$1" "$2" "$3" "$4" > "$5"
}
draw_script="$root/tools/ci/vector-draw.sh"

# --- The collector ---
export GITHUB_RUN_ATTEMPT=1 GITHUB_OUTPUT="$work/output" GITHUB_ENV="$work/env"
: > "$GITHUB_OUTPUT"
artifacts "$p-1-probe-1-5-yes" "$p-1-probe-1-2-no" "$p-1-probe-1-9-yes" "$p-1-probe-1-3-yes" "$p-1-probe-1-12-yes" other-artifact
jobs 1
bash "$draw_script" assign 1 12 4 > "$work/log"
[ "$(output name)" = "$p-1-assign-1-n1x3-n2x5-n3x9-n4x12" ] || fail "an early assignment by rank: $(output name)"
[ "$(output count)" = 0 ] && [ "$(output candidates)" = '[]' ] || fail 'a complete draw asks for another'
pass

# A first draw of another size than 3 per item is refused.
if bash "$draw_script" assign 1 6 4 > "$work/log" 2>&1; then fail 'a first draw of 6 candidates for 4 items'; fi
grep -q 'at least 3 per item' "$work/log" || fail 'a first draw of the wrong size refused without its reason'
pass

artifacts "$p-1-probe-2-5-yes" "$p-1-probe-2-2-no" "$p-1-probe-2-11-yes" "$p-1-probe-2-1-no"
jobs 1 "$(draw 2 3 completed 1)" "$(draw 2 4 completed 1)" "$(draw 2 6 in_progress 1)"
bash "$draw_script" assign 2 6 4 > "$work/log"
[ "$(output name)" = "$p-1-assign-2-n1x5-n2x11" ] || fail "an assignment once every candidate decided or ended: $(output name)"
[ "$(output count)" = 8 ] && [ "$(output candidates)" = '[1, 2, 3, 4, 5, 6, 7, 8]' ] || fail 'the next draw: 4 per missing item'
pass

# Too few decided: the limit (2 s here) assigns what qualified, with a warning.
jobs 1
bash "$draw_script" assign 2 12 4 > "$work/log" 2>&1
grep -q '::warning title=Vector draw::' "$work/log" || fail 'a collector past its limit warns'
[ "$(output name)" = "$p-1-assign-2-n1x5-n2x11" ] || fail 'a collector past its limit assigns what qualified'
pass

# A second draw: the first's items aren't missing, the costliest missing first.
artifacts "$p-1-assign-1-n1x5-n3x11" "$p-1-probe-2-2-yes" "$p-1-probe-2-7-yes" "$p-1-probe-2-1-no"
bash "$draw_script" assign 2 8 5 > "$work/log"
[ "$(output name)" = "$p-1-assign-2-n2x2-n4x7" ] || fail "a second draw's assignment: $(output name)"
[ "$(output count)" = 0 ] || fail 'a second draw that covers its items asks for another'
pass

# Nobody qualified: an empty assignment, which the waiting candidates and the verdict read.
artifacts "$p-1-probe-1-1-no" "$p-1-probe-1-2-no"
bash "$draw_script" assign 1 12 4 > "$work/log"
[ "$(output name)" = "$p-1-assign-1" ] || fail 'a draw where nobody qualified'
pass

# The pool of four configurations: 16 items, 48 candidates in the first draw, the costliest first across them.
export BUSTER_VECTOR_CONFIGURATIONS='none address thread memory'
qualifying=()
for candidate in 2 4 7 9 10 13 15 20 21 22 30 31 40; do qualifying+=("$p-1-probe-1-$candidate-yes"); done
artifacts "${qualifying[@]}"
jobs 1
bash "$draw_script" assign 1 48 4 > "$work/log" 2>&1
[ "$(output name)" = "$p-1-assign-1-m1x2-m2x4-m3x7-m4x9-a1x10-a2x13-a3x15-a4x20-t1x21-t2x22-t3x30-t4x31-n1x40" ] || fail "a pool's first draw by cost: $(output name)"
[ "$(output count)" = 12 ] || fail "a pool's next draw: 4 per missing item, $(output count)"
pass
artifacts "$p-1-assign-1-m1x2-m2x4-m3x7-m4x9-a1x10-a2x13-a3x15-a4x20-t1x21-t2x22-t3x30-t4x31-n1x40" "$p-1-probe-2-3-yes" "$p-1-probe-2-5-yes"
jobs 1 "$(draw 2 1 completed 1)" "$(draw 2 2 completed 1)" "$(draw 2 4 completed 1)" "$(draw 2 6 completed 1)" "$(draw 2 7 completed 1)" "$(draw 2 8 completed 1)" "$(draw 2 9 completed 1)" "$(draw 2 10 completed 1)" "$(draw 2 11 completed 1)" "$(draw 2 12 completed 1)"
bash "$draw_script" assign 2 12 5 > "$work/log" 2>&1
[ "$(output name)" = "$p-1-assign-2-n2x3-n3x5" ] && [ "$(output count)" = 5 ] || fail "a pool's second draw: $(output name), $(output count)"
pass
# Configurations whose initials agree, or items that aren't a configuration's shards 1 to N, are refused.
if BUSTER_VECTOR_CONFIGURATIONS='none nothing' bash "$draw_script" assign 1 12 4 > "$work/log" 2>&1; then fail 'two configurations of one initial'; fi
if BUSTER_VECTOR_ITEMS='n1 n2 n4' BUSTER_VECTOR_CONFIGURATIONS=none bash "$draw_script" assign 1 9 4 > "$work/log" 2>&1; then fail 'a configuration without its shard 3'; fi
if BUSTER_VECTOR_ITEMS='n1 n2 n2' BUSTER_VECTOR_CONFIGURATIONS=none bash "$draw_script" assign 1 9 4 > "$work/log" 2>&1; then fail 'a shard twice'; fi
if BUSTER_VECTOR_ITEMS='m1 m2' BUSTER_VECTOR_CONFIGURATIONS=none bash "$draw_script" assign 1 6 4 > "$work/log" 2>&1; then fail 'a row without its items'; fi
pass

# --- The candidates ---
export BUSTER_VECTOR_CONFIGURATIONS=none
take()
{
    : > "$GITHUB_ENV"
    record "$1" "$2" "AMD EPYC 9V45" "$3" "$work/record"
    bash "$draw_script" take "$1" "$2" "$3" "$work/record" > "$work/log" 2>&1
}
setting() { sed -n "s/^$1=//p" "$GITHUB_ENV"; }
artifacts "$p-1-assign-1-n1x3-n2x5-n3x9-n4x12"
jobs 1
take 1 9 qualified || fail 'a candidate that takes its item'
[ "$(setting BUSTER_VECTOR_ITEM)" = n3 ] && [ "$(setting BUSTER_SHARD)" = 3 ] && [ "$(setting BUSTER_SANITIZER)" = none ] || fail 'the item a candidate takes'
[ "$(setting BUSTER_VECTOR_SHARDS)" = 4 ] && [ "$(setting BUSTER_VECTOR_BUILDER)" = 0 ] || fail "the item's shard count and builder"
grep -qx "$(printf 'decision\ttook n3')" "$work/record" && grep -qx 'Vector item n3: none, shard 3 of 4' "$work/log" || fail "the take's record and costs line"
take 1 3 qualified && [ "$(setting BUSTER_VECTOR_BUILDER)" = 1 ] || fail "item 1's holder builds its configuration's product"
take 1 10 qualified || fail 'a candidate ranked out'
[ ! -s "$GITHUB_ENV" ] && grep -q "$(printf 'decision\tredraw: candidates of a higher rank')" "$work/record" || fail 'a candidate ranked out takes nothing'
take 1 2 'unqualified (status 1)' || fail 'a candidate that does not qualify'
[ ! -s "$GITHUB_ENV" ] && grep -q "$(printf 'decision\tredraw: the CPU does not qualify')" "$work/record" || fail 'a CPU that does not qualify takes nothing'
artifacts "$p-1-probe-1-3-yes"
take 1 3 qualified || fail 'a candidate whose collector never assigns'
[ ! -s "$GITHUB_ENV" ] && grep -q '::warning' "$work/log" || fail 'a candidate without an assignment ends as a redraw'
pass
# A pool's candidate takes its configuration's settings.
export BUSTER_VECTOR_CONFIGURATIONS='none address thread memory'
artifacts "$p-1-assign-1-m1x2-t3x4-a2x6"
take 1 4 qualified || fail "a pool's candidate"
[ "$(setting BUSTER_SANITIZER)" = thread ] && [ "$(setting BUSTER_SHARD)" = 3 ] && [ "$(setting BUSTER_VECTOR_ITEM)" = t3 ] || fail "a pool's candidate's configuration"
grep -qx 'Vector item t3: thread, shard 3 of 4' "$work/log" || fail "a pool's costs line"
export BUSTER_VECTOR_CONFIGURATIONS=none
pass

# A rerun of the failed jobs (attempt 2, whose collectors didn't run again): the holder retakes its item.
export GITHUB_RUN_ATTEMPT=2
artifacts "$p-1-assign-1-n1x3-n2x5" "$p-1-assign-2-n3x1-n4x6" "$p-1-evidence-n1"
jobs 2 "$(collector 1 completed 1)" "$(draw 2 6 in_progress 2)"
take 2 6 qualified || fail 'a rerun holder that qualifies'
[ "$(setting BUSTER_SHARD)" = 4 ] && grep -qx "$(printf 'decision\ttook n4 again (assigned in attempt 1)')" "$work/record" || fail 'a rerun holder retakes its item'
if take 2 6 'unqualified (status 1)'; then fail 'a rerun holder on a CPU that does not qualify passes'; fi
[ ! -s "$GITHUB_ENV" ] && grep -q 'No test failed: rerun the failed jobs to draw again' "$work/log" || fail 'a rerun holder that does not qualify asks to draw again'
take 2 7 qualified || fail 'a rerun candidate that held nothing'
[ ! -s "$GITHUB_ENV" ] && grep -q "$(printf 'decision\tredraw: it held no item')" "$work/record" || fail 'a rerun candidate that held nothing takes nothing'
# Rerunning every job: the collector runs in this attempt, so its assignment decides.
jobs 2 "$(collector 2 queued 2)" "$(draw 2 6 in_progress 2)"
artifacts "$p-1-assign-2-n3x1-n4x6" "$p-2-assign-2-n3x2"
take 2 2 qualified || fail 'a candidate of a full rerun'
[ "$(setting BUSTER_SHARD)" = 3 ] || fail "a full rerun's candidate takes its new item"
pass

# --- The products ---
export BUSTER_VECTOR_CONFIGURATIONS='none address thread memory' GITHUB_RUN_ATTEMPT=1
product()
{
    : > "$GITHUB_OUTPUT"
    bash "$draw_script" product "$1" > "$work/log" 2>&1
}
# Item 1 of thread is draw 2's candidate 4; its product is published.
artifacts "$p-1-assign-1-m1x2-t2x3" "$p-1-assign-2-t1x4" "$products-1-thread-$sha" "$products-1-memory-$other"
jobs 1 "$(draw 2 4 in_progress 1)" "$(draw 1 2 in_progress 1)"
product thread || { cat "$work/log"; fail 'a published product'; }
[ "$(output name)" = "$products-1-thread-$sha" ] && [ "$(output digest)" = "$sha" ] || fail "the product's name and digest: $(output name)"
pass
# Not yet published while its builder runs: the limit fails it.
artifacts "$p-1-assign-1-m1x2-t2x3" "$p-1-assign-2-t1x4"
if product thread; then fail 'a product never published'; fi
grep -q 'no product of thread' "$work/log" || fail 'an unpublished product failed without its reason'
# Its builder ended without one: it fails at once.
jobs 1 "$(draw 2 4 completed 1 failure)"
if product thread; then fail "a product whose builder failed"; fi
grep -q 'ended (failure) in attempt 1 without publishing' "$work/log" || fail "a builder's failure without its reason"
# Item 1 not assigned yet: it waits; after the last draw (4) assigned it to none, it fails at once.
artifacts "$p-1-assign-1-m1x2-t2x3"
if product thread; then fail 'a product without a builder'; fi
grep -q 'no product of thread' "$work/log" || fail 'a builder not yet drawn fails only at the limit'
artifacts "$p-1-assign-1-m1x2-t2x3" "$p-1-assign-2" "$p-1-assign-3" "$p-1-assign-4"
if product thread; then fail 'a product that no candidate can build'; fi
grep -q 'No candidate holds item t1' "$work/log" || fail 'an unbuildable product without its reason'
# Two products of one configuration in the builder's attempt: refused.
artifacts "$p-1-assign-1-m1x2-t2x3" "$p-1-assign-2-t1x4" "$products-1-thread-$sha" "$products-1-thread-$other"
jobs 1 "$(draw 2 4 completed 1 success)"
if product thread; then fail 'two products'; fi
grep -q 'has 2 products of thread' "$work/log" || fail 'two products without their reason'
pass
# A rerun of the failed jobs: the builder didn't rerun, so its earlier attempt's product stands; a builder that reruns
# publishes this attempt's, which its items wait for.
export GITHUB_RUN_ATTEMPT=2
artifacts "$p-1-assign-1-m1x2-t2x3" "$p-1-assign-2-t1x4" "$products-1-thread-$sha"
jobs 2 "$(draw 2 4 completed 1 success)" "$(draw 1 3 in_progress 2)"
product thread || { cat "$work/log"; fail "an earlier attempt's product"; }
[ "$(output digest)" = "$sha" ] || fail "an earlier attempt's product's digest"
jobs 2 "$(draw 2 4 in_progress 2)" "$(draw 1 3 in_progress 2)"
if product thread; then fail "a rerun builder's earlier product"; fi
artifacts "$p-1-assign-1-m1x2-t2x3" "$p-1-assign-2-t1x4" "$products-1-thread-$sha" "$products-2-thread-$other"
product thread && [ "$(output digest)" = "$other" ] || fail "a rerun builder's new product"
# Rerunning every job: attempt 2's assignments and its builder.
artifacts "$p-1-assign-1-m1x2-t1x3" "$p-2-assign-1-t1x5" "$products-1-thread-$sha" "$products-2-thread-$other"
jobs 2 "$(draw 1 5 completed 2 success)" "$(draw 1 3 completed 2 success)"
product thread && [ "$(output digest)" = "$other" ] || fail "a full rerun's product"
if product none; then fail 'a configuration without a product'; fi
if product race; then fail 'a configuration the row lacks'; fi
pass

# --- The verdict ---
export BUSTER_VECTOR_CONFIGURATIONS=none
records="$work/records"
evidence()
{
    local at=$1 item=$2 level=$3 candidate=$4 decision=${5:-took $2} product=${6:-}
    [ -n "$product" ] || { if [ "${item:1}" = 1 ]; then product="built $sha"; else product="installed $sha"; fi; }
    mkdir -p "$records/$p-$at-evidence-$item"
    record "$level" "$candidate" "Intel Xeon 8370C" qualified "$records/$p-$at-evidence-$item/record"
    printf 'decision\t%s\ntarget\t-march=x86-64-v4\nhost-vector\t1\nkernels\tok\nitem\t%s\nconfiguration\t%s\nshard\t%s\nproduct\t%s\nsuite\tpassed\n' \
        "$decision" "$item" "$BUSTER_VECTOR_CONFIGURATIONS" "${item:1}" "$product" >> "$records/$p-$at-evidence-$item/record"
}
verdict() { bash "$draw_script" verdict "$records" > "$work/log" 2>&1; }
debug() { cat "$work/log" >&2; }
rm -rf -- "$records"
mkdir -p "$records/$p-1-assign-1-n1x3-n2x5-n3x9" "$records/$p-1-draw-1-3"
record 1 3 "AMD EPYC 9V45" qualified "$records/$p-1-draw-1-3/record"
printf 'decision\ttook n1\n' >> "$records/$p-1-draw-1-3/record"
export GITHUB_RUN_ATTEMPT=1
evidence 1 n1 1 3; evidence 1 n2 1 5; evidence 1 n3 1 9
if verdict; then fail 'an item without a holder passes'; fi
grep -q 'incomplete: no qualifying CPU drawn for item(s) n4' "$work/log" || { debug; fail 'an unheld item is incomplete'; }
mkdir -p "$records/$p-1-assign-2-n4x2"
evidence 1 n4 2 2
verdict || fail "every item's holder passing fails: $(cat "$work/log")"
grep -q "| n4 | none | 4 | 2 | 2 | 1 | Intel Xeon 8370C | installed ${sha:0:12} | passed |" "$work/log" || { debug; fail "the verdict's table"; }
grep -q '| 1 | 1 | 3 | AMD EPYC 9V45 | qualified | took n1 |' "$work/log" || fail "the verdict's draws"
pass

# Evidence by another candidate than the holder, incomplete evidence, or an item 1 that didn't build, doesn't count.
evidence 1 n2 1 6
if verdict; then fail "another candidate's evidence passes"; fi
grep -q "Item(s) n2 have no single passing" "$work/log" || fail "another candidate's evidence fails the item"
evidence 1 n2 1 5
sed -i '/^kernels/d' "$records/$p-1-evidence-n2/record"
if verdict; then fail 'evidence without the kernels passes'; fi
evidence 1 n2 1 5
evidence 1 n1 1 3 'took n1' "installed $sha"
if verdict; then fail "an item 1 that installed its product passes"; fi
evidence 1 n1 1 3 'took n1' 'built'
if verdict; then fail "a product without its digest passes"; fi
evidence 1 n1 1 3
evidence 1 n2 1 5 'took n2' "built $sha"
if verdict; then fail "an item that built its own product passes"; fi
evidence 1 n2 1 5
verdict || fail "the restored evidence fails: $(cat "$work/log")"
pass

# A rerun of the failed jobs: attempt 1's assignments stand, and the holder's later pass counts.
rm -rf -- "$records/$p-1-evidence-n3"
export GITHUB_RUN_ATTEMPT=2
if verdict; then fail 'a failed item passes'; fi
evidence 2 n3 1 9 'took n3 again (assigned in attempt 1)'
verdict || fail "a rerun holder's pass fails: $(cat "$work/log")"
grep -q '| n3 | none | 3 | 1 | 9 | 2 | Intel Xeon 8370C |' "$work/log" || fail "the rerun's row"
# Rerunning every job: attempt 3's assignments replace the earlier ones, and the earlier passes don't count.
export GITHUB_RUN_ATTEMPT=3
mkdir -p "$records/$p-3-assign-1-n1x1-n2x2-n3x3-n4x4"
if verdict; then fail "a full rerun passes on an earlier attempt's executions"; fi
evidence 3 n1 1 1; evidence 3 n2 1 2; evidence 3 n3 1 3; evidence 3 n4 1 4
verdict || fail "a full rerun's executions fail: $(cat "$work/log")"
pass

# A pool's verdict: every configuration's items, each evidence of its own configuration.
rm -rf -- "$records"
export BUSTER_VECTOR_CONFIGURATIONS='none thread' GITHUB_RUN_ATTEMPT=1
mkdir -p "$records/$p-1-assign-1-t1x1-t2x2-t3x3-t4x4-n1x5-n2x6-n3x7-n4x8"
for item in t1 t2 t3 t4 n1 n2 n3 n4; do
    BUSTER_VECTOR_CONFIGURATIONS=$([ "${item:0:1}" = t ] && echo thread || echo none) evidence 1 "$item" 1 "$(( $(printf '%s\n' t1 t2 t3 t4 n1 n2 n3 n4 | grep -nx "$item" | cut -d : -f 1) ))"
done
verdict || fail "a pool's executions fail: $(cat "$work/log")"
grep -q '| t3 | thread | 3 | 1 | 3 | 1 |' "$work/log" || { debug; fail "a pool's table"; }
BUSTER_VECTOR_CONFIGURATIONS=none evidence 1 t3 1 3
if verdict; then fail "an item's evidence of another configuration passes"; fi

# A failing listing (the repository's REST limit, #2309) doubles the pause, up to VECTOR_DRAW_POLL_LIMIT, and a listing
# that succeeds ends the back-off. The collector's first three calls fail (the artifacts' listing, the jobs', the
# artifacts' again); its fifth lists no artifact yet, so it polls once more at the base pause.
mkdir -p "$work/flaky"
cat > "$work/flaky/gh" <<'GH'
#!/usr/bin/env bash
count=$(($(cat "$FLAKY_COUNT") + 1))
printf '%s\n' "$count" > "$FLAKY_COUNT"
if [ "$count" -le 3 ]; then
    echo 'gh: API rate limit exceeded for installation (HTTP 403)' >&2
    exit 1
fi
[ "$count" -ne 5 ] || exit 0
exec "$REAL_GH" "$@"
GH
cat > "$work/flaky/sleep" <<'SLEEP'
#!/usr/bin/env bash
printf '%s\n' "$1" >> "$SLEEPS"
SLEEP
chmod +x "$work/flaky/gh" "$work/flaky/sleep"
echo 0 > "$work/flaky/count"
: > "$work/flaky/sleeps"
export BUSTER_VECTOR_CONFIGURATIONS=none GITHUB_RUN_ATTEMPT=1
: > "$GITHUB_OUTPUT"
artifacts "$p-1-probe-1-5-yes" "$p-1-probe-1-9-yes" "$p-1-probe-1-3-yes" "$p-1-probe-1-12-yes"
jobs 1
FLAKY_COUNT="$work/flaky/count" SLEEPS="$work/flaky/sleeps" REAL_GH="$work/bin/gh" PATH="$work/flaky:$PATH" \
    VECTOR_DRAW_POLL=2 VECTOR_DRAW_POLL_LIMIT=5 VECTOR_DRAW_COLLECT_LIMIT=600 bash "$draw_script" assign 1 12 4 > "$work/log"
[ "$(tr '\n' ' ' < "$work/flaky/sleeps")" = '4 5 2 ' ] || fail "a failing listing's back-off: $(tr '\n' ' ' < "$work/flaky/sleeps")"
[ "$(output name)" = "$p-1-assign-1-n1x3-n2x5-n3x9-n4x12" ] || fail "the assignment after the back-off: $(output name)"
pass

printf 'vector-draw fixture: %s checks passed\n' "$checks"
