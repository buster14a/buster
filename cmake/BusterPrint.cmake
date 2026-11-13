# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Prints the build's description without configuring a build tree, for build.sh's print commands:
# cmake -DBUSTER_PRINT=WHAT -DBUSTER_PRINT_OUTPUT=FILE -DBUSTER_C_COMPILER=CLANG [-DBUSTER_PRINT_CONFIG=Debug|Release|Develop]
# [-DBUSTER_<SETTING>=VALUE...] -P BusterPrint.cmake
#
# Writes WHAT's arguments to FILE, each on a line of its own (none holds a newline: BusterSettings.cmake rejects
# them), which build.sh turns into NUL-delimited argv. WHAT is
#   sources          the sources, in the order the executable links them
#   compile-flags    a unit's compile arguments but -c, its source and its output, in the configuration's order
#                    (Release by default): debug information, the flags, optimization, BUSTER_BUILD_DEFINES; with
#                    -Isrc relative to the source directory, as a command run from there takes them
#   sanitizer-flags  a sanitizer build's optimization, debug information, sanitizer flags and link mode, and nothing
#                    for an uninstrumented build
#   assembly-flags   a lexer variant's assembly's arguments (build.sh lexer-variant-assembly): -g, the flags and the
#                    configuration's optimization
#   variant          the name of the build tree for these settings (build/cmake/NAME)
#   host-flags       the host target's flags: -march=BUSTER_HOST_ARCH, or the explicit vector target's (`vector`)
# The settings are build.sh's environment variables' values, an unset or empty one its default.

cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/BusterSettings.cmake")

if(NOT DEFINED BUSTER_PRINT_OUTPUT)
    message(FATAL_ERROR "BusterPrint.cmake: BUSTER_PRINT_OUTPUT is required")
endif()
if(NOT DEFINED BUSTER_PRINT_CONFIG)
    set(BUSTER_PRINT_CONFIG Release)
endif()
if(NOT BUSTER_PRINT_CONFIG MATCHES "^(Debug|Release|Develop)$")
    message(FATAL_ERROR "buster: the configuration must be Debug, Release or Develop")
endif()

buster_settings_normalize()
set(arguments "")
if(BUSTER_PRINT STREQUAL "variant")
    set(arguments "${buster_variant}")
elseif(BUSTER_PRINT STREQUAL "host-flags")
    set(arguments ${buster_host_flags})
elseif(BUSTER_PRINT STREQUAL "sources")
    set(arguments ${BUSTER_SOURCES})
elseif(BUSTER_PRINT MATCHES "^(compile|sanitizer|assembly)-flags$")
    if(NOT DEFINED BUSTER_C_COMPILER)
        message(FATAL_ERROR "BusterPrint.cmake: BUSTER_C_COMPILER is required")
    endif()
    buster_settings_tools("${BUSTER_C_COMPILER}")
    # The linker decides the static link, and with it the units' BUSTER_HOST_STATIC and the link's -static.
    buster_settings_linker()
    if(BUSTER_PRINT_CONFIG STREQUAL "Debug")
        set(buster_debug ${BUSTER_DEBUG_CONFIGURATION_DEBUG})
        set(buster_optimization ${BUSTER_DEBUG_CONFIGURATION_OPTIMIZATION})
    elseif(BUSTER_PRINT_CONFIG STREQUAL "Develop")
        set(buster_debug ${BUSTER_DEVELOP_CONFIGURATION_DEBUG})
    endif()
    if(BUSTER_PRINT STREQUAL "compile-flags")
        set(arguments ${buster_debug} ${buster_flags} ${buster_optimization} ${buster_defines})
    elseif(BUSTER_PRINT STREQUAL "assembly-flags")
        set(arguments -g ${buster_flags} ${buster_optimization})
    elseif(buster_sanitizer_flags)
        set(arguments ${buster_optimization} ${buster_debug} ${buster_sanitizer_flags} ${buster_link_mode})
    endif()
else()
    message(FATAL_ERROR "BusterPrint.cmake: BUSTER_PRINT must be variant, host-flags, sources, compile-flags, sanitizer-flags or assembly-flags")
endif()

set(text "")
foreach(argument IN LISTS arguments)
    string(APPEND text "${argument}\n")
endforeach()
file(WRITE "${BUSTER_PRINT_OUTPUT}" "${text}")
