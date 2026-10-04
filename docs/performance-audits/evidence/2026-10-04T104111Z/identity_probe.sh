#!/usr/bin/env bash
# Output-identity probe: does the removed validation pass change generated
# code, or only debug information? Builds plain base/probe compilers (reusing
# tools/work_ledger/build_subject.sh) and compares the stage-1 self-compile
# outputs with -g and with -g0, per ELF section. Runs ONLY on a hosted runner.
#
# usage: identity_probe.sh <pinned-root> <probe-root> <output-dir>
set -euo pipefail
ROOT=$(realpath "$1")
PROBE_SRC=$(realpath "$2")
OUT=$(realpath -m "$3")
mkdir -p "$OUT"
{
    echo "pinned_head=$(git -C "$ROOT" rev-parse HEAD)"
    echo "probe_diffstat=$(git -C "$PROBE_SRC" diff --stat | tr '\n' ' ')"
    echo "clang=$(clang --version | head -1)"
    echo "binutils=$(readelf --version | head -1)"
} > "$OUT/identity.txt"
bash "$ROOT/tools/work_ledger/build_subject.sh" "$ROOT" "$OUT/base"
bash "$ROOT/tools/work_ledger/build_subject.sh" "$PROBE_SRC" "$OUT/probe"
sha256sum "$OUT"/base/ide-plain "$OUT"/probe/ide-plain > "$OUT/compilers.sha256"

cd "$ROOT"
COMMON=(cc -Isrc -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -march=znver3 src/buster/apps/ide/ide.c -lm)
: > "$OUT/outputs.sha256"
: > "$OUT/sections.sha256"
for G in g g0; do
    for S in base probe; do
        EXE="$OUT/self-$S-$G.out"
        "$OUT/$S/ide-plain" "${COMMON[@]}" "-$G" -o "$EXE"
        sha256sum "$EXE" >> "$OUT/outputs.sha256"
        readelf -S -W "$EXE" > "$OUT/self-$S-$G.sections.txt"
        for SEC in .text .rodata .data .data.rel.ro .bss .init_array .fini_array .symtab .strtab .dynsym .dynstr .rela.dyn .rela.plt \
                   .debug_info .debug_abbrev .debug_line .debug_str .debug_line_str .debug_aranges .debug_frame .eh_frame .eh_frame_hdr; do
            if grep -q " $SEC " "$OUT/self-$S-$G.sections.txt"; then
                objcopy -O binary --only-section="$SEC" "$EXE" "$OUT/sec.bin" 2>/dev/null || continue
                printf '%s  %s %s %s %s\n' "$(sha256sum "$OUT/sec.bin" | cut -d' ' -f1)" "$(stat -c %s "$OUT/sec.bin")" "$S" "$G" "$SEC" >> "$OUT/sections.sha256"
            fi
        done
        rm -f "$OUT/sec.bin"
    done
done
echo "== whole-file hashes ==" | tee "$OUT/verdict.txt"
cat "$OUT/outputs.sha256" | tee -a "$OUT/verdict.txt"
echo "== sections differing between base and probe ==" | tee -a "$OUT/verdict.txt"
for G in g g0; do
    join -j1 <(grep " base $G " "$OUT/sections.sha256" | awk '{print $5, $1, $2}' | sort) \
             <(grep " probe $G " "$OUT/sections.sha256" | awk '{print $5, $1, $2}' | sort) \
        | awk -v g="$G" '{ status = ($2 == $4 && $3 == $5) ? "same" : "DIFF"; printf "%-4s %-16s %s base=%s/%s probe=%s/%s\n", g, $1, status, substr($2,1,12), $3, substr($4,1,12), $5 }' | tee -a "$OUT/verdict.txt"
done
rm -f "$OUT"/self-*.out "$OUT"/base/ide-* "$OUT"/probe/ide-*
echo "IDENTITY_DONE"
