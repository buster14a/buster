#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# #145's programs into DESTINATION, at the commit tests/c-testsuite/subset.txt was declared for: a shallow fetch of that
# one commit, whose hash fixes the files, without Git's sample hooks. Where BUSTER_CI_CACHE names a persistent
# directory (the 9700X's), a bare mirror there holds the commit and the checkout fetches it from the mirror through
# Git's pack protocol, which checks every object's hash again; a mirror that lacks the commit or fails is replaced
# from GitHub. BUSTER_C_TESTSUITE_URL and BUSTER_C_TESTSUITE_COMMIT exist for tests/ci/caches.sh alone.
set -euo pipefail
destination=${1:?usage: fetch-c-testsuite.sh DESTINATION}
commit=${BUSTER_C_TESTSUITE_COMMIT:-5c7275656d751de0e68b2d340a95b5681858ed07}
url=${BUSTER_C_TESTSUITE_URL:-https://github.com/c-testsuite/c-testsuite}
[[ "$commit" =~ ^[0-9a-f]{40}$ ]]
git init -q --template= "$destination"
cache=${BUSTER_CI_CACHE:-}
# A hosted job's BUSTER_CI_CACHE is the bundle its preparation job acquired (tools/ci/hosted-offline.sh): with
# BUSTER_CI_OFFLINE=1 its mirror is used as it is, or this fails; it is never replaced, nor GitHub reached.
offline=${BUSTER_CI_OFFLINE:-0}
case "$offline" in
    0) ;;
    1) [ -n "$cache" ] || { printf 'fetch-c-testsuite.sh: BUSTER_CI_OFFLINE needs the prepared bundle, BUSTER_CI_CACHE\n' >&2; exit 1; } ;;
    *) printf 'fetch-c-testsuite.sh: BUSTER_CI_OFFLINE must be 0 or 1, not %s\n' "$offline" >&2; exit 1 ;;
esac
fetched=false
if [ -n "$cache" ]; then
    mkdir -p "$cache"
    mirror="$cache/c-testsuite.git"
    exec 8> "$cache/c-testsuite.lock"
    flock 8
    for attempt in reuse refresh; do
        if [ "$attempt" = refresh ] || ! git -C "$mirror" cat-file -e "refs/pinned/$commit^{commit}" 2> /dev/null; then
            if [ "$offline" = 1 ]; then
                break
            fi
            rm -rf "$mirror"
            git init -q --bare --template= "$mirror"
            git -C "$mirror" fetch -q --depth 1 "$url" "$commit"
            git -C "$mirror" update-ref "refs/pinned/$commit" "$commit"
        fi
        if git -C "$destination" fetch -q --depth 1 "file://$mirror" "refs/pinned/$commit" 2> /dev/null &&
            [ "$(git -C "$destination" rev-parse FETCH_HEAD)" = "$commit" ]; then
            fetched=true
            break
        fi
        if [ "$offline" = 1 ]; then
            printf 'fetch-c-testsuite.sh: the prepared mirror failed\n' >&2
            break
        fi
        printf 'fetch-c-testsuite.sh: the cached mirror failed; fetching it again\n' >&2
    done
fi
if [ "$fetched" = false ] && [ "$offline" = 1 ]; then
    printf 'fetch-c-testsuite.sh: the prepared bundle holds no usable mirror of %s, and BUSTER_CI_OFFLINE forbids fetching it\n' "$commit" >&2
    exit 1
fi
if [ "$fetched" = false ]; then
    git -C "$destination" fetch -q --depth 1 "$url" "$commit"
fi
git -C "$destination" checkout -q FETCH_HEAD
test "$(git -C "$destination" rev-parse HEAD)" = "$commit"
