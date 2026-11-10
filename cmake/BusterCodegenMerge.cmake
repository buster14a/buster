# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Rejoin a source unit's native partitions in fixed order, restoring its original public definition set. The split
# (tools/codegen/split.c) hides the private functions, data and TLS another part refers to, and names a private function's
# alias for them, so its pieces can refer to one another. After the relocatable link those names must become local
# again, before another unity unit's independent copies are linked. Undefined external references retain their
# binding; only definitions absent from EXPORTS are localized.
#
# A relocatable link merges SHF_MERGE sections (lld's ObjFile::shouldMerge), which the final link at -O0 copies whole, as
# it copies the single object's: the allocated ones lose the flag first, so that the parts' constants are concatenated in
# order, as one object's would be.

cmake_minimum_required(VERSION 3.25)
foreach(name OUTPUT EXPORTS OBJECTS LLD OBJCOPY NM READELF)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterCodegenMerge.cmake: ${name} is required")
    endif()
endforeach()
set(merged "${OUTPUT}.merged")
set(partial "${OUTPUT}.partial")
set(keep "${OUTPUT}.globals")
file(REMOVE "${merged}" "${partial}" "${keep}")
set(unmerged "")
set(index 0)
foreach(object IN LISTS OBJECTS)
    execute_process(COMMAND "${READELF}" -S --wide "${object}" OUTPUT_VARIABLE sections RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't read ${object}'s sections")
    endif()
    # Section header rows: [Nr] Name Type Address Off Size ES Flg Lk Inf Al.
    string(REGEX MATCHALL "\\[ *[0-9]+\\] +[^ \n]+ +[A-Z_]+ +[0-9a-f]+ +[0-9a-f]+ +[0-9a-f]+ +[0-9a-f]+ +[A-Za-z]+" rows "${sections}")
    set(flags "")
    foreach(row IN LISTS rows)
        string(REGEX REPLACE "^\\[ *[0-9]+\\] +([^ ]+) +.* ([A-Za-z]+)$" "\\1;\\2" fields "${row}")
        list(GET fields 0 section)
        list(GET fields 1 letters)
        if(letters MATCHES "A" AND letters MATCHES "M")
            list(APPEND flags "--set-section-flags=${section}=alloc,readonly")
        endif()
    endforeach()
    set(copy "${OUTPUT}.part${index}.o")
    execute_process(COMMAND "${OBJCOPY}" ${flags} "${object}" "${copy}" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't clear ${object}'s merge flags")
    endif()
    list(APPEND unmerged "${copy}")
    math(EXPR index "${index} + 1")
endforeach()
execute_process(COMMAND "${LLD}" -r -O0 --threads=1 ${unmerged} -o "${merged}" RESULT_VARIABLE status)
file(REMOVE ${unmerged})
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
