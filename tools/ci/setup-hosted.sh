#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Per-VM setup. Acquisition belongs to prepare-hosted-tools, the release inputs'
# and the apt packages' alike; a missing handoff must not make each shard
# download independently.
set -euo pipefail
: "${RUNNER_TEMP:?A private runner temporary directory is required}"
: "${BUSTER_HOSTED_RELEASES:?Run the hosted-tools handoff action before setup}"
: "${BUSTER_HOSTED_OFFLINE:?Run the hosted-tools handoff action before setup: it restores the offline inputs}"
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
. /etc/os-release
test "$VERSION_ID" = 26.04
here=$(cd -- "$(dirname -- "$0")" && pwd)
bash "$here/hosted-release.sh" verify "$BUSTER_HOSTED_RELEASES"
# The packages (hosted-offline.sh's list) come from the closure the preparation
# job downloaded, with no apt-get update and no download (#2275): a resolver
# outage on Ubuntu's mirror names failed every hosted job for over half an hour
# on 2026-11-11. The preparation's own apt-get keeps the retries and the
# DNS-over-HTTPS pins this script had for it.
bash "$here/hosted-offline.sh" apt-install "$BUSTER_HOSTED_OFFLINE"
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
