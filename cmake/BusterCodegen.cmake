# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# Optional native-code jobs for the two longest units. Their ordinary object targets compile C through O3 to LLVM
# bitcode, retaining CMake's depfiles and compile database. llvm-split partitions that already optimized IR; Ninja
# schedules eight independent, single-threaded Clang backend jobs. A native partial link restores the source unit's
# original public symbols before the ordinary final link. No C boundary or private state moves between source units.

set(BUSTER_CODEGEN_PARTITIONS 8)

function(buster_codegen_tools)
    get_filename_component(directory "${compiler_file}" DIRECTORY)
    foreach(tool SPLIT NM OBJCOPY LLD)
        if(tool STREQUAL "LLD")
            set(name ld.lld)
        else()
            string(TOLOWER "${tool}" suffix)
            set(name "llvm-${suffix}")
        endif()
        find_program(program NAMES "${name}" HINTS "${directory}" NO_CACHE)
        if(NOT program)
            message(FATAL_ERROR "buster: BUSTER_PARALLEL_CODEGEN needs ${name} matching ${CMAKE_C_COMPILER}")
        endif()
        execute_process(COMMAND "${program}" --version OUTPUT_VARIABLE version ERROR_VARIABLE errors
            RESULT_VARIABLE status TIMEOUT 30)
        string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" version "${version}\n${errors}")
        if(NOT status EQUAL 0 OR NOT version VERSION_EQUAL CMAKE_C_COMPILER_VERSION)
            message(FATAL_ERROR "buster: BUSTER_PARALLEL_CODEGEN needs ${name} ${CMAKE_C_COMPILER_VERSION}, found ${program} (${version})")
        endif()
        set(BUSTER_CODEGEN_${tool} "${program}" PARENT_SCOPE)
        unset(program)
    endforeach()
endfunction()

function(buster_native_object output name)
    if(buster_parallel_codegen AND name MATCHES "^main_unit_(checker|backend)$")
        set(object "${CMAKE_BINARY_DIR}/Release/codegen/${name}/native.o")
    else()
        set(object "$<TARGET_OBJECTS:buster_${name}>")
    endif()
    set(${output} "${object}" PARENT_SCOPE)
endfunction()

function(buster_codegen_unit name)
    set(scripts "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    # The experiment accepts Release alone, so these generated source paths need no configuration expression.
    # A concrete path also lets CMake associate LANGUAGE C with its otherwise unknown .bc extension.
    set(directory "${CMAKE_BINARY_DIR}/Release/codegen/${name}")
    set(parts "")
    math(EXPR last "${BUSTER_CODEGEN_PARTITIONS} - 1")
    foreach(part RANGE ${last})
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
    add_custom_target(buster_${name}_split DEPENDS "${stamp}")

    set(native_parts "")
    foreach(part RANGE ${last})
        set(source "${directory}/part${part}.bc")
        set(target buster_${name}_codegen_${part})
        set_source_files_properties("${source}" PROPERTIES GENERATED TRUE LANGUAGE C)
        add_library(${target} OBJECT "${source}")
        set_target_properties(${target} PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
        # Keep the same driver's target/PIC defaults and CPU choice. The module has already passed O3; disable a
        # second IR optimizer while retaining the ordinary O3 instruction-selection and machine-code pipeline.
        target_compile_options(${target} PRIVATE -x ir "-march=${buster_arch}" -fno-stack-protector -O3
            -Wall -Werror -Xclang -disable-llvm-passes -Wno-unused-command-line-argument)
        # CMake supplies -MD/-MT/-MF for a C object target; IR has no textual includes and Clang does not use them.
        # This suppression is confined to generated IR. The original C compile retains every source warning.
        add_dependencies(${target} buster_${name}_split)
        list(APPEND native_parts "$<TARGET_OBJECTS:${target}>")
    endforeach()
    buster_native_object(output "${name}")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${output}" "-DEXPORTS=${exports}" "-DOBJECTS=${native_parts}"
            "-DLLD=${BUSTER_CODEGEN_LLD}" "-DOBJCOPY=${BUSTER_CODEGEN_OBJCOPY}" "-DNM=${BUSTER_CODEGEN_NM}"
            -P "${scripts}/BusterCodegenMerge.cmake"
        DEPENDS ${native_parts} "${exports}" "${BUSTER_CODEGEN_LLD}" "${BUSTER_CODEGEN_OBJCOPY}" "${BUSTER_CODEGEN_NM}"
            "${scripts}/BusterCodegenMerge.cmake"
        COMMENT "Combining ${name}'s native partitions"
        VERBATIM)
endfunction()
