#!/usr/bin/env bash
# Invocation glue: run one command inside the normal exclusive.sh lock.
set -euo pipefail
export PATH=/usr/bin:/bin
task_root=/home/david/buster-fast-bench/codex-117-01a16812
date -u +'%Y-%m-%dT%H:%M:%SZ acquired'
limit=${BUSTER_117_WINDOW_LIMIT_EPOCH:-0}
if [[ "$limit" != 0 && ! "$limit" =~ ^[1-9][0-9]{0,9}$ ]]; then
    printf 'Invalid acceptance window deadline: %s\n' "$limit" >&2
    exit 2
fi
if [ "$limit" -gt 0 ]; then
    start_file="$task_root/evidence/window-start.epoch"
    if [ ! -e "$start_file" ]; then
        date +%s > "$start_file"
    fi
    read -r started < "$start_file"
    deadline=$((started + 600))
    if [ "$limit" -lt "$deadline" ]; then deadline=$limit; fi
    remaining=$((deadline - $(date +%s) - 2))
    printf 'Window start=%s, deadline=%s, remaining=%ss\n' "$started" "$deadline" "$remaining"
    if [ "$remaining" -le 0 ]; then
        printf 'Acceptance window ended before this command started.\n' >&2
        exit 124
    fi
    exec timeout --signal=TERM --kill-after=1s "${remaining}s" "$@"
fi
exec "$@"
