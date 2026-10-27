#!/usr/bin/env bash
# A temporary probe (not part of the compiler), its ninth round: lanes when the process may run on more CPUs than it
# gets time for (a cgroup CPU quota, as containers set, and forced lanes on one CPU), and flat profiles of the
# preprocessor's main loop, the -E printer and Tier 0's compile on sqlite3.c. Everything goes to standard output between
# "=== " headers.
set -uo pipefail
root="$PWD"
work="$RUNNER_TEMP/probe"
mkdir -p "$work/real"
compiler="$root/build/compiler"
perf="${PERF:-perf}"

section()
{
    echo
    echo "=== $*"
}

lscpu | grep -E 'Model name|^CPU\(s\)'
nproc
cat /sys/fs/cgroup/cgroup.controllers 2> /dev/null
cd "$work"
curl -sSL --retry 3 -o real/sqlite.zip https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' real/sqlite.zip real
sqlite="$work/real/sqlite-amalgamation-3530200/sqlite3.c"
cc -std=c17 -O2 -o generator "$root/.github/perf-probe/gen.c"
./generator functions 20000 > functions.c

# Runs "$@" five times, printing each run's wall, user and system seconds, under the prefix in `limit`.
timed()
{
    local label="$1"
    shift
    for run in 1 2 3 4 5; do
        /usr/bin/time -f '%e %U %S' -o time.txt "${limit[@]}" "$@" > /dev/null 2> run.err
        local status=$?
        read -r wall user system < time.txt
        echo "  $label run $run: status $status, wall $wall s, user $user s, system $system s $(grep -m1 -i 'error' run.err | head -c 120)"
    done
}

for setting in none quota100 quota200 taskset1; do
    case "$setting" in
        none) limit=() ;;
        quota100) limit=(sudo systemd-run --scope --quiet -p CPUQuota=100% --uid="$(id -u)" --gid="$(id -g)" --setenv=HOME="$HOME" --) ;;
        quota200) limit=(sudo systemd-run --scope --quiet -p CPUQuota=200% --uid="$(id -u)" --gid="$(id -g)" --setenv=HOME="$HOME" --) ;;
        taskset1) limit=(taskset -c 0) ;;
    esac
    section "lanes, $setting"
    "${limit[@]}" sh -c 'echo "affinity $(nproc), cpu.max $(cat /sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup)/cpu.max 2> /dev/null)"'
    timed "sqlite3.c -c default lanes" "$compiler" -c "$sqlite" -o "$work/sqlite.o"
    timed "sqlite3.c -c --lanes 1" "$compiler" --lanes 1 -c "$sqlite" -o "$work/sqlite.o"
    timed "functions.c -o default lanes" "$compiler" --lower-all functions.c -o "$work/functions"
    timed "functions.c -o --lanes 1" "$compiler" --lower-all --lanes 1 functions.c -o "$work/functions"
    if [ "$setting" = taskset1 ]; then
        timed "functions.c -o --lanes 4" "$compiler" --lower-all --lanes 4 functions.c -o "$work/functions"
    fi
done

section "benchmark-streaming on one CPU and on four"
taskset -c 0 "$compiler" benchmark-streaming --units 100 --runs 5 | tail -n 6
taskset -c 0 "$compiler" benchmark-streaming --units 100 --runs 5 --lanes 1 | tail -n 6
taskset -c 0-3 "$compiler" benchmark-streaming --units 100 --runs 5 --lanes 4 --cpus 0,1 | tail -n 6

profile()
{
    local name="$1"
    shift
    section "profile $name"
    "$perf" record -q -o "$work/$name.data" -F 4999 -- "$@" > "$work/$name.out" 2>&1
    tail -n 30 "$work/$name.out" | cut -c 1-200
    "$perf" report -q -i "$work/$name.data" --no-children --sort symbol --percent-limit 0.7 --stdio 2> /dev/null | grep -v '^$' | head -n 40 | cut -c 1-150
}

profile preprocessor taskset -c 1 "$compiler" benchmark-preprocessor --only vectorized "$sqlite"
profile print taskset -c 1 "$compiler" -E -P "$sqlite" -o "$work/sqlite.i"
profile backend taskset -c 1 "$compiler" benchmark-backend-source --only vector "$sqlite"
profile compile taskset -c 1 "$compiler" --lanes 1 -c "$sqlite" -o "$work/sqlite.o"
section "done"
