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

# pragma-oracle-cache.sh with a stand-in compiler: a miss builds, a hit doesn't, a corrupted executable is rebuilt, and
# a changed source or flags is another key.
mkdir -p "$work/bin"
cat > "$work/bin/fake-c++" <<'SH'
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
printf '#!/bin/sh\necho "oracle %s"\n' "$FAKE_FLAVOR" > "$output"
chmod +x "$output"
SH
cat > "$work/bin/fake-llvm-config" <<'SH'
#!/usr/bin/env bash
echo "-I/fake/include ${FAKE_FLAGS:-}"
echo '-L/fake/lib'
SH
chmod +x "$work/bin/fake-c++" "$work/bin/fake-llvm-config"
export CXX="$work/bin/fake-c++" LLVM_CONFIG="$work/bin/fake-llvm-config" FAKE_BUILDS="$work/builds" FAKE_FLAVOR=one
: > "$FAKE_BUILDS"
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
test "$(find "$BUSTER_CI_CACHE/pragma-oracle" -mindepth 1 -maxdepth 1 -type d ! -name '.*' | wc -l)" -eq 2 || fail 'two keys, two entries'
cd "$root"
printf 'caches: downloads, c-testsuite mirror and pragma oracle hit, miss, corruption and changed inputs checked\n'
