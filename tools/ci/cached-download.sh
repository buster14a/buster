#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Download URL to OUTPUT, verified against its SHA-256 DIGEST. Where BUSTER_CI_CACHE names a persistent directory (the
# 9700X's private cache), the verified file is kept there under its digest and reused, checked again on every use: a
# cached copy that no longer matches is deleted and downloaded afresh, and a download that doesn't match fails. A hosted
# job's BUSTER_CI_CACHE is the bundle its preparation job acquired (tools/ci/hosted-offline.sh), with BUSTER_CI_OFFLINE=1:
# the copy is there and matches, or this fails; it never downloads, nor deletes the copy.
set -euo pipefail
url=${1:?usage: cached-download.sh URL SHA256 OUTPUT}
digest=${2:?usage: cached-download.sh URL SHA256 OUTPUT}
output=${3:?usage: cached-download.sh URL SHA256 OUTPUT}
[[ "$digest" =~ ^[0-9a-f]{64}$ ]]
verify() { printf '%s  %s\n' "$digest" "$1" | sha256sum --check --status; }
download()
{
    # Retry remote transport resets too (curl's default policy omits them). A missing local file has no
    # network transport to recover: retrying it made the corruption fixture sleep for about 255 seconds.
    local retry=(--retry 8 --retry-all-errors --retry-max-time 240)
    case "$url" in [Ff][Ii][Ll][Ee]://*) retry=() ;; esac
    curl --fail --location "${retry[@]}" --silent --show-error "$url" --output "$1"
    if ! verify "$1"; then
        printf 'cached-download.sh: %s does not match its SHA-256 %s\n' "$url" "$digest" >&2
        return 1
    fi
}
cache=${BUSTER_CI_CACHE:-}
offline=${BUSTER_CI_OFFLINE:-0}
case "$offline" in
    0|1) ;;
    *) printf 'cached-download.sh: BUSTER_CI_OFFLINE must be 0 or 1, not %s\n' "$offline" >&2; exit 1 ;;
esac
if [ "$offline" = 1 ]; then
    if [ -z "$cache" ]; then
        printf 'cached-download.sh: BUSTER_CI_OFFLINE needs the prepared bundle, BUSTER_CI_CACHE\n' >&2
        exit 1
    fi
    cached="$cache/downloads/$digest"
    if [ ! -f "$cached" ] || ! verify "$cached"; then
        printf 'cached-download.sh: the prepared bundle has no copy of %s matching its SHA-256 %s, and BUSTER_CI_OFFLINE forbids downloading it\n' "$url" "$digest" >&2
        exit 1
    fi
    cp "$cached" "$output"
    verify "$output"
    printf 'cached-download.sh: using the prepared copy of %s\n' "$url"
elif [ -z "$cache" ]; then
    download "$output"
else
    directory="$cache/downloads"
    mkdir -p "$directory"
    # Serialize publication even with a consumer that doesn't hold the host lock.
    exec 8> "$directory/$digest.lock"
    flock 8
    cached="$directory/$digest"
    if [ -f "$cached" ] && verify "$cached"; then
        printf 'cached-download.sh: reusing the verified copy of %s\n' "$url"
    else
        rm -f "$cached"
        temporary=$(mktemp "$directory/.download.XXXXXXXX")
        trap 'rm -f "$temporary"' EXIT
        download "$temporary"
        mv "$temporary" "$cached"
        trap - EXIT
    fi
    cp "$cached" "$output"
    verify "$output"
fi
