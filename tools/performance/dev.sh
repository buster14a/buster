#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
set -euo pipefail

usage()
{
    cat <<'EOF'
Usage: tools/performance/dev.sh [--9700x CHECKOUT] [--out NEW_DIR] COMMAND ...
  build [--trace] [--report] [--config CONFIG] [--jobs N] [--target TARGET]...
  paths [--only direct|tier0|tier1] [--compiler FILE] [--runs N] [--cpu N]
        [--events PERF_EVENTS] [--record] [--run] [--arg ARG]... SOURCE.c [-- CC_FLAGS...]
  asm BINARY SYMBOL [--uops instructions.xml] [--mca znver4] [--mca znver5]

build defaults to Release. paths measures -c, verifies straight-to-ELF admission,
and optionally links and times the resulting programs. asm accepts objects or executables.
--9700x uses the specified checkout on benchpress and copies the results back.
Paths are relative to the repository root on the execution host. See docs/commands/profiling.md.
EOF
}
die() { echo "dev.sh: $*" >&2; exit 1; }
root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
cd "$root"
original=("$@")
out="" remote=""
while (($#)); do
    case "$1" in
        --9700x|--out)
            (($# >= 2)) && [[ -n $2 ]] || die "$1 needs a value"
            case "$1" in --9700x) remote=$2 ;; --out) out=$2 ;; esac
            shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) break ;;
    esac
done
action=${1:-}
case "$action" in build|paths|asm) shift ;; *) usage >&2; exit 2 ;; esac
if [[ ${1:-} = --help && $action != asm ]]; then usage; exit 0; fi
if [[ -n $out ]]; then
    mkdir -p -- "$(dirname -- "$out")"
    mkdir -- "$out" || die "--out needs a new directory"
else
    mkdir -p build/perf-runs
    out=$(mktemp -d "build/perf-runs/$action.XXXXXXXX")
fi
out=$(realpath -- "$out")
echo "Results: $out" >&2

if [[ -n $remote ]]; then
    # Use a generated remote directory even for an explicitly named local result.
    # Keep the remote copy so interrupted transfers can be resumed by hand.
    remote_out="build/perf-runs/$action.$(cat /proc/sys/kernel/random/uuid)"
    command -v tailscale >/dev/null || die "--9700x needs tailscale on PATH"
    transport=(tailscale ssh david@benchpress)
    printf -v change_dir 'cd -- %q' "$remote"
    printf -v command '%q ' env BUSTER_PERF_BENCHPRESS=1 \
        "BUSTER_PERF_TIMEOUT=${BUSTER_PERF_TIMEOUT:-}" \
        "EXCLUSIVE_OWNER=${EXCLUSIVE_OWNER:-${USER:-developer}-dev}" \
        "EXCLUSIVE_TITLE=${EXCLUSIVE_TITLE:-Buster $action}" \
        "EXCLUSIVE_LINK=${EXCLUSIVE_LINK:-https://github.com/buster14a/buster}" \
        tools/performance/dev.sh --out "$remote_out" "$action" "$@"
    echo "benchpress: $remote/$remote_out" >&2
    status=0
    "${transport[@]}" "$change_dir && $command" || status=$?
    printf -v archive 'tar -C %q -cf - .' "$remote_out"
    if ! "${transport[@]}" "$change_dir && $archive" | tar -C "$out" -xf -; then
        ((status == 0)) || exit "$status"
        die "could not retrieve results from $remote/$remote_out"
    fi
    exit "$status"
fi

benchpress=${BUSTER_PERF_BENCHPRESS:-0}
if [[ $(uname -n) = benchpress ]]; then benchpress=1; fi
if [[ $benchpress = 1 ]]; then
    export EXCLUSIVE_OWNER=${EXCLUSIVE_OWNER:-${USER:-developer}-dev}
    export EXCLUSIVE_TITLE=${EXCLUSIVE_TITLE:-Buster $action}
    export EXCLUSIVE_LINK=${EXCLUSIVE_LINK:-https://github.com/buster14a/buster}
    export BUSTER_HOST_ARCH=${BUSTER_HOST_ARCH:-znver5}
fi
{
    git rev-parse HEAD
    git status --short
    uname -srnm
    awk '/^model name/ { print; exit }' /proc/cpuinfo
    printf '%q ' "${original[@]}"
    printf '\n'
} > "$out/context.txt"

# Keep the queue wait outside the reported time. The one-core command chooses
# a CPU *inside* its local grant; benchpress's dedicated measurement CPU is 2.
cpu=""
run_one()
{
    local kind=$1
    shift
    local prefix=()
    if [[ $kind = build ]] && command -v cores >/dev/null && [[ -z ${CORES:-} ]]; then
        if [[ -n $jobs ]]; then prefix+=(cores -n "$jobs" --); else prefix+=(cores --all --); fi
    elif [[ $kind = single && $benchpress != 1 && -z $cpu && -z ${CORES:-} ]] && command -v cores >/dev/null; then
        prefix+=(cores -n 1 --)
    fi
    if [[ $benchpress = 1 ]]; then
        prefix+=("$HOME/buster-fast-bench/exclusive.sh")
        prefix+=(bash -c '
            log=$1; shift
            snapshot() {
                { date -Is; cat /proc/loadavg; ps -eo psr,pcpu,pid,etimes,user,comm --sort=-pcpu | head -11; } >> "$log"
            }
            snapshot
            trap snapshot EXIT
            "$@"
        ' bash "$out/host-load.txt")
    fi
    local limit=${BUSTER_PERF_TIMEOUT:-}
    if [[ -z $limit && $benchpress = 1 ]]; then
        if [[ $kind = build ]]; then limit=1800; else limit=300; fi
    fi
    if [[ -n $limit ]]; then prefix+=(timeout -k 5 "$limit"); fi
    if [[ $kind = single ]]; then
        prefix+=(bash -c '
            cpu=$1; shift
            if [[ -z $cpu ]]; then
                read -r cpu < <(awk "/^Cpus_allowed_list:/ {print \$2}" /proc/self/status)
                cpu=${cpu%%[-,]*}
            fi
            echo "pinned CPU $cpu" >&2
            exec taskset -c "$cpu" "$@"
        ' bash "${cpu:-${bench_cpu:-}}")
    fi
    { printf '%q ' "${prefix[@]}" "$@"; printf '\n'; } >> "$out/commands.sh"
    "${prefix[@]}" "$@"
}
bench_cpu=""
[[ $benchpress != 1 ]] || bench_cpu=2

case "$action" in
    build)
        trace=0 report=0 jobs=""
        args=(--config Release)
        while (($#)); do
            case "$1" in
                --trace) trace=1; shift ;;
                --report) report=1; shift ;;
                --jobs|-j) (($# >= 2)) || die "$1 needs a count"; jobs=$2; shift 2 ;;
                --config|--target) (($# >= 2)) || die "$1 needs a value"; args+=("$1" "$2"); shift 2 ;;
                --verbose|-v) args+=("$1"); shift ;;
                *) die "unknown build option $1" ;;
            esac
        done
        if [[ -n $jobs ]]; then
            [[ $jobs =~ ^[1-9][0-9]{0,3}$ ]] || die "--jobs needs a positive count"
            args+=(--jobs "$jobs")
        fi
        timing=off
        if ((trace && report)); then timing=both
        elif ((trace)); then timing=trace
        elif ((report)); then timing=report
        fi
        run_one build env "BUSTER_BUILD_TIMINGS_DIR=$out" "BUSTER_CLANG_TIMING=$timing" \
            bash -c 'TIMEFORMAT="build total %3R s wall, %3U s user, %3S s system"; time "$@"' \
            bash ./build.sh "${args[@]}" 2>&1 | tee "$out/build.log"
        cat "$out/phases.tsv"
        if [[ -s $out/edges.tsv ]]; then
            echo "Longest Ninja steps (overlapping jobs, milliseconds)"
            head -1 "$out/edges.tsv"
            tail -n +2 "$out/edges.tsv" | sort -t $'\t' -k3,3nr | sed -n '1,10p'
        fi ;;
    paths)
        # shellcheck source=tools/performance/paths.sh
        source "$root/tools/performance/paths.sh" ;;
    asm) run_one single python3 tools/performance/function.py "$@" | tee "$out/function.txt" ;;
esac
