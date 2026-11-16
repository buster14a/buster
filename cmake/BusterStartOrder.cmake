# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The start's code order (#1444, #2873, docs/commands/build.md): the functions that compiles run, from LIST
# (cmake/BusterStartOrder.txt, which `compiler start-order` writes), placed together before .text, where the kernel's
# fault-around maps them in fewer 64 KB windows than where their units' objects have them. They start on a 2 MB
# boundary (ALIGN), in memory as in the file (CMakeLists.txt's -z max-page-size), so that the kernel can map them by
# huge pages from their first byte: three of them hold what every compile measured runs. The first 64 KB there, a
# window of the kernel's fault-around, hold no code (buster_start_probe): no fault of a process's own maps them, so a
# page of them is mapped only where a huge page maps the range, which the exit helper asks the kernel (lane.c's
# lane_exit_collapse); buster_start_end, the checker's section's end, is how far it asks for huge pages unasked.
#
#   buster_start_order_files(LIST SCRIPT SYMBOLS SHIFT_SOURCE)
#
# writes SCRIPT, a linker script for lld that places each `UNIT NAME` line's function by its unit's object and its
# section, which every unit compiled with -ffunction-sections has (.text.NAME, or .text.unlikely.NAME for a cold one,
# .text.hot.NAME for a hot one): one unit's copy of a static helper moves, the other units' copies don't, as a symbol
# ordering file, which names symbols, would move all of them. The object is the unit's own (*/UNIT.c.o) or its parallel
# parts' join (*/UNIT/native.o), whose sections may name a function by its alias (.text.NAME.buster_split). The checker's own code (.text.buster_checker, main.c: one section of each unit, which
# starts on a page) follows, every unit's together, and then .text. A shifted link's padding (build.sh shifts) goes right
# before SHIFT_SOURCE's first function here (.text.buster_shift_padding), so that its placements move that unit's code
# here too, and again first in .text (.text.buster_shift_padding_text, first in SYMBOLS), so that they move every unit's
# code there by exactly the shift (buster_start_order_shift_padded). In .text, each unit's page-aligned checker section, empty where the unit has none of
# the checker's code, started its functions on a page, and the page-aligned .text began after the padding: no shift
# moved .text's code (#2728). And SYMBOLS, the `- NAME` lines' names, a symbol ordering file for the global functions
# and the C library's (which the static link places after every object): lld puts their sections first in .text. The
# script can't reorder the functions inside a checker section: where a unit's native code is generated in parallel parts
# (BusterCodegen.cmake), in every static build, whatever its linker, buster_start_order_unit below writes the names the
# split (tools/codegen/presplit.c or split.c, --first) puts first in that section. Each file is written only when its text changes, so
# that an unchanged list relinks nothing.

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
            # In a unit's parts, a local function another part calls has its alias's name alone where its own
            # part's optimizer folded the two (presplit.c's NAME.buster_split): 368 of the list's 511 were.
            set(joined "${wanted} .text.${name}.buster_split .text.unlikely.${name}.buster_split .text.hot.${name}.buster_split")
            string(APPEND sections "        */${stem}.c.o(${wanted})\n        */${stem}/native.o(${joined})\n")
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
        "SECTIONS\n{\n    .text.buster_start : ALIGN(0x200000)\n    {\n        buster_start_probe = .;\n        . += 0x10000;\n${sections}    }\n"
        "    ASSERT(SIZEOF(.text.buster_start) > 0x10000, \"the start's code order placed no function: the units' objects or sections aren't the link's\")\n"
        "    .text.buster_checker :\n    {\n        *(.text.buster_checker)\n        buster_start_end = .;\n    }\n"
        "}\nINSERT BEFORE .text;\n")
    buster_start_order_write("${script}" "${text}")
    # A shifted link's padding before .text's code: the first section lld places there (absent elsewhere, which
    # --no-warn-symbol-ordering lets pass).
    buster_start_order_write("${symbols}" "buster_shift_padding_text\n${names}")
    # Whether SHIFT_SOURCE's padding goes before its functions here, so that a shifted link needs the second padding
    # to move .text (a source with no start-ordered function has its padding right before its objects in .text).
    set(buster_start_order_shift_padded ${padded} PARENT_SCOPE)
endfunction()

# The start's order inside unit UNIT's named sections (#1444): LIST's names of UNIT's own functions (`UNIT.c NAME`) and
# of the global ones (`- NAME`, which any unit may define), in LIST's order, one a line, written to PATH for the
# parallel split's --first, which puts those that the unit defines in .text.buster_checker first in that section.
# `count` is the number of UNIT's own lines; with none, PATH is removed and the split takes no list.
function(buster_start_order_unit list unit path count)
    file(STRINGS "${list}" lines)
    set(names "")
    set(own 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^${unit}\\.c ([A-Za-z_][A-Za-z0-9_.]*)$")
            string(APPEND names "${CMAKE_MATCH_1}\n")
            math(EXPR own "${own} + 1")
        elseif(line MATCHES "^- ([A-Za-z_.][A-Za-z0-9_.@]*)$")
            string(APPEND names "${CMAKE_MATCH_1}\n")
        endif()
    endforeach()
    if(own EQUAL 0)
        file(REMOVE "${path}")
    else()
        buster_start_order_write("${path}" "${names}")
    endif()
    set(${count} ${own} PARENT_SCOPE)
endfunction()
