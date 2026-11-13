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

for binary in "$work/nightly" "$work/nightly-sanitize"; do
    "$binary" worker-window < tools/ci/nightly.c > "$work/worker-current.tsv"
    expect_line "$work/worker-current.tsv" $'worker-window\t2\t8' 'the source reader recognizes the actual current worker'
    printf 'unknown historical source\n' | "$binary" worker-window > "$work/worker-unknown.tsv"
    expect_line "$work/worker-unknown.tsv" $'worker-window\t-\t-' 'unrecognized historical source stays unknown'
done

# The fake `gh`: each endpoint answers from $FIXTURES, or with nothing found; every call is logged.
mkdir "$work/bin" "$work/fixtures"
cat > "$work/bin/gh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
printf '%s %s\n' "${GH_TOKEN:-none}" "$*" >> "$FIXTURES/tokens.log"
# An expired branch token: GitHub rejects every call made with it. One without access to the repository: every call,
# the repository's own included, is a 404.
if [ "${GH_TOKEN:-}" = expired-token ]; then
    printf '{"message":"Bad credentials","status":"401"}\n'
    printf 'gh: Bad credentials (HTTP 401)\n' >&2
    exit 1
fi
if [ "${GH_TOKEN:-}" = outsider-token ]; then
    printf '{"message":"Not Found","status":"404"}\n'
    printf 'gh: Not Found (HTTP 404)\n' >&2
    exit 1
fi
if [ "$1" = workflow ]; then
    exit 0
fi
[ "$1" = api ]
shift
method=GET jq_filter='' endpoint='' fields=''
while [ "$#" -gt 0 ]; do
    case "$1" in
        --paginate) shift ;;
        -X) method=$2; shift 2 ;;
        -f) fields="$fields $2"; shift 2 ;;
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
    "POST repos/buster14a/buster/git/refs")
        # $FIXTURES/refuse-SHA: the workflow token may not create that commit's branch, as GitHub refused some on
        # 2026-11-05.
        sha=${fields##* sha=}
        if [ -f "$FIXTURES/refuse-${sha%% *}" ] && [ "${GH_TOKEN:-}" != branch-token ]; then
            printf '{"message":"Resource not accessible by integration","status":"403"}\n'
            printf 'gh: Resource not accessible by integration (HTTP 403)\n' >&2
            exit 1
        fi
        # $FIXTURES/flaky-SHA: one transient failure, then success.
        if [ -f "$FIXTURES/flaky-${sha%% *}" ]; then
            rm -f "$FIXTURES/flaky-${sha%% *}"
            printf '{"message":"Server Error","status":"502"}\n'
            printf 'gh: Server Error (HTTP 502)\n' >&2
            exit 1
        fi
        printf '{}\n'; exit 0 ;;
    "GET repos/buster14a/buster")
        printf '{"id":1421805693}\n' > "$FIXTURES/repository.json"
        answer "$FIXTURES/repository.json"; exit 0 ;;
    "DELETE repos/buster14a/buster/git/refs/heads/nightly/"*)
        # $FIXTURES/gone-SHA: someone else deleted that branch first.
        if [ -f "$FIXTURES/gone-${endpoint##*/}" ]; then
            printf '{"message":"Not Found","status":"404"}\n'
            printf 'gh: Not Found (HTTP 404)\n' >&2
            exit 1
        fi
        printf '{}\n'; exit 0 ;;
    "POST repos/buster14a/buster/actions/runs/"*/cancel)
        # $FIXTURES/uncancellable-RUN: GitHub refuses that run's cancel.
        run=${endpoint#repos/buster14a/buster/actions/runs/}
        if [ -f "$FIXTURES/uncancellable-${run%%/*}" ]; then
            printf '{"message":"Cannot cancel a workflow run that is completed.","status":"409"}\n'
            printf 'gh: Cannot cancel a workflow run that is completed. (HTTP 409)\n' >&2
            exit 1
        fi
        printf '{}\n'; exit 0 ;;
    "GET repos/buster14a/buster/git/ref/heads/gh-readonly-queue/main/"*)
        fixture="$FIXTURES/queue-ref-${endpoint##*/}.json" ;;
    "GET repos/buster14a/buster/compare/"*)
        sha=${endpoint#repos/buster14a/buster/compare/}; fixture="$FIXTURES/compare-${sha%%...*}.json" ;;
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
# The workflow's own token, as the fake gh logs it beside each call (the branch token's calls log theirs).
export GH_TOKEN=workflow-token
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
printf '#define BUSTER_NIGHTLY_WINDOW_START_HOUR 4\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 7\n' > "$history/tools/ci/nightly.c"
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
# The later state has the widened window; old commits keep their own worker declarations.
printf '#define BUSTER_NIGHTLY_WINDOW_START_HOUR 2\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8\n' > "$history/tools/ci/nightly.c"
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
    (cd "$1" && bash "$root/tools/ci/nightly.sh" evidence "$work/evidence-$RANDOM") > "$work/evidence.log" || return $?
    # Main's hosted rows are never reused, independently of hardware admission and nightly mode. In the nightly mode a
    # push runs the plain rows, its sanitizer rows and fuzzing the nightly snapshot's (#2057); before it, every row.
    if [ "${GITHUB_EVENT_NAME:-}" = push ]; then
        if [ "${EVIDENCE_NIGHTLY:-}" = on ]; then
            expect_line "$GITHUB_OUTPUT" 'scope=plain' 'a main push in the nightly mode runs the plain rows'
        else
            expect_line "$GITHUB_OUTPUT" 'scope=full' 'a main push before the nightly mode runs the full sanitizer and fuzz scope'
        fi
        expect_line "$GITHUB_OUTPUT" 'hosted=run' 'every main push runs its own hosted suite'
        expect_line "$GITHUB_OUTPUT" 'hosted_source=' 'PR evidence never replaces main hosted coverage'
        expect_line "$GITHUB_OUTPUT" 'retired=false' 'nightly mode never retires a main push'
    fi
}

# A pull request, draft or ready: hosted feedback, no hardware (#2055), and no API call. The workflow no longer passes
# the draft state; a stale EVIDENCE_DRAFT changes nothing.
for draft in true false ''; do
    GITHUB_EVENT_NAME=pull_request GITHUB_SHA=$head EVIDENCE_DRAFT=$draft run_evidence "$history"
    expect_line "$GITHUB_OUTPUT" 'scope=plain' 'a pull request runs the plain rows'
    expect_line "$GITHUB_OUTPUT" 'hosted=run' 'its own'
    expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'a pull request keeps off the 9700X'
    expect_line "$GITHUB_OUTPUT" 'hardware_reason=pull-request' 'and says why'
    expect_line "$GITHUB_OUTPUT" 'native=-' 'a pull request runs no native correctness'
    expect_line "$GITHUB_OUTPUT" 'measurement=-' 'and no measurements'
done
expect_no_match "$fixtures/calls.log" 'commits/|actions/' 'a pull request reads no API'
# A merge-queue candidate (#1430), documentation alone or not: the full hosted scope, its own, and no hardware, whose
# work is its first-parent state's in the window. No API call.
for revision in "$merge" "$docs"; do
    GITHUB_EVENT_NAME=merge_group GITHUB_SHA=$revision run_evidence "$history"
    expect_line "$GITHUB_OUTPUT" 'scope=full' 'a merge-queue candidate runs every hosted row and the fuzz campaigns'
    expect_line "$GITHUB_OUTPUT" 'hosted=run' 'its own'
    expect_line "$GITHUB_OUTPUT" 'hosted_source=' 'reusing no run'
    expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'a merge-queue candidate keeps off the 9700X'
    expect_line "$GITHUB_OUTPUT" 'hardware_reason=merge-group' 'and says why'
    expect_line "$GITHUB_OUTPUT" 'native=-' 'a candidate runs no native correctness'
    expect_line "$GITHUB_OUTPUT" 'measurement=-' 'and no measurements'
    expect_line "$GITHUB_OUTPUT" 'retired=false' 'and is never retired'
done
expect_no_match "$fixtures/calls.log" 'commits/|actions/' 'a merge-queue candidate reads no API'
# Any other event is no supported path: the job fails and nothing runs.
for event in pull_request_target workflow_run repository_dispatch; do
    if GITHUB_EVENT_NAME=$event GITHUB_SHA=$head run_evidence "$history" 2> /dev/null; then
        fail "the evidence job accepted a $event run"
    fi
    expect_no_match "$GITHUB_OUTPUT" 'hardware=admit' "a $event run admits no hardware"
done

# The nightly mode retires the daily run; before it, the daily run is as it was.
GITHUB_EVENT_NAME=schedule GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'retired=true' 'the nightly mode retires the daily run'
GITHUB_EVENT_NAME=schedule GITHUB_SHA=$head EVIDENCE_NIGHTLY='' run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'retired=false' 'without the nightly mode the daily run runs'
expect_line "$GITHUB_OUTPUT" 'scope=full' 'the daily run runs every row'
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'and is admitted'

# Landed merges, documentation-only revisions and direct pushes all get their own hosted rows in either nightly mode
# (#1271). Each checkout and GITHUB_SHA must agree, as the workflow's do. In the nightly mode a push runs the plain rows
# (#2057) and keeps off the 9700X, its native correctness and measurements deferred to the window; before it, a push
# runs every row and qualifies on hardware (#1272).
for revision in "$merge" "$docs" "$head"; do
    git -C "$history" worktree add -q --detach "$work/main-push" "$revision"
    GITHUB_EVENT_NAME=push GITHUB_SHA=$revision EVIDENCE_NIGHTLY=on run_evidence "$work/main-push"
    expect_line "$GITHUB_OUTPUT" 'scope=plain' 'a main push runs the plain rows in the nightly mode'
    expect_line "$GITHUB_OUTPUT" 'hosted=run' 'its own'
    expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'and keeps off the 9700X'
    expect_line "$GITHUB_OUTPUT" 'hardware_reason=deferred' 'deferring it to the window'
    GITHUB_EVENT_NAME=push GITHUB_SHA=$revision EVIDENCE_NIGHTLY='' run_evidence "$work/main-push"
    expect_line "$GITHUB_OUTPUT" 'scope=full' 'before the nightly mode a main push runs every hosted row'
    expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'and qualifies on hardware'
    git -C "$history" worktree remove "$work/main-push"
done

# A push to main needs no pull request's native evidence, and reads no API: before the nightly mode it runs its own
# native correctness; in it, none here.
tree=$(git -C "$history" rev-parse "$head^{tree}")
other=$(printf 'other' | sha1sum | cut -c 1-40)
now=$(madrid '2026-11-04 12:00')
export NIGHTLY_NOW=$now
: > "$fixtures/calls.log"
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY='' run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=run' 'before the nightly mode a push runs native correctness'
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'and is admitted'
expect_line "$GITHUB_OUTPUT" 'native_source=' 'reusing no pull request'"'"'s run'
GITHUB_EVENT_NAME=push GITHUB_SHA=$head EVIDENCE_NIGHTLY=on run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'native=-' 'in the nightly mode a push runs no native correctness here'
expect_line "$GITHUB_OUTPUT" 'measurement=-' 'and no measurements'
expect_line "$GITHUB_OUTPUT" 'native_source=' 'reusing no pull request'"'"'s run either'
expect_no_match "$fixtures/calls.log" 'pulls|actions/' 'a push looks up no pull request run'

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
expect_line "$GITHUB_OUTPUT" "window_start=$(madrid '2026-11-04 02:00')" 'the cycle'"'"'s window opens at 02:00 Madrid'
expect_line "$GITHUB_OUTPUT" "window_deadline=$(madrid '2026-11-04 08:00')" 'and closes at 08:00'
expect_line "$GITHUB_OUTPUT" 'window_override=false' 'without an override'
NIGHTLY_NOW=$(madrid '2026-11-04 07:40') GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$merge EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=admit EVIDENCE_SNAPSHOT=$head run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'too late to finish by 08:00, it is not'
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
# A backlog run of a state that a merge-queue candidate's run tested (#1430): main was fast-forwarded to that very
# commit, so the candidate's plain rows are the state's own.
cat > "$fixtures/runs-head-$docs.json" <<JSON
{"workflow_runs":[{"id":810,"run_attempt":1,"event":"merge_group","status":"completed","conclusion":"success","head_sha":"$docs","path":".github/workflows/9700x.yml","head_branch":"gh-readonly-queue/main/pr-7-$merge","created_at":"2026-11-03T10:00:00Z","updated_at":"2026-11-03T11:30:00Z","head_repository":{"id":1421805693},"display_title":"docs"}]}
JSON
{
    printf '{"jobs":['
    for arch in x86-64 native; do
        for shard in 1 2 3 4; do
            printf '{"name":"Tests on Ubuntu 26.04 (Clang 23, %s, none, O3, shard %s of 4)","status":"completed","conclusion":"success"},' "$arch" "$shard"
        done
    done
    printf '{"name":"Build and execute on Ryzen 7 9700X","status":"completed","conclusion":"success"}]}\n'
} > "$fixtures/jobs-810.json"
NIGHTLY_NOW=$(madrid '2026-11-04 04:10') GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$docs EVIDENCE_PURPOSE=backlog EVIDENCE_HARDWARE=admit EVIDENCE_SNAPSHOT=$head run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hosted=reused' 'a backlog run reuses the plain rows of its commit'"'"'s merge-queue run'
expect_line "$GITHUB_OUTPUT" 'hosted_source=810' 'naming the candidate'"'"'s run'
rm -f "$fixtures/runs-head-$docs.json"

# --- The aggregate's coverage check for a merge-queue candidate (#1430), against this run's jobs ---

# A full run's jobs as the API names them: each row's shards under its caller's name, the rows' builds, skipped jobs'
# unexpanded names, the vector pool's verdict and the fuzz campaigns. ODD names a job to drop or to conclude otherwise.
full_jobs()
{
    local odd=${1:-} conclusion=${2:-success} arch sanitizer shards shard name
    printf '{"jobs":['
    for arch in x86-64 native; do
        for sanitizer in none address thread memory; do
            shards=4
            if [ "$arch" = x86-64 ] && { [ "$sanitizer" = thread ] || [ "$sanitizer" = memory ]; }; then shards=6; fi
            if [ "$arch" = native ] && [ "$sanitizer" = memory ]; then shards=5; fi
            printf '{"name":"Tests on Ubuntu 26.04 (%s, %s, O3) / Build the compiler once for the row'"'"'s shards","status":"completed","conclusion":"success"},' "$arch" "$sanitizer"
            printf '{"name":"Tests on Ubuntu 26.04 (%s, %s, O3) / Vector row verdict (Clang 23, ${{ inputs.sanitizer }}, O${{ inputs.optimization }})","status":"completed","conclusion":"skipped"},' "$arch" "$sanitizer"
            for shard in $(seq 1 "$shards"); do
                name="Tests on Ubuntu 26.04 ($arch, $sanitizer, O3) / Tests on Ubuntu 26.04 (Clang 23, $arch, $sanitizer, O3, shard $shard of $shards)"
                if [ "$odd" = "$arch $sanitizer $shard" ]; then
                    if [ "$conclusion" = missing ]; then continue; fi
                    printf '{"name":"%s","status":"completed","conclusion":"%s"},' "$name" "$conclusion"
                else
                    printf '{"name":"%s","status":"completed","conclusion":"success"},' "$name"
                fi
            done
        done
    done
    printf '{"name":"Tests on Ubuntu 26.04 (vector, none address thread memory, O3) / Tests on Ubuntu 26.04 (Clang 23, ${{ inputs.host_arch }}, ${{ inputs.sanitizer }}, O${{ inputs.optimization }}, shard ${{ matrix.shard }} of ${{ inputs.shards }})","status":"completed","conclusion":"skipped"},'
    printf '{"name":"Tests on Ubuntu 26.04 (vector, none address thread memory, O3) / Vector draw 1, candidate 3 (Clang 23, none address thread memory, O3)","status":"completed","conclusion":"success"},'
    if [ "$odd" != vector ]; then
        printf '{"name":"Tests on Ubuntu 26.04 (vector, none address thread memory, O3) / Vector row verdict (Clang 23, none address thread memory, O3)","status":"completed","conclusion":"%s"},' "$( [ "$odd" = vector-failed ] && echo failure || echo success )"
    fi
    for entry in frontend elf-override crash-replay-proof; do
        if [ "$odd" != "fuzz $entry" ]; then
            printf '{"name":"libFuzzer (ASan + UBSan + leaks, %s)","status":"completed","conclusion":"success"},' "$entry"
        fi
    done
    printf '{"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"skipped"},'
    # The auxiliary workflows: selfhost.yml called (its jobs, a nested call's included), the others skipped.
    printf '{"name":"Auxiliary (build.yml)","status":"completed","conclusion":"skipped"},'
    printf '{"name":"Auxiliary (selfhost.yml) / Self-host harness regression tests","status":"completed","conclusion":"success"},'
    printf '{"name":"Auxiliary (selfhost.yml) / prepare-hosted-tools / Prepare verified hosted release inputs","status":"completed","conclusion":"success"},'
    printf '{"name":"Auxiliary (selfhost.yml) / Self-host bootstrap and probe equivalence (native)","status":"completed","conclusion":"%s"},' "$( [ "$odd" = selfhost-failed ] && echo failure || echo success )"
    printf '{"name":"Build and execute on Ryzen 7 9700X","status":"in_progress","conclusion":null}]}\n'
}
coverage_case()
{
    local run=$1 expected=$2 status=0
    : > "$fixtures/calls.log"
    : > "$GITHUB_STEP_SUMMARY"
    rm -f "$fixtures"/count-*
    AUXILIARY=${3-,selfhost,} GITHUB_RUN_ID=$run NIGHTLY_SLEEP=0 NIGHTLY_COVERAGE_TRIES=2 bash tools/ci/nightly.sh coverage "$work/coverage-$run" > "$work/coverage.log" 2>&1 || status=$?
    [ "$status" -eq "$expected" ] || { cat "$work/coverage.log" >&2; fail "coverage of run $run: expected $expected, got $status"; }
}
full_jobs > "$fixtures/jobs-9100.json"
coverage_case 9100 0
expect_line "$work/coverage.log" $'coverage\tcomplete' 'a full candidate'"'"'s coverage is complete'
expect_line "$work/coverage.log" $'row\tnative memory\tok\t5' 'each row by its own shard count'
expect_line "$work/coverage.log" $'auxiliary\tselfhost\tcalled\tok\t3' 'a called auxiliary workflow, every job of it'
expect_line "$work/coverage.log" $'auxiliary\tbuild\tnot called\tok\t0' 'and one not called, which ran nothing'
expect_match "$GITHUB_STEP_SUMMARY" 'Merge-queue coverage' 'and the summary says so'
test "$(grep -c 'actions/runs/9100/jobs' "$fixtures/calls.log")" -eq 1 || fail 'a complete set is read once'
# Each way a candidate's coverage falls short fails it, after asking again.
for odd in 'x86-64 address 3:missing' 'native memory 5:missing' 'x86-64 thread 2:cancelled' 'native none 1:skipped' 'x86-64 memory 6:timed_out' 'vector:' 'vector-failed:' 'fuzz elf-override:' 'selfhost-failed:'; do
    full_jobs "${odd%%:*}" "${odd#*:}" > "$fixtures/jobs-9101.json"
    coverage_case 9101 1
    expect_line "$work/coverage.log" $'coverage\tincomplete' "a candidate with $odd is incomplete"
    expect_match "$work/coverage.log" '::error title=Merge-queue coverage' 'and says so'
    test "$(grep -c 'actions/runs/9101/jobs' "$fixtures/calls.log")" -eq 2 || fail 'an incomplete set is asked for again'
done
# The selector's auxiliary list against the jobs: one it names that ran nothing, one that ran though it doesn't name it,
# and no list at all (the selector didn't run).
full_jobs > "$fixtures/jobs-9101.json"
coverage_case 9101 1 ,build,selfhost,
expect_line "$work/coverage.log" $'auxiliary\tbuild\tcalled\tmissing\t0' 'a called auxiliary workflow that ran nothing'
coverage_case 9101 1 ,
expect_line "$work/coverage.log" $'auxiliary\tselfhost\tnot called\tran although it doesn\'t apply\t3' 'an auxiliary workflow the selector did not name'
coverage_case 9101 1 ''
expect_line "$work/coverage.log" $'auxiliary\t\tnot the selector\'s list' 'no selector list'
# A shard beyond the row's count, or a row whose shards disagree.
full_jobs | sed 's/{"name":"Native tests/{"name":"Tests on Ubuntu 26.04 (x86-64, none, O3) \/ Tests on Ubuntu 26.04 (Clang 23, x86-64, none, O3, shard 5 of 5)","status":"completed","conclusion":"success"},&/' > "$fixtures/jobs-9101.json"
coverage_case 9101 1
expect_line "$work/coverage.log" $'row\tx86-64 none\tshard counts disagree\t5' 'shard counts that disagree are no coverage'
# A failed API call, then the full set: the second read passes it.
full_jobs > "$fixtures/jobs-9102.json"
touch "$fixtures/jobs-9102.json.1.fail"
coverage_case 9102 0
test "$(grep -c 'actions/runs/9102/jobs' "$fixtures/calls.log")" -eq 2 || fail 'a failed read is asked for again'

# --- The stale job: invalidated merge-queue candidates' runs (#1430) ---

# Unfinished candidates' runs, each listed for every status the job asks about (the fake answers each alike): this run;
# one still queued; one dropped; one merged; one rebuilt on the same base (its branch moved); one whose branch read
# fails; one on a branch that isn't a candidate's; and one dropped whose cancel GitHub refuses.
queue_base=$(printf '%040d' 7)
queue_branch() { printf 'gh-readonly-queue/main/pr-%s-%s' "$1" "$queue_base"; }
queue_sha() { printf '%039d%s' 0 "$1"; }
{
    printf '{"workflow_runs":['
    printf '{"id":9200,"head_branch":"%s","head_sha":"%s"},' "$(queue_branch 1)" "$(queue_sha 1)"
    printf '{"id":9201,"head_branch":"%s","head_sha":"%s"},' "$(queue_branch 2)" "$(queue_sha 2)"
    printf '{"id":9202,"head_branch":"%s","head_sha":"%s"},' "$(queue_branch 3)" "$(queue_sha 3)"
    printf '{"id":9203,"head_branch":"%s","head_sha":"%s"},' "$(queue_branch 4)" "$(queue_sha 4)"
    printf '{"id":9204,"head_branch":"%s","head_sha":"%s"},' "$(queue_branch 5)" "$(queue_sha 5)"
    printf '{"id":9205,"head_branch":"%s","head_sha":"%s"},' "$(queue_branch 6)" "$(queue_sha 6)"
    printf '{"id":9206,"head_branch":"main","head_sha":"%s"},' "$(queue_sha 7)"
    printf '{"id":9207,"head_branch":"%s","head_sha":"%s"}' "$(queue_branch 8)" "$(queue_sha 8)"
    printf ']}\n'
} > "$fixtures/runs-merge_group.json"
printf '{"object":{"sha":"%s"}}\n' "$(queue_sha 1)" > "$fixtures/queue-ref-pr-1-$queue_base.json"
printf '{"object":{"sha":"%s"}}\n' "$(queue_sha 2)" > "$fixtures/queue-ref-pr-2-$queue_base.json"
printf '{"object":{"sha":"%s"}}\n' "$(queue_sha 9)" > "$fixtures/queue-ref-pr-5-$queue_base.json"
printf '{"object":{"sha":"%s"}}\n' "$(queue_sha 6)" > "$fixtures/queue-ref-pr-6-$queue_base.json"
touch "$fixtures/queue-ref-pr-6-$queue_base.json.1.fail"
printf '{"status":"behind"}\n' > "$fixtures/compare-$(queue_sha 3).json"
printf '{"status":"ahead"}\n' > "$fixtures/compare-$(queue_sha 4).json"
printf '{"status":"diverged"}\n' > "$fixtures/compare-$(queue_sha 5).json"
printf '{"status":"behind"}\n' > "$fixtures/compare-$(queue_sha 8).json"
touch "$fixtures/uncancellable-9207"
: > "$fixtures/calls.log"
: > "$GITHUB_STEP_SUMMARY"
rm -f "$fixtures"/count-*
status=0
GITHUB_RUN_ID=9200 bash tools/ci/nightly.sh stale "$work/stale" > "$work/stale.out" 2>&1 || status=$?
[ "$status" -eq 1 ] || { cat "$work/stale.out" >&2; fail "a refused cancel fails the stale job (status $status)"; }
expect_line "$fixtures/calls.log" 'api -X POST repos/buster14a/buster/actions/runs/9202/cancel' 'a dropped candidate'"'"'s run is cancelled'
expect_line "$fixtures/calls.log" 'api -X POST repos/buster14a/buster/actions/runs/9204/cancel' 'a candidate rebuilt on the same base: its old run is cancelled'
test "$(grep -c '/cancel$' "$fixtures/calls.log")" -eq 3 || { cat "$fixtures/calls.log" >&2; fail 'only the invalidated candidates are cancelled'; }
expect_line "$work/stale.out" $'kept\t9200\tthis run' 'this run is kept'
expect_line "$work/stale.out" $'kept\t9201\tits candidate is queued' 'a queued candidate is kept'
expect_line "$work/stale.out" $'kept\t9203\tit merged' 'a merged candidate is kept'
expect_line "$work/stale.out" $'kept\t9205\tits branch is unknown' 'a failed branch read cancels nothing'
expect_line "$work/stale.out" $'kept\t9206\tits branch is unknown' 'a run on another branch is kept'
expect_line "$work/stale.out" $'not cancelled\t9207' 'a refused cancel is reported'
expect_match "$GITHUB_STEP_SUMMARY" 'Invalidated merge-queue candidates' 'the summary lists them'
test "$(grep -c 'git/ref/heads/gh-readonly-queue' "$fixtures/calls.log")" -eq 6 || fail 'each candidate'"'"'s branch is read once'
# Without the refused one, the job passes.
rm -f "$fixtures/uncancellable-9207" "$fixtures"/count-*
GITHUB_RUN_ID=9200 bash tools/ci/nightly.sh stale "$work/stale-2" > "$work/stale.out" 2>&1 || { cat "$work/stale.out" >&2; fail 'the stale job passes once every cancel does'; }
expect_match "$work/stale.out" 'Cancelled 3 invalidated candidate runs; 0 cancels failed' 'and counts them'
rm -f "$fixtures/runs-merge_group.json"

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
start_epoch=$(madrid '2026-11-04 02:00')
deadline_epoch=$(madrid '2026-11-04 08:00')
window_case $((start_epoch + 600)) "$start_epoch" "$deadline_epoch" false 25 || fail 'the window step admits inside the window'
expect_line "$GITHUB_OUTPUT" 'window=open' 'and says so'
if window_case $((deadline_epoch - 20 * 60)) "$start_epoch" "$deadline_epoch" false 25; then
    fail 'a lock taken at 07:40 admitted a 25-minute bound'
fi
expect_line "$GITHUB_OUTPUT" 'window=closed' 'a lock taken too late closes the window'
if window_case $((deadline_epoch + 3600)) "$start_epoch" "$deadline_epoch" false 25; then
    fail 'a lock taken after 08:00 admitted'
fi
if window_case $((start_epoch - 60)) "$start_epoch" "$deadline_epoch" false 25; then
    fail 'a lock taken before 02:00 admitted'
fi
if window_case $((start_epoch + 600)) '' '' false 25; then
    fail 'missing instants admitted'
fi
window_case $((deadline_epoch + 3600)) "$start_epoch" "$deadline_epoch" true 25 || fail 'an override admits after the lock'

# --- The host identity, as the workflow computes it, against stand-in tools ---

# The lines of the host identity's step from the kernel's to the identity's end, /proc/cpuinfo replaced by a fixture.
identity_step="$work/identity-step.sh"
awk '/^      - name: Record the host identity$/ { step = 1 } step && /^          kernel=\$\(uname -r/ { body = 1 }
    body { line = $0; sub(/^          /, "", line); print line } body && /^          # The identity.s end\.$/ { exit }' \
    "$root/.github/workflows/9700x.yml" | sed "s#/proc/cpuinfo#$work/cpuinfo#" > "$identity_step"
grep -q 'sw-%s' "$identity_step" || fail 'the host identity'"'"'s step is found in the workflow'
printf 'model name\t: AMD Ryzen 7 9700X\nmicrocode\t: 0xb404023\n' > "$work/cpuinfo"
mkdir -p "$work/identity-bin"
printf '#!/bin/sh\ncase "$1" in -s) echo Linux ;; -m) echo x86_64 ;; *) echo 6.17.1-arch1 ;; esac\n' > "$work/identity-bin/uname"
printf '#!/bin/sh\necho glibc 2.42\n' > "$work/identity-bin/getconf"
# A stand-in package database that answers as pacman does: -Q lists the packages (FAKE_VERSIONS); -Qo names glibc as the
# runtime fixtures' owner, and for any other file says on stderr that no package owns it; -Ql lists the owners' files
# as "PACKAGE PATH" (glibc: the runtime and a header; the other packages none). FAKE_PACMAN selects a failure: an
# ownership query that fails, that leaves a file unanswered or that names an owner at another version than -Q's; a
# file listing that fails after a partial list, that warns on stderr, or that omits a file -Qo attributed.
mkdir -p "$work/runtime/include"
printf 'static runtime\n' > "$work/runtime/libc.a"
printf 'startup object\n' > "$work/runtime/crt1.o"
printf '#define VERSION 1\n' > "$work/runtime/include/stdio.h"
export FAKE_RUNTIME
FAKE_RUNTIME=$(readlink -f "$work/runtime")
cat > "$work/identity-bin/pacman" <<'SH'
#!/bin/sh
operation=$1
shift
if [ "${1:-}" = --color ]; then shift 2; fi
case "$operation" in
    -Q) printf '%s\n' "${FAKE_VERSIONS:-gcc 15.2.1-1}" "binutils 2.45-1" "glibc 2.42-1" ;;
    -Qo)
        if [ "${FAKE_PACMAN:-}" = ownership-fails ]; then
            echo 'error: could not open database' >&2
            exit 1
        fi
        status=0
        for file in "$@"; do
            case "$file" in
                "$FAKE_RUNTIME"/*)
                    if [ "${FAKE_PACMAN:-}" = ownership-unanswered ] && [ "$file" = "$FAKE_RUNTIME/crt1.o" ]; then continue; fi
                    if [ "${FAKE_PACMAN:-}" = ownership-other-version ]; then
                        echo "$file is owned by glibc 2.42-2"
                    else
                        echo "$file is owned by glibc 2.42-1"
                    fi ;;
                *) echo "error: No package owns $file" >&2; status=1 ;;
            esac
        done
        exit "$status" ;;
    -Ql)
        for package in "$@"; do
            if [ "$package" = glibc ]; then
                printf 'glibc %s\n' "$FAKE_RUNTIME/"
                if [ "${FAKE_PACMAN:-}" != listing-omits ]; then printf 'glibc %s\n' "$FAKE_RUNTIME/libc.a"; fi
                printf 'glibc %s\n' "$FAKE_RUNTIME/crt1.o" "$FAKE_RUNTIME/include/"
                if [ "${FAKE_PACMAN:-}" = listing-fails ]; then
                    echo 'error: could not read the files of glibc' >&2
                    exit 1
                fi
                printf 'glibc %s\n' "$FAKE_RUNTIME/include/stdio.h"
            fi
        done
        if [ "${FAKE_PACMAN:-}" = listing-warns ]; then echo 'warning: glibc: the files list is damaged' >&2; fi ;;
    *) exit 1 ;;
esac
SH
cat > "$work/identity-bin/gcc" <<SH
#!/bin/sh
case "\$1" in
    -dumpfullversion) echo 15.2.1 ;;
    -print-file-name=libc.a) echo "$work/runtime/libc.a" ;;
    -print-file-name=crt1.o) echo "$work/runtime/crt1.o" ;;
    -print-file-name=*) echo "\$1" | sed 's/^-print-file-name=//' ;;
    *) echo "\$1" | sed "s/^-print-prog-name=//" ;;
esac
SH
printf '#!/bin/sh\necho "GNU objdump 2.45"\n' > "$work/identity-bin/objdump"
chmod +x "$work/identity-bin"/*
identity()
{
    : > "$GITHUB_OUTPUT"
    # As the workflow's shell runs a step: errexit and pipefail.
    PATH="$work/identity-bin:$PATH" bash -eo pipefail "$identity_step" > "$work/identity.log" 2>&1 || true
    sed -n 's/^host=//p' "$GITHUB_OUTPUT"
}
first=$(identity)
[[ "$first" =~ ^9700X/6.17.1-arch1/0xb404023/gcc-15.2.1/glibc_2.42/sw-[0-9a-f]{16}$ ]] || fail "the host identity has every component: $first"
test "$(identity)" = "$first" || fail 'the host identity is deterministic'
# An external oracle replaced, its version the same, and nothing else changed: another identity, so no reuse matches.
printf '#!/bin/sh\necho "GNU objdump 2.45"\n# rebuilt\n' > "$work/identity-bin/objdump"
second=$(identity)
[[ "$second" == 9700X/6.17.1-arch1/0xb404023/gcc-15.2.1/glibc_2.42/sw-* ]] && [ "$second" != "$first" ] || fail 'a replaced objdump of the same version changes the identity'
# The static runtime (test-static's cc -static) changed in place, its package's version the same: another identity.
printf 'static runtime, rebuilt\n' > "$work/runtime/libc.a"
runtime=$(identity)
[[ "$runtime" == 9700X/6.17.1-arch1/0xb404023/gcc-15.2.1/glibc_2.42/sw-* ]] && [ "$runtime" != "$second" ] || fail 'a changed libc.a changes the identity'
# A header of the toolchain's packages changed: another identity.
printf '#define VERSION 2\n' > "$work/runtime/include/stdio.h"
header=$(identity)
[ "$header" != "$runtime" ] || fail 'a changed header changes the identity'
# A package updated, every file the same: another identity.
third=$(FAKE_VERSIONS='gcc 15.2.1-2' identity)
[ "$third" != "$header" ] && [ "$third" != "$first" ] || fail 'a package rebuilt under the same GCC version changes the identity'
# A package database that doesn't answer completely leaves no identity, so the header changed meanwhile can't go
# unnoticed: the files' listing failing after a partial list (the header among the files it omits), the listing
# warning, the listing omitting a file -Qo attributed to its package, the ownership query failing, leaving a file
# unanswered or naming another version than -Q's. A file no package owns (the real tools here) is not a failure.
test "$(identity)" = "$header" || fail 'the database answering completely again gives the identity back'
printf '#define VERSION 3\n' > "$work/runtime/include/stdio.h"
for failure in 'listing-fails:the owners file listing failed' 'listing-warns:the owners file listing failed' \
    'listing-omits:the owners file listing omits files attributed to them' \
    'ownership-fails:the ownership query did not account for every file' \
    'ownership-unanswered:the ownership query did not account for every file' \
    'ownership-other-version:an owner at another version than -Q lists'; do
    test -z "$(FAKE_PACMAN=${failure%%:*} identity)" || fail "an incomplete package database (${failure%%:*}) leaves no identity"
    grep -Fq "(software: ${failure#*:})" "$work/identity.log" || fail "and the warning says why: ${failure#*:}"
done
changed=$(identity)
[ -n "$changed" ] && [ "$changed" != "$header" ] || fail 'the header changed while the database failed changes the identity'
# The 9700X job's steps from its first to the host identity's, run in order as the runner does (each in the workflow's
# shell, the job ending at the first that fails): with the window closed when the job took the lock, it refuses
# before any package query or hash; open, it records the identity after the window's check.
sequence="$work/sequence"
mkdir -p "$sequence/bin"
awk -v out="$sequence" '/^  benchmarks:$/ { job = 1; next } job && /^  [a-z]/ { exit }
    job && /^      - name: / { body = 0; if ($0 ~ /Use the runner.s private cache/) exit; steps += 1; file = sprintf("%s/step-%02d.sh", out, steps) }
    job && steps && /^        run: \|$/ { body = 1; next }
    job && body && /^          / { line = $0; sub(/^          /, "", line); print line > file; next }
    job && body && /^$/ { print "" > file }' "$root/.github/workflows/9700x.yml"
test "$(find "$sequence" -name 'step-*.sh' | wc -l)" -eq 3 || fail 'the 9700X job starts with the host check, the window and the host identity'
for tool in pacman sha256sum; do
    printf '#!/bin/sh\nprintf "%%s\\n" "%s $*" >> "%s/work.log"\nPATH="%s" exec %s "$@"\n' "$tool" "$sequence" "$work/identity-bin:$PATH" "$tool" > "$sequence/bin/$tool"
done
chmod +x "$sequence/bin"/*
sequence_case()
{
    : > "$GITHUB_OUTPUT"
    : > "$sequence/work.log"
    local step
    for step in "$sequence"/step-*.sh; do
        sed "s#/proc/cpuinfo#$work/cpuinfo#" "$step" > "$sequence/current.sh"
        FAKE_NOW=$1 WINDOW_START=$start_epoch WINDOW_DEADLINE=$deadline_epoch WINDOW_OVERRIDE=false BOUND=25 \
            PATH="$sequence/bin:$work/clock:$work/identity-bin:$PATH" bash -eo pipefail "$sequence/current.sh" > /dev/null 2>&1 || return 0
    done
}
sequence_case $((deadline_epoch + 600))
expect_line "$GITHUB_OUTPUT" 'window=closed' 'a job that took the lock after 08:00 refuses'
test ! -s "$sequence/work.log" || { cat "$sequence/work.log" >&2; fail 'and queries no package and hashes nothing first'; }
expect_no_match "$GITHUB_OUTPUT" '^host=' 'nor records an identity'
sequence_case $((start_epoch + 600))
expect_line "$GITHUB_OUTPUT" 'window=open' 'a job inside the window is admitted'
expect_line "$GITHUB_OUTPUT" "host=$changed" 'and records the host identity after the window'"'"'s check'
expect_match "$sequence/work.log" '^pacman -Q' 'by the package queries'
# A package's file that can't be read leaves no identity.
chmod 000 "$work/runtime/crt1.o"
if [ "$(id -u)" != 0 ]; then
    test -z "$(identity)" || fail 'an unreadable runtime file leaves no identity'
    grep -Fq "(software: a file that can't be hashed)" "$work/identity.log" || fail 'and the warning says why'
fi
chmod 644 "$work/runtime/crt1.o"
# No package database: no identity at all.
printf '#!/bin/sh\nexit 1\n' > "$work/identity-bin/pacman"
test -z "$(identity)" || fail 'without a package database no identity is recorded'
grep -Fq '(software: no package database)' "$work/identity.log" || fail 'and the warning says why'

# --- The record job, in this repository's checkout ---

: > "$GITHUB_OUTPUT"
RECORD_REPOSITORY=1421805693 RECORD_RUN=41 RECORD_ATTEMPT=1 RECORD_EVENT=workflow_dispatch RECORD_DISPATCH_PURPOSE=backlog \
    RECORD_REQUEST=20261104.9.1.admit RECORD_MODE=on RECORD_CODE=true RECORD_MEASURE=,lexer, \
    CHANGES_OUTPUTS='{"code":"true","checker":"true","zen5":"false","measure":",lexer,"}' RECORD_CHANGES_RESULT=success \
    RECORD_EVIDENCE_RESULT=success RECORD_SCOPE=plain RECORD_HOSTED=reused RECORD_HOSTED_SOURCE=40 RECORD_HARDWARE=admit \
    RECORD_HARDWARE_REASON=admitted RECORD_TEST_RESULT=skipped RECORD_FUZZ_RESULT=skipped RECORD_BENCHMARK_RESULT=success \
    RECORD_WINDOW=open RECORD_NATIVE=passed RECORD_HOST=9700X/6.17/0xb40/gcc-15/glibc_2.42/sw-0123456789abcdef \
    bash tools/ci/nightly.sh record "$work/record-job" > /dev/null
expect_line "$GITHUB_OUTPUT" 'outcome=complete' 'a backlog run that measured is complete'
expect_line "$work/record-job/record.tsv" $'gates\tchecker' 'the record names the selector'"'"'s true outputs'
# A merge-queue candidate's record (#1430), in a checkout of GitHub's merge: its base, and its pull request's head as
# the merge's second parent. It passes through the aggregate alone.
candidate="$work/candidate"
git init -q -b main "$candidate"
for file in .github/workflows/9700x.yml tools/ci/changes.c tools/ci/correctness.tests tools/ci/setup-benchpress.sh tools/ci/fetch-corpus.sh; do
    mkdir -p "$candidate/$(dirname "$file")"
    cp "$file" "$candidate/$file"
done
git -C "$candidate" add -A
git -C "$candidate" -c user.name=t -c user.email=t@example.com commit -q -m base
candidate_base=$(git -C "$candidate" rev-parse HEAD)
git -C "$candidate" checkout -q -b pull
echo change >> "$candidate/tools/ci/correctness.tests"
git -C "$candidate" -c user.name=t -c user.email=t@example.com commit -q -am pull
candidate_head=$(git -C "$candidate" rev-parse HEAD)
git -C "$candidate" checkout -q main
git -C "$candidate" -c user.name=t -c user.email=t@example.com merge -q --no-ff -m 'Merge pull request #7' pull
queue_record()
{
    : > "$GITHUB_OUTPUT"
    (cd "$candidate" && RECORD_REPOSITORY=1421805693 RECORD_RUN=42 RECORD_ATTEMPT=1 RECORD_EVENT=merge_group RECORD_MODE=on RECORD_CODE=true \
        RECORD_MEASURE=all CHANGES_OUTPUTS='{"code":"true","performance":"true","measure":"all"}' RECORD_CHANGES_RESULT=success \
        RECORD_EVIDENCE_RESULT=success RECORD_SCOPE=full RECORD_HOSTED=run RECORD_HARDWARE=skip RECORD_HARDWARE_REASON=merge-group \
        RECORD_TEST_RESULT="$1" RECORD_FUZZ_RESULT="$2" RECORD_BENCHMARK_RESULT=skipped RECORD_QUALIFICATION_RESULT="$3" \
        bash "$root/tools/ci/nightly.sh" record "$work/queue-record") > /dev/null
}
queue_record success success success
expect_line "$GITHUB_OUTPUT" 'outcome=deferred' 'a candidate the aggregate passed is deferred to the window'
expect_line "$work/queue-record/record.tsv" $'purpose\tqueue' 'its record is a queue record'
expect_line "$work/queue-record/record.tsv" $'coverage\tnone' 'that measures nothing'
expect_line "$work/queue-record/record.tsv" "parent	$candidate_base" 'with its base'
expect_line "$work/queue-record/record.tsv" "pr_head	$candidate_head" 'and its pull request'"'"'s head'
expect_line "$work/queue-record/record.tsv" $'gates\tperformance' 'and the profile'"'"'s one gate'
queue_record success success failure
expect_line "$GITHUB_OUTPUT" 'outcome=interrupted' 'a candidate the aggregate failed is no pass, even with its tests green'
queue_record success skipped success
expect_line "$GITHUB_OUTPUT" 'outcome=deferred' 'the aggregate decides whether the fuzz campaigns had to run'
queue_record failure success failure
expect_line "$GITHUB_OUTPUT" 'outcome=blocked-hosted' 'a candidate whose tests failed is blocked'
"$work/nightly" harvest --ledger /dev/null --facts /dev/null --states /dev/null --list 2> /dev/null && fail 'an empty state list was accepted'

# --- The coordinator ---

run_coordinator()
{
    : > "$GITHUB_OUTPUT"
    (cd "$root" && bash tools/ci/nightly.sh coordinate "$1" --phase "$2" --mode "$3") > "$work/coordinate.log" 2>&1 || { cat "$work/coordinate.log" >&2; fail 'the coordinator failed'; }
}
# The prepare job's pending dispatches as its output hands them to the window job (none: a valid empty list).
none_pending='buster-nightly-pending-1 0'
handed() { sed -n 's/^pending=//p' "$GITHUB_OUTPUT"; }
: > "$fixtures/calls.log"
rm -f "$fixtures/runs-head-"*.json
GITHUB_RUN_ID=9100 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') run_coordinator "$work/dry" prepare dry-run
expect_line "$work/dry/states.tsv" "state	$epoch	$(git -C "$history" rev-parse "$epoch^1")	1793700100" 'the epoch is the first state'
expect_line "$work/dry/states.tsv" $'worker-window\t4\t7' 'historical workers retain their 04:00-07:00 declaration'
expect_line "$work/dry/states.tsv" $'worker-window\t2\t8' 'later workers declare the expanded window'
test "$(find "$work/dry/worker-windows" -name '*.tsv' | wc -l)" -eq 2 || fail 'worker metadata is cached once per source blob'
expect_no_match "$work/dry/states.tsv" "$inside" 'a merged branch'"'"'s own commits are not states'
expect_match "$work/dry/report.md" '\| not-required \| 1 \|' 'the documentation change needs nothing'
expect_match "$work/dry/plan.tsv" "^would-dispatch	snapshot	defer	$head	20261104\.9100\.1\.defer	$head$" 'the dry run would prepare the snapshot'
expect_match "$work/dry/plan.tsv" "^would-dispatch	backlog	defer	$epoch	" 'and the oldest states'"'"' hosted rows'
expect_line "$work/dry/plan.tsv" $'done\tdry run' 'and does nothing'
expect_no_match "$fixtures/calls.log" '^workflow run|-X POST|-X DELETE' 'a dry run writes nothing'
expect_line "$GITHUB_OUTPUT" 'window=closed' 'a dry run never waits for the window'
expect_line "$GITHUB_OUTPUT" "snapshot=$head" 'the cycle pins main'"'"'s head'
expect_line "$GITHUB_OUTPUT" "pending=$none_pending" 'and hands over a valid empty list of pending dispatches'

# The ledger's publication to buster-performance: it creates the nightly-ledger branch with the ledger and the
# cycle's report; and when the push is refused (a token GitHub rejects, say), the step still succeeds and hands over
# its outputs, so the window job's night doesn't depend on it, and only a warning says so.
git init -q --bare "$work/performance.git"
NIGHTLY_PUBLICATION_TOKEN=token NIGHTLY_PERFORMANCE_REMOTE="$work/performance.git" GITHUB_RUN_ID=9110 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') \
    run_coordinator "$work/published" prepare dry-run
expect_match "$work/coordinate.log" 'The ledger is published' 'the ledger is published'
git -C "$work/performance.git" cat-file -e nightly-ledger:nightly/main-coverage.tsv || fail 'the published branch holds the ledger'
git -C "$work/performance.git" cat-file -e nightly-ledger:nightly/reports/2026-11-04.md || fail 'and the cycle'"'"'s report'
git init -q --bare "$work/refusing.git"
printf '#!/bin/sh\necho "remote: Permission to buster14a/buster-performance.git denied" >&2\nexit 1\n' > "$work/refusing.git/hooks/pre-receive"
chmod +x "$work/refusing.git/hooks/pre-receive"
NIGHTLY_PUBLICATION_TOKEN=token NIGHTLY_PERFORMANCE_REMOTE="$work/refusing.git" GITHUB_RUN_ID=9111 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') \
    run_coordinator "$work/refused" prepare dry-run
expect_match "$work/coordinate.log" '::warning title=Nightly ledger::the ledger could not be published' 'a refused publication warns'
expect_match "$work/coordinate.log" 'Permission to buster14a/buster-performance.git denied' 'and says why'
expect_line "$GITHUB_OUTPUT" "snapshot=$head" 'and the step still hands over its snapshot'
expect_line "$GITHUB_OUTPUT" "pending=$none_pending" 'its pending dispatches'
expect_line "$GITHUB_OUTPUT" 'window=closed' 'and its window'
git -C "$work/refusing.git" rev-parse -q --verify refs/heads/nightly-ledger > /dev/null && fail 'nothing reached the refusing remote'

# An earlier schedule's step on time, at 16:10 (there for hours-late scheduled starts): it harvests and reports, and
# pins, dispatches and waits for nothing, in run mode too.
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9150 NIGHTLY_NOW=$(madrid '2026-11-03 16:10') run_coordinator "$work/early" prepare run
expect_no_match "$fixtures/calls.log" '^workflow run|-X POST|-X DELETE' 'a step before 22:00 writes nothing'
expect_no_match "$work/early/ledger.tsv" $'^cycle\t2026-11-04\t' 'nor pins the cycle'
expect_match "$work/early/report.md" 'No plan: it is before 22:00 Europe/Madrid' 'and its report says why'
expect_line "$GITHUB_OUTPUT" 'window=closed' 'and it does not wait for the window'
expect_line "$GITHUB_OUTPUT" 'snapshot=' 'with no snapshot to hand over'

# Run mode before the window: branches and dispatches of the hosted preparations, and the wait for the window.
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') run_coordinator "$work/prepare" prepare run
expect_match "$fixtures/calls.log" "^api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$head -f sha=$head$" 'the coordinator makes the nightly branch'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml --repo buster14a/buster --ref nightly/$head -f purpose=snapshot -f hardware=defer -f snapshot=$head -f request=20261104\.9200\.1\.defer$" 'and dispatches the snapshot'"'"'s daily hosted run'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'and the oldest state'"'"'s'
expect_line "$GITHUB_OUTPUT" 'window=reachable' 'and waits for the window, under six hours away'
test "$(grep -E -c '^workflow run' "$fixtures/calls.log")" -eq 3 || fail 'the snapshot and the two states without hosted rows are prepared'
prepared=$(handed)
[[ "$prepared" == "buster-nightly-pending-1 3 20261104.9200.1.defer,snapshot,defer,$head,$(madrid '2026-11-03 22:10') "* ]] ||
    fail "the prepare job hands over its three dispatches: $prepared"

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
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-04 04:10') NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$prepared run_coordinator "$work/window" window run
expect_match "$work/window/ledger.tsv" "^run	900	1	$head	workflow_dispatch	snapshot	20261104\.9200\.1\.defer	" 'the snapshot'"'"'s hosted run is harvested'
expect_match "$work/window/ledger.tsv" "^run	900	.*	ok	skip	full	blocked-hosted	" 'its sanitizer row failed, but its plain rows qualified it'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$head -f purpose=snapshot -f hardware=admit .*-f request=20261104\.9200\.100[0-9][0-9]\.admit$" 'the comprehensive run is admitted first'
test "$(grep -E -c '^workflow run .*hardware=admit' "$fixtures/calls.log")" -eq 1 || fail 'one hardware run at a time'
expect_line "$work/window/ledger.tsv" "cycle	2026-11-04	$head	9200	$(madrid '2026-11-03 22:10')" 'the window keeps the prepare job'"'"'s pin'
expect_line "$GITHUB_OUTPUT" 'continue=closed' 'a window job with time left hands over nothing'
# The night the clocks go back, the window is 7 hours: longer than one job's 345 minutes, within two.
autumn=$("$work/nightly" window-times --now 1792886400)
autumn_length=$(( $(awk -F '\t' '$1 == "deadline" { print $2 }' <<< "$autumn") - $(awk -F '\t' '$1 == "start" { print $2 }' <<< "$autumn") ))
test "$autumn_length" -eq 25200 || fail "the fall-back night's window is 7 hours ($autumn_length s)"
test "$autumn_length" -gt 20700 && test "$autumn_length" -le $((2 * 20700)) || fail 'it needs one continuation job, and one is enough'
# A window job out of time (345 minutes since its start) while it still waits inside the window hands over: the pin and
# its pending dispatches, as the prepare job does, and the continuation job, with requests from 20001, repeats none.
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-04 04:10') NIGHTLY_STARTED=$(( $(madrid '2026-11-04 04:10') - 20700 )) NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$prepared run_coordinator "$work/window-out" window run
expect_line "$GITHUB_OUTPUT" 'continue=reachable' 'a window job out of time hands over to its continuation'
expect_line "$GITHUB_OUTPUT" "snapshot=$head" 'with the pin'
expect_match "$work/coordinate.log" 'the continuation job takes over' 'and says so'
continued=$(handed)
pending_count=$(awk '{ print $2 }' <<< "$continued")
[[ "$continued" == "buster-nightly-pending-1 "* ]] && test "$pending_count" -ge 1 || fail "and its pending dispatches: $continued"
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*-f purpose=snapshot -f hardware=admit .*-f request=20261104\.9200\.100[0-9][0-9]\.admit$" 'it admitted the comprehensive run before handing over'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-04 04:11') NIGHTLY_SEQUENCE=20001 NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$continued run_coordinator "$work/window-continued" window run
expect_no_match "$fixtures/calls.log" '^workflow run .*hardware=admit' 'the continuation repeats no dispatch the runs don'"'"'t show yet'
expect_no_match "$fixtures/calls.log" 'request=20261104\.9200\.100' 'and makes none with the window job'"'"'s request numbers'
# Out of time after the window closed: nothing to hand over.
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-04 08:10') NIGHTLY_STARTED=$(( $(madrid '2026-11-04 08:10') - 20700 )) NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$none_pending run_coordinator "$work/window-late" window run
expect_line "$GITHUB_OUTPUT" 'continue=closed' 'after 08:00 there is nothing to continue'
# A manual run's override admits outside the window, through the coordinator and its dispatch, and says so.
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9300 NIGHTLY_NOW=$(madrid '2026-11-04 13:00') NIGHTLY_OVERRIDE='validating the coordinator' run_coordinator "$work/override-prepare" prepare run
expect_line "$GITHUB_OUTPUT" 'window=reachable' 'an override makes the window job run at once'
prepared=$(handed)
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9300 NIGHTLY_NOW=$(madrid '2026-11-04 13:00') NIGHTLY_OVERRIDE='validating the coordinator' NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$prepared run_coordinator "$work/override-window" window run
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$head -f purpose=snapshot -f hardware=admit .* -f override=validating the coordinator$" 'the override admits the comprehensive run and passes its reason on'
expect_match "$work/override-window/report.md" 'Override.*validating the coordinator' 'and the report says so'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9300 NIGHTLY_NOW=$(madrid '2026-11-04 13:00') NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$none_pending run_coordinator "$work/no-override" window run
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
GITHUB_RUN_ID=9500 NIGHTLY_NOW=$(madrid '2026-11-04 04:20') NIGHTLY_ITERATIONS=3 NIGHTLY_SLEEP=0 NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$none_pending run_coordinator "$work/failed-poll" window run
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
# A branch the workflow token may not create (GitHub refused those of merges whose pull request's head lacked main's
# workflow changes): it warns, dispatches nothing for that state, and the job plans again at once, without the wait
# (a `sleep` on PATH logs any), giving the slot to the next state.
mkdir -p "$work/sleeping"
printf '#!/bin/sh\necho "sleep $*" >> "$FIXTURES/calls.log"\n' > "$work/sleeping/sleep"
chmod +x "$work/sleeping/sleep"
: > "$fixtures/refuse-$epoch"
: > "$fixtures/calls.log"
PATH="$work/sleeping:$PATH" GITHUB_RUN_ID=9650 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=300 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/refused-branch" prepare run
expect_match "$work/coordinate.log" "::warning title=Nightly branch::GitHub refused the workflow token nightly/$epoch" 'a refused branch warns'
expect_no_match "$fixtures/calls.log" "^workflow run .*--ref nightly/$epoch " 'and nothing is dispatched for its state'
test "$(grep -c "^api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$epoch " "$fixtures/calls.log")" -eq 1 || fail 'its branch is tried once a job'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$merge -f purpose=backlog -f hardware=defer .*request=20261104\.9650\.3\.defer$" 'the next step prepares the next state in its place'
expect_no_match "$fixtures/calls.log" '^sleep ' 'and plans it at once, without waiting'
expect_match "$work/refused-branch/report.md" 'Branch refused' 'and the report names the skipped state'
rm -f "$fixtures/refuse-$epoch" "$fixtures"/count-*
# Any other failure to create a branch (here a 502) is not a refusal: the state isn't skipped, the next step, after the
# usual wait, tries again, and dispatches it.
: > "$fixtures/flaky-$epoch"
: > "$fixtures/calls.log"
PATH="$work/sleeping:$PATH" GITHUB_RUN_ID=9660 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=300 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/flaky-branch" prepare run
expect_match "$work/coordinate.log" "::warning title=Nightly branch::nightly/$epoch could not be created \\(gh: Server Error \\(HTTP 502\\)\\); a later step tries again" 'a transient failure warns, with its reason'
test "$(grep -c "^api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$epoch " "$fixtures/calls.log")" -eq 2 || fail 'and its branch is tried again'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'and its state is dispatched once it is made'
expect_match "$fixtures/calls.log" '^sleep 300$' 'after the usual wait'
expect_no_match "$work/flaky-branch/report.md" 'Branch refused' 'and it is not reported as refused'
rm -f "$fixtures/flaky-$epoch" "$fixtures"/count-*
# A nightly branch that already points at another commit is never dispatched, and the state is skipped for the job
# (it stays wrong until a person fixes it), at once: the next state is prepared without the wait.
printf '{"object":{"sha":"%s"}}\n' "$merge" > "$fixtures/ref-$epoch.json"
: > "$fixtures/calls.log"
PATH="$work/sleeping:$PATH" GITHUB_RUN_ID=9665 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=300 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/mispointed-branch" prepare run
expect_match "$work/coordinate.log" "::error title=Nightly branch::nightly/$epoch points at $merge; refusing to dispatch it" 'a branch at another commit is an error'
expect_no_match "$fixtures/calls.log" "^workflow run .*--ref nightly/$epoch |-X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$epoch " 'and is neither dispatched nor made again'
test "$(grep -c "^api repos/buster14a/buster/git/ref/heads/nightly/$epoch " "$fixtures/calls.log")" -eq 1 || fail 'it is looked up once a job'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$merge -f purpose=backlog -f hardware=defer .*request=20261104\.9665\.3\.defer$" 'the next state is prepared in its place'
expect_no_match "$fixtures/calls.log" '^sleep ' 'at once'
rm -f "$fixtures/ref-$epoch.json" "$fixtures"/count-*
# With the nightly-ledger environment's branch token, the branches are made and deleted with it, and a state the
# workflow token was refused is dispatched; every other call keeps the workflow's token.
: > "$fixtures/refuse-$epoch"
: > "$fixtures/calls.log"
: > "$fixtures/tokens.log"
NIGHTLY_BRANCH_TOKEN=branch-token GITHUB_RUN_ID=9668 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=1 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/branch-token" prepare run
expect_match "$fixtures/tokens.log" "^branch-token api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$epoch " 'the branch token makes the branches'
expect_no_match "$fixtures/tokens.log" '^branch-token (workflow|api repos|api --paginate|api -X POST repos/buster14a/buster/actions)' 'and nothing else'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'so the state the workflow token was refused is dispatched'
expect_no_match "$work/coordinate.log" "Nightly branch::" 'without a warning'
rm -f "$fixtures/refuse-$epoch" "$fixtures"/count-*
# An expired branch token is rejected once, warned about once, and the job makes its branches with the workflow
# token, as without one: a branch the workflow token may have is made and dispatched, one it is refused is skipped.
: > "$fixtures/refuse-$merge"
: > "$fixtures/calls.log"
: > "$fixtures/tokens.log"
PATH="$work/sleeping:$PATH" NIGHTLY_BRANCH_TOKEN=expired-token GITHUB_RUN_ID=9669 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=300 NIGHTLY_PREPARATIONS=3 run_coordinator "$work/expired-token" prepare run
test "$(grep -c '^expired-token ' "$fixtures/tokens.log")" -eq 1 || { cat "$fixtures/tokens.log" >&2; fail 'an expired branch token is tried once a job'; }
test "$(grep -c '::warning title=Nightly branch token::GitHub rejected BUSTER_NIGHTLY_BRANCH_TOKEN (gh: Bad credentials (HTTP 401))' "$work/coordinate.log")" -eq 1 || fail 'and warned about once'
expect_match "$fixtures/tokens.log" "^workflow-token api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$epoch " 'the workflow token makes the branches instead'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'a branch it may have is dispatched'
expect_match "$work/coordinate.log" "::warning title=Nightly branch::GitHub refused the workflow token nightly/$merge" 'one it is refused is skipped, as without a branch token'
rm -f "$fixtures/refuse-$merge" "$fixtures"/count-*
# A branch someone else deleted before the prune: a 404 to a valid branch token, which keeps it, so the next branch
# the workflow token would be refused is still made with it and dispatched. A token without access to the repository
# (a 404 there too) is rejected, as an expired one is.
gone=1111111111111111111111111111111111111111
printf '[{"ref":"refs/heads/nightly/%s"}]\n' "$gone" > "$fixtures/refs.json"
: > "$fixtures/gone-$gone"
: > "$fixtures/refuse-$epoch"
: > "$fixtures/calls.log"
: > "$fixtures/tokens.log"
NIGHTLY_BRANCH_TOKEN=branch-token GITHUB_RUN_ID=9671 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=0 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/pruned-404" prepare run
expect_match "$fixtures/tokens.log" "^branch-token api -X DELETE repos/buster14a/buster/git/refs/heads/nightly/$gone$" 'the prune of a branch already gone is a 404'
expect_no_match "$work/coordinate.log" 'Nightly branch token::' 'which doesn'"'"'t reject a valid branch token'
expect_match "$fixtures/tokens.log" "^branch-token api -X POST repos/buster14a/buster/git/refs -f ref=refs/heads/nightly/$epoch " 'so a later branch is still made with it'
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'and its state dispatched'
: > "$fixtures/calls.log"
: > "$fixtures/tokens.log"
NIGHTLY_BRANCH_TOKEN=outsider-token GITHUB_RUN_ID=9672 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=2 NIGHTLY_SLEEP=0 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/outsider-token" prepare run
test "$(grep -c '::warning title=Nightly branch token::GitHub rejected BUSTER_NIGHTLY_BRANCH_TOKEN (gh: Not Found (HTTP 404))' "$work/coordinate.log")" -eq 1 || fail 'a branch token without access to the repository is rejected once'
expect_match "$fixtures/tokens.log" "^workflow-token api -X DELETE repos/buster14a/buster/git/refs/heads/nightly/$gone$" 'and the workflow token takes over'
rm -f "$fixtures/refs.json" "$fixtures/gone-$gone" "$fixtures/refuse-$epoch" "$fixtures"/count-*
# A branch another step made meanwhile (the POST fails, the branch is then there at the commit) is used as it is.
printf '{"object":{"sha":"%s"}}\n' "$epoch" > "$fixtures/ref-$epoch.json.2"
: > "$fixtures/refuse-$epoch"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9670 NIGHTLY_NOW=$(madrid '2026-11-03 22:10') NIGHTLY_ITERATIONS=1 NIGHTLY_PREPARATIONS=1 run_coordinator "$work/raced-branch" prepare run
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$epoch -f purpose=backlog -f hardware=defer" 'a branch made meanwhile is dispatched'
expect_no_match "$work/coordinate.log" "Nightly branch::" 'without a warning'
rm -f "$fixtures/refuse-$epoch" "$fixtures/ref-$epoch.json.2" "$fixtures"/count-*
# The handoff from the prepare job to the window job: a prepare step inside the window admits no hardware, and the
# window job, handed the prepare job's pending dispatches as its output, repeats none that aren't visible yet, whether
# or not the ledger's artifact can be read.
rm -f "$fixtures"/count-* "$fixtures/runs-push.json"
echo '{"workflow_runs":[]}' > "$fixtures/runs-workflow_dispatch.json"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9700 NIGHTLY_NOW=$(madrid '2026-11-04 04:05') run_coordinator "$work/handoff-prepare" prepare run
expect_no_match "$fixtures/calls.log" 'hardware=admit' 'a prepare step inside the window admits no hardware'
expect_match "$fixtures/calls.log" "^workflow run .*-f purpose=snapshot -f hardware=defer .*request=20261104\\.9700\\.1\\.defer$" 'it prepares'
expect_line "$GITHUB_OUTPUT" 'window=reachable' 'and hands over to the window job'
test -s "$work/handoff-prepare/pending.tsv" || fail 'the prepare job keeps its pending dispatches'
handoff=$(handed)
[[ "$handoff" == "buster-nightly-pending-1 3 20261104.9700.1.defer,snapshot,defer,$head,"* ]] || fail "and hands them over: $handoff"
(cd "$work/handoff-prepare" && rm -f "$fixtures/artifact-9701.zip" && zip -q "$fixtures/artifact-9701.zip" ledger.tsv report.md pending.tsv)
echo '{"artifacts":[{"id":9701,"name":"nightly-ledger-prepare-1","expired":false}]}' > "$fixtures/artifacts-9700.json"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9700 NIGHTLY_NOW=$(madrid '2026-11-04 04:06') NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$handoff run_coordinator "$work/handoff-window" window run
expect_no_match "$fixtures/calls.log" '^workflow run' 'the window job repeats none of the prepare job'"'"'s dispatches the API doesn'"'"'t show yet'
cmp "$work/handoff-prepare/pending.tsv" "$work/handoff-window/pending.tsv" || fail 'the window job starts from the prepare job'"'"'s pending dispatches'
# The artifact that can't be served, or whose listing fails: the dispatch, still hidden, isn't repeated either.
mv "$fixtures/artifact-9701.zip" "$fixtures/artifact-9701.zip.away"
for unreadable in served listed; do
    rm -f "$fixtures"/count-*
    if [ "$unreadable" = listed ]; then : > "$fixtures/artifacts-9700.json.1.fail"; : > "$fixtures/artifacts-9700.json.2.fail"; fi
    : > "$fixtures/calls.log"
    GITHUB_RUN_ID=9700 NIGHTLY_NOW=$(madrid '2026-11-04 04:06') NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$handoff run_coordinator "$work/handoff-$unreadable" window run
    expect_no_match "$fixtures/calls.log" '^workflow run' "with the ledger's artifact not $unreadable, the hidden dispatch isn't repeated"
    cmp "$work/handoff-prepare/pending.tsv" "$work/handoff-$unreadable/pending.tsv" || fail "and the pending dispatches are the prepare job's ($unreadable)"
done
# Without a valid handoff (none, another format, a count that doesn't match, a malformed dispatch), the window job plans
# nothing and fails, though a valid empty list would have planned: an absent list is not an empty one.
for broken in '' 'buster-nightly-pending-2 0' "buster-nightly-pending-1 2 ${handoff#buster-nightly-pending-1 3 }" \
    "buster-nightly-pending-1 1 20261104.9700.1.defer,snapshot,defer,$head"; do
    rm -f "$fixtures"/count-*
    : > "$fixtures/calls.log"
    : > "$GITHUB_OUTPUT"
    if (cd "$root" && GITHUB_RUN_ID=9700 NIGHTLY_NOW=$(madrid '2026-11-04 04:06') NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$broken \
        bash tools/ci/nightly.sh coordinate "$work/handoff-broken" --phase window --mode run) > "$work/coordinate.log" 2>&1; then
        fail "a window job without a valid handoff ($broken) succeeded"
    fi
    expect_match "$work/coordinate.log" 'pending dispatches did not reach this job, so nothing is planned' "and says why ($broken)"
    expect_no_match "$fixtures/calls.log" '^workflow run|-X POST|-X DELETE' "and writes nothing ($broken)"
done
rm -f "$fixtures"/count-* "$fixtures/artifacts-9700.json.1.fail" "$fixtures/artifacts-9700.json.2.fail"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9700 NIGHTLY_NOW=$(madrid '2026-11-04 04:06') NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$none_pending run_coordinator "$work/handoff-empty" window run
expect_match "$fixtures/calls.log" "^workflow run .*-f purpose=snapshot -f hardware=defer " 'a valid empty list plans: the hidden dispatch, not handed over, is made again'
mv "$fixtures/artifact-9701.zip.away" "$fixtures/artifact-9701.zip"
rm -f "$fixtures/artifacts-9700.json"

# Historical requests do not consume the 64 active pending slots. Retain unseen admissions and preparations across
# the window-to-continuation handoff, including a timestamp ahead of this job's clock. The 600-second grace is exact.
pending_now=$(madrid '2026-11-04 04:06')
: > "$work/pending-history.tsv"
for i in $(seq 1 70); do
    printf 'pending\t20261104.9720.%s.admit\tbacklog\tadmit\t%s\t%s\n' "$i" "$epoch" "$((pending_now - 600))" >> "$work/pending-history.tsv"
done
{
    printf 'pending\t20261104.9720.101.defer\tsnapshot\tdefer\t%s\t%s\n' "$head" "$pending_now"
    printf 'pending\t20261104.9720.102.admit\tbacklog\tadmit\t%s\t%s\n' "$epoch" "$((pending_now - 599))"
    printf 'pending\t20261104.9720.103.defer\tbacklog\tdefer\t%s\t%s\n' "$merge" "$((pending_now + 60))"
} > "$work/pending-active.tsv"
cat "$work/pending-history.tsv" "$work/pending-active.tsv" > "$work/pending-long.tsv"
long_handoff="buster-nightly-pending-1 73$(awk -F '\t' '{ printf " %s,%s,%s,%s,%s", $2, $3, $4, $5, $6 }' "$work/pending-long.tsv")"
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9720 NIGHTLY_NOW=$pending_now NIGHTLY_STARTED=$((pending_now - 20700)) NIGHTLY_SNAPSHOT=$head NIGHTLY_PENDING=$long_handoff run_coordinator "$work/handoff-compacted" window run
expect_no_match "$fixtures/calls.log" '^workflow run' 'historical pending rows cannot hide a still-unseen dispatch'
cmp "$work/pending-active.tsv" "$work/handoff-compacted/pending.tsv" || fail 'the handoff keeps exactly the three unseen active requests'
expect_match "$GITHUB_OUTPUT" '^pending=buster-nightly-pending-1 3 ' 'the continuation receives the compacted handoff'
expect_line "$GITHUB_OUTPUT" 'continue=reachable' 'the compacted window job can hand over normally'

# Recent requests visible in the facts also leave the pending file. A queued admission must still block the planner
# after compaction, through its run facts rather than its pending row. Exercise this with both instrumentations.
: > "$work/pending-visible.tsv"
: > "$work/pending-facts.tsv"
for i in $(seq 1 70); do
    flavor=defer status=completed conclusion=success
    if [ "$i" -eq 70 ]; then flavor=admit status=queued conclusion=-; fi
    request="20261104.9730.$i.$flavor"
    printf 'pending\t%s\tbacklog\t%s\t%s\t%s\n' "$request" "$flavor" "$epoch" "$pending_now" >> "$work/pending-visible.tsv"
    printf 'run\t%s\t1\tworkflow_dispatch\t%s\t%s\t%s\t.github/workflows/9700x.yml\tnightly/%s\t%s\t%s\t1421805693\tNightly backlog (%s) of %s [%s]\n' \
        "$i" "$status" "$conclusion" "$epoch" "$epoch" "$pending_now" "$pending_now" "$flavor" "$epoch" "$request" >> "$work/pending-facts.tsv"
done
grep -v $'\tadmit\t' "$work/pending-active.tsv" > "$work/pending-unseen.tsv"
plan_pending()
{
    local binary=$1 facts=$2 pending=$3
    shift 3
    "$binary" plan --states "$work/handoff-window/states.tsv" --ledger "$work/handoff-window/ledger.tsv" \
        --facts "$facts" --now "$pending_now" --main "$head" --mode run --coordinator 9740 \
        --pending "$pending" --report "$work/pending-report.md" "$@"
}
for binary in "$work/nightly" "$work/nightly-sanitize"; do
    cat "$work/pending-visible.tsv" "$work/pending-unseen.tsv" > "$work/pending-input.tsv"
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 plan_pending "$binary" "$work/pending-facts.tsv" "$work/pending-input.tsv" > "$work/pending-plan.tsv"
    cmp "$work/pending-unseen.tsv" "$work/pending-input.tsv" || fail 'visible requests leave only the unseen preparations pending'
    expect_no_match "$work/pending-plan.tsv" $'^dispatch\t[^\t]+\tadmit\t' 'a visible queued admission still prevents a second hardware dispatch'
    expect_line "$work/pending-plan.tsv" $'wait\ta hardware run is in flight' 'the visible queued facts retain the hardware guard'
done

# Invalid historical data, another invalid input, an invalid option, or more than 64 still-active requests rejects
# the whole plan without replacing the original handoff. Stale rows must not hide an active overflow.
: > "$work/pending-empty-facts.tsv"
printf 'not-a-commit\n' > "$work/pending-bad-refs.tsv"
for broken in malformed refs option overflow; do
    cat "$work/pending-history.tsv" "$work/pending-active.tsv" > "$work/pending-input.tsv"
    arguments=()
    case "$broken" in
        malformed)
            printf 'pending\t20261104.9740.1.admit\tbacklog\tadmit\tnot-a-sha\t%s\n' "$((pending_now - 600))" >> "$work/pending-input.tsv" ;;
        refs) arguments=(--refs "$work/pending-bad-refs.tsv") ;;
        option) arguments=(--attempts invalid) ;;
        overflow)
            cat "$work/pending-history.tsv" > "$work/pending-input.tsv"
            for i in $(seq 1 65); do
                printf 'pending\t20261104.9740.%s.admit\tbacklog\tadmit\t%s\t%s\n' "$i" "$epoch" "$pending_now" >> "$work/pending-input.tsv"
            done ;;
    esac
    cp "$work/pending-input.tsv" "$work/pending-before.tsv"
    if plan_pending "$work/nightly" "$work/pending-empty-facts.tsv" "$work/pending-input.tsv" "${arguments[@]}" > "$work/pending-plan.tsv" 2> "$work/pending-error.log"; then
        fail "invalid pending plan ($broken) succeeded"
    fi
    cmp "$work/pending-before.tsv" "$work/pending-input.tsv" || fail "invalid pending plan ($broken) changed the handoff"
    test ! -s "$work/pending-plan.tsv" || fail "invalid pending plan ($broken) emitted actions"
done

# Main moves on between the prepare job and the window job: the window job's dispatches still name the cycle's pinned
# snapshot, which the worker checks ancestry against and records, not main's new head.
rm -f "$fixtures"/count-*
cat > "$fixtures/runs-workflow_dispatch.json" <<JSON
{"workflow_runs":[{"id":900,"run_attempt":1,"event":"workflow_dispatch","status":"completed","conclusion":"success","head_sha":"$head","path":".github/workflows/9700x.yml","head_branch":"nightly/$head","created_at":"2026-11-03T21:15:00Z","updated_at":"2026-11-03T22:30:00Z","head_repository":{"id":1421805693},"display_title":"Nightly snapshot (defer) of $head [20261104.9200.1.defer]"}]}
JSON
echo more > "$history/src/d.c"
git -C "$history" add -A
commit_at 1793700700 commit -q -m later
later=$(git -C "$history" rev-parse HEAD)
git -C "$history" -c push.negotiate=false push -q "$remote" main
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9200 NIGHTLY_NOW=$(madrid '2026-11-04 04:10') NIGHTLY_PENDING=$none_pending run_coordinator "$work/moved" window run
expect_match "$fixtures/calls.log" "^workflow run 9700x.yml .*--ref nightly/$head -f purpose=snapshot -f hardware=admit -f snapshot=$head -f request=" 'after main moves on, the worker gets the cycle'"'"'s pinned snapshot'
expect_no_match "$fixtures/calls.log" "snapshot=$later" 'and never main'"'"'s new head'

# A mixed old/new backlog after the daily snapshot completed: early and late steps skip old workers, while an inside
# step admits the oldest. A new job rebuilds the same windows from Git, independently of its pending handoff.
export FIXTURES="$work/worker-fixtures"
fixtures=$FIXTURES
mkdir -p "$fixtures" "$work/worker-ledger"
{
    printf '# buster-nightly-ledger 1\ncycle\t2026-11-04\t%s\t9800\t%s\n' "$later" "$(madrid '2026-11-03 22:10')"
    for state in "$epoch" "$merge" "$head"; do
        case "$state" in "$epoch") run=9811 ;; "$merge") run=9812 ;; "$head") run=9813 ;; esac
        printf 'run\t%s\t1\t%s\tpush\tpush\t-\t1793701000\t1793702000\tok\tskip\ttargeted\tdeferred\t-\t-\t-\n' "$run" "$state"
    done
    printf 'run\t9814\t1\t%s\tworkflow_dispatch\tsnapshot\t20261104.9800.1.defer\t1793740200\t1793743800\tok\tskip\tfull\tdeferred\t-\t-\t-\n' "$later"
    printf 'run\t9815\t1\t%s\tworkflow_dispatch\tsnapshot\t20261104.9800.2.admit\t1793754000\t1793754300\tok\tadmit\tfull\tcomplete\t-\t-\t-\n' "$later"
} > "$work/worker-ledger/ledger.tsv"
(cd "$work/worker-ledger" && zip -q "$fixtures/artifact-98001.zip" ledger.tsv)
printf '{"artifacts":[{"id":98001,"name":"nightly-ledger-prepare-1","expired":false}]}\n' > "$fixtures/artifacts-9800.json"
: > "$fixtures/calls.log"
early=$(madrid '2026-11-04 02:10')
GITHUB_RUN_ID=9800 NIGHTLY_NOW=$early NIGHTLY_STARTED=$((early - 20700)) NIGHTLY_PENDING=$none_pending run_coordinator "$work/worker-early" window run
expect_match "$fixtures/calls.log" "^workflow run .*--ref nightly/$head -f purpose=backlog -f hardware=admit" 'a later worker can use the added early hours'
expect_no_match "$fixtures/calls.log" "--ref nightly/($epoch|$merge) .*hardware=admit" 'the old 04:00 workers are never admitted early'
expect_match "$work/worker-early/report.md" 'worker window 04:00-07:00 Europe/Madrid' 'the report explains the old workers still pending'
expect_line "$GITHUB_OUTPUT" 'continue=reachable' 'a job that hands over does so normally with worker constraints'
worker_handoff=$(handed)
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9800 NIGHTLY_NOW=$((early + 60)) NIGHTLY_SEQUENCE=20001 NIGHTLY_PENDING=$worker_handoff run_coordinator "$work/worker-continued" window run
expect_no_match "$fixtures/calls.log" '^workflow run .*hardware=admit' 'the continuation duplicates no pending hardware dispatch'
cmp "$work/worker-early/states.tsv" "$work/worker-continued/states.tsv" || fail 'the continuation rebuilds exactly the same immutable worker windows'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9800 NIGHTLY_NOW=$(madrid '2026-11-04 04:00') NIGHTLY_PENDING=$none_pending run_coordinator "$work/worker-open" window run
expect_match "$fixtures/calls.log" "^workflow run .*--ref nightly/$epoch -f purpose=backlog -f hardware=admit" 'at 04:00 the oldest worker becomes eligible'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9800 NIGHTLY_NOW=$(madrid '2026-11-04 07:10') NIGHTLY_PENDING=$none_pending run_coordinator "$work/worker-late" window run
expect_match "$fixtures/calls.log" "^workflow run .*--ref nightly/$head -f purpose=backlog -f hardware=admit" 'a later worker can use the added late hour'
expect_no_match "$fixtures/calls.log" "--ref nightly/($epoch|$merge) .*hardware=admit" 'the old workers are never admitted after their cutoff'
expect_match "$work/worker-late/ledger.tsv" "^run.*$epoch.*targeted.deferred" 'skipping a closed worker does not mark its coverage complete'
: > "$fixtures/calls.log"
GITHUB_RUN_ID=9800 NIGHTLY_NOW=$(madrid '2026-11-04 07:36') NIGHTLY_PENDING=$none_pending run_coordinator "$work/worker-cutoff" window run
expect_no_match "$fixtures/calls.log" '^workflow run .*hardware=admit' 'no new worker bypasses the global bound and 08:00 cutoff'

printf 'nightly glue: evidence, merge-queue coverage and stale candidates, records, first-parent states, the ledger, worker windows, the window step, dry and real coordination, overrides, failed polls, refilled preparations, refused, failed and raced branches, pending compaction, the prepare-to-window handoff and a pin that main moved past checked\n'
