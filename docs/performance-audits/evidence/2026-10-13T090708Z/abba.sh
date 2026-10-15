#!/usr/bin/env bash
# abba.sh IDE OUTDIR PAIRS : ABBA pairs of the unity stage-1 compile, B = -fcanonical-inline
set -euo pipefail
IDE=$1; OUT=$2; PAIRS=$3; T=$(dirname "$0")
mkdir -p "$OUT"
BASE=(cc -march=native -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -ftime-canonical-fast -v)
run() { # variant index
    local v=$1 i=$2 extra=()
    [[ $v == b ]] && extra=(-fcanonical-inline)
    taskset -c ${CPU:-2} python3 -I "$T/rus.py" "$OUT/r-$v-$i.json" "$OUT/v-$v-$i.txt" "$OUT/e-$v-$i.txt" -- \
        "$IDE" "${BASE[@]}" "${extra[@]}" -fmetrics-out="$OUT/m-$v-$i.txt" -o "$OUT/bin-$v" > /dev/null
}
run a warm; run b warm
for i in $(seq 1 "$PAIRS"); do
    if (( i % 2 )); then run a "$i"; run b "$i"; else run b "$i"; run a "$i"; fi
done
echo complete
