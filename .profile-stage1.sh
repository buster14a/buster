#!/bin/bash
# Profile a stage-1 self-compile with the trusted Clang Release binary.
# Produces a symbolized, self-time-ranked function histogram.
# Usage: .profile-stage1.sh <output-prefix>
set -u
ROOT=/home/david/dev/buster/.claude/worktrees/suspicious-lalande-55eade
cd "$ROOT" || exit 1
PREFIX="${1:-stage1}"
DATA=/tmp/$PREFIX.data

# Single lane so samples are not smeared across workers.
BUSTER_TEST_JOBS=1 perf record -F 999 -g --call-graph fp -o "$DATA" -- \
    ./build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 \
    -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -o /tmp/$PREFIX-ide \
    >/tmp/$PREFIX-compile.log 2>&1

echo "=== total samples ==="
perf report -i "$DATA" --stdio -q 2>/dev/null | head -1 >/dev/null
perf script -i "$DATA" --no-demangle -F comm,ip,sym,symoff,dso 2>/dev/null > /tmp/$PREFIX.script
wc -l < /tmp/$PREFIX.script

echo "=== self time by symbol (top 40) ==="
# Leaf frame of each sample = first line of each sample block.
awk 'NF==0{blank=1;next} blank||NR==1{print $2; blank=0}' /tmp/$PREFIX.script \
  | sed 's/+0x.*//' | sort | uniq -c | sort -rn | head -40
