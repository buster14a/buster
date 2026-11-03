# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The no-recursion rule on the compiler's call graph: cmake -DCOMPILER=EXECUTABLE -DIR=DIRECTORY
# -P BusterCallGraph.cmake
#
# DIRECTORY holds the call-graph-ir target's captures (CMakeLists.txt): each translation unit's LLVM IR, NAME.ll, and
# capture/inline.ll and capture/strong.ll. The IR is clang's before any LLVM pass, so a call in it is a call in the
# source, at the Release configuration's optimization level, so it holds every body the build may inline (at -O0, clang
# doesn't emit an extern inline body at all). First EXECUTABLE checks the captures of
# src/buster/compiler/call_graph_capture, whose cycle runs through such a body, and the step fails if the check doesn't
# report it; then it checks the units' IR, in the sources' order (BusterSettings.cmake).

cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/BusterSettings.cmake")

foreach(name COMPILER IR)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterCallGraph.cmake: ${name} is required")
    endif()
endforeach()
if(NOT EXISTS "${COMPILER}")
    message(FATAL_ERROR "build.sh check-call-graph: ${COMPILER} is missing; run ./build.sh first")
endif()

set(expected "cycle: inline.ll:recurse -> inline.ll:again -> inline.ll:recurse")
execute_process(COMMAND "${COMPILER}" check-call-graph inline.ll strong.ll
    WORKING_DIRECTORY "${IR}/capture" OUTPUT_VARIABLE report RESULT_VARIABLE status)
string(REGEX MATCH "(^|\n)${expected}(\n|$)" found "${report}")
if(status EQUAL 0 OR found STREQUAL "")
    message(FATAL_ERROR "build.sh check-call-graph: the capture lost the cycle through an inline body; expected \"${expected}\", got:\n${report}")
endif()

set(names "")
foreach(source IN LISTS BUSTER_SOURCES)
    get_filename_component(name "${source}" NAME_WE)
    list(APPEND names "${name}.ll")
endforeach()
execute_process(COMMAND "${COMPILER}" check-call-graph ${names} WORKING_DIRECTORY "${IR}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "build.sh check-call-graph: the call graph breaks the no-recursion rule")
endif()
