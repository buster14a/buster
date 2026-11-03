# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# BUSTER_PGO's profile (#880): cmake -DLLVM_PROFDATA=PATH -DPROFILES=DIRECTORY -DDIRECTORIES=NAME,... -DOUTPUT=FILE
# -P BusterMergeProfile.cmake
#
# Merges the raw profiles in DIRECTORY's subdirectories NAME, the current training's processes' (CMakeLists.txt's pgo
# steps; another training's left over beside them take no part), into FILE, which is written in place by a rename once
# llvm-profdata succeeds, and only when it changed. Fails when the training left no profile.

cmake_minimum_required(VERSION 3.25)

foreach(name LLVM_PROFDATA PROFILES DIRECTORIES OUTPUT)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterMergeProfile.cmake: ${name} is required")
    endif()
endforeach()

string(REPLACE "," ";" directories "${DIRECTORIES}")
set(profiles "")
foreach(directory IN LISTS directories)
    file(GLOB found "${PROFILES}/${directory}/*.profraw")
    list(APPEND profiles ${found})
endforeach()
if(NOT profiles)
    message(FATAL_ERROR "buster: BUSTER_PGO's training left no profile under ${PROFILES}")
endif()
list(SORT profiles)
set(partial "${OUTPUT}.partial")
file(REMOVE "${partial}")
execute_process(COMMAND "${LLVM_PROFDATA}" merge -o "${partial}" ${profiles} RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    file(REMOVE "${partial}")
    message(FATAL_ERROR "buster: ${LLVM_PROFDATA} couldn't merge the profile")
endif()
# The same profile leaves the file as it was, and so the units compiled with it, which Ninja then doesn't recompile.
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${partial}" "${OUTPUT}" RESULT_VARIABLE different)
if(different EQUAL 0)
    file(REMOVE "${partial}")
else()
    file(RENAME "${partial}" "${OUTPUT}")
endif()
list(LENGTH profiles count)
message(NOTICE "buster: merged ${count} profiles into ${OUTPUT}")
