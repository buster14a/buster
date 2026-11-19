#!/usr/bin/env bash
set -euo pipefail
evidence=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d)
base=${1:?base compiler SHA}
head=${2:?head compiler SHA}
minmax=ebbffc8f5b8b3575f78ed503cd226bb8fa3d4f11
float=8a407c64085de96835bb30e0cea22b231c4add80
git fetch --no-tags --depth=1 origin "$base" "$head" "$minmax" "$float"
git worktree add --detach "$tmp/tree" "$base"
trap 'git -C "$root" worktree remove --force "$tmp/tree"' EXIT
git show "$minmax:tests/ir/vectorize/minmax.c" > "$tmp/minmax.c"
git show "$float:tests/driver/tier1_patterns.c" | awk '/^__attribute__.* patterns_float_[[:alnum:]_]+\(/ { show=1 } show { print } /^}/ { show=0 }' > "$tmp/float.c"
test "$(grep -c '^__attribute__' "$tmp/float.c")" -eq 9
awk '/^unsigned (shared32|volatile32|guarded|quotient|float_condition|plain_reload)\(/ { show=1 } show { print } /^}/ { show=0 }' "$evidence/conditional.c" > "$tmp/controls.c"
test "$(grep -c '^unsigned ' "$tmp/controls.c")" -eq 6
cd "$tmp/tree"
export BUSTER_HOST_ARCH=vector BUSTER_SANITIZER=none BUSTER_DEBUG_INFO=0
tool_path=$(bash tools/ci/session-toolchain.sh stage)
export PATH="$tool_path:$PATH"
clang --version
cmake --version | head -1
ninja --version
elf=$(realpath "$(command -v clang)")
sha256sum "$elf"
flags=(--tier1 --tier1-strict --verify -O2 -march=x86-64-v4)
clang -O2 -march=x86-64-v4 -c "$evidence/conditional.c" -o "$tmp/clang.o"
objcopy --prefix-symbols=clang_ "$tmp/clang.o" "$tmp/oracle.o"
for rev in base head; do
    sha=${!rev}
    git checkout --detach "$sha"
    ./build.sh --config Release
    cp build/compiler "$tmp/compiler-$rev"
    echo "BEGIN $rev $sha"
    build/compiler test-ir-optimize
    build/compiler "${flags[@]}" -c "$evidence/conditional.c" -o "$tmp/$rev.o"
    objdump -drw -Mintel "$tmp/$rev.o"
    clang -O2 -march=x86-64-v4 "$evidence/check.c" "$tmp/$rev.o" "$tmp/oracle.o" -o "$tmp/check"
    "$tmp/check" "$elf"
    for unit in controls float minmax; do
        passes=()
        if [[ $unit == minmax ]]; then passes=(--tier1-passes=+vectorize); fi
        build/compiler "${flags[@]}" "${passes[@]}" -c "$tmp/$unit.c" -o "$tmp/$rev-$unit.o"
        objcopy -O binary --only-section=.text "$tmp/$rev-$unit.o" "$tmp/$rev-$unit.text"
    done
    echo "END $rev"
done
for unit in controls float minmax; do
    cmp "$tmp/base-$unit.text" "$tmp/head-$unit.text"
    sha256sum "$tmp/base-$unit.text" "$tmp/head-$unit.text"
done

# Run both supplemental workloads and retain their individual exit statuses.
set +e
(
set -e
clang -std=c23 -O2 -march=x86-64-v4 -c "$evidence/bitint.c" -o "$tmp/clang-bitint.o"
objcopy --prefix-symbols=clang_ "$tmp/clang-bitint.o" "$tmp/narrow-oracle.o"
for rev in base head; do
    echo "BEGIN $rev bit-precise"
    "$tmp/compiler-$rev" --tier1 --tier1-strict --verify -O2 -std=c23 -march=x86-64-v4 -c "$evidence/bitint.c" -o "$tmp/$rev-bitint.o"
    objdump -drw -Mintel "$tmp/$rev-bitint.o"
    clang -std=c23 -O2 -march=x86-64-v4 "$evidence/check-bitint.c" "$tmp/$rev-bitint.o" "$tmp/narrow-oracle.o" -o "$tmp/check-bitint"
    "$tmp/check-bitint"
    echo "END $rev bit-precise"
done

)
bit_status=$?
(
set -e
bash tools/ci/fetch-corpus.sh "$tmp/corpus"
sqlite="$tmp/corpus/sqlite"
sha256sum "$sqlite/sqlite3.c"
wc -c "$sqlite/sqlite3.c"
mapfile -t defines < <(sed -n '/static const char\* const sqlite_test_shell_options\[/,/^};/s/.*"\(-D[^"]*\)".*/\1/p' src/buster/compiler/sqlite_test.c)
test "${#defines[@]}" -ge 9
args=(-O2 -march=x86-64-v4 -fno-strict-aliasing -I"$sqlite" "${defines[@]}")
clang "${args[@]}" -c "$sqlite/shell.c" -o "$tmp/shell.o"
clang "${args[@]}" -c "$sqlite/sqlite3.c" -o "$tmp/clang.o"
cp tests/sqlite/core.sql "$tmp/core.sql"
printf '\n.print BUSTER_3049_SQL_END\n' >> "$tmp/core.sql"
for rev in clang base head; do
    if [[ $rev != clang ]]; then
        "$tmp/compiler-$rev" --tier1 "${args[@]}" -c "$sqlite/sqlite3.c" -o "$tmp/$rev.o"
    fi
    clang "$tmp/shell.o" "$tmp/$rev.o" -lm -ldl -lpthread -o "$tmp/sqlite-$rev"
    status=0
    "$tmp/sqlite-$rev" -batch :memory: < "$tmp/core.sql" > "$tmp/$rev.out" 2> "$tmp/$rev.err" || status=$?
    echo "$status" > "$tmp/$rev.status"
    test "$status" -le 1
    grep -Fxq BUSTER_3049_SQL_END "$tmp/$rev.out"
    sha256sum "$tmp/$rev.err"
    echo "SQLite $rev status $status"
    sha256sum "$tmp/$rev.out"
    wc -lc "$tmp/$rev.out"
    size --format=sysv "$tmp/$rev.o" | awk '$1==".text" {print "SQLite .text bytes", $2}'
done
cmp "$tmp/clang.out" "$tmp/base.out"
cmp "$tmp/clang.out" "$tmp/head.out"
for rev in base head; do
    cmp "$tmp/clang.err" "$tmp/$rev.err"
    cmp "$tmp/clang.status" "$tmp/$rev.status"
done
# Count accepted AND rows on the actual TU in a separate, temporary probe compiler.
python3 - <<'PY'
from pathlib import Path
p = Path("src/buster/compiler/ir_if_convert.c")
s = p.read_text()
needle = "                work->and_types |= 1u << f->type[chosen];"
assert s.count(needle) == 1
p.write_text(s.replace(needle, needle + '\n                if (getenv("BUSTER_3049_COUNT")) fprintf(stderr, "3049 AND width %u\\n", ir_type_bits[f->type[chosen]]);'))
PY
./build.sh --config Release
BUSTER_3049_COUNT=1 build/compiler --tier1 "${args[@]}" -c "$sqlite/sqlite3.c" -o "$tmp/probe.o" 2> "$tmp/counts"
awk '/^3049 AND width/ { n[$4]++; total++ } END { for (i in n) print "accepted ANDs of width", i, n[i]; print "total accepted ANDs", total+0 }' "$tmp/counts"
cmp "$tmp/head.o" "$tmp/probe.o"

sha256sum "$tmp/head.o" "$tmp/probe.o"

)
sql_status=$?
set -e
echo "C23 bit-precise workload status $bit_status"
echo "SQLite workload status $sql_status"
exit "$((bit_status || sql_status))"
