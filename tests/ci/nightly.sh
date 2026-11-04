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

# Pull requests never reach the 9700X (#1272), drafts and ready ones alike, and read no API.
GITHUB_EVENT_NAME=pull_request GITHUB_SHA=$head run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'scope=plain' 'a pull request runs the plain rows'
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'a pull request keeps off the 9700X'
expect_line "$GITHUB_OUTPUT" 'hardware_reason=pull-request' 'and says it is the policy, not a pass'
GITHUB_EVENT_NAME=pull_request GITHUB_SHA=$head EVIDENCE_DRAFT=false run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'a ready pull request is not admitted either'
expect_no_match "$fixtures/calls.log" 'commits/|actions/' 'a pull request reads no API'
# A merge queue, and any other event, is no supported path: the job fails and nothing runs.
for event in merge_group pull_request_target workflow_run repository_dispatch; do
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

# A push to main runs its own native correctness, needing no pull request's native evidence, and reads no API.
tree=$(git -C "$history" rev-parse "$head^{tree}")
other=$(printf 'other' | sha1sum | cut -c 1-40)
now=$(madrid '2026-11-04 12:00')
export NIGHTLY_NOW=$now
: > "$fixtures/calls.log"
for nightly in on ''; do
    GITHUB_EVENT_NAME=push GITHUB_REF=refs/heads/main GITHUB_SHA=$head EVIDENCE_NIGHTLY=$nightly run_evidence "$history"
    expect_line "$GITHUB_OUTPUT" 'native=run' "a push runs native correctness (nightly mode '$nightly')"
    expect_line "$GITHUB_OUTPUT" 'hardware=admit' "and is admitted (nightly mode '$nightly')"
    expect_line "$GITHUB_OUTPUT" 'native_source=' 'reusing no pull request'"'"'s run'
done
expect_no_match "$fixtures/calls.log" 'pulls|actions/' 'a push looks up no pull request run'
if GITHUB_EVENT_NAME=push GITHUB_REF=refs/heads/feature GITHUB_SHA=$head run_evidence "$history" 2> /dev/null; then
    fail 'a push to another branch was accepted'
fi

# A manual full run reaches the 9700X only for a first-parent state of main: no way around the pull-request exclusion.
GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$head EVIDENCE_PURPOSE=full run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=admit' 'a full run of main'"'"'s head is admitted'
GITHUB_EVENT_NAME=workflow_dispatch GITHUB_SHA=$inside EVIDENCE_PURPOSE=full run_evidence "$history"
expect_line "$GITHUB_OUTPUT" 'hardware=skip' 'a full run of a branch-internal commit is not'
expect_line "$GITHUB_OUTPUT" 'hardware_reason=not-main' 'and says why'
expect_line "$GITHUB_OUTPUT" 'scope=full' 'it still runs every hosted row'

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
expect_line "$GITHUB_OUTPUT" 'window=closed' 'a job that took the lock after 07:00 refuses'
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
printf 'nightly glue: evidence, records, first-parent states, the ledger, the window step, dry and real coordination, overrides, failed polls, refilled preparations, the prepare-to-window handoff and a pin that main moved past checked\n'
