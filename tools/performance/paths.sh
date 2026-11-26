# shellcheck shell=bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Sourced by dev.sh with this run's fresh output directory and run_one.
only=all compiler="" runs=7 events="" record=0 execute=0
program_args=()
while (($#)); do
    case "$1" in
        --only|--compiler|--runs|--events|--cpu|--arg)
            (($# >= 2)) || die "$1 needs a value"
            case "$1" in
                --only) only=$2 ;; --compiler) compiler=$2 ;; --runs) runs=$2 ;;
                --events) events=$2 ;; --cpu) cpu=$2 ;; --arg) program_args+=("$2") ;;
            esac
            shift 2 ;;
        --record) record=1; shift ;;
        --run) execute=1; shift ;;
        -*) die "unknown paths option $1" ;;
        *) break ;;
    esac
done
case "$only" in direct|tier0|tier1|all) ;; *) die "--only takes direct, tier0 or tier1" ;; esac
[[ $runs =~ ^[1-9][0-9]{0,3}$ ]] && ((runs <= 1000)) || die "--runs takes 1..1000"
[[ -z $cpu || $cpu =~ ^[0-9]+$ ]] || die "--cpu takes one logical CPU"
((execute || ${#program_args[@]} == 0)) || die "--arg requires --run"
(($#)) && [[ -f $1 && $1 = *.c ]] || die "paths needs one C source file"
source_file=$(realpath -- "$1")
shift
if (($#)); then [[ $1 = -- ]] || die "place compiler flags after --"; shift; fi
for flag in "$@"; do
    case "$flag" in
        -c|-o*|-E|-S|-fsyntax-only|--tier1|--no-direct|--time-stages|--tier1-report|--lanes*|--list)
            die "paths owns compiler mode, lanes and output (got $flag)" ;;
    esac
done
if [[ -z $compiler ]]; then compiler=$(./build.sh print-output --config Release); fi
[[ -x $compiler ]] || die "missing Release compiler: $compiler (run dev.sh build)"
compiler=$(realpath -- "$compiler")
sha256sum "$compiler" "$source_file" > "$out/inputs.sha256"
common=(--lanes 1 "$@" -c "$source_file")
lists=() binaries=() measured=()
printf 'list\tpath\tobject_bytes\n' > "$out/paths.tsv"

# time-runs accepts a program whose status matches the warmup. These jobs
# require successful compilation and, where requested, a zero-exit workload.
measure()
{
    local log=$1
    shift
    run_one single "$compiler" time-runs --runs "$runs" --warmups 1 "$@" | tee "$log"
    awk '/^run [0-9]+ list / { n++; if ($0 !~ /, status 0$/) bad=1 } END { exit (bad || !n) }' "$log" ||
        die "nonzero workload status or missing samples in $log"
    if grep -Eq '^run [0-9]+ list .* 0 user cycles, 0 instructions,' "$log"; then
        echo "Hardware counters unavailable for one or more samples (time-runs prints zeros)." | tee -a "$log"
    fi
}

counters()
{
    local prefix=$1
    shift
    if [[ -n $events ]]; then
        run_one single perf stat -r "$runs" -x ';' -e "$events" -o "$prefix.stat" -- "$@"
    fi
    if ((record)); then
        run_one single perf record -e cycles:u -g -o "$prefix.data" -- "$@"
        run_one single perf report -i "$prefix.data" --stdio --no-children > "$prefix.hotspots"
    fi
}

# Keep mode flags and the object destination identical for stage, timing and perf runs.
compile_args()
{
    object="$out/$1.o"
    args=("${common[@]}" -o "$object")
    case "$1" in
        tier0) args=(--no-direct "${args[@]}") ;;
        tier1) args=(--tier1 "${args[@]}") ;;
    esac
}

direct_object()
{
    local comment
    comment=$(LC_ALL=C readelf -p .comment "$1") || return 1
    grep -Eq '][[:space:]]+buster direct$' <<< "$comment"
}

for mode in direct tier0 tier1; do
    [[ $only = all || $only = "$mode" ]] || continue
    compile_args "$mode"
    diagnostics=(--time-stages)
    [[ $mode != tier1 ]] || diagnostics+=(--tier1-report)
    if ! run_one single "$compiler" "${diagnostics[@]}" "${args[@]}" > "$out/$mode.stages" 2>&1; then
        cat "$out/$mode.stages" >&2
        die "$mode compile failed"
    fi
    if [[ $mode = direct ]]; then
        if ! direct_object "$object"; then
            mv -- "$object" "$out/fallback.o"
            mv -- "$out/direct.stages" "$out/fallback.stages"
            echo "direct: fell back to the full pipeline; skipping direct measurements" | tee "$out/direct.skipped" >&2
            [[ $only = all ]] || exit 1
            continue
        fi
    elif [[ $mode = tier1 ]]; then
        awk -F '\t' '$1 == "tier1" { for (i=2; i<=NF; i++) if ($i ~ /^(compiled|fallbacks)=/) printf "%s ",$i; print "" }' "$out/tier1.stages"
    fi
    lists+=(--list "${args[@]}")
    measured+=("$mode")
done
measure "$out/compile.txt" "${lists[@]}"
list_number=0
for mode in "${measured[@]}"; do
    compile_args "$mode"
    if [[ $mode = direct ]] && ! direct_object "$object"; then die "timed compile lost its direct ELF marker"; fi
    list_number=$((list_number + 1))
    printf '%s\t%s\t%s\n' "$list_number" "$mode" "$(stat -c %s "$object")" >> "$out/paths.tsv"
    size -A "$object" > "$out/$mode.size"
    if ((execute)); then
        program="$out/$mode.exe"
        run_one single "${CC:-cc}" -no-pie "$object" -o "$program"
        run_one single "$program" "${program_args[@]}" > "$out/$mode.stdout" || die "$mode program failed"
        if (("${#binaries[@]}")); then
            cmp -- "$out/$first_mode.stdout" "$out/$mode.stdout" || die "generated programs produced different stdout"
        else
            first_mode=$mode
        fi
        binaries+=(--build "$mode" "$program")
    fi
done
cat "$out/paths.tsv"
if ((execute)); then measure "$out/runtime.txt" "${binaries[@]}" --list "${program_args[@]}"; fi
if [[ -n $events || $record = 1 ]]; then
    for mode in "${measured[@]}"; do
        compile_args "$mode"
        counters "$out/$mode.compile" "$compiler" "${args[@]}"
        if ((execute)); then counters "$out/$mode.run" "$out/$mode.exe" "${program_args[@]}"; fi
    done
fi
sha256sum --check --status "$out/inputs.sha256" || die "compiler or source changed during this run"
