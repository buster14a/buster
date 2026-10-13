#!/usr/bin/env bash
# Usage: identity.sh BASELINE_IDE CANDIDATE_IDE WORK_DIR. Links every cell with
# both binaries; prints one TSV row per cell: cell, baseline sha256,
# candidate sha256, identical, baseline output, candidate output, reference output.
set -euo pipefail
a=$1; b=$2; cd "$3"
printf 'cell\tbaseline_sha256\tcandidate_sha256\tidentical\tbaseline_stdout\tcandidate_stdout\treference_stdout\n'
row()
{
    local cell=$1 ref=$2; shift 2
    "$a" cc "$@" -o "id-a-$cell"; "$b" cc "$@" -o "id-b-$cell"
    local ha hb same oa ob
    ha=$(sha256sum "id-a-$cell" | cut -d' ' -f1); hb=$(sha256sum "id-b-$cell" | cut -d' ' -f1)
    same=no; [ "$ha" = "$hb" ] && same=yes
    oa=$("./id-a-$cell"); ob=$("./id-b-$cell")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$cell" "$ha" "$hb" "$same" "$oa" "$ob" "$ref"
}
for n in 256 1024 4096 16384; do
    clang main.o "m$n.o" -o "ref-$n"; ref=$("./ref-$n")
    for mode in -no-pie -pie; do
        row "m$n.o$mode" "$ref" "$mode" main.o "m$n.o"
        row "n$n.o$mode" "$ref" "$mode" main.o "n$n.o"
        row "m$n.a$mode" "$ref" "$mode" main.o "m$n.a"
    done
done
clang main.o plain.o -o ref-plain; ref=$(./ref-plain)
for mode in -no-pie -pie; do row "plain.o$mode" "$ref" "$mode" main.o plain.o; done
