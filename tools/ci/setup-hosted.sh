#!/usr/bin/env bash
# Install test-only dependencies on a disposable GitHub-hosted Ubuntu 26.04 VM.
set -euo pipefail
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64
. /etc/os-release
test "$VERSION_ID" = 26.04
curl --fail --location --retry 3 https://apt.llvm.org/llvm-snapshot.gpg.key --output "$RUNNER_TEMP/llvm.asc"
sudo install -m 644 "$RUNNER_TEMP/llvm.asc" /usr/share/keyrings/buster-llvm.asc
printf '%s\n' 'deb [signed-by=/usr/share/keyrings/buster-llvm.asc] https://apt.llvm.org/resolute/ llvm-toolchain-resolute-23 main' |
    sudo tee /etc/apt/sources.list.d/buster-llvm.list > /dev/null
sudo apt-get update
sudo apt-get install --yes --no-install-recommends clang-23 llvm-23-dev llvm-23-tools \
    libclang-23-dev libclang-cpp23-dev libclang-rt-23-dev lld-23 \
    gcc g++ binutils libc6-dev gdb mold tcc zstd linux-tools-azure
# LLVM's release branch receives current patch updates. Use the same toolchain
# for the build, C/C++ oracles, pragma adapter, object and DWARF consumers.
toolbin="$RUNNER_TEMP/buster-test-tools"
mkdir -p "$toolbin"
ln -s /usr/lib/llvm-23/bin/clang "$toolbin/cc"
printf '%s\n' "$toolbin" /usr/lib/llvm-23/bin >> "$GITHUB_PATH"
perf=$(find /usr/lib -maxdepth 3 -type f -path '*linux-tools*/perf' -print -quit)
test -n "$perf"
ln -s "$perf" "$toolbin/perf"
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
