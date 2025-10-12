cmake_minimum_required(VERSION 3.31)
project(buster)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug STRING "Build type" FORCE)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_COLOR_DIAGNOSTICS ON)

option(CMAKE_COMPILE_WARNING_AS_ERROR "Compile warnings as errors" ON)
option(USE_SANITIZER "Build with sanitizer support" OFF)

add_compile_options(-march=native -funsigned-char -fwrapv -ferror-limit=1 -Wno-unused-function)

set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CLANG_TARGET_TRIPLE "x86_64-unknown-elf")
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(CMAKE_LINKER_TYPE LLD)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LINKER_SCRIPT ${SRC_DIR}/linker_script_x86_64.lds)

add_compile_options(--target=x86_64-unknown-elf -ffreestanding -nostdlib -nostdlibinc -static -fno-pic -mno-red-zone -fno-stack-protector -fno-pie -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables -fno-unwind-tables)
add_link_options(-nostdlib -nostartfiles -static -fno-pie -Wl,--build-id=none -T ${LINKER_SCRIPT})

if (CMAKE_C_COMPILER_TARGET MATCHES "^(x86_64|AMD64)$")
    add_compile_options(-masm=intel)
endif()

include_directories(BEFORE ${SRC_DIR} ${SRC_DIR}/.. ${SRC_DIR}/limine)
add_compile_definitions(
    $<$<CONFIG:Debug>:BUSTER_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:BUSTER_DEBUG=0>
)

add_compile_definitions(BUSTER_KERNEL=1)

if (NOT "${XC_SDK_PATH}" STREQUAL "")
    add_compile_definitions(XC_SDK_PATH="${XC_SDK_PATH}")
endif()

add_compile_options(-Wno-pragma-once-outside-header)

# Optional LLVM support
if (USE_SANITIZER)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()


if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    set(UNITY_BUILD ON)
else()
    set(UNITY_BUILD OFF)
endif()

message(STATUS "UNITY_BUILD: ${UNITY_BUILD}")

if (UNITY_BUILD)
    add_compile_definitions(UNITY_BUILD=1)
    add_executable(buster_kernel
        ${SRC_DIR}/main.c
    )
else()
    add_compile_definitions(UNITY_BUILD=0)
    add_executable(buster_kernel
        ${SRC_DIR}/../lib.c
        ${SRC_DIR}/main.c
    )
endif()
set_target_properties(buster_kernel PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
