#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Prepare Ubuntu's generic amd64 runtime and startup objects privately for
# emulating a baseline x86-64 compiler. Do not install these packages.
set -euo pipefail

: "${RUNNER_TEMP:?RUNNER_TEMP must name a private temporary directory}"
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64

runtime="$RUNNER_TEMP/buster-baseline-runtime"
mkdir -p "$runtime"
staging=$(mktemp -d "$runtime/staging.XXXXXXXX")
trap 'rm -rf "$staging"' EXIT

# Exact package hashes from Ubuntu's resolute-updates (glibc) and resolute
# (GCC 15) amd64 Packages indexes. Download only generic amd64 packages.
fetch_package()
{
    local path="$1" digest="$2" filename="${1##*/}"
    curl --fail --location --retry 3 "https://archive.ubuntu.com/ubuntu/$path" --output "$staging/$filename"
    printf '%s  %s\n' "$digest" "$staging/$filename" | sha256sum --check --status
    ar p "$staging/$filename" data.tar.zst | tar --zstd -xf - -C "$staging" --no-same-owner
}
fetch_package pool/main/g/glibc/libc6_2.43-2ubuntu2.4_amd64.deb \
    a613b457f3ff9c84ebd28af8a05afdec22605ad9aed87c71e3a6ae5170aeb9ca
fetch_package pool/main/g/glibc/libc6-dev_2.43-2ubuntu2.4_amd64.deb \
    17bb816a65a25c3416854eef7e9cc6d9a3761f45bad0d44e49bc676b7bab1769
fetch_package pool/main/g/gcc-15/libgcc-15-dev_15.2.0-16ubuntu1_amd64.deb \
    32fa5bde1891f46fd4f422e466c863fb78e834b634e1aa08211c8eea68870af5
fetch_package pool/main/g/gcc-15/gcc-15-base_15.2.0-16ubuntu1_amd64.deb \
    5a302d705d562fb7e150415b00651c00fed58b633faadff0b332eac1b73fadb3

# The packages' installed copyright and license files stay in the tree.
root="$runtime/root"
mkdir -p "$root"
cp -a "$staging/usr" "$root/"
lib="$root/usr/lib/x86_64-linux-gnu"
test -f "$root/usr/lib/gcc/x86_64-linux-gnu/15/libgcc.a"
test -f "$lib/crt1.o"
test -f "$lib/crti.o"
test -f "$lib/crtn.o"
test -f "$lib/libc.so.6"
test -f "$lib/libm.so.6"
test -x "$lib/ld-linux-x86-64.so.2"
printf 'BUSTER_HOST_RUNTIME=%s\n' "$root"
if [ -n "${GITHUB_ENV:-}" ]; then
    printf 'BUSTER_HOST_RUNTIME=%s\n' "$root" >> "$GITHUB_ENV"
fi
