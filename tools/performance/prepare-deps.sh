#!/usr/bin/env bash
# Hash-pinned external dependencies. Downloads and sources stay in ignored build
# storage; no upstream implementation is copied into the repository.
set -euo pipefail
cd "$(dirname "$0")/../.."
dependency_root=${BUSTER_PERFORMANCE_DEPS:-"$PWD/build/performance/deps"}
mkdir -p "$dependency_root/downloads" "$dependency_root/include/nanoarrow"

fetch()
{
    local name=$1 url=$2 digest=$3
    local archive="$dependency_root/downloads/$name"
    if [[ ! -f $archive ]]; then
        curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 120 "$url" -o "$archive.part"
        printf '%s  %s\n' "$digest" "$archive.part" | sha256sum --check --status
        mv "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$digest" "$archive" | sha256sum --check --status
}

extract()
{
    local archive=$1 destination=$2 pin=$3
    if [[ ! -f $destination/.buster-pin ]] || [[ $(cat "$destination/.buster-pin") != "$pin" ]]; then
        local staging
        staging=$(mktemp -d "$dependency_root/unpack.XXXXXXXX")
        tar -xzf "$archive" --strip-components=1 -C "$staging"
        printf '%s\n' "$pin" > "$staging/.buster-pin"
        if [[ -e $destination ]]; then
            rm -r -- "$destination"
        fi
        mv "$staging" "$destination"
    fi
}

fetch nanoarrow.tar.gz https://codeload.github.com/apache/arrow-nanoarrow/tar.gz/2cfba631b40886f1418a463f3b7c4552c8ae0dc7 2e27a776772fba80018ea4adaefef0a3e3fb90697685f1c5c4f5c42fcce34567
fetch yyjson.tar.gz https://codeload.github.com/ibireme/yyjson/tar.gz/8b4a38dc994a110abaec8a400615567bd996105f 94e9c90f8f12d8329f2c0756782d21e5c278729023cd2c5fe47e323a35947ed6
fetch arrow-js.tgz https://registry.npmjs.org/apache-arrow/-/apache-arrow-21.1.0.tgz fa3fd5781698838eafc0f74f5ae35ab0bf13bb8cb227b3f57d40bf3b34055d72
extract "$dependency_root/downloads/nanoarrow.tar.gz" "$dependency_root/nanoarrow" 2cfba631b40886f1418a463f3b7c4552c8ae0dc7
extract "$dependency_root/downloads/yyjson.tar.gz" "$dependency_root/yyjson" 8b4a38dc994a110abaec8a400615567bd996105f
extract "$dependency_root/downloads/arrow-js.tgz" "$dependency_root/arrow-js" 21.1.0-fa3fd5781698838eafc0f74f5ae35ab0bf13bb8cb227b3f57d40bf3b34055d72

# Configure the upstream header directly; no generated amalgamation, Python or
# new build framework is needed. All implementation files stay upstream files.
sed -e 's/@NANOARROW_VERSION_MAJOR@/0/' \
    -e 's/@NANOARROW_VERSION_MINOR@/7/' \
    -e 's/@NANOARROW_VERSION_PATCH@/0/' \
    -e 's/@NANOARROW_VERSION@/0.7.0/' \
    -e 's/@NANOARROW_NAMESPACE_DEFINE@//' \
    "$dependency_root/nanoarrow/src/nanoarrow/nanoarrow_config.h.in" > "$dependency_root/include/nanoarrow/nanoarrow_config.h"
printf '%s\n' "$dependency_root"
