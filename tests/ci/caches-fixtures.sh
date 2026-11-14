#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Network-free tests of the 9700X's verified caches (#1207): cached-download.sh, fetch-c-testsuite.sh and
# fetch-sqlite.sh (#146) and fetch-performance.sh (#2629), through a hit, a miss and a corrupted entry each, and tree-manifest.sh, through a
# tree changed in each way it can be.
set -euo pipefail
root=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'tests/ci/caches.sh: %s\n' "$1" >&2; exit 1; }
export BUSTER_CI_CACHE="$work/cache"
# The 9700X's caches, which refresh what fails: a hosted job's offline mode is tests/ci/hosted-offline.sh's.
export BUSTER_CI_OFFLINE=0

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

# fetch-performance.sh (#2629): a pinned commit's directory alone, through the mirror, which keeps each commit it was
# asked for; the same commit again without the upstream; a corrupted mirror fetched again; and refusals.
upstream="$work/performance-upstream"
git init -q -b main "$upstream"
mkdir -p "$upstream/docs/measurements/wanted" "$upstream/docs/measurements/other"
printf 'print(1)\n' > "$upstream/docs/measurements/wanted/one.lua"
printf 'large\n' > "$upstream/docs/measurements/other/raw.log"
git -C "$upstream" add -A
git -C "$upstream" -c user.name=t -c user.email=t@example.com commit -q -m records
git -C "$upstream" config uploadpack.allowAnySHA1InWant true
git -C "$upstream" config uploadpack.allowFilter true
first=$(git -C "$upstream" rev-parse HEAD)
printf 'print(2)\n' > "$upstream/docs/measurements/wanted/two.lua"
git -C "$upstream" add -A
git -C "$upstream" -c user.name=t -c user.email=t@example.com commit -q -m more
second=$(git -C "$upstream" rev-parse HEAD)
export BUSTER_PERFORMANCE_URL="file://$upstream"
bash tools/ci/fetch-performance.sh "$first" docs/measurements/wanted "$work/performance-1"
cmp "$work/performance-1/docs/measurements/wanted/one.lua" "$upstream/docs/measurements/wanted/one.lua" || fail 'the directory is checked out'
test ! -e "$work/performance-1/docs/measurements/other" || fail 'only the directory is checked out'
test ! -e "$work/performance-1/docs/measurements/wanted/two.lua" || fail 'the checkout is the pinned commit'
bash tools/ci/fetch-performance.sh "$second" docs/measurements/wanted "$work/performance-2"
test -f "$work/performance-2/docs/measurements/wanted/two.lua" || fail 'a second pinned commit is checked out'
git -C "$BUSTER_CI_CACHE/buster-performance.git" cat-file -e "refs/pinned/$first^{commit}" || fail 'the mirror keeps the first commit beside the second'
mv "$upstream" "$upstream.moved"
bash tools/ci/fetch-performance.sh "$first" docs/measurements/wanted "$work/performance-3"
cmp "$work/performance-3/docs/measurements/wanted/one.lua" "$upstream.moved/docs/measurements/wanted/one.lua" || fail 'the mirror serves the commit without the upstream'
BUSTER_CI_OFFLINE=1 bash tools/ci/fetch-performance.sh "$second" docs/measurements/wanted "$work/performance-4" || fail 'offline, the mirror serves its commits'
mv "$upstream.moved" "$upstream"
find "$BUSTER_CI_CACHE/buster-performance.git/objects" -type f -name '*.pack' -exec sh -c 'chmod u+w "$1" && printf garbage > "$1"' _ {} \;
find "$BUSTER_CI_CACHE/buster-performance.git/objects" -type f ! -name '*.pack' ! -name '*.idx' ! -path '*/info/*' -exec sh -c 'chmod u+w "$1" && printf garbage > "$1"' _ {} \;
if BUSTER_CI_OFFLINE=1 bash tools/ci/fetch-performance.sh "$first" docs/measurements/wanted "$work/performance-5" 2> /dev/null; then
    fail 'offline, a corrupted mirror was used'
fi
bash tools/ci/fetch-performance.sh "$first" docs/measurements/wanted "$work/performance-6" 2> "$work/log"
grep -q 'the cached mirror failed' "$work/log" || fail 'a corrupted mirror is noticed'
cmp "$work/performance-6/docs/measurements/wanted/one.lua" "$upstream/docs/measurements/wanted/one.lua" || fail 'a corrupted mirror is fetched again'
git -C "$work/performance-6" fsck --no-progress > /dev/null 2>&1 || fail 'the checkout is intact'
for refused in "not-a-commit docs/measurements/wanted" "$first ../escape" "$first /absolute" "$first docs/../escape"; do
    read -r commit directory <<< "$refused"
    if bash tools/ci/fetch-performance.sh "$commit" "$directory" "$work/performance-refused" 2> /dev/null; then
        fail "fetch-performance.sh accepted $refused"
    fi
done
unset BUSTER_PERFORMANCE_URL

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
printf 'caches: downloads, c-testsuite, SQLite and buster-performance mirrors and tree manifests through hits, misses, corruption and changes checked\n'
