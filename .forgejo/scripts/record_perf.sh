#!/usr/bin/env bash
# Records one CI run's compile-time/run-time numbers into the `perf-history`
# orphan branch, and warns if compile time or preloaded parse time per corpus file
# regresses past PERF_REGRESSION_THRESHOLD relative to that runner's own recent
# median for the same config. The file-read-per-iteration BENCH_IO mode remains
# diagnostic only; its old filesystem-dominated history is never read as the
# new gate.
#
# History is kept as one row per metric, not one wide line per run: phase
# (tokenize/parse) and per-file breakdown (from BUSTER_INSTRUMENT builds)
# don't fit a fixed set of columns, and new metrics can be added later
# without changing the shape of rows already written. Still plain
# "key=value ..." text, no JSON: it must be parseable with grep/sort/awk on
# every CI runner (including macOS's stock bash 3.2 and Windows' git-bash),
# without requiring jq.
#
#   ts=<unix> runner=<r> config=<c> commit=<sha> metric=<m> value=<v> [file=<f>]
#
# Required env:
#   RUNNER_NAME     matrix.runner name
#   CONFIG          build config, e.g. Debug or Release
#   COMPILE_MILLISECONDS wall-clock milliseconds for the clean build
#   BENCH_IO_LINE   the raw "BENCH_IO parse_all_tests iterations=... files=...
#                   min_ns=... median_ns=..." line printed by `ide bench`
#   BENCH_PARSE_LINE the raw "BENCH_PARSE parse_all_tests iterations=... files=...
#                    min_ns=... median_ns=..." line printed by `ide bench`
#   COMMIT_SHA      git sha for this run
#   REPO_PUSH_URL   https URL (with embedded push credentials) for the
#                   perf-history remote; used for both the read (baseline
#                   fetch) and, when PUSH_HISTORY=1, the write
#
# Optional env:
#   BENCH_RUN_MILLISECONDS     wall-clock milliseconds for the bench_all run
#                               (compile time excluded). Recorded, not gated:
#                               it includes ninja/process overhead, so the
#                               per-file median remains the gated signal.
#   BENCH_PHASE_LINES          newline-separated "BENCH_IO_PHASE" or
#                               "BENCH_PARSE_PHASE" lines (BUSTER_INSTRUMENT
#                               only). Recorded, not gated.
#   BENCH_FILE_LINES            newline-separated "BENCH_IO_FILE" or
#                               "BENCH_PARSE_FILE" lines (BUSTER_INSTRUMENT
#                               only). Recorded, not gated -- 21+ per-file
#                               numbers would make the job flaky on any one
#                               noisy file; the aggregate already tells you
#                               *whether* something regressed, these tell
#                               you *where*, on demand.
#   PERF_REGRESSION_THRESHOLD  fraction, default 0.15 (15%)
#   HISTORY_LOOKBACK           samples per (runner, config) used for the
#                               baseline, default 10
#   HISTORY_BRANCH             default perf-history
#   HISTORY_FILE                default history.log
#   PUSH_HISTORY                 "1" to append+commit+push; unset/"0" to only compare
set -euo pipefail

: "${RUNNER_NAME:?RUNNER_NAME is required}"
: "${CONFIG:?CONFIG is required}"
: "${COMPILE_MILLISECONDS:?COMPILE_MILLISECONDS is required}"
: "${BENCH_IO_LINE:?BENCH_IO_LINE is required}"
: "${BENCH_PARSE_LINE:?BENCH_PARSE_LINE is required}"
: "${COMMIT_SHA:?COMMIT_SHA is required}"
: "${REPO_PUSH_URL:?REPO_PUSH_URL is required}"

BENCH_RUN_MILLISECONDS="${BENCH_RUN_MILLISECONDS:-}"
BENCH_PHASE_LINES="${BENCH_PHASE_LINES:-}"
BENCH_FILE_LINES="${BENCH_FILE_LINES:-}"
PERF_REGRESSION_THRESHOLD="${PERF_REGRESSION_THRESHOLD:-0.15}"
HISTORY_LOOKBACK="${HISTORY_LOOKBACK:-10}"
HISTORY_BRANCH="${HISTORY_BRANCH:-perf-history}"
HISTORY_FILE="${HISTORY_FILE:-history.log}"
PUSH_HISTORY="${PUSH_HISTORY:-0}"

field_of() {
    # field_of <text> <field-name> -- prints a key=value numeric field's value.
    printf '%s\n' "$1" | grep -o "${2}=[0-9.]*" | head -n1 | cut -d= -f2
}

case "$BENCH_IO_LINE" in
    BENCH_IO\ *) ;;
    *)
        echo "record_perf.sh: BENCH_IO_LINE has an unexpected prefix: $BENCH_IO_LINE" >&2
        exit 1
        ;;
esac
case "$BENCH_PARSE_LINE" in
    BENCH_PARSE\ *) ;;
    *)
        echo "record_perf.sh: BENCH_PARSE_LINE has an unexpected prefix: $BENCH_PARSE_LINE" >&2
        exit 1
        ;;
esac

bench_io_median_ns=$(field_of "$BENCH_IO_LINE" "median_ns")
bench_io_min_ns=$(field_of "$BENCH_IO_LINE" "min_ns")
bench_io_file_count=$(field_of "$BENCH_IO_LINE" "files")
bench_parse_median_ns=$(field_of "$BENCH_PARSE_LINE" "median_ns")
bench_parse_min_ns=$(field_of "$BENCH_PARSE_LINE" "min_ns")
bench_parse_file_count=$(field_of "$BENCH_PARSE_LINE" "files")

if [[ -z "$bench_io_median_ns" || -z "$bench_io_min_ns" || -z "$bench_io_file_count" || "$bench_io_file_count" == "0" ]]; then
    echo "record_perf.sh: could not parse nonzero files and median_ns/min_ns out of BENCH_IO_LINE: $BENCH_IO_LINE" >&2
    exit 1
fi
if [[ -z "$bench_parse_median_ns" || -z "$bench_parse_min_ns" || -z "$bench_parse_file_count" || "$bench_parse_file_count" == "0" ]]; then
    echo "record_perf.sh: could not parse nonzero files and median_ns/min_ns out of BENCH_PARSE_LINE: $BENCH_PARSE_LINE" >&2
    exit 1
fi

bench_io_median_ns_per_file=$(awk -v median="$bench_io_median_ns" -v files="$bench_io_file_count" \
    'BEGIN { printf "%.0f\n", median / files }')
bench_parse_median_ns_per_file=$(awk -v median="$bench_parse_median_ns" -v files="$bench_parse_file_count" \
    'BEGIN { printf "%.0f\n", median / files }')

# Rows to append this run, built up as we parse the inputs: each entry is
# "metric=<m> value=<v>[ file=<f>]", ready to be prefixed with ts/runner/
# config/commit and written out.
new_rows=()
new_rows+=("metric=compile_milliseconds value=$COMPILE_MILLISECONDS")
if [[ -n "$BENCH_RUN_MILLISECONDS" ]]; then
    new_rows+=("metric=bench_run_milliseconds value=$BENCH_RUN_MILLISECONDS")
fi
new_rows+=("metric=bench_io_median_ns value=$bench_io_median_ns")
new_rows+=("metric=bench_io_min_ns value=$bench_io_min_ns")
new_rows+=("metric=bench_io_file_count value=$bench_io_file_count")
new_rows+=("metric=bench_io_median_ns_per_file value=$bench_io_median_ns_per_file")
new_rows+=("metric=bench_parse_median_ns value=$bench_parse_median_ns")
new_rows+=("metric=bench_parse_min_ns value=$bench_parse_min_ns")
new_rows+=("metric=bench_parse_file_count value=$bench_parse_file_count")
new_rows+=("metric=bench_parse_median_ns_per_file value=$bench_parse_median_ns_per_file")

if [[ -n "$BENCH_PHASE_LINES" ]]; then
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        phase_prefix=$(printf '%s\n' "$line" | awk '{print $1}')
        phase_name=$(printf '%s\n' "$line" | awk '{print $2}')
        median_ns=$(field_of "$line" "median_ns")
        min_ns=$(field_of "$line" "min_ns")
        case "$phase_prefix" in
            BENCH_IO_PHASE) phase_mode=io ;;
            BENCH_PARSE_PHASE) phase_mode=parse ;;
            *) continue ;;
        esac
        [[ -z "$phase_name" || -z "$median_ns" || -z "$min_ns" ]] && continue
        new_rows+=("metric=bench_${phase_mode}_${phase_name}_median_ns value=$median_ns")
        new_rows+=("metric=bench_${phase_mode}_${phase_name}_min_ns value=$min_ns")
    done <<< "$BENCH_PHASE_LINES"
fi

if [[ -n "$BENCH_FILE_LINES" ]]; then
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        file_prefix=$(printf '%s\n' "$line" | awk '{print $1}')
        file_path=$(printf '%s\n' "$line" | grep -o 'path=[^ ]*' | cut -d= -f2)
        median_ns=$(field_of "$line" "median_ns")
        case "$file_prefix" in
            BENCH_IO_FILE) file_mode=io ;;
            BENCH_PARSE_FILE) file_mode=parse ;;
            *) continue ;;
        esac
        [[ -z "$file_path" || -z "$median_ns" ]] && continue
        new_rows+=("metric=bench_${file_mode}_file_median_ns value=$median_ns file=$file_path")
    done <<< "$BENCH_FILE_LINES"
fi

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

git -C "$work_dir" init -q
git -C "$work_dir" remote add origin "$REPO_PUSH_URL"

history_path=""
history_ref=""

if [[ "$PUSH_HISTORY" == "1" ]]; then
    if git -C "$work_dir" fetch -q origin "$HISTORY_BRANCH" 2>/dev/null; then
        git -C "$work_dir" checkout -q -B "$HISTORY_BRANCH" FETCH_HEAD
    else
        # A missing branch is the only fetch failure that can be recovered from.
        # Do not turn authentication, network, or remote failures into a false
        # empty baseline.
        if git -C "$work_dir" ls-remote --exit-code -q origin "refs/heads/$HISTORY_BRANCH" >/dev/null 2>&1; then
            echo "record_perf.sh: failed to fetch existing $HISTORY_BRANCH branch" >&2
            exit 1
        else
            remote_status=$?
            if [[ "$remote_status" -ne 2 ]]; then
                echo "record_perf.sh: could not query $HISTORY_BRANCH on the perf-history remote" >&2
                exit 1
            fi
        fi

        echo "record_perf.sh: no existing $HISTORY_BRANCH branch, starting one"
        git -C "$work_dir" checkout -q --orphan "$HISTORY_BRANCH"
    fi

    history_path="$work_dir/$HISTORY_FILE"
    touch "$history_path"
else
    # Compare-only needs neither a worktree copy of the growing tip blob nor the
    # full branch history. A depth-1 fetch plus git show of the fetched tip
    # provides the baseline without checking anything out.
    if git -C "$work_dir" fetch -q --depth=1 origin "$HISTORY_BRANCH" 2>/dev/null; then
        if ! history_ref=$(git -C "$work_dir" rev-parse FETCH_HEAD); then
            echo "record_perf.sh: failed to resolve fetched $HISTORY_BRANCH branch" >&2
            exit 1
        fi
    else
        # A missing branch is the only fetch failure that can be recovered from.
        # Do not turn authentication, network, or remote failures into a false
        # empty baseline.
        if git -C "$work_dir" ls-remote --exit-code -q origin "refs/heads/$HISTORY_BRANCH" >/dev/null 2>&1; then
            echo "record_perf.sh: failed to fetch existing $HISTORY_BRANCH branch" >&2
            exit 1
        else
            remote_status=$?
            if [[ "$remote_status" -ne 2 ]]; then
                echo "record_perf.sh: could not query $HISTORY_BRANCH on the perf-history remote" >&2
                exit 1
            fi
        fi

        echo "record_perf.sh: no existing $HISTORY_BRANCH branch, starting one"
    fi
fi

# Prints the value's median for a given metric across this (runner, config)'s
# last HISTORY_LOOKBACK samples. Integer-division upper-middle index for
# count entries (index count/2 zero-based), matching parser_bench_run's own
# median. Returns failure if there's no prior history for this metric,
# so callers can skip the check on a cold history.
median_of() {
    local metric="$1"
    local values=()
    local line val
    local history_lines

    if [[ -n "$history_ref" ]]; then
        if ! history_lines=$(git -C "$work_dir" show "$history_ref:$HISTORY_FILE" \
            | awk -v runner="$RUNNER_NAME" -v config="$CONFIG" -v metric="$metric" \
                'index($0, "runner=" runner " ") &&
                 index($0, "config=" config " ") &&
                 index($0, "metric=" metric " value=") { print }' \
            | tail -n "$HISTORY_LOOKBACK"); then
            echo "record_perf.sh: failed to read $HISTORY_FILE from $HISTORY_BRANCH history" >&2
            return 2
        fi
    elif [[ -n "$history_path" ]]; then
        if ! history_lines=$(awk -v runner="$RUNNER_NAME" -v config="$CONFIG" -v metric="$metric" \
            'index($0, "runner=" runner " ") &&
             index($0, "config=" config " ") &&
             index($0, "metric=" metric " value=") { print }' \
            "$history_path" | tail -n "$HISTORY_LOOKBACK"); then
            echo "record_perf.sh: failed to read $HISTORY_FILE from $HISTORY_BRANCH history" >&2
            return 2
        fi
    else
        return 1
    fi

    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        val=$(field_of "$line" "value")
        [[ -n "$val" ]] && values+=("$val")
    done <<< "$history_lines"

    local count=${#values[@]}
    if [[ "$count" -eq 0 ]]; then
        return 1
    fi

    local idx=$(( count / 2 + 1 ))
    printf '%s\n' "${values[@]}" | sort -n | awk -v idx="$idx" 'NR==idx { print; exit }'
}

check_regression() {
    local label="$1" new_value="$2" baseline="$3"
    awk -v new="$new_value" -v base="$baseline" -v thresh="$PERF_REGRESSION_THRESHOLD" -v label="$label" '
        BEGIN {
            limit = base * (1 + thresh)
            delta = new - base
            pct = (base != 0) ? (delta / base) * 100 : 0
            if (new > limit) {
                format = "performance regression: %s = %s exceeds baseline median %s by %+g (%+.1f%%, threshold %.0f%%)"
                printf "WARNING: " format "\n", label, new, base, delta, pct, thresh * 100 > "/dev/stderr"
                printf "::warning title=Performance regression::" format "\n", label, new, base, delta, pct, thresh * 100
                exit 0
            }
            printf "OK: %s = %s (baseline median %s, delta %+g, %+.1f%%)\n", label, new, base, delta, pct
        }
    '
}

for gated_metric in compile_milliseconds bench_parse_median_ns_per_file; do
    gated_value=$([[ "$gated_metric" == "compile_milliseconds" ]] && echo "$COMPILE_MILLISECONDS" || echo "$bench_parse_median_ns_per_file")
    if baseline=$(median_of "$gated_metric"); then
        check_regression "$gated_metric" "$gated_value" "$baseline"
    else
        median_status=$?
        if [[ "$median_status" -ne 1 ]]; then
            exit "$median_status"
        fi
        echo "record_perf.sh: no prior $gated_metric history for runner '$RUNNER_NAME' config '$CONFIG' yet; skipping regression check"
    fi
done

row_prefix="ts=$(date +%s) runner=$RUNNER_NAME config=$CONFIG commit=$COMMIT_SHA"

if [[ "$PUSH_HISTORY" == "1" ]]; then
    pushed=0
    for attempt in 1 2 3 4 5; do
        if [[ "$attempt" -gt 1 ]]; then
            # Someone else pushed between our read and our write: resync
            # onto their tip and re-append our rows on top of it.
            if ! git -C "$work_dir" fetch -q origin "$HISTORY_BRANCH" 2>/dev/null; then
                echo "record_perf.sh: failed to resync perf history before push retry" >&2
                exit 1
            fi
            git -C "$work_dir" checkout -q -B "$HISTORY_BRANCH" FETCH_HEAD
        fi

        for row in "${new_rows[@]}"; do
            echo "$row_prefix $row" >> "$history_path"
        done
        git -C "$work_dir" add "$HISTORY_FILE"
        git -C "$work_dir" -c user.name="buster-ci" -c user.email="ci@buster.dev" \
            commit -q -m "perf: record $RUNNER_NAME/$CONFIG @ $COMMIT_SHA"

        if git -C "$work_dir" push -q origin "HEAD:$HISTORY_BRANCH"; then
            pushed=1
            break
        fi

        echo "record_perf.sh: push attempt $attempt failed, retrying..." >&2
        sleep "$((attempt * 2))"
    done

    if [[ "$pushed" -ne 1 ]]; then
        echo "record_perf.sh: failed to push perf history after retries" >&2
        exit 1
    fi

    echo "record_perf.sh: recorded ${#new_rows[@]} rows to $HISTORY_BRANCH"
else
    echo "record_perf.sh: PUSH_HISTORY not set; comparing only, not persisting this run"
fi

exit 0
