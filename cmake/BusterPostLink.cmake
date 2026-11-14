# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The step after each link: cmake -DINPUT=LINKED -DOUTPUT=EXECUTABLE -DBAKE=0|1 -DNM=PATH
# [-DPADDING=1 [-DUNSHIFTED=EXECUTABLE [-DMOVED=1 -DSHIFT=BYTES]]] -P BusterPostLink.cmake
#
# Writes OUTPUT from the linked INPUT, which it leaves as it is, so that an unchanged build links nothing. With BAKE,
# INPUT writes itself with the backend's tables built (bake-tables, src/buster/compiler/baked.c, #590), so that a
# compile starts with them rather than building them: about 2 ms of a small file's compile on a Xeon VM whose kernel
# only madvises huge pages. A PIE (TSan, MSan and fuzzing builds) keeps building them when it runs (status 77), and so
# does every executable with BUSTER_BAKE=0. Any other failure, an initializer's state outside the section among them,
# fails the step.
#
# The executable must not reference the stack canary's runtime (#916): -fno-stack-protector pins the policy, and this
# checks that the toolchain's configured default didn't add it back, in the compiler's objects (-DOBJECTS=LIST), or
# without them in the executable. With PADDING, it is a shifted link (build.sh
# shifts): its padding must be in it, and with UNSHIFTED, it must not be that executable byte for byte. With MOVED (a
# link with the start's code order, whose padding also goes first in .text), main_unit_backend, an entry in .text, must
# be SHIFT's multiple of 64 bytes later than in UNSHIFTED, give or take whole pages (the C library's string functions,
# first in .text, and some of the units' functions are aligned to 64, which takes up the rest, and the start's padding
# can push the page-aligned checker's sections before .text by a page): before #2728 each unit's page-aligned checker
# section and .text's page alignment absorbed every shift short of a page there.
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

# Only shifted links and the no-objects fallback need the executable's full symbol table. Ordinary links check
# the compiler's objects below, so reading the linked runtime's symbols as well would produce unused work.
if(PADDING OR NOT DEFINED OBJECTS OR OBJECTS STREQUAL "")
    execute_process(COMMAND "${NM}" "${partial}" OUTPUT_VARIABLE symbols RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        post_link_fail("${NM} couldn't read ${partial}")
    endif()
endif()
# The canary in the compiler's own objects (OBJECTS) where they are given: a static link's libc.a defines and calls
# __stack_chk_fail itself, as libc.so.6 does where the executable doesn't see it. Without OBJECTS, in the executable.
if(DEFINED OBJECTS AND NOT OBJECTS STREQUAL "")
    execute_process(COMMAND "${NM}" -u ${OBJECTS} OUTPUT_VARIABLE checked RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        post_link_fail("${NM} couldn't read the objects ${OUTPUT} links")
    endif()
else()
    set(checked "${symbols}")
endif()
string(FIND "${checked}" "__stack_chk_fail" canary)
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
    if(MOVED)
        execute_process(COMMAND "${NM}" "${UNSHIFTED}" OUTPUT_VARIABLE unshifted RESULT_VARIABLE status)
        set(moved "")
        if(status EQUAL 0 AND unshifted MATCHES "(^|\n)([0-9a-f]+) T main_unit_backend\n" )
            set(before "${CMAKE_MATCH_2}")
            if(symbols MATCHES "(^|\n)([0-9a-f]+) T main_unit_backend\n")
                math(EXPR moved "0x${CMAKE_MATCH_2} - 0x${before}")
            endif()
        endif()
        math(EXPR expected "(${SHIFT} - ${SHIFT} % 64) % 4096")
        set(offset "")
        if(NOT moved STREQUAL "")
            math(EXPR offset "${moved} % 4096")
        endif()
        if(NOT offset STREQUAL "${expected}")
            post_link_fail("build.sh shifts: ${OUTPUT}'s .text moved by '${moved}' bytes, not ${expected} bytes and whole pages")
        endif()
    endif()
endif()

file(RENAME "${partial}" "${OUTPUT}")
