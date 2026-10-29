#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Install test-only dependencies on a disposable GitHub-hosted Ubuntu 26.04 VM.
set -euo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
. /etc/os-release
test "$VERSION_ID" = 26.04
sudo apt-get update
sudo apt-get install --yes --no-install-recommends gcc g++ binutils libc6-dev \
    gdb tcc zstd linux-perf qemu-user libzstd-dev libxml2-dev zlib1g-dev
# Use the matching official stable release for the compiler, Clang API oracle,
# linker, and LLVM object/DWARF consumers. llvm-config's static library list
# needs the LLVM archives and the Ubuntu development libraries above.
version=23.1.4
archive=LLVM-${version}-Linux-X64
llvm="$RUNNER_TEMP/llvm-$version"
curl --fail --location --retry 3 \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-${version}/${archive}.tar.zst" \
    --output "$RUNNER_TEMP/llvm.tar.zst"
printf '%s  %s\n' efdd1dbd089b99bfcfcde7b8eb1236163f2e88df5a981cca4f783ec9cb5e0378 \
    "$RUNNER_TEMP/llvm.tar.zst" | sha256sum --check
mkdir -p "$llvm"
# Avoid unpacking the 9 GiB of unrelated release binaries. The archive uses a
# 1 GiB zstd window, larger than the default decoder limit.
tar -I 'zstd -d --long=30' --extract --file "$RUNNER_TEMP/llvm.tar.zst" \
    --directory "$llvm" --strip-components=1 --no-same-owner --wildcards \
    "$archive/bin/clang" "$archive/bin/clang-23" "$archive/bin/clang++" \
    "$archive/bin/lld" "$archive/bin/ld.lld" "$archive/bin/llvm-config" \
    "$archive/bin/llvm-dwarfdump" "$archive/bin/llvm-readobj" "$archive/bin/llvm-symbolizer" \
    "$archive/include/*" "$archive/lib/clang/23/*" \
    "$archive/lib/libLLVM*.a" "$archive/lib/libPolly*.a" "$archive/lib/libclang-cpp.so*"
rm "$RUNNER_TEMP/llvm.tar.zst"
# The binary archive omits the project license; retain the one from its exact
# source revision, verified independently.
curl --fail --location --retry 3 \
    https://raw.githubusercontent.com/llvm/llvm-project/7a941dfb2877a0b8cf30323fefa85ad019e0b0f5/llvm/LICENSE.TXT \
    --output "$llvm/LICENSE.TXT"
printf '%s  %s\n' 8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee \
    "$llvm/LICENSE.TXT" | sha256sum --check
test "$("$llvm/bin/clang" -dumpversion)" = "$version"
test "$("$llvm/bin/llvm-config" --version)" = "$version"
test -f /usr/lib/x86_64-linux-gnu/libzstd.a
toolbin="$RUNNER_TEMP/buster-test-tools"
mkdir -p "$toolbin"
printf '%s\n' "$toolbin" "$llvm/bin" >> "$GITHUB_PATH"
printf 'BUSTER_CLANG_RESOURCE_DIR=%s\n' "$llvm/lib/clang/23" >> "$GITHUB_ENV"
# The pragma oracle links the official libclang-cpp shared object; it must load
# that same release at execution time rather than any runner-provided library.
printf 'LD_LIBRARY_PATH=%s%s\n' "$llvm/lib" "${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" >> "$GITHUB_ENV"
perf --version
# Keep the external mold oracle at the qualified release rather than the
# runner distribution's older package. Its archive retains its MIT license.
curl --fail --location --retry 3 \
    https://github.com/rui314/mold/releases/download/v3.0.0/mold-3.0.0-x86_64-linux.tar.gz \
    --output "$RUNNER_TEMP/mold.tar.gz"
printf '%s  %s\n' 6c90d4a474c7c0409dfb575be03a5345878ac14fdba18de8b40fa58c60121189 \
    "$RUNNER_TEMP/mold.tar.gz" | sha256sum --check
tar -xzf "$RUNNER_TEMP/mold.tar.gz" -C "$RUNNER_TEMP" --no-same-owner
mold="$RUNNER_TEMP/mold-3.0.0-x86_64-linux"
test -f "$mold/share/doc/mold/LICENSE"
test "$("$mold/bin/mold" --version)" = 'mold 3.0.0 (8de38c35a2df16a25f7ff87ac3ad07156a925beb; compatible with GNU ld)'
ln -s "$mold/bin/mold" "$toolbin/mold"
ln -s "$mold/bin/mold" "$toolbin/ld.mold"
# Match the existing external-linker oracle rather than silently omitting it.
curl --fail --location --retry 3 \
    https://github.com/wild-linker/wild/releases/download/0.10.0/wild-linker-0.10.0-x86_64-unknown-linux-gnu.tar.gz \
    --output "$RUNNER_TEMP/wild.tar.gz"
printf '%s  %s\n' 641265506a7c06cfb03181b8916ab663ec8407855db6d4db7f8450667d105283 "$RUNNER_TEMP/wild.tar.gz" | sha256sum --check --status
mkdir -p "$RUNNER_TEMP/wild"
tar -xzf "$RUNNER_TEMP/wild.tar.gz" -C "$RUNNER_TEMP/wild" --no-same-owner
wild=$(find "$RUNNER_TEMP/wild" -type f -name wild -print -quit)
test -x "$wild"
ln -s "$wild" "$toolbin/wild"
ln -s "$wild" "$toolbin/ld.wild"
sudo sysctl -w kernel.perf_event_paranoid=-1 kernel.kptr_restrict=0 kernel.yama.ptrace_scope=0
