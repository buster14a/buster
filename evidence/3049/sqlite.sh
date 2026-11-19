#!/usr/bin/env bash
set -euo pipefail
root=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d)
base=${1:?base compiler SHA}
head=${2:?head compiler SHA}
git fetch --no-tags --depth=1 origin "$base" "$head"
git worktree add --detach "$tmp/tree" "$base"
trap 'git -C "$root" worktree remove --force "$tmp/tree"' EXIT
cd "$tmp/tree"
export BUSTER_HOST_ARCH=vector BUSTER_SANITIZER=none BUSTER_DEBUG_INFO=0
tool_path=$(bash tools/ci/session-toolchain.sh stage)
export PATH="$tool_path:$PATH"
bash tools/ci/fetch-corpus.sh "$tmp/corpus"
sqlite="$tmp/corpus/sqlite"
sha256sum "$sqlite/sqlite3.c"
wc -c "$sqlite/sqlite3.c"
mapfile -t defines < <(sed -n '/static const char\* const sqlite_test_shell_options\[/,/^};/s/.*"\(-D[^"]*\)".*/\1/p' src/buster/compiler/sqlite_test.c)
test "${#defines[@]}" -ge 9
args=(-O2 -march=x86-64-v4 -fno-strict-aliasing -I"$sqlite" "${defines[@]}")
clang "${args[@]}" -c "$sqlite/shell.c" -o "$tmp/shell.o"
clang "${args[@]}" -c "$sqlite/sqlite3.c" -o "$tmp/clang.o"
for rev in clang base head; do
    if [[ $rev != clang ]]; then
        git checkout --detach "${!rev}"
        ./build.sh --config Release
        build/compiler --tier1 "${args[@]}" -c "$sqlite/sqlite3.c" -o "$tmp/$rev.o"
    fi
    clang "$tmp/shell.o" "$tmp/$rev.o" -lm -ldl -lpthread -o "$tmp/sqlite-$rev"
    "$tmp/sqlite-$rev" -batch -bail :memory: < tests/sqlite/core.sql > "$tmp/$rev.out"
    sha256sum "$tmp/$rev.out"
    wc -lc "$tmp/$rev.out"
    size --format=sysv "$tmp/$rev.o" | awk '$1==".text" {print "SQLite .text bytes", $2}'
done
cmp "$tmp/clang.out" "$tmp/base.out"
cmp "$tmp/clang.out" "$tmp/head.out"
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
