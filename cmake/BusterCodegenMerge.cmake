# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Rejoin a source unit's native partitions in fixed order, restoring its original public definition set. The split
# (tools/codegen/split.c) hides the private functions, data and TLS another part refers to, and names a private function's
# alias for them, so its pieces can refer to one another. After the relocatable link those names must become local
# again, before another unity unit's independent copies are linked. Undefined external references retain their
# binding; only definitions absent from EXPORTS are localized.
#
# A relocatable link merges SHF_MERGE sections, which the final link at -O0 copies whole from the single object (the
# checker's parts lost 27,904 bytes of duplicate constants that way): the allocated ones lose the flag first, so that the
# parts' constants are concatenated in order, as one object's are. A final link that merges them itself (MERGES: wild,
# mold or GNU ld, the sanitizer builds' dynamic links) merges the single object's with every other object's, so the
# parts keep the flag there: cleared, their 548 KB of strings that other units repeat stayed in an MSan executable.
#
# llvm-objcopy clears GRP_COMDAT on every section group whose signature symbol is local, as the code generator's
# sanitizer groups are (AddressSanitizer's module constructor and destructor, which the linker keeps once among the
# units): COMDAT, tools/codegen/comdat.c, sets it again on the joined object's groups that the parts had as comdats.

cmake_minimum_required(VERSION 3.25)
foreach(name OUTPUT EXPORTS LOCALS OBJECTS LLD OBJCOPY NM READELF COMDAT)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterCodegenMerge.cmake: ${name} is required")
    endif()
endforeach()
set(merged "${OUTPUT}.merged")
set(partial "${OUTPUT}.partial")
set(keep "${OUTPUT}.globals")
file(REMOVE "${merged}" "${partial}" "${keep}")
if(NOT DEFINED MERGES)
    set(MERGES 0)
endif()
set(unmerged "")
set(copies "")
if(MERGES)
    set(unmerged ${OBJECTS})
else()
    set(index 0)
    foreach(object IN LISTS OBJECTS)
        execute_process(COMMAND "${READELF}" --elf-output-style=GNU -S --wide "${object}" OUTPUT_VARIABLE sections RESULT_VARIABLE status)
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
        list(APPEND copies "${copy}")
        math(EXPR index "${index} + 1")
    endforeach()
endif()
execute_process(COMMAND "${LLD}" -r -O0 --threads=1 ${unmerged} -o "${merged}" RESULT_VARIABLE status)
if(copies)
    file(REMOVE ${copies})
endif()
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
# A name the unit defined locally (LOCALS, the split's list, with its aliases) must be defined again here: one still
# undefined was a local definition no part kept, which lld accepts unreferenced but GNU ld refuses as a hidden symbol.
file(WRITE "${OUTPUT}.undefined" "${undefined_names}")
execute_process(COMMAND grep -F -x -f "${LOCALS}" "${OUTPUT}.undefined" OUTPUT_VARIABLE lost RESULT_VARIABLE status)
file(REMOVE "${OUTPUT}.undefined")
if(status EQUAL 0)
    string(REGEX MATCH "^[^\n]*" first "${lost}")
    message(FATAL_ERROR "BusterCodegenMerge.cmake: ${OUTPUT} leaves the unit's own ${first} undefined")
elseif(NOT status EQUAL 1)
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't check ${OUTPUT}'s undefined symbols against ${LOCALS}")
endif()
file(WRITE "${keep}" "${expected}${undefined_names}")
execute_process(COMMAND "${OBJCOPY}" "--keep-global-symbols=${keep}" "${merged}" "${partial}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    file(REMOVE "${merged}" "${partial}")
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't restore ${OUTPUT}'s private definitions")
endif()
execute_process(COMMAND "${COMDAT}" "${partial}" ${OBJECTS} RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    file(REMOVE "${merged}" "${partial}")
    message(FATAL_ERROR "BusterCodegenMerge.cmake: couldn't restore ${OUTPUT}'s comdat groups")
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
