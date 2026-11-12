# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# One configuration's compile commands: cmake -DINPUT=TREE/compile_commands.json -DOUTPUT=FILE -DCONFIG=Debug|Release|Develop
# -P BusterCompileCommands.cmake
#
# Ninja Multi-Config's compile_commands.json has every unit once per configuration, which leaves an editor or a static
# analyzer to pick one of each by chance; build.sh publishes the configuration it built, as build/compile_commands.json.

cmake_minimum_required(VERSION 3.25)

foreach(name INPUT OUTPUT CONFIG)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "BusterCompileCommands.cmake: ${name} is required")
    endif()
endforeach()

file(READ "${INPUT}" commands)
string(JSON count LENGTH "${commands}")
set(selected "")
if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON output ERROR_VARIABLE missing GET "${commands}" ${index} output)
        if(missing)
            # CMake 3.25's entries have no output member; the object is the command's -o argument.
            string(JSON line GET "${commands}" ${index} command)
            string(REGEX MATCH " -o ([^ ]+)" output "${line}")
            set(output "${CMAKE_MATCH_1}")
        endif()
        if(output MATCHES "\\.dir/${CONFIG}/")
            string(JSON command GET "${commands}" ${index})
            if(selected STREQUAL "")
                string(APPEND selected "[\n${command}")
            else()
                string(APPEND selected ",\n${command}")
            endif()
        endif()
    endforeach()
endif()
if(selected STREQUAL "")
    message(FATAL_ERROR "BusterCompileCommands.cmake: ${INPUT} has no ${CONFIG} commands")
endif()
# Keep an interrupted write from leaving a newer, partial cached output that Ninja could consider up to date.
file(WRITE "${OUTPUT}.tmp" "${selected}\n]\n")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
