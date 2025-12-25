#!/usr/bin/env bash
set -eu

if [[ -z "${BUSTER_CI:-}" ]]; then
    BUSTER_CI=0
fi

if [[ -z "${BUSTER_OPTIMIZE:-}" ]]; then
    BUSTER_OPTIMIZE=0
fi

if [[ -z "${BUSTER_LLVM_VERSION:-}" ]]; then
    BUSTER_LLVM_VERSION=21.1.7
fi

if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    if [[ "$BUSTER_CI" == "1" ]]; then
        source ./setup_ci.sh
    else
        export BUSTER_ARCH=x86_64
        export BUSTER_OS=linux
        export CMAKE_PREFIX_PATH=$HOME/dev/toolchain/install/llvm_${BUSTER_LLVM_VERSION}_${BUSTER_ARCH}-${BUSTER_OS}-Release
    fi
fi

if [[ -z "${CLANG:-}" ]]; then
    export CLANG=$CMAKE_PREFIX_PATH/bin/clang
fi

if [[ "$#" != "0" ]]; then
    set -x
fi

XC_SDK_PATH=""
CLANG_EXTRA_FLAGS=""
if [[ "$BUSTER_OS" == "macos" ]]; then
    export XC_SDK_PATH=$(xcrun --sdk macosx --show-sdk-path)
    CLANG_EXTRA_FLAGS="-isysroot $XC_SDK_PATH"
fi

#if 0BUSTER_REGENERATE=0 build/builder $@ 2>/dev/null
#endif
#if [[ "$?" != "0" && "$?" != "333" ]]; then
    mkdir -p build
    $CLANG build.c -o build/builder -fuse-ld=lld $CLANG_EXTRA_FLAGS -Isrc -std=gnu2x -march=native -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_IO_RING=0 -DBUSTER_USE_PTHREAD=1 -DBUSTER_INCLUDE_TESTS=1 -g -Werror -Wall -Wextra -Wpedantic -pedantic -Wno-language-extension-token -Wno-gnu-auto-type -Wno-gnu-empty-struct -Wno-bitwise-instead-of-logical -Wno-unused-function -Wno-gnu-flexible-array-initializer -Wno-missing-field-initializers -Wno-pragma-once-outside-header -pthread -fwrapv -fno-strict-aliasing -funsigned-char -ferror-limit=1 -fsanitize=address -fsanitize=undefined -fsanitize=bounds -fsanitize-recover=undefined #-ftime-trace -ftime-trace-verbose
    if [[ "$?" == "0" ]]; then
        BUSTER_REGENERATE=1 build/builder $@
        # BUSTER_REGENERATE=1 lldb -b -o run -o 'bt all' -- build/builder $@
    fi
#endif fi
exit $?
