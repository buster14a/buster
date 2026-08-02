#!/usr/bin/env bash
# For each of Debug and Release (sanitize/fuzz off): a clean instrumented +
# time-traced `ide` build, a separately run `ide bench`, two diagnostic
# reports of where compile time went (ninja_log_summary, time_trace_summary
# -- printed to the CI log only, not recorded), then hands the numbers to
# record_perf.sh.
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

# Both configs must always run and record. Keep genuine record/push failures
# until both configurations have had an opportunity to run.
overall_result=0

for config in Debug Release; do
    build_dir="build/perf-${config}"
    rm -rf "$build_dir"

    # Bash's time keyword is available on both Linux and macOS and reports
    # fractional seconds. `date +%s` is too coarse here: independently
    # rounding generation and building can add almost two seconds of error
    # to a build that only takes a few seconds in total. Keep the timing file
    # outside build_dir because `build.sh generate` recreates that directory.
    timing_file=$(mktemp)
    trap 'rm -f "$timing_file"' EXIT
    TIMEFORMAT='%R'
    { time {
        ./build.sh generate --build-dir "$build_dir" --config "$config" \
            --no-sanitize --no-fuzz --instrument --time-trace 2>&3
    }; } 3>&2 2>"$timing_file"

    export CONFIG="$config"
    generation_milliseconds=$(awk '{ printf "%.0f\n", $1 * 1000 }' "$timing_file")
    rm -f "$timing_file"

    timing_file=$(mktemp)
    { time {
        ./build.sh build --build-dir "$build_dir" --config "$config" -t ide 2>&3
    }; } 3>&2 2>"$timing_file"

    ide_build_milliseconds=$(awk '{ printf "%.0f\n", $1 * 1000 }' "$timing_file")
    export COMPILE_MILLISECONDS
    COMPILE_MILLISECONDS=$((generation_milliseconds + ide_build_milliseconds))
    rm -f "$timing_file"

    echo "--- Where compile time went ($config) ---"
    ./build.sh ninja_log_summary "$build_dir" --limit 15 || true

    json_files=$(find "$build_dir" -name '*.json' -path '*CMakeFiles*ide.dir*')
    if [[ -n "$json_files" ]]; then
        ./build.sh time_trace_summary $json_files --limit 15 || true
    fi

    # Keep benchmark execution outside the compile timer and after the build
    # diagnostics. `bench_all` depends on the already up-to-date `ide`, so this
    # invocation only runs the benchmark, does not inflate compile time, and
    # does not appear among the compile edges printed above. Its wall time is
    # timed separately as this config's total benchmark run time. The command's
    # stderr is merged into the pipe before the outer redirection, so the
    # timing file receives only the `time` output.
    timing_file=$(mktemp)
    { time {
        ./build.sh build --build-dir "$build_dir" --config "$config" -t bench_all \
            2>&1 | tee "$build_dir/bench_output.log"
    }; } 2>"$timing_file"
    export BENCH_RUN_MILLISECONDS
    BENCH_RUN_MILLISECONDS=$(awk '{ printf "%.0f\n", $1 * 1000 }' "$timing_file")
    rm -f "$timing_file"

    export BENCH_LINE=$(grep '^BENCH ' "$build_dir/bench_output.log" | tail -n1)
    export BENCH_PHASE_LINES=$(grep '^BENCH_PHASE ' "$build_dir/bench_output.log" || true)
    export BENCH_FILE_LINES=$(grep '^BENCH_FILE ' "$build_dir/bench_output.log" || true)

    # Per-config totals; Debug and Release each report their own, never summed.
    echo "PERF_TOTAL config=$config generation_milliseconds=$generation_milliseconds ide_build_milliseconds=$ide_build_milliseconds compile_milliseconds=$COMPILE_MILLISECONDS bench_run_milliseconds=$BENCH_RUN_MILLISECONDS"

    if ! ./.forgejo/scripts/record_perf.sh; then
        overall_result=1
    fi
done

exit "$overall_result"
