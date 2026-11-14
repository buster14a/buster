#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
#   fetch-performance.sh COMMIT DIRECTORY DESTINATION
#
# DIRECTORY of buster-performance at the pinned COMMIT, checked out at DESTINATION/DIRECTORY as actions/checkout's sparse
# checkout placed it (#2629): the measurement records a 9700X step reads (the branch-sizing harness, Tier 1's Lua
# scripts), which no run fetches from GitHub again. Where BUSTER_CI_CACHE names a persistent directory (the 9700X's), a
# bare mirror there holds each pinned commit, shallow, under refs/pinned/COMMIT; the checkout fetches the commit's
# trees from the mirror and then DIRECTORY's files alone (a partial clone, sparse), through Git's pack protocol, which
# checks every object's hash again. A mirror that lacks the commit or fails is fetched again from GitHub
# (buster-performance is public: no token). Without BUSTER_CI_CACHE, the checkout fetches from GitHub the same way. With
# BUSTER_CI_OFFLINE=1 the mirror is used as it is, or this fails. BUSTER_PERFORMANCE_URL exists for
# tests/ci/caches-fixtures.sh alone.
set -euo pipefail
usage='usage: fetch-performance.sh COMMIT DIRECTORY DESTINATION'
commit=${1:?$usage}
directory=${2:?$usage}
destination=${3:?$usage}
url=${BUSTER_PERFORMANCE_URL:-https://github.com/buster14a/buster-performance}
[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || { printf 'fetch-performance.sh: %s is not a commit hash\n' "$commit" >&2; exit 1; }
[[ "$directory" =~ ^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$ && "/$directory/" != */../* && "/$directory/" != */./* ]] ||
    { printf 'fetch-performance.sh: %s is not a relative directory\n' "$directory" >&2; exit 1; }
cache=${BUSTER_CI_CACHE:-}
offline=${BUSTER_CI_OFFLINE:-0}
case "$offline" in
    0) ;;
    1) [ -n "$cache" ] || { printf 'fetch-performance.sh: BUSTER_CI_OFFLINE needs the prepared bundle, BUSTER_CI_CACHE\n' >&2; exit 1; } ;;
    *) printf 'fetch-performance.sh: BUSTER_CI_OFFLINE must be 0 or 1, not %s\n' "$offline" >&2; exit 1 ;;
esac
# The checkout from `source`: the commit and its trees, then DIRECTORY's blobs as the sparse checkout needs them. A
# failure leaves no checkout behind.
checkout()
{
    local source=$1
    rm -rf -- "$destination"
    git init -q --template= "$destination"
    git -C "$destination" sparse-checkout set "$directory"
    git -C "$destination" remote add origin "$source"
    git -C "$destination" fetch -q --depth 1 --filter=blob:none origin "$2" 2> /dev/null &&
        [ "$(git -C "$destination" rev-parse FETCH_HEAD)" = "$commit" ] &&
        git -C "$destination" checkout -q FETCH_HEAD 2> /dev/null &&
        [ -d "$destination/$directory" ]
}
fetched=false
if [ -n "$cache" ]; then
    mkdir -p "$cache"
    mirror="$cache/buster-performance.git"
    exec 8> "$cache/buster-performance.lock"
    flock 8
    for attempt in reuse refresh; do
        if [ "$attempt" = refresh ] || ! git -C "$mirror" cat-file -e "refs/pinned/$commit^{commit}" 2> /dev/null; then
            if [ "$offline" = 1 ]; then
                break
            fi
            # A refresh starts the mirror afresh; a reuse that lacks the commit adds it to the mirror's others, and
            # fails over to the refresh where the mirror can't take it.
            if [ "$attempt" = refresh ] || [ ! -d "$mirror" ]; then
                rm -rf -- "$mirror"
                git init -q --bare --template= "$mirror"
                git -C "$mirror" config uploadpack.allowFilter true
                git -C "$mirror" config uploadpack.allowAnySHA1InWant true
            fi
            if ! { git -C "$mirror" fetch -q --depth 1 "$url" "$commit" && git -C "$mirror" update-ref "refs/pinned/$commit" "$commit"; } 2> /dev/null; then
                if [ "$attempt" = refresh ]; then
                    printf 'fetch-performance.sh: %s %s: no mirror\n' "$url" "$commit" >&2
                    exit 1
                fi
                printf 'fetch-performance.sh: the cached mirror failed; fetching it again\n' >&2
                continue
            fi
        fi
        if checkout "file://$mirror" "refs/pinned/$commit"; then
            fetched=true
            break
        fi
        if [ "$offline" = 1 ]; then
            printf 'fetch-performance.sh: the prepared mirror failed\n' >&2
            break
        fi
        printf 'fetch-performance.sh: the cached mirror failed; fetching it again\n' >&2
    done
fi
if [ "$fetched" = false ] && [ "$offline" = 1 ]; then
    printf 'fetch-performance.sh: the prepared bundle holds no usable mirror of %s, and BUSTER_CI_OFFLINE forbids fetching it\n' "$commit" >&2
    exit 1
fi
if [ "$fetched" = false ]; then
    checkout "$url" "$commit" || { printf 'fetch-performance.sh: %s %s: no checkout\n' "$url" "$commit" >&2; exit 1; }
fi
test "$(git -C "$destination" rev-parse HEAD)" = "$commit"
