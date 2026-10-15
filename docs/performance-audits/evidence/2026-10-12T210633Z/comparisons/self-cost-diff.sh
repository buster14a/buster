#!/usr/bin/env bash
# Self-cost difference by function between two Callgrind outputs, as the
# audit's per-function tables were taken: callgrind_annotate (Valgrind 3.22.0)
# self Ir per file:function, joined, rows whose difference exceeds 100,000 Ir.
# Usage: self-cost-diff.sh base.out[.gz] candidate.out[.gz]
set -euo pipefail
table() {
    local input=$1
    if [[ $input == *.gz ]]; then
        local plain
        plain=$(mktemp)
        gzip -dc "$input" > "$plain"
        input=$plain
    fi
    callgrind_annotate --auto=no --threshold=100 "$input" 2>/dev/null |
        grep -E '^ *[0-9,]+ +\(' |
        sed -E 's/^ *([0-9,]+) +\([^)]*\) +(.*)$/\1\t\2/; s/,//g' |
        sed -E 's| \[[^]]*\]$||' |
        awk -F'\t' '{print $2"\t"$1}' | LC_ALL=C sort -t$'\t' -k1,1
}
join -t$'\t' -a1 -a2 -e0 -o 0,1.2,2.2 <(table "$1") <(table "$2") |
    awk -F'\t' '{d=$3-$2; if (d>100000||d<-100000) print d"\t"$1}' | LC_ALL=C sort -n
