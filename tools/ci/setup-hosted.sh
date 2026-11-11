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
# A runner's resolver or Ubuntu's mirrors can fail for a few minutes ("Temporary failure resolving
# 'security.ubuntu.com'"): that failed every hosted job of main's runs from 13:51 to 14:01 UTC on
# 2026-11-11, with apt-get exiting 100 before any test ran. Retry each apt-get, refreshing the lists
# between attempts, over about the 240 s the curl downloads here already allow; the packages are
# the distribution's own signed ones, so a retried fetch is the same fetch.
apt_get() {
    local attempt=1
    local status=0
    sudo apt-get -o Acquire::Retries=3 "$@" || status=$?
    while [ "$status" -ne 0 ] && [ "$attempt" -lt 5 ]; do
        printf 'setup-hosted: apt-get %s failed with status %s (attempt %s of 5); retrying in %s s\n' \
            "$1" "$status" "$attempt" "$((attempt * 15))" >&2
        sleep "$((attempt * 15))"
        attempt=$((attempt + 1))
        status=0
        if [ "$1" != update ]; then
            sudo apt-get -o Acquire::Retries=3 update || true
        fi
        sudo apt-get -o Acquire::Retries=3 "$@" || status=$?
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
