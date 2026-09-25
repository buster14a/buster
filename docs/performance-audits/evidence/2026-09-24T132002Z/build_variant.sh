#!/bin/bash
# usage: build_variant.sh <repo_root> <out> <march>
set -e
R=$1; OUT=$2; MARCH=${3:-native}
DBG=-g; [ "$MARCH" != native ] && DBG=-gdwarf-4
/usr/bin/clang-18 -DBUSTER_BENCH_ALLOCATIONS=0 -DBUSTER_COMPILER_ZIG=0 -DBUSTER_FUZZ_AVAILABLE=0 -DBUSTER_HOST_C_COMPILER=\"/home/ubuntu/.local/bin/clang\" -DBUSTER_HOST_C_COMPILER_ARG1=\"\" -DBUSTER_HOST_C_COMPILER_ID=\"Clang\" -DBUSTER_HOST_C_COMPILER_MSVC=0 -DBUSTER_HOST_C_RESOURCE_INCLUDE=\"/usr/lib/llvm-18/lib/clang/18/include\" -DBUSTER_INCLUDE_TESTS=1 -DBUSTER_INSTRUMENT=0 -DBUSTER_LINK_LIBC=1 -DBUSTER_LSAN_SUPPORTED=0 -DBUSTER_OPTIMIZE=1 -DBUSTER_SANITIZE=0 -DBUSTER_SINGLE_THREADED=0 -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_D3D12=0 -DBUSTER_USE_METAL=0 -DBUSTER_USE_SLANG_SHADERS=0 -DBUSTER_USE_VULKAN=0 -I<repo>/build/generated -I$R/src -O3 -DNDEBUG -fwrapv -fno-strict-aliasing -funsigned-char -fno-exceptions -fno-omit-frame-pointer $DBG -march=$MARCH -c $R/src/buster/apps/ide/ide.c -o $OUT.o
LINK="-O3"
LIBS=-lm
/usr/bin/clang-18 $LINK $OUT.o -o $OUT $LIBS
rm -f $OUT.o
