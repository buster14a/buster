# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The step after each link: cmake -DINPUT=LINKED -DOUTPUT=EXECUTABLE -DBAKE=0|1 -DNM=PATH
# [-DPADDING=1 [-DUNSHIFTED=EXECUTABLE]] -P BusterPostLink.cmake
#
# Writes OUTPUT from the linked INPUT, which it leaves as it is, so that an unchanged build links nothing. With BAKE,
# INPUT writes itself with the backend's tables built (bake-tables, src/buster/compiler/baked.c, #590), so that a
# compile starts with them rather than building them: about 2 ms of a small file's compile on a Xeon VM whose kernel
# only madvises huge pages. A PIE (the sanitizers' builds) keeps building them when it runs (status 77), and so does
# every executable with BUSTER_BAKE=0. Any other failure, an initializer's state outside the section among them, fails
# the step.
#
# The executable must not reference the stack canary's runtime (#916): -fno-stack-protector pins the policy, and this
# checks that the toolchain's configured default didn't add it back. With PADDING, it is a shifted link (build.sh
# shifts): its padding must be in it, and with UNSHIFTED, it must not be that executable byte for byte.
#
# OUTPUT is removed first and written last, by a rename of the finished file, so that a failed step leaves no
# executable there, rather than one that the inputs no longer make.

cmake_minimum_required(VERSION 3.25)

foreach(name INPUT OUTPUT BAKE NM)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterPostLink.cmake: ${name} is required")
    endif()
endforeach()

set(partial "${OUTPUT}.partial")
file(REMOVE "${OUTPUT}" "${partial}")

function(post_link_fail message)
    file(REMOVE "${partial}")
    message(FATAL_ERROR "${message}")
endfunction()

set(baked FALSE)
if(BAKE)
    execute_process(COMMAND "${INPUT}" bake-tables "${partial}" RESULT_VARIABLE status)
    if(status EQUAL 0)
        set(baked TRUE)
    elseif(NOT status EQUAL 77)
        post_link_fail("${INPUT} couldn't bake its tables")
    endif()
endif()
if(NOT baked)
    file(REMOVE "${partial}")
    # The copy keeps the linked file's permissions.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${INPUT}" "${partial}" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        post_link_fail("couldn't copy ${INPUT}")
    endif()
endif()

execute_process(COMMAND "${NM}" "${partial}" OUTPUT_VARIABLE symbols RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    post_link_fail("${NM} couldn't read ${partial}")
endif()
string(FIND "${symbols}" "__stack_chk_fail" canary)
if(NOT canary EQUAL -1)
    post_link_fail("${OUTPUT} references __stack_chk_fail; -fno-stack-protector didn't take effect")
endif()

if(PADDING)
    string(FIND "${symbols}" " buster_shift_padding\n" padding)
    set(same 1)
    if(DEFINED UNSHIFTED)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${UNSHIFTED}" "${partial}" RESULT_VARIABLE same)
    endif()
    if(padding EQUAL -1 OR same EQUAL 0)
        post_link_fail("build.sh shifts: the linker dropped ${OUTPUT}'s padding")
    endif()
endif()

file(RENAME "${partial}" "${OUTPUT}")
