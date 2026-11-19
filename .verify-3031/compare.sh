#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Run in a disposable Buster checkout with the hosted Release toolchain on PATH.
set -euo pipefail
export LC_ALL=C BUSTER_HOST_ARCH=vector BUSTER_DEBUG_INFO=0 BUSTER_SANITIZER=none BUSTER_SANITIZER_OPTIMIZATION=3
base=$1 head=$2 out=$3
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"/{base,head}
cat > "$out/aux.c" <<'EOF'
unsigned dwarf_aux(unsigned x)
{
#line 10 "up.c"
    x = x * 7 + 3;
#line 6 "down.c"
    x ^= x >> 5;
#line 8 "up.c"
    return x - 2;
}
EOF

capture() {
    local file=$1 stem=$2
    llvm-dwarfdump --verify "$file" > "$stem.verify" || { cat "$stem.verify"; return 1; }
    llvm-dwarfdump --debug-line "$file" > "$stem.dump"
    # Keep file tables, sequence boundaries and every ordered row and flag. The
    # only two line-table header fields allowed to move are its offset and size.
    sed -n '/^\.debug_line contents:/,$p' "$stem.dump" |
        sed -E '/^[[:space:]]*total_length:/d; s/^debug_line\[0x[[:xdigit:]]+\]/debug_line[unit]/' > "$stem.matrix"
    awk '/^0x[[:xdigit:]]+[[:space:]]+[0-9]+[[:space:]]/ { print }' "$stem.matrix" > "$stem.rows"
    test -s "$stem.rows"
    readelf -wl "$file" > "$stem.program"
    size -A -d "$file" | awk '$1==".debug_line"{print $2}' > "$stem.size"
    objcopy -O binary --only-section=.text "$file" "$stem.text"
    test -s "$stem.text"
}

relocations() {
    local file=$1 stem=$2
    readelf -rW "$file" | awk '/^Relocation section / { target=($3=="\047.rela.debug_line\047"); next }
        target && $3=="R_X86_64_64" { print }' > "$stem.reloc"
    awk '{ print $1 }' "$stem.reloc" > "$stem.slots"
    awk '{$1=""; print}' "$stem.reloc" > "$stem.targets"
    # readelf decodes the opcode's location independently. Its 8-byte operand
    # starts after the extended-opcode escape, length, and DW_LNE_set_address.
    awk '/Extended opcode 2: set Address to/ { gsub(/[^[:xdigit:]x]/,"",$1); print $1 }' "$stem.program" |
        while read -r offset; do printf '%016x\n' "$((offset + 3))"; done > "$stem.operands"
    test -s "$stem.slots"
    diff -u "$stem.operands" "$stem.slots"
}

for version in base head; do
    rev=$base
    if [[ $version == head ]]; then rev=$head; fi
    git checkout --quiet --detach "$rev"
    echo "$version $(git rev-parse HEAD)"
    ./build.sh --config Release
    dir=$out/$version
    build/compiler -g -c "$out/aux.c" -o "$dir/aux.o"
    for source in tools/codegen/unreferenced.c tools/ci/rerun.c; do
        name=$(basename "$source" .c)
        build/compiler -g -c "$source" -o "$dir/$name.o"
        build/compiler --scalar -g -c "$source" -o "$dir/$name.scalar.o"
        cmp "$dir/$name.o" "$dir/$name.scalar.o"
        build/compiler -c "$source" -o "$dir/$name.plain.o"
        cc -fuse-ld=bfd -no-pie -Wl,--build-id=none "$dir/$name.o" "$dir/aux.o" -o "$dir/$name.elf"
    done
    mapfile -d '' -t flags < <(./build.sh print-host-flags)
    clang -std=c17 -O3 "${flags[@]}" -Isrc/buster/compiler "$here/rows.c" -o "$dir/rows"
    "$dir/rows" > "$dir/rows.s"
    as --64 "$dir/rows.s" -o "$dir/rows.o"
    ld.bfd -e first -Ttext=0x420000 "$dir/rows.o" -o "$dir/rows.elf"
    for name in unreferenced rerun rows; do
        for kind in o elf; do
            stem="$dir/$name.$kind"
            capture "$stem" "$stem"
            if [[ $kind == o ]]; then relocations "$stem" "$stem"; fi
        done
    done
    build/compiler test-debug --cross-check
done

for name in unreferenced rerun rows; do
    for kind in o elf; do
        a="$out/base/$name.$kind" b="$out/head/$name.$kind"
        diff -u "$a.matrix" "$b.matrix"
        cmp "$a.text" "$b.text"
        old=$(cat "$a.size") new=$(cat "$b.size")
        before=$(awk '/Special opcode/{n++} END{print n+0}' "$a.program")
        specials=$(awk '/Special opcode/{n++} END{print n+0}' "$b.program")
        test "$before" -eq 0
        test "$specials" -gt 0
        test "$((old - new))" -eq "$((4 * specials))"
        echo "$name.$kind .debug_line=$old->$new special=$specials text=$(wc -c < "$a.text")"
        awk '{n++; pe+=/prologue_end/; es+=/end_sequence/; if(prev) {d+=($1==addr); f+=($4!=file); down+=($2+0<line)}
              addr=$1; line=$2+0; file=$4; prev=!/end_sequence/}
             END{print "rows="n,"prologue_end="pe,"end_sequence="es,"duplicate_pc="d+0,"file_switches="f+0,"decreasing_lines="down+0}' "$b.rows"
        sha256sum "$b.matrix" "$b.text"
        if [[ $kind == o ]]; then
            diff -u "$a.targets" "$b.targets"
            paste "$a.slots" "$b.slots" | awk '$1!=$2{if(!m) print "first moved slot", $1, $2; m++}
                END{print "relocations="NR, "moved="m+0; exit !(NR>1 && m>0)}'
        elif [[ $name != rows ]]; then
            cmp "$out/base/$name.plain.o" "$out/head/$name.plain.o"
            for version in base head; do
                dir=$out/$version
                readelf -wi "$dir/$name.elf" | awk '/DW_AT_stmt_list[[:space:]]*:/{print $NF}' > "$dir/$name.stmt"
                mapfile -t stmt < "$dir/$name.stmt"
                test "${#stmt[@]}" -eq 2
                test "$((stmt[0]))" -eq 0
                test "$((stmt[1]))" -eq "$(cat "$dir/$name.o.size")"
                echo "$version second CU statement list ${stmt[1]}"
            done
        fi
    done
done
awk '$1=="0x0000000000420000" {total++; if ((total==1 && $2!=6) || (total==2 && ($2!=1 || !/prologue_end/))) bad=1}
     END{exit !(total==2 && !bad)}' "$out/head/rows.elf.rows"
grep 'prologue_end' "$out/head/rows.elf.rows"
grep 'end_sequence' "$out/head/rows.elf.rows"
