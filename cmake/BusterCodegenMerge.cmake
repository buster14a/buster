# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Rejoin a source unit's native partitions in fixed order, restoring its original public definition set. SplitModule
# promotes private functions, data and TLS to hidden globals so its pieces can refer to one another. After the
# relocatable link those names must become local again, before another unity unit's independent copies are linked.
# Undefined external references retain their binding; only definitions absent from EXPORTS are localized.

cmake_minimum_required(VERSION 3.25)
foreach(name OUTPUT EXPORTS OBJECTS LLD OBJCOPY NM)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterCodegenMerge.cmake: ${name} is required")
    endif()
endforeach()
set(merged "${OUTPUT}.merged")
set(partial "${OUTPUT}.partial")
set(keep "${OUTPUT}.globals")
file(REMOVE "${merged}" "${partial}" "${keep}")
execute_process(COMMAND "${LLD}" -r -O0 --threads=1 ${OBJECTS} -o "${merged}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't combine ${OUTPUT}'s partitions")
endif()
file(READ "${EXPORTS}" expected)
# Read undefined symbols after merging: unresolved references between partitions were private definitions in the
# source module. Only references still unresolved in the native unit are part of its actual external boundary.
execute_process(COMMAND "${NM}" --extern-only --undefined-only --format=just-symbols "${merged}"
    OUTPUT_VARIABLE undefined_names RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't read ${OUTPUT}'s unresolved symbols")
endif()
execute_process(COMMAND "${NM}" --extern-only --undefined-only --format=posix "${merged}"
    OUTPUT_VARIABLE undefined_before RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't read ${OUTPUT}'s unresolved bindings")
endif()
file(WRITE "${keep}" "${expected}${undefined_names}")
execute_process(COMMAND "${OBJCOPY}" "--keep-global-symbols=${keep}" "${merged}" "${partial}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    file(REMOVE "${merged}" "${partial}")
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't restore ${OUTPUT}'s private definitions")
endif()
execute_process(COMMAND "${NM}" --extern-only --defined-only --format=just-symbols "${partial}"
    OUTPUT_VARIABLE actual RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT actual STREQUAL expected)
    file(REMOVE "${merged}" "${partial}")
    message(FATAL_ERROR "BusterCodegenMerge.cmake: ${OUTPUT}'s public definitions changed")
endif()
execute_process(COMMAND "${NM}" --extern-only --undefined-only --format=posix "${partial}"
    OUTPUT_VARIABLE undefined_after RESULT_VARIABLE status)
if(NOT status EQUAL 0 OR NOT undefined_after STREQUAL undefined_before)
    file(REMOVE "${merged}" "${partial}" "${keep}")
    message(FATAL_ERROR "BusterCodegenMerge.cmake: ${OUTPUT}'s unresolved bindings changed")
endif()
file(REMOVE "${merged}" "${keep}")
file(RENAME "${partial}" "${OUTPUT}")
