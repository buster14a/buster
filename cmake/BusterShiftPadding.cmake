# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The padding of a shifted link (build.sh shifts): cmake -DOBJECT=UNIT.o -DSOURCE=NAME -DSHIFT=BYTES -DOUTPUT=PADDING.o
# -DCLANG=PATH -DREADELF=PATH [-DTEXT=1] -P BusterShiftPadding.cmake
#
# SHIFT bytes in a section of their own, .text.buster_shift_padding, which the linkers put in .text in the objects'
# order, with UNIT.o's .text's alignment and SHF_GNU_RETAIN: wild collects sections nothing refers to (0.10 even what
# -u names, which 0.11 keeps), and places .text's by their alignment, the largest first, in the objects' order within
# each, so only padding of the same alignment goes right before the unit's. The link puts it right before UNIT.o, the
# object of the source named SOURCE. Fails for a shift that isn't a multiple of that alignment, which would round it up.
# With TEXT (a link with the start's code order, BusterStartOrder.cmake), a second section of SHIFT bytes,
# .text.buster_shift_padding_text, which that link's symbol ordering puts first in .text, moves every unit's code there.

cmake_minimum_required(VERSION 3.25)

foreach(name OBJECT SOURCE SHIFT OUTPUT CLANG READELF)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterShiftPadding.cmake: ${name} is required")
    endif()
endforeach()

execute_process(COMMAND "${READELF}" -SW "${OBJECT}" OUTPUT_VARIABLE sections RESULT_VARIABLE status)
set(alignment "")
if(status EQUAL 0 AND sections MATCHES "\\] \\.text [^\n]* ([0-9]+)\n")
    set(alignment "${CMAKE_MATCH_1}")
endif()
if(NOT alignment MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "build.sh shifts: can't read the alignment of ${SOURCE}'s .text")
endif()
math(EXPR remainder "${SHIFT} % ${alignment}")
if(NOT remainder EQUAL 0)
    message(FATAL_ERROR "build.sh shifts: ${SHIFT} isn't a multiple of ${SOURCE}'s .text's alignment, ${alignment}")
endif()

set(assembly "${OUTPUT}.s")
set(text "\t.section .text.buster_shift_padding,\"axR\",@progbits\n\t.balign ${alignment}\n\t.globl buster_shift_padding\nbuster_shift_padding:\n\t.skip ${SHIFT}\n")
if(TEXT)
    string(APPEND text "\t.section .text.buster_shift_padding_text,\"axR\",@progbits\n\t.balign ${alignment}\n\t.globl buster_shift_padding_text\nbuster_shift_padding_text:\n\t.skip ${SHIFT}\n")
endif()
file(WRITE "${assembly}" "${text}")
execute_process(COMMAND "${CLANG}" -c -x assembler "${assembly}" -o "${OUTPUT}" RESULT_VARIABLE status)
file(REMOVE "${assembly}")
if(NOT status EQUAL 0)
    file(REMOVE "${OUTPUT}")
    message(FATAL_ERROR "build.sh shifts: couldn't assemble ${OUTPUT}")
endif()
message(NOTICE "build.sh shifts: ${SOURCE}'s code ${SHIFT} bytes later (its .text is aligned to ${alignment})")
