# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The start's code order (#1444, docs/commands/build.md): the functions that small compiles run, from LIST
# (cmake/BusterStartOrder.txt, which `compiler start-order` writes), placed together before .text, where the kernel's
# fault-around maps them in fewer 64 KB windows than where their units' objects have them.
#
#   buster_start_order_files(LIST SCRIPT SYMBOLS SHIFT_SOURCE)
#
# writes SCRIPT, a linker script for lld that places each `UNIT NAME` line's function by its unit's object and its
# section, which every unit compiled with -ffunction-sections has (.text.NAME, or .text.unlikely.NAME for a cold one,
# .text.hot.NAME for a hot one): one unit's copy of a static helper moves, the other units' copies don't, as a symbol
# ordering file, which names symbols, would move all of them. The object is the unit's own (*/UNIT.c.o) or its parallel
# parts' join (*/UNIT/native.o). A shifted link's padding (build.sh shifts, .text.buster_shift_padding) goes right before
# SHIFT_SOURCE's first function, so that its placements move that unit's code here too. And SYMBOLS, the `- NAME` lines'
# names, a symbol ordering file for the global functions and the C library's (which the static link places after every
# object): lld puts their sections first in .text, right after the units' local ones. The checker's own code
# (.text.buster_checker, main.c) is one section of each unit, which stays where it is. Each file is written only when
# its text changes, so that an unchanged list relinks nothing.

function(buster_start_order_write path text)
    set(current "")
    if(EXISTS "${path}")
        file(READ "${path}" current)
    endif()
    if(NOT current STREQUAL text)
        file(WRITE "${path}" "${text}")
    endif()
endfunction()

function(buster_start_order_files list script symbols shift_source)
    file(STRINGS "${list}" lines)
    set(sections "")
    set(names "")
    set(padded FALSE)
    set(functions 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^#" OR line STREQUAL "")
            continue()
        endif()
        if(line MATCHES "^(main|main_unit_[a-z0-9_]+)\\.c ([A-Za-z_][A-Za-z0-9_.]*)$")
            set(stem "${CMAKE_MATCH_1}")
            set(name "${CMAKE_MATCH_2}")
            if(NOT padded AND "${stem}.c" STREQUAL "${shift_source}")
                string(APPEND sections "        *(.text.buster_shift_padding)\n")
                set(padded TRUE)
            endif()
            set(wanted ".text.${name} .text.unlikely.${name} .text.hot.${name}")
            string(APPEND sections "        */${stem}.c.o(${wanted})\n        */${stem}/native.o(${wanted})\n")
            math(EXPR functions "${functions} + 1")
        elseif(line MATCHES "^- ([A-Za-z_.][A-Za-z0-9_.@]*)$")
            string(APPEND names "${CMAKE_MATCH_1}\n")
        else()
            message(FATAL_ERROR "buster: ${list} has a line that is neither `UNIT NAME` nor `- NAME`: ${line}")
        endif()
    endforeach()
    if(functions EQUAL 0)
        message(FATAL_ERROR "buster: ${list} names no function of main.c's units")
    endif()
    string(CONCAT text
        "/* The start's code order (cmake/BusterStartOrder.cmake), from ${list}. */\n"
        "SECTIONS\n{\n    .text.buster_start :\n    {\n${sections}    }\n"
        "    ASSERT(SIZEOF(.text.buster_start) > 0, \"the start's code order placed no function: the units' objects or sections aren't the link's\")\n"
        "}\nINSERT BEFORE .text;\n")
    buster_start_order_write("${script}" "${text}")
    buster_start_order_write("${symbols}" "${names}")
endfunction()
