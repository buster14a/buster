# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Split one source module into PARTITIONS code parts and the part of its global variables, recording its public
# definitions before the split hides local ones another part refers to: SPLIT is tools/codegen/split.c (Release's fully
# optimized module, in module order) or tools/codegen/presplit.c (Develop's, before the optimizer), with OPTIONS.
# Unchanged parts and exports retain their mtimes so Ninja can reuse their native objects. Changed files are renamed
# into place, and the stamp is written last. An interrupted split cannot publish a successful collection.

cmake_minimum_required(VERSION 3.25)
foreach(name INPUT DIRECTORY SPLIT NM PARTITIONS)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterCodegenSplit.cmake: ${name} is required")
    endif()
endforeach()

function(buster_codegen_publish input output)
    set(same 1)
    if(EXISTS "${output}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${input}" "${output}" RESULT_VARIABLE same)
    endif()
    if(same EQUAL 0)
        file(REMOVE "${input}")
    else()
        file(RENAME "${input}" "${output}")
    endif()
endfunction()

set(partial "${DIRECTORY}/partial")
file(REMOVE "${DIRECTORY}/split.stamp")
file(REMOVE_RECURSE "${partial}")
file(MAKE_DIRECTORY "${partial}")
execute_process(COMMAND "${NM}" --extern-only --defined-only --format=just-symbols "${INPUT}"
    OUTPUT_FILE "${partial}/exports.txt" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "BusterCodegenSplit.cmake: couldn't read ${INPUT}'s public definitions")
endif()
if(NOT DEFINED OPTIONS)
    set(OPTIONS "")
endif()
execute_process(COMMAND "${SPLIT}" "${INPUT}" "${PARTITIONS}" "${partial}" ${OPTIONS} RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "BusterCodegenSplit.cmake: couldn't split ${INPUT}")
endif()
if(NOT EXISTS "${partial}/locals.txt")
    message(FATAL_ERROR "BusterCodegenSplit.cmake: missing the local names of ${INPUT}")
endif()
foreach(part RANGE ${PARTITIONS})
    if(NOT EXISTS "${partial}/part${part}.bc")
        message(FATAL_ERROR "BusterCodegenSplit.cmake: missing partition ${part} of ${INPUT}")
    endif()
endforeach()
foreach(part RANGE ${PARTITIONS})
    buster_codegen_publish("${partial}/part${part}.bc" "${DIRECTORY}/part${part}.bc")
endforeach()
buster_codegen_publish("${partial}/exports.txt" "${DIRECTORY}/exports.txt")
buster_codegen_publish("${partial}/locals.txt" "${DIRECTORY}/locals.txt")
file(REMOVE_RECURSE "${partial}")
file(TOUCH "${DIRECTORY}/split.stamp")
