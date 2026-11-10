# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Parallel native generation for the two longest units (docs/commands/build.md, "Parallel native generation"). In the
# Release configuration their object targets compile C through O3 to LLVM bitcode, retaining CMake's depfiles and
# compile database. tools/codegen/split.c partitions that optimized IR in module order; Ninja schedules a single-threaded
# Clang backend job per part; a relocatable link joins the parts in order and restores the source unit's original public
# symbols before the ordinary final link. The code is the single object's, function for function, at the same
# addresses. No C boundary or private state moves between source units. The Debug configuration compiles them as usual.

# Code parts per unit: with the part of its global variables, one job per hardware thread of the 9700X (15).
set(BUSTER_CODEGEN_PARTITIONS 14)

# Finds the tools: llvm-config, clang++, llvm-nm, llvm-objcopy, llvm-readelf and ld.lld matching the configured Clang,
# beside it or on PATH, and LLVM's C API headers. Sets `available`, and `missing` to what is missing or mismatched.
function(buster_codegen_tools available missing)
    get_filename_component(directory "${compiler_file}" DIRECTORY)
    set(absent "")
    foreach(tool CONFIG CXX NM OBJCOPY READELF LLD)
        if(tool STREQUAL "LLD")
            set(name ld.lld)
        elseif(tool STREQUAL "CXX")
            set(name clang++)
        else()
            string(TOLOWER "${tool}" suffix)
            set(name "llvm-${suffix}")
        endif()
        find_program(program NAMES "${name}" HINTS "${directory}" NO_CACHE NO_DEFAULT_PATH)
        if(NOT program)
            find_program(program NAMES "${name}" NO_CACHE)
        endif()
        set(version "")
        set(status 1)
        if(program)
            execute_process(COMMAND "${program}" --version OUTPUT_VARIABLE version ERROR_VARIABLE errors
                RESULT_VARIABLE status TIMEOUT 30)
            string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" version "${version}\n${errors}")
        endif()
        if(NOT program OR NOT status EQUAL 0 OR NOT version VERSION_EQUAL CMAKE_C_COMPILER_VERSION)
            list(APPEND absent "${name} ${CMAKE_C_COMPILER_VERSION}")
        endif()
        set(BUSTER_CODEGEN_${tool} "${program}" PARENT_SCOPE)
        set(found_${tool} "${program}")
        unset(program CACHE)
        unset(program)
    endforeach()
    if(found_CONFIG)
        foreach(query cflags cxxflags ldflags libs system-libs includedir)
            execute_process(COMMAND "${found_CONFIG}" "--${query}" OUTPUT_VARIABLE value RESULT_VARIABLE status
                OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 30)
            if(NOT status EQUAL 0)
                list(APPEND absent "llvm-config --${query}")
            endif()
            separate_arguments(value UNIX_COMMAND "${value}")
            string(REPLACE "-" "_" key "${query}")
            set(BUSTER_CODEGEN_LLVM_${key} "${value}" PARENT_SCOPE)
            set(llvm_${key} "${value}")
        endforeach()
        if(NOT EXISTS "${llvm_includedir}/llvm-c/Core.h")
            list(APPEND absent "LLVM's C API headers (llvm-c/Core.h)")
        endif()
    endif()
    # The tools can be there and LLVM's libraries not link (a release without its shared library, or system libraries
    # llvm-config names missing): link a small C program against LLVM here, as the splitter links, so that auto decides
    # on what will build. A clean build configures first, so the probe stays small (about 0.3 s).
    if(NOT absent)
        if(NOT EXISTS "${llvm_includedir}/llvm/Bitcode/BitcodeWriter.h")
            list(APPEND absent "LLVM's C++ headers (llvm/Bitcode/BitcodeWriter.h)")
        endif()
        set(probe "${CMAKE_BINARY_DIR}/codegen-tools/probe")
        file(MAKE_DIRECTORY "${probe}")
        file(WRITE "${probe}/probe.c" "#include <llvm-c/Core.h>\nint main(void)\n{\n    LLVMContextDispose(LLVMContextCreate());\n    return 0;\n}\n")
        set(log "${probe}/build.log")
        execute_process(COMMAND "${CMAKE_C_COMPILER}" -O1 ${llvm_cflags} -c "${probe}/probe.c" -o "${probe}/probe.o"
            RESULT_VARIABLE status OUTPUT_FILE "${log}" ERROR_FILE "${log}")
        if(status EQUAL 0)
            execute_process(COMMAND "${found_CXX}" -o "${probe}/probe" "${probe}/probe.o" ${llvm_ldflags} ${llvm_libs} ${llvm_system_libs}
                RESULT_VARIABLE status OUTPUT_FILE "${log}" ERROR_FILE "${log}")
        endif()
        if(NOT status EQUAL 0)
            list(APPEND absent "LLVM library that links (${log})")
        endif()
    endif()
    if(absent)
        set(${available} FALSE PARENT_SCOPE)
    else()
        set(${available} TRUE PARENT_SCOPE)
    endif()
    string(REPLACE ";" ", " absent "${absent}")
    set(${missing} "${absent}" PARENT_SCOPE)
endfunction()

# The splitter, built once for every configuration with the LLVM it reads, at configure-time flags llvm-config gives.
function(buster_codegen_splitter)
    set(directory "${CMAKE_BINARY_DIR}/codegen-tools")
    set(splitter "${directory}/split")
    get_filename_component(tools "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/codegen" ABSOLUTE)
    set(sources "${tools}/split.c" "${tools}/split_bitcode.cpp")
    add_custom_command(OUTPUT "${splitter}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
        COMMAND "${CMAKE_C_COMPILER}" -O2 -Wall -Wextra -Werror ${BUSTER_CODEGEN_LLVM_cflags}
            -c "${tools}/split.c" -o "${directory}/split.o"
        COMMAND "${BUSTER_CODEGEN_CXX}" -O2 -Wall -Werror ${BUSTER_CODEGEN_LLVM_cxxflags}
            -c "${tools}/split_bitcode.cpp" -o "${directory}/split_bitcode.o"
        COMMAND "${BUSTER_CODEGEN_CXX}" -o "${splitter}" "${directory}/split.o" "${directory}/split_bitcode.o"
            ${BUSTER_CODEGEN_LLVM_ldflags} ${BUSTER_CODEGEN_LLVM_libs} ${BUSTER_CODEGEN_LLVM_system_libs}
        DEPENDS ${sources} "${BUSTER_CODEGEN_CONFIG}"
        COMMENT "Building the optimized-IR splitter"
        VERBATIM)
    add_custom_target(buster_codegen_splitter DEPENDS "${splitter}")
    set(BUSTER_CODEGEN_SPLIT "${splitter}" PARENT_SCOPE)
endfunction()

function(buster_codegen_split_unit name output)
    if(buster_parallel_codegen AND name MATCHES "^main_unit_(checker|backend)$")
        set(${output} TRUE PARENT_SCOPE)
    else()
        set(${output} FALSE PARENT_SCOPE)
    endif()
endfunction()

# The unit's object for the link: in Release, the joined native parts of a split unit.
function(buster_native_object output name)
    buster_codegen_split_unit("${name}" split)
    if(split)
        set(native "${CMAKE_BINARY_DIR}/Release/codegen/${name}/native.o")
        set(object "$<IF:$<CONFIG:Release>,${native},$<TARGET_OBJECTS:buster_${name}>>")
    else()
        set(object "$<TARGET_OBJECTS:buster_${name}>")
    endif()
    set(${output} "${object}" PARENT_SCOPE)
endfunction()

# The split, the parts' native generation and the join of unit `name`, whose Release object is bitcode. `options` are
# the parts' code-generation options (the unit's -march and debug information).
function(buster_codegen_unit name)
    set(scripts "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(directory "${CMAKE_BINARY_DIR}/$<CONFIG>/codegen/${name}")
    set(parts "")
    foreach(part RANGE ${BUSTER_CODEGEN_PARTITIONS})
        list(APPEND parts "${directory}/part${part}.bc")
    endforeach()
    set(exports "${directory}/exports.txt")
    set(stamp "${directory}/split.stamp")
    add_custom_command(OUTPUT "${stamp}"
        BYPRODUCTS ${parts} "${exports}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_OBJECTS:buster_${name}>" "-DDIRECTORY=${directory}"
            "-DSPLIT=${BUSTER_CODEGEN_SPLIT}" "-DNM=${BUSTER_CODEGEN_NM}" "-DPARTITIONS=${BUSTER_CODEGEN_PARTITIONS}"
            -P "${scripts}/BusterCodegenSplit.cmake"
        DEPENDS "$<TARGET_OBJECTS:buster_${name}>" "${BUSTER_CODEGEN_SPLIT}" "${BUSTER_CODEGEN_NM}"
            "${scripts}/BusterCodegenSplit.cmake"
        COMMENT "Partitioning ${name}'s optimized IR"
        VERBATIM)
    # The target attaches the split to the build, which then knows the parts' rule; the parts' own commands depend on
    # their bitcode alone.
    add_custom_target(buster_${name}_split DEPENDS "${stamp}")

    # The parts' native code: the same driver's target and PIC defaults, CPU choice, O3 instruction selection and
    # machine-code pipeline as the unit's own compile, with the IR optimizer disabled, since the module has passed O3.
    # A part depends on its bitcode alone, which the split rewrites only when it changed (Ninja's restat).
    set(debug "")
    if(NOT "${buster_debug}" STREQUAL "")
        set(debug "$<$<CONFIG:Release>:${buster_debug}>")
    endif()
    set(objects "")
    foreach(part RANGE ${BUSTER_CODEGEN_PARTITIONS})
        set(object "${directory}/part${part}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${CMAKE_C_COMPILER}" -x ir "-march=${buster_arch}" ${debug} -fno-stack-protector -O3 -Wall -Werror
                -Xclang -disable-llvm-passes -Wno-unused-command-line-argument -c "${directory}/part${part}.bc" -o "${object}"
            DEPENDS "${directory}/part${part}.bc"
            COMMENT "Generating ${name}'s native part ${part}"
            COMMAND_EXPAND_LISTS
            VERBATIM)
        list(APPEND objects "${object}")
    endforeach()
    set(native "${directory}/native.o")
    add_custom_command(OUTPUT "${native}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${native}" "-DEXPORTS=${exports}" "-DOBJECTS=${objects}"
            "-DLLD=${BUSTER_CODEGEN_LLD}" "-DOBJCOPY=${BUSTER_CODEGEN_OBJCOPY}" "-DNM=${BUSTER_CODEGEN_NM}"
            "-DREADELF=${BUSTER_CODEGEN_READELF}" -P "${scripts}/BusterCodegenMerge.cmake"
        DEPENDS ${objects} "${exports}" "${BUSTER_CODEGEN_LLD}" "${BUSTER_CODEGEN_OBJCOPY}" "${BUSTER_CODEGEN_NM}"
            "${BUSTER_CODEGEN_READELF}" "${scripts}/BusterCodegenMerge.cmake"
        COMMENT "Combining ${name}'s native parts"
        VERBATIM)
endfunction()
