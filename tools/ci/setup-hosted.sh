#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Per-VM setup. Release acquisition belongs to prepare-hosted-tools; a missing
# handoff must not make each shard download independently.
set -euo pipefail
: "${RUNNER_TEMP:?A private runner temporary directory is required}"
: "${BUSTER_HOSTED_RELEASES:?Run the hosted-tools handoff action before setup}"
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
. /etc/os-release
test "$VERSION_ID" = 26.04
here=$(cd -- "$(dirname -- "$0")" && pwd)
bash "$here/hosted-release.sh" verify "$BUSTER_HOSTED_RELEASES"
# A runner's resolver can fail on Ubuntu's mirror names for a long while: from 13:51 UTC on 2026-11-11, for well
# over half an hour, every hosted job failed in apt-get with "Temporary failure resolving 'archive.ubuntu.com'"
# (and 'security.ubuntu.com', 'azure.archive.ubuntu.com') while packages.microsoft.com resolved and downloaded on
# the same runner, and some public resolvers (dns.google) gave no address for those names either. So, before
# using apt, pin each apt host that doesn't resolve here in /etc/hosts, to an address a DNS-over-HTTPS resolver
# gives it (Cloudflare's, else Google's). This can't bring in other packages: apt checks the archive's signed
# Release files and every package's hash, and setup-baseline-runtime.sh checks its downloads' SHA-256.
apt_hosts() {
    local file
    for file in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/*; do
        if [ -f "$file" ]; then
            { grep -vE '^[[:space:]]*#' "$file" || true; } | { grep -oE 'https?://[^/[:space:]]+' || true; }
        fi
    done | sed -E 's#^https?://##'
    printf '%s\n' archive.ubuntu.com security.ubuntu.com azure.archive.ubuntu.com
}
resolve_over_https() {
    local url
    local addresses=
    for url in "https://cloudflare-dns.com/dns-query?name=$1&type=A" "https://dns.google/resolve?name=$1&type=A"; do
        if [ -z "$addresses" ]; then
            addresses=$(curl --fail --silent --show-error --max-time 10 --retry 2 -H 'accept: application/dns-json' "$url" |
                grep -oE '"data": *"[0-9]+(\.[0-9]+){3}"' | grep -oE '[0-9]+(\.[0-9]+){3}' | head -n 1) || addresses=
        fi
    done
    printf '%s' "$addresses"
}
pin_unresolved_apt_hosts() {
    local host
    local address
    for host in $(apt_hosts | sort -u); do
        if ! timeout 10 getent hosts "$host" > /dev/null; then
            address=$(resolve_over_https "$host")
            if [ -n "$address" ]; then
                printf '%s %s\n' "$address" "$host" | sudo tee -a /etc/hosts > /dev/null
                printf 'setup-hosted: %s does not resolve here; pinned it to %s through DNS over HTTPS\n' "$host" "$address" >&2
            else
                printf 'setup-hosted: %s does not resolve here, nor over HTTPS\n' "$host" >&2
            fi
        fi
    done
}
# Retry each apt-get, pinning hosts again and refreshing the lists between attempts. Each attempt is bounded, so a
# resolver that times out on every request can't hold the job for an hour: a failing attempt took about 10 minutes.
apt_get() {
    local attempt=1
    local status=0
    pin_unresolved_apt_hosts
    sudo timeout 480 apt-get -o Acquire::Retries=3 "$@" || status=$?
    while [ "$status" -ne 0 ] && [ "$attempt" -lt 3 ]; do
        printf 'setup-hosted: apt-get %s failed with status %s (attempt %s of 3); retrying in %s s\n' \
            "$1" "$status" "$attempt" "$((attempt * 20))" >&2
        sleep "$((attempt * 20))"
        attempt=$((attempt + 1))
        status=0
        pin_unresolved_apt_hosts
        if [ "$1" != update ]; then
            sudo timeout 480 apt-get -o Acquire::Retries=3 update || true
        fi
        sudo timeout 480 apt-get -o Acquire::Retries=3 "$@" || status=$?
    done
    return "$status"
}
apt_get update
apt_get install --yes --no-install-recommends gcc g++ binutils libc6-dev \
    gdb tcc zstd linux-perf qemu-user valgrind libzstd-dev libxml2-dev zlib1g-dev tcl8.6-dev
root="$RUNNER_TEMP/buster-hosted-tools"
start=$SECONDS
bash "$here/install-hosted-tools.sh" "$root"
printf 'hosted-tools: extraction-and-probes seconds=%s\n' "$((SECONDS-start))"
version=$(bash "$here/hosted-release.sh" version llvm)
llvm="$root/llvm-$version"
build_tools=$(bash "$here/setup-build-tools.sh" "$root/build-tools")
test -f /usr/lib/x86_64-linux-gnu/libzstd.a
printf '%s\n' "$root/bin" "$llvm/bin" "$build_tools" >> "$GITHUB_PATH"
printf 'BUSTER_CLANG_RESOURCE_DIR=%s\n' "$llvm/lib/clang/23" >> "$GITHUB_ENV"
printf 'LD_LIBRARY_PATH=%s%s\n' "$llvm/lib" "${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" >> "$GITHUB_ENV"
perf --version
sudo sysctl -w kernel.perf_event_paranoid=-1 kernel.kptr_restrict=0 kernel.yama.ptrace_scope=0
