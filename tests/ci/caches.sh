#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Network-free tests of the 9700X's verified caches (#1207): cached-download.sh, fetch-c-testsuite.sh and
# pragma-oracle-cache.sh, through a hit, a miss, a corrupted entry and changed inputs each.
set -euo pipefail
root=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'tests/ci/caches.sh: %s\n' "$1" >&2; exit 1; }
export BUSTER_CI_CACHE="$work/cache"

# cached-download.sh: a miss downloads and keeps the file, a hit reuses it, a corrupted copy is replaced, and a source
# that doesn't match fails without leaving anything cached.
printf 'frozen input\n' > "$work/source"
digest=$(sha256sum "$work/source" | cut -c 1-64)
bash tools/ci/cached-download.sh "file://$work/source" "$digest" "$work/first" > "$work/log"
cmp "$work/source" "$work/first"
test -f "$BUSTER_CI_CACHE/downloads/$digest" || fail 'a download is kept'
mv "$work/source" "$work/source.moved"
bash tools/ci/cached-download.sh "file://$work/source" "$digest" "$work/second" > "$work/log"
grep -q 'reusing the verified copy' "$work/log" || fail 'a verified copy is reused without the source'
cmp "$work/source.moved" "$work/second"
printf 'tampered\n' > "$BUSTER_CI_CACHE/downloads/$digest"
if bash tools/ci/cached-download.sh "file://$work/source" "$digest" "$work/third" > "$work/log" 2>&1; then
    fail 'a corrupted copy was used'
fi
test ! -f "$BUSTER_CI_CACHE/downloads/$digest" || fail 'a corrupted copy is removed'
mv "$work/source.moved" "$work/source"
bash tools/ci/cached-download.sh "file://$work/source" "$digest" "$work/fourth" > "$work/log"
cmp "$work/source" "$work/fourth"
printf 'changed\n' > "$work/source"
if bash tools/ci/cached-download.sh "file://$work/source" "$(printf 0%.0s {1..64})" "$work/fifth" 2> /dev/null; then
    fail 'a download that does not match was accepted'
fi
test ! -e "$BUSTER_CI_CACHE/downloads/$(printf 0%.0s {1..64})" || fail 'nothing unverified is cached'
BUSTER_CI_CACHE='' bash tools/ci/cached-download.sh "file://$work/source" "$(sha256sum "$work/source" | cut -c 1-64)" "$work/hosted"
cmp "$work/source" "$work/hosted"

# fetch-c-testsuite.sh: the mirror serves the pinned commit, a corrupted mirror is replaced, and the checkout is that
# commit's tree either way.
upstream="$work/upstream"
git init -q -b main "$upstream"
printf 'int main(void) { return 0; }\n' > "$upstream/00001.c"
git -C "$upstream" add -A
git -C "$upstream" -c user.name=t -c user.email=t@example.com commit -q -m tests
git -C "$upstream" config uploadpack.allowAnySHA1InWant true
pinned=$(git -C "$upstream" rev-parse HEAD)
export BUSTER_C_TESTSUITE_URL="file://$upstream" BUSTER_C_TESTSUITE_COMMIT="$pinned"
bash tools/ci/fetch-c-testsuite.sh "$work/suite-1"
test "$(git -C "$work/suite-1" rev-parse HEAD)" = "$pinned" || fail 'the checkout is the pinned commit'
git -C "$BUSTER_CI_CACHE/c-testsuite.git" cat-file -e "refs/pinned/$pinned^{commit}" || fail 'the mirror keeps the commit'
mv "$upstream" "$upstream.moved"
bash tools/ci/fetch-c-testsuite.sh "$work/suite-2"
cmp "$work/suite-2/00001.c" "$upstream.moved/00001.c" || fail 'the mirror serves the commit without the upstream'
mv "$upstream.moved" "$upstream"
find "$BUSTER_CI_CACHE/c-testsuite.git/objects" -type f -name '*.pack' -exec sh -c 'chmod u+w "$1" && printf garbage > "$1"' _ {} \;
find "$BUSTER_CI_CACHE/c-testsuite.git/objects" -type f ! -name '*.pack' ! -name '*.idx' ! -path '*/info/*' -exec sh -c 'chmod u+w "$1" && printf garbage > "$1"' _ {} \;
bash tools/ci/fetch-c-testsuite.sh "$work/suite-3" 2> "$work/log"
test "$(git -C "$work/suite-3" rev-parse HEAD)" = "$pinned" || fail 'a corrupted mirror is fetched again'
git -C "$work/suite-3" fsck --no-progress > /dev/null 2>&1 || fail 'the checkout is intact'
unset BUSTER_C_TESTSUITE_URL BUSTER_C_TESTSUITE_COMMIT

# tree-manifest.sh: a tree matches its manifest until a file's contents, a file, or a link changes.
tree="$work/tree"
mkdir -p "$tree/bin" "$tree/lib"
printf 'compiler\n' > "$tree/bin/clang-23"
ln -s clang-23 "$tree/bin/clang"
printf 'library\n' > "$tree/lib/libclang-cpp.so"
bash tools/ci/tree-manifest.sh write "$tree"
bash tools/ci/tree-manifest.sh check "$tree" || fail 'a tree matches its own manifest'
printf 'marker\n' > "$tree/.correctness-complete"
bash tools/ci/tree-manifest.sh check "$tree" || fail 'the markers written after the manifest are not entries'
printf 'COMPILER\n' > "$tree/bin/clang-23"
if bash tools/ci/tree-manifest.sh check "$tree" 2> /dev/null; then fail 'a file changed in place was accepted'; fi
printf 'compiler\n' > "$tree/bin/clang-23"
printf 'extra\n' > "$tree/lib/extra.so"
if bash tools/ci/tree-manifest.sh check "$tree" 2> /dev/null; then fail 'an added file was accepted'; fi
rm "$tree/lib/extra.so"
ln -sf libclang-cpp.so "$tree/bin/clang"
if bash tools/ci/tree-manifest.sh check "$tree" 2> /dev/null; then fail 'a link retargeted was accepted'; fi
ln -sf clang-23 "$tree/bin/clang"
rm "$tree/lib/libclang-cpp.so"
if bash tools/ci/tree-manifest.sh check "$tree" 2> /dev/null; then fail 'a removed file was accepted'; fi
printf 'library\n' > "$tree/lib/libclang-cpp.so"
bash tools/ci/tree-manifest.sh check "$tree" || fail 'the restored tree matches again'

# pragma-oracle-cache.sh with a stand-in toolchain: a miss builds, a hit doesn't, a corrupted executable is rebuilt,
# changed flags are another key, a compiler changed in place (its version the same) is refused rather than its old
# oracle reused, a toolchain staged afresh is another key, and one without a manifest builds without the cache.
toolchain="$work/toolchain"
mkdir -p "$toolchain/bin"
cat > "$toolchain/bin/fake-c++" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
if [ "${1:-}" = --version ]; then
    echo 'fake clang version 23.1.4'
    exit 0
fi
output=''
while [ "$#" -gt 0 ]; do
    if [ "$1" = -o ]; then output=$2; shift; fi
    shift
done
printf 'build\n' >> "$FAKE_BUILDS"
printf '#!/bin/sh\necho "oracle %s"\n' "${FAKE_FLAVOR:-one}" > "$output"
chmod +x "$output"
SH
cat > "$toolchain/bin/fake-llvm-config" <<'SH'
#!/usr/bin/env bash
if [ "$1" = --prefix ]; then
    echo "$FAKE_PREFIX"
    exit 0
fi
echo "-I/fake/include ${FAKE_FLAGS:-}"
echo '-L/fake/lib'
SH
chmod +x "$toolchain/bin/fake-c++" "$toolchain/bin/fake-llvm-config"
bash tools/ci/tree-manifest.sh write "$toolchain"
export CXX="$toolchain/bin/fake-c++" LLVM_CONFIG="$toolchain/bin/fake-llvm-config" FAKE_BUILDS="$work/builds" FAKE_PREFIX="$toolchain"
: > "$FAKE_BUILDS"
entries() { find "$BUSTER_CI_CACHE/pragma-oracle" -mindepth 1 -maxdepth 1 -type d ! -name '.*' | wc -l; }
bash tools/ci/pragma-oracle-cache.sh "$work/oracle-1" > /dev/null
bash tools/ci/pragma-oracle-cache.sh "$work/oracle-2" > "$work/log"
grep -q 'reusing' "$work/log" || fail 'a matching oracle is reused'
test "$(wc -l < "$FAKE_BUILDS")" -eq 1 || fail 'one build for two uses'
entry=$(find "$BUSTER_CI_CACHE/pragma-oracle" -mindepth 1 -maxdepth 1 -type d ! -name '.*')
printf '#!/bin/sh\necho tampered\n' > "$entry/oracle"
FAKE_FLAVOR=two bash tools/ci/pragma-oracle-cache.sh "$work/oracle-3" > "$work/log"
grep -q 'oracle two' "$work/log" || fail 'a corrupted oracle is rebuilt'
test "$(wc -l < "$FAKE_BUILDS")" -eq 2 || fail 'the rebuild is a build'
FAKE_FLAGS=-DOTHER bash tools/ci/pragma-oracle-cache.sh "$work/oracle-4" > /dev/null
test "$(wc -l < "$FAKE_BUILDS")" -eq 3 || fail 'other flags are another key'
test "$(entries)" -eq 2 || fail 'two keys, two entries'
# The compiler replaced at the same path, reporting the same version but building another oracle.
sed -i 's/FAKE_FLAVOR:-one/FAKE_FLAVOR:-three/' "$toolchain/bin/fake-c++"
if bash tools/ci/pragma-oracle-cache.sh "$work/oracle-5" > "$work/log" 2>&1; then
    fail 'a toolchain changed in place reused an oracle another compiler built'
fi
grep -q 'differ from its manifest' "$work/log" || fail 'and says why'
# Staged afresh (a new manifest), it is another key, and its own build.
bash tools/ci/tree-manifest.sh write "$toolchain"
bash tools/ci/pragma-oracle-cache.sh "$work/oracle-6" > "$work/log"
grep -q 'oracle three' "$work/log" || fail 'a toolchain staged afresh builds its own oracle'
test "$(entries)" -eq 3 || fail 'the new toolchain is a third key'
rm "$toolchain/.manifest"
bash tools/ci/pragma-oracle-cache.sh "$work/oracle-7" > "$work/log"
grep -q 'without the cache' "$work/log" || fail 'a toolchain without a manifest builds without the cache'
test "$(entries)" -eq 3 || fail 'and keeps nothing'
cd "$root"
printf 'caches: downloads, c-testsuite mirror, tree manifests and pragma oracle hit, miss, corruption and changed inputs checked\n'
