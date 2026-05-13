cmake_minimum_required(VERSION 3.10)
project(buster C CXX ASM)

set (CMAKE_COLOR_DIAGNOSTICS ON)

option(BUSTER_CI "Enable CI mode" OFF)
option(BUSTER_LINK_LIBC "Link libc" ON)
option(BUSTER_OPTIMIZE "Enable optimized build" OFF)
option(BUSTER_UNITY_BUILD "Enable unity build" ${BUSTER_OPTIMIZE})
option(BUSTER_INCLUDE_TESTS "Enable tests" ON)
option(BUSTER_SANITIZE "Enable sanitizers" OFF)
option(BUSTER_TIME_TRACE "Enable time tracing" OFF)
option(BUSTER_FUZZ "Enable fuzzing" OFF)
option(BUSTER_LTO "Enable LTO" OFF)

if (NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS OR CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL "")
    set(BUSTER_EXPORT_COMPILE_COMMANDS_DEFAULT ON)
    if (BUSTER_CI)
        set(BUSTER_EXPORT_COMPILE_COMMANDS_DEFAULT OFF)
    endif()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ${BUSTER_EXPORT_COMPILE_COMMANDS_DEFAULT} CACHE BOOL "Enable compile commands" FORCE)
endif()

if (NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    if (BUSTER_OPTIMIZE)
        set(CMAKE_BUILD_TYPE Release)
    else()
        set(CMAKE_BUILD_TYPE Debug)
    endif()
endif()

set(BUSTER_CROSS_COMPILE OFF)

message(STATUS "BUSTER_CI: ${BUSTER_CI}")
message(STATUS "BUSTER_LINK_LIBC: ${BUSTER_LINK_LIBC}")
message(STATUS "BUSTER_OPTIMIZE: ${BUSTER_OPTIMIZE}")
message(STATUS "BUSTER_UNITY_BUILD: ${BUSTER_UNITY_BUILD}")
message(STATUS "BUSTER_INCLUDE_TESTS: ${BUSTER_INCLUDE_TESTS}")
message(STATUS "BUSTER_SANITIZE: ${BUSTER_SANITIZE}")
message(STATUS "BUSTER_TIME_TRACE: ${BUSTER_TIME_TRACE}")
message(STATUS "BUSTER_FUZZ: ${BUSTER_FUZZ}")
message(STATUS "BUSTER_LTO: ${BUSTER_LTO}")

message(STATUS "CMAKE_HOST_SYSTEM_PROCESSOR: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_ASM_COMPILER_ID: ${CMAKE_ASM_COMPILER_ID}")
message(STATUS "CMAKE_COLOR_DIAGNOSTICS: ${CMAKE_COLOR_DIAGNOSTICS}")
message(STATUS "CMAKE_EXPORT_COMPILE_COMMANDS: ${CMAKE_EXPORT_COMPILE_COMMANDS}")

find_program(gdb_executable gdb)
find_program(lldb_executable lldb)

message(STATUS "GDB: ${gdb_executable}")
message(STATUS "LLDB: ${lldb_executable}")

set(COMMON_EXTENSION ".c")

set(BUSTER_SOURCE_DIR "src/buster")

set(BUSTER_MODULE_OS "${BUSTER_SOURCE_DIR}/os${COMMON_EXTENSION}")
set(BUSTER_MODULE_ENTRY_POINT "${BUSTER_SOURCE_DIR}/entry_point${COMMON_EXTENSION}")
set(BUSTER_MODULE_ARENA "${BUSTER_SOURCE_DIR}/arena${COMMON_EXTENSION}")
set(BUSTER_MODULE_MEMORY "${BUSTER_SOURCE_DIR}/memory${COMMON_EXTENSION}")
set(BUSTER_MODULE_STRING "${BUSTER_SOURCE_DIR}/string${COMMON_EXTENSION}")
set(BUSTER_MODULE_INTEGER "${BUSTER_SOURCE_DIR}/integer${COMMON_EXTENSION}")
set(BUSTER_MODULE_FLOAT "${BUSTER_SOURCE_DIR}/float${COMMON_EXTENSION}")
set(BUSTER_MODULE_FILE "${BUSTER_SOURCE_DIR}/file${COMMON_EXTENSION}")
set(BUSTER_MODULE_ASSERTION "${BUSTER_SOURCE_DIR}/assertion${COMMON_EXTENSION}")
set(BUSTER_MODULE_COMPILER_PARSER "${BUSTER_SOURCE_DIR}/compiler/frontend/buster/parser${COMMON_EXTENSION}")
set(BUSTER_MODULE_COMPILER_ANALYSIS "${BUSTER_SOURCE_DIR}/compiler/frontend/buster/analysis${COMMON_EXTENSION}")
set(BUSTER_MODULE_COMPILER_IR "${BUSTER_SOURCE_DIR}/compiler/ir/ir${COMMON_EXTENSION}")
set(BUSTER_MODULE_SIMD "${BUSTER_SOURCE_DIR}/simd${COMMON_EXTENSION}")
set(BUSTER_MODULE_TARGET "${BUSTER_SOURCE_DIR}/target${COMMON_EXTENSION}")
set(BUSTER_MODULE_X86_64 "${BUSTER_SOURCE_DIR}/x86_64${COMMON_EXTENSION}")
set(BUSTER_MODULE_TEST "${BUSTER_SOURCE_DIR}/test${COMMON_EXTENSION}")
set(BUSTER_MODULE_TIME "${BUSTER_SOURCE_DIR}/time${COMMON_EXTENSION}")
set(BUSTER_MODULE_UI_CORE "${BUSTER_SOURCE_DIR}/ui_core${COMMON_EXTENSION}")
set(BUSTER_MODULE_UI_BUILDER "${BUSTER_SOURCE_DIR}/ui_builder${COMMON_EXTENSION}")
set(BUSTER_MODULE_WINDOW "${BUSTER_SOURCE_DIR}/window${COMMON_EXTENSION}")
set(BUSTER_MODULE_RENDERING "${BUSTER_SOURCE_DIR}/rendering${COMMON_EXTENSION}")
set(BUSTER_MODULE_FONT_PROVIDER "${BUSTER_SOURCE_DIR}/font_provider${COMMON_EXTENSION}")

set(C_COMPILER_GNU ${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
set(C_COMPILER_APPLE_CLANG ${CMAKE_C_COMPILER_ID} STREQUAL "AppleClang")
set(C_COMPILER_CLANG ${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
set(C_COMPILER_TCC ${CMAKE_C_COMPILER_ID} STREQUAL "TinyCC")
set(C_COMPILER_CLANG_FAMILY ${C_COMPILER_CLANG} OR ${C_COMPILER_APPLE_CLANG} OR ${C_COMPILER_TCC})
set(C_COMPILER_GNU_FAMILY ${C_COMPILER_CLANG_FAMILY} OR ${C_COMPILER_GNU})

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
    -Wexperimental-lifetime-safety
    -Wfixed-point-overflow
    -Wflag-enum
    -Wfortify-source
    -Wfour-char-constants
    -Whigher-precision-for-complex-division
    -Wimplicit-fallthrough-per-function
    -Wimplicit-float-conversion
    -Wimplicit-int-conversion
    -Wimplicit-void-ptr-cast
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
    -Wshift-bool
    -Wshift-sign-overflow
    -Wsigned-enum-bitfield
    -Wtautological-constant-in-range-compare
    -Wthread-safety
    -Wunaligned-access
    -Wvector-conversion
    -Wunique-object-duplication
)

if (BUSTER_FUZZ)
    set(TEST_ARGUMENTS -max_len=4096;-max_total_time=2)
else()
    set(TEST_ARGUMENTS test)
endif()

function(executable_add name is_buster_program main_file)
    set(target_sources ${ARGN})

    if (${is_buster_program} AND ${BUSTER_INCLUDE_TESTS})
        list(APPEND target_sources ${BUSTER_MODULE_TEST})
    endif()

    if(${is_buster_program} AND ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^(x86_64|amd64)$")
        list(APPEND target_sources ${BUSTER_MODULE_X86_64})
    endif()

    message(STATUS "${name} files: ${main_file} ${target_sources}")

    if(${BUSTER_UNITY_BUILD})
        add_executable(${name} ${main_file})
    else()
        add_executable(${name} ${main_file} ${target_sources})
    endif()

    set_target_properties(${name} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${name} PRIVATE
        BUSTER_UNITY_BUILD=$<IF:$<BOOL:${BUSTER_UNITY_BUILD}>,1,0>
        BUSTER_OPTIMIZE=$<IF:$<BOOL:${BUSTER_OPTIMIZE}>,1,0>
        BUSTER_INCLUDE_TESTS=$<IF:$<BOOL:${BUSTER_INCLUDE_TESTS}>,1,0>
        BUSTER_FUZZ=$<IF:$<BOOL:${BUSTER_FUZZ}>,1,0>
        BUSTER_LINK_LIBC=$<IF:$<BOOL:${BUSTER_LINK_LIBC}>,1,0>
    )

    target_compile_options(${name} PRIVATE
        -fwrapv
        -fno-strict-aliasing
        -funsigned-char
        -fno-exceptions
    )

    if (${C_COMPILER_GNU_FAMILY})
        target_compile_options(${name} PRIVATE ${GNU_FAMILY_WARNINGS})
    endif()

    if (${C_COMPILER_CLANG_FAMILY})
        target_compile_options(${name} PRIVATE ${CLANG_GNU_FAMILY_WARNINGS})
    endif()

    # Disable warnings
    target_compile_options(${name} PRIVATE
        -Wno-pragma-once-outside-header
        -Wno-unused-function
        -Wno-missing-field-initializers
    )

    if (NOT ${BUSTER_CROSS_COMPILE})
        target_compile_options(${name} PRIVATE
            -march=native
        )
    endif()

    if (${BUSTER_SANITIZE})
        if (NOT WIN32)
            target_compile_options(${name} PRIVATE -fsanitize=address)
            target_link_options(${name} PRIVATE "-fsanitize=address")
        endif()

        target_compile_options(${name} PRIVATE "-fsanitize=undefined")
        target_link_options(${name} PRIVATE "-fsanitize=undefined")

        target_compile_options(${name} PRIVATE "-fsanitize=bounds")
        target_link_options(${name} PRIVATE "-fsanitize=bounds")

        target_compile_options(${name} PRIVATE "-fsanitize-recover=all")
        target_link_options(${name} PRIVATE "-fsanitize-recover=all")
    endif()

    if (${is_buster_program} AND ${BUSTER_FUZZ})
        target_compile_options(${name} PRIVATE "-fsanitize=fuzzer")
        target_link_options(${name} PRIVATE "-fsanitize=fuzzer")
    endif()

    if (${BUSTER_TIME_TRACE})
        target_compile_options(${name} PRIVATE "-ftime-trace")
        target_compile_options(${name} PRIVATE "-ftime-trace-verbose")
    endif()

    if (${is_buster_program})
        target_link_libraries(${name} PRIVATE m)
        target_link_libraries(${name} PRIVATE xcb)
        target_link_libraries(${name} PRIVATE fontconfig)
    endif()

    # if (NOT ${BUSTER_CI})
    #     target_compile_options(${name} PRIVATE
    #         -ferror-limit=1
    #     )
    # endif()

    if(NOT ${name} STREQUAL "metaprogram")
        message(STATUS "Adding metaprogram dependency to ${name}")
        add_dependencies(${name} metaprogram_generate)
    endif()

    add_custom_target(run_${name}
        COMMAND "$<TARGET_FILE:${name}>"
        DEPENDS ${name}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Running ${name}"
        VERBATIM)

    add_custom_target(test_${name}
        COMMAND "$<TARGET_FILE:${name}>" ${TEST_ARGUMENTS}
        DEPENDS ${name}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Running ${name}"
        VERBATIM)

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
endfunction()

executable_add(metaprogram OFF src/buster/metaprogram.c)

set(METAPROGRAM_STAMP "${CMAKE_CURRENT_BINARY_DIR}/metaprogram.stamp")

add_custom_command(
    OUTPUT "${METAPROGRAM_STAMP}"
    COMMAND "$<TARGET_FILE:metaprogram>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${METAPROGRAM_STAMP}"
    DEPENDS metaprogram
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running metaprogram"
    VERBATIM)

add_custom_target(metaprogram_generate DEPENDS "${METAPROGRAM_STAMP}")

function(buster_add_executable name main_file)
    executable_add(${name} ON ${main_file} ${ARGN})
endfunction()

buster_add_executable(ide src/buster/ide/ide.c
    ${BUSTER_MODULE_OS}
    ${BUSTER_MODULE_ENTRY_POINT}
    ${BUSTER_MODULE_ARENA}
    ${BUSTER_MODULE_INTEGER}
    ${BUSTER_MODULE_ASSERTION}
    ${BUSTER_MODULE_MEMORY}
    ${BUSTER_MODULE_STRING}
    ${BUSTER_MODULE_FLOAT}
    ${BUSTER_MODULE_COMPILER_PARSER}
    ${BUSTER_MODULE_COMPILER_ANALYSIS}
    ${BUSTER_MODULE_COMPILER_IR}
    ${BUSTER_MODULE_SIMD}
    ${BUSTER_MODULE_FILE}
    ${BUSTER_MODULE_TARGET}
    ${BUSTER_MODULE_TIME}
    ${BUSTER_MODULE_UI_CORE}
    ${BUSTER_MODULE_UI_BUILDER}
    ${BUSTER_MODULE_WINDOW}
    ${BUSTER_MODULE_RENDERING}
    ${BUSTER_MODULE_FONT_PROVIDER}
)
#buster_add_executable(compiler src/buster/compiler/frontend/buster/main.c)

add_custom_target(test_all
    COMMAND $<TARGET_FILE:ide> ${TEST_ARGUMENTS}
    DEPENDS ide
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Test all"
    VERBATIM
)
