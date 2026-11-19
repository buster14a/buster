#!/usr/bin/env bash
set -euo pipefail
evidence=$(cd -- "$(dirname -- "$0")" && pwd)
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
clang -std=c23 -O2 -march=x86-64-v4 -c "$evidence/bitint.c" -o "$tmp/clang.o"
objcopy --prefix-symbols=clang_ "$tmp/clang.o" "$tmp/oracle.o"
for rev in base head; do
    git checkout --detach "${!rev}"
    ./build.sh --config Release
    echo "BEGIN $rev bit-precise"
    build/compiler --tier1 --tier1-strict --verify -O2 -std=c23 -march=x86-64-v4 -c "$evidence/bitint.c" -o "$tmp/$rev.o"
    objdump -drw -Mintel "$tmp/$rev.o"
    clang -std=c23 -O2 -march=x86-64-v4 "$evidence/check-bitint.c" "$tmp/$rev.o" "$tmp/oracle.o" -o "$tmp/check"
    "$tmp/check"
    echo "END $rev bit-precise"
done
