#!/bin/bash
set -euo pipefail
root=/home/david/buster-fast-bench/codex-116
cd "$root/fifth"
perf record -q -e cycles:u -c 10000 -o "$root/eighth-window128.perf" -- \
    taskset -c 2 "$root/seventh-window128" --only vectorized "$root/inputs/sqlite3.c"
perf report --stdio --no-children --sort symbol --percent-limit 1 -i "$root/eighth-window128.perf"
perf annotate --stdio --no-source --symbol refine_pass_b_64_plain -i "$root/eighth-window128.perf"
perf annotate --stdio --no-source --symbol pp_prepare_into -i "$root/eighth-window128.perf"
