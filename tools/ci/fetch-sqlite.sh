#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# #146's SQLite source tree into DESTINATION: SQLite 3.53.2's commit, whose amalgamation and shell are the frozen
# corpus's sqlite3.c and shell.c, and whose test/ directory holds the tests test-sqlite runs. A shallow fetch of that one
# commit, whose hash fixes the files, without Git's sample hooks. Where BUSTER_CI_CACHE names a persistent directory
# (the 9700X's), a bare mirror there holds the commit and the checkout fetches it from the mirror through Git's pack
# protocol, which checks every object's hash again; a mirror that lacks the commit or fails is replaced from GitHub.
set -euo pipefail
destination=${1:?usage: fetch-sqlite.sh DESTINATION}
commit=ccc132c5be20ab5c755c97a08d06b1b592fef330
url=https://github.com/sqlite/sqlite
git init -q --template= "$destination"
cache=${BUSTER_CI_CACHE:-}
fetched=false
if [ -n "$cache" ]; then
    mkdir -p "$cache"
    mirror="$cache/sqlite.git"
    exec 8> "$cache/sqlite.lock"
    flock 8
    for attempt in reuse refresh; do
        if [ "$attempt" = refresh ] || ! git -C "$mirror" cat-file -e "refs/pinned/$commit^{commit}" 2> /dev/null; then
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
        printf 'fetch-sqlite.sh: the cached mirror failed; fetching it again\n' >&2
    done
fi
if [ "$fetched" = false ]; then
    git -C "$destination" fetch -q --depth 1 "$url" "$commit"
fi
git -C "$destination" checkout -q FETCH_HEAD
test "$(git -C "$destination" rev-parse HEAD)" = "$commit"
test "$(cat "$destination/VERSION")" = 3.53.2
