#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Fixed API responses exercise bounded collection without network or hardware.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/fixtures" "$work/bin"
fixtures="$work/fixtures"
fail() { printf 'nightly-observe fixtures: %s\n' "$1" >&2; exit 1; }
expect() { grep -E -- "$2" "$1" > /dev/null || { cat "$1" >&2; fail "$3"; }; }
bash -n tools/ci/nightly-observe.sh
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/nightly.c -o "$work/nightly"
if [[ "$#" -eq 0 ]]; then
    "$work/nightly" --self-test
    cc -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tools/ci/nightly.c -o "$work/nightly-sanitize"
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/nightly-sanitize" --self-test
elif [[ "$#" -ne 1 || "$1" != --glue-only ]]; then
    fail 'usage: nightly-observe.sh [--glue-only]'
fi

sha=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
other=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
snapshot=$(date -u -d '2026-11-26 10:00:00' +%s)
pinned=$(date -u -d '2026-11-25 21:15:00' +%s)
printf '# buster-nightly-ledger 1\ncycle\t2026-11-26\t%s\t9000\t%s\n' "$sha" "$pinned" > "$fixtures/ledger.tsv"
(cd "$fixtures" && zip -q ledger.zip ledger.tsv)
printf '# buster-nightly-ledger 1\n' > "$fixtures/ledger.tsv"
(cd "$fixtures" && zip -q empty-ledger.zip ledger.tsv)
cat > "$fixtures/worker.c" <<'C'
#define BUSTER_NIGHTLY_WINDOW_START_HOUR 2
#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8
C
jq -n --arg content "$(base64 -w0 "$fixtures/worker.c")" --arg sha "$(git hash-object "$fixtures/worker.c")" --argjson size "$(wc -c < "$fixtures/worker.c")" \
    '{type:"file",encoding:"base64",path:"tools/ci/nightly.c",content:$content,sha:$sha,size:$size}' > "$fixtures/worker.json"
jq -n --arg sha "$sha" '{id:700,run_attempt:1,event:"workflow_dispatch",status:"completed",conclusion:"success",head_sha:$sha,path:".github/workflows/9700x.yml",head_branch:("nightly/"+$sha),created_at:"2026-11-26T01:15:00Z",run_started_at:"2026-11-26T01:15:00Z",updated_at:"2026-11-26T01:55:00Z",head_repository:{id:1421805693},display_title:("Nightly snapshot (admit) of "+$sha+" [20261126.9000.1.admit]")}' > "$fixtures/run-1.json"
jq '.run_attempt=2 | .run_started_at="2026-11-26T02:00:00Z" | .updated_at="2026-11-26T02:30:00Z" | .conclusion="failure"' "$fixtures/run-1.json" > "$fixtures/run-2.json"
jq -n '{total_count:1,jobs:[{id:1001,run_id:700,run_attempt:1,status:"completed",conclusion:"success",name:"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)",started_at:"2026-11-26T01:20:00Z",completed_at:"2026-11-26T01:50:00Z"}]}' > "$fixtures/jobs-1.json"
jq '.jobs[0].id=1002 | .jobs[0].run_attempt=2 | .jobs[0].conclusion="failure" | .jobs[0].started_at="2026-11-26T02:05:00Z" | .jobs[0].completed_at="2026-11-26T02:25:00Z"' "$fixtures/jobs-1.json" > "$fixtures/jobs-2.json"
cat > "$fixtures/record.tsv" <<EOF
format	buster-coverage-record-1
repository	1421805693
run	700
attempt	1
event	workflow_dispatch
purpose	snapshot
request	20261126.9000.1.admit
commit	$sha
tree	$other
parent	$other
parent_tree	$other
pr_head	-
snapshot	$sha
workflow	$other
selector	$other
inventory	$other
toolchain	$other
corpus	$other
mode	on
code	true
measure	-
gates	checker,zen5
changes_result	skipped
evidence_result	success
scope	plain
hosted	run
hosted_source	-
native_evidence	run
native_source	-
measurement_evidence	run
measurement_source	-
hardware	admit
hardware_reason	admitted
test_result	success
fuzz_result	skipped
benchmark_result	success
window	open
native	passed
host	9700X/6.17.1-arch1/0xb404023/gcc-15.2.1/glibc_2.42/sw-0123456789abcdef
qualification_result	success
override	-
coverage	full
outcome	complete
EOF
(cd "$fixtures" && zip -q record-1.zip record.tsv)
sed -i -e $'s/^attempt\t1$/attempt\t2/' -e $'s/^benchmark_result\tsuccess$/benchmark_result\tfailure/' -e $'s/^native\tpassed$/native\tstarted/' -e $'s/^outcome\tcomplete$/outcome\tblocked-native/' "$fixtures/record.tsv"
(cd "$fixtures" && zip -q record-2.zip record.tsv)

cat > "$work/bin/gh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
[[ "$#" -eq 4 && "$1 $2 $3" == 'api --method GET' ]] || { printf 'observer attempted a non-GET API operation\n' >&2; exit 99; }
endpoint=$4
printf '%s\n' "$endpoint" >> "$FIXTURES/calls.log"
case "$endpoint" in
    repos/buster14a/buster/actions/workflows/nightly-backfill.yml/runs*)
        # A failed coordinator is deliberate: ledger evidence does not need green.
        printf '{"workflow_runs":[{"id":9000,"head_repository":{"id":1421805693},"head_branch":"main","event":"schedule","path":".github/workflows/nightly-backfill.yml","status":"completed","conclusion":"failure"}]}' ;;
    repos/buster14a/buster/actions/runs/9000/artifacts*)
        published='2026-11-26T08:45:00Z'
        [[ "$CASE" != stale ]] || published='2026-11-24T08:45:00Z'
        printf '{"total_count":1,"artifacts":[{"id":9001,"name":"nightly-ledger-window-1","expired":false,"created_at":"%s"}]}\n' "$published" ;;
    repos/buster14a/buster/actions/artifacts/9001/zip)
        if [[ "$CASE" == no-attempt ]]; then cat "$FIXTURES/empty-ledger.zip"; else cat "$FIXTURES/ledger.zip"; fi ;;
    repos/buster14a/buster/actions/workflows/9700x.yml/runs*)
        if [[ "$CASE" == api-failure ]]; then printf '{"message":"upstream unavailable"}'; exit 1; fi
        if [[ "$endpoint" == *created=2026-11-26T10:* ]]; then
            if [[ "$CASE" == new-admission ]]; then
                jq -s '{total_count:1,workflow_runs:[.[0] | .id=701 | .created_at="2026-11-26T10:00:00Z"]}' "$FIXTURES/run-1.json"
            else
                printf '{"total_count":0,"workflow_runs":[]}\n'
            fi
            exit 0
        fi
        if [[ "$endpoint" == *created=2026-11-26T* && "$CASE" != no-attempt ]]; then
            jq -s '{total_count:1,workflow_runs:.}' "$FIXTURES/run-1.json" > "$FIXTURES/list.json"
            case "$CASE" in
                cap) jq '.total_count=101' "$FIXTURES/list.json" ;;
                targeted) jq '.workflow_runs[0].display_title |= sub("snapshot";"backlog")' "$FIXTURES/list.json" ;;
                wrong-repository) jq '.workflow_runs[0].head_repository.id=42' "$FIXTURES/list.json" ;;
                hidden-admission) jq '.total_count=2 | .workflow_runs += [.workflow_runs[0] | .id=701 | .display_title=null]' "$FIXTURES/list.json" ;;
                *) cat "$FIXTURES/list.json" ;;
            esac
        else
            printf '{"total_count":0,"workflow_runs":[]}\n'
        fi ;;
    repos/buster14a/buster/actions/runs/700)
        if [[ "$CASE" == rerun || "$CASE" == inherited ]]; then cat "$FIXTURES/run-2.json"; else cat "$FIXTURES/run-1.json"; fi ;;
    repos/buster14a/buster/actions/runs/700/attempts/1) cat "$FIXTURES/run-1.json" ;;
    repos/buster14a/buster/actions/runs/700/attempts/2) cat "$FIXTURES/run-2.json" ;;
    repos/buster14a/buster/actions/runs/700/attempts/1/jobs*)
        if [[ "$CASE" == wrong-attempt ]]; then jq '.jobs[0].run_attempt=2' "$FIXTURES/jobs-1.json"; else cat "$FIXTURES/jobs-1.json"; fi ;;
    repos/buster14a/buster/actions/runs/700/attempts/2/jobs*)
        if [[ "$CASE" == inherited ]]; then cat "$FIXTURES/jobs-1.json"; else cat "$FIXTURES/jobs-2.json"; fi ;;
    repos/buster14a/buster/actions/runs/700/artifacts*)
        if [[ "$CASE" == bad-publication ]]; then
            printf '{"total_count":1,"artifacts":[{"id":701,"name":"coverage-record-1","expired":false,"created_at":"not-a-date"}]}\n'
        elif [[ "$CASE" == missing-record ]]; then
            printf '{"total_count":0,"artifacts":[]}\n'
        elif [[ "$CASE" == expired ]]; then
            printf '{"total_count":1,"artifacts":[{"id":701,"name":"coverage-record-1","expired":true,"created_at":"2026-11-26T01:55:00Z"}]}\n'
        elif [[ "$CASE" == rerun || "$CASE" == inherited ]]; then
            printf '{"total_count":2,"artifacts":[{"id":701,"name":"coverage-record-1","expired":false,"created_at":"2026-11-26T01:55:00Z"},{"id":702,"name":"coverage-record-2","expired":false,"created_at":"2026-11-26T02:30:00Z"}]}\n'
        else
            printf '{"total_count":1,"artifacts":[{"id":701,"name":"coverage-record-1","expired":false,"created_at":"2026-11-26T01:55:00Z"}]}\n'
        fi ;;
    repos/buster14a/buster/actions/artifacts/701/zip) cat "$FIXTURES/record-1.zip" ;;
    repos/buster14a/buster/actions/artifacts/702/zip) cat "$FIXTURES/record-2.zip" ;;
    repos/buster14a/buster/contents/tools/ci/nightly.c*)
        if [[ "$CASE" == wrong-worker ]]; then jq '.sha="0000000000000000000000000000000000000000"' "$FIXTURES/worker.json"; else cat "$FIXTURES/worker.json"; fi ;;
    *) printf 'unexpected endpoint: %s\n' "$endpoint" >&2; exit 1 ;;
esac
SH
chmod +x "$work/bin/gh"

run_case()
{
    local name=$1 expected=$2 status=0
    : > "$fixtures/calls.log"
    FIXTURES="$fixtures" CASE="$name" GITHUB_REPOSITORY=buster14a/buster NIGHTLY_OBSERVE_GH="$work/bin/gh" NIGHTLY_OBSERVE_SINCE=2026-11-26 NIGHTLY_OBSERVE_NOW="$snapshot" NIGHTLY_OBSERVE_MAX_PAGES=1 NIGHTLY_OBSERVE_TEST_TOOL="$work/nightly" \
        bash tools/ci/nightly-observe.sh "$work/$name" > "$work/$name.log" 2>&1 || status=$?
    if [[ "$status" -ne "$expected" ]]; then cat "$work/$name.log" >&2; fail "$name returned $status, expected $expected"; fi
    [[ -s "$work/$name/report.md" ]] || fail "$name left no explicit report"
}

run_case complete 0
expect "$work/complete/report.md" 'complete-at-pin' 'exact full native record completes at the pin'
expect "$work/complete/observations.tsv" $'^attempt\t700\t1\t[0-9]+\t1001\t[0-9]+\t[0-9]+\tcomplete\tpresent\t[0-9]+\t2\t8$' 'attempt, native timestamps and worker window retained'
expect "$fixtures/calls.log" '/attempts/1/jobs' 'collection asks for the exact attempt jobs'
[[ -s "$work/complete/records/700-1.tsv" ]] || fail 'raw coverage record not retained'
expect "$work/complete/raw/ledger-coordinators.json" 'failure' 'fixture used a failed coordinator'
run_case no-attempt 1
expect "$work/no-attempt/report.md" 'not-attempted' 'whole missing date is not success'
expect "$work/no-attempt/report.md" 'no pin and no snapshot admission' 'missing dates are not inferred from retained pins'
run_case targeted 1
expect "$work/targeted/report.md" 'not-attempted' 'targeted work supplies no full obligation'
run_case api-failure 2
expect "$work/api-failure/observations.tsv" $'^collection\t[0-9]+\t[0-9]+\tunavailable$' 'API failure stays explicit'
run_case stale 2
run_case cap 2
run_case wrong-repository 2
run_case hidden-admission 2
run_case new-admission 2
run_case bad-publication 2
run_case wrong-attempt 2
run_case wrong-worker 2
run_case missing-record 2
run_case expired 2
run_case rerun 1
expect "$work/rerun/observations.tsv" $'^attempt\t700\t2\t' 'later rerun preserved'
expect "$fixtures/calls.log" '/attempts/2/jobs' 'latest jobs never substitute for earlier attempts'
[[ -s "$work/rerun/records/700-1.tsv" && -s "$work/rerun/records/700-2.tsv" ]] || fail 'rerun erased an original record'
run_case inherited 2

status=0
GITHUB_REPOSITORY=buster14a/buster bash tools/ci/nightly-observe.sh "$work/complete" > "$work/reused.log" 2>&1 || status=$?
[[ "$status" -ne 0 ]] || fail 'reused output directory accepted stale files'
workflow=.github/workflows/nightly-observe.yml
expect "$workflow" 'group: nightly-observe-.*github.run_id' 'scheduled observer needs its own run-id concurrency group'
expect "$workflow" 'actions: read' 'observer must read Actions evidence'
if grep -E 'actions: write|contents: write|runs-on:.*self-hosted|workflow_run:|needs:.*(prepare|window)' "$workflow" > /dev/null; then fail 'observer depends on coordinator or can mutate work'; fi
[[ "$(grep -c 'timezone: Europe/Madrid' "$workflow")" -eq 3 ]] || fail 'independent observer schedules changed without fixture update'
expect "$workflow" 'retention-days: 90' 'observer must retain original decisions and evidence'
printf 'nightly-observe: bounded collection and independent workflow fixtures passed\n'
