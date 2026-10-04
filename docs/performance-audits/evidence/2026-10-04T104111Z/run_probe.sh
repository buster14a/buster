#!/usr/bin/env bash
# Validate-then-discard probe for the C frontend. Runs ONLY on a hosted runner.
#
# Builds the work-ledger subject pair (ledger + plain compiler) from the pinned
# checkout and from a probe checkout whose only change is apply_probe.py, then
# for each workload and subject records: exact work-ledger counters, output
# hashes, deterministic callgrind instruction counts with inclusive per-function
# attribution, and alternating wall-time pairs.
#
# usage: run_probe.sh <pinned-root> <probe-root> <output-dir> [pairs] [sqlite-root]
set -euo pipefail
ROOT=$(realpath "$1")
PROBE_SRC=$(realpath "$2")
OUT=$(realpath -m "$3")
PAIRS=${4:-8}
SQLITE_ROOT=${5:-}
mkdir -p "$OUT"

{
    echo "pinned_head=$(git -C "$ROOT" rev-parse HEAD)"
    echo "pinned_tree=$(git -C "$ROOT" rev-parse 'HEAD^{tree}')"
    echo "probe_head=$(git -C "$PROBE_SRC" rev-parse HEAD)"
    echo "probe_diffstat=$(git -C "$PROBE_SRC" diff --stat | tr '\n' ' ')"
    echo "clang=$(clang --version | head -1)"
    echo "valgrind=$(valgrind --version)"
    echo "kernel=$(uname -r)"
    echo "nproc=$(nproc)"
    echo "cpu=$(lscpu | grep 'Model name' | sed 's/.*: *//')"
    echo "mem_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')"
    echo "pairs=$PAIRS"
    echo "sqlite_root=$SQLITE_ROOT"
} > "$OUT/identity.txt"
git -C "$PROBE_SRC" diff > "$OUT/probe.diff"
cat "$OUT/identity.txt"

echo "== building subjects =="
bash "$ROOT/tools/work_ledger/build_subject.sh" "$ROOT" "$OUT/base"
bash "$ROOT/tools/work_ledger/build_subject.sh" "$PROBE_SRC" "$OUT/probe"
sha256sum "$OUT"/base/ide-* "$OUT"/probe/ide-* > "$OUT/compilers.sha256"

# workload name -> cwd and argv (without -o)
declare -A WL_CWD WL_ARGS
WL_CWD[self]="$ROOT"
WL_ARGS[self]="cc -Isrc -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -march=znver3 src/buster/apps/ide/ide.c -lm"
WORKLOADS=(self)
if [[ -n "$SQLITE_ROOT" && -f "$SQLITE_ROOT/sqlite3.c" ]]; then
    WL_CWD[sqlite]="$SQLITE_ROOT"
    WL_ARGS[sqlite]="cc -g0 -O2 -I. -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_ENABLE_COLUMN_METADATA -march=znver3 -c sqlite3.c"
    WORKLOADS+=(sqlite)
fi

: > "$OUT/outputs.sha256"
: > "$OUT/statuses.txt"
record_status() { echo "$1=$2" >> "$OUT/statuses.txt"; }

for W in "${WORKLOADS[@]}"; do
    cd "${WL_CWD[$W]}"
    # shellcheck disable=SC2206
    ARGS=(${WL_ARGS[$W]})
    for S in base probe; do
        echo "== $W / $S: ledger run =="
        set +e
        "$OUT/$S/ide-ledger" "${ARGS[@]}" "-fsource-metrics=$OUT/$W-$S.metrics" -o "$OUT/$W-$S-ledger.out" > "$OUT/$W-$S-ledger.log" 2>&1
        record_status "$W-$S-ledger" $?
        echo "== $W / $S: plain run =="
        "$OUT/$S/ide-plain" "${ARGS[@]}" -o "$OUT/$W-$S-plain.out" > "$OUT/$W-$S-plain.log" 2>&1
        record_status "$W-$S-plain" $?
        set -e
        sha256sum "$OUT/$W-$S-ledger.out" "$OUT/$W-$S-plain.out" >> "$OUT/outputs.sha256" || true
    done
    echo "== $W: syntax-only ledger (base) =="
    set +e
    SYN=()
    for a in "${ARGS[@]}"; do [[ "$a" == "-c" || "$a" == "-lm" ]] || SYN+=("$a"); done
    "$OUT/base/ide-ledger" "${SYN[@]}" -fsyntax-only "-fsource-metrics=$OUT/$W-base-syntaxonly.metrics" > "$OUT/$W-base-syntaxonly.log" 2>&1
    record_status "$W-base-syntaxonly" $?
    set -e
    for S in base probe; do
        echo "== $W / $S: callgrind =="
        set +e
        valgrind --tool=callgrind --cache-sim=no --branch-sim=no "--callgrind-out-file=$OUT/$W-$S.callgrind" \
            "$OUT/$S/ide-plain" "${ARGS[@]}" -o "$OUT/$W-$S-cg.out" > "$OUT/$W-$S-callgrind.log" 2>&1
        record_status "$W-$S-callgrind" $?
        set -e
        sha256sum "$OUT/$W-$S-cg.out" >> "$OUT/outputs.sha256" || true
        rm -f "$OUT/$W-$S-cg.out"
        callgrind_annotate --inclusive=yes --threshold=99.99 "$OUT/$W-$S.callgrind" > "$OUT/$W-$S.inclusive.txt" 2>&1 || true
        callgrind_annotate --threshold=99.99 "$OUT/$W-$S.callgrind" > "$OUT/$W-$S.exclusive.txt" 2>&1 || true
    done
    echo "== $W: wall-time pairs =="
    echo "workload,pair,position,subject,wall_s,user_s,sys_s,maxrss_kb,sha256" > "$OUT/$W-timing.csv"
    for ((i = 0; i < PAIRS; i++)); do
        if (( i % 2 == 0 )); then ORDER=(base probe); else ORDER=(probe base); fi
        pos=0
        for S in "${ORDER[@]}"; do
            TMP="$OUT/$W-timing-tmp.out"
            set +e
            taskset -c 1 /usr/bin/time -f "%e,%U,%S,%M" -o "$OUT/time.tmp" "$OUT/$S/ide-plain" "${ARGS[@]}" -o "$TMP" > /dev/null 2>&1
            set -e
            H=$(sha256sum "$TMP" 2>/dev/null | cut -d' ' -f1 || echo missing)
            echo "$W,$i,$pos,$S,$(cat "$OUT/time.tmp"),$H" >> "$OUT/$W-timing.csv"
            rm -f "$TMP"
            pos=$((pos + 1))
        done
    done
    rm -f "$OUT/time.tmp"
done
rm -f "$OUT"/*-ledger.out "$OUT"/*-plain.out
cd "$OUT" && sha256sum ./*.metrics ./*.callgrind ./*.txt ./*.csv ./*.diff > "$OUT/SHA256SUMS"
echo "PROBE_DONE"
