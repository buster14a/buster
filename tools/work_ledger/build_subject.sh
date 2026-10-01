#!/usr/bin/env bash
# Build one work-ledger subject: a ledger compiler (BUSTER_BENCH_ALLOCATIONS=1)
# and a plain compiler (=0) from the same checkout, as unity headless builds
# with the host Clang. -march=x86-64-v3 keeps the binaries runnable under
# valgrind (no EVEX) on every hosted x86-64 runner.
# usage: build_subject.sh <checkout> <output-directory>
set -euo pipefail
checkout=$1
output=$2
mkdir -p "$output"
common=(
    -DBUSTER_COMPILER_ZIG=0 -DBUSTER_FUZZ_AVAILABLE=0 "-DBUSTER_HOST_C_COMPILER=\"clang\"" "-DBUSTER_HOST_C_COMPILER_ARG1=\"\""
    "-DBUSTER_HOST_C_COMPILER_ID=\"Clang\"" -DBUSTER_HOST_C_COMPILER_MSVC=0
    "-DBUSTER_HOST_C_RESOURCE_INCLUDE=\"$(clang -print-resource-dir)/include\""
    -DBUSTER_INCLUDE_TESTS=0 -DBUSTER_INSTRUMENT=0 -DBUSTER_LINK_LIBC=1 -DBUSTER_LSAN_SUPPORTED=0 -DBUSTER_OPTIMIZE=1
    -DBUSTER_SANITIZE=0 -DBUSTER_SINGLE_THREADED=0 -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_D3D12=0 -DBUSTER_USE_METAL=0
    -DBUSTER_USE_SLANG_SHADERS=0 -DBUSTER_USE_VULKAN=0
    -I"$checkout/src" -O2 -g -DNDEBUG -fwrapv -fno-strict-aliasing -funsigned-char -fno-exceptions -fno-omit-frame-pointer
    -march=x86-64-v3 -Wno-unused-function -Wno-invalid-feature-combination
)
clang "${common[@]}" -DBUSTER_BENCH_ALLOCATIONS=1 "$checkout/src/buster/apps/ide/ide.c" -lm -o "$output/ide-ledger" &
ledger_pid=$!
clang "${common[@]}" -DBUSTER_BENCH_ALLOCATIONS=0 "$checkout/src/buster/apps/ide/ide.c" -lm -o "$output/ide-plain" &
plain_pid=$!
wait "$ledger_pid"
wait "$plain_pid"
sha256sum "$output/ide-ledger" "$output/ide-plain"
