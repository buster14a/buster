#!/usr/bin/env bash
# For each of Debug and Release (sanitize/fuzz off): a clean instrumented +
# time-traced build, `ide bench`, two diagnostic reports of where compile
# time went (ninja_log_summary, time_trace_summary -- printed to the CI log
# only, not recorded), then hands the numbers to record_perf.sh.
#
# Used by the Linux and macOS Perf CI steps. Windows drives the same
# build.ps1 -> record_perf.sh handoff inline in its own PowerShell step
# instead of calling this script, since its compile step needs the Visual
# Studio dev shell environment that only lives inside that one PowerShell
# process.
#
# Required env: RUNNER_NAME, COMMIT_SHA, PERF_HISTORY_TOKEN, PUSH_HISTORY.
# GITHUB_SERVER_URL and GITHUB_REPOSITORY are auto-exported by Forgejo
# Actions.
set -euo pipefail

host="${GITHUB_SERVER_URL#https://}"
host="${host#http://}"
export REPO_PUSH_URL="https://x-access-token:${PERF_HISTORY_TOKEN}@${host}/${GITHUB_REPOSITORY}.git"

# Both configs must always run and record, even if one regresses -- `set -e`
# would otherwise abort the loop after the first config's record_perf.sh
# failure and silently skip the other one.
overall_result=0

for config in Debug Release; do
    build_dir="build/perf-${config}"
    rm -rf "$build_dir"

    generate_start=$(date +%s)
    ./build.sh generate --build-dir "$build_dir" --config "$config" \
        --no-sanitize --no-fuzz --instrument --time-trace
    generate_end=$(date +%s)

    build_start=$(date +%s)
    ./build.sh build --build-dir "$build_dir" --config "$config" -t bench_all | tee "$build_dir/bench_output.log"
    build_end=$(date +%s)

    export CONFIG="$config"
    export COMPILE_SECONDS=$(( (generate_end - generate_start) + (build_end - build_start) ))
    export BENCH_LINE=$(grep '^BENCH ' "$build_dir/bench_output.log" | tail -n1)
    export BENCH_PHASE_LINES=$(grep '^BENCH_PHASE ' "$build_dir/bench_output.log" || true)
    export BENCH_FILE_LINES=$(grep '^BENCH_FILE ' "$build_dir/bench_output.log" || true)

    echo "--- Where compile time went ($config) ---"
    ./build.sh ninja_log_summary "$build_dir" --limit 15 || true

    json_files=$(find "$build_dir" -name '*.json' -path '*CMakeFiles*ide.dir*')
    if [[ -n "$json_files" ]]; then
        ./build.sh time_trace_summary $json_files --limit 15 || true
    fi

    if ! ./.forgejo/scripts/record_perf.sh; then
        overall_result=1
    fi
done

exit "$overall_result"
