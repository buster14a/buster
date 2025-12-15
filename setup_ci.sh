#!/usr/bin/env bash
set -eu

clang -std=gnu2x -g -o $GITHUB_WORKSPACE/setup_ci $GITHUB_WORKSPACE/setup_ci.c
echo $GITHUB_WORKSPACE
$GITHUB_WORKSPACE/setup_ci $GITHUB_WORKSPACE/helper.sh
ls
cat $GITHUB_WORKSPACE/helper.sh
source $GITHUB_WORKSPACE/helper.sh
echo "BUSTER_OS=$BUSTER_OS" >> $GITHUB_ENV
echo "BUSTER_ARCH=$BUSTER_ARCH" >> $GITHUB_ENV

if [[ "$BUSTER_OS" == "linux" ]]; then
    cat /proc/cpuinfo
elif [[ "$BUSTER_OS" == "macos" ]]; then
    sysctl -n machdep.cpu.brand_string
elif [[ "$BUSTER_OS" == "windows" ]]; then
    wmic cpu get name
fi

BUSTER_TOOLCHAIN_BASENAME=llvm_${BUSTER_LLVM_VERSION}_${BUSTER_ARCH}-${BUSTER_OS}-Release
BUSTER_TOOLCHAIN_7Z=$BUSTER_TOOLCHAIN_BASENAME.7z
BUSTER_TOOLCHAIN_INSTALL_PATH=$HOME/dev/toolchain/install
BUSTER_TOOLCHAIN_ABSOLUTE_PATH=$BUSTER_TOOLCHAIN_INSTALL_PATH/$BUSTER_TOOLCHAIN_BASENAME
if [[ ! -d "$BUSTER_TOOLCHAIN_ABSOLUTE_PATH" ]]; then
    BUSTER_TOOLCHAIN_URL=https://github.com/buster14a/toolchain/releases/download/v${BUSTER_LLVM_VERSION}/$BUSTER_TOOLCHAIN_7Z
    wget -q $BUSTER_TOOLCHAIN_URL
    mkdir -p $BUSTER_TOOLCHAIN_INSTALL_PATH
    7z x $BUSTER_TOOLCHAIN_7Z -o$BUSTER_TOOLCHAIN_INSTALL_PATH -y
fi
export CLANG="$BUSTER_TOOLCHAIN_ABSOLUTE_PATH/bin/clang"
