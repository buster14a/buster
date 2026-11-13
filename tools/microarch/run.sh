#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The micro-architecture campaign's driver (docs/commands/microarch.md):
#
#   tools/microarch/run.sh tools/microarch/hosts/HOST.sh STEP...
#
# The run root (ROOT, build/microarch by default) holds bin/compiler-*, corpus,
# self-main.i, predefines-znver5.h, results/HOST and scratch/HOST; `setup`
# makes it. Steps:
#   setup        the run root: corpus link, builds of this checkout (BUILD_ARCHES),
#                predefines-znver5.h, self-main.i (keyed by self-main.i.source)
#                and the uarch tool
#   build-commit NAME COMMIT ARCH[,ARCH...]  bin/compiler-NAME-ARCH from `git archive COMMIT`
#   env          the host's identity (no lock: reads sysfs, perf's catalogs, hashes);
#                a catalog that numbers results stays frozen (KEEP_CATALOG)
#   validate     every variant's outputs, hashed (the objects must be identical)
#   time         whole-process wall time and rusage, 11 rotated rounds, with the
#                compiler's --time-stages reports
#   time-self    the compiler's own main.c (.i), 3 rotated rounds
#   groups       six fixed counter groups (each one perf stat group, never multiplexed)
#                per variant and workload, 5 runs
#   groups-self  the same groups for the main.c workload, 2 runs
#   sweep        every core event (user and kernel, with cycles:u and instructions:u)
#                per sweep variant and workload, 2 runs, in chunks
#   metrics      every perf metric, one perf stat -M each, native variant, 2 runs
#   profiles     leader-sampled groups, IBS ops (all and L3 misses only), LBR and
#                cycles (user and kernel) of sq-c, reduced to per-symbol tables;
#                musl's cycles; the microcode group
#   profiles-same  the znver4 build's profiles (the same binary on every host)
#   profile-self   IBS ops of the main.c workload, by function and by line
#   scaling      one compile on 1..N lanes, musl's sources on 1..N lanes, and K
#                independent compiles on K cores, with package energy
#   energy       package energy, core energy and APERF/MPERF of single-core compiles
#   uncore       UMC CAS commands (where the host exposes amd_umc)
#   ab           AB_BUILDS (bin/compiler-NAME for each NAME) on sq-c and sq-syn with
#                the stage clock, 11 rotated rounds, to results/HOST/AB_NAME
#   ab-self      AB_BUILDS on the main.c workload, 3 rotated rounds
#   generic-scaling  -fsyntax-only of generated units of 250..4,000 _Generic
#                selections for AB_BUILDS (#1341)
#   summarize    the tables of every step's raw files (steps that didn't run are
#                skipped; a reducer that fails fails the step)
# SWEEP_WORKLOADS and METRIC_WORKLOADS (default "sq-c sq-syn") and the profile's
# VARIANTS, SWEEP_VARIANTS and SELF_VARIANTS narrow a run. A sweep or metrics chunk
# with a recorded failure fails the step again on every rerun; RETRY_FAILED=1 runs
# its failed entries again. A build is reused only when its .commit file names the
# commit asked for (build-commit) or the checkout's own (setup), and self-main.i only
# when self-main.i.source names the checkout, preprocessor and predefines. A chunk
# whose recorded entries aren't its catalog's lines fails the step.
# results/HOST/identity.txt binds the results to their inputs (the builds, the self
# input, the corpus, the workloads, the host profile, the counter groups): a step
# that measures is refused once one of them changed, and a done profile of another
# command (PROFILE_WORKLOAD, PROFILE_RUNS) fails its step. A run that can't be pinned to its CPUs fails. REPOSITORY names the
# checkout (this one by default). Each step, or each chunk of one, takes the host's
# lock or core grant itself, for that chunk alone; no step needs another's output
# except summarize.
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repository=${REPOSITORY:-$(cd "$here/../.." && pwd)}
profile=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
# shellcheck source=/dev/null
source "$profile"
ROOT=${ROOT:-$repository/build/microarch}
mkdir -p "$ROOT"
cd "$ROOT" || exit 2
OUT=results/$HOST
SCRATCH=scratch/$HOST
UARCH=$ROOT/uarch
export LC_ALL=C LC_NUMERIC=C SOURCE_DATE_EPOCH=1792454400
unset CPATH C_INCLUDE_PATH
# Every build, of this checkout and of the commits it compares, with CI's toolchain first on PATH (#2628;
# docs/commands/build.md, "The toolchain"): an older commit's build.sh takes PATH's clang where a newer one's takes CI's,
# and two compilers would confound the comparison.
if [[ -f $repository/tools/ci/session-toolchain.sh ]]; then
    toolchain=$(bash "$repository/tools/ci/session-toolchain.sh" stage) || { echo "run.sh: can't stage CI's toolchain" >&2; exit 2; }
    PATH=$toolchain:$PATH
fi
mkdir -p "$OUT" "$SCRATCH"
if [[ ! -x $UARCH || $here/uarch.c -nt $UARCH ]]; then
    clang -std=c17 -O2 -Wall -Wextra -Werror -o "$UARCH" "$here/uarch.c" -lm || exit 2
fi
# shellcheck source=workloads.sh
source "$here/workloads.sh"

log() { printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$OUT/run.log" >&2; }

# The CPUs this process may run on, one per line.
affinity_cpus()
{
    local list part
    list=$(taskset -pc $$) || return 1
    list=${list##*: }
    for part in ${list//,/ }; do
        if [[ $part == *-* ]]; then seq "${part%-*}" "${part#*-}"; else echo "$part"; fi
    done
}

# One CPU per physical core of this process's affinity, comma-separated.
physical_cpus()
{
    local seen=" " cpu core out="" cpus
    cpus=$(affinity_cpus) || return 1
    for cpu in $cpus; do
        core="$(cat "/sys/devices/system/cpu/cpu$cpu/topology/physical_package_id" 2>/dev/null):$(cat "/sys/devices/system/cpu/cpu$cpu/topology/core_id" 2>/dev/null)"
        if [[ $seen != *" $core "* ]]; then seen+="$core "; out+="${out:+,}$cpu"; fi
    done
    echo "$out"
}

single_cpu() { if [[ -n ${SINGLE_CPU:-} ]]; then echo "$SINGLE_CPU"; else affinity_cpus | head -1; fi; }

# Runs "$0 PROFILE --inner STEP ARG..." under the host's single-core hold, pinned.
held() { hold "$here/run.sh" "$profile" --inner "$@"; local s=$?; [[ $PAUSE -gt 0 ]] && sleep "$PAUSE"; return $s; }
held_multi() { hold_multi "$here/run.sh" "$profile" --inner-multi "$@"; local s=$?; [[ $PAUSE -gt 0 ]] && sleep "$PAUSE"; return $s; }

group_spec()
{
    case $1 in
        basic) echo "-e {cycles:u,instructions:u,cycles:k,instructions:k} -e task-clock,page-faults,minor-faults,major-faults,context-switches,cpu-migrations" ;;
        td1) echo "-e {ls_not_halted_cyc:u,de_src_op_disp.all:u,ex_ret_ops:u,de_no_dispatch_per_slot.no_ops_from_frontend:u,de_no_dispatch_per_slot.backend_stalls:u}" ;;
        td2) echo "-e {ls_not_halted_cyc:u,ex_no_retire.load_not_complete:u,ex_no_retire.not_complete:u,cpu/de_no_dispatch_per_slot.no_ops_from_frontend,cmask=$TD_WIDTH/u,ex_ret_ucode_ops:u}" ;;
        br) echo "-e {instructions:u,ex_ret_brn:u,ex_ret_brn_misp:u,ex_ret_brn_tkn:u,$TD_RESYNC:u}" ;;
        fe) echo "-e {instructions:u,ic_tag_hit_miss.instruction_cache_miss:u,op_cache_hit_miss.op_cache_miss:u,op_cache_hit_miss.all_op_cache_accesses:u,bp_l1_tlb_miss_l2_tlb_miss.all:u}" ;;
        mem) echo "-e {instructions:u,ls_dmnd_fills_from_sys.all:u,ls_dmnd_fills_from_sys.dram_io_near:u,l2_cache_req_stat.ic_dc_miss_in_l2:u,ls_l1_d_tlb_miss.all:u}" ;;
    esac
}
COUNTER_GROUPS="basic td1 td2 br fe mem"

# One perf stat measurement around a command, as a unit. OUTPUT (perf's CSV)
# counts only once OUTPUT.done exists, written after perf and the measured command
# both succeeded. A measurement without it runs again whole. When LABEL names the
# command's fanout batches, their completion records are removed first, so that the
# batches run inside the new measurement: otherwise uarch fanout would skip them
# and perf would measure an empty wrapper over the old CSV.
measure()
{
    local output=${1:?a measurement output} label=$2 record; shift 2
    [[ -f $output.done ]] && return 0
    if [[ -n $label ]]; then
        for record in "$(dirname "$output")"/logs/"$label".r*.fanout; do
            [[ -f $record ]] && rm -f -- "${record:?}" "${record%.fanout}.log"
        done
    fi
    rm -f -- "${output:?}"
    "$@" || return 1
    [[ -s $output ]] || return 1
    : > "$output.done.tmp" && mv -- "$output.done.tmp" "$output.done"
}

# ---------------------------------------------------------------------------
# Steps inside a hold (pinned to one CPU).

# Runs one validation command into a fresh output: the output is removed first,
# and a failed command is reported and fails the step (no stale output is hashed).
validate_run()
{
    local output=${1:?an output path} log=$2; shift 2
    rm -f -- "$output"
    "$@" > "$log" 2>&1
    local status=$?
    if [[ $status -ne 0 || ! -f $output ]]; then
        log "validate: '$*' failed (status $status, output ${output##*/} $([[ -f $output ]] && echo present || echo missing))"
        return 1
    fi
}

# The hash validate.tsv holds for VARIANT and WORKLOAD.
validated()
{
    awk -F'\t' -v v="$1" -v w="$2" '$1 == v && $2 == w { print $4 }' "$OUT/validate.tsv"
}

# Every variant's object, -E output, default-lanes object and diagnostics, hashed
# into validate.tsv. The objects and -E outputs of every variant must be the same
# bytes, the default-lanes object the one-lane object's, and the diagnostics the
# same text. A failed command stops the step; a mismatch fails it. Diagnostics
# that differ fail it too unless VALIDATE_DIAGNOSTICS=warn (#1339's divergence).
inner_validate()
{
    local v f="$OUT/validate.tsv" first="" status=0 object preprocessed default diagnostics
    : > "$f"
    for v in $VARIANTS; do
        workload "$v" sq-c && validate_run "$SCRATCH/$v-sq.o" "$SCRATCH/validate-$v-sq-c.log" "${WL[@]}" || return 1
        object=$(sha256sum < "$SCRATCH/$v-sq.o" | cut -d' ' -f1)
        workload "$v" sq-E && validate_run "$SCRATCH/$v-sq.i" "$SCRATCH/validate-$v-sq-E.log" "${WL[@]}" || return 1
        preprocessed=$(sha256sum < "$SCRATCH/$v-sq.i" | cut -d' ' -f1)
        workload "$v" sq-c-default && validate_run "$SCRATCH/$v-sq-default.o" "$SCRATCH/validate-$v-sq-c-default.log" "${WL[@]}" || return 1
        default=$(sha256sum < "$SCRATCH/$v-sq-default.o" | cut -d' ' -f1)
        # The diagnostics are the output: the log itself.
        workload "$v" sq-syn && validate_run "$SCRATCH/validate-$v-sq-syn.log" "$SCRATCH/validate-$v-sq-syn.log" "${WL[@]}" || return 1
        diagnostics=$(sha256sum < "$SCRATCH/validate-$v-sq-syn.log" | cut -d' ' -f1)
        printf '%s\tsq-c\t0\t%s\n%s\tsq-E\t0\t%s\n%s\tsq-c-default\t0\t%s\n%s\tsq-syn\t0\t%s\n' \
            "$v" "$object" "$v" "$preprocessed" "$v" "$default" "$v" "$diagnostics" >> "$f"
        if [[ $default != "$object" ]]; then
            log "validate: $v's default-lanes object differs from its one-lane object"
            status=1
        fi
        if [[ -z $first ]]; then
            first=$v
            continue
        fi
        if [[ $object != "$(validated "$first" sq-c)" ]]; then
            log "validate: $v's object differs from $first's"
            status=1
        fi
        if [[ $preprocessed != "$(validated "$first" sq-E)" ]]; then
            log "validate: $v's -E output differs from $first's"
            status=1
        fi
        if [[ $diagnostics != "$(validated "$first" sq-syn)" ]]; then
            log "validate: $v's diagnostics differ from $first's ($SCRATCH/validate-$v-sq-syn.log, $SCRATCH/validate-$first-sq-syn.log)"
            [[ ${VALIDATE_DIAGNOSTICS:-fail} == warn ]] || status=1
        fi
    done
    if command -v tcc > /dev/null; then
        workload x tcc-c && validate_run "$SCRATCH/tcc-sq.o" "$SCRATCH/validate-tcc.log" "${WL[@]}" || return 1
        printf 'tcc\ttcc-c\t0\t%s\n' "$(sha256sum < "$SCRATCH/tcc-sq.o" | cut -d' ' -f1)" >> "$f"
    fi
    cat "$f"
    return $status
}

inner_time()
{
    local args=() v w
    for v in $VARIANTS; do
        for w in sq-c sq-syn sq-E; do
            workload "$v" "$w"
            args+=(--list "$v:$w" "${WL[@]}")
        done
        workload "$v" sq-c
        args+=(--list "stages-$v" "${WL[@]}" --time-stages)
    done
    if command -v tcc > /dev/null; then
        workload x tcc-c
        args+=(--list "tcc:tcc-c" "${WL[@]}")
    fi
    "$UARCH" time "$OUT/time" 11 "${args[@]}"
}

inner_time_self()
{
    local args=() v
    for v in $SELF_VARIANTS; do
        workload "$v" self-syn
        args+=(--list "$v:self-syn" "${WL[@]}" --list "stages-self-$v" "${WL[@]}" --time-stages)
    done
    "$UARCH" time "$OUT/time-self" 3 "${args[@]}"
}

# AB_BUILDS on sq-c and sq-syn (ab) or on the main.c workload (ab-self).
inner_ab()
{
    local dir=$1 rounds=$2; shift 2
    local args=() v w
    for v in ${AB_BUILDS:?AB_BUILDS names bin/compiler-NAME builds}; do
        for w in "$@"; do
            workload "$v" "$w"
            args+=(--list "$v:$w" "${WL[@]}" --time-stages)
        done
    done
    local status=0
    "$UARCH" time "$OUT/$dir" "$rounds" "${args[@]}" || status=1
    if [[ $* == *sq-c* ]]; then
        for v in $AB_BUILDS; do
            workload "$v" sq-c
            # shellcheck disable=SC2046
            "$UARCH" stat "$OUT/$dir" "$v.sq-c.basic" 5 $(group_spec basic) -- "${WL[@]}" || status=1
            # shellcheck disable=SC2046
            "$UARCH" stat "$OUT/$dir" "$v.sq-c.br" 5 $(group_spec br) -- "${WL[@]}" || status=1
            # shellcheck disable=SC2046
            "$UARCH" stat "$OUT/$dir" "$v.sq-c.td1" 5 $(group_spec td1) -- "${WL[@]}" || status=1
        done
    fi
    return $status
}

# -fsyntax-only --time-stages of generated units of N _Generic selections, 3 runs
# each, for AB_BUILDS (#1341).
inner_generic_scaling()
{
    local dir="$OUT/generic-scaling" v n r status=0
    mkdir -p "$dir"
    for n in 250 500 1000 2000 4000; do
        "$UARCH" generate-generic "$n" > "$SCRATCH/generic-$n.c" || return 1
    done
    for v in ${AB_BUILDS:?AB_BUILDS names bin/compiler-NAME builds}; do
        for n in 250 500 1000 2000 4000; do
            for r in 0 1 2; do
                # A run's .done record follows its success; a run without one runs again.
                [[ -f $dir/$v.$n.r$r.done ]] && continue
                if "bin/compiler-$v" -fsyntax-only --time-stages "$SCRATCH/generic-$n.c" > "$dir/$v.$n.r$r.log" 2>&1; then
                    : > "$dir/$v.$n.r$r.done"
                else
                    log "generic-scaling: $v on $n selections failed (run $r)"
                    status=1
                fi
            done
        done
    done
    {
        printf 'build\tselections\tcheck_program_ms\ttotal_ms\n'
        for v in $AB_BUILDS; do
            for n in 250 500 1000 2000 4000; do
                "$UARCH" stages "$dir/$v.$n".r*.log | awk -F'\t' -v v="$v" -v n="$n" '
                    $2 == "check-program" && $3 == "ms" { c = $4 } $2 == "total" && $3 == "ms" { t = $4 }
                    END { printf "%s\t%s\t%s\t%s\n", v, n, c, t }'
            done
        done
    } > "$dir/generic-scaling.tsv"
    cat "$dir/generic-scaling.tsv"
    return $status
}

inner_groups()
{
    local v=$1 runs=$2; shift 2
    local w g status=0
    for w in "$@"; do
        workload "$v" "$w"
        for g in $COUNTER_GROUPS; do
            # shellcheck disable=SC2046
            "$UARCH" stat "$OUT/groups" "$v.$w.$g" "$runs" $(group_spec "$g") -- "${WL[@]}" || status=1
        done
    done
    return $status
}

inner_sweep()
{
    local v=$1 w=$2 from=$3 to=$4
    workload "$v" "$w"
    "$UARCH" sweep "$OUT/sweep-$v-$w" "$OUT/env/core-events.txt" "$from" "$to" 2 "cycles:u,instructions:u" -- "${WL[@]}"
}

inner_metrics()
{
    local v=$1 w=$2 from=$3 to=$4
    workload "$v" "$w"
    "$UARCH" metrics "$OUT/metrics-$v-$w" "$OUT/env/metrics.txt" "$from" "$to" 2 -- "${WL[@]}"
}

# Records 8 compiles of sq-c (or PROFILE_RUNS of PROFILE_WORKLOAD) under one perf
# record and reduces the samples. Leader-sampled groups use a 50,021-cycle period:
# each sample's member deltas go to the leader's address, so a long period blends
# short functions with their neighbours. perf.data stays in scratch/HOST. The
# profile is done (NAME.done, written last, through a rename) only when perf record
# succeeded and every reducer succeeded with rows; otherwise its outputs are
# removed, the step fails, and a rerun records it again. NAME.done holds the perf
# record command, one argument per line, then perf.data's size: a done profile is
# skipped only for the same command, and one of another (its events, the workload,
# PROFILE_WORKLOAD or PROFILE_RUNS changed) fails the step until it is moved away.
profile_one()
{
    local name=${1:?a profile name} v=${2:?a variant}; shift 2
    local dir="$OUT/profiles/$v" data="$SCRATCH/profile-$v-$name.data" status=0 output
    mkdir -p "$dir"
    workload "$v" "${PROFILE_WORKLOAD:-sq-c}"
    local loop="" runs=${PROFILE_RUNS:-8} command key
    printf -v loop '%q ' "${WL[@]}"
    command=(perf record -q -o "$data" "$@" -- bash -c "for ((i = 0; i < $runs; i++)); do $loop > /dev/null 2>&1 || exit 1; done")
    key=$(printf '%s\n' "${command[@]}")
    if [[ -f $dir/$name.done ]]; then
        if [[ $(sed '$d' "$dir/$name.done") == "$key" ]]; then
            return 0
        fi
        log "profile $v $name: $name.done was recorded by another command: move the profile away to record this one"
        return 1
    fi
    "${command[@]}" > "$dir/$name.record.log" 2>&1 || status=1
    echo "status $status" >> "$dir/$name.record.log"
    local outputs=("$dir/$name.header.txt")
    if [[ $status -eq 0 ]]; then
        set -o pipefail
        case $name in
            cycles | musl-cycles | self-cycles)
                outputs+=("$dir/$name.txt" "$dir/$name-dso.txt")
                perf report -i "$data" --no-children --sort dso,sym --stdio -q --percent-limit 0.05 > "$dir/$name.txt" 2>> "$dir/$name.record.log" || status=1
                perf report -i "$data" --no-children --sort dso --stdio -q > "$dir/$name-dso.txt" 2>> "$dir/$name.record.log" || status=1 ;;
            lbr)
                outputs+=("$dir/lbr.tsv")
                perf script -i "$data" -F brstacksym 2>> "$dir/$name.record.log" | "$UARCH" branches - 120 > "$dir/lbr.tsv" || status=1 ;;
            ibs | ibs-l3)
                outputs+=("$dir/$name.tsv")
                perf script -i "$data" -F ip,sym,data_src,weight 2>> "$dir/$name.record.log" | "$UARCH" memory - > "$dir/$name.tsv" || status=1 ;;
            self-ibs)
                outputs+=("$dir/self-ibs.txt" "$dir/self-ibs-check-common-evaluated-lines.tsv")
                perf report -i "$data" --no-children --sort sym --stdio -q --percent-limit 0.3 > "$dir/self-ibs.txt" 2>> "$dir/$name.record.log" || status=1
                "$here/inline-split.sh" "bin/compiler-$v" "$data" '^check_common_evaluated$' check_common_evaluated --lines \
                    > "$dir/self-ibs-check-common-evaluated-lines.tsv" 2>> "$dir/$name.record.log" || status=1 ;;
            *)
                outputs+=("$dir/$name.tsv")
                perf script -i "$data" -F period,event,ip,sym 2>> "$dir/$name.record.log" | "$UARCH" symbols - > "$dir/$name.tsv" || status=1 ;;
        esac
        set +o pipefail
        perf report -i "$data" --header-only > "$dir/$name.header.txt" 2>> "$dir/$name.record.log" || status=1
        # A reducer that read no samples writes only its headers (uarch's tables start
        # their header lines with "symbol", "site" or "#"): no row is a failure.
        for output in "${outputs[@]:1}"; do
            if [[ $(grep -c -v -E $'^(#|symbol\t|site\t|symbol_level\t|$)' "$output" 2>/dev/null) -lt 1 ]]; then
                log "profile $v $name: ${output##*/} has no rows"
                status=1
            fi
        done
    fi
    if [[ $status -eq 0 ]]; then
        { { printf '%s\n' "$key" && stat -c 'bytes %s' "$data"; } > "$dir/$name.done.tmp" && mv -- "$dir/$name.done.tmp" "$dir/$name.done"; } || status=1
    else
        log "profile $v $name failed; see $dir/$name.record.log"
        for output in "${outputs[@]}"; do
            rm -f -- "${output:?}"
        done
    fi
    return $status
}

inner_profiles()
{
    local v=$1 part=$2 status=0
    case $part in
        a)
            profile_one cycles "$v" -e cycles -c 100003 || status=1
            profile_one td1 "$v" -e "{ls_not_halted_cyc:u,de_src_op_disp.all:u,ex_ret_ops:u,de_no_dispatch_per_slot.no_ops_from_frontend:u,de_no_dispatch_per_slot.backend_stalls:u}:S" -c 50021 || status=1
            profile_one td2 "$v" -e "{ls_not_halted_cyc:u,ex_no_retire.load_not_complete:u,ex_no_retire.not_complete:u,instructions:u,ex_ret_brn_misp:u}:S" -c 50021 || status=1 ;;
        b)
            profile_one mem "$v" -e "{cycles:u,ls_dmnd_fills_from_sys.all:u,ls_dmnd_fills_from_sys.dram_io_near:u,l2_cache_req_stat.ic_dc_miss_in_l2:u,ls_l1_d_tlb_miss.all:u}:S" -c 50021 || status=1
            profile_one fe "$v" -e "{cycles:u,ic_tag_hit_miss.instruction_cache_miss:u,op_cache_hit_miss.op_cache_miss:u,op_cache_hit_miss.all_op_cache_accesses:u,bp_l1_tlb_miss_l2_tlb_miss.all:u}:S" -c 50021 || status=1 ;;
        c)
            profile_one ibs "$v" -e "ibs_op/cnt_ctl=1,swfilt=1/u" -d -W -c 200000 || status=1
            profile_one ibs-l3 "$v" -e "ibs_op/cnt_ctl=1,swfilt=1,l3missonly=1/u" -d -W -c 4000 || status=1
            profile_one lbr "$v" -j any,u -e cycles:u -c 1000003 || status=1 ;;
        d)
            PROFILE_WORKLOAD=musl-syn PROFILE_RUNS=2 profile_one musl-cycles "$v" -e cycles -c 100003 || status=1 ;;
        e)
            profile_one ucode "$v" -e "{cycles:u,ex_ret_ucode_ops:u,ex_ret_ops:u,fp_ops_retired_by_width.pack_512_uops_retired:u,ls_bad_status2.stli_other:u}:S" -c 50021 || status=1 ;;
        self)
            PROFILE_WORKLOAD=self-syn PROFILE_RUNS=1 profile_one self-ibs "$v" -e "ibs_op/cnt_ctl=1,swfilt=1/u" -c 400000 || status=1 ;;
    esac
    return $status
}

inner_energy()
{
    local cpu=$1 v loop dir="$OUT/energy" status=0
    mkdir -p "$dir"
    measure "$dir/idle.csv" "" perf stat -a -x, -o "$dir/idle.csv" -e power/energy-pkg/ -- sleep 3 || status=1
    for v in $VARIANTS; do
        workload "$v" sq-c
        printf -v loop '%q ' "${WL[@]}"
        # Each loop stops at its first failed compile; perf passes that status on.
        measure "$dir/$v.pkg.csv" "" perf stat -a -x, -o "$dir/$v.pkg.csv" -e power/energy-pkg/ -- \
            bash -c "for i in 1 2 3 4 5 6 7 8 9 10; do $loop > /dev/null 2>&1 || exit 1; done" || status=1
        measure "$dir/$v.freq.csv" "" perf stat -C "$cpu" -x, -o "$dir/$v.freq.csv" -e msr/aperf/,msr/mperf/,msr/tsc/,cycles,instructions -- \
            bash -c "for i in 1 2 3 4 5 6 7 8 9 10; do $loop > /dev/null 2>&1 || exit 1; done" || status=1
        measure "$dir/$v.core.csv" "" perf stat -a --per-core -x, -o "$dir/$v.core.csv" -e power_core/energy-core/ -- \
            bash -c "for i in 1 2 3 4 5 6 7 8 9 10; do $loop > /dev/null 2>&1 || exit 1; done" || status=1
    done
    return $status
}

# ---------------------------------------------------------------------------
# Steps inside a multi-core hold.

inner_scaling()
{
    local cpus=$1 dir="$OUT/scaling" n k r list args=() status=0
    mkdir -p "$dir"
    echo "$cpus" > "$dir/cpus.txt"
    # One compile on N lanes and musl's sources on N lanes, rotated.
    for n in $SCALING_LANES; do
        workload "$NATIVE:$n" sq-c
        args+=(--list "sq-c-lanes$n" taskset -c "$cpus" "${WL[@]}")
        workload "$NATIVE:$n" musl-syn
        args+=(--list "musl-lanes$n" taskset -c "$cpus" "${WL[@]}")
    done
    workload "$NATIVE" sq-c-default
    args+=(--list "sq-c-default" taskset -c "$cpus" "${WL[@]}")
    "$UARCH" time "$dir" 5 "${args[@]}" || status=1
    for n in $SCALING_LANES; do
        workload "$NATIVE:$n" sq-c
        "$UARCH" stat "$dir" "sq-c-lanes$n" 3 -e task-clock,cycles:u,instructions:u,cycles:k,instructions:k,context-switches,cpu-migrations,page-faults \
            -- taskset -c "$cpus" "${WL[@]}" || status=1
        workload "$NATIVE:$n" musl-syn
        "$UARCH" stat "$dir" "musl-lanes$n" 2 -e task-clock,cycles:u,instructions:u,cycles:k,instructions:k,context-switches,cpu-migrations,page-faults \
            -- taskset -c "$cpus" "${WL[@]}" || status=1
    done
    # K independent single-lane compiles on K cores, each batch under package energy.
    for k in $FANOUT_WORKERS; do
        list=$(echo "$cpus" | cut -d, -f1-"$k")
        workload "$NATIVE" sq-c
        local command=("${WL[@]}")
        command[${#command[@]}-3]="$SCRATCH/fan-{i}.o"
        for r in 0 1 2 3; do
            # The batch and its energy are one measurement.
            measure "$dir/fanout-energy-$k.r$r.csv" "fanout$k-r$r" perf stat -a -x, -o "$dir/fanout-energy-$k.r$r.csv" -e power/energy-pkg/ -- \
                "$UARCH" fanout "$dir" "fanout$k-r$r" 1 "$list" -- "${command[@]}" || status=1
        done
    done
    return $status
}

inner_uncore()
{
    local cpus=$1 dir="$OUT/uncore" loop status=0
    mkdir -p "$dir"
    local events="amd_umc/umc_cas_cmd.rd/,amd_umc/umc_cas_cmd.wr/,amd_umc/umc_act_cmd.all/,amd_umc/umc_mem_clk/"
    measure "$dir/idle.csv" "" perf stat -a -x, -o "$dir/idle.csv" -e "$events" -- sleep 2 || status=1
    workload "$NATIVE" sq-c
    printf -v loop '%q ' "${WL[@]}"
    measure "$dir/single.csv" "" perf stat -a -x, -o "$dir/single.csv" -e "$events" -- taskset -c "$(single_cpu)" \
        bash -c "for i in 1 2 3 4 5 6 7 8; do $loop > /dev/null 2>&1 || exit 1; done" || status=1
    local command=("${WL[@]}")
    command[${#command[@]}-3]="$SCRATCH/fan-{i}.o"
    # The two batches and their counters are one measurement: a rerun repeats both.
    measure "$dir/fanout8.csv" fanout8 perf stat -a -x, -o "$dir/fanout8.csv" -e "$events" -- \
        "$UARCH" fanout "$dir" fanout8 2 "$cpus" -- "${command[@]}" || status=1
    return $status
}

inner()
{
    local step=$1 cpu; shift
    receipt >> "$OUT/holds.txt"
    cpu=$(single_cpu)
    log "inner $step $* (cpu $cpu)"
    # An unpinned run is no sample.
    if [[ -z $cpu ]] || ! taskset -pc "$cpu" $$ > /dev/null; then
        log "inner $step $*: can't pin to CPU '$cpu'"
        return 1
    fi
    case $step in
        validate) inner_validate ;;
        time) inner_time ;;
        time-self) inner_time_self ;;
        ab) inner_ab "${AB_NAME:-ab}" 11 sq-c sq-syn ;;
        ab-self) inner_ab "${AB_NAME:-ab-self}" 3 self-syn ;;
        generic-scaling) inner_generic_scaling ;;
        groups) inner_groups "$@" ;;
        sweep) inner_sweep "$@" ;;
        metrics) inner_metrics "$@" ;;
        profiles) inner_profiles "$@" ;;
        energy) inner_energy "$cpu" ;;
        *) log "inner: unknown step $step"; false ;;
    esac
    local status=$?
    if [[ $status -eq 0 ]]; then
        log "inner $step $* done"
    else
        log "inner $step $* failed (status $status)"
    fi
    return $status
}

inner_multi()
{
    local step=$1 cpus; shift
    receipt >> "$OUT/holds.txt"
    cpus=$(physical_cpus)
    if [[ -z $cpus ]]; then
        log "inner-multi $step: no CPU of this process's affinity"
        return 1
    fi
    log "inner-multi $step (cpus $cpus)"
    case $step in
        scaling) inner_scaling "$cpus" ;;
        uncore) inner_uncore "$cpus" ;;
        *) log "inner-multi: unknown step $step"; false ;;
    esac
    local status=$?
    if [[ $status -eq 0 ]]; then
        log "inner-multi $step done"
    else
        log "inner-multi $step failed (status $status)"
    fi
    return $status
}

# ---------------------------------------------------------------------------
# Outer steps.

# The checkout's commit; when tracked files differ from it, "-dirty-" and the
# first 16 hex digits of the diff's SHA-256, so that two dirty states differ.
checkout_identity()
{
    local head diff
    head=$(git -C "$repository" rev-parse HEAD) || return 1
    if ! git -C "$repository" diff --quiet HEAD --; then
        diff=$(git -C "$repository" diff --binary HEAD -- | sha256sum) || return 1
        head+=-dirty-${diff:0:16}
    fi
    echo "$head"
}

# The run root from this checkout: the frozen corpus (CORPUS, the 9700X's copy, or
# fetched into ROOT), each BUILD_ARCHES build of the checkout, the predefines and
# the preprocessed main.c. Copy bin/, predefines-znver5.h and self-main.i with their
# records (.commit, .source) from one host to the others to run identical
# executables and input everywhere.
step_setup()
{
    local corpus=${CORPUS:-} arch
    if [[ -z $corpus && -d /var/lib/buster-9700x/corpora/preprocessor-391e729af658 ]]; then
        corpus=/var/lib/buster-9700x/corpora/preprocessor-391e729af658
    fi
    if [[ -z $corpus ]]; then
        corpus=$ROOT/corpus-preprocessor-391e729af658
        [[ -f $corpus/MANIFESTS.sha256 ]] || bash "$repository/tools/ci/fetch-corpus.sh" "$corpus" || exit 2
    fi
    ln -sfn "$corpus" corpus
    mkdir -p bin
    local identity
    identity=$(checkout_identity) || exit 2
    for arch in ${BUILD_ARCHES:-znver4 znver5 x86-64}; do
        if [[ -x bin/compiler-$arch ]]; then
            # A build of another commit (or of no recorded one) is never measured as this checkout's.
            if [[ $(cat "bin/compiler-$arch.commit" 2>/dev/null) != "$identity" ]]; then
                log "setup: bin/compiler-$arch was built from '$(cat "bin/compiler-$arch.commit" 2>/dev/null)', the checkout is $identity: remove it to rebuild"
                exit 2
            fi
            continue
        fi
        log "setup: build $arch"
        hold_build env BUSTER_HOST_ARCH="$arch" "$repository/build.sh" --config Release > "$SCRATCH/build-$arch.log" 2>&1 || { log "setup: build $arch failed"; exit 2; }
        cp "$repository/build/compiler" "bin/compiler-$arch"
        echo "$identity" > "bin/compiler-$arch.commit"
    done
    # The predefines are kept until removed, like the corpus: the self workloads read
    # them, and a copy from another host stays that host's.
    if [[ ! -s predefines-znver5.h ]]; then
        clang -dM -E -march=znver5 -fwrapv -funsigned-char -x c /dev/null > predefines-znver5.h.tmp || exit 2
        mv -f predefines-znver5.h.tmp predefines-znver5.h || exit 2
    fi
    # self-main.i.source keys the preprocessed main.c to what made it: the checkout,
    # the preprocessor's build and the predefines. One made from anything else is
    # never measured as this checkout's.
    local preprocessor=bin/compiler-${SELF_PREPROCESSOR:-znver4} options=(-march=znver5 -fwrapv -funsigned-char -fno-strict-aliasing -E) key binary predefines
    binary=$(sha256sum < "$preprocessor") && predefines=$(sha256sum < predefines-znver5.h) || exit 2
    key=$(printf 'checkout %s\npreprocessor %s %s %s\npredefines %s\noptions %s' "$identity" "${preprocessor#bin/}" \
        "$(cat "$preprocessor.commit" 2>/dev/null)" "${binary%% *}" "${predefines%% *}" "${options[*]}")
    if [[ -e self-main.i ]]; then
        if [[ $(cat self-main.i.source 2>/dev/null) != "$key" ]]; then
            log "setup: self-main.i was made from {$(paste -sd ';' self-main.i.source 2>/dev/null)}, this checkout's is {$(paste -sd ';' <<< "$key")}: remove it to make it again"
            exit 2
        fi
    else
        "$preprocessor" --predefines predefines-znver5.h "-I$repository/src" "${options[@]}" -o self-main.i.tmp "$repository/src/buster/compiler/main.c" || exit 2
        [[ -s self-main.i.tmp ]] || { log "setup: $preprocessor wrote no self-main.i"; exit 2; }
        # The key first: an interrupted setup leaves no self-main.i, and the next one makes it.
        printf '%s\n' "$key" > self-main.i.source.tmp && mv -f self-main.i.source.tmp self-main.i.source && mv -f self-main.i.tmp self-main.i || exit 2
    fi
    log "setup: $(ls bin | tr '\n' ' ')"
}

# The sweep's and the metrics' results are numbered by the lines of their catalog,
# NAME.txt, so a catalog is frozen once results use it. This perf's list
# (NAME.live.txt) replaces it only while no result does (RESULTS, a glob, matches
# nothing); otherwise a changed list fails the step, and KEEP_CATALOG=1 keeps
# measuring the frozen catalog.
freeze_catalog()
{
    local name=$1 results=$2 status=0
    if [[ ! -s $name.live.txt ]]; then
        log "env: perf listed no entry of ${name##*/}; ${name##*/}.txt is unchanged"
        status=1
    elif [[ ! -f $name.txt ]] || cmp -s "$name.live.txt" "$name.txt"; then
        mv -f "$name.live.txt" "$name.txt" || status=1
    elif ! compgen -G "$results" > /dev/null; then
        log "env: ${name##*/}.txt replaced by this perf's list; no results used it"
        mv -f "$name.live.txt" "$name.txt" || status=1
    elif [[ ${KEEP_CATALOG:-0} == 1 ]]; then
        log "env: ${name##*/}.txt differs from this perf's list (${name##*/}.live.txt); KEEP_CATALOG=1 keeps measuring it"
    else
        log "env: ${name##*/}.txt numbers recorded results and differs from this perf's list (${name##*/}.live.txt): KEEP_CATALOG=1 keeps measuring it, or start new results"
        status=1
    fi
    return $status
}

# bin/compiler-NAME-ARCH for each ARCH, built from `git archive COMMIT`. COMMIT is
# resolved to its full id first; a tree or build already under NAME is reused only
# when it holds that commit, and anything else fails the step instead of measuring
# another commit under NAME.
step_build_commit()
{
    local name=${1:?a build name} commit arch tree; shift
    commit=$(git -C "$repository" rev-parse --verify "${1:?a commit}^{commit}") || { log "build-commit: no commit $1"; exit 2; }
    shift
    tree=$ROOT/trees/$name
    if [[ -f $tree/COMMIT && $(cat "$tree/COMMIT") != "$commit" ]]; then
        log "build-commit: $name's tree holds $(cat "$tree/COMMIT"), not $commit: choose another name or remove $tree"
        exit 2
    fi
    if [[ ! -f $tree/COMMIT ]]; then
        mkdir -p "$tree"
        (set -o pipefail; git -C "$repository" archive "$commit" | tar -x -C "$tree") || exit 2
        echo "$commit" > "$tree/COMMIT"
    fi
    for arch in "$@"; do
        if [[ -x bin/compiler-$name-$arch ]]; then
            if [[ $(cat "bin/compiler-$name-$arch.commit" 2>/dev/null) != "$commit" ]]; then
                log "build-commit: bin/compiler-$name-$arch was built from '$(cat "bin/compiler-$name-$arch.commit" 2>/dev/null)', not $commit: remove it to rebuild"
                exit 2
            fi
            continue
        fi
        log "build-commit: $name $arch ($(cat "$tree/COMMIT"))"
        hold_build env BUSTER_HOST_ARCH="$arch" "$tree/build.sh" --config Release > "$SCRATCH/build-$name-$arch.log" 2>&1 || { log "build-commit: $name $arch failed"; exit 2; }
        cp "$tree/build/compiler" "bin/compiler-$name-$arch"
        cp "$tree/COMMIT" "bin/compiler-$name-$arch.commit"
    done
}

step_env()
{
    local dir="$OUT/env" c status=0 listed
    mkdir -p "$dir"
    uname -a > "$dir/uname.txt"
    lscpu > "$dir/lscpu.txt"
    awk '/^$/{exit} {print}' /proc/cpuinfo > "$dir/cpuinfo-cpu0.txt"
    {
        printf 'cpu\tonline\tgovernor\tepp\tmin_khz\tmax_khz\tcur_khz\n'
        for c in /sys/devices/system/cpu/cpu[0-9]*; do
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "${c##*/}" "$(cat "$c/online" 2>/dev/null || echo 1)" \
                "$(cat "$c/cpufreq/scaling_governor" 2>/dev/null)" "$(cat "$c/cpufreq/energy_performance_preference" 2>/dev/null)" \
                "$(cat "$c/cpufreq/scaling_min_freq" 2>/dev/null)" "$(cat "$c/cpufreq/scaling_max_freq" 2>/dev/null)" \
                "$(cat "$c/cpufreq/scaling_cur_freq" 2>/dev/null)"
        done
    } > "$dir/cpufreq.tsv"
    {
        echo "boost $(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null)"
        echo "thp_enabled $(cat /sys/kernel/mm/transparent_hugepage/enabled)"
        echo "thp_defrag $(cat /sys/kernel/mm/transparent_hugepage/defrag)"
        echo "perf_event_paranoid $(cat /proc/sys/kernel/perf_event_paranoid)"
        echo "nmi_watchdog $(cat /proc/sys/kernel/nmi_watchdog)"
        echo "perf_event_max_sample_rate $(cat /proc/sys/kernel/perf_event_max_sample_rate)"
        # Device identifiers stay out of the record.
        echo "kernel_cmdline $(sed 's/cryptdevice=[^ ]*//g; s/root=[^ ]*//g; s/resume=[^ ]*//g; s/rd\.luks[^ ]*//g; s/initrd=[^ ]*//g' /proc/cmdline)"
        echo "loadavg $(cat /proc/loadavg)"
        echo "pmus $(ls /sys/bus/event_source/devices | tr '\n' ' ')"
        echo "harness_commit $(git -C "$repository" rev-parse HEAD 2>/dev/null)"
    } > "$dir/settings.txt"
    free -m > "$dir/memory.txt"
    { perf --version; clang --version | head -1; tcc -v 2>&1 | head -1; bash --version | head -1; } > "$dir/tools.txt"
    sha256sum bin/compiler-* self-main.i predefines-znver5.h "$here"/uarch.c "$here"/run.sh "$here"/workloads.sh "$profile" > "$dir/sha256.txt" 2>/dev/null
    sha256sum corpus/MANIFESTS.sha256 corpus/sqlite/sqlite3.c >> "$dir/sha256.txt"
    # perf's status is checked apart from the filters after it: a perf that fails
    # lists nothing, and an empty list is no catalog.
    if perf list -j > "$dir/perf-list.json" 2> /dev/null; then
        gzip -9nf "$dir/perf-list.json" || status=1
    else
        log "env: perf list -j failed"
        status=1
    fi
    if listed=$(perf list --raw-dump pmu 2> /dev/null); then
        # perf lists tool and sensor names beside the core events; they don't count per thread.
        tr ' ' '\n' <<< "$listed" | grep -v '^$' | grep -v / | grep -v '^drm-' \
            | grep -v -x -E 'core_wide|duration_time|has_pmem|num_cores|num_cpus|num_cpus_online|num_dies|num_packages|slots|smt_on|system_time|user_time' \
            | sort -u > "$dir/core-events.live.txt"
        freeze_catalog "$dir/core-events" "$OUT/sweep-*/sweep/*.event" || status=1
    else
        log "env: perf list --raw-dump pmu failed; core-events.txt is unchanged"
        status=1
    fi
    if listed=$(perf list -j metrics 2> /dev/null); then
        sed -n 's/.*"MetricName": "\([^"]*\)".*/\1/p' <<< "$listed" | sort -u > "$dir/metrics.live.txt"
        freeze_catalog "$dir/metrics" "$OUT/metrics-*/metrics/*.metric" || status=1
    else
        log "env: perf list -j metrics failed; metrics.txt is unchanged"
        status=1
    fi
    log "env: $(cat "$dir/core-events.txt" 2> /dev/null | wc -l) core events, $(cat "$dir/metrics.txt" 2> /dev/null | wc -l) metrics"
    return $status
}

# Runs the chunks of a sweep or metrics step that aren't complete. uarch check reads
# every run's record of the chunk: a complete chunk is skipped, a chunk with a
# recorded failure fails the step (RETRY_FAILED=1 removes the failed runs' records
# and runs them again), and an incomplete one is run under the hold. A chunk whose
# recorded entries aren't the catalog's lines of their numbers fails the step.
run_chunks()
{
    local kind=$1 directory=$2 count=$3 chunk=$4 variant=$5 workload=$6 from to state retry=() catalog=$OUT/env/core-events.txt
    [[ $kind == metrics ]] && catalog=$OUT/env/metrics.txt
    [[ ${RETRY_FAILED:-0} == 1 ]] && retry=(--retry-failed)
    for ((from = 0; from < count; from += chunk)); do
        to=$((from + chunk < count ? from + chunk : count))
        "$UARCH" check "$directory" "$kind" "$from" "$to" 2 "${retry[@]}" --names "$catalog" > "$SCRATCH/check.txt"
        state=$?
        case $state in
            0) continue ;;
            1)
                log "$kind $variant $workload $from: $(grep -c . "$SCRATCH/check.txt") runs failed before (RETRY_FAILED=1 runs them again)"
                return 1 ;;
            4)
                log "$kind $variant $workload $from: $(grep -c . "$SCRATCH/check.txt") recorded entries aren't ${catalog##*/}'s lines, first $(head -1 "$SCRATCH/check.txt")"
                return 1 ;;
            3)
                log "$kind $variant $workload $from"
                held "$kind" "$variant" "$workload" "$from" "$to" || return 1 ;;
            *) return 1 ;;
        esac
    done
}

# A sweep or metrics step without its catalog fails rather than measure nothing.
need_catalog()
{
    [[ -s $OUT/env/$1.txt ]] || { log "$2: env/$1.txt is missing or empty (env writes it)"; return 1; }
}

step_sweep()
{
    local v w n
    need_catalog core-events sweep || return 1
    n=$(wc -l < "$OUT/env/core-events.txt")
    for v in $SWEEP_VARIANTS; do
        for w in ${SWEEP_WORKLOADS:-sq-c sq-syn}; do
            run_chunks sweep "$OUT/sweep-$v-$w" "$n" "$SWEEP_CHUNK" "$v" "$w" || return 1
        done
    done
}

step_metrics()
{
    local w n
    need_catalog metrics metrics || return 1
    n=$(wc -l < "$OUT/env/metrics.txt")
    for w in ${METRIC_WORKLOADS:-sq-c sq-syn}; do
        run_chunks metrics "$OUT/metrics-$NATIVE-$w" "$n" "$METRIC_CHUNK" "$NATIVE" "$w" || return 1
    done
}

# The inputs results/HOST is measured with, one "NAME SHA-256" line each: every
# build in bin/, the self input and its predefines, the corpus's manifest, the
# workloads' command lines (workloads.sh), the host profile (its CPUs, widths and
# event names), and the counter groups as this run expands them (TD_WIDTH and
# TD_RESYNC in them). The CPUs a hold grants aren't an input: on a shared host
# they differ between runs, and each is pinned.
campaign_inputs()
{
    local file name hash group
    for file in bin/compiler-* self-main.i predefines-znver5.h corpus/MANIFESTS.sha256 "$here/workloads.sh" "$profile"; do
        [[ -f $file && $file != *.commit ]] || continue
        name=${file##*/}
        [[ $file == corpus/* ]] && name=corpus
        [[ $file == "$profile" ]] && name=profile
        hash=$(sha256sum < "$file") || return 1
        echo "$name ${hash%% *}"
    done
    hash=$(for group in $COUNTER_GROUPS; do group_spec "$group"; done | sha256sum) || return 1
    echo "groups ${hash%% *}"
}

# Records are reused by their names alone, so results/HOST/identity.txt binds the
# results to the inputs they were measured with. A step that measures runs only
# while every recorded input is unchanged; an input first seen (a build-commit
# build) is added. Results from before identity.txt can't be checked: refused too.
check_campaign()
{
    local recorded=$OUT/identity.txt current name hash old added="" status=0
    current=$(campaign_inputs) || return 1
    if [[ ! -f $recorded ]] && compgen -G "$OUT/*" | grep -q -v -x -F "$OUT/run.log"; then
        log "results/$HOST holds results but no identity.txt, so their inputs are unknown: move them away to measure"
        return 1
    fi
    while read -r name hash; do
        [[ -n $name ]] || continue
        old=$(awk -v name="$name" '$1 == name { print $2 }' "$recorded" 2> /dev/null)
        if [[ -z $old ]]; then
            added+="$name $hash"$'\n'
        elif [[ $old != "$hash" ]]; then
            log "results/$HOST was measured with another $name ($old, now $hash): move the results away to measure these inputs"
            status=1
        fi
    done <<< "$current"
    # Inputs may be added as builds are prepared, but a recorded input may not
    # disappear: existing completion records would otherwise be reused.
    if [[ -f $recorded ]]; then
        while read -r name hash; do
            [[ -n $name ]] || continue
            if ! awk -v name="$name" '$1 == name { found = 1 } END { exit !found }' <<< "$current"; then
                log "results/$HOST was measured with $name, which is now missing: restore it or move the results away"
                status=1
            fi
        done < "$recorded"
    fi
    if [[ $status -eq 0 && -n $added ]]; then
        { cat "$recorded" 2> /dev/null; printf '%s' "$added"; } > "$recorded.tmp" && mv -f "$recorded.tmp" "$recorded" || status=1
    fi
    return $status
}

# Reduces whatever the campaign recorded: a step that didn't run is no failure, a
# reducer that fails is.
step_summarize()
{
    local d v status=0
    for d in "$OUT"/time "$OUT"/time-self "$OUT"/groups "$OUT"/sweep-* "$OUT"/metrics-* "$OUT"/scaling "$OUT"/ab*; do
        if [[ -d $d ]]; then
            "$UARCH" summarize "$d" || { log "summarize: $d failed"; status=1; }
        fi
    done
    local log record
    local logs=()
    for v in $VARIANTS; do
        logs=()
        for log in "$OUT"/time/logs/stages-"$v".r*.log; do
            [[ -f $log ]] || continue
            record=${log%.log}.time
            if [[ -f $record ]] && awk -F '\t' 'NR == 1 && $3 == "0" { ok = 1 } END { exit !ok }' "$record"; then logs+=("$log"); fi
        done
        if [[ ${#logs[@]} -gt 0 ]]; then
            "$UARCH" stages "${logs[@]}" > "$OUT/stages-$v.tsv" || { log "summarize: stages-$v failed"; status=1; }
        else
            rm -f -- "$OUT/stages-$v.tsv"
        fi
    done
    for v in $SELF_VARIANTS; do
        logs=()
        for log in "$OUT"/time-self/logs/stages-self-"$v".r*.log; do
            [[ -f $log ]] || continue
            record=${log%.log}.time
            if [[ -f $record ]] && awk -F '\t' 'NR == 1 && $3 == "0" { ok = 1 } END { exit !ok }' "$record"; then logs+=("$log"); fi
        done
        if [[ ${#logs[@]} -gt 0 ]]; then
            "$UARCH" stages "${logs[@]}" > "$OUT/stages-self-$v.tsv" || { log "summarize: stages-self-$v failed"; status=1; }
        else
            rm -f -- "$OUT/stages-self-$v.tsv"
        fi
    done
    return $status
}

if [[ ${1:-} == --inner ]]; then
    shift
    inner "$@"
    exit $?
elif [[ ${1:-} == --inner-multi ]]; then
    shift
    inner_multi "$@"
    exit $?
fi

failed=0
while [[ $# -gt 0 ]]; do
    step=$1
    shift
    log "step $step"
    # Steps that prepare the run root or only read results don't measure.
    if [[ $step != setup && $step != build-commit && $step != summarize ]] && ! check_campaign; then
        log "step $step failed (status 1)"
        failed=1
        [[ ${KEEP_GOING:-0} == 1 ]] && continue
        break
    fi
    case $step in
        setup) step_setup ;;
        build-commit)
            # build-commit NAME COMMIT ARCH[,ARCH...]
            step_build_commit "$1" "$2" ${3//,/ }
            shift 3 ;;
        env) step_env ;;
        validate) held validate ;;
        time) held time ;;
        time-self) held time-self ;;
        ab) held ab ;;
        ab-self) held ab-self ;;
        generic-scaling) held generic-scaling ;;
        groups)
            status=0
            first=1
            for v in $VARIANTS; do
                held groups "$v" 5 sq-c sq-syn sq-E || status=1
                if [[ $first == 1 ]] && command -v tcc > /dev/null; then held groups tcc 5 tcc-c || status=1; fi
                first=0
            done
            [[ $status -eq 0 ]] ;;
        groups-self)
            status=0
            for v in $SELF_VARIANTS; do held groups "$v" 2 self-syn || status=1; done
            [[ $status -eq 0 ]] ;;
        sweep) step_sweep ;;
        metrics) step_metrics ;;
        profiles)
            status=0
            for part in a b c d e; do held profiles "$NATIVE" "$part" || status=1; done
            [[ $status -eq 0 ]] ;;
        profiles-same)
            status=0
            for part in a b c e; do held profiles znver4 "$part" || status=1; done
            [[ $status -eq 0 ]] ;;
        profile-self) held profiles "$NATIVE" self ;;
        scaling) held_multi scaling ;;
        energy) held energy ;;
        uncore) [[ $UNCORE != 1 ]] || held_multi uncore ;;
        summarize) step_summarize ;;
        *) log "unknown step $step"; false ;;
    esac
    status=$?
    if [[ $status -ne 0 ]]; then
        log "step $step failed (status $status)"
        failed=1
        [[ ${KEEP_GOING:-0} == 1 ]] || break
    fi
done
if [[ $failed -ne 0 ]]; then
    log "run failed: see $OUT/run.log"
fi
exit $failed
