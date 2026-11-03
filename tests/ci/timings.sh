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
        printf '%s\n' '{"workflow_runs":[{"id":1,"event":"pull_request"},{"id":2,"event":"push"},{"id":3,"event":"schedule"},{"id":4,"event":"unsupported"}]}' ;;
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
11	pr	benchmark-steps	15
23	main	execution	100
31	periodic	queue	5
31	periodic	execution	5
32	periodic	queue	10
32	periodic	execution	20
TSV
cmp "$work/expected.tsv" "$work/report/observations.tsv"
grep -Fq '| scheduled/manual | queue | 2 | 7.5 | 10 |' "$work/report/summary.md"
grep -Fq '| scheduled/manual | execution | 2 | 12.5 | 20 |' "$work/report/summary.md"
"$work/timings" < /dev/null > "$work/empty.md"
grep -Fq 'No eligible observations' "$work/empty.md"
printf '%s\n' 'invalid' > "$work/invalid.tsv"
if "$work/timings" < "$work/invalid.tsv" > "$work/invalid.md" 2> /dev/null; then
    echo 'invalid input unexpectedly accepted' >&2
    exit 1
fi
test ! -s "$work/invalid.md"
printf 'timings extraction: successful, failed, missing timestamps and paginated jobs checked\n'
