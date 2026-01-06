#!/usr/bin/env bash
set -eu
#
# source build.env
#
# BUSTER_CI=0
# BUSTER_DOWNLOAD_TOOLCHAIN=0
# BUSTER_SELF_HOSTED=0
# BUSTER_FUZZ_DURATION_SECONDS=0
#
# if [ "$#" -ne 0 ]; then
#     set -x
# fi
#
# for arg in "${@:2}"; do
#     case "$arg" in
#         --ci=*) BUSTER_CI="${arg#*=}";;
#         --download_toolchain=*) BUSTER_DOWNLOAD_TOOLCHAIN="${arg#*=}";;
#         --self-hosted=*) BUSTER_SELF_HOSTED="${arg#*=}";;
#         --fuzz-duration=*) BUSTER_FUZZ_DURATION_SECONDS="${arg#*=}";;
#         *) ;;
#     esac
# done
#
# if [[ "$BUSTER_FUZZ_DURATION_SECONDS" == "0" ]]; then
#     BUSTER_FUZZ_DURATION_SECONDS=$BUSTER_FAST_FUZZ_DURATION_SECONDS
# fi
#     if [[ "$BUSTER_CI" == "1" ]]; then
#         if [[ "$BUSTER_SELF_HOSTED" == "1" ]]; then
#             BUSTER_FUZZ_DURATION_SECONDS=$BUSTER_FAST_FUZZ_DURATION_SECONDS
#         else
#             if [ "$(git branch --show-current)" = "main" ]; then
#                 BUSTER_FUZZ_DURATION_SECONDS=$BUSTER_THOROUGH_FUZZ_DURATION_SECONDS
#             else
#                 BUSTER_FUZZ_DURATION_SECONDS=$BUSTER_FAST_FUZZ_DURATION_SECONDS
#             fi
#         fi
#     else
#         BUSTER_FUZZ_DURATION_SECONDS=$BUSTER_FAST_FUZZ_DURATION_SECONDS
#     fi
#
# BUSTER_NATIVE_ARCH=$(uname -m)
# BUSTER_NATIVE_OS=$(uname -s)
#
# case "$BUSTER_NATIVE_ARCH" in
#     x86_64) BUSTER_ARCH=x86_64;;
#     arm64|aarch64) BUSTER_ARCH=aarch64;;
#     *) echo "error: unknown CPU architecture: $BUSTER_NATIVE_ARCH"; exit 1;;
# esac
#
# case "$BUSTER_NATIVE_OS" in
#     Linux) BUSTER_OS=linux;;
#     Darwin) BUSTER_OS=macos;;
#     *) echo "error: unknown operating system: $BUSTER_NATIVE_OS"; exit 1;;
# esac
#
# if [[ "$BUSTER_CI" == "1" ]]; then
#     case "$BUSTER_OS" in
#         linux) cat /proc/cpuinfo;;
#         macos) sysctl -n machdep.cpu.brand_string;;
#         *);;
#     esac
# fi
#
# BUSTER_LLVM_VERSION_STRING="${BUSTER_LLVM_VERSION_MAJOR}.${BUSTER_LLVM_VERSION_MINOR}.${BUSTER_LLVM_VERSION_REVISION}"
# BUSTER_TOOLCHAIN_BASENAME=llvm_${BUSTER_LLVM_VERSION_STRING}_${BUSTER_ARCH}-${BUSTER_OS}-Release
# BUSTER_TOOLCHAIN_INSTALL_PATH=$HOME/dev/toolchain/install
# BUSTER_TOOLCHAIN_ABSOLUTE_PATH=$BUSTER_TOOLCHAIN_INSTALL_PATH/$BUSTER_TOOLCHAIN_BASENAME
#
# # Force removing the directory if we the download is also forced
# if [[ "$BUSTER_DOWNLOAD_TOOLCHAIN" == "1" ]]; then
#     rm -rf $BUSTER_TOOLCHAIN_ABSOLUTE_PATH || true
# fi
#
# if [[ "$BUSTER_CI" == "1" && "$BUSTER_SELF_HOSTED" == "0" ]]; then
#     if [[ ! -d "$BUSTER_TOOLCHAIN_ABSOLUTE_PATH" ]]; then
#         BUSTER_DOWNLOAD_TOOLCHAIN=1
#     fi
# fi
#
# if [[ "$BUSTER_DOWNLOAD_TOOLCHAIN" == "1" ]]; then
#     BUSTER_TOOLCHAIN_7Z=$BUSTER_TOOLCHAIN_BASENAME.7z
#     BUSTER_TOOLCHAIN_URL=https://github.com/buster14a/toolchain/releases/download/v${BUSTER_LLVM_VERSION_STRING}/$BUSTER_TOOLCHAIN_7Z
#     wget -q $BUSTER_TOOLCHAIN_URL
#     mkdir -p $BUSTER_TOOLCHAIN_INSTALL_PATH
#     7z x $BUSTER_TOOLCHAIN_7Z -o$BUSTER_TOOLCHAIN_INSTALL_PATH -y
#     rm $BUSTER_TOOLCHAIN_7Z
# fi
#
# BUSTER_CLANG_ABSOLUTE_PATH="$BUSTER_TOOLCHAIN_ABSOLUTE_PATH/bin/clang"
#
# CLANG_EXTRA_FLAGS=()
# if [[ "$BUSTER_OS" == "macos" ]]; then
#     BUSTER_XC_SDK_PATH=$(xcrun --sdk macosx --show-sdk-path)
#     CLANG_EXTRA_FLAGS+=("-DBUSTER_XC_SDK_PATH=\"$BUSTER_XC_SDK_PATH\"")
#     CLANG_EXTRA_FLAGS+=("-isysroot")
#     CLANG_EXTRA_FLAGS+=("$BUSTER_XC_SDK_PATH")
# fi
#
# BUSTER_BUILD_DIRECTORY=build
# mkdir -p $BUSTER_BUILD_DIRECTORY
# $BUSTER_CLANG_ABSOLUTE_PATH build.c -o $BUSTER_BUILD_DIRECTORY/builder -fuse-ld=lld "-DBUSTER_TOOLCHAIN_ABSOLUTE_PATH=\"$BUSTER_TOOLCHAIN_ABSOLUTE_PATH\"" "-DBUSTER_CLANG_ABSOLUTE_PATH=\"$BUSTER_CLANG_ABSOLUTE_PATH\"" "${CLANG_EXTRA_FLAGS[@]}" "${BUSTER_COMMON_COMPILE_FLAGS[@]}" -O0 -march=native -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_IO_RING=0 -DBUSTER_USE_PTHREAD=1 -DBUSTER_INCLUDE_TESTS=1  -pthread -ferror-limit=1 -g -ftime-trace -ftime-trace-verbose #-fsanitize=address -fsanitize=undefined -fsanitize=bounds -fsanitize-recover=undefined #-ftime-trace -ftime-trace-verbose
# if [[ "$?" == "0" ]]; then
#     build/builder $@ --fuzz-duration=$BUSTER_FUZZ_DURATION_SECONDS
#     # lldb -b -o run -o 'bt all' -- build/builder $@
# fi
# exit $?
BUSTER_BUILD_DIRECTORY=build
mkdir -p $BUSTER_BUILD_DIRECTORY
clang preamble.c -o $BUSTER_BUILD_DIRECTORY/preamble -Isrc -Wall -Werror -std=gnu2x -Wno-unused-function -funsigned-char -g #-ftime-trace -ftime-trace-verbose
$BUSTER_BUILD_DIRECTORY/preamble
