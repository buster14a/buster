#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Network-free fixture glue for the C statistics and the API extraction boundary.
set -euo pipefail
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c17 -O2 -Wall -Wextra -Werror -fno-strict-aliasing tools/ci/timings.c -o "$work/timings"
"$work/timings" --self-test
cc -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -fno-sanitize-recover=all tools/ci/timings.c -o "$work/timings-sanitize"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$work/timings-sanitize" --self-test
mkdir "$work/bin"
cat > "$work/bin/gh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == api ]]
case "${*: -1}" in
    *workflows/9700x.yml/runs\?per_page=100)
        printf '%s\n' '{"workflow_runs":[{"id":1,"event":"pull_request"},{"id":2,"event":"push"},{"id":3,"event":"schedule"},{"id":4,"event":"unsupported"},{"id":5,"event":"workflow_dispatch","display_title":"Nightly backlog (admit) of 0123456789abcdef0123456789abcdef01234567 [20261104.9.1.admit]"},{"id":6,"event":"workflow_dispatch","display_title":"Tests and Zen 5 benchmarks"}]}' ;;
    *runs/1/jobs\?per_page=100\&filter=latest)
        cat <<'JSON'
{"jobs":[{"id":11,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success","created_at":"2026-11-03T01:00:00Z","started_at":"2026-11-03T01:00:10Z","completed_at":"2026-11-03T01:02:00Z","steps":[{"name":"Set up runner","conclusion":"success","started_at":"2026-11-03T01:00:10Z","completed_at":"2026-11-03T01:00:20Z"},{"name":"Verify benchmark host","conclusion":"success","started_at":"2026-11-03T01:00:20Z","completed_at":"2026-11-03T01:00:21Z"},{"name":"Run complete native vector correctness before benchmarks","conclusion":"success","started_at":"2026-11-03T01:00:30Z","completed_at":"2026-11-03T01:01:00Z"},{"name":"Benchmark first","conclusion":"success","started_at":"2026-11-03T01:01:00Z","completed_at":"2026-11-03T01:01:10Z"},{"name":"Measure second","conclusion":"success","started_at":"2026-11-03T01:01:10Z","completed_at":"2026-11-03T01:01:15Z"},{"name":"Benchmark skipped","conclusion":"skipped","started_at":"2026-11-03T01:01:15Z","completed_at":"2026-11-03T01:01:59Z"}]}]}
JSON
        ;;
    *runs/2/jobs\?per_page=100\&filter=latest)
        cat <<'JSON'
{"jobs":[{"id":22,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"failure","created_at":"2026-11-03T01:00:00Z","started_at":"2026-11-03T01:00:10Z","completed_at":"2026-11-03T01:02:00Z"},{"id":23,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success","started_at":"2026-11-03T01:00:00Z","completed_at":"2026-11-03T01:01:40Z"}]}
JSON
        ;;
    *runs/3/jobs\?per_page=100\&filter=latest)
        [[ "$2" == --paginate ]]
        cat <<'JSON'
{"jobs":[{"id":31,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success","created_at":"2026-11-03T01:00:00Z","started_at":"2026-11-03T01:00:05Z","completed_at":"2026-11-03T01:00:10Z"}]}
{"jobs":[{"id":32,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success","created_at":"2026-11-03T01:00:00Z","started_at":"2026-11-03T01:00:10Z","completed_at":"2026-11-03T01:00:30Z","steps":[{"name":"Run complete native vector correctness before benchmarks","conclusion":"success","started_at":null,"completed_at":"2026-11-03T01:00:20Z"}]}]}
JSON
        ;;
    *runs/5/jobs\?per_page=100\&filter=latest)
        printf '%s\n' '{"jobs":[{"id":51,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success","created_at":"2026-11-04T03:00:00Z","started_at":"2026-11-04T03:00:30Z","completed_at":"2026-11-04T03:04:30Z"}]}' ;;
    *runs/6/jobs\?per_page=100\&filter=latest)
        printf '%s\n' '{"jobs":[{"id":61,"name":"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)","status":"completed","conclusion":"success","created_at":"2026-11-04T09:00:00Z","started_at":"2026-11-04T09:00:20Z","completed_at":"2026-11-04T09:20:20Z"}]}' ;;
    # The runner hooks' lines in the jobs' logs: a whole cycle (11), a wait behind another holder (51), none of them
    # (23, 32), a log that can't be read (31), and one without its release, its first line behind the byte-order mark (61).
    *jobs/11/logs)
        printf '%s\n' "2026-11-03T01:00:10.1000000Z Current runner version: '2.338.0'" \
            '2026-11-03T01:00:10.2000000Z A job started hook has been configured by the self-hosted runner administrator' \
            "2026-11-03T01:00:10.2500000Z ##[group]Run '/usr/local/libexec/buster-9700x/job-started.sh'" \
            '2026-11-03T01:00:10.2600000Z ##[endgroup]' \
            '2026-11-03T01:00:19.8000000Z buster-9700x: this job has the machine' \
            '2026-11-03T01:00:20.0000000Z ##[group]Run test "$(uname -s)" = Linux' \
            "2026-11-03T01:01:59.8000000Z ##[group]Run '/usr/local/libexec/buster-9700x/job-completed.sh'" \
            '2026-11-03T01:01:59.9000000Z buster-9700x: gave the machine back' ;;
    *jobs/51/logs)
        printf '%s\n' "2026-11-04T03:00:30.4000000Z ##[group]Run '/usr/local/libexec/buster-9700x/job-started.sh'" \
            '2026-11-04T03:00:30.4100000Z buster-9700x: waiting for the machine, held by: 2026-11-04T02:59:00+00:00 someone: taskset -c 2 ./build/compiler benchmark-lexer src' \
            '2026-11-04T03:00:38.4000000Z buster-9700x: this job has the machine' \
            '2026-11-04T03:04:29.6000000Z buster-9700x: gave the machine back' ;;
    *jobs/23/logs|*jobs/32/logs)
        printf '%s\n' '2026-11-03T01:00:00.0000000Z Current runner version: '"'2.338.0'" ;;
    *jobs/31/logs)
        printf '{"message":"Not Found","status":"404"}\n'
        exit 1 ;;
    *jobs/61/logs)
        printf '\xef\xbb\xbf%s\n' "2026-11-04T09:00:20.0000000Z ##[group]Run '/usr/local/libexec/buster-9700x/job-started.sh'"
        printf '%s\n' '2026-11-04T09:00:21.0000000Z buster-9700x: this job has the machine' ;;
    # The merge queue: a merged candidate (7), one that passed but was invalidated (8), a failure (9), a cancelled one
    # (10) and one whose fate can't be read (12); another event's run (13) and one without its times (14) are left out.
    *workflows/9700x.yml/runs\?event=merge_group\&status=completed\&per_page=100)
        printf '%s\n' '{"workflow_runs":[{"id":7,"event":"merge_group","status":"completed","conclusion":"success","head_sha":"0000000000000000000000000000000000000007","created_at":"2026-11-13T09:00:00Z","run_started_at":"2026-11-13T09:00:10Z","updated_at":"2026-11-13T09:37:26Z"},{"id":8,"event":"merge_group","status":"completed","conclusion":"success","head_sha":"0000000000000000000000000000000000000008","created_at":"2026-11-13T09:00:00Z","updated_at":"2026-11-13T09:30:00Z"},{"id":9,"event":"merge_group","status":"completed","conclusion":"failure","head_sha":"0000000000000000000000000000000000000009","created_at":"2026-11-13T09:00:00Z","updated_at":"2026-11-13T09:04:00Z"},{"id":10,"event":"merge_group","status":"completed","conclusion":"cancelled","head_sha":"0000000000000000000000000000000000000010","created_at":"2026-11-13T09:00:00Z","updated_at":"2026-11-13T09:10:00Z"},{"id":12,"event":"merge_group","status":"completed","conclusion":"success","head_sha":"0000000000000000000000000000000000000012","created_at":"2026-11-13T09:00:00Z","updated_at":"2026-11-13T09:10:00Z"},{"id":13,"event":"push","status":"completed","conclusion":"success","head_sha":"0000000000000000000000000000000000000013","created_at":"2026-11-13T09:00:00Z","updated_at":"2026-11-13T09:10:00Z"},{"id":14,"event":"merge_group","status":"completed","conclusion":"success","head_sha":"0000000000000000000000000000000000000014","created_at":null,"updated_at":"2026-11-13T09:10:00Z"}]}' ;;
    *compare/0000000000000000000000000000000000000007...main) echo ahead ;;
    *compare/0000000000000000000000000000000000000008...main) echo diverged ;;
    *compare/0000000000000000000000000000000000000009...main) echo behind ;;
    *compare/0000000000000000000000000000000000000010...main) echo diverged ;;
    *compare/0000000000000000000000000000000000000012...main)
        printf '{"message":"Server Error","status":"502"}\n'
        exit 1 ;;
    *runs/7/jobs\?per_page=100\&filter=latest)
        [[ "$2" == --paginate ]]
        printf '%s\n' '{"jobs":[{"id":71,"started_at":"2026-11-13T09:00:20Z","completed_at":"2026-11-13T09:30:20Z"},{"id":72,"started_at":"2026-11-13T09:01:00Z","completed_at":"2026-11-13T09:01:00Z"}]}' \
            '{"jobs":[{"id":73,"started_at":"2026-11-13T09:05:00Z","completed_at":"2026-11-13T09:15:00Z"},{"id":74,"started_at":null,"completed_at":"2026-11-13T09:15:00Z"}]}' ;;
    *runs/8/jobs\?per_page=100\&filter=latest)
        printf '%s\n' '{"jobs":[{"id":81,"started_at":"2026-11-13T09:00:20Z","completed_at":"2026-11-13T09:20:20Z"}]}' ;;
    *runs/9/jobs\?per_page=100\&filter=latest)
        printf '%s\n' '{"jobs":[{"id":91,"started_at":"2026-11-13T09:00:20Z","completed_at":"2026-11-13T09:00:50Z"}]}' ;;
    *runs/10/jobs\?per_page=100\&filter=latest)
        printf '%s\n' '{"jobs":[]}' ;;
    *) echo "unexpected gh invocation: $*" >&2; exit 1 ;;
esac
SH
chmod +x "$work/bin/gh"
PATH="$work/bin:$PATH" GITHUB_REPOSITORY=buster14a/buster bash tools/ci/timings.sh "$work/report"
cat > "$work/expected.tsv" <<'TSV'
11	pr	queue	10
11	pr	execution	110
11	pr	runner-hook	10
11	pr	setup-build	10
11	pr	native-tests	30
11	pr	host-occupation	100
11	pr	benchmark-steps	15
23	main	execution	100
31	full	queue	5
31	full	execution	5
32	full	queue	10
32	full	execution	20
51	targeted	queue	30
51	targeted	execution	240
61	full	queue	20
61	full	execution	1200
11	pr	lock-wait	10
11	pr	lock-hold	100
51	targeted	lock-wait	8
51	targeted	lock-hold	231
61	full	lock-wait	1
TSV
cmp "$work/expected.tsv" "$work/report/observations.tsv"
grep -Fq '| nightly targeted | lock-wait | 1 | 8.0 | 8 |' "$work/report/summary.md"
grep -Fq '| PR | lock-hold | 1 | 100.0 | 100 |' "$work/report/summary.md"
grep -Fq '| full (daily, dispatched, snapshot) | lock-wait | 1 | 1.0 | 1 |' "$work/report/summary.md"
if grep -Fq '| full (daily, dispatched, snapshot) | lock-hold |' "$work/report/summary.md"; then
    echo 'a log without its release gave a lock hold' >&2
    exit 1
fi
test ! -e "$work/report/job.log" || { echo 'a job log was kept' >&2; exit 1; }
# The collector run again into the same directory reports the same observations, none of the first run's lock lines
# replayed.
PATH="$work/bin:$PATH" GITHUB_REPOSITORY=buster14a/buster bash tools/ci/timings.sh "$work/report" > /dev/null
cmp "$work/expected.tsv" "$work/report/observations.tsv" || { echo 'a second run into the same directory replayed rows' >&2; exit 1; }
test "$(wc -l < "$work/report/locks.tsv")" -eq 5 || { echo 'the lock lines were not started afresh' >&2; exit 1; }
grep -Fq '| full (daily, dispatched, snapshot) | queue | 3 | 10.0 | 20 |' "$work/report/summary.md"
grep -Fq '| full (daily, dispatched, snapshot) | execution | 3 | 20.0 | 1200 |' "$work/report/summary.md"
grep -Fq '| nightly targeted | execution | 1 | 240.0 | 240 |' "$work/report/summary.md"
grep -Fq '| PR | host-occupation | 1 | 100.0 | 100 |' "$work/report/summary.md"
# The merge queue's candidates, each by its fate, and the time spent on those that didn't merge.
printf '7\tmerged\t2236\t2400\n8\tpassed\t1800\t1200\n9\tfailed\t240\t30\n10\tcancelled\t600\t0\n' > "$work/expected-candidates.tsv"
cmp "$work/expected-candidates.tsv" "$work/report/candidates.tsv" || { cat "$work/report/candidates.tsv" >&2; echo 'the candidates differ' >&2; exit 1; }
grep -Fq '| merged | 1 | 40 |' "$work/report/summary.md"
grep -Fq '| passed, not merged (invalidated) | 1 | 20 |' "$work/report/summary.md"
grep -Fq '| all | 4 | 60 |' "$work/report/summary.md"
grep -Fq "Candidates that didn't merge took 20 runner-minutes, 33.9% of the queue's" "$work/report/summary.md"
grep -Fq 'median 37.3 min, p95 37.3 min (1 candidates)' "$work/report/summary.md"
"$work/timings" --queue < /dev/null > "$work/empty-queue.md"
grep -Fq 'No completed candidate runs' "$work/empty-queue.md"
if printf '7\tmerged\t1\t1\n7\tfailed\t1\t1\n' | "$work/timings" --queue > /dev/null 2>&1; then
    echo 'a candidate counted twice was accepted' >&2
    exit 1
fi
"$work/timings" < /dev/null > "$work/empty.md"
grep -Fq 'No eligible observations' "$work/empty.md"
printf '%s\n' 'invalid' > "$work/invalid.tsv"
if "$work/timings" < "$work/invalid.tsv" > "$work/invalid.md" 2> /dev/null; then
    echo 'invalid input unexpectedly accepted' >&2
    exit 1
fi
test ! -s "$work/invalid.md"
printf 'timings extraction: successful, failed, missing timestamps, paginated jobs, the lock lines of job logs and the merge queue'"'"'s candidates checked\n'
