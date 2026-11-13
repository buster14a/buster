# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The build of build/cmake/VARIANT/CONFIG/compiler: Ninja Multi-Config, Debug, Release and Develop in one tree per
# toolchain and variant (CMakePresets.json, build.sh). docs/commands/build.md has the configurations, the settings, the
# targets and the outputs. cmake/BusterSettings.cmake holds the sources and the flags; this file applies them.

cmake_minimum_required(VERSION 3.25)

# Debug, Release and Develop, and Release when a build names no configuration, as ./build.sh builds.
if(NOT DEFINED CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Develop" CACHE STRING "The configurations: any of Debug, Release and Develop")
endif()
if(NOT DEFINED CMAKE_DEFAULT_BUILD_TYPE)
    set(CMAKE_DEFAULT_BUILD_TYPE Release CACHE STRING "The configuration a build that names none builds")
endif()

# The flags are the build description's alone. Clear the cache before project() enables C and compiles/links its
# probes: clearing normal variables afterwards is too late for CFLAGS, LDFLAGS or flags from -D/the existing cache.
# Defining every entry also prevents CMake's defaults (Release's -DNDEBUG among them) from being initialized.
# Compiler identification reads CFLAGS itself when CMAKE_C_FLAGS is empty, even with that cache entry defined.
set(ENV{CFLAGS} "")
set(ENV{LDFLAGS} "")
foreach(suffix "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_C_FLAGS${suffix} "" CACHE STRING "Unused: the build description supplies the C flags" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS${suffix} "" CACHE STRING "Unused: the build description supplies the linker flags" FORCE)
endforeach()

project(buster LANGUAGES C)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
    message(FATAL_ERROR "buster: configure with -G \"Ninja Multi-Config\" (CMakePresets.json and build.sh do)")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "buster: the compiler builds with Clang, not ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER})")
endif()
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    if(NOT config MATCHES "^(Debug|Release|Develop)$")
        message(FATAL_ERROR "buster: CMAKE_CONFIGURATION_TYPES may hold Debug, Release and Develop, not ${config}")
    endif()
endforeach()

# The settings, which build.sh takes from its environment variables of the same names (docs/commands/build.md). An
# empty value is the default, but for BUSTER_DEBUG_INFO and BUSTER_LINKER.
set(BUSTER_HOST_ARCH native CACHE STRING "The host ISA: clang's -march (native, x86-64, znver5, ...)")
set(BUSTER_HOST_RUNTIME "" CACHE PATH "A baseline runtime's root to link against (BUSTER_HOST_ARCH=x86-64 alone)")
set(BUSTER_DEBUG_INFO 1 CACHE STRING "1: Release has -g; 0: Release has no debug information")
set(BUSTER_LINKER auto CACHE STRING "auto, system, wild, mold, lld, bfd or an absolute path")
set(BUSTER_BUILD_DEFINES "" CACHE STRING "Arguments every unit's compile adds, split on whitespace")
set(BUSTER_SANITIZER "" CACHE STRING "none, address, undefined, thread or memory; empty: address when fuzzing, else none")
set(BUSTER_SANITIZER_OPTIMIZATION 3 CACHE STRING "A sanitizer build's Release optimization level: 1, 2 or 3")
set(BUSTER_FUZZ_AVAILABLE 0 CACHE STRING "1 builds in libFuzzer and the --fuzz driver")
set(BUSTER_FUZZ_ENTRY "" CACHE FILEPATH "The fuzzing entry's C file (src/buster/compiler/fuzz.c when empty)")
set(BUSTER_BAKE 1 CACHE STRING "0 leaves the backend's tables to be built when the executable runs")
set(BUSTER_PGO 0 CACHE STRING "1 builds Release with a profile of the compiler's own compiles (Release alone)")
set(BUSTER_PGO_TRAIN "" CACHE FILEPATH "A bash script that trains BUSTER_PGO's profile in place of the default compiles")
set(BUSTER_PARALLEL_CODEGEN auto CACHE STRING "auto, 1 or 0: the two longest Release units' native code, and Develop's units, in parallel parts")
set(BUSTER_LEXER_VARIANTS "" CACHE STRING "NAME=SOURCE;... lexers compiled in for benchmark-lexer (build.sh lexer-variant)")
set(BUSTER_SHIFT_SOURCE "" CACHE STRING "The source whose code the shifted links move (build.sh shifts)")
set(BUSTER_SHIFTS "" CACHE STRING "The shifted links' padding sizes in bytes (build.sh shifts)")

include(cmake/BusterSettings.cmake)
buster_settings_normalize()
buster_settings_tools("${CMAKE_C_COMPILER}")
buster_settings_linker()
# The start's code order (#1444, cmake/BusterStartOrder.cmake): in a static link by lld, every unit's functions in
# sections of their own, and the ones small compiles run (cmake/BusterStartOrder.txt) placed together before .text.
set(buster_start_order FALSE)
if(buster_static AND buster_lld)
    set(buster_start_order TRUE)
    include(cmake/BusterStartOrder.cmake)
    set(start_order_list "${CMAKE_SOURCE_DIR}/cmake/BusterStartOrder.txt")
    set(start_order_script "${CMAKE_BINARY_DIR}/start-order.ld")
    set(start_order_symbols "${CMAKE_BINARY_DIR}/start-order.symbols")
    buster_start_order_files("${start_order_list}" "${start_order_script}" "${start_order_symbols}" "${BUSTER_SHIFT_SOURCE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${start_order_list}" "${CMAKE_SOURCE_DIR}/cmake/BusterStartOrder.cmake")
    list(APPEND buster_host_flags -ffunction-sections)
    list(APPEND buster_flags -ffunction-sections)
    list(APPEND buster_linker_flags "-Wl,-T,${start_order_script}" "-Wl,--symbol-ordering-file=${start_order_symbols}" -Wl,--no-warn-symbol-ordering)
endif()
message(STATUS "buster: variant ${buster_variant}, stage 1 linker: ${buster_linker}, static: ${buster_static}, start order: ${buster_start_order}")
if(buster_pgo AND "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "buster: BUSTER_PGO builds the Release configuration alone; configure with CMAKE_CONFIGURATION_TYPES=Release")
endif()
if(buster_pgo AND NOT BUSTER_LEXER_VARIANTS STREQUAL "")
    message(FATAL_ERROR "buster: lexer-variant: BUSTER_PGO builds build/compiler and the shifts' executables alone")
endif()
if(buster_parallel_codegen AND NOT BUSTER_LEXER_VARIANTS STREQUAL "")
    if(buster_parallel_codegen_required)
        message(FATAL_ERROR "buster: BUSTER_PARALLEL_CODEGEN does not yet support lexer variants")
    endif()
    set(buster_parallel_codegen 0)
endif()

find_program(BUSTER_NM NAMES nm llvm-nm NO_CACHE REQUIRED)

# The toolchain's identity, as build.sh records it, for build.sh to see when a tree a preset configured has another
# clang now: CMake itself notices a compiler at another path, but not another file or version at the same one (after
# which `cmake --fresh --preset NAME` configures the tree afresh).
get_filename_component(compiler_file "${CMAKE_C_COMPILER}" REALPATH)
execute_process(COMMAND "${CMAKE_C_COMPILER}" --version OUTPUT_VARIABLE compiler_version)
string(REGEX MATCH "^[^\n]*" compiler_version "${compiler_version}")
file(WRITE "${CMAKE_BINARY_DIR}/buster-toolchain.txt" "clang ${CMAKE_C_COMPILER} (${compiler_file}): ${compiler_version}\n")

# The flags' only differences from a direct clang command are the build's mechanics: CMake's depfile options and
# CMAKE_INTDIR, a definition no source reads, the include directory's absolute path, and -fmacro-prefix-map, which
# keeps __FILE__ relative to the source directory, as it is when clang runs from there (assert's messages).
# Nor does the link take CMake's additions: no RPATH for the baseline runtime's libraries, which the executable has
# never had, and no linker depfile (--dependency-file), which CMake probes on the default linker rather than the one
# BUSTER_LINKER selects. The link's inputs are the objects and the files given by path, which CMake tracks itself.
set(CMAKE_SKIP_RPATH ON)
set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)

# Arguments in one SHELL: group, which keeps them in their order and as many times as they come; CMake would otherwise
# merge repeated options.
function(buster_shell_group output)
    set(group "")
    foreach(argument IN LISTS ARGN)
        string(REPLACE "\\" "\\\\" argument "${argument}")
        string(REPLACE "\"" "\\\"" argument "${argument}")
        string(APPEND group " \"${argument}\"")
    endforeach()
    if(group STREQUAL "")
        set(${output} "" PARENT_SCOPE)
    else()
        set(${output} "SHELL:${group}" PARENT_SCOPE)
    endif()
endfunction()

set(flags ${buster_flags})
list(TRANSFORM flags REPLACE "^-Isrc$" "-I${CMAKE_SOURCE_DIR}/src")
buster_shell_group(flags_group ${flags})

# A lexer-variant build replaces BUSTER_BUILD_DEFINES with the list of the variants it compiles in.
set(lexer_variants "")
set(lexer_names "")
foreach(variant IN LISTS BUSTER_LEXER_VARIANTS)
    if(NOT variant MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.+)$")
        message(FATAL_ERROR "buster: BUSTER_LEXER_VARIANTS' ${variant} isn't NAME=SOURCE")
    endif()
    set(name "${CMAKE_MATCH_1}")
    get_filename_component(path "${CMAKE_MATCH_2}" REALPATH)
    buster_settings_plain("${path}" plain)
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR NOT plain OR name IN_LIST lexer_names)
        message(FATAL_ERROR "buster: BUSTER_LEXER_VARIANTS' ${variant} isn't NAME=SOURCE of a file, each NAME once")
    endif()
    list(APPEND lexer_names "${name}")
    list(APPEND lexer_variants "${name}=${path}")
endforeach()
if(lexer_variants)
    set(variant_list "")
    foreach(name IN LISTS lexer_names)
        string(APPEND variant_list " BUSTER_LEXER_VARIANT(${name})")
    endforeach()
    buster_shell_group(defines_group "-DBUSTER_LEXER_VARIANTS=${variant_list}")
else()
    buster_shell_group(defines_group ${buster_defines})
endif()

# The compile options of a unit of the executable: the Release configuration's debug information and optimization
# (BusterSettings.cmake), the Debug configuration's or the Develop configuration's (Release's optimization with line
# tables), around the flags, then the definitions, as print-compile-flags prints them.
# With BUSTER_PGO, the profile follows the flags, as -fprofile-instr-use; the trained profile is built before the
# units and recompiles them when it changes.
set(pgo "${CMAKE_BINARY_DIR}/pgo")
set(profile_use "")
if(buster_pgo)
    set(profile_use "-fprofile-instr-use=${pgo}/compiler.profdata")
endif()
buster_shell_group(develop_debug_group ${BUSTER_DEVELOP_CONFIGURATION_DEBUG})
function(buster_unit_options target definitions)
    target_compile_options(${target} PRIVATE
        "$<$<CONFIG:Release>:${buster_debug}>"
        "$<$<CONFIG:Debug>:${BUSTER_DEBUG_CONFIGURATION_DEBUG}>"
        "$<$<CONFIG:Develop>:${develop_debug_group}>"
        "${flags_group}"
        ${profile_use}
        "$<IF:$<CONFIG:Debug>,${BUSTER_DEBUG_CONFIGURATION_OPTIMIZATION},${buster_optimization}>"
        ${definitions}
        "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=")
endfunction()

include(cmake/BusterCodegen.cmake)
if(buster_parallel_codegen)
    buster_codegen_tools(codegen_available codegen_missing)
    if(NOT codegen_available AND buster_parallel_codegen_required)
        message(FATAL_ERROR "buster: BUSTER_PARALLEL_CODEGEN=1 needs ${codegen_missing}")
    elseif(NOT codegen_available)
        message(STATUS "buster: parallel native generation is off: no ${codegen_missing}")
        set(buster_parallel_codegen 0)
    else()
        buster_codegen_splitter()
    endif()
endif()
message(STATUS "buster: parallel native generation: ${buster_parallel_codegen}")
# The Develop configuration (docs/commands/build.md, "Develop"): its main.c units in parts before the optimizer where
# the parts' tools are (BusterCodegen.cmake), else whole, and the benchmark units BUSTER_DEVELOP_ON_DEMAND names left
# out of its executable, whose commands then name the configurations that have them.
set(buster_develop FALSE)
if("Develop" IN_LIST CMAKE_CONFIGURATION_TYPES)
    set(buster_develop TRUE)
endif()
# A sanitizer tree's Develop units compile whole: its parts would be instrumented apart, after inlining across parts was
# lost, which Release's parts (after the whole module's pipeline) aren't.
set(buster_develop_parts FALSE)
if(buster_develop AND buster_parallel_codegen AND buster_sanitizer STREQUAL "none")
    set(buster_develop_parts TRUE)
endif()
message(STATUS "buster: Develop configuration: ${buster_develop}, in parts: ${buster_develop_parts}")

# Each source is an object library of its own, defined in the compile order, which is the order a clean build starts
# them in; the executable links their objects in the sources' order.
buster_compile_order(order)
set(develop_absent_sources "")
foreach(index IN LISTS order)
    list(GET BUSTER_SOURCES ${index} source)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(file "${source}" NAME)
    if(buster_develop AND file IN_LIST BUSTER_DEVELOP_ON_DEMAND)
        # No source in Develop: the link names this object library's objects in every configuration, which would
        # otherwise make Develop's executable wait for the unit's compile, though it doesn't link it.
        add_library(buster_${name} OBJECT "$<$<NOT:$<CONFIG:Develop>>:${CMAKE_SOURCE_DIR}/${source}>")
        set_target_properties(buster_${name} PROPERTIES LINKER_LANGUAGE C)
        list(APPEND develop_absent_sources "${CMAKE_SOURCE_DIR}/${source}")
    else()
        add_library(buster_${name} OBJECT "${source}")
    endif()
    buster_unit_options(buster_${name} "${defines_group}")
    buster_codegen_split_unit("${name}" split)
    if(split)
        # This is the ordinary O3 pipeline through optimized IR, not LTO's pre-link pipeline. The source target
        # remains the dependency owner and the sole compile database entry for this C translation unit.
        target_compile_options(buster_${name} PRIVATE "$<$<CONFIG:Release>:-emit-llvm>")
        buster_codegen_unit("${name}")
    endif()
    buster_develop_partitions("${name}" develop_partitions)
    if(NOT develop_partitions EQUAL 0)
        # Develop's unit stops before LLVM's optimizer, which its parts' jobs run (BusterCodegen.cmake).
        target_compile_options(buster_${name} PRIVATE "$<$<CONFIG:Develop>:SHELL:-Xclang -disable-llvm-passes>"
            "$<$<CONFIG:Develop>:-emit-llvm>")
        buster_develop_unit("${name}")
    endif()
endforeach()
set(unit_names "")
foreach(source IN LISTS BUSTER_SOURCES)
    get_filename_component(name "${source}" NAME_WE)
    list(APPEND unit_names ${name})
endforeach()


# Each lexer variant, after the units: a copy of c_lexer.c compiled through c_lexer_variant.c into
# c_lexer_variant_NAME, or that function's assembly (build.sh lexer-variant-assembly), assembled as it is.
set(lexer_objects "")
foreach(variant IN LISTS lexer_variants)
    string(REGEX MATCH "^([^=]+)=(.*)$" variant "${variant}")
    set(name "${CMAKE_MATCH_1}")
    set(path "${CMAKE_MATCH_2}")
    if(path MATCHES "\\.s$")
        set(object "${CMAKE_BINARY_DIR}/lexer-variants/${name}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${CMAKE_C_COMPILER}" -c "${path}" -o "${object}"
            DEPENDS "${path}"
            COMMENT "Assembling the lexer variant ${name}"
            VERBATIM)
        list(APPEND lexer_objects "${object}")
    else()
        add_library(buster_lexer_variant_${name} OBJECT src/buster/compiler/c_lexer_variant.c)
        buster_shell_group(variant_group "-DBUSTER_LEXER_VARIANT_SOURCE=\"${path}\"" "-DBUSTER_LEXER_VARIANT_NAME=c_lexer_variant_${name}")
        buster_unit_options(buster_lexer_variant_${name} "${variant_group}")
        list(APPEND lexer_objects "$<TARGET_OBJECTS:buster_lexer_variant_${name}>")
    endif()
endforeach()

# The link of `target`, an executable of objects: fixed-address (-no-pie) but for TSan's and MSan's PIEs, by the
# selected linker. As a PIE, its 10.6 MB of pointer tables (.data.rel.ro) took 366,684 relative relocations at every
# start, whose 2,800 page faults made a run that does nothing take 8 to 13 ms, where linked at its address it takes
# 1.5 ms with 113 (#86: link-foreign and the tests run it as a process of its own, and a small link's work is 0.8 ms).
# With BUSTER_HOST_RUNTIME, CI's emulated old CPU's runtime, linked explicitly: the host's startup objects and loader
# can require a newer ISA even with -march=x86-64.
# The link's arguments before the objects and after them, without BUSTER_HOST_RUNTIME, which buster_link adds itself:
# into `before` and `after`. libm is a needed library only where the link references it (#652): the compiler's own
# floating operations are host_math.h's and its tests' libm calls go through dlopen (test_libm.c), so that a compile
# doesn't load libm.so.6 at its start, 24 microseconds and 12 page faults on the 9700X; a build whose code still calls
# libm (the baseline host's rounding functions, a fuzzing runtime) takes it.
function(buster_link_arguments before after)
    set(${before} ${buster_link_mode} ${buster_sanitizer_flags} ${buster_linker_flags} PARENT_SCOPE)
    set(${after} ${buster_fuzz_libraries} -Wl,--push-state,--as-needed -lm -Wl,--pop-state -ldl PARENT_SCOPE)
endfunction()

function(buster_link target)
    set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C)
    if(NOT BUSTER_HOST_RUNTIME STREQUAL "")
        if(NOT buster_arch STREQUAL "x86-64")
            message(FATAL_ERROR "buster: BUSTER_HOST_RUNTIME requires BUSTER_HOST_ARCH=x86-64")
        endif()
        set(library "${BUSTER_HOST_RUNTIME}/usr/lib/x86_64-linux-gnu")
        set(support "${BUSTER_HOST_RUNTIME}/usr/lib/gcc/x86_64-linux-gnu/15/libgcc.a")
        target_link_options(${target} PRIVATE ${buster_link_mode} -nostdlib ${buster_linker_flags}
            "${library}/crt1.o" "${library}/crti.o")
        # Paths in link options aren't tracked as inputs, so record these startup objects explicitly.
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${library}/crt1.o" "${library}/crti.o")
        target_link_libraries(${target} PRIVATE -Wl,--push-state,--as-needed "${library}/libm.so.6" -Wl,--pop-state
            "${library}/libc.so.6" "${support}" "${library}/crtn.o")
    else()
        buster_link_arguments(before after)
        target_link_options(${target} PRIVATE ${before})
        target_link_libraries(${target} PRIVATE ${after})
    endif()
    if(buster_start_order)
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${start_order_script}" "${start_order_symbols}")
    endif()
endfunction()

# The post-link step (cmake/BusterPostLink.cmake) of `target`'s linked file into `output`: the backend's tables baked
# in, and the stack canary's absence checked in OBJECTS, the objects it links of the compiler's own code. ARGUMENTS go
# to the step, which also depends on DEPENDS.
function(buster_post_link target output)
    cmake_parse_arguments(PARSE_ARGV 2 post "" "" "ARGUMENTS;DEPENDS;OBJECTS")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_FILE:${target}>" "-DOUTPUT=${output}" "-DBAKE=${buster_bake}"
            "-DNM=${BUSTER_NM}" "-DOBJECTS=${post_OBJECTS}" ${post_ARGUMENTS} -P "${CMAKE_SOURCE_DIR}/cmake/BusterPostLink.cmake"
        DEPENDS ${target} "${CMAKE_SOURCE_DIR}/cmake/BusterPostLink.cmake" ${post_DEPENDS}
        COMMENT "Baking and checking ${target}'s executable"
        VERBATIM)
endfunction()

# BUSTER_PGO's profile of the compiler's own compiles (#880), under pgo/: the sources compiled with
# -fprofile-instr-generate, without debug information, into an executable left unbaked (its counters are writable
# memory outside buster_baked, which bake-tables refuses); that executable compiles each source as -E -P,
# -fsyntax-only and -c (or runs BUSTER_PGO_TRAIN, a bash script, in pgo/train with BUSTER_PGO_COMPILER naming it), a
# profile per process; and llvm-profdata, beside clang or on PATH, merges them into pgo/compiler.profdata, which every
# unit then compiles with. Each step reruns when its inputs change: an edited source retrains the profile, and the new
# profile recompiles every unit. On the 9700X, the compiler's own sources took 3.8 to 5.2% off -c's time
# (docs/compile-time.md).
if(buster_pgo)
    get_filename_component(compiler_directory "${compiler_file}" DIRECTORY)
    find_program(BUSTER_LLVM_PROFDATA NAMES llvm-profdata HINTS "${compiler_directory}" NO_CACHE)
    if(NOT BUSTER_LLVM_PROFDATA)
        message(FATAL_ERROR "buster: BUSTER_PGO needs llvm-profdata")
    endif()
    set(instrumented_objects "")
    foreach(name IN LISTS unit_names)
        list(FIND unit_names "${name}" index)
        list(GET BUSTER_SOURCES ${index} source)
        set(object "${pgo}/objects/${name}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${CMAKE_C_COMPILER}" -c "${CMAKE_SOURCE_DIR}/${source}" -o "${object}" ${flags} -fprofile-instr-generate
                ${buster_optimization} ${buster_defines} "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=" -MD -MF "${object}.d"
            DEPFILE "${object}.d"
            COMMENT "Building ${source} with -fprofile-instr-generate"
            VERBATIM)
        list(APPEND instrumented_objects "${object}")
    endforeach()
    buster_link_arguments(before after)
    add_custom_command(OUTPUT "${pgo}/compiler"
        COMMAND "${CMAKE_C_COMPILER}" ${before} -fprofile-instr-generate ${instrumented_objects} ${after} -o "${pgo}/compiler"
        DEPENDS ${instrumented_objects}
        COMMENT "Linking the instrumented executable"
        VERBATIM)
    set(trained "")
    set(trained_directories "")
    if(buster_pgo_train STREQUAL "")
        foreach(name IN LISTS unit_names)
            list(FIND unit_names "${name}" index)
            list(GET BUSTER_SOURCES ${index} source)
            set(profiles "${pgo}/profiles/${name}")
            set(run "${CMAKE_COMMAND}" -E env "LLVM_PROFILE_FILE=${profiles}/%p.profraw" "${pgo}/compiler")
            add_custom_command(OUTPUT "${pgo}/train/${name}.stamp"
                COMMAND "${CMAKE_COMMAND}" -E rm -rf "${profiles}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${profiles}" "${pgo}/train"
                COMMAND ${run} -E -P "${source}" ${buster_flags} ${buster_defines} -o /dev/null
                COMMAND ${run} -fsyntax-only "${source}" ${buster_flags} ${buster_defines}
                COMMAND ${run} -c "${source}" ${buster_flags} ${buster_defines} -o "${pgo}/train/${name}.o"
                COMMAND "${CMAKE_COMMAND}" -E touch "${pgo}/train/${name}.stamp"
                DEPENDS "${pgo}/compiler" "${CMAKE_SOURCE_DIR}/${source}"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                COMMENT "Training the profile on ${source}"
                VERBATIM)
            list(APPEND trained "${pgo}/train/${name}.stamp")
            if(trained_directories STREQUAL "")
                set(trained_directories "${name}")
            else()
                string(APPEND trained_directories ",${name}")
            endif()
        endforeach()
    else()
        add_custom_command(OUTPUT "${pgo}/train/script.stamp"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${pgo}/profiles" "${pgo}/train"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${pgo}/profiles/script" "${pgo}/train"
            COMMAND "${CMAKE_COMMAND}" -E env "LLVM_PROFILE_FILE=${pgo}/profiles/script/%p.profraw"
                "BUSTER_PGO_COMPILER=${pgo}/compiler" bash "${buster_pgo_train}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${pgo}/train/script.stamp"
            DEPENDS "${pgo}/compiler" "${buster_pgo_train}"
            WORKING_DIRECTORY "${pgo}"
            COMMENT "Training the profile with ${buster_pgo_train}"
            VERBATIM)
        set(trained "${pgo}/train/script.stamp")
        set(trained_directories script)
    endif()
    add_custom_command(OUTPUT "${pgo}/compiler.profdata"
        COMMAND "${CMAKE_COMMAND}" "-DLLVM_PROFDATA=${BUSTER_LLVM_PROFDATA}" "-DPROFILES=${pgo}/profiles"
            "-DDIRECTORIES=${trained_directories}" "-DOUTPUT=${pgo}/compiler.profdata"
            -P "${CMAKE_SOURCE_DIR}/cmake/BusterMergeProfile.cmake"
        DEPENDS ${trained} "${CMAKE_SOURCE_DIR}/cmake/BusterMergeProfile.cmake"
        COMMENT "Merging the profile"
        VERBATIM)
    add_custom_target(pgo-profile DEPENDS "${pgo}/compiler.profdata")
    foreach(name IN LISTS unit_names)
        list(FIND unit_names "${name}" index)
        list(GET BUSTER_SOURCES ${index} source)
        set_source_files_properties("${source}" PROPERTIES OBJECT_DEPENDS "${pgo}/compiler.profdata")
        add_dependencies(buster_${name} pgo-profile)
    endforeach()
endif()

set(objects "")
foreach(name IN LISTS unit_names)
    buster_native_object(object "${name}")
    list(APPEND objects "${object}")
endforeach()
# Develop's stand-ins for the benchmark units it leaves out (cmake/BusterDevelopAbsent.cmake): each of their run_*
# entry points says which configurations have its command.
if(develop_absent_sources)
    set(absent "${CMAKE_BINARY_DIR}/$<CONFIG>/develop/absent.o")
    add_custom_command(OUTPUT "${absent}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${absent}" "-DSOURCES=${develop_absent_sources}" "-DCLANG=${CMAKE_C_COMPILER}"
            "-DFLAGS=${flags};${buster_optimization}" -P "${CMAKE_SOURCE_DIR}/cmake/BusterDevelopAbsent.cmake"
        DEPENDS ${develop_absent_sources} "${CMAKE_SOURCE_DIR}/cmake/BusterDevelopAbsent.cmake"
        COMMENT "Writing Develop's stand-ins for the benchmark units"
        VERBATIM)
    # Joined to the last object, so that Release's and Debug's lists, the post-link step's argument among them, stay
    # as they were rather than gain an empty element.
    list(POP_BACK objects last)
    list(APPEND objects "${last}$<$<CONFIG:Develop>:$<SEMICOLON>${CMAKE_BINARY_DIR}/Develop/develop/absent.o>")
endif()
set(compiler "${CMAKE_BINARY_DIR}/$<CONFIG>/compiler")
add_executable(compiler_link ${objects} ${lexer_objects})
set_target_properties(compiler_link PROPERTIES OUTPUT_NAME compiler.link)
buster_link(compiler_link)
buster_post_link(compiler_link "${compiler}" OBJECTS ${objects} ${lexer_objects})
add_custom_target(compiler ALL DEPENDS "${compiler}")

# Filter the multi-config database only when CMake regenerates it or the filter changes. Keeping this in Ninja's graph
# avoids parsing the whole JSON again after every unchanged build or source edit. Direct CMake builds may leave
# export disabled; build.sh and the presets enable it and publish the selected configuration's cached copy.
function(buster_compile_commands target)
    if(CMAKE_EXPORT_COMPILE_COMMANDS)
        set(commands "${CMAKE_BINARY_DIR}/$<CONFIG>/compile_commands.json")
        add_custom_command(OUTPUT "${commands}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_BINARY_DIR}/compile_commands.json" "-DOUTPUT=${commands}"
                "-DCONFIG=$<CONFIG>" -P "${CMAKE_SOURCE_DIR}/cmake/BusterCompileCommands.cmake"
            DEPENDS "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_SOURCE_DIR}/cmake/BusterCompileCommands.cmake"
            COMMENT "Selecting $<CONFIG> compile commands"
            VERBATIM)
        add_custom_target(${target}_compile_commands DEPENDS "${commands}")
        add_dependencies(${target} ${target}_compile_commands)
    endif()
endfunction()
buster_compile_commands(compiler)

# build.sh shifts: the same objects linked again with BUSTER_SHIFTS' bytes of padding in .text before
# BUSTER_SHIFT_SOURCE's (cmake/BusterShiftPadding.cmake), into CONFIG/shifts/compiler-SHIFT, for time-runs to compare
# builds over their code's placements: the same code ran up to 2% faster or slower so on the 9700X (#509).
set(shifted "")
if(NOT BUSTER_SHIFTS STREQUAL "")
    # A shift given twice is linked once.
    set(shifts ${BUSTER_SHIFTS})
    list(REMOVE_DUPLICATES shifts)
    get_filename_component(shift_name "${BUSTER_SHIFT_SOURCE}" NAME_WE)
    list(FIND unit_names "${shift_name}" shift_index)
    if(shift_index EQUAL -1 OR NOT BUSTER_SHIFT_SOURCE STREQUAL "${shift_name}.c")
        message(FATAL_ERROR "build.sh shifts: ${BUSTER_SHIFT_SOURCE} isn't a source's file name")
    endif()
    find_program(BUSTER_READELF NAMES readelf llvm-readelf NO_CACHE REQUIRED)
    buster_native_object(shift_object "${shift_name}")
    foreach(bytes IN LISTS shifts)
        if(NOT bytes MATCHES "^(0|[1-9][0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?)$")
            message(FATAL_ERROR "build.sh shifts: ${bytes} isn't a shift of 0 to 9,999,999 bytes")
        endif()
        set(directory "${CMAKE_BINARY_DIR}/$<CONFIG>/shifts")
        set(padding "${directory}/padding-${bytes}.o")
        add_custom_command(OUTPUT "${padding}"
            COMMAND "${CMAKE_COMMAND}" "-DOBJECT=${shift_object}" "-DSOURCE=${BUSTER_SHIFT_SOURCE}" "-DSHIFT=${bytes}"
                "-DOUTPUT=${padding}" "-DCLANG=${CMAKE_C_COMPILER}" "-DREADELF=${BUSTER_READELF}"
                -P "${CMAKE_SOURCE_DIR}/cmake/BusterShiftPadding.cmake"
            DEPENDS "${shift_object}" "${CMAKE_SOURCE_DIR}/cmake/BusterShiftPadding.cmake"
            VERBATIM)
        set(shift_objects ${objects})
        list(INSERT shift_objects ${shift_index} "${padding}")
        add_executable(compiler_shift_${bytes} EXCLUDE_FROM_ALL ${shift_objects})
        set_target_properties(compiler_shift_${bytes} PROPERTIES OUTPUT_NAME compiler-${bytes}.link
            RUNTIME_OUTPUT_DIRECTORY "${directory}")
        buster_link(compiler_shift_${bytes})
        set(output "${directory}/compiler-${bytes}")
        if(bytes EQUAL 0)
            buster_post_link(compiler_shift_${bytes} "${output}" ARGUMENTS -DPADDING=1 OBJECTS ${shift_objects})
        else()
            buster_post_link(compiler_shift_${bytes} "${output}" ARGUMENTS -DPADDING=1 "-DUNSHIFTED=${compiler}"
                DEPENDS "${compiler}" OBJECTS ${shift_objects})
        endif()
        list(APPEND shifted "${output}")
    endforeach()
    add_custom_target(shifts DEPENDS ${shifted})
    add_dependencies(shifts compiler)
endif()

# check-call-graph: each unit's LLVM IR, before any LLVM pass, at the Release configuration's optimization, with the
# flags but debug information and BUSTER_BUILD_DEFINES, and without sanitizer instrumentation, whose generated globals
# and runtime calls are not compiler state or source recursion (cmake/BusterCallGraph.cmake). The IR is the same for
# both configurations; the check runs the configuration's executable on it.
set(call_graph "${CMAKE_BINARY_DIR}/call-graph")
set(captures "")
function(buster_capture source output)
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_C_COMPILER}" -S -emit-llvm ${buster_optimization} -Xclang -disable-llvm-passes ${flags}
            "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=" -fno-sanitize=all "${CMAKE_SOURCE_DIR}/${source}" -o "${output}"
            -MD -MF "${output}.d"
        DEPFILE "${output}.d"
        COMMENT "Capturing ${source}'s call graph"
        VERBATIM)
    set(captures ${captures} "${output}" PARENT_SCOPE)
endfunction()
buster_capture(src/buster/compiler/call_graph_capture/inline.c "${call_graph}/capture/inline.ll")
buster_capture(src/buster/compiler/call_graph_capture/strong.c "${call_graph}/capture/strong.ll")
foreach(index IN LISTS order)
    list(GET BUSTER_SOURCES ${index} source)
    get_filename_component(name "${source}" NAME_WE)
    buster_capture("${source}" "${call_graph}/${name}.ll")
endforeach()
add_custom_target(call-graph-ir DEPENDS ${captures})
add_custom_target(check-call-graph
    COMMAND "${CMAKE_COMMAND}" "-DCOMPILER=${compiler}" "-DIR=${call_graph}"
        -P "${CMAKE_SOURCE_DIR}/cmake/BusterCallGraph.cmake"
    DEPENDS ${captures} "${compiler}"
    COMMENT "Checking the no-recursion rule on the call graph"
    VERBATIM USES_TERMINAL)
add_dependencies(check-call-graph compiler)

# Tests (CTest): `build` checks what the build made and needs nothing else; `correctness` runs the whole correctness
# inventory, tools/ci/correctness.tests, through the configuration's executable's parallel runner, which gives each
# command that executable as BUSTER_COMPILER. The inventory needs CI's tools and frozen inputs (docs/ci.md), and its
# commands take every core, so it runs alone. Its commands' own ./build.sh calls (print-*, check-call-graph) get this
# tree's settings.
enable_testing()
set(settings_environment
    "BUSTER_HOST_ARCH=${BUSTER_HOST_ARCH}"
    "BUSTER_HOST_RUNTIME=${BUSTER_HOST_RUNTIME}"
    "BUSTER_DEBUG_INFO=${BUSTER_DEBUG_INFO}"
    "BUSTER_LINKER=${BUSTER_LINKER}"
    "BUSTER_BUILD_DEFINES=${BUSTER_BUILD_DEFINES}"
    "BUSTER_SANITIZER=${BUSTER_SANITIZER}"
    "BUSTER_SANITIZER_OPTIMIZATION=${BUSTER_SANITIZER_OPTIMIZATION}"
    "BUSTER_FUZZ_AVAILABLE=${BUSTER_FUZZ_AVAILABLE}"
    "BUSTER_FUZZ_ENTRY=${BUSTER_FUZZ_ENTRY}"
    "BUSTER_BAKE=${BUSTER_BAKE}"
    "BUSTER_PGO=${BUSTER_PGO}"
    "BUSTER_PGO_TRAIN=${BUSTER_PGO_TRAIN}"
    "BUSTER_PARALLEL_CODEGEN=${BUSTER_PARALLEL_CODEGEN}")
add_test(NAME baked-tables COMMAND "${compiler}" test-baked-tables)
add_test(NAME parallel-runner COMMAND "${compiler}" test-parallel)
set_tests_properties(baked-tables parallel-runner PROPERTIES LABELS build)
add_test(NAME correctness COMMAND "${compiler}" parallel --timeout 600 "${CMAKE_SOURCE_DIR}/tools/ci/correctness.tests"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(correctness PROPERTIES LABELS correctness RUN_SERIAL TRUE TIMEOUT 21600
    ENVIRONMENT "${settings_environment}")
