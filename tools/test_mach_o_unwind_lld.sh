#!/usr/bin/env bash
set -euo pipefail

ide=${1:-build/Release/ide}
output=${2:-build/mach-o-unwind-lld}
mkdir -p "$output"

for tool in clang ld64.lld llvm-readobj; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done
clang --version | tee "$output/clang-version.txt"
ld64.lld --version | tee "$output/lld-version.txt"
llvm-readobj --version | tee "$output/llvm-readobj-version.txt"

while IFS='|' read -r name buster_target clang_target lld_arch; do
    directory="$output/$name"
    mkdir -p "$directory"
    "$ide" cc -target "$buster_target" -g0 -fregister-allocator=fast -c \
        tests/basic_c_mach_unwind_subject.c -o "$directory/buster-subject.o"
    clang --target="$clang_target" -g0 -c tests/basic_c_mach_unwind_host.c -o "$directory/clang-host.o"
    clang --target="$clang_target" -g0 -c tests/basic_c_mach_unwind_subject.c -o "$directory/clang-subject.o"
    llvm-readobj --file-headers --sections --relocations --symbols "$directory/buster-subject.o" \
        > "$directory/buster-subject.txt"
    llvm-readobj --file-headers --sections --relocations --symbols "$directory/clang-subject.o" \
        > "$directory/clang-subject.txt"
    ld64.lld -arch "$lld_arch" -platform_version macos 13.0.0 13.0.0 -e _main \
        -o "$directory/buster-linked" "$directory/clang-host.o" "$directory/buster-subject.o"
    ld64.lld -arch "$lld_arch" -platform_version macos 13.0.0 13.0.0 -e _main \
        -o "$directory/clang-linked" "$directory/clang-host.o" "$directory/clang-subject.o"
    llvm-readobj --file-headers --sections "$directory/buster-linked" > "$directory/buster-linked.txt"
    llvm-readobj --file-headers --sections "$directory/clang-linked" > "$directory/clang-linked.txt"
done <<'TARGETS'
x86_64|x86_64-apple-macos|x86_64-apple-macos13|x86_64
aarch64|aarch64-apple-macos|arm64-apple-macos13|arm64
TARGETS
