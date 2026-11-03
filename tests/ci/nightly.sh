#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Network-free tests of main's nightly measurements (#1207): tools/ci/nightly.c's self-test, optimized and with
# ASan/UBSan, and tools/ci/nightly.sh's glue against a fake `gh` and a local Git history with merges.
set -euo pipefail
shopt -s inherit_errexit
root=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/nightly.c -o "$work/nightly"
"$work/nightly" --self-test
cc -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tools/ci/nightly.c -o "$work/nightly-sanitize"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/nightly-sanitize" --self-test
bash -n tools/ci/nightly.sh

fail() { printf 'tests/ci/nightly.sh: %s\n' "$1" >&2; exit 1; }
expect_line() { grep -Fx -- "$2" "$1" > /dev/null || { cat "$1" >&2; fail "$3"; }; }
expect_match() { grep -E -- "$2" "$1" > /dev/null || { cat "$1" >&2; fail "$3"; }; }
expect_no_match() { if grep -E -- "$2" "$1" > /dev/null; then cat "$1" >&2; fail "$3"; fi; }

# The fake `gh`: each endpoint answers from $FIXTURES, or with nothing found; every call is logged.
mkdir "$work/bin" "$work/fixtures"
cat > "$work/bin/gh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
if [ "$1" = workflow ]; then
    exit 0
fi
[ "$1" = api ]
shift
method=GET jq_filter='' endpoint=''
while [ "$#" -gt 0 ]; do
    case "$1" in
        --paginate) shift ;;
        -X) method=$2; shift 2 ;;
        -f) shift 2 ;;
        --jq) jq_filter=$2; shift 2 ;;
        *) endpoint=$1; shift ;;
    esac
done
answer()
{
    if [ -n "$jq_filter" ]; then
        jq -r "$jq_filter" "$1"
    else
        cat "$1"
    fi
}
fixture=''
empty=''
case "$method $endpoint" in
    "GET repos/buster14a/buster/actions/workflows/9700x.yml/runs?event="*)
        event=${endpoint#*event=}; fixture="$FIXTURES/runs-${event%%&*}.json"; empty='{"workflow_runs":[]}' ;;
    "GET repos/buster14a/buster/actions/workflows/9700x.yml/runs?head_sha="*)
        sha=${endpoint#*head_sha=}; fixture="$FIXTURES/runs-head-${sha%%&*}.json"; empty='{"workflow_runs":[]}' ;;
    "GET repos/buster14a/buster/actions/workflows/nightly-backfill.yml/runs"*)
        fixture="$FIXTURES/coordinator-runs.json"; empty='{"workflow_runs":[]}' ;;
    "GET repos/buster14a/buster/actions/runs/"*/jobs*)
        run=${endpoint#repos/buster14a/buster/actions/runs/}; fixture="$FIXTURES/jobs-${run%%/*}.json"; empty='{"jobs":[]}' ;;
    "GET repos/buster14a/buster/actions/runs/"*/artifacts*)
        run=${endpoint#repos/buster14a/buster/actions/runs/}; fixture="$FIXTURES/artifacts-${run%%/*}.json"; empty='{"artifacts":[]}' ;;
    "GET repos/buster14a/buster/actions/artifacts/"*/zip)
        id=${endpoint#repos/buster14a/buster/actions/artifacts/}; fixture="$FIXTURES/artifact-${id%%/*}.zip" ;;
    "GET repos/buster14a/buster/commits/"*/pulls)
        sha=${endpoint#repos/buster14a/buster/commits/}; fixture="$FIXTURES/pulls-${sha%%/*}.json"; empty='[]' ;;
    "GET repos/buster14a/buster/git/matching-refs/heads/nightly/")
        fixture="$FIXTURES/refs.json"; empty='[]' ;;
    "GET repos/buster14a/buster/git/ref/heads/nightly/"*)
        fixture="$FIXTURES/ref-${endpoint##*/}.json" ;;
    "POST repos/buster14a/buster/git/refs"|"POST repos/buster14a/buster/actions/runs/"*/cancel|"DELETE repos/buster14a/buster/git/refs/heads/nightly/"*)
        printf '{}\n'; exit 0 ;;
    *)
        printf 'fake gh: unexpected %s %s\n' "$method" "$endpoint" >&2; exit 1 ;;
esac
# A fixture FILE.N answers the endpoint's Nth call instead, and FILE.N.fail makes that call fail.
if [ -n "$fixture" ]; then
    key=$(basename "$fixture")
    count=$(( $(cat "$FIXTURES/count-$key" 2> /dev/null || echo 0) + 1 ))
    printf '%s\n' "$count" > "$FIXTURES/count-$key"
    if [ -f "$fixture.$count.fail" ]; then
        printf '{"message":"Server Error","status":"502"}\n'
        printf 'gh: Server Error (HTTP 502)\n' >&2
        exit 1
    elif [ -f "$fixture.$count" ]; then
        fixture="$fixture.$count"
    fi
fi
if [ -f "$fixture" ]; then
    answer "$fixture"
elif [ -n "$empty" ]; then
    printf '%s\n' "$empty" > "$FIXTURES/empty.json"
    answer "$FIXTURES/empty.json"
else
    # As gh does, a failed call prints the response's body on standard output too.
    printf '{"message":"Not Found","documentation_url":"https://docs.github.com/rest","status":"404"}\n'
    printf 'gh: Not Found (HTTP 404)\n' >&2
    exit 1
fi
SH
chmod +x "$work/bin/gh"
export FIXTURES="$work/fixtures"
export NIGHTLY_GH="$work/bin/gh"
export GITHUB_REPOSITORY=buster14a/buster
fixtures="$work/fixtures"
: > "$fixtures/calls.log"

# A history with a merge: main's first-parent states are the epoch (adding tools/ci/nightly.c), the merge, a
# documentation change and a code change; the merged branch's own commits are not states.
remote="$work/remote.git"
git init -q --bare "$remote"
git -C "$remote" config uploadpack.allowFilter true
history="$work/history"
git init -q -b main "$history"
commit_at()
{
    local when=$1
    shift
    GIT_AUTHOR_DATE="@$when +0000" GIT_COMMITTER_DATE="@$when +0000" git -C "$history" -c user.name=t -c user.email=t@example.com "$@"
}
mkdir -p "$history/tools/ci" "$history/src" "$history/docs"
echo root > "$history/README.md"
git -C "$history" add -A
commit_at 1793700000 commit -q -m root
echo tool > "$history/tools/ci/nightly.c"
git -C "$history" add -A
commit_at 1793700100 commit -q -m epoch
epoch=$(git -C "$history" rev-parse HEAD)
git -C "$history" checkout -q -b feature
echo one > "$history/src/a.c"
git -C "$history" add -A
commit_at 1793700200 commit -q -m inside-1
inside=$(git -C "$history" rev-parse HEAD)
echo two > "$history/src/b.c"
git -C "$history" add -A
commit_at 1793700300 commit -q -m inside-2
git -C "$history" checkout -q main
commit_at 1793700400 merge -q --no-ff -m merge feature
merge=$(git -C "$history" rev-parse HEAD)
echo docs > "$history/docs/x.md"
git -C "$history" add -A
commit_at 1793700500 commit -q -m docs
docs=$(git -C "$history" rev-parse HEAD)
echo code > "$history/src/c.c"
git -C "$history" add -A
commit_at 1793700600 commit -q -m code
head=$(git -C "$history" rev-parse HEAD)
git -C "$history" -c push.negotiate=false push -q "$remote" main
export NIGHTLY_REMOTE="$remote"
export NIGHTLY_PERFORMANCE_REMOTE="$work/no-such-remote.git"

madrid() { TZ=Europe/Madrid date -d "$1" +%s; }

# --- The evidence job ---

export GITHUB_OUTPUT="$work/output" GITHUB_STEP_SUMMARY="$work/summary" GITHUB_RUN_ID=9000 GITHUB_SHA
run_evidence()
{
    : > "$GITHUB_OUTPUT"
    (cd "$1" && bash "$root/tools/ci/nightly.sh" evidence "$work/evidence-$RANDOM") > "$work/evidence.log"
}

# A draft pull request: hosted feedback, no hardware, and no API call.
GITHUB_EVENT_NAME=pull_request GITHUB_SHA=$head EVIDENCE_DRAFT=true run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'scope=plain' 'a pull request runs the plain rows'
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'a draft keeps off the 9700X'
expect_line "$GITHUB_OUTPUT" 'hardware_reason=draft' 'a draft says why'
GITHUB_EVENT_NAME=pull_request GITHUB_SHA=$head EVIDENCE_DRAFT=false run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'a ready pull request is admitted'
expect_no_match "$fixtures/calls.log" 'commits/' 'a pull request reads no API'

# The nightly mode retires the daily run; before it, the daily run is as it was.
GITHUB_EVENT_NAME=schedule GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'retired=true' 'the nightly mode retires the daily run'
GITHUB_EVENT_NAME=schedule GITHUB_SHA=$head EVIDENCE_NIGHTLY='' run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'retired=false' 'without the nightly mode the daily run runs'
expect_line "$GITHUB_OUTPUT" 'scope=full' 'the daily run runs every row'

# A push whose pull request's newest run tested the identical tree on the identical first-parent tree.
tree=$(git -C "$history" rev-parse "$head^{tree}")
parent_tree=$(git -C "$history" rev-parse "$head^1^{tree}")
pull_head=$(printf 'pullhead' | sha1sum | cut -c 1-40)
now=$(madrid '2026-11-04 12:00')
export NIGHTLY_NOW=$now
cat > "$fixtures/pulls-$head.json" <<JSON
[{"number":77,"merged_at":"2026-11-04T10:00:00Z","base":{"ref":"main"},"merge_commit_sha":"$head","head":{"sha":"$pull_head","repo":{"id":1421805693}}}]
JSON
cat > "$fixtures/runs-head-$pull_head.json" <<JSON
{"workflow_runs":[{"id":700,"run_attempt":1,"event":"pull_request","status":"completed","conclusion":"success","head_sha":"$pull_head","path":".github/workflows/9700x.yml","head_branch":"feature","created_at":"2026-11-04T10:00:00Z","updated_at":"2026-11-04T11:00:00Z","head_repository":{"id":1421805693},"display_title":"a pull request"}]}
JSON
{
    printf '{"jobs":['
    for arch in x86-64 native; do
        for shard in 1 2 3 4; do
            printf '{"name":"Tests on Ubuntu 26.04 (Clang 23, %s, none, O3, shard %s of 4)","status":"completed","conclusion":"success"},' "$arch" "$shard"
        done
    done
    printf '{"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success"},'
    printf '{"name":"Build and execute on Ryzen 7 9700X","status":"completed","conclusion":"success"}]}\n'
} > "$fixtures/jobs-700.json"
echo '{"artifacts":[{"id":7001,"name":"coverage-record-1","expired":false}]}' > "$fixtures/artifacts-700.json"
other=$(printf 'other' | sha1sum | cut -c 1-40)
write_record()
{
    local record=$1 tested_tree=$2 tested_parent=$3
    {
        printf 'format\tbuster-coverage-record-1\nrepository\t1421805693\nrun\t700\nattempt\t1\nevent\tpull_request\npurpose\tpr\nrequest\t-\n'
        printf 'commit\t%s\ntree\t%s\nparent\t%s\nparent_tree\t%s\npr_head\t%s\nsnapshot\t-\n' "$other" "$tested_tree" "$other" "$tested_parent" "$pull_head"
        printf 'workflow\t%s\nselector\t%s\ninventory\t%s\ntoolchain\t%s\ncorpus\t%s\nmode\toff\ncode\ttrue\nmeasure\t,lexer,\ngates\t-\n' "$other" "$other" "$other" "$other" "$other"
        printf 'changes_result\tsuccess\nevidence_result\tsuccess\nscope\tplain\nhosted\trun\nhosted_source\t-\nnative_evidence\trun\nnative_source\t-\n'
        printf 'measurement_evidence\trun\nmeasurement_source\t-\nhardware\tadmit\nhardware_reason\tadmitted\ntest_result\tsuccess\nfuzz_result\tskipped\n'
        printf 'benchmark_result\tsuccess\nwindow\t-\nnative\tpassed\nhost\t9700X/6.17/0xb40/gcc-15\nqualification_result\tsuccess\noverride\t-\ncoverage\ttargeted\noutcome\tcomplete\n'
    } > "$record"
}
write_record "$work/record.tsv" "$tree" "$parent_tree"
(cd "$work" && rm -f "$fixtures/artifact-7001.zip" && zip -q "$fixtures/artifact-7001.zip" record.tsv)
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=reused' 'a push reuses its pull request'"'"'s native correctness of the identical tree'
expect_line "$GITHUB_OUTPUT" 'native_source=700' 'the reuse names its source run'
expect_line "$GITHUB_OUTPUT" 'native_host=9700X/6.17/0xb40/gcc-15' 'and the host identity its run recorded, for the push to confirm'
expect_line "$GITHUB_OUTPUT" 'measurement=reused' 'and its measurements on the identical first-parent tree'
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'the nightly mode skips the 9700X for it'
expect_line "$GITHUB_OUTPUT" 'hardware_reason=native-reused' 'and says why'
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY='' run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'before the nightly mode a push still measures every plain run'
# Another first-parent tree: native correctness alone; another tree: nothing.
write_record "$work/record.tsv" "$tree" "$other"
(cd "$work" && rm -f "$fixtures/artifact-7001.zip" && zip -q "$fixtures/artifact-7001.zip" record.tsv)
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=reused' 'another first-parent tree still reuses native correctness'
expect_line "$GITHUB_OUTPUT" 'measurement=run' 'but not the measurements'
write_record "$work/record.tsv" "$other" "$parent_tree"
(cd "$work" && rm -f "$fixtures/artifact-7001.zip" && zip -q "$fixtures/artifact-7001.zip" record.tsv)
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=run' 'another tree reuses nothing'
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'and runs the 9700X job'
# An unqualified direct push (no pull request) and a corrupt record take the fallback too.
rm "$fixtures/pulls-$head.json"
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=run' 'a direct push runs native correctness'
cat > "$fixtures/pulls-$head.json" <<JSON
[{"number":77,"merged_at":"2026-11-04T10:00:00Z","base":{"ref":"main"},"merge_commit_sha":"$head","head":{"sha":"$pull_head","repo":{"id":1421805693}}}]
JSON
printf 'not a zip' > "$fixtures/artifact-7001.zip"
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=run' 'a corrupt record reuses nothing'

# A nightly backlog dispatch: its commit's push run passed the plain rows, and the window decides.
cat > "$fixtures/runs-head-$merge.json" <<JSON
{"workflow_runs":[{"id":800,"run_attempt":1,"event":"push","status":"completed","conclusion":"failure","head_sha":"$merge","path":".github/workflows/9700x.yml","head_branch":"main","created_at":"2026-11-03T10:00:00Z","updated_at":"2026-11-03T11:00:00Z","head_repository":{"id":1421805693},"display_title":"merge"}]}
JSON
{
    printf '{"jobs":['
    for arch in x86-64 native; do
        for shard in 1 2 3 4; do
            printf '{"name":"Tests on Ubuntu 26.04 (Clang 23, %s, none, O3, shard %s of 4)","status":"completed","conclusion":"success"},' "$arch" "$shard"
        done
    done
    printf '{"name":"Tests on Ubuntu 26.04 (Clang 23, native, thread, O3, shard 2 of 4)","status":"completed","conclusion":"failure"}]}\n'
} > "$fixtures/jobs-800.json"
NIGHTLY_NOW=$(madrid '2026-11-04 04:10') GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$merge EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=admit EVIDENCE_SNAPSHOT=$head run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hosted=reused' 'a backlog run reuses its commit'"'"'s plain rows, whatever its sanitizer rows did'
expect_line "$GITHUB_OUTPUT" 'hosted_source=800' 'naming the run'
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'inside the window it is admitted'
expect_line "$GITHUB_OUTPUT" 'scope=plain' 'a backlog run is in the plain scope'
expect_line "$GITHUB_OUTPUT" "window_start=$(madrid '2026-11-04 04:00')" 'the cycle'"'"'s window opens at 04:00 Madrid'
expect_line "$GITHUB_OUTPUT" "window_deadline=$(madrid '2026-11-04 07:00')" 'and closes at 07:00'
expect_line "$GITHUB_OUTPUT" 'window_override=false' 'without an override'
NIGHTLY_NOW=$(madrid '2026-11-04 06:40') GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$merge EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=admit EVIDENCE_SNAPSHOT=$head run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'too late to finish by 07:00, it is not'
expect_line "$GITHUB_OUTPUT" 'hardware_reason=window' 'and says why'
NIGHTLY_NOW=$(madrid '2026-11-04 13:00') GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$merge EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=admit EVIDENCE_SNAPSHOT=$head EVIDENCE_OVERRIDE='manual check' run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'an override admits outside the window'
expect_match "$work/evidence.log" 'overridden: manual check' 'and reports it'
expect_line "$GITHUB_OUTPUT" 'window_override=true' 'and holds after the lock'
NIGHTLY_NOW=$(madrid '2026-11-04 01:00') GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$head EVIDENCE_PURPOSE=snapshot EVIDENCE_HARDWARE=defer EVIDENCE_SNAPSHOT=$head run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'scope=full' 'the snapshot'"'"'s deferred run is the day'"'"'s full hosted run'
expect_line "$GITHUB_OUTPUT" 'hardware_reason=deferred' 'and runs no hardware'
# A commit that isn't a first-parent state of main is refused.
if GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$inside EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=defer EVIDENCE_SNAPSHOT=$head run_evidence "$history" 2> /dev/null; then
    fail 'a branch-internal commit was accepted for a nightly run'
fi
if GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$head EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=defer EVIDENCE_SNAPSHOT=$merge run_evidence "$history" 2> /dev/null; then
    fail 'a commit after the pinned snapshot was accepted'
fi

# --- The 9700X job's window step, as the workflow has it, with a stand-in clock ---

window_step="$work/window-step.sh"
awk '/^      - name: Admit nightly work inside the Europe\/Madrid window, holding the lock$/ { step = 1 }
    step && /^        run: \|$/ { body = 1; next }
    body && /^      - name: / { exit }
    body { sub(/^          /, ""); print }' "$root/.github/workflows/9700x.yml" > "$window_step"
grep -q 'WINDOW_DEADLINE' "$window_step" || fail 'the window step is found in the workflow'
mkdir -p "$work/clock"
printf '#!/bin/sh\nprintf "%%s\\n" "$FAKE_NOW"\n' > "$work/clock/date"
chmod +x "$work/clock/date"
window_case()
{
    : > "$GITHUB_OUTPUT"
    FAKE_NOW=$1 WINDOW_START=$2 WINDOW_DEADLINE=$3 WINDOW_OVERRIDE=$4 BOUND=$5 PATH="$work/clock:$PATH" bash "$window_step" > /dev/null
}
start_epoch=$(madrid '2026-11-04 04:00')
deadline_epoch=$(madrid '2026-11-04 07:00')
window_case $((start_epoch + 600)) "$start_epoch" "$deadline_epoch" false 25 || fail 'the window step admits inside the window'
expect_line "$GITHUB_OUTPUT" 'window=open' 'and says so'
if window_case $((deadline_epoch - 20 * 60)) "$start_epoch" "$deadline_epoch" false 25; then
    fail 'a lock taken at 06:40 admitted a 25-minute bound'
fi
expect_line "$GITHUB_OUTPUT" 'window=closed' 'a lock taken too late closes the window'
if window_case $((deadline_epoch + 3600)) "$start_epoch" "$deadline_epoch" false 25; then
    fail 'a lock taken after 07:00 admitted'
fi
if window_case $((start_epoch - 60)) "$start_epoch" "$deadline_epoch" false 25; then
    fail 'a lock taken before 04:00 admitted'
fi
if window_case $((start_epoch + 600)) '' '' false 25; then
    fail 'missing instants admitted'
fi
window_case $((deadline_epoch + 3600)) "$start_epoch" "$deadline_epoch" true 25 || fail 'an override admits after the lock'

# --- The record job, in this repository's checkout ---

: > "$GITHUB_OUTPUT"
RECORD_REPOSITORY=1421805693 RECORD_RUN=41 RECORD_ATTEMPT=1 RECORD_EVENT=workflow_dispatch RECORD_DISPATCH_PURPOSE=backlog \
    RECORD_REQUEST=20261104.9.1.admit RECORD_MODE=on RECORD_CODE=true RECORD_MEASURE=,lexer, \
    CHANGES_OUTPUTS='{"code":"true","checker":"true","zen5":"false","measure":",lexer,"}' RECORD_CHANGES_RESULT=success \
    RECORD_EVIDENCE_RESULT=success RECORD_SCOPE=plain RECORD_HOSTED=reused RECORD_HOSTED_SOURCE=40 RECORD_HARDWARE=admit \
    RECORD_HARDWARE_REASON=admitted RECORD_TEST_RESULT=skipped RECORD_FUZZ_RESULT=skipped RECORD_BENCHMARK_RESULT=success \
    RECORD_WINDOW=open RECORD_NATIVE=passed RECORD_HOST=9700X/6.17/0xb40/gcc-15 \
    bash tools/ci/nightly.sh record "$work/record-job" > /dev/null
expect_line "$GITHUB_OUTPUT" 'outcome=complete' 'a backlog run that measured is complete'
expect_line "$work/record-job/record.tsv" $'gates\tchecker' 'the record names the selector'"'"'s true outputs'
"$work/nightly" harvest --ledger /dev/null --facts /dev/null --states /dev/null --list 2> /dev/null && fail 'an empty state list was accepted'

# --- The coordinator ---

run_coordinator()
{
    : > "$GITHUB_OUTPUT"
    (cd "$root" && bash tools/ci/nightly.sh coordinate "$1" --phase "$2" --mode "$3") > "$work/coordinate.log" 2>&1 || { cat "$work/coordinate.log" >&2; fail 'the coordinator failed'; }
}
: > "$fixtures/calls.log"
rm -f "$fixtures/runs-head-"*.json
GITHUB_RUN_ID=9100 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') run_coordinator "$work/dry" prepare dry-run
expect_line "$work/dry/states.tsv" "state	$epoch	$(git -C "$history" rev-parse "$epoch^1")	1793700100" 'the epoch is the first state'
expect_no_match "$work/dry/states.tsv" "$inside" 'a merged branch'"'"'s own commits are not states'
expect_match "$work/dry/report.md" '\| not-required \| 1 \|' 'the documentation change needs nothing'
expect_match "$work/dry/plan.tsv" "^would-dispatch	snapshot	defer	$head	20261104\.9100\.1\.defer$" 'the dry run would prepare the snapshot'
expect_match "$work/dry/plan.tsv" "^would-dispatch	backlog	defer	$epoch	" 'and the oldest states'"'"' hosted rows'
expect_line "$work/dry/plan.tsv" $'done\tdry run' 'and does nothing'
expect_no_match "$fixtures/calls.log" '^workflow run|-X POST|-X DELETE' 'a dry run writes nothing'
expect_line "$GITHUB_OUTPUT" 'window=closed' 'a dry run never waits for the window'
expect_line "$GITHUB_OUTPUT" "snapshot=$head" 'the cycle pins main'"'"'s head'

# Run mode before the window: branches and dispatches of the hosted preparations, and the wait for the window.
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') run_coordinator "$work/prepare" prepare run
expect_match "$fixtures/calls.log" "^api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$head -f sha=$head$" 'the coordinator makes the nightly branch'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml --repo buster14a/buster --ref nightly/$head -f purpose=snapshot -f hardware=defer -f snapshot=$head -f request=20261104\.9200\.1\.defer$" 'and dispatches the snapshot'"'"'s daily hosted run'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'and the oldest state'"'"'s'
expect_line "$GITHUB_OUTPUT" 'window=reachable' 'and waits for the window, under six hours away'
test "$(grep -E -c '^workflow run' "$fixtures/calls.log")" -eq 3 || fail 'the snapshot and the two states without hosted rows are prepared'

# Inside the window, with the snapshot's hosted run qualified and its record harvested: the comprehensive run.
cat > "$fixtures/runs-workflow_dispatch.json" <<JSON
{"workflow_runs":[{"id":900,"run_attempt":1,"event":"workflow_dispatch","status":"completed","conclusion":"success","head_sha":"$head","path":".github/workflows/9700x.yml","head_branch":"nightly/$head","created_at":"2026-11-03T21:15:00Z","updated_at":"2026-11-03T22:30:00Z","head_repository":{"id":1421805693},"display_title":"Nightly snapshot (defer) of $head [20261104.9200.1.defer]"}]}
JSON
cp "$fixtures/jobs-800.json" "$fixtures/jobs-900.json"
{
    printf 'format\tbuster-coverage-record-1\nrepository\t1421805693\nrun\t900\nattempt\t1\nevent\tworkflow_dispatch\npurpose\tsnapshot\nrequest\t20261104.9200.1.defer\n'
    printf 'commit\t%s\ntree\t%s\nparent\t-\nparent_tree\t-\npr_head\t-\nsnapshot\t%s\n' "$head" "$tree" "$head"
    printf 'workflow\t%s\nselector\t%s\ninventory\t%s\ntoolchain\t%s\ncorpus\t%s\nmode\ton\ncode\t-\nmeasure\t-\ngates\t-\n' "$other" "$other" "$other" "$other" "$other"
    printf 'changes_result\tskipped\nevidence_result\tsuccess\nscope\tfull\nhosted\trun\nhosted_source\t-\nnative_evidence\trun\nnative_source\t-\n'
    printf 'measurement_evidence\trun\nmeasurement_source\t-\nhardware\tskip\nhardware_reason\tdeferred\ntest_result\tfailure\nfuzz_result\tsuccess\n'
    printf 'benchmark_result\tskipped\nwindow\t-\nnative\t-\nhost\t-\nqualification_result\tskipped\noverride\t-\ncoverage\tfull\noutcome\tblocked-hosted\n'
} > "$work/record.tsv"
(cd "$work" && rm -f "$fixtures/artifact-9001.zip" && zip -q "$fixtures/artifact-9001.zip" record.tsv)
echo '{"artifacts":[{"id":9001,"name":"coverage-record-1","expired":false}]}' > "$fixtures/artifacts-900.json"
(cd "$work/prepare" && zip -q "$fixtures/artifact-9201.zip" ledger.tsv)
echo '{"artifacts":[{"id":9201,"name":"nightly-ledger-prepare-1","expired":false}]}' > "$fixtures/artifacts-9200.json"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-04 04:10') NIGHTLY_SNAPSHOT=$head run_coordinator "$work/window" window run
expect_match "$work/window/ledger.tsv" "^run	900	1	$head	workflow_dispatch	snapshot	20261104\.9200\.1\.defer	" 'the snapshot'"'"'s hosted run is harvested'
expect_match "$work/window/ledger.tsv" "^run	900	.*	ok	skip	full	blocked-hosted	" 'its sanitizer row failed, but its plain rows qualified it'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$head -f purpose=snapshot -f hardware=admit .*-f request=20261104\.9200\.100[0-9][0-9]\.admit$" 'the comprehensive run is admitted first'
test "$(grep -E -c '^workflow run .*hardware=admit' "$fixtures/calls.log")" -eq 1 || fail 'one hardware run at a time'
expect_line "$work/window/ledger.tsv" "cycle	2026-11-04	$head	9200	$(madrid '2026-11-03 22:10')" 'the window keeps the prepare job'"'"'s pin'
# A manual run's override admits outside the window, through the coordinator and its dispatch, and says so.
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9300 NIGHTLY_NOW=$(madrid '2026-11-04 13:00') NIGHTLY_OVERRIDE='validating the coordinator' run_coordinator "$work/override-prepare" prepare run
expect_line "$GITHUB_OUTPUT" 'window=reachable' 'an override makes the window job run at once'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9300 NIGHTLY_NOW=$(madrid '2026-11-04 13:00') NIGHTLY_OVERRIDE='validating the coordinator' NIGHTLY_SNAPSHOT=$head run_coordinator "$work/override-window" window run
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$head -f purpose=snapshot -f hardware=admit .* -f override=validating the coordinator$" 'the override admits the comprehensive run and passes its reason on'
expect_match "$work/override-window/report.md" 'Override.*validating the coordinator' 'and the report says so'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9300 NIGHTLY_NOW=$(madrid '2026-11-04 13:00') NIGHTLY_SNAPSHOT=$head run_coordinator "$work/no-override" window run
expect_no_match "$fixtures/calls.log" 'hardware=admit' 'without an override 13:00 admits nothing'
expect_match "$work/override-window/report.md" 'out of scope' 'the report says what the epoch leaves out'
# A later step whose run listing fails plans nothing: an admitted run in flight that the failed list would hide isn't
# dispatched again.
rm -f "$fixtures"/count-*
cat > "$fixtures/runs-workflow_dispatch.json" <<JSON
{"workflow_runs":[{"id":950,"run_attempt":1,"event":"workflow_dispatch","status":"in_progress","conclusion":null,"head_sha":"$head","path":".github/workflows/9700x.yml","head_branch":"nightly/$head","created_at":"2026-11-04T03:12:00Z","updated_at":"2026-11-04T03:12:00Z","head_repository":{"id":1421805693},"display_title":"Nightly snapshot (admit) of $head [20261104.9500.1.admit]"}]}
JSON
# The snapshot's push qualified its plain rows: it is ready, so a list that hid the run in flight would admit it again.
cat > "$fixtures/runs-push.json" <<JSON
{"workflow_runs":[{"id":951,"run_attempt":1,"event":"push","status":"completed","conclusion":"failure","head_sha":"$head","path":".github/workflows/9700x.yml","head_branch":"main","created_at":"2026-11-03T20:00:00Z","updated_at":"2026-11-03T21:00:00Z","head_repository":{"id":1421805693},"display_title":"code"}]}
JSON
cp "$fixtures/jobs-800.json" "$fixtures/jobs-951.json"
: > "$fixtures/runs-workflow_dispatch.json.2.fail"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9500 NIGHTLY_NOW=$(madrid '2026-11-04 04:20') NIGHTLY_ITERATIONS=3 NIGHTLY_SLEEP=0 NIGHTLY_SNAPSHOT=$head run_coordinator "$work/failed-poll" window run
expect_match "$work/coordinate.log" 'the runs could not be listed; nothing is planned' 'a failed listing is reported'
expect_no_match "$fixtures/calls.log" 'hardware=admit' 'and no second hardware run is dispatched beside the one in flight'
test "$(grep -c 'actions/workflows/9700x.yml/runs?event=workflow_dispatch' "$fixtures/calls.log")" -eq 3 || fail 'the coordinator tries again after a failed listing'
rm -f "$fixtures/runs-workflow_dispatch.json.2.fail" "$fixtures/runs-push.json" "$fixtures"/count-*

# Before the window, a waiting prepare job refills its hosted preparations as they complete.
cat > "$fixtures/runs-workflow_dispatch.json" <<JSON
{"workflow_runs":[]}
JSON
cat > "$fixtures/runs-workflow_dispatch.json.2" <<JSON
{"workflow_runs":[{"id":960,"run_attempt":1,"event":"workflow_dispatch","status":"completed","conclusion":"success","head_sha":"$epoch","path":".github/workflows/9700x.yml","head_branch":"nightly/$epoch","created_at":"2026-11-03T21:11:00Z","updated_at":"2026-11-03T21:20:00Z","head_repository":{"id":1421805693},"display_title":"Nightly backlog (defer) of $epoch [20261104.9600.2.defer]"}]}
JSON
cp "$fixtures/jobs-800.json" "$fixtures/jobs-960.json"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9600 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=0 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/refill" prepare run
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer .*request=20261104\.9600\.2\.defer$" 'the first step prepares the oldest state, one slot'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$merge -f purpose=backlog -f hardware=defer .*request=20261104\.9600\.3\.defer$" 'the next step refills the slot its completion freed'
expect_line "$GITHUB_OUTPUT" 'window=reachable' 'and the job still waits for the window'
rm -f "$fixtures/runs-workflow_dispatch.json.2" "$fixtures"/count-*
printf 'nightly glue: evidence, records, first-parent states, the ledger, the window step, dry and real coordination, overrides, failed polls and refilled preparations checked\n'
