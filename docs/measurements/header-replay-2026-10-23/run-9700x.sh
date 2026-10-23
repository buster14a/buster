#!/usr/bin/env bash
# Small build/run glue for the original C transcript experiment. One lock per run.
set -euo pipefail
replay_root="${1:-$PWD}"
cd "$replay_root"
mkdir -p logs generated
if [[ -e hold-for-116 ]]; then
    printf 'The approved issue116 screen has priority; this job is prepared but held.\n' >&2
    exit 2
fi
export EXCLUSIVE_OWNER=codex-header111
export EXCLUSIVE_TITLE='Header transcript experiment #111'
export EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/111
export SOURCE_DATE_EPOCH=1767225600
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH
unset PROBE_PARSE PROBE_CONDITION_MS PROBE_FILES_MANIFEST
phase=starting
screen="${HEADER111_SCREEN:-0}"
screen_deadline="${HEADER111_SCREEN_DEADLINE:-0}"
trap 'result=$?; printf "exit=%s phase=%s utc=%s\n" "$result" "$phase" "$(date -u +%FT%TZ)" > "$replay_root/logs/status.txt"' EXIT
lock_run()
{
    if [[ "$screen" == 1 && "$screen_deadline" != 0 ]] && (( $(date +%s) >= screen_deadline )); then
        phase=screen-time-limited
        exit 0
    fi
    phase="$1"
    shift
    printf 'Running %s\n' "$phase"
    printf 'running phase=%s utc=%s\n' "$phase" "$(date -u +%FT%TZ)" > "$replay_root/logs/status.txt"
    {
        printf 'command '
        printf '%q ' "$@"
        printf '\n'
        "$HOME/buster-fast-bench/exclusive.sh" "$@"
    } > "$replay_root/logs/$phase.log" 2>&1
    if [[ "$screen" == 1 && "$screen_deadline" == 0 ]]; then
        date -u +%FT%TZ > "$replay_root/logs/screen-first-acquisition.txt"
        screen_deadline=$(( $(date +%s) + 270 ))
    fi
}
{
    date -u +%FT%TZ
    uname -a
    lscpu
    clang --version
    cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor
    cat /sys/devices/system/cpu/cpu2/topology/thread_siblings_list
    cat /sys/devices/system/cpu/online
} > logs/host.txt
lock_run source-identity sha256sum -c source.sha256
flags=(-O3 -g -march=native -I"$replay_root/tree/src" -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror)
mkdir -p reference-harness
cp "$replay_root/final-probe/probe.c" reference-harness/probe.c
sha256sum reference-harness/probe.c > logs/reference-harness.sha256
if [[ "${HEADER111_REUSE_BUILDS:-0}" != 1 ]]; then
    lock_run build-reference clang -I"$replay_root/base-probe" "${flags[@]}" -MMD -MF "$replay_root/logs/reference.d" -DBUSTER_PROBE_REFERENCE "$replay_root/reference-harness/probe.c" -o "$replay_root/reference"
    lock_run build-experiment clang -I"$replay_root/final-probe" "${flags[@]}" "$replay_root/final-probe/experiment.c" -o "$replay_root/experiment"
else
    lock_run reuse-builds sha256sum -c logs/binaries.sha256
fi
if [[ ! -x "$replay_root/measure-run" ]]; then
    lock_run build-resource-observer clang -O2 -march=native -Wall -Wextra -Werror "$replay_root/measure-run.c" -o "$replay_root/measure-run"
fi
sha256sum reference experiment > logs/binaries.sha256
lock_run guard taskset -c 2 "$replay_root/experiment" guard
lock_run state-fixtures taskset -c 2 "$replay_root/experiment" verify -- "$replay_root/state-fixtures/"*.c
lock_run source-identities taskset -c 2 "$replay_root/experiment" source "$replay_root/state-fixtures/virtual.h"
musl="$replay_root/musl"
lua="$replay_root/lua-5.4.8/src"
cd "$musl"
shopt -s nullglob
# Match musl's SRC_DIRS, ARCH_SRCS and REPLACED_OBJS for the static libc C units.
# Assembly, CRT and the dynamic loader are separate build products.
declare -A replaced
for source in src/*/x86_64/*.[csS] src/malloc/mallocng/x86_64/*.[csS]; do
    generic="${source/\/x86_64\//\/}"
    replaced["${generic%.*}.c"]=1
done
musl_units=()
for source in src/*/*.c src/malloc/mallocng/*.c src/*/x86_64/*.c src/malloc/mallocng/x86_64/*.c; do
    if [[ -z "${replaced[$source]:-}" ]]; then
        musl_units+=("$source")
    fi
done
musl_options=(-std=c99 -nostdinc -ffreestanding -Os -D_XOPEN_SOURCE=700 -I./arch/x86_64 -I./arch/generic -Iobj/src/internal -I./src/include -I./src/internal -Iobj/include -I./include)
printf '%s\n' "${musl_units[@]}" > "$replay_root/logs/musl-units.txt"
if [[ "$screen" != 1 ]]; then
    lock_run verify-musl taskset -c 2 "$replay_root/experiment" verify "${musl_options[@]}" -- "${musl_units[@]}"
fi
cd "$lua"
lua_units=(*.c)
lua_options=(-std=gnu99 -O2 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX)
printf '%s\n' "${lua_units[@]}" > "$replay_root/logs/lua-units.txt"
if [[ "$screen" != 1 ]]; then
    lock_run verify-lua taskset -c 2 "$replay_root/experiment" verify "${lua_options[@]}" -- "${lua_units[@]}"
fi
cd "$replay_root"
if [[ "$screen" != 1 ]]; then
    lock_run generate-cases taskset -c 2 "$replay_root/experiment" generate "$replay_root/generated"
    lock_run verify-c17 taskset -c 2 "$replay_root/experiment" verify -std=c17 -- generated/c17-*.c
    lock_run verify-c23 taskset -c 2 "$replay_root/experiment" verify -std=c23 -- generated/c23-*.c
    lock_run verify-copy-twin env VERIFY_CACHE_SCALAR=1 taskset -c 2 "$replay_root/experiment" verify -std=c17 -- generated/c17-*.c
fi
conditions=(0 100)
consumers=(0 1)
if [[ "$screen" == 1 ]]; then
    conditions=(0)
    consumers=(0)
fi
if [[ "${HEADER111_QUALIFY_ONLY:-0}" == 1 ]]; then
    conditions=()
fi
for conditioned in "${conditions[@]}"; do
    for parse in "${consumers[@]}"; do
        for round in 1 2 3; do
            if (( round % 2 )); then
                order=(reference experiment)
            else
                order=(experiment reference)
            fi
            for variant in "${order[@]}"; do
                program=("$replay_root/$variant")
                if [[ "$variant" == experiment ]]; then
                    program+=(benchmark)
                fi
                mode=()
                if [[ "$parse" == 1 ]]; then
                    mode+=(PROBE_PARSE=1)
                fi
                for workload in musl lua; do
                    if [[ "$workload" == musl ]]; then
                        cd "$musl"
                        options=("${musl_options[@]}")
                        units=("${musl_units[@]}")
                    else
                        cd "$lua"
                        options=("${lua_options[@]}")
                        units=("${lua_units[@]}")
                    fi
                    label="$workload-$variant-parse$parse-after$conditioned-round$round"
                    lock_run "$label" "$replay_root/measure-run" env "${mode[@]}" PROBE_PASSES=8 PROBE_CONDITION_MS="$conditioned" \
                        PROBE_FILES_MANIFEST="$replay_root/logs/$label.files" taskset -c 2 "${program[@]}" "${options[@]}" -- "${units[@]}"
                done
            done
        done
    done
done
cd "$replay_root"
if [[ "$screen" == 1 ]]; then
    phase=screen-complete
    date -u +%FT%TZ > "$replay_root/logs/screen-complete.txt"
    exit 0
fi
# Hash every resolved source, including the external system headers used by Lua.
for workload in musl lua; do
    if [[ "$workload" == musl ]]; then
        cd "$musl"
    else
        cd "$lua"
    fi
    sort -u "$replay_root/logs/$workload-reference-parse0-after0-round1.files" > "$replay_root/logs/$workload-resolved-files.txt"
    lock_run "$workload-input-identity" xargs -r -d '\n' sha256sum < "$replay_root/logs/$workload-resolved-files.txt"
done
cd "$replay_root"
# Bounded groups keep every correctness run well below the shared-host limit.
libc=/var/lib/buster-9700x/corpora/preprocessor-391e729af658/libc
mapfile -d '' libc_units < <(find "$libc" -type f \( -name '*.c' -o -name '*.h' \) -print0 | sort -z)
for ((at=0, group=0; at<${#libc_units[@]}; at+=512, group+=1)); do
    lock_run "verify-libc-$group" taskset -c 2 "$replay_root/experiment" verify -- "${libc_units[@]:at:512}"
done
lock_run build-metrics clang -I"$replay_root/final-probe" "${flags[@]}" "$replay_root/final-probe/metrics.c" -o "$replay_root/metrics"
sha256sum "$replay_root/final-probe/metrics.c" "$replay_root/measure-run.c" "$replay_root/metrics" "$replay_root/measure-run" > logs/diagnostics.sha256
cd "$musl"
lock_run metrics-musl taskset -c 2 "$replay_root/metrics" "${musl_options[@]}" -- "${musl_units[@]}"
cd "$lua"
lock_run metrics-lua taskset -c 2 "$replay_root/metrics" "${lua_options[@]}" -- "${lua_units[@]}"
cd "$replay_root"
phase=complete
printf 'Requested header transcript campaign completed.\n'
