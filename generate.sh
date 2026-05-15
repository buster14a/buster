#!/usr/bin/env bash
set -euox pipefail

if [[ -z "${BUSTER_BUILD_DIRECTORY:-}" ]]; then
    BUSTER_BUILD_DIRECTORY=build
fi
echo "BUSTER_BUILD_DIRECTORY: $BUSTER_BUILD_DIRECTORY"

rm -rf "$BUSTER_BUILD_DIRECTORY"
mkdir -p "$BUSTER_BUILD_DIRECTORY"

if [[ -z "${BUSTER_CI:-}" ]]; then
    BUSTER_CI=OFF
fi

if [[ -z "${BUSTER_FUZZ:-}" ]]; then
    BUSTER_FUZZ=OFF
fi

if [[ -z "${BUSTER_OPTIMIZE:-}" ]]; then
    BUSTER_OPTIMIZE=OFF
fi

if [[ -z "${BUSTER_SANITIZE:-}" ]]; then
    BUSTER_SANITIZE=OFF
fi

if [[ -z "${BUSTER_LTO:-}" ]]; then
    BUSTER_LTO=OFF
fi

if [[ -z "${BUSTER_TIME_TRACE:-}" ]]; then
    BUSTER_TIME_TRACE=OFF
fi

if [[ -z "${BUSTER_INCLUDE_TESTS:-}" ]]; then
    BUSTER_INCLUDE_TESTS=ON
fi

if [[ -z "${BUSTER_LINK_LIBC:-}" ]]; then
    BUSTER_LINK_LIBC=ON
fi

if [[ -z "${BUSTER_CC:-}" ]]; then
    BUSTER_CC=clang
fi

if [[ "$BUSTER_CC" == "zig cc" || "$BUSTER_CC" == "zig;cc" ]]; then
    BUSTER_CC=zig
fi

echo "BUSTER_CC: $BUSTER_CC"

CMAKE_COMPILER_ARGS=()
CMAKE_EXTRA_ARGS=()

if [[ "$BUSTER_CC" == *zig* ]]; then
    CMAKE_EXTRA_ARGS=(
        -DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE
        -DCMAKE_C_LINK_DEPENDS_USE_LINKER=FALSE
    )
    BUSTER_ASM=$BUSTER_CC
    BUSTER_CXX=$BUSTER_CC
    CMAKE_COMPILER_ARGS=(
        "-DCMAKE_C_COMPILER=${BUSTER_CC};cc"
        "-DCMAKE_CXX_COMPILER=${BUSTER_CXX};c++"
        "-DCMAKE_ASM_COMPILER=${BUSTER_ASM};cc"
    )
elif [[ "$BUSTER_CC" == *clang* ]]; then
    BUSTER_ASM=$BUSTER_CC
    BUSTER_CXX="${BUSTER_CC/clang/clang++}"
elif [[ "$BUSTER_CC" == *gcc* ]]; then
    BUSTER_ASM=$BUSTER_CC
    BUSTER_CXX="${BUSTER_CC/gcc/g++}"
else
    BUSTER_ASM=$BUSTER_CC
    BUSTER_CXX=clang++
fi

if [[ ${#CMAKE_COMPILER_ARGS[@]} -eq 0 ]]; then
    CMAKE_COMPILER_ARGS=(
        "-DCMAKE_C_COMPILER=$BUSTER_CC"
        "-DCMAKE_CXX_COMPILER=$BUSTER_CXX"
        "-DCMAKE_ASM_COMPILER=$BUSTER_ASM"
    )
fi

if [[ -z "${BUSTER_LINKER:-}" ]]; then
    if [[ "$(uname -s)" == *Linux* ]]; then
        if [[ "$BUSTER_CC" != *zig* && "$BUSTER_CC" != *tcc* ]]; then
            BUSTER_LINKER=MOLD
        fi
    fi
fi

if [[ -z "${BUSTER_LINKER:-}" ]]; then
    BUSTER_LINKER=DEFAULT
fi

echo "BUSTER_LINKER: $BUSTER_LINKER"

cmake \
    --warn-uninitialized \
    -Werror=dev \
    -B "$BUSTER_BUILD_DIRECTORY" \
    -G Ninja \
    "${CMAKE_COMPILER_ARGS[@]}" \
    "-DCMAKE_LINKER_TYPE=$BUSTER_LINKER" \
    "-DBUSTER_FUZZ=$BUSTER_FUZZ" \
    "-DBUSTER_OPTIMIZE=$BUSTER_OPTIMIZE" \
    "-DBUSTER_SANITIZE=$BUSTER_SANITIZE" \
    "-DBUSTER_LTO=$BUSTER_LTO" \
    "-DBUSTER_TIME_TRACE=$BUSTER_TIME_TRACE" \
    "-DBUSTER_INCLUDE_TESTS=$BUSTER_INCLUDE_TESTS" \
    "-DBUSTER_CI=$BUSTER_CI" \
    "-DBUSTER_LINK_LIBC=$BUSTER_LINK_LIBC" \
    "${CMAKE_EXTRA_ARGS[@]}" \
    "$@"
