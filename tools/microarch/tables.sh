#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The report's tables from the raw results (run.sh's summarize step first):
#   tools/microarch/tables.sh build/microarch/results/zen4-7940hs:6 build/microarch/results/zen5-9700x:8 > tables.md
# Each argument is a host's results directory and its dispatch width. UARCH names
# the uarch tool (build/microarch/uarch by default, built here when missing).
set -u
export LC_ALL=C
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
UARCH=${UARCH:-$here/../../build/microarch/uarch}
if [[ ! -x $UARCH || $here/uarch.c -nt $UARCH ]]; then
    mkdir -p "$(dirname "$UARCH")"
    clang -std=c17 -O2 -Wall -Wextra -Werror -o "$UARCH" "$here/uarch.c" -lm || exit 2
fi
SQLITE_BYTES=9507037

hosts=("$@")
name() { basename "${1%%:*}"; }
dir() { echo "${1%%:*}"; }
width() { echo "${1##*:}"; }
# True when some host's results have FILE.
any_file() { local h; for h in "${hosts[@]}"; do [[ -f $(dir "$h")/$1 ]] && return 0; done; return 1; }

if any_file time/time-summary.tsv; then
    echo "## Whole processes (medians of 11 rotated runs; MB/s of sqlite3.c's 9,507,037 source bytes)"
    echo
    echo "| host | run | wall ms | user ms | system ms | peak RSS MB | minor faults | MB/s |"
    echo "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"
    for h in "${hosts[@]}"; do
        f=$(dir "$h")/time/time-summary.tsv
        [[ -f $f ]] || continue
        awk -F'\t' -v host="$(name "$h")" -v bytes=$SQLITE_BYTES 'NR > 1 && $1 !~ /^stages/ {
            printf "| %s | %s | %.1f | %.1f | %.1f | %.0f | %.0f | %s |\n", host, $1, $4, $7, $8, $9 / 1024, $10,
                ($1 ~ /sq-/ ? sprintf("%.1f", bytes / $4 / 1000) : "") }' "$f"
    done
    echo
fi

files=()
labels=()
for h in "${hosts[@]}"; do
    for f in "$(dir "$h")"/stages-*.tsv; do
        [[ -f $f && $f != *stages-self* ]] || continue
        files+=("$f")
        labels+=("$(name "$h") $(basename "$f" .tsv | sed 's/stages-//')")
    done
done
if [[ ${#files[@]} -gt 0 ]]; then
    echo "## Stages (--time-stages, ms, medians of 11 runs, --lanes 1)"
    echo
    printf "| stage |"
    for label in "${labels[@]}"; do printf ' %s |' "$label"; done
    echo
    printf "| --- |"
    for label in "${labels[@]}"; do printf ' ---: |'; done
    echo
    awk -F'\t' '
        FNR == 1 { file += 1 }
        $1 == "time-stage" && $3 == "ms" { if (!($2 in seen)) { seen[$2] = 1; order[++n] = $2 } value[$2, file] = $4 }
        END { for (i = 1; i <= n; i += 1) { s = order[i]; line = "| " s " |"; any = 0
              for (f = 1; f <= file; f += 1) { v = value[s, f]; if (v + 0 >= 0.05) any = 1; line = line sprintf(" %.2f |", v) }
              if (any) print line } }' "${files[@]}"
    echo
fi

if any_file groups/stat.tsv; then
    echo "## Top-down and rates from fixed groups (user mode, medians of 5 runs, each group one schedule)"
    echo
    echo "| host | build.workload | M instr | M cycles | IPC | retiring | frontend | frontend latency | backend | backend memory | bad spec | microcoded ops | branch MPKI | IC miss PKI | op cache hit | L2 miss (DC) PKI | DRAM fill PKI | L1 DTLB miss PKI |"
    echo "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    for h in "${hosts[@]}"; do
        f=$(dir "$h")/groups/stat.tsv
        [[ -f $f ]] || continue
        awk -F'\t' -v host="$(name "$h")" -v W="$(width "$h")" 'NR > 1 {
            n = split($1, a, "."); key = a[1]; for (j = 2; j < n; j += 1) key = key "." a[j]; v[key, $2] = $3; keys[key] = 1 }
            END { for (k in keys) { c = v[k, "ls_not_halted_cyc:u"]; s = c * W; i = v[k, "instructions:u"]; if (s == 0 || i == 0) continue
                # A build and workload whose six groups are not all there yet.
                if (v[k, "ex_ret_ops:u"] == 0 || v[k, "ex_no_retire.not_complete:u"] == 0 || v[k, "op_cache_hit_miss.all_op_cache_accesses:u"] == 0 || v[k, "ex_ret_brn:u"] == 0) continue
                be = v[k, "de_no_dispatch_per_slot.backend_stalls:u"] / s
                latency = 0; for (key2 in v) { split(key2, parts, SUBSEP); if (parts[1] == k && parts[2] ~ /cmask/) latency = v[key2] }
                printf "| %s | %s | %.0f | %.0f | %.2f | %.1f%% | %.1f%% | %.1f%% | %.1f%% | %.1f%% | %.1f%% | %.2f%% | %.2f | %.2f | %.1f%% | %.2f | %.2f | %.2f |\n", host, k,
                    i / 1e6, v[k, "cycles:u"] / 1e6, i / v[k, "cycles:u"], 100 * v[k, "ex_ret_ops:u"] / s,
                    100 * v[k, "de_no_dispatch_per_slot.no_ops_from_frontend:u"] / s, 100 * latency / v[k, "ls_not_halted_cyc:u"], 100 * be,
                    100 * be * v[k, "ex_no_retire.load_not_complete:u"] / v[k, "ex_no_retire.not_complete:u"],
                    100 * (v[k, "de_src_op_disp.all:u"] - v[k, "ex_ret_ops:u"]) / s, 100 * v[k, "ex_ret_ucode_ops:u"] / v[k, "ex_ret_ops:u"],
                    1000 * v[k, "ex_ret_brn_misp:u"] / i, 1000 * v[k, "ic_tag_hit_miss.instruction_cache_miss:u"] / i,
                    100 * (1 - v[k, "op_cache_hit_miss.op_cache_miss:u"] / v[k, "op_cache_hit_miss.all_op_cache_accesses:u"]),
                    1000 * v[k, "l2_cache_req_stat.ic_dc_miss_in_l2:u"] / i, 1000 * v[k, "ls_dmnd_fills_from_sys.dram_io_near:u"] / i,
                    1000 * v[k, "ls_l1_d_tlb_miss.all:u"] / i } }' "$f" | sort
    done
    echo
fi

if any_file groups/stat.tsv; then
    echo "## Kernel share (basic group: user and kernel cycles and instructions, medians of 5 runs)"
    echo
    echo "| host | build.workload | user cycles M | kernel cycles M | kernel share of cycles | task-clock ms | page faults |"
    echo "| --- | --- | ---: | ---: | ---: | ---: | ---: |"
    for h in "${hosts[@]}"; do
        f=$(dir "$h")/groups/stat.tsv
        [[ -f $f ]] || continue
        awk -F'\t' -v host="$(name "$h")" 'NR > 1 && $1 ~ /\.basic$/ { k = $1; sub(/\.basic$/, "", k); v[k, $2] = $3; keys[k] = 1 }
            END { for (k in keys) printf "| %s | %s | %.0f | %.0f | %.1f%% | %.1f | %.0f |\n", host, k, v[k, "cycles:u"] / 1e6, v[k, "cycles:k"] / 1e6,
                100 * v[k, "cycles:k"] / (v[k, "cycles:u"] + v[k, "cycles:k"]), v[k, "task-clock"], v[k, "page-faults"] }' "$f" | sort
    done
    echo
fi

for h in "${hosts[@]}"; do
    for p in "$(dir "$h")"/profiles/*/; do
        [[ -f $p/td1.tsv && -f $p/td2.tsv ]] || continue
        echo "## Per-function top-down, $(name "$h"), $(basename "$p") build (leader-sampled groups, functions with at least 0.7% of user cycles)"
        echo
        echo "| function | cycles | IPC | retiring | frontend | backend | backend memory | bad spec | mispredicts PKI |"
        echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
        "$UARCH" topdown "$p/td1.tsv" "$p/td2.tsv" "$(width "$h")" 0.7 | awk -F'\t' 'NR > 1 {
            printf "| `%s` | %.1f%% | %.2f | %.1f%% | %.1f%% | %.1f%% | %.1f%% | %.1f%% | %.2f |\n", $1, 100 * $2, $3, 100 * $4, 100 * $5, 100 * $6, 100 * $8, 100 * $7, $10 }'
        echo
    done
done

for h in "${hosts[@]}"; do
    for p in "$(dir "$h")"/profiles/*/; do
        [[ -f $p/lbr.tsv ]] || continue
        echo "## Mispredicted branches by site, $(name "$h"), $(basename "$p") build (LBR, 8 compiles)"
        echo
        head -1 "$p/lbr.tsv" | awk -F'\t' '{ printf "%s LBR entries, %s mispredicted\n\n", $2, $4 }'
        echo "| site | entries | mispredicted | rate | share of mispredicts |"
        echo "| --- | ---: | ---: | ---: | ---: |"
        awk -F'\t' '/^site/ { p = 1; next } p && n < 20 { n += 1; printf "| `%s` | %s | %s | %.1f%% | %.1f%% |\n", $1, $2, $3, 100 * $4, 100 * $5 }' "$p/lbr.tsv"
        echo
    done
done

for h in "${hosts[@]}"; do
    s=$(dir "$h")/scaling
    [[ -f $s/time-summary.tsv ]] || continue
    echo "## Scaling, $(name "$h") (CPUs $(cat "$s/cpus.txt"))"
    echo
    echo "| run | wall ms | user ms | system ms | peak RSS MB | minor faults |"
    echo "| --- | ---: | ---: | ---: | ---: | ---: |"
    awk -F'\t' 'NR > 1 { printf "| %s | %.1f | %.1f | %.1f | %.0f | %.0f |\n", $1, $4, $7, $8, $9 / 1024, $10 }' "$s/time-summary.tsv"
    echo
    echo "| independent compiles | batch wall ms (median of 4) | MB/s | user ms per compile | system ms per compile | package J per compile |"
    echo "| ---: | ---: | ---: | ---: | ---: | ---: |"
    for k in $(awk -F'\t' 'NR > 1 { print $3 }' "$s/fanout.tsv" | sort -n | uniq); do
        energy=$(cat "$s"/fanout-energy-"$k".r*.csv 2>/dev/null | awk -F, '$3 ~ /energy-pkg/ { print $1 }' | sort -n | awk '{ a[NR] = $1 } END { print (NR % 2 ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2) }')
        awk -F'\t' -v k="$k" -v bytes=$SQLITE_BYTES -v energy="$energy" 'NR > 1 && $3 == k { w[++n] = $5; u += $6; s += $7 }
            END { asort(w); m = n % 2 ? w[(n + 1) / 2] : (w[n / 2] + w[n / 2 + 1]) / 2
                  printf "| %d | %.1f | %.1f | %.1f | %.1f | %.2f |\n", k, m, k * bytes / m / 1000, u / n / k, s / n / k, energy / k }' "$s/fanout.tsv"
    done
    echo
done

for h in "${hosts[@]}"; do
    e=$(dir "$h")/energy
    [[ -f $e/idle.csv ]] || continue
    echo "## Energy and clock of single-core sq-c, $(name "$h") (10 compiles per build)"
    echo
    idle=$(awk -F, '$3 ~ /energy-pkg/ { print $1 / ($4 / 1e9) }' "$e/idle.csv")
    echo "Idle package power: $(printf '%.1f' "$idle") W."
    echo
    echo "| build | package J per compile | above idle J per compile | measured core J per compile | effective GHz (APERF/MPERF x TSC) | instructions per cycle |"
    echo "| --- | ---: | ---: | ---: | ---: | ---: |"
    for f in "$e"/*.freq.csv; do
        v=$(basename "$f" .freq.csv)
        tsc_mhz=$(awk -F, '$3 == "msr/tsc/" { t = $1 } END { print t }' "$f")
        awk -F, -v v="$v" -v idle="$idle" -v pkgfile="$e/$v.pkg.csv" -v corefile="$e/$v.core.csv" '
            $3 == "msr/aperf/" { a = $1 } $3 == "msr/mperf/" { m = $1 } $3 == "msr/tsc/" { t = $1; ns = $4 } $3 == "cycles" { c = $1 } $3 == "instructions" { i = $1 }
            END { while ((getline line < pkgfile) > 0) { split(line, p, ","); if (p[3] ~ /energy-pkg/) { pkg = p[1]; pns = p[4] } }
                  while ((getline line < corefile) > 0) { split(line, q, ","); if (q[5] ~ /energy-core/ && q[3] + 0 > best) best = q[3] + 0 }
                  printf "| %s | %.2f | %.2f | %.2f | %.2f | %.2f |\n", v, pkg / 10, (pkg - idle * pns / 1e9) / 10, best / 10, t / (ns / 1e9) * a / m / 1e9, i / c }' "$f"
    done
    echo
done

for h in "${hosts[@]}"; do
    for a in "$(dir "$h")"/ab*; do
        [[ -f $a/time-summary.tsv ]] || continue
        echo "## $(basename "$a"), $(name "$h"): builds of other commits against main (rotated rounds, with --time-stages)"
        echo
        echo "| run | runs | wall ms | user ms | system ms | peak RSS MB |"
        echo "| --- | ---: | ---: | ---: | ---: | ---: |"
        awk -F'\t' 'NR > 1 { printf "| %s | %s | %.1f | %.1f | %.1f | %.0f |\n", $1, $2, $4, $7, $8, $9 / 1024 }' "$a/time-summary.tsv"
        echo
        labels=$(awk -F'\t' 'NR > 1 && $1 !~ /sq-syn/ { print $1 }' "$a/time-summary.tsv")
        files=()
        header="| stage |"
        rule="| --- |"
        for label in $labels; do
            "$UARCH" stages "$a"/logs/"$label".r*.log > "$a/stages-${label//:/-}.tsv"
            files+=("$a/stages-${label//:/-}.tsv")
            header+=" $label |"
            rule+=" ---: |"
        done
        echo "$header"
        echo "$rule"
        awk -F'\t' 'FNR == 1 { file += 1 } $1 == "time-stage" && $3 == "ms" { if (!($2 in seen)) { seen[$2] = 1; order[++n] = $2 } value[$2, file] = $4 }
            END { for (i = 1; i <= n; i += 1) { s = order[i]; line = "| " s " |"; any = 0
                  for (f = 1; f <= file; f += 1) { if (value[s, f] >= 0.05) any = 1; line = line sprintf(" %.2f |", value[s, f]) }
                  if (any) print line } }' "${files[@]}"
        echo
    done
done

for h in "${hosts[@]}"; do
    u=$(dir "$h")/uncore
    [[ -f $u/single.csv ]] || continue
    echo "## DRAM traffic (UMC CAS, system-wide, both channels, 64 bytes per CAS), $(name "$h")"
    echo
    echo "| run | compiles | read CAS | write CAS | activates | window s | GB/s | DRAM bytes per source byte | activates per CAS |"
    echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    for f in idle:0 single:8 fanout8:16; do
        file=$u/${f%%:*}.csv
        [[ -f $file ]] || continue
        awk -F, -v run="${f%%:*}" -v n="${f##*:}" -v bytes=$SQLITE_BYTES '
            $3 ~ /cas_cmd.rd/ { rd = $1; ns = $4 } $3 ~ /cas_cmd.wr/ { wr = $1 } $3 ~ /act_cmd/ { act = $1 }
            END { window = ns / 2 / 1e9
                  printf "| %s | %d | %.0f | %.0f | %.0f | %.3f | %.2f | %s | %.2f |\n", run, n, rd, wr, act, window, (rd + wr) * 64 / window / 1e9,
                      n ? sprintf("%.1f", (rd + wr) * 64 / n / bytes) : "", act / (rd + wr) }' "$file"
    done
    echo
done

for h in "${hosts[@]}"; do
    g=$(dir "$h")/generic-scaling/generic-scaling.tsv
    [[ -f $g ]] || continue
    echo "## _Generic scaling, $(name "$h") (-fsyntax-only of N generated selections, medians of 3)"
    echo
    echo "| build | selections | check-program ms | whole compile ms |"
    echo "| --- | ---: | ---: | ---: |"
    awk -F'\t' 'NR > 1 { printf "| %s | %s | %.2f | %.2f |\n", $1, $2, $3, $4 }' "$g"
    echo
done
