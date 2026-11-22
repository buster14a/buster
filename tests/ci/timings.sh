#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Offline API-boundary fixtures; the C tests own the numeric accounting oracle.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/timings.c -o "$work/timings"
"$work/timings" --self-test
"$work/timings" --accounting-self-test
cc -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tools/ci/timings.c -o "$work/timings-sanitize"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1} UBSAN_OPTIONS=halt_on_error=1 "$work/timings-sanitize" --accounting-self-test
jq -n -f tests/ci/timings-fixtures.jq > "$work/api.json"
mkdir "$work/bin"
cat > "$work/bin/gh" <<'GH'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == api && "$#" == 2 ]]
endpoint=$2
printf '%s\n' "$endpoint" >> "$TIMINGS_FIXTURE_CALLS"
if [[ "$endpoint" =~ workflows/([^/]+)/runs\?per_page=([0-9]+) ]]; then
    workflow=${BASH_REMATCH[1]} limit=${BASH_REMATCH[2]} event=all lower=- upper=-
    if [[ "$endpoint" =~ \&event=([^\&]+) ]]; then event=${BASH_REMATCH[1]}; fi
    if [[ "$endpoint" =~ \&created=([^\&]+) ]]; then
        bounds=${BASH_REMATCH[1]}
        case "$bounds" in
            '>='*) lower=${bounds:2} ;;
            '<='*) upper=${bounds:2} ;;
            *..*) lower=${bounds%%..*}; upper=${bounds#*..} ;;
            *) echo 'unexpected created filter' >&2; exit 1 ;;
        esac
    fi
    jq --arg workflow "$workflow" --arg event "$event" --arg lower "$lower" --arg upper "$upper" --argjson limit "$limit" '
        [.runs[] | select(.workflow == $workflow and ($event == "all" or .event == $event))
            | select(($lower == "-" or .created_at >= $lower) and ($upper == "-" or .created_at <= $upper))]
        | sort_by(.created_at) | reverse | {total_count:length,workflow_runs:.[:$limit]}' "$TIMINGS_FIXTURE_API"
elif [[ "$endpoint" =~ runs/([0-9]+)/attempts/([0-9]+)/jobs\?per_page=100\&page=([0-9]+)$ ]]; then
    run=${BASH_REMATCH[1]} attempt=${BASH_REMATCH[2]} page=${BASH_REMATCH[3]}
    # A transient gateway error succeeds on the bounded retry. The other
    # failure persists, but cannot discard the report's available data.
    if [[ "$run" == 9 && ! -e "$TIMINGS_FIXTURE_STATE/retried" ]]; then
        touch "$TIMINGS_FIXTURE_STATE/retried"
        printf '{"message":"Bad Gateway","status":"502"}\n'
        exit 1
    fi
    if [[ "$run" == 11 ]]; then
        printf '{"message":"Bad Gateway","status":"502"}\n'
        exit 1
    fi
    jq --arg key "$run-$attempt" --argjson page "$page" '.jobs[$key] // [] | {total_count:length,jobs:.[(($page-1)*100):($page*100)]}' "$TIMINGS_FIXTURE_API"
elif [[ "$endpoint" =~ runs/([0-9]+)/attempts/([0-9]+)$ ]]; then
    jq --arg key "${BASH_REMATCH[1]}-${BASH_REMATCH[2]}" '.attempts[$key]' "$TIMINGS_FIXTURE_API"
elif [[ "$endpoint" =~ compare/([0-9a-f]{40})\.\.\.main$ ]]; then
    sha=${BASH_REMATCH[1]}
    run=$(jq -r --arg sha "$sha" '.runs[] | select(.head_sha == $sha) | .id' "$TIMINGS_FIXTURE_API")
    if [[ "$run" == 6 ]]; then printf '{"message":"Bad Gateway","status":"502"}\n'; exit 1; fi
    if [[ "$run" == 12 && "$(grep -Fc "$endpoint" "$TIMINGS_FIXTURE_CALLS")" -gt 1 ]]; then printf '{"status":"ahead"}\n'; exit 0; fi
    jq --arg run "$run" '{status:.compares[$run]}' "$TIMINGS_FIXTURE_API"
elif [[ "$endpoint" =~ branches/gh-readonly-queue%2Fmain%2Fpr-([0-9]+)-base$ ]]; then
    run=${BASH_REMATCH[1]}
    if [[ "$run" == 5 ]]; then printf '{"message":"Not Found","status":"404"}\n'; exit 1; fi
    jq --argjson run "$run" 'if $run == 12 then {commit:{sha:"ffffffffffffffffffffffffffffffffffffffff"}} else .runs[] | select(.id == $run) | {commit:{sha:.head_sha}} end' "$TIMINGS_FIXTURE_API"
elif [[ "$endpoint" =~ jobs/([0-9]+)/logs$ ]]; then
    case "${BASH_REMATCH[1]}" in
        12)
            printf '%s\n' '2026-11-03T01:00:30.0000000Z model name : Fixture Hosted CPU' \
                '2026-11-03T01:00:30.0000000Z $(false) this untrusted line is never executed' ;;
        31)
            printf '%s\n' "2026-11-03T01:02:10.1000000Z ##[group]Run '/usr/local/libexec/buster-9700x/job-started.sh'" \
                '2026-11-03T01:02:15.9000000Z buster-9700x: this job has the machine' \
                '2026-11-03T01:03:09.9000000Z buster-9700x: gave the machine back' \
                '2026-11-03T01:02:16.0000000Z cpu model: Fixture Hardware CPU' ;;
        *) echo 'unexpected log request' >&2; exit 1 ;;
    esac
elif [[ "$endpoint" == 'repos/buster14a/buster-performance/contents/nightly/reports?ref=nightly-ledger' ]]; then
    printf '[{"type":"file","name":"2026-11-03.md"}]\n'
elif [[ "$endpoint" == 'repos/buster14a/buster-performance/contents/nightly/reports/2026-11-03.md?ref=nightly-ledger' ]]; then
    jq '{encoding:"base64",content:(.nightly|@base64)}' "$TIMINGS_FIXTURE_API"
else
    echo "unexpected gh invocation: $endpoint" >&2
    exit 1
fi
GH
chmod +x "$work/bin/gh"
export PATH="$work/bin:$PATH" GITHUB_REPOSITORY=buster14a/buster
export TIMINGS_FIXTURE_API="$work/api.json" TIMINGS_FIXTURE_CALLS="$work/calls" TIMINGS_FIXTURE_STATE="$work"
export CI_TIMINGS_NOW=1793668600 CI_TIMINGS_RUN_LIMIT=10 CI_TIMINGS_SPLIT_AT=2026-11-03T01:03:00Z
export CI_TIMINGS_WORKFLOWS='9700x.yml build.yml publish-hosted-inputs.yml ci-timings.yml'
export CI_TIMINGS_MAX_SECONDS=600 CI_TIMINGS_LOG_LIMIT=24
bash tools/ci/timings.sh "$work/report" > "$work/stdout"
facts="$work/report/facts.tsv"
# Every outcome and both cohorts survive, including old attempts and a missing
# API page. There are no native tests in the PR's setup/build observation.
awk -F '\t' '$1=="run"&&$2==1&&$3==1&&$6=="before"&&$10=="failure" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==1&&$3==2&&$10=="success" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==7&&$6=="after" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==2&&$10=="cancelled"&&$14=="superseded-possible" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==3&&$10=="timed_out"&&$15==1 {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==4&&$14=="live" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==5&&$14=="invalidated" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==6&&$14=="unknown" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==8&&$14=="merged" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==12&&$14=="merged" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==13&&$5=="other"&&$14=="unknown" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="job"&&$2==131&&$4=="hosted" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="run"&&$2==11&&$15==0 {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="job"&&$2==101&&$7=="in_progress"&&$11=="-" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="job"&&$2==3000&&$4=="none"&&$8=="skipped" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="job"&&$2==121&&$4=="none"&&$8=="cancelled" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="job"&&$2==22&&$4=="hosted"&&$10=="-" {found=1} END {exit !found}' "$facts"
awk -F '\t' '$1=="job"&&$2==12&&$6=="Fixture Hosted CPU" {found=1} END {exit !found}' "$facts"
test "$(awk -F '\t' '$1=="job" {n++} END {print n}' "$facts")" -eq 114
test "$(awk -F '\t' '$1=="step"&&($3~/^Compare /||$3~/^Time /||$3~/^Attribute /) {n++} END {print n}' "$facts")" -eq 4
grep -Fq 'https://github.com/buster14a/buster/actions/runs/1/attempts/1' "$work/report/run-links.tsv"
grep -Fq 'Carried over: 3 states;' "$work/report/notes.tsv"
grep -Fq 'unknown' "$work/report/summary.md"
grep -Fq 'jobs-9-1-1' "$work/report/requests.tsv"
test "$(grep -Fc 'runs/9/attempts/1/jobs' "$work/calls")" -eq 2
test "$(grep -Fc 'runs/11/attempts/1/jobs' "$work/calls")" -eq 2
test "$(grep -Fc 'runs/3/attempts/1/jobs' "$work/calls")" -eq 2
# Logs contain data only and are not retained, including failed log downloads.
test ! -e "$work/report/job.log"
test ! -e "$work/report/job-12-log.json"
# Replay is network-free and reproduces every fact, independent of note counters.
cp "$work/calls" "$work/calls-before-replay"
CI_TIMINGS_REPLAY="$work/report" bash tools/ci/timings.sh "$work/replayed" > /dev/null
cmp "$work/calls" "$work/calls-before-replay"
awk -F '\t' '$1!="note"' "$facts" > "$work/facts-expected"
awk -F '\t' '$1!="note"' "$work/replayed/facts.tsv" > "$work/facts-actual"
cmp "$work/facts-expected" "$work/facts-actual"
# Reusing the output directory must discard previous CPU markers/raw responses.
CI_TIMINGS_LOG_LIMIT=0 CI_TIMINGS_WORKFLOWS=build.yml bash tools/ci/timings.sh "$work/report" > /dev/null
test ! -e "$work/report/job-12-evidence.json"
test ! -e "$work/report/jobs-1-2-1.json"
# Exhaustion is a successful partial report with an explicit missing observation.
CI_TIMINGS_MAX_REQUESTS=1 CI_TIMINGS_WORKFLOWS=9700x.yml bash tools/ci/timings.sh "$work/bounded" > /dev/null
grep -Fq 'budget exhausted' "$work/bounded/notes.tsv"
test -s "$work/bounded/summary.md"
# Truncated retry histories cannot claim complete resource accounting.
CI_TIMINGS_ATTEMPT_LIMIT=1 CI_TIMINGS_REPLAY="$work/replayed" bash tools/ci/timings.sh "$work/attempt-bound" > /dev/null
awk -F '\t' '$1=="run"&&$2==1&&$3==2&&$15==0 {found=1} END {exit !found}' "$work/attempt-bound/facts.tsv"
if CI_TIMINGS_HOSTED_CAPACITY=500 CI_TIMINGS_CAPACITY_SOURCE=- bash tools/ci/timings.sh "$work/no-source" > /dev/null 2>&1; then
    echo 'an unverified capacity limit was accepted' >&2; exit 1
fi
"$work/timings" < /dev/null > "$work/empty-legacy.md"
if printf 'invalid\n' | "$work/timings" --accounting > "$work/invalid.md" 2> /dev/null; then
    echo 'invalid facts accepted' >&2; exit 1
fi
test ! -s "$work/invalid.md"
printf 'timings collection: outcomes, cohorts, attempts, pagination, CPU/lock evidence, queue fate, partial reads, bounds and replay checked\n'
