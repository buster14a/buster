# CMake's compiler-identification probe normally links its test source.
# For `zig cc`, that cold link can construct Zig's libc/linker cache even
# though CMake only needs the identification strings in the object file.
# This file is included before project() and changes only explicit
# `zig cc` C compiler commands; all other compiler state is untouched.
set(BUSTER_ZIG_CC OFF)
if (DEFINED CMAKE_C_COMPILER AND NOT CMAKE_C_COMPILER STREQUAL "")
    set(BUSTER_ZIG_COMPILER_COMMAND ${CMAKE_C_COMPILER})
    list(GET BUSTER_ZIG_COMPILER_COMMAND 0 BUSTER_ZIG_COMPILER_EXECUTABLE)
    list(LENGTH BUSTER_ZIG_COMPILER_COMMAND BUSTER_ZIG_COMPILER_COMMAND_LENGTH)
    get_filename_component(BUSTER_ZIG_COMPILER_NAME "${BUSTER_ZIG_COMPILER_EXECUTABLE}" NAME)

    if (BUSTER_ZIG_COMPILER_COMMAND_LENGTH GREATER 1)
        list(GET BUSTER_ZIG_COMPILER_COMMAND 1 BUSTER_ZIG_COMPILER_ARGUMENT)
        if (BUSTER_ZIG_COMPILER_ARGUMENT STREQUAL "cc")
            set(BUSTER_ZIG_CC ON)
        endif()
    elseif (DEFINED CMAKE_C_COMPILER_ARG1 AND CMAKE_C_COMPILER_ARG1 STREQUAL "cc")
        set(BUSTER_ZIG_CC ON)
    endif()

    if (NOT BUSTER_ZIG_COMPILER_NAME STREQUAL "zig" AND
        NOT BUSTER_ZIG_COMPILER_NAME STREQUAL "zig.exe")
        set(BUSTER_ZIG_CC OFF)
    endif()
endif()

if (BUSTER_ZIG_CC)
    set(CMAKE_C_COMPILER_ID_FLAGS_ALWAYS "-c")
endif()

unset(BUSTER_ZIG_CC)
unset(BUSTER_ZIG_COMPILER_ARGUMENT)
unset(BUSTER_ZIG_COMPILER_COMMAND_LENGTH)
unset(BUSTER_ZIG_COMPILER_NAME)
unset(BUSTER_ZIG_COMPILER_EXECUTABLE)
unset(BUSTER_ZIG_COMPILER_COMMAND)
