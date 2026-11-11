#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Network-free tests of the 9700X's verified caches (#1207): cached-download.sh, fetch-c-testsuite.sh and
# fetch-sqlite.sh (#146), through a hit, a miss and a corrupted entry each, and tree-manifest.sh, through a tree changed in each way it can be.
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

# fetch-sqlite.sh (#146): the same, for SQLite's tree, whose VERSION must be 3.53.2.
upstream="$work/sqlite-upstream"
git init -q -b main "$upstream"
mkdir "$upstream/test"
printf '3.53.2\n' > "$upstream/VERSION"
printf 'SELECT 1;\n' > "$upstream/test/one.sql"
git -C "$upstream" add -A
git -C "$upstream" -c user.name=t -c user.email=t@example.com commit -q -m tree
git -C "$upstream" config uploadpack.allowAnySHA1InWant true
pinned=$(git -C "$upstream" rev-parse HEAD)
export BUSTER_SQLITE_URL="file://$upstream" BUSTER_SQLITE_COMMIT="$pinned"
bash tools/ci/fetch-sqlite.sh "$work/sqlite-1"
test "$(git -C "$work/sqlite-1" rev-parse HEAD)" = "$pinned" || fail 'the SQLite checkout is the pinned commit'
git -C "$BUSTER_CI_CACHE/sqlite.git" cat-file -e "refs/pinned/$pinned^{commit}" || fail 'the SQLite mirror keeps the commit'
mv "$upstream" "$upstream.moved"
bash tools/ci/fetch-sqlite.sh "$work/sqlite-2"
cmp "$work/sqlite-2/test/one.sql" "$upstream.moved/test/one.sql" || fail 'the SQLite mirror serves the commit without the upstream'
mv "$upstream.moved" "$upstream"
find "$BUSTER_CI_CACHE/sqlite.git/objects" -type f -name '*.pack' -exec sh -c 'chmod u+w "$1" && printf garbage > "$1"' _ {} \;
find "$BUSTER_CI_CACHE/sqlite.git/objects" -type f ! -name '*.pack' ! -name '*.idx' ! -path '*/info/*' -exec sh -c 'chmod u+w "$1" && printf garbage > "$1"' _ {} \;
bash tools/ci/fetch-sqlite.sh "$work/sqlite-3" 2> "$work/log"
test "$(git -C "$work/sqlite-3" rev-parse HEAD)" = "$pinned" || fail 'a corrupted SQLite mirror is fetched again'
git -C "$work/sqlite-3" fsck --no-progress > /dev/null 2>&1 || fail 'the SQLite checkout is intact'
printf '3.53.1\n' > "$upstream/VERSION"
git -C "$upstream" -c user.name=t -c user.email=t@example.com commit -q -am other
export BUSTER_SQLITE_COMMIT="$(git -C "$upstream" rev-parse HEAD)"
if bash tools/ci/fetch-sqlite.sh "$work/sqlite-4" 2> /dev/null; then
    fail 'a tree of another SQLite version was accepted'
fi
unset BUSTER_SQLITE_URL BUSTER_SQLITE_COMMIT

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

cd "$root"
printf 'caches: downloads, c-testsuite and SQLite mirrors and tree manifests through hits, misses, corruption and changes checked\n'
