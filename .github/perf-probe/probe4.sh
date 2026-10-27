#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its fourth round: concurrent compiles of sqlite3.c with the default lanes
# and with one. Everything goes to standard output between "=== " headers, for reading from the job's log.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work"
compiler="$root/build/compiler"
perf_bin="${PERF:-perf}"

section()
{
    echo
    echo "=== $*"
}

now()
{
    printf -v "$1" %s "${EPOCHREALTIME//[!0-9]/}"
}

section "cpu"
lscpu | sed -n '1,20p'
uname -r
cat /sys/kernel/mm/transparent_hugepage/enabled

sqlite_dir="$work/sqlite"
mkdir -p "$sqlite_dir"
curl -sSL --retry 3 -o "$work/sqlite.zip" https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$work/sqlite.zip" "$sqlite_dir"
sqlite="$sqlite_dir/sqlite-amalgamation-3530200"
printf '%s  %s\n' 0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d "$sqlite/sqlite3.c" | sha256sum --check
options=(-c -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE "-I$sqlite" "$sqlite/sqlite3.c")
mkdir -p "$work/small"
printf '' > "$work/small/empty.c"

# K compiles of sqlite3.c at once, each with the default lanes or with --lanes 1, in turn: the batch's wall time and the
# CPU time its processes took.
batch()
{
    local k="$1" mode="$2" round="$3" start end
    local lanes=()
    [ "$mode" = one ] && lanes=(--lanes 1)
    rm -f "$work"/batch-*.time
    now start
    for i in $(seq "$k"); do
        /usr/bin/time -f '%e %U %S' -o "$work/batch-$i.time" "$compiler" "${lanes[@]}" "${options[@]}" -o "$work/batch-$i.o" &
    done
    wait
    now end
    cat "$work"/batch-*.time | awk -v k="$k" -v mode="$mode" -v round="$round" -v wall="$(((end - start) / 1000))" \
        '{ user_time += $2; system_time += $3; if ($1 > slowest) slowest = $1 } END { printf "k=%d lanes=%s round=%d batch_wall_ms=%d slowest_s=%.3f user_s=%.3f system_s=%.3f\n", k, mode, round, wall, slowest, user_time, system_time }'
}

section "concurrent compiles of sqlite3.c"
for k in 1 4 8; do
    for round in 1 2 3 4 5; do
        if [ $((round % 2)) = 1 ]; then
            batch "$k" default "$round"
            batch "$k" one "$round"
        else
            batch "$k" one "$round"
            batch "$k" default "$round"
        fi
    done
done

section "done"
