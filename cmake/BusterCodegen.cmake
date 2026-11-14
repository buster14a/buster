# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The units in parts (docs/commands/build.md, "Units in parts"; docs/stage1-build-design.md, #2559). In the Release
# and Develop configurations every main.c unit compiles C to LLVM bitcode without any LLVM pass, retaining CMake's
# depfile and compile database; tools/codegen/presplit.c deals its functions into parts by file; Ninja schedules a job
# a part, which runs the O3 pipeline and the code generator on its part; a relocatable link joins the parts in order
# and restores the source unit's public symbols before the ordinary final link. A rebuild repeats only the parts whose
# bitcode changed. The configurations differ in what a part reads of the others and in their debug information:
# Release reads bodies of up to 300 unoptimized instructions where they are called, keeps the whole unit's inlining of
# static functions (--keep-statics) and its benchmarks' kernels (--benchmark-bodies), and keeps its -g (the design's
# step 5); Develop reads up to 100, with line tables and the tests' own functions optnone (steps 1 to 3). No C boundary
# or private state moves between source units. The Debug configuration compiles every unit whole.
#
# A sanitizer build's Release instead generates the two longest units' native code in parallel after the ordinary O3
# IR pipeline (#2058, #2639): their object targets compile C through O3 to bitcode, tools/codegen/split.c partitions
# that optimized IR in module order, and the same join restores the unit's symbols. The code is the single object's,
# function for function, instrumented by the whole module's pipeline; its Develop compiles every unit whole.

# split.c's code parts per unit, besides the part of its global variables: one per job of build.sh's default, a
# physical core of the 9700X (8). Fourteen, one per hardware thread, ran in two waves there and took 74.0 s against
# 73.1 s.
set(BUSTER_CODEGEN_PARTITIONS 8)

# The code parts of each main.c unit in parts, besides the part of its global variables: about 50,000 unoptimized
# instructions each in the checker's unit (the design's rule 8, the one count measured: 48), a share of that in the
# others by their size on the 9700X (#2559's record): the backend's two thirds, the front end's a third, the rest 8.
set(BUSTER_PARTITIONS main_unit_checker=48 main_unit_backend=32 main_unit_frontend=16)
set(BUSTER_DEFAULT_PARTITIONS 8)
# presplit's options, the design's (docs/stage1-build-design.md, "The splitter's rules"): bodies of up to LIMIT
# unoptimized instructions read where they are called, chunks at one name in 24, single callers' trees up to 150,000
# instructions, each file's functions over a run of parts sized at half the unit's mean. Release reads bodies of up to
# 300 instructions and keeps the whole unit's inlining of static functions and benchmarks' kernels (step 5's gate);
# Develop up to 100, for a shorter edit, with the tests' functions optnone.
set(BUSTER_RELEASE_SPLIT_OPTIONS --import 200 --simplified-size --anchor 24 --cohere 150000 --spread 2 --keep-statics --benchmark-bodies)
set(BUSTER_DEVELOP_SPLIT_OPTIONS --import 100 --anchor 24 --cohere 150000 --spread 2 --tests-optnone)

# The directories buster_codegen_system_libraries looks in for a library: the linker's default search path on the
# Linux/x86-64 hosts the build supports. build-regression sets its own.
if(NOT DEFINED BUSTER_CODEGEN_LIBRARY_DIRECTORIES)
    set(BUSTER_CODEGEN_LIBRARY_DIRECTORIES /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /lib/x86_64-linux-gnu /lib64 /lib)
endif()

# llvm-config's system libraries (ARGN) as this host links them, into `output`: one it names by a path the host lacks
# links by its name, from the host's library path, where the host has its development file (libNAME.so or .a);
# without one, a host with its runtime library alone (libNAME.so.N) links that file by its path, as a session's
# container with zstd's runtime and not its development package does (#2628). The official release's llvm-config names
# its build machine's static zstd, /usr/lib/x86_64-linux-gnu/libzstd.a (Ubuntu's), which the 9700X, an Arch host, has
# as /usr/lib/libzstd.so. buster_codegen_tools' probe decides whether the result links; build-regression checks the
# mapping.
function(buster_codegen_system_libraries output)
    set(libraries "")
    foreach(library IN LISTS ARGN)
        if(IS_ABSOLUTE "${library}" AND NOT EXISTS "${library}" AND library MATCHES "/lib([^/]+)\\.(a|so)(\\.[0-9]+)*$")
            set(name "${CMAKE_MATCH_1}")
            set(library "-l${name}")
            set(development FALSE)
            set(runtime "")
            foreach(directory IN LISTS BUSTER_CODEGEN_LIBRARY_DIRECTORIES)
                if(EXISTS "${directory}/lib${name}.so" OR EXISTS "${directory}/lib${name}.a")
                    set(development TRUE)
                endif()
                if(runtime STREQUAL "")
                    file(GLOB runtime "${directory}/lib${name}.so.[0-9]*")
                    list(SORT runtime)
                endif()
            endforeach()
            if(NOT development AND NOT runtime STREQUAL "")
                list(GET runtime 0 library)
            endif()
        endif()
        list(APPEND libraries "${library}")
    endforeach()
    set(${output} "${libraries}" PARENT_SCOPE)
endfunction()

# Finds the tools: llvm-config, clang++, llvm-nm, llvm-objcopy, llvm-readobj and ld.lld matching the configured Clang,
# beside it or on PATH, and LLVM's C API headers. Sets `available`, and `missing` to what is missing or mismatched.
# llvm-readobj, not its llvm-readelf alias, which a trimmed toolchain (CI's hosted one) leaves out; the merge asks it
# for readelf's output style. LLVM's libraries link shared where llvm-config finds them, else static (the official
# release ships only the archives, which CI's hosted install and the 9700X's staged tree keep), only the components the
# splitter uses.
function(buster_codegen_tools available missing)
    get_filename_component(directory "${compiler_file}" DIRECTORY)
    set(absent "")
    foreach(tool CONFIG CXX NM OBJCOPY READELF LLD)
        if(tool STREQUAL "LLD")
            set(name ld.lld)
        elseif(tool STREQUAL "CXX")
            set(name clang++)
        elseif(tool STREQUAL "READELF")
            set(name llvm-readobj)
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
        foreach(query cflags cxxflags ldflags includedir)
            execute_process(COMMAND "${found_CONFIG}" "--${query}" OUTPUT_VARIABLE value RESULT_VARIABLE status
                OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 30)
            if(NOT status EQUAL 0)
                list(APPEND absent "llvm-config --${query}")
            endif()
            separate_arguments(value UNIX_COMMAND "${value}")
            set(BUSTER_CODEGEN_LLVM_${query} "${value}" PARENT_SCOPE)
            set(llvm_${query} "${value}")
        endforeach()
        set(llvm_libs "")
        foreach(mode link-shared link-static)
            if(llvm_libs STREQUAL "")
                execute_process(COMMAND "${found_CONFIG}" "--${mode}" --libs core bitreader bitwriter analysis passes transformutils
                    OUTPUT_VARIABLE libraries RESULT_VARIABLE libraries_status ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 30)
                execute_process(COMMAND "${found_CONFIG}" "--${mode}" --system-libs
                    OUTPUT_VARIABLE system RESULT_VARIABLE system_status ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 30)
                if(libraries_status EQUAL 0 AND system_status EQUAL 0 AND NOT libraries STREQUAL "")
                    separate_arguments(llvm_libs UNIX_COMMAND "${libraries}")
                    separate_arguments(system UNIX_COMMAND "${system}")
                    buster_codegen_system_libraries(llvm_system_libs ${system})
                endif()
            endif()
        endforeach()
        if(llvm_libs STREQUAL "")
            list(APPEND absent "LLVM libraries llvm-config can link, shared or static")
        endif()
        set(BUSTER_CODEGEN_LLVM_libs "${llvm_libs}" PARENT_SCOPE)
        set(BUSTER_CODEGEN_LLVM_system_libs "${llvm_system_libs}" PARENT_SCOPE)
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

# The splitters, built once for every configuration with the LLVM they read, at configure-time flags llvm-config gives:
# split (a sanitizer build's Release, after the optimizer) and presplit (the units in parts, before it), which share
# split_bitcode.cpp; and comdat, which the join runs and which reads ELF alone.
function(buster_codegen_splitter)
    set(directory "${CMAKE_BINARY_DIR}/codegen-tools")
    set(splitter "${directory}/split")
    set(presplitter "${directory}/presplit")
    set(comdat "${directory}/comdat")
    get_filename_component(tools "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/codegen" ABSOLUTE)
    add_custom_command(OUTPUT "${comdat}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
        COMMAND "${CMAKE_C_COMPILER}" -O2 -Wall -Wextra -Werror "${tools}/comdat.c" -o "${comdat}"
        DEPENDS "${tools}/comdat.c"
        COMMENT "Building the joins' comdat restorer"
        VERBATIM)
    set(link "${BUSTER_CODEGEN_LLVM_ldflags}" "${BUSTER_CODEGEN_LLVM_libs}" "${BUSTER_CODEGEN_LLVM_system_libs}")
    add_custom_command(OUTPUT "${directory}/split_bitcode.o"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
        COMMAND "${BUSTER_CODEGEN_CXX}" -O2 -Wall -Werror ${BUSTER_CODEGEN_LLVM_cxxflags}
            -c "${tools}/split_bitcode.cpp" -o "${directory}/split_bitcode.o"
        DEPENDS "${tools}/split_bitcode.cpp" "${BUSTER_CODEGEN_CONFIG}"
        COMMENT "Building the splitters' LLVM calls"
        VERBATIM)
    foreach(tool split presplit)
        add_custom_command(OUTPUT "${directory}/${tool}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
            COMMAND "${CMAKE_C_COMPILER}" -O2 -Wall -Wextra -Werror ${BUSTER_CODEGEN_LLVM_cflags}
                -c "${tools}/${tool}.c" -o "${directory}/${tool}.o"
            COMMAND "${BUSTER_CODEGEN_CXX}" -o "${directory}/${tool}" "${directory}/${tool}.o" "${directory}/split_bitcode.o" ${link}
            DEPENDS "${tools}/${tool}.c" "${directory}/split_bitcode.o" "${BUSTER_CODEGEN_CONFIG}"
            COMMENT "Building the IR splitter ${tool}"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endforeach()
    add_custom_target(buster_codegen_splitter DEPENDS "${splitter}" "${presplitter}" "${comdat}")
    set(BUSTER_CODEGEN_SPLIT "${splitter}" PARENT_SCOPE)
    set(BUSTER_CODEGEN_PRESPLIT "${presplitter}" PARENT_SCOPE)
    set(BUSTER_CODEGEN_COMDAT "${comdat}" PARENT_SCOPE)
endfunction()

# Whether unit `name` is split after the optimizer (split.c), into `output`: the two longest units of a sanitizer build's
# Release (buster_split_parts, which CMakeLists.txt sets).
function(buster_codegen_split_unit name output)
    if(buster_split_parts AND name MATCHES "^main_unit_(checker|backend)$")
        set(${output} TRUE PARENT_SCOPE)
    else()
        set(${output} FALSE PARENT_SCOPE)
    endif()
endfunction()

# The code parts of unit `name` in parts (presplit.c), into `output`: 0 for a unit that compiles whole (the units of
# their own), or when the build has no units in parts (buster_unit_parts, which CMakeLists.txt sets: none in a
# sanitizer build, or one without the splitter's tools).
function(buster_unit_partitions name output)
    set(parts 0)
    if(buster_unit_parts AND name MATCHES "^main(_unit_[a-z0-9_]+)?$")
        set(parts ${BUSTER_DEFAULT_PARTITIONS})
        foreach(entry IN LISTS BUSTER_PARTITIONS)
            if(entry MATCHES "^${name}=([0-9]+)$")
                set(parts ${CMAKE_MATCH_1})
            endif()
        endforeach()
    endif()
    set(${output} ${parts} PARENT_SCOPE)
endfunction()

# Whether the final link merges the objects' identical constants and strings (buster_linker_merges, from
# BusterSettings.cmake), into `output`: 1 or 0, which the join (BusterCodegenMerge.cmake) reads. Without the setting
# (the build regression's fixture), 0.
function(buster_codegen_merges output)
    if(buster_linker_merges)
        set(${output} 1 PARENT_SCOPE)
    else()
        set(${output} 0 PARENT_SCOPE)
    endif()
endfunction()

# The unit's object for the link: in Release and Develop, the joined parts of a unit in parts; in a sanitizer build's
# Release, those of a unit split after the optimizer; else its object.
function(buster_native_object output name)
    buster_codegen_split_unit("${name}" split)
    buster_unit_partitions("${name}" parts)
    set(object "$<TARGET_OBJECTS:buster_${name}>")
    if(NOT parts EQUAL 0)
        set(object "$<IF:$<CONFIG:Debug>,${object},${CMAKE_BINARY_DIR}/$<CONFIG>/parts/${name}/native.o>")
    elseif(split)
        set(object "$<IF:$<CONFIG:Release>,${CMAKE_BINARY_DIR}/Release/codegen/${name}/native.o,${object}>")
    endif()
    set(${output} "${object}" PARENT_SCOPE)
endfunction()

# The split, the parts' native generation and the join of unit `name`, whose Release object is bitcode. `options` are
# the parts' code-generation options (the unit's -march and debug information).
function(buster_codegen_unit name)
    set(scripts "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    buster_codegen_merges(merges)
    # The link uses every unit's object, and CMake makes a command whose output the link uses wait for all of them as
    # well (implicit order-only dependencies): the backend's parts, ready at 43.7 s on the 9700X, waited for the
    # checker's bitcode until 58.2 s and then competed with the checker's parts. These commands name every input they
    # read, so with CMake 3.27 and later they depend on those alone.
    set(explicit "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
        set(explicit DEPENDS_EXPLICIT_ONLY)
    endif()
    set(directory "${CMAKE_BINARY_DIR}/$<CONFIG>/codegen/${name}")
    set(parts "")
    foreach(part RANGE ${BUSTER_CODEGEN_PARTITIONS})
        list(APPEND parts "${directory}/part${part}.bc")
    endforeach()
    set(exports "${directory}/exports.txt")
    set(locals "${directory}/locals.txt")
    set(stamp "${directory}/split.stamp")
    add_custom_command(OUTPUT "${stamp}"
        BYPRODUCTS ${parts} "${exports}" "${locals}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_OBJECTS:buster_${name}>" "-DDIRECTORY=${directory}"
            "-DSPLIT=${BUSTER_CODEGEN_SPLIT}" "-DNM=${BUSTER_CODEGEN_NM}" "-DPARTITIONS=${BUSTER_CODEGEN_PARTITIONS}"
            -P "${scripts}/BusterCodegenSplit.cmake"
        DEPENDS "$<TARGET_OBJECTS:buster_${name}>" "${BUSTER_CODEGEN_SPLIT}" "${BUSTER_CODEGEN_NM}"
            "${scripts}/BusterCodegenSplit.cmake"
        COMMENT "Partitioning ${name}'s optimized IR"
        ${explicit}
        VERBATIM)
    # The target attaches the split to the build, which then knows the parts' rule; the parts' own commands depend on
    # their bitcode alone.
    add_custom_target(buster_${name}_split DEPENDS "${stamp}")

    # The parts' native code: the same driver's target and PIC defaults, CPU choice, instruction selection and
    # machine-code pipeline as the unit's own compile (its optimization level, and a sanitizer build's flags, with its
    # PIE), with the IR optimizer disabled, since the module has passed it: the sanitizers' instrumentation with it.
    # A part depends on its bitcode alone, which the split rewrites only when it changed (Ninja's restat).
    set(debug "")
    if(NOT "${buster_debug}" STREQUAL "")
        set(debug "$<$<CONFIG:Release>:${buster_debug}>")
    endif()
    set(optimization -O3)
    if(NOT "${buster_optimization}" STREQUAL "")
        set(optimization ${buster_optimization})
    endif()
    set(objects "")
    foreach(part RANGE ${BUSTER_CODEGEN_PARTITIONS})
        set(object "${directory}/part${part}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${CMAKE_C_COMPILER}" -x ir ${buster_host_flags} ${debug} -fno-stack-protector ${buster_sanitizer_flags}
                ${optimization} -Wall -Werror -Xclang -disable-llvm-passes -Wno-unused-command-line-argument
                -c "${directory}/part${part}.bc" -o "${object}"
            DEPENDS "${directory}/part${part}.bc"
            COMMENT "Generating ${name}'s native part ${part}"
            COMMAND_EXPAND_LISTS
            ${explicit}
            VERBATIM)
        list(APPEND objects "${object}")
    endforeach()
    set(native "${directory}/native.o")
    add_custom_command(OUTPUT "${native}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${native}" "-DEXPORTS=${exports}" "-DLOCALS=${locals}" "-DOBJECTS=${objects}"
            "-DLLD=${BUSTER_CODEGEN_LLD}" "-DOBJCOPY=${BUSTER_CODEGEN_OBJCOPY}" "-DNM=${BUSTER_CODEGEN_NM}"
            "-DREADELF=${BUSTER_CODEGEN_READELF}" "-DCOMDAT=${BUSTER_CODEGEN_COMDAT}" "-DMERGES=${merges}"
            -P "${scripts}/BusterCodegenMerge.cmake"
        DEPENDS ${objects} "${exports}" "${locals}" "${BUSTER_CODEGEN_LLD}" "${BUSTER_CODEGEN_OBJCOPY}" "${BUSTER_CODEGEN_NM}"
            "${BUSTER_CODEGEN_READELF}" "${BUSTER_CODEGEN_COMDAT}" "${scripts}/BusterCodegenMerge.cmake"
        COMMENT "Combining ${name}'s native parts"
        ${explicit}
        VERBATIM)
endfunction()

# Unit `name` in parts, in Release and Develop: its object target compiles C to bitcode without any LLVM pass, which
# tools/codegen/presplit.c splits into CONFIG/parts/NAME/ with the configuration's options; a part's job runs the
# unit's -march, the configuration's debug information and the O3 pipeline and code generator on its part, and depends
# on its bitcode alone, which the split rewrites only when it changed (Ninja's restat); the join is split.c's. A Release
# build without debug information (BUSTER_DEBUG_INFO=0, the 9700X job's and the hosted rows') compiles the unit with
# line tables, which name each function's file for the plan, and presplit writes the parts without them
# (--strip-debug).
function(buster_parts_unit name)
    set(scripts "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    buster_codegen_merges(merges)
    # As in buster_codegen_unit: the commands name every input they read, so that they wait for those alone.
    set(explicit "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
        set(explicit DEPENDS_EXPLICIT_ONLY)
    endif()
    set(release_options ${BUSTER_RELEASE_SPLIT_OPTIONS})
    if("${buster_debug}" STREQUAL "")
        target_compile_options(buster_${name} PRIVATE "$<$<CONFIG:Release>:-gline-tables-only>")
        list(APPEND release_options --strip-debug)
    endif()
    target_compile_options(buster_${name} PRIVATE "$<$<NOT:$<CONFIG:Debug>>:SHELL:-Xclang -disable-llvm-passes>"
        "$<$<NOT:$<CONFIG:Debug>>:-emit-llvm>")
    buster_unit_partitions("${name}" partitions)
    set(directory "${CMAKE_BINARY_DIR}/$<CONFIG>/parts/${name}")
    set(parts "")
    foreach(part RANGE ${partitions})
        list(APPEND parts "${directory}/part${part}.bc")
    endforeach()
    set(exports "${directory}/exports.txt")
    set(locals "${directory}/locals.txt")
    set(stamp "${directory}/split.stamp")
    add_custom_command(OUTPUT "${stamp}"
        BYPRODUCTS ${parts} "${exports}" "${locals}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=$<TARGET_OBJECTS:buster_${name}>" "-DDIRECTORY=${directory}"
            "-DSPLIT=${BUSTER_CODEGEN_PRESPLIT}" "-DNM=${BUSTER_CODEGEN_NM}" "-DPARTITIONS=${partitions}"
            "-DOPTIONS=$<IF:$<CONFIG:Develop>,${BUSTER_DEVELOP_SPLIT_OPTIONS},${release_options}>"
            -P "${scripts}/BusterCodegenSplit.cmake"
        DEPENDS "$<TARGET_OBJECTS:buster_${name}>" "${BUSTER_CODEGEN_PRESPLIT}" "${BUSTER_CODEGEN_NM}"
            "${scripts}/BusterCodegenSplit.cmake"
        COMMENT "Partitioning ${name}'s IR before the optimizer"
        ${explicit}
        VERBATIM)
    # The target attaches the split to the build, which then knows the parts' rule; the parts' own commands depend on
    # their bitcode alone.
    add_custom_target(buster_${name}_parts_split DEPENDS "${stamp}")
    set(debug "$<IF:$<CONFIG:Develop>,${BUSTER_DEVELOP_CONFIGURATION_DEBUG},${buster_debug}>")
    set(objects "")
    foreach(part RANGE ${partitions})
        set(object "${directory}/part${part}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${CMAKE_C_COMPILER}" -x ir ${buster_host_flags} "${debug}" -fno-stack-protector ${buster_optimization}
                -Wall -Werror -Wno-unused-command-line-argument -c "${directory}/part${part}.bc" -o "${object}"
            DEPENDS "${directory}/part${part}.bc"
            COMMENT "Optimizing and generating ${name}'s part ${part}"
            COMMAND_EXPAND_LISTS
            ${explicit}
            VERBATIM)
        list(APPEND objects "${object}")
    endforeach()
    set(native "${directory}/native.o")
    add_custom_command(OUTPUT "${native}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${native}" "-DEXPORTS=${exports}" "-DLOCALS=${locals}" "-DOBJECTS=${objects}"
            "-DLLD=${BUSTER_CODEGEN_LLD}" "-DOBJCOPY=${BUSTER_CODEGEN_OBJCOPY}" "-DNM=${BUSTER_CODEGEN_NM}"
            "-DREADELF=${BUSTER_CODEGEN_READELF}" "-DCOMDAT=${BUSTER_CODEGEN_COMDAT}" "-DMERGES=${merges}"
            -P "${scripts}/BusterCodegenMerge.cmake"
        DEPENDS ${objects} "${exports}" "${locals}" "${BUSTER_CODEGEN_LLD}" "${BUSTER_CODEGEN_OBJCOPY}" "${BUSTER_CODEGEN_NM}"
            "${BUSTER_CODEGEN_READELF}" "${BUSTER_CODEGEN_COMDAT}" "${scripts}/BusterCodegenMerge.cmake"
        COMMENT "Combining ${name}'s parts"
        ${explicit}
        VERBATIM)
endfunction()
