#!/usr/bin/env bash
# One paired probe executable, or two separate Buster executables and an output directory.
set -eu -o pipefail
export LC_ALL=C
base=$(realpath "$1")
head=$(realpath "${2:-$1}")
fixture=$(realpath "$(dirname "$0")/input.S")
if [ "$#" -ge 3 ]; then
    mkdir -p "$3"
    cd "$3"
else
    scratch=$(mktemp -d)
    trap 'rm -rf -- "$scratch"' EXIT
    cd "$scratch"
fi
uname -m
lscpu | awk '/^Model name:/ {print}'
gcc --version | head -1
as --version | head -1
for side in before after; do
    exe=$base
    [ "$side" = before ] || exe=$head
    objdump -d --no-show-raw-insn --disassemble="refs3166_clean_$side" "$exe" > "$side.asm"
    if grep -q "<refs3166_clean_$side>:" "$side.asm"; then
        echo "ASM3166 $side"
        sed -n "/<refs3166_clean_$side>:/,/^$/p" "$side.asm"
    fi
done
for n in 64 256; do
    for variant in ordinary bound positive negative bound_negative short_field unsupported_zero bad_then_interior unique; do
        bound=0
        unique=0
        mode=0
        case $variant in
            bound) bound=1 ;;
            positive) mode=1 ;;
            negative) mode=2 ;;
            bound_negative) mode=2; bound=1 ;;
            short_field) mode=3 ;;
            unsupported_zero) mode=5 ;;
            bad_then_interior) mode=6 ;;
            unique) mode=4; unique=1 ;;
        esac
        key=$n-$variant
        mkdir -p "$key"
        (
        cd "$key"
        gcc -c -x assembler-with-cpp -DWIN=1 -DBOUND=$bound "$fixture" -o w.o
        gcc -c -x assembler-with-cpp -DWIN=0 -DREFS=$n -DMODE=$mode -DBOUND=$bound -DUNIQUE=$unique "$fixture" -o l.o
        sha256sum w.o l.o > objects.sha256
        readelf -Wr l.o > input.rela
        readelf -Wg l.o > input.groups
        readelf -SW l.o > input.sections
        readelf -wf l.o > input.frames
        grep -q 'COMDAT group' input.groups
        grep -q 'R_X86_64_NONE.*middle3166 + 7' input.rela
        test "$(grep -c ' FDE cie=' input.frames)" = 2
        rela=.rela.rodata.refs3166
        [ "$bound" = 0 ] || rela=.relatable3166
        expected=
        if [ "$mode" = 1 ] || [ "$mode" = 2 ] || [ "$mode" = 6 ]; then
            sec=$(sed -nE 's/^ *\[ *([0-9]+)\] +([^[:space:]]+).*/\1 \2/p' input.sections | awk -v s="$rela" '$2==s {print $1}')
            read -r index info < <(awk -v s="'$rela'" '
                /^Relocation section / { use=($3==s); i=0; next }
                use && $1 ~ /^[[:xdigit:]]+$/ && $3 ~ /^R_X86_64_/ {
                    if (($5=="middle3166" && $6=="+" && $7=="4") || ($5=="other3166" && $6=="-" && $7=="4")) { print i,$2; exit }
                    i++
                }' input.rela)
            # ELF64 r_info is the symbol index followed by the type.
            expected="2 $sec $index $((16#${info:0:8})) $((16#${info:8:8})) $((index * 24))"
        fi
        for lanes in 1 3; do
            for side in before after; do
                exe=$base
                [ "$side" = before ] || exe=$head
                export BUSTER_UNWIND_SIDE_3166=$side
                unset BUSTER_TRACE_UNWIND_3166
                prefix=$side-$lanes
                printf 'sentinel\n' > "$prefix.elf"
                rc=0
                "$exe" link-foreign --lanes "$lanes" -static -e _start -o "$prefix.elf" w.o l.o > "$prefix.out" 2> "$prefix.err" || rc=$?
                echo "$rc" > "$prefix.status"
                printf 'sentinel\n' > traced.elf
                observed=0
                BUSTER_TRACE_UNWIND_3166=1 "$exe" link-foreign --lanes "$lanes" -static -e _start -o traced.elf w.o l.o > traced.out 2> "$prefix.trace" || observed=$?
                test "$rc" = "$observed"
                sed '/^VIS3166 /d; /^ERR3166 /d' "$prefix.trace" > traced.err
                cmp traced.elf "$prefix.elf"
                cmp traced.out "$prefix.out"
                cmp traced.err "$prefix.err"
                awk '/^ERR3166 / {print $2,$3,$4,$5,$6,$7}' "$prefix.trace" > "$prefix.fields"
                awk '/^VIS3166 / {z+=$4; none+=$5; eligible+=$6; calls++}
                     END {if(calls) print z+0,none+0,eligible+0,z+none+eligible}' "$prefix.trace" > "$prefix.nodes"
                if [ -s "$prefix.nodes" ]; then
                    test "$(cat "$prefix.fields")" = "$expected"
                fi
                if [ "$mode" = 0 ] || [ "$unique" = 1 ]; then
                    if [ "$rc" != 0 ]; then cat "$prefix.err"; exit 1; fi
                    test ! -s "$prefix.err"
                    ran=0
                    "./$prefix.elf" || ran=$?
                    test "$ran" = 42
                    readelf -wf "$prefix.elf" > "$prefix.frames"
                    test "$(grep -c ' FDE cie=' "$prefix.frames")" = "$((3+unique))"
                else
                    test "$rc" = 1
                    test "$(cat "$prefix.elf")" = sentinel
                    grep -q '^link-foreign: ' "$prefix.err"
                fi
            done
            for field in elf out err status fields; do
                cmp "before-$lanes.$field" "after-$lanes.$field"
                cmp "after-1.$field" "after-$lanes.$field"
            done
            if [ -s "before-$lanes.nodes" ]; then
                read -r bz bn be bt < "before-$lanes.nodes"
                read -r az an ae at < "after-$lanes.nodes"
                printf 'NODES %s/%d before=%d,%d,%d,%d after=%d,%d,%d,%d\n' "$key" "$lanes" "$bz" "$bn" "$be" "$bt" "$az" "$an" "$ae" "$at"
                test "$az" = 0
                test "$an" = 0
                test "$ae" = "$be"
                if [ "$unique" = 0 ]; then
                    test "$bz" -gt 0
                    test "$bn" = 1
                fi
            fi
            digest=$(sha256sum "after-$lanes.elf" | cut -d' ' -f1)
            printf 'MATCH %s/%d status=%s bytes=%s sha256=%s\n' "$key" "$lanes" "$(cat after-$lanes.status)" "$(wc -c < after-$lanes.elf)" "$digest"
            if [ -n "$expected" ]; then printf 'FIELDS %s/%d %s\n' "$key" "$lanes" "$expected"; fi
            if [ -s "after-$lanes.err" ]; then cat "after-$lanes.err"; fi
        done
        )
    done
done
echo 'PAIR3166 passed'
