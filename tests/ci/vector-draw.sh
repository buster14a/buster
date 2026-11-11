#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# tools/ci/vector-draw.sh's collector, candidates and verdict (#2033) against a fake `gh` that applies each call's own
# --jq filter to a fixture of the run's artifacts and jobs: an assignment by rank, early when enough qualified and late
# when every candidate decided or ended; a later draw's missing items; a candidate that takes its item, one ranked out,
# one that doesn't qualify; a rerun of the failed jobs, whose holder retakes its item or fails on a CPU that doesn't
# qualify; and the verdict's passes, its incomplete and failed items and its reruns. No network.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
checks=0
fail() { printf 'vector-draw fixture: %s\n' "$*" >&2; exit 1; }
pass() { checks=$((checks + 1)); }
command -v jq > /dev/null || fail 'jq is required'

export LC_ALL=C GITHUB_REPOSITORY=buster14a/buster GITHUB_RUN_ID=7 BUSTER_SANITIZER=none BUSTER_SANITIZER_OPTIMIZATION=3
export BUSTER_VECTOR_ITEMS='1 2 3 4' VECTOR_DRAW_POLL=0 VECTOR_DRAW_COLLECT_LIMIT=2 VECTOR_DRAW_TAKE_LIMIT=2 GH_TOKEN=fixture
export RUNNER_TEMP="$work/runner" FIXTURE="$work/api"
mkdir -p "$RUNNER_TEMP" "$FIXTURE" "$work/bin"
p=vector-none-O3

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
# Attempt $1's jobs: NAME|STATUS|ATTEMPT each.
jobs()
{
    local attempt=$1
    shift
    [ $# -gt 0 ] || { printf '{ "jobs": [] }\n' > "$FIXTURE/jobs-$attempt.json"; return 0; }
    printf '%s\n' "$@" | jq -R 'split("|") | { name: .[0], status: .[1], run_attempt: (.[2] | tonumber) }' | jq -s '{ jobs: . }' > "$FIXTURE/jobs-$attempt.json"
}
draw() { printf 'Tests on Ubuntu 26.04 (vector, none, O3) / Vector draw %s, candidate %s (Clang 23, none, O3)|%s|%s' "$1" "$2" "$3" "$4"; }
collector() { printf "Tests on Ubuntu 26.04 (vector, none, O3) / Vector draw %s's assignment (Clang 23, none, O3)|%s|%s" "$1" "$2" "$3"; }
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
[ "$(output name)" = "$p-1-assign-1-1x3-2x5-3x9-4x12" ] || fail "an early assignment by rank: $(output name)"
[ "$(output count)" = 0 ] && [ "$(output candidates)" = '[]' ] || fail 'a complete draw asks for another'
pass

artifacts "$p-1-probe-1-5-yes" "$p-1-probe-1-2-no" "$p-1-probe-1-11-yes" "$p-1-probe-1-1-no"
jobs 1 "$(draw 1 3 completed 1)" "$(draw 1 4 completed 1)" "$(draw 1 6 in_progress 1)"
bash "$draw_script" assign 1 6 4 > "$work/log"
[ "$(output name)" = "$p-1-assign-1-1x5-2x11" ] || fail "an assignment once every candidate decided or ended: $(output name)"
[ "$(output count)" = 8 ] && [ "$(output candidates)" = '[1, 2, 3, 4, 5, 6, 7, 8]' ] || fail 'the next draw: 4 per missing item'
pass

# Too few decided: the limit (2 s here) assigns what qualified, with a warning.
jobs 1
bash "$draw_script" assign 1 12 4 > "$work/log" 2>&1
grep -q '::warning title=Vector draw::' "$work/log" || fail 'a collector past its limit warns'
[ "$(output name)" = "$p-1-assign-1-1x5-2x11" ] || fail 'a collector past its limit assigns what qualified'
pass

# A second draw: the first's items aren't missing, the costliest missing first.
artifacts "$p-1-assign-1-1x5-3x11" "$p-1-probe-2-2-yes" "$p-1-probe-2-7-yes" "$p-1-probe-2-1-no"
bash "$draw_script" assign 2 8 5 > "$work/log"
[ "$(output name)" = "$p-1-assign-2-2x2-4x7" ] || fail "a second draw's assignment: $(output name)"
[ "$(output count)" = 0 ] || fail 'a second draw that covers its items asks for another'
pass

# Nobody qualified: an empty assignment, which the waiting candidates and the verdict read.
artifacts "$p-1-probe-1-1-no" "$p-1-probe-1-2-no"
bash "$draw_script" assign 1 2 4 > "$work/log"
[ "$(output name)" = "$p-1-assign-1" ] && [ "$(output count)" = 16 ] || fail 'a draw where nobody qualified'
pass

# --- The candidates ---
take()
{
    : > "$GITHUB_ENV"
    record "$1" "$2" "AMD EPYC 9V45" "$3" "$work/record"
    bash "$draw_script" take "$1" "$2" "$3" "$work/record" > "$work/log" 2>&1
}
artifacts "$p-1-assign-1-1x3-2x5-3x9-4x12"
jobs 1
take 1 9 qualified || fail 'a candidate that takes its item'
grep -qx 'BUSTER_SHARD=3' "$GITHUB_ENV" && grep -qx "$(printf 'decision\ttook 3')" "$work/record" || fail 'the item a candidate takes'
take 1 10 qualified || fail 'a candidate ranked out'
[ ! -s "$GITHUB_ENV" ] && grep -q "$(printf 'decision\tredraw: candidates of a higher rank')" "$work/record" || fail 'a candidate ranked out takes nothing'
take 1 2 'unqualified (status 1)' || fail 'a candidate that does not qualify'
[ ! -s "$GITHUB_ENV" ] && grep -q "$(printf 'decision\tredraw: the CPU does not qualify')" "$work/record" || fail 'a CPU that does not qualify takes nothing'
artifacts "$p-1-probe-1-3-yes"
take 1 3 qualified || fail 'a candidate whose collector never assigns'
[ ! -s "$GITHUB_ENV" ] && grep -q '::warning' "$work/log" || fail 'a candidate without an assignment ends as a redraw'
pass

# A rerun of the failed jobs (attempt 2, whose collectors didn't run again): the holder retakes its item.
export GITHUB_RUN_ATTEMPT=2
artifacts "$p-1-assign-1-1x3-2x5" "$p-1-assign-2-3x1-4x6" "$p-1-evidence-1"
jobs 2 "$(collector 1 completed 1)" "$(draw 2 6 in_progress 2)"
take 2 6 qualified || fail 'a rerun holder that qualifies'
grep -qx 'BUSTER_SHARD=4' "$GITHUB_ENV" && grep -qx "$(printf 'decision\ttook 4 again (assigned in attempt 1)')" "$work/record" || fail 'a rerun holder retakes its item'
if take 2 6 'unqualified (status 1)'; then fail 'a rerun holder on a CPU that does not qualify passes'; fi
[ ! -s "$GITHUB_ENV" ] && grep -q 'No test failed: rerun the failed jobs to draw again' "$work/log" || fail 'a rerun holder that does not qualify asks to draw again'
take 2 7 qualified || fail 'a rerun candidate that held nothing'
[ ! -s "$GITHUB_ENV" ] && grep -q "$(printf 'decision\tredraw: it held no item')" "$work/record" || fail 'a rerun candidate that held nothing takes nothing'
# Rerunning every job: the collector runs in this attempt, so its assignment decides.
jobs 2 "$(collector 2 queued 2)" "$(draw 2 6 in_progress 2)"
artifacts "$p-1-assign-2-3x1-4x6" "$p-2-assign-2-3x2"
take 2 2 qualified || fail 'a candidate of a full rerun'
grep -qx 'BUSTER_SHARD=3' "$GITHUB_ENV" || fail "a full rerun's candidate takes its new item"
pass

# --- The verdict ---
records="$work/records"
evidence()
{
    local at=$1 item=$2 level=$3 candidate=$4 decision=${5:-took $2}
    mkdir -p "$records/$p-$at-evidence-$item"
    record "$level" "$candidate" "Intel Xeon 8370C" qualified "$records/$p-$at-evidence-$item/record"
    printf 'decision\t%s\ntarget\t-march=x86-64-v4\nhost-vector\t1\nkernels\tok\nshard\t%s\nsuite\tpassed\n' "$decision" "$item" >> "$records/$p-$at-evidence-$item/record"
}
verdict() { bash "$draw_script" verdict "$records" > "$work/log" 2>&1; }
debug() { cat "$work/log" >&2; }
rm -rf -- "$records"
mkdir -p "$records/$p-1-assign-1-1x3-2x5-3x9" "$records/$p-1-draw-1-3"
record 1 3 "AMD EPYC 9V45" qualified "$records/$p-1-draw-1-3/record"
printf 'decision\ttook 1\n' >> "$records/$p-1-draw-1-3/record"
export GITHUB_RUN_ATTEMPT=1
evidence 1 1 1 3; evidence 1 2 1 5; evidence 1 3 1 9
if verdict; then fail 'an item without a holder passes'; fi
grep -q 'incomplete: no qualifying CPU drawn for item(s) 4' "$work/log" || { debug; fail 'an unheld item is incomplete'; }
mkdir -p "$records/$p-1-assign-2-4x2"
evidence 1 4 2 2
verdict || fail "every item's holder passing fails: $(cat "$work/log")"
grep -q '| 4 | 2 | 2 | 1 | Intel Xeon 8370C | passed |' "$work/log" || fail "the verdict's table"
grep -q '| 1 | 1 | 3 | AMD EPYC 9V45 | qualified | took 1 |' "$work/log" || fail "the verdict's draws"
pass

# Evidence by another candidate than the holder, or incomplete evidence, doesn't count.
evidence 1 2 1 6
if verdict; then fail "another candidate's evidence passes"; fi
grep -q "Item(s) 2 have no single passing" "$work/log" || fail "another candidate's evidence fails the item"
evidence 1 2 1 5
sed -i '/^kernels/d' "$records/$p-1-evidence-2/record"
if verdict; then fail 'evidence without the kernels passes'; fi
evidence 1 2 1 5
pass

# A rerun of the failed jobs: attempt 1's assignments stand, and the holder's later pass counts.
rm -rf -- "$records/$p-1-evidence-3"
export GITHUB_RUN_ATTEMPT=2
if verdict; then fail 'a failed item passes'; fi
evidence 2 3 1 9 'took 3 again (assigned in attempt 1)'
verdict || fail "a rerun holder's pass fails: $(cat "$work/log")"
grep -q '| 3 | 1 | 9 | 2 | Intel Xeon 8370C | passed |' "$work/log" || fail "the rerun's row"
# Rerunning every job: attempt 3's assignments replace the earlier ones, and the earlier passes don't count.
export GITHUB_RUN_ATTEMPT=3
mkdir -p "$records/$p-3-assign-1-1x1-2x2-3x3-4x4"
if verdict; then fail "a full rerun passes on an earlier attempt's executions"; fi
evidence 3 1 1 1; evidence 3 2 1 2; evidence 3 3 1 3; evidence 3 4 1 4
verdict || fail "a full rerun's executions fail: $(cat "$work/log")"
pass

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
export GITHUB_RUN_ATTEMPT=1
: > "$GITHUB_OUTPUT"
artifacts "$p-1-probe-1-5-yes" "$p-1-probe-1-9-yes" "$p-1-probe-1-3-yes" "$p-1-probe-1-12-yes"
jobs 1
FLAKY_COUNT="$work/flaky/count" SLEEPS="$work/flaky/sleeps" REAL_GH="$work/bin/gh" PATH="$work/flaky:$PATH" \
    VECTOR_DRAW_POLL=2 VECTOR_DRAW_POLL_LIMIT=5 VECTOR_DRAW_COLLECT_LIMIT=600 bash "$draw_script" assign 1 12 4 > "$work/log"
[ "$(tr '\n' ' ' < "$work/flaky/sleeps")" = '4 5 2 ' ] || fail "a failing listing's back-off: $(tr '\n' ' ' < "$work/flaky/sleeps")"
[ "$(output name)" = "$p-1-assign-1-1x3-2x5-3x9-4x12" ] || fail "the assignment after the back-off: $(output name)"
pass

printf 'vector-draw fixture: %s checks passed\n' "$checks"
