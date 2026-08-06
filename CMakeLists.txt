cmake_minimum_required(VERSION 3.17)
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(buster C)

set (CMAKE_COLOR_DIAGNOSTICS ON)

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)

set(BUSTER_CONFIG_TYPES Debug Release)

function(buster_config_is_optimized out_var config)
    if ("${config}" STREQUAL "Release" OR "${config}" STREQUAL "RelWithDebInfo" OR "${config}" STREQUAL "MinSizeRel")
        set(${out_var} ON PARENT_SCOPE)
    elseif("${config}" STREQUAL "Debug")
        set(${out_var} OFF PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Invalid build configuration: ${config}")
    endif()
endfunction()

get_property(BUSTER_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if (BUSTER_MULTI_CONFIG)
    if (DEFINED CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "")
        message(FATAL_ERROR "CMAKE_BUILD_TYPE is not used with multi-config generators; use CMAKE_DEFAULT_BUILD_TYPE or build with --config")
    endif()

    if (NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "${BUSTER_CONFIG_TYPES}" CACHE STRING "Available build configurations" FORCE)
    endif()

    if (DEFINED CMAKE_DEFAULT_BUILD_TYPE AND NOT CMAKE_DEFAULT_BUILD_TYPE STREQUAL "")
        set(BUSTER_DEFAULT_CONFIG "${CMAKE_DEFAULT_BUILD_TYPE}")
    else()
        set(BUSTER_DEFAULT_CONFIG Debug)
    endif()

    if (NOT BUSTER_DEFAULT_CONFIG IN_LIST CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR "CMAKE_DEFAULT_BUILD_TYPE '${BUSTER_DEFAULT_CONFIG}' is not in CMAKE_CONFIGURATION_TYPES: ${CMAKE_CONFIGURATION_TYPES}")
    endif()

    set(CMAKE_DEFAULT_BUILD_TYPE "${BUSTER_DEFAULT_CONFIG}" CACHE STRING "Default multi-config build configuration" FORCE)
    set_property(CACHE CMAKE_DEFAULT_BUILD_TYPE PROPERTY STRINGS ${CMAKE_CONFIGURATION_TYPES})
else()
    if (DEFINED CMAKE_DEFAULT_BUILD_TYPE AND NOT CMAKE_DEFAULT_BUILD_TYPE STREQUAL "")
        message(FATAL_ERROR "CMAKE_DEFAULT_BUILD_TYPE is only used with multi-config generators; use CMAKE_BUILD_TYPE")
    endif()

    if (DEFINED CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "")
        set(BUSTER_DEFAULT_CONFIG "${CMAKE_BUILD_TYPE}")
    else()
        set(BUSTER_DEFAULT_CONFIG Debug)
    endif()

    set(CMAKE_BUILD_TYPE "${BUSTER_DEFAULT_CONFIG}" CACHE STRING "Build configuration" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${BUSTER_CONFIG_TYPES})
endif()

buster_config_is_optimized(BUSTER_DEFAULT_CONFIG_OPTIMIZED "${BUSTER_DEFAULT_CONFIG}")

set(BUSTER_STATUS_CMAKE_CONFIGURATION_TYPES "")
if (DEFINED CMAKE_CONFIGURATION_TYPES)
    set(BUSTER_STATUS_CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}")
endif()

set(BUSTER_STATUS_CMAKE_DEFAULT_BUILD_TYPE "")
if (DEFINED CMAKE_DEFAULT_BUILD_TYPE)
    set(BUSTER_STATUS_CMAKE_DEFAULT_BUILD_TYPE "${CMAKE_DEFAULT_BUILD_TYPE}")
endif()

set(BUSTER_STATUS_CMAKE_BUILD_TYPE "")
if (DEFINED CMAKE_BUILD_TYPE)
    set(BUSTER_STATUS_CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

set(BUSTER_CROSS_COMPILE ${CMAKE_CROSSCOMPILING})

# CMake leaves CMAKE_SYSTEM_PROCESSOR empty for the iOS platform; derive it from
# the requested architecture so the per-arch codegen module selection still works.
if (NOT CMAKE_SYSTEM_PROCESSOR)
    if (CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 CMAKE_SYSTEM_PROCESSOR)
    else()
        set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
endif()

set(C_COMPILER_GNU OFF)
set(C_COMPILER_APPLE_CLANG OFF)
set(C_COMPILER_CLANG OFF)
set(C_COMPILER_TCC OFF)
set(C_COMPILER_ZIG OFF)
set(C_COMPILER_MSVC OFF)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(C_COMPILER_GNU ON)
elseif (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(C_COMPILER_APPLE_CLANG ON)
elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(C_COMPILER_CLANG ON)
elseif (CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
    set(C_COMPILER_TCC ON)
elseif (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(C_COMPILER_MSVC ON)
endif()

if (WIN32 AND C_COMPILER_TCC)
    # TinyCC links Windows DLLs through .def files using -lname.
    set(CMAKE_LINK_LIBRARY_SUFFIX "")
endif()

get_filename_component(C_COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)
if ((C_COMPILER_NAME STREQUAL "zig" OR C_COMPILER_NAME STREQUAL "zig.exe") AND "${CMAKE_C_COMPILER_ARG1}" STREQUAL "cc")
    set(C_COMPILER_ZIG ON)
endif()

option(BUSTER_CI "Enable CI mode" OFF)
option(BUSTER_LINK_LIBC "Link libc" ON)
option(BUSTER_UNITY_BUILD "Enable unity builds for optimized non-sanitized Clang configurations" ON)
option(BUSTER_INCLUDE_TESTS "Enable tests" ON)
option(BUSTER_SANITIZE "Enable sanitizers" OFF)
option(BUSTER_TIME_TRACE "Enable time tracing" OFF)
option(BUSTER_INSTRUMENT "Enable fine-grained timing instrumentation for ide bench" OFF)
option(BUSTER_FUZZ_AVAILABLE "Build with runtime fuzzing support" OFF)
option(BUSTER_LTO "Enable LTO" OFF)
option(BUSTER_CHECK_OPTIONAL_WARNINGS "Probe and enable optional compiler warning flags" ON)
option(BUSTER_DEVELOPER_TARGETS "Enable convenience run/test/debug targets" ON)

set(BUSTER_SANITIZER_REDUCED_DEBUG OFF)
if (BUSTER_SANITIZE AND NOT WIN32 AND (C_COMPILER_CLANG OR C_COMPILER_APPLE_CLANG) AND NOT C_COMPILER_ZIG)
    # CMake's Debug and RelWithDebInfo defaults add full debug information before
    # target options. Remove only the exact full -g token so sanitizer builds use
    # one reduced debug-info mode, while preserving every other flag byte and
    # leaving the cache untouched when the sanitizer option is later disabled.
    set(BUSTER_SANITIZER_REDUCED_DEBUG ON)
    foreach(flag_variable IN ITEMS
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_C_FLAGS_RELWITHDEBINFO
    )
        string(REGEX REPLACE "(^|[ \t])-g([ \t]|$)" "\\1\\2" sanitizer_flags "${${flag_variable}}")
        set("${flag_variable}" "${sanitizer_flags}")
    endforeach()
endif()

set(BUSTER_FUZZ_NO_MAIN_RUNTIME "")
if (BUSTER_FUZZ_AVAILABLE)
    if (NOT C_COMPILER_CLANG OR C_COMPILER_ZIG OR APPLE)
        message(FATAL_ERROR "BUSTER_FUZZ_AVAILABLE requires Clang on Linux or Windows")
    endif()

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
        set(BUSTER_FUZZ_RUNTIME_ARCH x86_64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
        set(BUSTER_FUZZ_RUNTIME_ARCH aarch64)
    else()
        message(FATAL_ERROR "Unsupported fuzz runtime architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if (WIN32)
        set(BUSTER_FUZZ_RUNTIME_NAME "clang_rt.fuzzer_no_main-${BUSTER_FUZZ_RUNTIME_ARCH}.lib")
    else()
        set(BUSTER_FUZZ_RUNTIME_NAME "libclang_rt.fuzzer_no_main-${BUSTER_FUZZ_RUNTIME_ARCH}.a")
    endif()

    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" "--print-file-name=${BUSTER_FUZZ_RUNTIME_NAME}"
        OUTPUT_VARIABLE BUSTER_FUZZ_NO_MAIN_RUNTIME
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE BUSTER_FUZZ_RUNTIME_RESULT
    )
    if (NOT BUSTER_FUZZ_RUNTIME_RESULT EQUAL 0 OR NOT IS_ABSOLUTE "${BUSTER_FUZZ_NO_MAIN_RUNTIME}" OR NOT EXISTS "${BUSTER_FUZZ_NO_MAIN_RUNTIME}")
        message(FATAL_ERROR "Could not find the Clang no-main fuzz runtime '${BUSTER_FUZZ_RUNTIME_NAME}': ${BUSTER_FUZZ_NO_MAIN_RUNTIME}")
    endif()
endif()

set(BUSTER_USE_VULKAN_DEFAULT OFF)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android" OR C_COMPILER_TCC)
    set(BUSTER_USE_VULKAN_DEFAULT ON)
endif()

# Adreno's shader compiler crashes on buffer_reference SPIR-V, so on Android the
# Vulkan rect shader pulls vertices from a storage buffer instead of a raw pointer.
set(BUSTER_SLANG_VULKAN_EXTRA_DEFINES "")
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(BUSTER_SLANG_VULKAN_EXTRA_DEFINES -DBUSTER_VULKAN_VERTEX_SSBO=1)
endif()
option(BUSTER_USE_VULKAN "Use the Vulkan renderer instead of the platform fallback" ${BUSTER_USE_VULKAN_DEFAULT})

set(BUSTER_USE_D3D12 OFF)
if (WIN32 AND NOT BUSTER_USE_VULKAN)
    set(BUSTER_USE_D3D12 ON)
endif()

set(BUSTER_USE_METAL OFF)
if (APPLE AND NOT BUSTER_USE_VULKAN)
    set(BUSTER_USE_METAL ON)
endif()

set(BUSTER_VULKAN_SDK_DEFAULT "")
if (DEFINED ENV{VULKAN_SDK})
    set(BUSTER_VULKAN_SDK_DEFAULT "$ENV{VULKAN_SDK}")
endif()
set(BUSTER_VULKAN_SDK "${BUSTER_VULKAN_SDK_DEFAULT}" CACHE PATH "Vulkan SDK root directory")
if (NOT BUSTER_VULKAN_SDK AND BUSTER_VULKAN_SDK_DEFAULT)
    set(BUSTER_VULKAN_SDK "${BUSTER_VULKAN_SDK_DEFAULT}" CACHE PATH "Vulkan SDK root directory" FORCE)
endif()

set(BUSTER_VULKAN_SDK_ROOT_DIRS)
set(BUSTER_VULKAN_SDK_BIN_DIRS)
set(BUSTER_VULKAN_SDK_INCLUDE_DIRS)
if (BUSTER_VULKAN_SDK)
    set(BUSTER_VULKAN_SDK_ARCH_DIR_NAMES)
    if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        list(APPEND BUSTER_VULKAN_SDK_ARCH_DIR_NAMES aarch64 arm64)
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        list(APPEND BUSTER_VULKAN_SDK_ARCH_DIR_NAMES x86_64)
    endif()

    list(APPEND BUSTER_VULKAN_SDK_ROOT_DIRS "${BUSTER_VULKAN_SDK}")

    get_filename_component(BUSTER_VULKAN_SDK_NAME "${BUSTER_VULKAN_SDK}" NAME)
    if (BUSTER_VULKAN_SDK_NAME IN_LIST BUSTER_VULKAN_SDK_ARCH_DIR_NAMES)
        get_filename_component(BUSTER_VULKAN_SDK_PARENT "${BUSTER_VULKAN_SDK}" DIRECTORY)
        list(APPEND BUSTER_VULKAN_SDK_ROOT_DIRS "${BUSTER_VULKAN_SDK_PARENT}")
    endif()

    foreach(BUSTER_VULKAN_SDK_ROOT IN LISTS BUSTER_VULKAN_SDK_ROOT_DIRS)
        list(APPEND BUSTER_VULKAN_SDK_BIN_DIRS
            "${BUSTER_VULKAN_SDK_ROOT}/bin"
            "${BUSTER_VULKAN_SDK_ROOT}/Bin"
        )
        list(APPEND BUSTER_VULKAN_SDK_INCLUDE_DIRS
            "${BUSTER_VULKAN_SDK_ROOT}/include"
        )
        foreach(BUSTER_VULKAN_SDK_ARCH_DIR_NAME IN LISTS BUSTER_VULKAN_SDK_ARCH_DIR_NAMES)
            list(APPEND BUSTER_VULKAN_SDK_BIN_DIRS
                "${BUSTER_VULKAN_SDK_ROOT}/${BUSTER_VULKAN_SDK_ARCH_DIR_NAME}/bin"
                "${BUSTER_VULKAN_SDK_ROOT}/${BUSTER_VULKAN_SDK_ARCH_DIR_NAME}/Bin"
            )
            list(APPEND BUSTER_VULKAN_SDK_INCLUDE_DIRS
                "${BUSTER_VULKAN_SDK_ROOT}/${BUSTER_VULKAN_SDK_ARCH_DIR_NAME}/include"
            )
        endforeach()
    endforeach()
endif()

unset(BUSTER_VULKAN_INCLUDE_DIR CACHE)
unset(BUSTER_SLANGC_EXECUTABLE CACHE)
unset(BUSTER_SPIRV_OPT_EXECUTABLE CACHE)
find_path(BUSTER_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS ${BUSTER_VULKAN_SDK_INCLUDE_DIRS} NO_DEFAULT_PATH)
find_program(BUSTER_SLANGC_EXECUTABLE NAMES slangc HINTS ${BUSTER_VULKAN_SDK_BIN_DIRS} NO_DEFAULT_PATH)
find_program(BUSTER_SPIRV_OPT_EXECUTABLE NAMES spirv-opt HINTS ${BUSTER_VULKAN_SDK_BIN_DIRS} NO_DEFAULT_PATH)
option(BUSTER_REQUIRE_VULKAN_SDK "Require Vulkan SDK headers and shader tools" ON)
set(BUSTER_COMPILE_SHADERS_DEFAULT OFF)
if (BUSTER_USE_VULKAN OR BUSTER_USE_D3D12 OR BUSTER_USE_METAL)
    set(BUSTER_COMPILE_SHADERS_DEFAULT ON)
endif()
option(BUSTER_COMPILE_SHADERS "Compile renderer shaders" ${BUSTER_COMPILE_SHADERS_DEFAULT})

set(BUSTER_USE_SLANG_SHADERS_DEFINE OFF)
if (BUSTER_COMPILE_SHADERS)
    set(BUSTER_USE_SLANG_SHADERS_DEFINE ON)
endif()

set(BUSTER_NEEDS_VULKAN_SDK OFF)
if (BUSTER_USE_VULKAN OR BUSTER_COMPILE_SHADERS)
    set(BUSTER_NEEDS_VULKAN_SDK ON)
endif()

if (BUSTER_REQUIRE_VULKAN_SDK AND BUSTER_NEEDS_VULKAN_SDK AND NOT BUSTER_VULKAN_SDK)
    message(FATAL_ERROR "BUSTER_REQUIRE_VULKAN_SDK requires the Vulkan SDK. Set the VULKAN_SDK environment variable or configure with -DBUSTER_VULKAN_SDK=/path/to/vulkan-sdk.")
endif()

# Android gets Vulkan headers from the NDK sysroot (on the default include path),
# so the desktop Vulkan SDK headers are not required there.
if (BUSTER_REQUIRE_VULKAN_SDK AND BUSTER_USE_VULKAN AND NOT BUSTER_VULKAN_INCLUDE_DIR AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    message(FATAL_ERROR "Could not find vulkan/vulkan.h in BUSTER_VULKAN_SDK='${BUSTER_VULKAN_SDK}'.")
endif()

if (BUSTER_COMPILE_SHADERS AND NOT BUSTER_SLANGC_EXECUTABLE)
    message(FATAL_ERROR "BUSTER_COMPILE_SHADERS requires slangc from the Vulkan SDK. Set VULKAN_SDK or -DBUSTER_VULKAN_SDK to a Vulkan SDK that contains slangc.")
endif()
if (BUSTER_USE_VULKAN AND BUSTER_COMPILE_SHADERS AND NOT BUSTER_SPIRV_OPT_EXECUTABLE)
    message(FATAL_ERROR "BUSTER_COMPILE_SHADERS requires spirv-opt from the Vulkan SDK to strip Slang OpSource debug records for Android/SwiftShader compatibility.")
endif()

set(BUSTER_SLANGC_COMMAND "${BUSTER_SLANGC_EXECUTABLE}")
if (BUSTER_SLANGC_EXECUTABLE)
    get_filename_component(BUSTER_SLANGC_BIN_DIR "${BUSTER_SLANGC_EXECUTABLE}" DIRECTORY)
    get_filename_component(BUSTER_SLANGC_ROOT_DIR "${BUSTER_SLANGC_BIN_DIR}" DIRECTORY)
    set(BUSTER_SLANGC_LIB_DIR "${BUSTER_SLANGC_ROOT_DIR}/lib")
    if (EXISTS "${BUSTER_SLANGC_LIB_DIR}")
        if (WIN32)
            if (DEFINED ENV{PATH})
                set(BUSTER_SLANGC_PATH "${BUSTER_SLANGC_LIB_DIR};$ENV{PATH}")
            else()
                set(BUSTER_SLANGC_PATH "${BUSTER_SLANGC_LIB_DIR}")
            endif()
            # Keep the Windows PATH as one cmake -E env argument when stored in a CMake list.
            string(REPLACE ";" "\\;" BUSTER_SLANGC_PATH "${BUSTER_SLANGC_PATH}")
            set(BUSTER_SLANGC_COMMAND "${CMAKE_COMMAND}" -E env "PATH=${BUSTER_SLANGC_PATH}" "${BUSTER_SLANGC_EXECUTABLE}")
        elseif(APPLE)
            if (DEFINED ENV{DYLD_LIBRARY_PATH})
                set(BUSTER_SLANGC_LIBRARY_PATH "${BUSTER_SLANGC_LIB_DIR}:$ENV{DYLD_LIBRARY_PATH}")
            else()
                set(BUSTER_SLANGC_LIBRARY_PATH "${BUSTER_SLANGC_LIB_DIR}")
            endif()
            set(BUSTER_SLANGC_COMMAND "${CMAKE_COMMAND}" -E env "DYLD_LIBRARY_PATH=${BUSTER_SLANGC_LIBRARY_PATH}" "${BUSTER_SLANGC_EXECUTABLE}")
        else()
            if (DEFINED ENV{LD_LIBRARY_PATH})
                set(BUSTER_SLANGC_LIBRARY_PATH "${BUSTER_SLANGC_LIB_DIR}:$ENV{LD_LIBRARY_PATH}")
            else()
                set(BUSTER_SLANGC_LIBRARY_PATH "${BUSTER_SLANGC_LIB_DIR}")
            endif()
            set(BUSTER_SLANGC_COMMAND "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${BUSTER_SLANGC_LIBRARY_PATH}" "${BUSTER_SLANGC_EXECUTABLE}")
        endif()
    endif()
endif()

if (BUSTER_MULTI_CONFIG)
    set(BUSTER_OPTIMIZE_CONDITION "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>")
else()
    set(BUSTER_OPTIMIZE_CONDITION "$<BOOL:${BUSTER_DEFAULT_CONFIG_OPTIMIZED}>")
endif()

set(BUSTER_OPTIMIZE_DEFINE "$<IF:${BUSTER_OPTIMIZE_CONDITION},1,0>")
set(BUSTER_UNITY_BUILD_CONDITION "$<AND:$<BOOL:${BUSTER_UNITY_BUILD}>,${BUSTER_OPTIMIZE_CONDITION},$<NOT:$<BOOL:${BUSTER_SANITIZE}>>,$<OR:$<BOOL:${C_COMPILER_CLANG}>,$<BOOL:${C_COMPILER_APPLE_CLANG}>>,$<NOT:$<BOOL:${C_COMPILER_ZIG}>>>")
set(BUSTER_UNITY_BUILD_DEFINE "$<IF:${BUSTER_UNITY_BUILD_CONDITION},1,0>")
set(BUSTER_NON_UNITY_SOURCE_CONDITION "$<NOT:${BUSTER_UNITY_BUILD_CONDITION}>")

if (NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS OR CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL "")
    set(BUSTER_EXPORT_COMPILE_COMMANDS_DEFAULT ON)
    if (BUSTER_CI)
        set(BUSTER_EXPORT_COMPILE_COMMANDS_DEFAULT OFF)
    endif()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ${BUSTER_EXPORT_COMPILE_COMMANDS_DEFAULT} CACHE BOOL "Enable compile commands" FORCE)
endif()

set(BUSTER_ZIG_DARWIN_ASAN_RUNTIME "")
set(BUSTER_ZIG_DARWIN_UBSAN_RUNTIME "")
set(BUSTER_ZIG_DARWIN_COMPILER_RT_DIR "")
set(BUSTER_ZIG_LINUX_ASAN_RUNTIME "")
set(BUSTER_ZIG_LINUX_COMPILER_RT_DIR "")
set(BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR "")
set(BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_DIR "")
set(BUSTER_CLANG_WINDOWS_DYNAMIC_RUNTIMES "")
if (WIN32 AND C_COMPILER_CLANG AND NOT MINGW AND BUSTER_SANITIZE)
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE BUSTER_CLANG_WINDOWS_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE BUSTER_CLANG_WINDOWS_RESOURCE_RESULT
    )
    if (NOT BUSTER_CLANG_WINDOWS_RESOURCE_RESULT EQUAL 0 OR BUSTER_CLANG_WINDOWS_RESOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "Failed to query clang resource directory from ${CMAKE_C_COMPILER}")
    endif()

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
        set(BUSTER_CLANG_WINDOWS_RUNTIME_ARCH x86_64)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
        set(BUSTER_CLANG_WINDOWS_RUNTIME_ARCH aarch64)
    else()
        message(FATAL_ERROR "Unsupported Windows clang sanitizer runtime architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(BUSTER_CLANG_WINDOWS_RUNTIME_DIR "${BUSTER_CLANG_WINDOWS_RESOURCE_DIR}/lib/windows")
    file(GLOB BUSTER_CLANG_WINDOWS_DYNAMIC_RUNTIMES
        "${BUSTER_CLANG_WINDOWS_RUNTIME_DIR}/clang_rt.*-${BUSTER_CLANG_WINDOWS_RUNTIME_ARCH}.dll"
    )
    if (NOT BUSTER_CLANG_WINDOWS_DYNAMIC_RUNTIMES)
        message(FATAL_ERROR "Could not find Windows clang sanitizer runtime DLLs in ${BUSTER_CLANG_WINDOWS_RUNTIME_DIR}")
    endif()
    message(STATUS "BUSTER_CLANG_WINDOWS_RUNTIME_DIR: ${BUSTER_CLANG_WINDOWS_RUNTIME_DIR}")
endif()
if ((APPLE OR UNIX) AND NOT WIN32 AND C_COMPILER_ZIG AND BUSTER_SANITIZE)
    string(REGEX MATCH "^[0-9]+" BUSTER_ZIG_CLANG_MAJOR "${CMAKE_C_COMPILER_VERSION}")
    message(STATUS "BUSTER_ZIG_SANITIZER_CLANG_MAJOR: ${BUSTER_ZIG_CLANG_MAJOR}")
    find_program(BUSTER_ZIG_SANITIZER_CLANG
        NAMES "clang-${BUSTER_ZIG_CLANG_MAJOR}" clang
        PATHS
            "/opt/homebrew/opt/llvm@${BUSTER_ZIG_CLANG_MAJOR}/bin"
            "/usr/local/opt/llvm@${BUSTER_ZIG_CLANG_MAJOR}/bin"
            "/opt/homebrew/opt/llvm/bin"
            "/usr/local/opt/llvm/bin"
        NO_DEFAULT_PATH
    )
    if (NOT BUSTER_ZIG_SANITIZER_CLANG)
        find_program(BUSTER_ZIG_SANITIZER_CLANG NAMES "clang-${BUSTER_ZIG_CLANG_MAJOR}" clang)
    endif()
    message(STATUS "BUSTER_ZIG_SANITIZER_CLANG: ${BUSTER_ZIG_SANITIZER_CLANG}")
    if (APPLE AND NOT BUSTER_ZIG_SANITIZER_CLANG)
        message(FATAL_ERROR "BUSTER_SANITIZE with zig on macOS requires clang to locate compiler-rt sanitizer runtimes")
    endif()

    if (BUSTER_ZIG_SANITIZER_CLANG)
        execute_process(
            COMMAND "${BUSTER_ZIG_SANITIZER_CLANG}" -print-resource-dir
            OUTPUT_VARIABLE BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_RESULT
        )
        execute_process(
            COMMAND "${BUSTER_ZIG_SANITIZER_CLANG}" -print-runtime-dir
            OUTPUT_VARIABLE BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_RESULT
        )
        message(STATUS "BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_RESULT: ${BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_RESULT}")
        message(STATUS "BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR: ${BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR}")
        message(STATUS "BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_RESULT: ${BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_RESULT}")
        message(STATUS "BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_DIR: ${BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_DIR}")
        if (APPLE AND (NOT BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_RESULT EQUAL 0 OR BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR STREQUAL ""))
            message(FATAL_ERROR "Failed to query clang resource directory from ${BUSTER_ZIG_SANITIZER_CLANG}")
        endif()
    else()
        message(STATUS "BUSTER_ZIG_SANITIZER_CLANG: not found; Linux builds will fall back to -lasan")
    endif()

    if (APPLE)
        set(BUSTER_ZIG_DARWIN_COMPILER_RT_DIR "${BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR}/lib/darwin")
        set(BUSTER_ZIG_DARWIN_ASAN_RUNTIME "${BUSTER_ZIG_DARWIN_COMPILER_RT_DIR}/libclang_rt.asan_osx_dynamic.dylib")
        set(BUSTER_ZIG_DARWIN_UBSAN_RUNTIME "${BUSTER_ZIG_DARWIN_COMPILER_RT_DIR}/libclang_rt.ubsan_osx_dynamic.dylib")
        message(STATUS "BUSTER_ZIG_DARWIN_COMPILER_RT_DIR: ${BUSTER_ZIG_DARWIN_COMPILER_RT_DIR}")
        message(STATUS "BUSTER_ZIG_DARWIN_ASAN_RUNTIME: ${BUSTER_ZIG_DARWIN_ASAN_RUNTIME}")
        message(STATUS "BUSTER_ZIG_DARWIN_UBSAN_RUNTIME: ${BUSTER_ZIG_DARWIN_UBSAN_RUNTIME}")

        foreach(runtime IN ITEMS BUSTER_ZIG_DARWIN_ASAN_RUNTIME BUSTER_ZIG_DARWIN_UBSAN_RUNTIME)
            if (NOT EXISTS "${${runtime}}")
                message(FATAL_ERROR "Could not find macOS sanitizer runtime at ${${runtime}}")
            endif()
        endforeach()
    elseif (UNIX AND BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
            set(BUSTER_ZIG_LINUX_ASAN_ARCH x86_64)
        elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
            set(BUSTER_ZIG_LINUX_ASAN_ARCH aarch64)
        endif()
        message(STATUS "BUSTER_ZIG_LINUX_ASAN_ARCH: ${BUSTER_ZIG_LINUX_ASAN_ARCH}")

        if (BUSTER_ZIG_LINUX_ASAN_ARCH)
            set(BUSTER_ZIG_LINUX_ASAN_RUNTIME_NAMES
                "libclang_rt.asan-${BUSTER_ZIG_LINUX_ASAN_ARCH}.so"
                "libclang_rt.asan.so"
            )

            foreach(runtime_name IN LISTS BUSTER_ZIG_LINUX_ASAN_RUNTIME_NAMES)
                execute_process(
                    COMMAND "${BUSTER_ZIG_SANITIZER_CLANG}" "--print-file-name=${runtime_name}"
                    OUTPUT_VARIABLE BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE_RESULT
                )
                message(STATUS "BUSTER_ZIG_LINUX_ASAN_RUNTIME_LOOKUP '${runtime_name}': ${BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE}")
                if (BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE_RESULT EQUAL 0
                    AND IS_ABSOLUTE "${BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE}"
                    AND EXISTS "${BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE}")
                    set(BUSTER_ZIG_LINUX_ASAN_RUNTIME "${BUSTER_ZIG_LINUX_ASAN_RUNTIME_CANDIDATE}")
                    break()
                endif()
            endforeach()

            if (NOT BUSTER_ZIG_LINUX_ASAN_RUNTIME AND BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_DIR)
                file(GLOB BUSTER_ZIG_LINUX_ASAN_RUNTIME_GLOB_DIRECT
                    "${BUSTER_ZIG_SANITIZER_CLANG_RUNTIME_DIR}/libclang_rt.asan*.so"
                )
                foreach(runtime_path IN LISTS BUSTER_ZIG_LINUX_ASAN_RUNTIME_GLOB_DIRECT)
                    message(STATUS "BUSTER_ZIG_LINUX_ASAN_RUNTIME_GLOB_DIRECT: ${runtime_path}")
                    if (runtime_path MATCHES "/libclang_rt\\.asan(-${BUSTER_ZIG_LINUX_ASAN_ARCH})?\\.so$")
                        set(BUSTER_ZIG_LINUX_ASAN_RUNTIME "${runtime_path}")
                        break()
                    endif()
                endforeach()
            endif()

            if (NOT BUSTER_ZIG_LINUX_ASAN_RUNTIME)
                file(GLOB_RECURSE BUSTER_ZIG_LINUX_ASAN_RUNTIME_GLOB_RECURSIVE
                    "${BUSTER_ZIG_SANITIZER_CLANG_RESOURCE_DIR}/lib/libclang_rt.asan*.so"
                )
                foreach(runtime_path IN LISTS BUSTER_ZIG_LINUX_ASAN_RUNTIME_GLOB_RECURSIVE)
                    message(STATUS "BUSTER_ZIG_LINUX_ASAN_RUNTIME_GLOB_RECURSIVE: ${runtime_path}")
                    if (runtime_path MATCHES "/libclang_rt\\.asan(-${BUSTER_ZIG_LINUX_ASAN_ARCH})?\\.so$")
                        set(BUSTER_ZIG_LINUX_ASAN_RUNTIME "${runtime_path}")
                        break()
                    endif()
                endforeach()
            endif()

            if (BUSTER_ZIG_LINUX_ASAN_RUNTIME)
                get_filename_component(BUSTER_ZIG_LINUX_COMPILER_RT_DIR "${BUSTER_ZIG_LINUX_ASAN_RUNTIME}" DIRECTORY)
                message(STATUS "BUSTER_ZIG_LINUX_COMPILER_RT_DIR: ${BUSTER_ZIG_LINUX_COMPILER_RT_DIR}")
                message(STATUS "BUSTER_ZIG_LINUX_ASAN_RUNTIME: ${BUSTER_ZIG_LINUX_ASAN_RUNTIME}")
            else()
                message(STATUS "BUSTER_ZIG_LINUX_ASAN_RUNTIME: not found; falling back to -lasan")
            endif()
        else()
            message(STATUS "BUSTER_ZIG_LINUX_ASAN_ARCH: unsupported processor '${CMAKE_SYSTEM_PROCESSOR}'; falling back to -lasan")
        endif()
    endif()
endif()

if (APPLE AND C_COMPILER_TCC)
    # CMake's Darwin defaults are clang-style linker-driver flags.
    # TinyCC links through its own driver and rejects these options.
    foreach(flag_variable IN ITEMS
        CMAKE_C_LINK_FLAGS
        CMAKE_EXECUTABLE_CREATE_C_FLAGS
        CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS
        CMAKE_SHARED_MODULE_CREATE_C_FLAGS
    )
        string(REPLACE "-Wl,-search_paths_first" "" ${flag_variable} "${${flag_variable}}")
        string(REPLACE "-Wl,-headerpad_max_install_names" "" ${flag_variable} "${${flag_variable}}")
        string(STRIP "${${flag_variable}}" ${flag_variable})
    endforeach()
endif()

set(C_COMPILER_CLANG_FAMILY OFF)
if (C_COMPILER_CLANG OR C_COMPILER_APPLE_CLANG OR C_COMPILER_TCC)
    set(C_COMPILER_CLANG_FAMILY ON)
endif()

set(C_COMPILER_GNU_FAMILY OFF)
if (C_COMPILER_CLANG_FAMILY OR C_COMPILER_GNU)
    set(C_COMPILER_GNU_FAMILY ON)
endif()

set(BUSTER_HOST_C_RESOURCE_INCLUDE "")
if ((C_COMPILER_CLANG OR C_COMPILER_APPLE_CLANG) AND
    NOT C_COMPILER_ZIG AND NOT C_COMPILER_TCC)
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" ${CMAKE_C_COMPILER_ARG1} -print-resource-dir
        OUTPUT_VARIABLE BUSTER_HOST_C_RESOURCE_DIRECTORY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (BUSTER_HOST_C_RESOURCE_DIRECTORY)
        set(BUSTER_HOST_C_RESOURCE_INCLUDE
            "${BUSTER_HOST_C_RESOURCE_DIRECTORY}/include")
    endif()
else()
    find_program(BUSTER_HOST_CLANG_EXECUTABLE
        NAMES clang)
    if (BUSTER_HOST_CLANG_EXECUTABLE)
        execute_process(
            COMMAND "${BUSTER_HOST_CLANG_EXECUTABLE}" -print-resource-dir
            OUTPUT_VARIABLE BUSTER_HOST_C_RESOURCE_DIRECTORY
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if (BUSTER_HOST_C_RESOURCE_DIRECTORY)
            set(BUSTER_HOST_C_RESOURCE_INCLUDE
                "${BUSTER_HOST_C_RESOURCE_DIRECTORY}/include")
        endif()
    endif()
endif()
if (NOT BUSTER_HOST_C_RESOURCE_INCLUDE AND C_COMPILER_GNU)
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-file-name=include
        OUTPUT_VARIABLE BUSTER_HOST_C_RESOURCE_INCLUDE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if (BUSTER_HOST_C_RESOURCE_INCLUDE)
    string(REPLACE "\\" "/"
        BUSTER_HOST_C_RESOURCE_INCLUDE
        "${BUSTER_HOST_C_RESOURCE_INCLUDE}")
endif()

set(BUSTER_LINUX_ASAN_PRELOAD_RUNTIME "")
set(BUSTER_LINUX_ASAN_PRELOAD_RUNTIME_DIR "")
if (UNIX AND NOT APPLE AND BUSTER_SANITIZE)
    if (C_COMPILER_ZIG AND BUSTER_ZIG_LINUX_ASAN_RUNTIME)
        set(BUSTER_LINUX_ASAN_PRELOAD_RUNTIME "${BUSTER_ZIG_LINUX_ASAN_RUNTIME}")
    elseif (C_COMPILER_GNU_FAMILY)
        set(BUSTER_LINUX_ASAN_RUNTIME_NAMES)
        if (C_COMPILER_CLANG_FAMILY)
            if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
                set(BUSTER_LINUX_ASAN_ARCH x86_64)
            elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
                set(BUSTER_LINUX_ASAN_ARCH aarch64)
            endif()

            if (BUSTER_LINUX_ASAN_ARCH)
                list(APPEND BUSTER_LINUX_ASAN_RUNTIME_NAMES
                    "libclang_rt.asan-${BUSTER_LINUX_ASAN_ARCH}.so"
                    "libclang_rt.asan.so"
                )
            endif()
        endif()
        list(APPEND BUSTER_LINUX_ASAN_RUNTIME_NAMES "libasan.so")

        foreach(runtime_name IN LISTS BUSTER_LINUX_ASAN_RUNTIME_NAMES)
            execute_process(
                COMMAND "${CMAKE_C_COMPILER}" "-print-file-name=${runtime_name}"
                OUTPUT_VARIABLE BUSTER_LINUX_ASAN_RUNTIME_CANDIDATE
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE BUSTER_LINUX_ASAN_RUNTIME_CANDIDATE_RESULT
            )
            if (BUSTER_LINUX_ASAN_RUNTIME_CANDIDATE_RESULT EQUAL 0
                AND IS_ABSOLUTE "${BUSTER_LINUX_ASAN_RUNTIME_CANDIDATE}"
                AND EXISTS "${BUSTER_LINUX_ASAN_RUNTIME_CANDIDATE}")
                get_filename_component(BUSTER_LINUX_ASAN_PRELOAD_RUNTIME "${BUSTER_LINUX_ASAN_RUNTIME_CANDIDATE}" REALPATH)
                get_filename_component(BUSTER_LINUX_ASAN_PRELOAD_RUNTIME_DIR "${BUSTER_LINUX_ASAN_PRELOAD_RUNTIME}" DIRECTORY)
                break()
            endif()
        endforeach()
    endif()

    if (BUSTER_LINUX_ASAN_PRELOAD_RUNTIME)
        if (NOT BUSTER_LINUX_ASAN_PRELOAD_RUNTIME_DIR)
            get_filename_component(BUSTER_LINUX_ASAN_PRELOAD_RUNTIME_DIR "${BUSTER_LINUX_ASAN_PRELOAD_RUNTIME}" DIRECTORY)
        endif()
        message(STATUS "BUSTER_LINUX_ASAN_PRELOAD_RUNTIME: ${BUSTER_LINUX_ASAN_PRELOAD_RUNTIME}")
    else()
        message(STATUS "BUSTER_LINUX_ASAN_PRELOAD_RUNTIME: not found")
    endif()
endif()

set(C_COMPILER_MSVC_FAMILIY OFF)
if (C_COMPILER_MSVC)
    set(C_COMPILER_MSVC_FAMILIY ON)
endif()

function(buster_filter_supported_c_flags out_var)
    set(supported_flags "")
    set(unsupported_flags "")

    foreach(flag IN LISTS ARGN)
        string(MAKE_C_IDENTIFIER "BUSTER_HAS_C_FLAG_${flag}" flag_var)
        set(saved_required_flags "")
        if (DEFINED CMAKE_REQUIRED_FLAGS)
            set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
        endif()

        if (CMAKE_C_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
            string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=unknown-warning-option")
        endif()

        check_c_compiler_flag("${flag}" ${flag_var})
        set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

        if (${flag_var})
            list(APPEND supported_flags "${flag}")
        else()
            list(APPEND unsupported_flags "${flag}")
        endif()
    endforeach()

    if (unsupported_flags)
        message(STATUS "Skipping unsupported optional C compiler flags: ${unsupported_flags}")
    endif()

    set(${out_var} "${supported_flags}" PARENT_SCOPE)
endfunction()

option(BUSTER_SINGLE_THREADED "Enable single-threaded mode" ON)

if (C_COMPILER_MSVC)
    foreach(linker_flags IN ITEMS
        CMAKE_EXE_LINKER_FLAGS_DEBUG
        CMAKE_EXE_LINKER_FLAGS_RELEASE
        CMAKE_EXE_LINKER_FLAGS_MINSIZEREL
        CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
        CMAKE_MODULE_LINKER_FLAGS_DEBUG
        CMAKE_MODULE_LINKER_FLAGS_RELEASE
        CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL
        CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO
        CMAKE_SHARED_LINKER_FLAGS_DEBUG
        CMAKE_SHARED_LINKER_FLAGS_RELEASE
        CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL
        CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO
    )
        string(REPLACE "/INCREMENTAL:NO" "" "${linker_flags}" "${${linker_flags}}")
        string(REPLACE "/INCREMENTAL:YES" "" "${linker_flags}" "${${linker_flags}}")
        string(REPLACE "/INCREMENTAL" "" "${linker_flags}" "${${linker_flags}}")
        string(STRIP "${${linker_flags}}" "${linker_flags}")
        set("${linker_flags}" "${${linker_flags}}" CACHE STRING "CMake linker flags for ${linker_flags}" FORCE)
    endforeach()
endif()

message(STATUS "BUSTER_VULKAN_SDK: ${BUSTER_VULKAN_SDK}")
message(STATUS "BUSTER_SLANGC_EXECUTABLE: ${BUSTER_SLANGC_EXECUTABLE}")
message(STATUS "BUSTER_CI: ${BUSTER_CI}")
message(STATUS "BUSTER_LINK_LIBC: ${BUSTER_LINK_LIBC}")
message(STATUS "BUSTER_OPTIMIZE: configuration-derived")
message(STATUS "BUSTER_UNITY_BUILD: ${BUSTER_UNITY_BUILD}")
message(STATUS "BUSTER_INCLUDE_TESTS: ${BUSTER_INCLUDE_TESTS}")
message(STATUS "BUSTER_SANITIZE: ${BUSTER_SANITIZE}")
message(STATUS "BUSTER_SANITIZER_REDUCED_DEBUG: ${BUSTER_SANITIZER_REDUCED_DEBUG}")
message(STATUS "BUSTER_TIME_TRACE: ${BUSTER_TIME_TRACE}")
message(STATUS "BUSTER_INSTRUMENT: ${BUSTER_INSTRUMENT}")
message(STATUS "BUSTER_FUZZ_AVAILABLE: ${BUSTER_FUZZ_AVAILABLE}")
message(STATUS "BUSTER_LTO: ${BUSTER_LTO}")
message(STATUS "BUSTER_CHECK_OPTIONAL_WARNINGS: ${BUSTER_CHECK_OPTIONAL_WARNINGS}")
message(STATUS "BUSTER_DEVELOPER_TARGETS: ${BUSTER_DEVELOPER_TARGETS}")
message(STATUS "BUSTER_REQUIRE_VULKAN_SDK: ${BUSTER_REQUIRE_VULKAN_SDK}")
message(STATUS "BUSTER_COMPILE_SHADERS: ${BUSTER_COMPILE_SHADERS}")
message(STATUS "BUSTER_USE_SLANG_SHADERS: ${BUSTER_USE_SLANG_SHADERS_DEFINE}")
message(STATUS "BUSTER_SINGLE_THREADED: ${BUSTER_SINGLE_THREADED}")

message(STATUS "CMAKE_HOST_SYSTEM_PROCESSOR: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
message(STATUS "CMAKE_CONFIGURATION_TYPES: ${BUSTER_STATUS_CMAKE_CONFIGURATION_TYPES}")
message(STATUS "CMAKE_DEFAULT_BUILD_TYPE: ${BUSTER_STATUS_CMAKE_DEFAULT_BUILD_TYPE}")
message(STATUS "CMAKE_BUILD_TYPE: ${BUSTER_STATUS_CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "CMAKE_LINKER_TYPE: ${CMAKE_LINKER_TYPE}")
message(STATUS "C_COMPILER_ZIG: ${C_COMPILER_ZIG}")
message(STATUS "BUSTER_USE_VULKAN: ${BUSTER_USE_VULKAN}")
message(STATUS "BUSTER_USE_D3D12: ${BUSTER_USE_D3D12}")
message(STATUS "BUSTER_USE_METAL: ${BUSTER_USE_METAL}")
message(STATUS "CMAKE_COLOR_DIAGNOSTICS: ${CMAKE_COLOR_DIAGNOSTICS}")
message(STATUS "CMAKE_EXPORT_COMPILE_COMMANDS: ${CMAKE_EXPORT_COMPILE_COMMANDS}")

if (BUSTER_DEVELOPER_TARGETS)
    find_program(gdb_executable gdb)
    find_program(lldb_executable lldb)
endif()
set(BUSTER_CLANG_ANALYZE_DRIVER "${CMAKE_SOURCE_DIR}/build/build${CMAKE_EXECUTABLE_SUFFIX}")

if (BUSTER_DEVELOPER_TARGETS)
    message(STATUS "GDB: ${gdb_executable}")
    message(STATUS "LLDB: ${lldb_executable}")
endif()

set(COMMON_EXTENSION ".c")

set(BUSTER_SOURCE_DIR "src/buster/lib")

function(buster_normalize_module_name out_var module)
    string(TOUPPER "${module}" module_key)
    string(REPLACE "-" "_" module_key "${module_key}")

    if ("${module_key}" STREQUAL "WINDOWING")
        set(module_key "WINDOW")
    endif()

    set(${out_var} "${module_key}" PARENT_SCOPE)
endfunction()

macro(buster_register_module module source)
    buster_normalize_module_name(module_key "${module}")
    set(BUSTER_MODULE_${module_key}_SOURCE "${source}")
    set(BUSTER_MODULE_${module_key} "${source}")
    list(APPEND BUSTER_KNOWN_MODULES "${module_key}")
    list(REMOVE_DUPLICATES BUSTER_KNOWN_MODULES)
endmacro()

function(buster_resolve_modules out_sources out_modules)
    set(module_sources "")
    set(module_keys "")

    foreach(module IN LISTS ARGN)
        buster_normalize_module_name(module_key "${module}")
        set(module_source_variable "BUSTER_MODULE_${module_key}_SOURCE")
        if (NOT DEFINED ${module_source_variable})
            message(FATAL_ERROR "Unknown buster module '${module}'. Known modules: ${BUSTER_KNOWN_MODULES}")
        endif()
        list(APPEND module_keys "${module_key}")
    endforeach()

    list(REMOVE_DUPLICATES module_keys)
    foreach(module_key IN LISTS module_keys)
        set(module_source_variable "BUSTER_MODULE_${module_key}_SOURCE")
        list(APPEND module_sources "${${module_source_variable}}")
    endforeach()

    set(${out_sources} "${module_sources}" PARENT_SCOPE)
    set(${out_modules} "${module_keys}" PARENT_SCOPE)
endfunction()

function(buster_apply_module_options name)
    set(modules ${ARGN})
    if ("WINDOW" IN_LIST modules AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${name} PRIVATE xcb)
        target_link_libraries(${name} PRIVATE xcb-imdkit)
        target_link_libraries(${name} PRIVATE xcb-util)
        target_link_libraries(${name} PRIVATE xcb-keysyms)
        target_link_libraries(${name} PRIVATE xcb-xkb)
        target_link_libraries(${name} PRIVATE xkbcommon-x11)
        target_link_libraries(${name} PRIVATE xkbcommon)
    endif()

    if ("WINDOW" IN_LIST modules AND APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(${name} PRIVATE "-framework AppKit")
    endif()

    if ("WINDOW" IN_LIST modules AND WIN32)
        target_link_libraries(${name} PRIVATE dwmapi shell32)

        if (C_COMPILER_TCC)
            target_link_libraries(${name} PRIVATE user32)
        endif()
    endif()

    if ("RENDERING" IN_LIST modules AND BUSTER_USE_METAL)
        target_link_libraries(${name} PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    endif()

    if ("FONT_PROVIDER" IN_LIST modules AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${name} PRIVATE fontconfig)
    endif()
endfunction()

buster_register_module(os "${BUSTER_SOURCE_DIR}/os${COMMON_EXTENSION}")
buster_register_module(entry_point "${BUSTER_SOURCE_DIR}/entry_point${COMMON_EXTENSION}")
buster_register_module(arena "${BUSTER_SOURCE_DIR}/arena${COMMON_EXTENSION}")
buster_register_module(string "${BUSTER_SOURCE_DIR}/string${COMMON_EXTENSION}")
buster_register_module(integer "${BUSTER_SOURCE_DIR}/integer${COMMON_EXTENSION}")
buster_register_module(float "${BUSTER_SOURCE_DIR}/float${COMMON_EXTENSION}")
buster_register_module(file "${BUSTER_SOURCE_DIR}/file${COMMON_EXTENSION}")
buster_register_module(compiler_parser "${BUSTER_SOURCE_DIR}/compiler/frontend/buster/parser${COMMON_EXTENSION}")
buster_register_module(compiler_analysis "${BUSTER_SOURCE_DIR}/compiler/frontend/buster/analysis${COMMON_EXTENSION}")
buster_register_module(compiler_c_frontend "${BUSTER_SOURCE_DIR}/compiler/frontend/c/c${COMMON_EXTENSION}")
buster_register_module(compiler_assembly "${BUSTER_SOURCE_DIR}/compiler/assembly/assembly${COMMON_EXTENSION}")
buster_register_module(compiler_assembly_metadata "${BUSTER_SOURCE_DIR}/compiler/assembly/x86_64_metadata${COMMON_EXTENSION}")
buster_register_module(compiler_ir "${BUSTER_SOURCE_DIR}/compiler/ir/ir${COMMON_EXTENSION}")
buster_register_module(compiler_ir_interpreter "${BUSTER_SOURCE_DIR}/compiler/ir/interpreter${COMMON_EXTENSION}")
buster_register_module(compiler_debug "${BUSTER_SOURCE_DIR}/compiler/debug/debug${COMMON_EXTENSION}")
buster_register_module(compiler_codegen "${BUSTER_SOURCE_DIR}/compiler/codegen/codegen${COMMON_EXTENSION}")
buster_register_module(compiler_dwarf "${BUSTER_SOURCE_DIR}/compiler/dwarf/dwarf${COMMON_EXTENSION}")
buster_register_module(compiler_codeview "${BUSTER_SOURCE_DIR}/compiler/codeview/codeview${COMMON_EXTENSION}")
buster_register_module(compiler_pdb "${BUSTER_SOURCE_DIR}/compiler/pdb/pdb${COMMON_EXTENSION}")
buster_register_module(compiler_object "${BUSTER_SOURCE_DIR}/compiler/object/object${COMMON_EXTENSION}")
buster_register_module(compiler_link "${BUSTER_SOURCE_DIR}/compiler/link/link${COMMON_EXTENSION}")
buster_register_module(compiler_driver "${BUSTER_SOURCE_DIR}/compiler/driver/driver${COMMON_EXTENSION}")
buster_register_module(simd "${BUSTER_SOURCE_DIR}/simd${COMMON_EXTENSION}")
buster_register_module(target "${BUSTER_SOURCE_DIR}/target${COMMON_EXTENSION}")
buster_register_module(x86_64 "${BUSTER_SOURCE_DIR}/x86_64${COMMON_EXTENSION}")
buster_register_module(aarch64 "${BUSTER_SOURCE_DIR}/aarch64${COMMON_EXTENSION}")
buster_register_module(time "${BUSTER_SOURCE_DIR}/time${COMMON_EXTENSION}")
buster_register_module(ui_core "${BUSTER_SOURCE_DIR}/ui_core${COMMON_EXTENSION}")
buster_register_module(ui_builder "${BUSTER_SOURCE_DIR}/ui_builder${COMMON_EXTENSION}")
buster_register_module(window "${BUSTER_SOURCE_DIR}/window${COMMON_EXTENSION}")
buster_register_module(rendering "${BUSTER_SOURCE_DIR}/rendering${COMMON_EXTENSION}")
buster_register_module(truetype "${BUSTER_SOURCE_DIR}/truetype${COMMON_EXTENSION}")
buster_register_module(font_provider "${BUSTER_SOURCE_DIR}/font_provider${COMMON_EXTENSION}")
buster_register_module(hash "${BUSTER_SOURCE_DIR}/hash${COMMON_EXTENSION}")
buster_register_module(ide_document "${BUSTER_SOURCE_DIR}/ide_document${COMMON_EXTENSION}")

# Test sources follow the selected build mode: test.c includes them in unity
# builds, while CMake compiles them as independent translation units otherwise.
set(BUSTER_TEST_SOURCES
    src/buster/tests/arena_test.c
    src/buster/tests/hash_test.c
    src/buster/tests/string_test.c
    src/buster/tests/os_test.c
    src/buster/tests/file_test.c
    src/buster/tests/ide_document_test.c
    src/buster/tests/window_test.c
    src/buster/tests/rendering_test.c
    src/buster/tests/ui_test.c
    src/buster/tests/target_test.c
    src/buster/tests/compiler/frontend/buster/parser_test.c
    src/buster/tests/compiler/frontend/buster/analysis_test.c
    src/buster/tests/compiler/frontend/c/c_test.c
    src/buster/tests/compiler/assembly/assembly_test.c
    src/buster/tests/compiler/assembly/x86_64_metadata_test.c
    src/buster/tests/compiler/ir/ir_test.c
    src/buster/tests/compiler/ir/interpreter_test.c
    src/buster/tests/compiler/codegen/codegen_test.c
    src/buster/tests/compiler/debug/debug_test.c
    src/buster/tests/compiler/dwarf/dwarf_test.c
    src/buster/tests/compiler/codeview/codeview_test.c
    src/buster/tests/compiler/pdb/pdb_test.c
    src/buster/tests/compiler/object/object_test.c
    src/buster/tests/compiler/link/link_test.c
    src/buster/tests/compiler/driver/driver_test.c
)
set(BUSTER_TEST_HEADERS
    src/buster/tests/test.h
    src/buster/tests/arena_test.h
    src/buster/tests/hash_test.h
    src/buster/tests/string_test.h
    src/buster/tests/os_test.h
    src/buster/tests/file_test.h
    src/buster/tests/ide_document_test.h
    src/buster/tests/window_test.h
    src/buster/tests/rendering_test.h
    src/buster/tests/ui_test.h
    src/buster/tests/target_test.h
    src/buster/tests/x86_64_test.h
    src/buster/tests/compiler/frontend/buster/parser_test.h
    src/buster/tests/compiler/frontend/buster/analysis_test.h
    src/buster/tests/compiler/frontend/c/c_test.h
    src/buster/tests/compiler/assembly/assembly_test.h
    src/buster/tests/compiler/assembly/x86_64_metadata_test.h
    src/buster/tests/compiler/ir/ir_test.h
    src/buster/tests/compiler/ir/interpreter_test.h
    src/buster/tests/compiler/codegen/codegen_test.h
    src/buster/tests/compiler/debug/debug_test.h
    src/buster/tests/compiler/dwarf/dwarf_test.h
    src/buster/tests/compiler/codeview/codeview_test.h
    src/buster/tests/compiler/pdb/pdb_test.h
    src/buster/tests/compiler/object/object_test.h
    src/buster/tests/compiler/link/link_test.h
    src/buster/tests/compiler/driver/driver_test.h
)
set_source_files_properties(${BUSTER_TEST_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

set(GNU_FAMILY_OPTIONS
    -fwrapv
    -fno-strict-aliasing
    -funsigned-char
    -fno-exceptions
)

set(GNU_FAMILY_WARNINGS
    -Werror
    -Wall
    -Wextra
    -Wundef
    -Wconversion
    -Wstrict-overflow=5
    -Woverflow
    -Wshift-overflow
    -Walloca
    -Wbool-operation
    -Wdouble-promotion
    -Wformat
    -Wimplicit
    -Wimplicit-fallthrough
    -Winfinite-recursion
    -Winvalid-utf8
    #-Wlarge-by-value-copy
    -Wmain
    -Wmisleading-indentation
    -Wmissing-braces
    -Woverlength-strings
    -Wpacked
    -Wparentheses
    -Wpointer-arith
    -Wpragmas
    -Wshadow
    -Wtautological-compare
    -Wtype-limits
    -Wuninitialized
)

set(CLANG_GNU_FAMILY_WARNINGS
    -Wpedantic
    -pedantic
    -Warray-bounds-pointer-arithmetic
    -Wassign-enum
    -Wbool-conversion
    -Wcomma
    -Wconditional-uninitialized
    -Wdangling
    -Wenum-compare-conditional
    -Wenum-too-large
    -Wfixed-point-overflow
    -Wflag-enum
    -Wfortify-source
    -Wfour-char-constants
    -Wimplicit-fallthrough-per-function
    -Wimplicit-float-conversion
    -Wimplicit-int-conversion
    -Wlinker-warnings
    -Wloop-analysis
    -Wnon-power-of-two-alignment
    -Woption-ignored
    -Wpragma-pack
    -Wpragma-pack-suspicious-include
    -Wpedantic-macros
    -Wread-only-types
    -Wredundant-parens
    -Wreserved-identifier
    -Wreserved-macro-identifier
    -Wreserved-module-identifier
    -Wself-assign
    -Wself-assign-field
    -Wshadow-all
    -Wshadow-field
    -Wshift-sign-overflow
    -Wsigned-enum-bitfield
    -Wtautological-constant-in-range-compare
    -Wunaligned-access
    -Wvector-conversion
)

set(CLANG_GNU_FAMILY_OPTIONAL_WARNING_CANDIDATES
    -Wexperimental-lifetime-safety
    -Whigher-precision-for-complex-division
    -Wimplicit-void-ptr-cast
    -Wshift-bool
    -Wunique-object-duplication
)

if (C_COMPILER_CLANG_FAMILY AND BUSTER_CHECK_OPTIONAL_WARNINGS)
    set(BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_KEY
        "${CMAKE_C_COMPILER_ID}|${CMAKE_C_COMPILER_VERSION}|${CMAKE_C_COMPILER}"
    )

    if (NOT DEFINED BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_CACHE_KEY
        OR NOT BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_CACHE_KEY STREQUAL BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_KEY)
        buster_filter_supported_c_flags(
            BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_COMPUTED
            ${CLANG_GNU_FAMILY_OPTIONAL_WARNING_CANDIDATES}
        )
        set(
            BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS
            "${BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_COMPUTED}"
            CACHE INTERNAL "Supported optional Clang-family warning flags" FORCE
        )
        set(
            BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_CACHE_KEY
            "${BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS_KEY}"
            CACHE INTERNAL "Compiler signature for supported optional Clang-family warning flags" FORCE
        )
    endif()

    list(APPEND CLANG_GNU_FAMILY_WARNINGS ${BUSTER_SUPPORTED_CLANG_GNU_FAMILY_OPTIONAL_WARNINGS})
endif()

set(GNU_FAMILY_DISABLE_WARNINGS
    -Wno-missing-field-initializers
)

set(GNU_FAMILY_UNITY_DISABLE_WARNINGS
    "$<${BUSTER_UNITY_BUILD_CONDITION}:-Wno-unused-function>"
)

set(GNU_FAMILY_NATIVE_TARGET -march=native)
if (WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # Win64 guarantees only 16-byte stack alignment. MinGW GCC 15 can emit
    # 32-byte-aligned AVX spills without realigning the incoming stack.
    list(APPEND GNU_FAMILY_NATIVE_TARGET -mno-avx)
endif()

set(MSVC_FAMILY_OPTIONS )
set(MSVC_FAMILY_WARNINGS )
set(MSVC_FAMILY_DISABLE_WARNINGS )

set(TEST_ARGUMENTS test --verbose=1)
if (BUSTER_CI)
    list(APPEND TEST_ARGUMENTS --ci=1)
endif()
set(UNIT_TEST_ARGUMENTS test --verbose=1)
set(APP_TEST_ARGUMENTS test_app --verbose=1 --ci=1)

set(BUSTER_TEST_ENV)

if (NOT WIN32)
    set(BUSTER_LSAN_OPTIONS "suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:print_suppressions=1")
    if (DEFINED ENV{LSAN_OPTIONS})
        if (NOT "$ENV{LSAN_OPTIONS}" STREQUAL "")
            set(BUSTER_LSAN_OPTIONS "$ENV{LSAN_OPTIONS}:${BUSTER_LSAN_OPTIONS}")
        endif()
    endif()
    list(APPEND BUSTER_TEST_ENV "LSAN_OPTIONS=${BUSTER_LSAN_OPTIONS}")

    if (UNIX AND NOT APPLE)
        list(APPEND BUSTER_TEST_ENV "VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1")
    endif()

    if (BUSTER_SANITIZE)
        string(REGEX MATCH "^[0-9]+" BUSTER_SANITIZER_COMPILER_MAJOR "${CMAKE_C_COMPILER_VERSION}")
        set(BUSTER_LLVM_SYMBOLIZER_NAMES llvm-symbolizer)
        if (BUSTER_SANITIZER_COMPILER_MAJOR)
            set(BUSTER_LLVM_SYMBOLIZER_NAMES "llvm-symbolizer-${BUSTER_SANITIZER_COMPILER_MAJOR}" ${BUSTER_LLVM_SYMBOLIZER_NAMES})
        endif()
        find_program(BUSTER_LLVM_SYMBOLIZER NAMES ${BUSTER_LLVM_SYMBOLIZER_NAMES})

        set(BUSTER_ASAN_OPTIONS "symbolize=1")
        set(BUSTER_UBSAN_OPTIONS "print_stacktrace=1")
        if (BUSTER_LLVM_SYMBOLIZER)
            list(APPEND BUSTER_TEST_ENV "ASAN_SYMBOLIZER_PATH=${BUSTER_LLVM_SYMBOLIZER}")
            set(BUSTER_ASAN_OPTIONS "${BUSTER_ASAN_OPTIONS}:external_symbolizer_path=${BUSTER_LLVM_SYMBOLIZER}")
            set(BUSTER_UBSAN_OPTIONS "${BUSTER_UBSAN_OPTIONS}:external_symbolizer_path=${BUSTER_LLVM_SYMBOLIZER}")
            message(STATUS "BUSTER_LLVM_SYMBOLIZER: ${BUSTER_LLVM_SYMBOLIZER}")
        else()
            message(STATUS "BUSTER_LLVM_SYMBOLIZER: not found")
        endif()

        if (DEFINED ENV{ASAN_OPTIONS})
            if (NOT "$ENV{ASAN_OPTIONS}" STREQUAL "")
                set(BUSTER_ASAN_OPTIONS "$ENV{ASAN_OPTIONS}:${BUSTER_ASAN_OPTIONS}")
            endif()
        endif()
        if (DEFINED ENV{UBSAN_OPTIONS})
            if (NOT "$ENV{UBSAN_OPTIONS}" STREQUAL "")
                set(BUSTER_UBSAN_OPTIONS "$ENV{UBSAN_OPTIONS}:${BUSTER_UBSAN_OPTIONS}")
            endif()
        endif()

        list(APPEND BUSTER_TEST_ENV "ASAN_OPTIONS=${BUSTER_ASAN_OPTIONS}")
        list(APPEND BUSTER_TEST_ENV "UBSAN_OPTIONS=${BUSTER_UBSAN_OPTIONS}")

        if (UNIX AND NOT APPLE AND BUSTER_LINUX_ASAN_PRELOAD_RUNTIME)
            set(BUSTER_LD_PRELOAD "${BUSTER_LINUX_ASAN_PRELOAD_RUNTIME}")
            if (DEFINED ENV{LD_PRELOAD})
                if (NOT "$ENV{LD_PRELOAD}" STREQUAL "")
                    set(BUSTER_LD_PRELOAD "${BUSTER_LD_PRELOAD}:$ENV{LD_PRELOAD}")
                endif()
            endif()
            list(APPEND BUSTER_TEST_ENV "LD_PRELOAD=${BUSTER_LD_PRELOAD}")
        endif()
    endif()
endif()

set(BUSTER_APP_TEST_ENV ${BUSTER_TEST_ENV})
if (NOT WIN32 AND BUSTER_SANITIZE)
    # The Vulkan validation layer loads an unsanitized C++ libLLVM whose
    # allocator pairing is outside buster's control. Keep mismatch detection
    # enabled for unit tests and fuzzing, and disable it only for this external
    # graphical smoke-test process.
    list(FILTER BUSTER_APP_TEST_ENV EXCLUDE REGEX "^ASAN_OPTIONS=")
    list(APPEND BUSTER_APP_TEST_ENV "ASAN_OPTIONS=${BUSTER_ASAN_OPTIONS}:alloc_dealloc_mismatch=0")
endif()

set(BUSTER_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/buster/lib/shaders")
set(BUSTER_SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders")
set(BUSTER_SHADER_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
set(BUSTER_SHADER_GENERATED_HEADER_DIR "${BUSTER_SHADER_GENERATED_INCLUDE_DIR}/buster/lib/shaders")
set(BUSTER_SHADER_GENERATED_PATH_HEADER "${BUSTER_SHADER_GENERATED_HEADER_DIR}/paths.h")
set(BUSTER_RECT_TEXTURE_SLOT_COUNT 2)

function(buster_c_string out_var value)
    string(REPLACE "\\" "\\\\" escaped "${value}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    set(${out_var} "${escaped}" PARENT_SCOPE)
endfunction()

function(buster_compile_slang_spirv out_var shader_source entry_point stage output_name)
    if (IS_ABSOLUTE "${shader_source}")
        set(shader_source_path "${shader_source}")
    else()
        set(shader_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}")
    endif()

    set(emit_spirv_flag -emit-spirv-directly)
    if ("${stage}" STREQUAL "fragment")
        # Slang's direct SPIR-V path currently emits Sampler2D as a depth-unknown image.
        # Route fragments through glslang so sampler2D matches the working GLSL shader.
        set(emit_spirv_flag -emit-spirv-via-glsl)
    endif()

    set(shader_output "${BUSTER_SHADER_BINARY_DIR}/${output_name}")
    set(shader_output_unstripped "${shader_output}.unstripped")
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUSTER_SHADER_BINARY_DIR}"
        COMMAND ${BUSTER_SLANGC_COMMAND}
            "${shader_source_path}"
            -entry "${entry_point}"
            -stage "${stage}"
            -target spirv
            ${emit_spirv_flag}
            -DBUSTER_VULKAN=1
            -DBUSTER_RECT_TEXTURE_SLOT_COUNT=${BUSTER_RECT_TEXTURE_SLOT_COUNT}
            ${BUSTER_SLANG_VULKAN_EXTRA_DEFINES}
            -o "${shader_output_unstripped}"
        # Slang emits OpSource Slang records (source language operand 11). The
        # Android emulator's SwiftShader build rejects that newer debug enum and
        # can take adb offline while creating shader modules, so ship stripped SPIR-V.
        COMMAND "${BUSTER_SPIRV_OPT_EXECUTABLE}" --strip-debug "${shader_output_unstripped}" -o "${shader_output}"
        MAIN_DEPENDENCY "${shader_source_path}"
        DEPENDS "${BUSTER_SHADER_SOURCE_DIR}/rect_shared.h" "${BUSTER_SHADER_SOURCE_DIR}/blur_shared.h"
        COMMENT "Compiling Slang shader ${entry_point} to ${output_name}"
        VERBATIM
    )

    set_source_files_properties("${shader_output}" PROPERTIES GENERATED TRUE)
    set(${out_var} "${shader_output}" PARENT_SCOPE)
endfunction()

function(buster_compile_slang_metal out_var shader_source entry_point stage output_name)
    if (IS_ABSOLUTE "${shader_source}")
        set(shader_source_path "${shader_source}")
    else()
        set(shader_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}")
    endif()

    set(shader_output "${BUSTER_SHADER_BINARY_DIR}/${output_name}")
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUSTER_SHADER_BINARY_DIR}"
        COMMAND ${BUSTER_SLANGC_COMMAND}
            "${shader_source_path}"
            -entry "${entry_point}"
            -stage "${stage}"
            -target metal
            -DBUSTER_METAL=1
            -DBUSTER_RECT_TEXTURE_SLOT_COUNT=${BUSTER_RECT_TEXTURE_SLOT_COUNT}
            -warnings-disable 39001,39029
            -o "${shader_output}"
        MAIN_DEPENDENCY "${shader_source_path}"
        COMMENT "Compiling Slang shader ${entry_point} to ${output_name}"
        VERBATIM
    )

    set_source_files_properties("${shader_output}" PROPERTIES GENERATED TRUE)
    set(${out_var} "${shader_output}" PARENT_SCOPE)
endfunction()

function(buster_compile_slang_hlsl out_var shader_source entry_point stage output_name)
    if (IS_ABSOLUTE "${shader_source}")
        set(shader_source_path "${shader_source}")
    else()
        set(shader_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}")
    endif()

    if ("${stage}" STREQUAL "vertex")
        set(profile vs_5_1)
    elseif("${stage}" STREQUAL "fragment")
        set(profile ps_5_1)
    else()
        message(FATAL_ERROR "Unsupported HLSL shader stage: ${stage}")
    endif()

    set(shader_output "${BUSTER_SHADER_BINARY_DIR}/${output_name}")
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUSTER_SHADER_BINARY_DIR}"
        COMMAND ${BUSTER_SLANGC_COMMAND}
            "${shader_source_path}"
            -entry "${entry_point}"
            -target hlsl
            -profile "${profile}"
            -DBUSTER_D3D12=1
            -DBUSTER_RECT_TEXTURE_SLOT_COUNT=${BUSTER_RECT_TEXTURE_SLOT_COUNT}
            -o "${shader_output}"
        MAIN_DEPENDENCY "${shader_source_path}"
        COMMENT "Compiling Slang shader ${entry_point} to ${output_name}"
        VERBATIM
    )

    set_source_files_properties("${shader_output}" PROPERTIES GENERATED TRUE)
    set(${out_var} "${shader_output}" PARENT_SCOPE)
endfunction()

function(buster_embed_metal_shaders out_var vertex_source fragment_source output_name)
    set(header_output "${BUSTER_SHADER_GENERATED_HEADER_DIR}/${output_name}")
    add_custom_command(
        OUTPUT "${header_output}"
        COMMAND "${CMAKE_COMMAND}"
            -D "BUSTER_METAL_VERTEX_INPUT=${vertex_source}"
            -D "BUSTER_METAL_FRAGMENT_INPUT=${fragment_source}"
            -D "BUSTER_METAL_HEADER_OUTPUT=${header_output}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_metal_shaders.cmake"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_metal_shaders.cmake"
        DEPENDS "${vertex_source}" "${fragment_source}"
        COMMENT "Embedding Slang-generated Metal shaders"
        VERBATIM
    )

    set_source_files_properties("${header_output}" PROPERTIES GENERATED TRUE)
    set(${out_var} "${header_output}" PARENT_SCOPE)
endfunction()

function(buster_embed_d3d12_shaders out_var vertex_source pixel_source output_name)
    set(header_output "${BUSTER_SHADER_GENERATED_HEADER_DIR}/${output_name}")
    add_custom_command(
        OUTPUT "${header_output}"
        COMMAND "${CMAKE_COMMAND}"
            -D "BUSTER_D3D12_VERTEX_INPUT=${vertex_source}"
            -D "BUSTER_D3D12_PIXEL_INPUT=${pixel_source}"
            -D "BUSTER_D3D12_HEADER_OUTPUT=${header_output}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_d3d12_shaders.cmake"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_d3d12_shaders.cmake"
        DEPENDS "${vertex_source}" "${pixel_source}"
        COMMENT "Embedding Slang-generated D3D12 shaders"
        VERBATIM
    )

    set_source_files_properties("${header_output}" PROPERTIES GENERATED TRUE)
    set(${out_var} "${header_output}" PARENT_SCOPE)
endfunction()

set(BUSTER_SHADER_SOURCES
    src/buster/lib/shaders/rect.slang
    src/buster/lib/shaders/blur.slang
)

set(BUSTER_RECT_VERTEX_SHADER_BINARY "${BUSTER_SHADER_BINARY_DIR}/rect.vert.spv")
set(BUSTER_RECT_FRAGMENT_SHADER_BINARY "${BUSTER_SHADER_BINARY_DIR}/rect.frag.spv")
set(BUSTER_RECT_METAL_VERTEX_SHADER_SOURCE "${BUSTER_SHADER_BINARY_DIR}/rect.vert.metal")
set(BUSTER_RECT_METAL_FRAGMENT_SHADER_SOURCE "${BUSTER_SHADER_BINARY_DIR}/rect.frag.metal")
set(BUSTER_RECT_D3D12_VERTEX_SHADER_SOURCE "${BUSTER_SHADER_BINARY_DIR}/rect.vert.hlsl")
set(BUSTER_RECT_D3D12_PIXEL_SHADER_SOURCE "${BUSTER_SHADER_BINARY_DIR}/rect.frag.hlsl")
set(BUSTER_BLUR_VERTEX_SHADER_BINARY "${BUSTER_SHADER_BINARY_DIR}/blur.vert.spv")
set(BUSTER_BLUR_FRAGMENT_SHADER_BINARY "${BUSTER_SHADER_BINARY_DIR}/blur.frag.spv")

set(BUSTER_SHADER_TARGET_DEPENDS)

if (BUSTER_USE_VULKAN AND BUSTER_COMPILE_SHADERS)
    if (NOT BUSTER_SLANGC_EXECUTABLE)
        message(FATAL_ERROR "BUSTER_COMPILE_SHADERS requires slangc. Install Slang or set BUSTER_SLANGC_EXECUTABLE.")
    endif()

    message(STATUS "BUSTER_VULKAN_INCLUDE_DIR: ${BUSTER_VULKAN_INCLUDE_DIR}")

    message(STATUS "BUSTER_SLANGC_EXECUTABLE: ${BUSTER_SLANGC_EXECUTABLE}")
    buster_compile_slang_spirv(BUSTER_RECT_VERTEX_SHADER_BINARY src/buster/lib/shaders/rect.slang vs_main vertex rect.vert.spv)
    buster_compile_slang_spirv(BUSTER_RECT_FRAGMENT_SHADER_BINARY src/buster/lib/shaders/rect.slang ps_main fragment rect.frag.spv)
    buster_compile_slang_spirv(BUSTER_BLUR_VERTEX_SHADER_BINARY src/buster/lib/shaders/blur.slang blur_vs vertex blur.vert.spv)
    buster_compile_slang_spirv(BUSTER_BLUR_FRAGMENT_SHADER_BINARY src/buster/lib/shaders/blur.slang blur_fs fragment blur.frag.spv)

    list(APPEND BUSTER_SHADER_TARGET_DEPENDS
        "${BUSTER_RECT_VERTEX_SHADER_BINARY}"
        "${BUSTER_RECT_FRAGMENT_SHADER_BINARY}"
        "${BUSTER_BLUR_VERTEX_SHADER_BINARY}"
        "${BUSTER_BLUR_FRAGMENT_SHADER_BINARY}"
    )
endif()

if (BUSTER_USE_METAL AND BUSTER_COMPILE_SHADERS)
    buster_compile_slang_metal(BUSTER_RECT_METAL_VERTEX_SHADER_SOURCE src/buster/lib/shaders/rect.slang rect_vs vertex rect.vert.metal)
    buster_compile_slang_metal(BUSTER_RECT_METAL_FRAGMENT_SHADER_SOURCE src/buster/lib/shaders/rect.slang rect_fs fragment rect.frag.metal)
    buster_embed_metal_shaders(BUSTER_RECT_METAL_SHADER_HEADER "${BUSTER_RECT_METAL_VERTEX_SHADER_SOURCE}" "${BUSTER_RECT_METAL_FRAGMENT_SHADER_SOURCE}" metal.h)
    list(APPEND BUSTER_SHADER_TARGET_DEPENDS
        "${BUSTER_RECT_METAL_SHADER_HEADER}"
    )
endif()

if (BUSTER_USE_D3D12 AND BUSTER_COMPILE_SHADERS)
    buster_compile_slang_hlsl(BUSTER_RECT_D3D12_VERTEX_SHADER_SOURCE src/buster/lib/shaders/rect.slang vs_main vertex rect.vert.hlsl)
    buster_compile_slang_hlsl(BUSTER_RECT_D3D12_PIXEL_SHADER_SOURCE src/buster/lib/shaders/rect.slang ps_main fragment rect.frag.hlsl)
    buster_embed_d3d12_shaders(BUSTER_RECT_D3D12_SHADER_HEADER "${BUSTER_RECT_D3D12_VERTEX_SHADER_SOURCE}" "${BUSTER_RECT_D3D12_PIXEL_SHADER_SOURCE}" d3d12.h)
    list(APPEND BUSTER_SHADER_TARGET_DEPENDS
        "${BUSTER_RECT_D3D12_SHADER_HEADER}"
    )
endif()

if (BUSTER_SHADER_TARGET_DEPENDS)
    add_custom_target(buster_shaders DEPENDS ${BUSTER_SHADER_TARGET_DEPENDS})
endif()

buster_c_string(BUSTER_RECT_VERTEX_SHADER_BINARY_C "${BUSTER_RECT_VERTEX_SHADER_BINARY}")
buster_c_string(BUSTER_RECT_FRAGMENT_SHADER_BINARY_C "${BUSTER_RECT_FRAGMENT_SHADER_BINARY}")
buster_c_string(BUSTER_BLUR_VERTEX_SHADER_BINARY_C "${BUSTER_BLUR_VERTEX_SHADER_BINARY}")
buster_c_string(BUSTER_BLUR_FRAGMENT_SHADER_BINARY_C "${BUSTER_BLUR_FRAGMENT_SHADER_BINARY}")
file(MAKE_DIRECTORY "${BUSTER_SHADER_GENERATED_HEADER_DIR}")
file(WRITE "${BUSTER_SHADER_GENERATED_PATH_HEADER}"
"/* Generated by CMake. Do not edit. */\n"
"#ifndef BUSTER_SHADERS_PATHS_H\n"
"#define BUSTER_SHADERS_PATHS_H\n"
"\n"
"#define BUSTER_SHADER_RECT_VERT_SPV \"${BUSTER_RECT_VERTEX_SHADER_BINARY_C}\"\n"
"#define BUSTER_SHADER_RECT_FRAG_SPV \"${BUSTER_RECT_FRAGMENT_SHADER_BINARY_C}\"\n"
"#define BUSTER_SHADER_BLUR_VERT_SPV \"${BUSTER_BLUR_VERTEX_SHADER_BINARY_C}\"\n"
"#define BUSTER_SHADER_BLUR_FRAG_SPV \"${BUSTER_BLUR_FRAGMENT_SHADER_BINARY_C}\"\n"
"\n"
"#endif /* BUSTER_SHADERS_PATHS_H */\n"
)

function(executable_add name is_buster_program main_file)
    set(options)
    set(one_value_args)
    set(multi_value_args MODULES SOURCES)
    cmake_parse_arguments(EXECUTABLE_ADD "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if (EXECUTABLE_ADD_UNPARSED_ARGUMENTS)
        list(APPEND EXECUTABLE_ADD_SOURCES ${EXECUTABLE_ADD_UNPARSED_ARGUMENTS})
    endif()

    buster_resolve_modules(module_sources module_names ${EXECUTABLE_ADD_MODULES})
    set(target_sources ${module_sources} ${EXECUTABLE_ADD_SOURCES})

    if(${is_buster_program} AND "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
        list(APPEND target_sources ${BUSTER_MODULE_X86_64})
    elseif(${is_buster_program} AND "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
        list(APPEND target_sources ${BUSTER_MODULE_AARCH64})
    endif()

    list(REMOVE_DUPLICATES target_sources)

    if (module_names)
        message(STATUS "${name} modules: ${module_names}")
    endif()
    message(STATUS "${name} files: ${main_file} ${target_sources}")

    if (CMAKE_SYSTEM_NAME STREQUAL "Android")
        # NativeActivity loads the app as a shared library (android.app.lib_name).
        add_library(${name} SHARED ${main_file})
    elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # iOS ships the program as a .app bundle (UIApplicationMain entry point).
        add_executable(${name} MACOSX_BUNDLE ${main_file})
    else()
        add_executable(${name} ${main_file})
    endif()
    if (${is_buster_program})
        target_sources(${name} PRIVATE ${BUSTER_TEST_HEADERS})
        set(test_sources ${BUSTER_TEST_SOURCES})
        if ("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
            list(APPEND test_sources src/buster/tests/x86_64_test.c)
        endif()
        foreach(test_source IN LISTS test_sources)
            target_sources(${name} PRIVATE "$<${BUSTER_NON_UNITY_SOURCE_CONDITION}:${CMAKE_CURRENT_SOURCE_DIR}/${test_source}>")
        endforeach()
        if (${BUSTER_INCLUDE_TESTS})
            target_sources(${name} PRIVATE "$<${BUSTER_NON_UNITY_SOURCE_CONDITION}:${CMAKE_CURRENT_SOURCE_DIR}/src/buster/tests/test.c>")
        endif()
    endif()
    foreach(target_source IN LISTS target_sources)
        if (IS_ABSOLUTE "${target_source}")
            set(target_source_path "${target_source}")
        else()
            set(target_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${target_source}")
        endif()
        target_sources(${name} PRIVATE "$<${BUSTER_NON_UNITY_SOURCE_CONDITION}:${target_source_path}>")
    endforeach()

    set_target_properties(${name} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    if(C_COMPILER_MSVC)
        target_link_options(${name} PRIVATE /INCREMENTAL:NO)
    endif()

    target_include_directories(${name} PRIVATE ${BUSTER_SHADER_GENERATED_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src)
    if (BUSTER_USE_VULKAN AND BUSTER_VULKAN_INCLUDE_DIR)
        target_include_directories(${name} PRIVATE ${BUSTER_VULKAN_INCLUDE_DIR})
    endif()

    target_compile_definitions(${name} PRIVATE
        BUSTER_UNITY_BUILD=${BUSTER_UNITY_BUILD_DEFINE}
        BUSTER_OPTIMIZE=${BUSTER_OPTIMIZE_DEFINE}
        BUSTER_INCLUDE_TESTS=$<IF:$<BOOL:${BUSTER_INCLUDE_TESTS}>,1,0>
        BUSTER_SANITIZE=$<IF:$<BOOL:${BUSTER_SANITIZE}>,1,0>
        BUSTER_FUZZ_AVAILABLE=$<IF:$<BOOL:${BUSTER_FUZZ_AVAILABLE}>,1,0>
        BUSTER_LINK_LIBC=$<IF:$<BOOL:${BUSTER_LINK_LIBC}>,1,0>
        BUSTER_SINGLE_THREADED=$<IF:$<BOOL:${BUSTER_SINGLE_THREADED}>,1,0>
        BUSTER_USE_VULKAN=$<IF:$<BOOL:${BUSTER_USE_VULKAN}>,1,0>
        BUSTER_USE_D3D12=$<IF:$<BOOL:${BUSTER_USE_D3D12}>,1,0>
        BUSTER_USE_METAL=$<IF:$<BOOL:${BUSTER_USE_METAL}>,1,0>
        BUSTER_USE_SLANG_SHADERS=$<IF:$<BOOL:${BUSTER_USE_SLANG_SHADERS_DEFINE}>,1,0>
        BUSTER_COMPILER_ZIG=$<IF:$<BOOL:${C_COMPILER_ZIG}>,1,0>
        BUSTER_HOST_C_COMPILER="${CMAKE_C_COMPILER}"
        BUSTER_HOST_C_COMPILER_ARG1="${CMAKE_C_COMPILER_ARG1}"
        BUSTER_HOST_C_RESOURCE_INCLUDE="${BUSTER_HOST_C_RESOURCE_INCLUDE}"
        BUSTER_HOST_C_COMPILER_MSVC=$<IF:$<BOOL:${C_COMPILER_MSVC}>,1,0>
        BUSTER_INSTRUMENT=$<IF:$<BOOL:${BUSTER_INSTRUMENT}>,1,0>
    )

    if (C_COMPILER_GNU_FAMILY)
        target_compile_options(${name} PRIVATE ${GNU_FAMILY_OPTIONS})
        target_compile_options(${name} PRIVATE ${GNU_FAMILY_WARNINGS})
        target_compile_options(${name} PRIVATE ${GNU_FAMILY_DISABLE_WARNINGS})
        target_compile_options(${name} PRIVATE ${GNU_FAMILY_UNITY_DISABLE_WARNINGS})
        if (BUSTER_CI)
            target_compile_options(${name} PRIVATE "$<$<CONFIG:Release>:-O2>")
        endif()
    endif()

    if (C_COMPILER_CLANG_FAMILY)
        target_compile_options(${name} PRIVATE ${CLANG_GNU_FAMILY_WARNINGS})

        if (BUSTER_DEVELOPER_TARGETS)
            target_compile_options(${name} PRIVATE -ferror-limit=1)
        endif()
    endif()

    if (C_COMPILER_MSVC_FAMILIY)
        target_compile_options(${name} PRIVATE ${MSVC_FAMILY_OPTIONS})
        target_compile_options(${name} PRIVATE ${MSVC_FAMILY_WARNINGS})
        target_compile_options(${name} PRIVATE ${MSVC_FAMILY_DISABLE_WARNINGS})
    endif()

    if (NOT ${BUSTER_CROSS_COMPILE})
        if (C_COMPILER_GNU_FAMILY)
            target_compile_options(${name} PRIVATE ${GNU_FAMILY_NATIVE_TARGET})
        endif()
    endif()

    if (${BUSTER_SANITIZE})
        if (C_COMPILER_MSVC)
            set(sanitizers address)
        else()
            set(sanitizers address undefined bounds)
        endif()

        string(REPLACE ";" "," sanitizer_flag "${sanitizers}")
        target_compile_options(${name} PRIVATE "-fsanitize=${sanitizer_flag}")
        target_link_options(${name} PRIVATE "-fsanitize=${sanitizer_flag}")

        if (WIN32)
            # Clang's sanitizer instrumentation substantially enlarges Debug
            # test frames; the PE default 1 MiB stack is insufficient.
            target_link_options(${name} PRIVATE "LINKER:/stack:8388608")
        endif()

        if (UNIX AND NOT APPLE AND C_COMPILER_CLANG AND NOT C_COMPILER_ZIG AND BUSTER_LINUX_ASAN_PRELOAD_RUNTIME)
            target_link_options(${name} PRIVATE -shared-libasan)
            target_link_options(${name} PRIVATE "-Wl,-rpath,${BUSTER_LINUX_ASAN_PRELOAD_RUNTIME_DIR}")
        endif()

        if (NOT WIN32)
            if (BUSTER_SANITIZER_REDUCED_DEBUG)
                target_compile_options(${name} PRIVATE -gline-tables-only)
            else()
                target_compile_options(${name} PRIVATE -g)
            endif()
        elseif (C_COMPILER_MSVC)
            target_compile_options(${name} PRIVATE /Zi)
            target_link_options(${name} PRIVATE /DEBUG)
        endif()

        if (NOT WIN32)
            if (C_COMPILER_ZIG)
                if (APPLE)
                    target_link_libraries(${name} PRIVATE "${BUSTER_ZIG_DARWIN_ASAN_RUNTIME}")
                    target_link_libraries(${name} PRIVATE "${BUSTER_ZIG_DARWIN_UBSAN_RUNTIME}")
                    target_link_options(${name} PRIVATE
                        "-Wl,-rpath,@executable_path"
                        "-Wl,-rpath,${BUSTER_ZIG_DARWIN_COMPILER_RT_DIR}"
                    )
                elseif (UNIX AND BUSTER_ZIG_LINUX_ASAN_RUNTIME)
                    target_link_libraries(${name} PRIVATE "${BUSTER_ZIG_LINUX_ASAN_RUNTIME}")
                    target_link_options(${name} PRIVATE "-Wl,-rpath,${BUSTER_ZIG_LINUX_COMPILER_RT_DIR}")
                elseif (UNIX)
                    target_link_libraries(${name} PRIVATE asan)
                endif()
            endif()
        endif()

        if (NOT C_COMPILER_MSVC)
            target_compile_options(${name} PRIVATE -fsanitize-recover=all)
            target_link_options(${name} PRIVATE -fsanitize-recover=all)
        endif()
    else()
        if (C_COMPILER_ZIG AND NOT WIN32)
            target_compile_options(${name} PRIVATE -fno-sanitize=all)
            target_link_options(${name} PRIVATE -fno-sanitize=all)
        endif()
    endif()

    set(uses_windows_clang_runtime_helpers ${BUSTER_SANITIZE})
    if (${is_buster_program} AND ${BUSTER_FUZZ_AVAILABLE})
        set(uses_windows_clang_runtime_helpers ON)
    endif()

    if (${uses_windows_clang_runtime_helpers} AND WIN32 AND C_COMPILER_CLANG AND NOT MINGW)
        target_sources(${name} PRIVATE src/buster/lib/sanitizer_coe_win.c)
        target_link_libraries(${name} PRIVATE dbghelp onecore synchronization)
        target_link_options(${name} PRIVATE "LINKER:/ignore:4217")
    endif()

    if (BUSTER_CLANG_WINDOWS_DYNAMIC_RUNTIMES)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${BUSTER_CLANG_WINDOWS_DYNAMIC_RUNTIMES} "$<TARGET_FILE_DIR:${name}>"
            VERBATIM
        )
    endif()

    if (${is_buster_program} AND ${BUSTER_FUZZ_AVAILABLE})
        if (WIN32 AND C_COMPILER_CLANG AND NOT MINGW)
            # LLVM's Windows libFuzzer runtime is built against the static release CRT.
            set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
        endif()
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
        target_link_libraries(${name} PRIVATE "${BUSTER_FUZZ_NO_MAIN_RUNTIME}")
    endif()

    if (${BUSTER_TIME_TRACE})
        target_compile_options(${name} PRIVATE -ftime-trace)
        target_compile_options(${name} PRIVATE -ftime-trace-verbose)
    endif()

    if (${is_buster_program})
        if (UNIX)
            target_link_libraries(${name} PRIVATE m)
        endif()
        if (WIN32)
            target_link_libraries(${name} PRIVATE ws2_32)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Android")
            # NativeActivity glue + platform libraries.
            set(BUSTER_ANDROID_GLUE
                "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
            target_sources(${name} PRIVATE "${BUSTER_ANDROID_GLUE}")
            # Third-party glue is not clean under the project's -Werror set.
            set_source_files_properties("${BUSTER_ANDROID_GLUE}" PROPERTIES COMPILE_OPTIONS "-w")
            target_include_directories(${name} PRIVATE
                "${ANDROID_NDK}/sources/android/native_app_glue")
            target_link_libraries(${name} PRIVATE android log)
            # The framework finds ANativeActivity_onCreate via dlsym; keep it.
            target_link_options(${name} PRIVATE -u ANativeActivity_onCreate)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
            # Drive the app through UIKit; Metal/QuartzCore back the render layer.
            target_link_libraries(${name} PRIVATE
                "-framework UIKit"
                "-framework Foundation"
                "-framework CoreGraphics"
                "-framework QuartzCore"
                "-framework Metal")

            set_target_properties(${name} PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/ios/Info-${name}.plist"
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.buster.ide"
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")

            # Stage the bundle Info.plist (kept out of the source tree).
            configure_file(
                "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
                "${CMAKE_BINARY_DIR}/ios/Info-${name}.plist"
                @ONLY)

            # Bundle test data (read at runtime via relative paths) into Resources/tests.
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${CMAKE_SOURCE_DIR}/tests"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${name}>/tests"
                COMMENT "Bundling test data into ${name}.app")

            # Bundle a monospace font if one is available (see BUSTER_IOS_FONT).
            if (BUSTER_IOS_FONT AND EXISTS "${BUSTER_IOS_FONT}")
                add_custom_command(TARGET ${name} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy
                        "${BUSTER_IOS_FONT}"
                        "$<TARGET_BUNDLE_CONTENT_DIR:${name}>/FiraCode-Regular.ttf"
                    COMMENT "Bundling font into ${name}.app")
            else()
                message(STATUS "BUSTER_IOS_FONT not set or missing; ${name}.app will have no bundled font")
            endif()
        endif()
    endif()

    buster_apply_module_options(${name} ${module_names})

    # if (NOT ${BUSTER_CI})
    #     target_compile_options(${name} PRIVATE
    #         -ferror-limit=1
    #     )
    # endif()

    if (BUSTER_DEVELOPER_TARGETS)
        add_custom_target(run_${name}
            COMMAND "$<TARGET_FILE:${name}>"
            DEPENDS ${name}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            USES_TERMINAL
            COMMENT "Running ${name}"
            VERBATIM)

        if (${is_buster_program} AND BUSTER_CI AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
            add_custom_target(test_${name}
                COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:${name}>" ${UNIT_TEST_ARGUMENTS}
                COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_APP_TEST_ENV} "$<TARGET_FILE:${name}>" ${APP_TEST_ARGUMENTS}
                DEPENDS ${name}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                USES_TERMINAL
                COMMENT "Running ${name} tests and graphical smoke test"
                VERBATIM)
        else()
            add_custom_target(test_${name}
                COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:${name}>" ${TEST_ARGUMENTS}
                DEPENDS ${name}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                USES_TERMINAL
                COMMENT "Running ${name}"
                VERBATIM)
        endif()

        if(gdb_executable)
            add_custom_target(debug_${name}
                COMMAND "${gdb_executable}" --args "$<TARGET_FILE:${name}>"
                DEPENDS ${name}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                USES_TERMINAL
                COMMENT "Debugging ${name} with gdb"
                VERBATIM)
        elseif(lldb_executable)
            add_custom_target(debug_${name}
                COMMAND "${lldb_executable}" -- "$<TARGET_FILE:${name}>"
                DEPENDS ${name}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                USES_TERMINAL
                COMMENT "Debugging ${name} with lldb"
                VERBATIM)
        else()
            message(STATUS "No supported debugger found for ${name}_debug")
        endif()
    endif()
endfunction()

function(buster_add_executable name main_file)
    executable_add(${name} ON ${main_file} ${ARGN})
endfunction()

# buster_add_executable(<name> <main_file> MODULES <module>... [SOURCES <source>...])
buster_add_executable(ide src/buster/apps/ide/ide.c
    MODULES
    os
    entry_point
    arena
    integer
    string
    float
    compiler_parser
    compiler_analysis
    compiler_c_frontend
    compiler_assembly
    compiler_assembly_metadata
    compiler_ir
    compiler_ir_interpreter
    compiler_debug
    compiler_codegen
    compiler_dwarf
    compiler_codeview
    compiler_pdb
    compiler_object
    compiler_link
    compiler_driver
    simd
    file
    target
    time
    ui_core
    ui_builder
    window
    rendering
    truetype
    font_provider
    hash
    ide_document
)

if (((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Windows") AND
     CMAKE_SYSTEM_PROCESSOR MATCHES
        "^(x86_64|amd64|X86_64|X86-64|AMD64)$") OR
    (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    add_custom_target(test_self_host
        COMMAND
            "${CMAKE_SOURCE_DIR}/build/build${CMAKE_EXECUTABLE_SUFFIX}"
            test_self_host
            --build-directory "${CMAKE_BINARY_DIR}"
            --config "$<CONFIG>"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
    )
endif()

if (TARGET buster_shaders)
    add_dependencies(ide buster_shaders)
    target_sources(ide PRIVATE ${BUSTER_SHADER_SOURCES})
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # --- Android packaging: bundle libide.so into an installable APK -----------
    find_program(BUSTER_ADB_EXECUTABLE adb)
    find_program(BUSTER_BASH_EXECUTABLE bash)

    if (NOT BUSTER_ANDROID_SDK)
        if (DEFINED ENV{ANDROID_HOME})
            set(BUSTER_ANDROID_SDK "$ENV{ANDROID_HOME}")
        elseif (DEFINED ENV{ANDROID_SDK_ROOT})
            set(BUSTER_ANDROID_SDK "$ENV{ANDROID_SDK_ROOT}")
        endif()
    endif()

    # Pick the newest installed build-tools unless told otherwise.
    if (NOT BUSTER_ANDROID_BUILD_TOOLS_DIR)
        file(GLOB BUSTER_ANDROID_BUILD_TOOLS_CANDIDATES "${BUSTER_ANDROID_SDK}/build-tools/*")
        if (BUSTER_ANDROID_BUILD_TOOLS_CANDIDATES)
            list(SORT BUSTER_ANDROID_BUILD_TOOLS_CANDIDATES)
            list(GET BUSTER_ANDROID_BUILD_TOOLS_CANDIDATES -1 BUSTER_ANDROID_BUILD_TOOLS_DIR)
        endif()
    endif()

    # android.jar matching the configured platform (fallback to newest available).
    string(REGEX MATCH "[0-9]+" BUSTER_ANDROID_API_LEVEL "${ANDROID_PLATFORM}")
    set(BUSTER_ANDROID_JAR "${BUSTER_ANDROID_SDK}/platforms/android-${BUSTER_ANDROID_API_LEVEL}/android.jar")
    if (NOT EXISTS "${BUSTER_ANDROID_JAR}")
        file(GLOB BUSTER_ANDROID_PLATFORM_JARS "${BUSTER_ANDROID_SDK}/platforms/*/android.jar")
        if (BUSTER_ANDROID_PLATFORM_JARS)
            list(SORT BUSTER_ANDROID_PLATFORM_JARS)
            list(GET BUSTER_ANDROID_PLATFORM_JARS -1 BUSTER_ANDROID_JAR)
        endif()
    endif()

    find_program(BUSTER_AAPT2 aapt2 HINTS "${BUSTER_ANDROID_BUILD_TOOLS_DIR}")
    find_program(BUSTER_ZIPALIGN zipalign HINTS "${BUSTER_ANDROID_BUILD_TOOLS_DIR}")
    find_program(BUSTER_APKSIGNER apksigner HINTS "${BUSTER_ANDROID_BUILD_TOOLS_DIR}")
    find_program(BUSTER_JAR jar)
    find_program(BUSTER_KEYTOOL keytool)

    set(BUSTER_ANDROID_PACKAGE "dev.buster.ide")
    set(BUSTER_ANDROID_ACTIVITY "${BUSTER_ANDROID_PACKAGE}/android.app.NativeActivity")
    set(BUSTER_ANDROID_MANIFEST "${CMAKE_SOURCE_DIR}/android/AndroidManifest.xml")
    set(BUSTER_ANDROID_TEST_SCRIPT "${CMAKE_SOURCE_DIR}/android/run_tests.sh")
    set(BUSTER_ANDROID_SIGN_SCRIPT "${CMAKE_SOURCE_DIR}/android/sign_apk_ci.sh")
    set(BUSTER_APK "${CMAKE_BINARY_DIR}/buster.apk")
    set(BUSTER_KEYSTORE "${CMAKE_BINARY_DIR}/debug.keystore")
    set(BUSTER_APK_STAGE "${CMAKE_BINARY_DIR}/apk")
    set(BUSTER_ANDROID_TEST_ARGUMENTS ${TEST_ARGUMENTS})
    list(JOIN BUSTER_ANDROID_TEST_ARGUMENTS " " BUSTER_ANDROID_TEST_ARGUMENT_STRING)

    # Generate a debug keystore once, at configure time.
    if (BUSTER_KEYTOOL AND NOT EXISTS "${BUSTER_KEYSTORE}")
        execute_process(COMMAND "${BUSTER_KEYTOOL}" -genkeypair
            -keystore "${BUSTER_KEYSTORE}" -storepass android -keypass android
            -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000
            -dname "CN=Android Debug,O=Android,C=US"
            RESULT_VARIABLE BUSTER_KEYSTORE_RESULT)
    endif()

    message(STATUS "Android APK tools: aapt2=${BUSTER_AAPT2} zipalign=${BUSTER_ZIPALIGN} apksigner=${BUSTER_APKSIGNER} jar=${BUSTER_JAR}")
    message(STATUS "Android APK jar: ${BUSTER_ANDROID_JAR}")

    add_custom_command(
        OUTPUT "${BUSTER_APK}"
        # 0. Stage assets read at runtime via AAssetManager: tests + compiled shaders.
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/assets"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_BINARY_DIR}/assets/tests"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/assets/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_BINARY_DIR}/shaders/rect.vert.spv" "${CMAKE_BINARY_DIR}/shaders/rect.frag.spv" "${CMAKE_BINARY_DIR}/shaders/blur.vert.spv" "${CMAKE_BINARY_DIR}/shaders/blur.frag.spv" "${CMAKE_BINARY_DIR}/assets/shaders/"
        # 1. Compile the manifest + assets into a base APK (binary manifest + resources.arsc).
        COMMAND "${BUSTER_AAPT2}" link
            -I "${BUSTER_ANDROID_JAR}"
            --manifest "${BUSTER_ANDROID_MANIFEST}"
            -A "${CMAKE_BINARY_DIR}/assets"
            --min-sdk-version 24 --target-sdk-version 35
            -o "${CMAKE_BINARY_DIR}/buster-base.apk"
        # 2. Stage the native library under lib/<abi>/.
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${BUSTER_APK_STAGE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUSTER_APK_STAGE}/lib/${ANDROID_ABI}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:ide>" "${BUSTER_APK_STAGE}/lib/${ANDROID_ABI}/libide.so"
        # 3. Add the native library into the APK.
        COMMAND "${BUSTER_JAR}" uf "${CMAKE_BINARY_DIR}/buster-base.apk" -C "${BUSTER_APK_STAGE}" "lib/${ANDROID_ABI}/libide.so"
        # 4. Align before signing, then sign with APK Signature Scheme v2/v3.
        COMMAND "${BUSTER_BASH_EXECUTABLE}" "${BUSTER_ANDROID_SIGN_SCRIPT}"
            "${BUSTER_APKSIGNER}"
            "${BUSTER_ZIPALIGN}"
            "${BUSTER_KEYSTORE}"
            "${CMAKE_BINARY_DIR}/buster-base.apk"
            "${BUSTER_APK}"
        DEPENDS ide "${BUSTER_ANDROID_MANIFEST}" "${BUSTER_ANDROID_SIGN_SCRIPT}"
        COMMENT "Packaging buster.apk"
        VERBATIM
    )
    add_custom_target(apk DEPENDS "${BUSTER_APK}")

    if (BUSTER_ADB_EXECUTABLE AND BUSTER_BASH_EXECUTABLE)
        # Install, launch, and wait for the on-device full GUI test result.
        add_custom_target(test_all
            COMMAND "${BUSTER_BASH_EXECUTABLE}" "${BUSTER_ANDROID_TEST_SCRIPT}"
                "${BUSTER_ADB_EXECUTABLE}"
                "${BUSTER_APK}"
                "${BUSTER_ANDROID_PACKAGE}"
                "${BUSTER_ANDROID_ACTIVITY}"
                "${BUSTER_ANDROID_TEST_ARGUMENT_STRING}"
            DEPENDS apk
            COMMENT "Install and test buster.apk on device"
            VERBATIM
        )
    else()
        add_custom_target(test_all
            COMMAND "${CMAKE_COMMAND}" -E echo "adb/bash not found; built ${BUSTER_APK} but cannot run Android tests"
            COMMAND "${CMAKE_COMMAND}" -E false
            DEPENDS apk
            COMMENT "Build buster.apk (Android test runner unavailable)"
            VERBATIM
        )
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Boot a simulator, install the .app, launch it with the console attached and
    # parse the in-app test result from stdout (mirrors the Android device flow).
    add_custom_target(test_all
        COMMAND "${CMAKE_COMMAND}" -E env
            "BUSTER_IOS_APP_BUNDLE=$<TARGET_BUNDLE_DIR:ide>"
            bash "${CMAKE_SOURCE_DIR}/ios/launch_simulator.sh"
        DEPENDS ide
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Install and run buster on the iOS simulator"
        VERBATIM
    )
else()
    add_custom_target(test_units
        COMMAND
        ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV}
        "$<TARGET_FILE:ide>" ${UNIT_TEST_ARGUMENTS}
        DEPENDS ide
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Run unit tests"
        VERBATIM
    )
    if (BUSTER_CI)
        add_custom_target(test_all
            COMMAND
            ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV}
            "$<TARGET_FILE:ide>" ${UNIT_TEST_ARGUMENTS}
            COMMAND
            ${CMAKE_COMMAND} -E env ${BUSTER_APP_TEST_ENV}
            "$<TARGET_FILE:ide>" ${APP_TEST_ARGUMENTS}
            DEPENDS ide
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Test all and run graphical smoke test"
            VERBATIM
        )
    else()
        add_custom_target(test_all
            COMMAND
            ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV}
            "$<TARGET_FILE:ide>" ${TEST_ARGUMENTS}
            DEPENDS ide
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Test all"
            VERBATIM
        )
    endif()

    # Desktop-only performance benchmark. Not wired up for Android/iOS:
    # emulator/simulator numbers are too noisy for useful local comparisons.
    add_custom_target(bench_all
        COMMAND
        ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV}
        "$<TARGET_FILE:ide>" bench
        DEPENDS ide
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Bench all"
        VERBATIM
    )
endif()

if ((C_COMPILER_CLANG OR C_COMPILER_APPLE_CLANG) AND CMAKE_EXPORT_COMPILE_COMMANDS)
    if (EXISTS "${BUSTER_CLANG_ANALYZE_DRIVER}")
        add_custom_target(clang_analyze
            COMMAND "${BUSTER_CLANG_ANALYZE_DRIVER}"
                clang_analyze
                "${CMAKE_BINARY_DIR}/compile_commands.json"
                --config "$<CONFIG>"
                --quiet
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            USES_TERMINAL
            COMMENT "Running clang --analyze"
            VERBATIM
        )
    else()
        message(STATUS "clang_analyze target disabled: ${BUSTER_CLANG_ANALYZE_DRIVER} not found")
    endif()
elseif (C_COMPILER_CLANG OR C_COMPILER_APPLE_CLANG)
    message(STATUS "clang_analyze target disabled: CMAKE_EXPORT_COMPILE_COMMANDS is OFF")
endif()
