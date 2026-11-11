#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# The setup re-run's tests (#2276; docs/commands/ci.md, "Setup failures re-run once"), without network access:
# tools/ci/rerun.c's self-test, also under ASan and UBSan, with its allowlist against this tree's workflows and against
# mutants of them that must fail it; its decisions on runs recorded from the API (tests/ci/rerun/cases.tsv), whose
# reports and Markdown must match NAME.expected and NAME.md; and tools/ci/rerun.sh against a fake `gh` serving them,
# which must request a re-run and post its note exactly where the rule allows. `--update` rewrites NAME.expected and
# NAME.md from the classifier; read their difference before keeping it.
set -euo pipefail
update=0
if [ "${1:-}" = --update ]; then
    update=1
elif [ "$#" -ne 0 ]; then
    echo 'usage: bash tests/ci/rerun.sh [--update]' >&2
    exit 2
fi
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fixtures="$root/tests/ci/rerun"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$root"
failures=0
fail()
{
    printf 'tests/ci/rerun.sh: %s\n' "$*" >&2
    failures=$((failures + 1))
}

cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/rerun.c -o "$work/rerun"
cc -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tools/ci/rerun.c -o "$work/rerun-sanitize"
"$work/rerun" --self-test --root .
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/rerun-sanitize" --self-test --root .

# Mutants of the workflows that the allowlist's policy must refuse.
mutant()
{
    local what=$1 file=$2 expression=$3
    rm -rf "$work/mutant"
    mkdir -p "$work/mutant/.github"
    cp -R .github/workflows "$work/mutant/.github/"
    sed -i -e "$expression" "$work/mutant/.github/workflows/$file"
    if cmp -s ".github/workflows/$file" "$work/mutant/.github/workflows/$file"; then
        fail "the mutant doesn't change $file: $what"
    elif "$work/rerun" --self-test --root "$work/mutant" > /dev/null 2>&1; then
        fail "the policy accepts a mutant: $what"
    fi
}
mutant 'a renamed setup step' hosted-correctness.yml 's/name: Install official Clang 23.1.4 and test tools$/name: Install official Clang 23.1.4 and the test tools/'
mutant 'a renamed fetch' hosted-correctness.yml 's/name: Fetch generic baseline startup objects and runtime for emulation$/name: Fetch the baseline runtime/'
mutant 'a renamed preparation' prepare-hosted-tools.yml 's/name: Acquire missing inputs once and verify the complete set$/name: Acquire the inputs/'
mutant 'a renamed row build' hosted-correctness.yml "s/name: Build the compiler once for the row's shards$/name: Build the row's compiler/"
mutant 'a renamed test step' hosted-correctness.yml 's/name: Run correctness tests within the hosted CPU budget$/name: Run the correctness tests/'
mutant 'a renamed caller' 9700x.yml '1s/^name: .*/name: Tests and benchmarks/'
mutant 'another caller' build.yml '0,/^jobs:$/s//jobs:\n  rows:\n    uses: .\/.github\/workflows\/hosted-correctness.yml/'
mutant 'a re-run of every attempt' rerun.yml '/run_attempt == 1/d'
mutant 'a re-run of a success' rerun.yml "s/conclusion == 'failure'/conclusion != 'success'/"
mutant 'a re-run of forks' rerun.yml '/head_repository.full_name/d'
mutant 'a trigger on requests' rerun.yml 's/types: \[completed\]/types: [requested, completed]/'
mutant 'a watched workflow without rows' rerun.yml 's/workflows: \["Tests and Zen 5 benchmarks"\]/workflows: ["Tests and Zen 5 benchmarks", "Build"]/'

# The recorded runs: each decision as cases.tsv says, and the report and Markdown as recorded.
listed=0
while IFS=$'\t' read -r name run attempt event decision what; do
    case "$name" in '#'* | '') continue ;; esac
    listed=$((listed + 1))
    input="$fixtures/$name.json"
    [ -f "$input" ] || { fail "$name: no $input"; continue; }
    [[ "$run" =~ ^[0-9]+$ && "$attempt" =~ ^[0-9]+$ && ( "$event" = push || "$event" = pull_request ) && -n "$what" ]] || fail "$name: a malformed line in cases.tsv"
    "$work/rerun-sanitize" classify --markdown "$work/$name.md" "$input" > "$work/$name.expected"
    got=$(awk -F '\t' '$1 == "decision" { print $2 }' "$work/$name.expected")
    [ "$got" = "$decision" ] || fail "$name: decided $got, not $decision"
    if [ "$update" = 1 ]; then
        cp "$work/$name.expected" "$fixtures/$name.expected"
        cp "$work/$name.md" "$fixtures/$name.md"
    else
        diff -u "$fixtures/$name.expected" "$work/$name.expected" || fail "$name: the report differs from $name.expected"
        diff -u "$fixtures/$name.md" "$work/$name.md" || fail "$name: the Markdown differs from $name.md"
    fi
done < "$fixtures/cases.tsv"
fixture_count=$(find "$fixtures" -name '*.json' | wc -l)
[ "$listed" = "$fixture_count" ] || fail "cases.tsv lists $listed fixtures, $fixture_count are recorded"

# The glue against a fake `gh` that serves one fixture as run 7's first attempt and logs what it is asked to change.
mkdir -p "$work/bin"
cat > "$work/bin/gh" << 'FAKE'
#!/usr/bin/env bash
set -euo pipefail
[ "$1" = api ]
shift
method=GET
if [ "$1" = -X ]; then
    method=$2
    shift 2
fi
endpoint=$1
shift
case "$method $endpoint" in
    'GET repos/buster14a/buster/actions/runs/7')
        # Each call takes the next of FAKE_ATTEMPTS, then keeps the last.
        calls=$(($(cat "$FAKE_STATE/calls" 2> /dev/null || echo 0) + 1))
        echo "$calls" > "$FAKE_STATE/calls"
        read -r -a attempts <<< "$FAKE_ATTEMPTS"
        index=$((calls <= ${#attempts[@]} ? calls - 1 : ${#attempts[@]} - 1))
        printf '{"id":7,"run_attempt":%s,"status":"completed","conclusion":"%s"}\n' "${attempts[$index]}" "$FAKE_CONCLUSION" ;;
    'GET repos/buster14a/buster/actions/runs/7/attempts/1/jobs?per_page='*)
        page=${endpoint##*page=}
        jq -c 'select(.jobs)' "$FAKE_FIXTURE" | sed -n "${page}p" | grep . ;;
    'GET repos/buster14a/buster/check-runs/'*'/annotations?per_page=100&page=1')
        id=${endpoint#repos/buster14a/buster/check-runs/}
        id=${id%%/*}
        jq -s -c --argjson id "$id" '[.[] | select(.job_id == $id) | {message, annotation_level: "failure"}]' "$FAKE_FIXTURE" ;;
    'GET repos/buster14a/buster/check-runs/'*'/annotations?per_page=100&page='*)
        echo '[]' ;;
    'POST repos/buster14a/buster/actions/runs/7/rerun-failed-jobs')
        echo rerun >> "$FAKE_STATE/log" ;;
    'POST repos/buster14a/buster/issues/'*'/comments' | 'POST repos/buster14a/buster/commits/'*'/comments')
        [ "$1" = -F ] && [[ "$2" == body=@* ]]
        target=${endpoint#repos/buster14a/buster/}
        echo "note ${target%/comments}" >> "$FAKE_STATE/log"
        cp "${2#body=@}" "$FAKE_STATE/note.md" ;;
    *)
        echo "fake gh: unexpected $method $endpoint" >&2
        exit 1 ;;
esac
FAKE
chmod +x "$work/bin/gh"

# glue NAME FIXTURE EVENT ATTEMPTS EXPECTED_LOG [PER_PAGE]: the glue on FIXTURE as run 7, whose state calls answer
# ATTEMPTS in turn; what it asked to change must be EXPECTED_LOG.
glue()
{
    local name=$1 fixture=$2 event=$3 attempts=$4 expected=$5 per_page=${6:-100}
    local state="$work/glue-$name"
    mkdir -p "$state"
    : > "$state/log"
    : > "$state/step-summary"
    if ! FAKE_FIXTURE="$fixtures/$fixture.json" FAKE_STATE="$state" FAKE_ATTEMPTS="$attempts" FAKE_CONCLUSION=failure \
        GH="$work/bin/gh" GITHUB_REPOSITORY=buster14a/buster GITHUB_STEP_SUMMARY="$state/step-summary" \
        GITHUB_SERVER_URL=https://github.com GITHUB_RUN_ID=99 RERUN_TOOL="$work/rerun-sanitize" RERUN_EVENT="$event" \
        RERUN_SHA=0123456789abcdef0123456789abcdef01234567 RERUN_URL=https://github.com/buster14a/buster/actions/runs/7 \
        RERUN_NUMBER=6372 RERUN_PULL_REQUEST="$([ "$event" = pull_request ] && echo 2276)" RERUN_PER_PAGE="$per_page" \
        bash tools/ci/rerun.sh 7 "$state/out" > "$state/stdout" 2> "$state/stderr"; then
        fail "glue $name: tools/ci/rerun.sh failed: $(cat "$state/stderr")"
    fi
    local log
    log=$(tr '\n' ' ' < "$state/log")
    [ "${log% }" = "$expected" ] || fail "glue $name: asked for '${log% }', not '$expected'"
    cmp -s "$state/stdout" "$state/step-summary" || fail "glue $name: the step summary isn't what it printed"
}
glue outage-push outage-push push 1 'rerun note commits/0123456789abcdef0123456789abcdef01234567' 20
grep -qF '### Re-ran the failed jobs of [run 6372](https://github.com/buster14a/buster/actions/runs/7), once' "$work/glue-outage-push/step-summary" || fail 'glue outage-push: no headline'
grep -qF -- "- \`Install official Clang 23.1.4 and test tools\` (5 jobs). Setup: a setup step." "$work/glue-outage-push/note.md" || fail 'glue outage-push: the note lacks the setup step'
grep -qF -- "- \`Require this row's compiler product\` (4 jobs). Follows setup: its row's compiler build stopped in setup." "$work/glue-outage-push/note.md" || fail "glue outage-push: the note lacks the shards' step"
grep -qF '[Re-run hosted setup failures](https://github.com/buster14a/buster/actions/runs/99)' "$work/glue-outage-push/note.md" || fail 'glue outage-push: the note lacks its link'
glue fail-fast-draws fail-fast-draws pull_request 1 'rerun note issues/2276'
grep -qF '(2 jobs). Cancelled: fail-fast cancelled it after another job failed.' "$work/glue-fail-fast-draws/note.md" || fail 'glue fail-fast-draws: the annotations went unread'
glue test-failure-push test-failure-push push 1 ''
grep -qF '### Kept [run 6372]' "$work/glue-test-failure-push/step-summary" || fail 'glue test-failure-push: no headline'
glue timeout timeout push 1 ''
grep -qF '(1 job). Not setup: its timeout stopped it outside setup.' "$work/glue-timeout/step-summary" || fail 'glue timeout: the timeout went unread'
glue second-attempt outage-push push 2 ''
grep -qF 'only a failed first attempt is re-run' "$work/glue-second-attempt/step-summary" || fail 'glue second-attempt: no reason'
glue rerun-meanwhile outage-push push '1 2' '' 20
grep -qF 'someone re-ran it' "$work/glue-rerun-meanwhile/step-summary" || fail 'glue rerun-meanwhile: no reason'

if [ "$failures" -ne 0 ]; then
    printf 'tests/ci/rerun.sh: %s failures\n' "$failures" >&2
    exit 1
fi
printf 'tests/ci/rerun.sh: %s recorded runs, the policy and its mutants, and the glue passed\n' "$listed"
