#!/usr/bin/env bash
# Run against either stock compiler or the separately instrumented build.
set -eu -o pipefail
export LC_ALL=C
compiler=$(realpath "$1")
source_file=$(realpath "$(dirname "$0")/input.S")
if [ "$#" -gt 1 ]; then
    mkdir -p "$2"
    cd "$2"
else
    scratch=$(mktemp -d)
    trap 'rm -rf -- "$scratch"' EXIT
    cd "$scratch"
fi
uname -m
lscpu | awk '/^Model name:/ {print}'
gcc --version | head -1
for n in 64 256; do
    for variant in ordinary bound positive negative bound_negative short_field unique; do
        bound=0
        unique=0
        mode=0
        case $variant in
            bound) bound=1 ;;
            positive) mode=1 ;;
            negative) mode=2 ;;
            bound_negative) mode=2; bound=1 ;;
            short_field) mode=3 ;;
            unique) mode=4; unique=1 ;;
        esac
        d=$n-$variant
        mkdir -p "$d"
        (
        cd "$d"
        gcc -c -x assembler-with-cpp -DWIN=1 -DBOUND=$bound "$source_file" -o w.o
        gcc -c -x assembler-with-cpp -DWIN=0 -DREFS=$n -DMODE=$mode -DBOUND=$bound -DUNIQUE=$unique "$source_file" -o l.o
        readelf -Wr l.o > input.rela
        readelf -Wg l.o > input.groups
        readelf -wf l.o > input.frames
        grep -q 'COMDAT group' input.groups
        grep -q 'R_X86_64_NONE.*middle3166 + 7' input.rela
        test "$(grep -c ' FDE cie=' input.frames)" = 2
        for lanes in 1 3; do
            export BUSTER_TRACE_UNWIND_3166=1
            printf 'sentinel\n' > image
            status=0
            "$compiler" link-foreign --lanes "$lanes" -static -e _start -o image w.o l.o > out 2> trace || status=$?
            unset BUSTER_TRACE_UNWIND_3166
            sed '/^VIS3166 /d; /^ERR3166 /d' trace > diag
            # A second link observes the normal driver's complete output.
            printf 'sentinel\n' > clean
            normal=0
            "$compiler" link-foreign --lanes "$lanes" -static -e _start -o clean w.o l.o > plain.out 2> plain.err || normal=$?
            cmp out plain.out
            cmp diag plain.err
            test "$normal" = "$status"
            cmp image clean
            mv image "image-$lanes"
            cp diag "diag-$lanes"
            awk -v key="$d/$lanes" '/^VIS3166 / { z+=$4; none+=$5; other+=$6; active++ }
                END { if(active) printf "NODES %s zero=%d none=%d eligible=%d total=%d\n",key,z,none,other,z+none+other }' trace
            # The diagnostic carries its original RELA index and byte offset.
            awk -v key="$d/$lanes" '/^ERR3166 / { print "FIELDS",key,$2,$3,$4,$5,$6,$7 }' trace
            printf 'RESULT %s/%d status=%d bytes=%s sha256=%s\n' "$d" "$lanes" "$status" "$(wc -c < clean)" "$(sha256sum clean | cut -d' ' -f1)"
            if [ "$mode" = 0 ] || [ "$unique" = 1 ]; then
                if [ "$status" != 0 ]; then cat trace; exit 1; fi
                test ! -s diag
                run_status=0
                ./clean || run_status=$?
                test "$run_status" = 42
                readelf -wf clean > output.frames
                test "$(grep -c ' FDE cie=' output.frames)" = "$((3+unique))"
            else
                cat diag
                test "$status" = 1
                test "$(cat clean)" = sentinel
                grep -q '^link-foreign: .*relocation in l.o (section [0-9]*, relocation [0-9]*, offset [0-9]*)$' diag
                if [ "$mode" != 3 ]; then
                    # readelf prints the original order and the two signed addends.
                    awk '/R_X86_64_(64|PC32)/ && ( $5=="middle3166" && $6=="+" && $7=="4" || $5=="other3166" && $6=="-" && $7=="4" )' input.rela
                fi
            fi
        done
        cmp image-1 image-3
        cmp diag-1 diag-3
        )
    done
done
