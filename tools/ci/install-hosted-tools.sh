#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Install only portable release inputs under a private, initially absent ROOT.
# No network or system changes: this is also the future bundle publisher's recipe.
set -euo pipefail
[ $# -eq 1 ] || { echo 'usage: install-hosted-tools.sh ROOT' >&2; exit 2; }
: "${BUSTER_HOSTED_RELEASES:?A verified hosted release handoff is required}"
root=$1
here=$(cd -- "$(dirname -- "$0")" && pwd)
bash "$here/hosted-release.sh" verify "$BUSTER_HOSTED_RELEASES"
test ! -e "$root" && test ! -L "$root"
mkdir -p -- "$(dirname -- "$root")"
staging=$(mktemp -d "${root}.install.XXXXXXXX")
trap 'rm -rf -- "$staging"' EXIT
version=$(bash "$here/hosted-release.sh" version llvm)
archive=LLVM-${version}-Linux-X64
llvm="$staging/llvm-$version"
mkdir -p "$llvm"
# Preserve the existing selected release files and the large-window decoder.
tar -I 'zstd -d --long=30' --extract --file "$BUSTER_HOSTED_RELEASES/llvm" \
    --directory "$llvm" --strip-components=1 --no-same-owner --wildcards \
    "$archive/bin/clang" "$archive/bin/clang-23" "$archive/bin/clang++" \
    "$archive/bin/lld" "$archive/bin/ld.lld" "$archive/bin/llvm-config" \
    "$archive/bin/llvm-dwarfdump" "$archive/bin/llvm-readobj" "$archive/bin/llvm-symbolizer" \
    "$archive/bin/llvm-split" "$archive/bin/llvm-nm" "$archive/bin/llvm-objcopy" \
    "$archive/include/*" "$archive/lib/clang/23/*" \
    "$archive/lib/libLLVM*.a" "$archive/lib/libPolly*.a" "$archive/lib/libclang-cpp.so*"
cp -- "$BUSTER_HOSTED_RELEASES/llvm-license" "$llvm/LICENSE.TXT"
test "$("$llvm/bin/clang" -dumpversion)" = "$version"
test "$("$llvm/bin/llvm-config" --version)" = "$version"
for tool in llvm-split llvm-nm llvm-objcopy; do
    test -x "$llvm/bin/$tool"
done
test -f "$("$llvm/bin/clang" -print-runtime-dir)/libclang_rt.fuzzer_no_main.a"
bash "$here/setup-build-tools.sh" "$staging/build-tools" > /dev/null
mold_version=$(bash "$here/hosted-release.sh" version mold)
tar -xzf "$BUSTER_HOSTED_RELEASES/mold" -C "$staging" --no-same-owner
mold="$staging/mold-$mold_version-x86_64-linux"
test -f "$mold/share/doc/mold/LICENSE"
test "$("$mold/bin/mold" --version)" = 'mold 3.0.0 (8de38c35a2df16a25f7ff87ac3ad07156a925beb; compatible with GNU ld)'
mkdir "$staging/wild"
tar -xzf "$BUSTER_HOSTED_RELEASES/wild" -C "$staging/wild" --no-same-owner
wild=$(find "$staging/wild" -type f -name wild -print -quit)
test -x "$wild"
# Relative links survive the atomic rename and later bundle relocation.
mkdir "$staging/bin"
ln -s "../mold-$mold_version-x86_64-linux/bin/mold" "$staging/bin/mold"
ln -s "../mold-$mold_version-x86_64-linux/bin/mold" "$staging/bin/ld.mold"
ln -s "../${wild#"$staging/"}" "$staging/bin/wild"
ln -s "../${wild#"$staging/"}" "$staging/bin/ld.wild"
cp -- "$here/hosted-tools.lock" "$staging/hosted-tools.lock"
mv -- "$staging" "$root"
trap - EXIT
