#!/usr/bin/env bash
set -euo pipefail
evidence=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d)
base=${1:?base compiler SHA}
head=${2:?head compiler SHA}
minmax=6e3d58070df89b2bb5872b2cedf808c5b443eb04
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
