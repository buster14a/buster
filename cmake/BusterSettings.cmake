# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
#
# The build's description, which CMakeLists.txt builds and cmake/BusterPrint.cmake prints (build.sh's print-sources,
# print-compile-flags and print-sanitizer-flags): the sources, in the order they are linked, and the flags that the
# BUSTER_* settings select. Nothing else lists them. A setting unset or empty takes its default, but for
# BUSTER_DEBUG_INFO and BUSTER_LINKER, for which an empty value is an error, as build.sh's environment interface has
# always had them (docs/commands/build.md).

include_guard(GLOBAL)

get_filename_component(BUSTER_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Fails with `text`, one line: CMake's errors wrap theirs, which a reader matching the message, or a person, would
# find split.
function(buster_settings_fail text)
    message(NOTICE "buster: ${text}")
    message(FATAL_ERROR "buster: the settings above are invalid")
endfunction()

# Whether `text` can be one argument of the units' compiles: no quote, backslash or newline, which a string literal
# or the shell would read apart, no semicolon, which separates CMake's list elements, and no $<, which starts a
# generator expression. Into `output`, TRUE or FALSE.
function(buster_settings_plain text output)
    set(plain TRUE)
    foreach(character "\"" "\\" "\n" ";" "$<")
        string(FIND "${text}" "${character}" found)
        if(NOT found EQUAL -1)
            set(plain FALSE)
        endif()
    endforeach()
    set(${output} ${plain} PARENT_SCOPE)
endfunction()

# The translation units, in the order the executable links their objects: main.c's units (each main.c again with
# another BUSTER_UNIT) and the benchmarks and measurements that are units of their own.
set(BUSTER_SOURCES
    src/buster/compiler/main.c
    src/buster/compiler/main_unit_x86.c
    src/buster/compiler/main_unit_frontend.c
    src/buster/compiler/main_unit_assembler.c
    src/buster/compiler/main_unit_link.c
    src/buster/compiler/main_unit_executable.c
    src/buster/compiler/main_unit_dynamic.c
    src/buster/compiler/main_unit_types.c
    src/buster/compiler/main_unit_checker.c
    src/buster/compiler/main_unit_check_builtins.c
    src/buster/compiler/main_unit_backend.c
    src/buster/compiler/c_lexer_benchmark.c
    src/buster/compiler/x86_64_encoder_benchmark.c
    src/buster/compiler/lane_benchmark.c
    src/buster/compiler/dataflow_benchmark.c
    src/buster/compiler/dominators_benchmark.c
    src/buster/compiler/tier1_allocate_benchmark.c
    src/buster/compiler/object_writer_benchmark.c
    src/buster/compiler/executable_benchmark.c
    src/buster/compiler/import_benchmark.c
    src/buster/compiler/branch_sizing_benchmark.c
    src/buster/compiler/bind_benchmark.c
    src/buster/compiler/c_refine_benchmark.c
    src/buster/compiler/c_preprocessor_benchmark.c
    src/buster/compiler/c_files_benchmark.c
    src/buster/compiler/c_parser_benchmark.c
    src/buster/compiler/c_types_benchmark.c
    src/buster/compiler/c_builtin_benchmark.c
    src/buster/compiler/c_checker_benchmark.c
    src/buster/compiler/calling_convention_benchmark.c
    src/buster/compiler/tier1_frame_benchmark.c
    src/buster/compiler/snapshot_benchmark.c
    src/buster/compiler/link_benchmark.c
    src/buster/compiler/foreign_benchmark.c
    src/buster/compiler/floors_benchmark.c
    src/buster/compiler/x86_64_disassembler_benchmark.c
    src/buster/compiler/x86_semantics_benchmark.c
    src/buster/compiler/tier0_benchmark.c
    src/buster/compiler/tier1_schedule_benchmark.c
    src/buster/compiler/tier1_select_benchmark.c
    src/buster/compiler/tier1_model_measure.c
    src/buster/compiler/emission_metadata_test.c
    src/buster/compiler/c_check_target_feature_emission_test.c)

# The order a clean build starts the compiles in, each source's file name once, the longest first by their times on
# the 9700X (#270): one compile per core at a time (build.sh's --jobs), so a long one that started late would make the
# link wait for it. CMakeLists.txt defines the units' targets in this order, which is the order Ninja starts them in when
# its log has no times for them yet; with times (Ninja 1.12 and later), it starts the longest path first itself. A new
# source goes where its time puts it.
set(BUSTER_COMPILE_ORDER
    main_unit_checker.c main_unit_backend.c main_unit_frontend.c main_unit_executable.c main_unit_dynamic.c
    main_unit_link.c c_parser_benchmark.c main.c main_unit_x86.c main_unit_types.c main_unit_check_builtins.c
    c_files_benchmark.c c_preprocessor_benchmark.c link_benchmark.c executable_benchmark.c
    main_unit_assembler.c foreign_benchmark.c tier0_benchmark.c snapshot_benchmark.c x86_64_encoder_benchmark.c
    bind_benchmark.c c_refine_benchmark.c c_checker_benchmark.c import_benchmark.c tier1_select_benchmark.c
    c_lexer_benchmark.c branch_sizing_benchmark.c object_writer_benchmark.c x86_64_disassembler_benchmark.c
    calling_convention_benchmark.c tier1_schedule_benchmark.c x86_semantics_benchmark.c tier1_model_measure.c
    c_types_benchmark.c dominators_benchmark.c tier1_frame_benchmark.c tier1_allocate_benchmark.c lane_benchmark.c
    floors_benchmark.c dataflow_benchmark.c c_builtin_benchmark.c emission_metadata_test.c
    c_check_target_feature_emission_test.c)

# The sources' indices in BUSTER_SOURCES, in BUSTER_COMPILE_ORDER's order, into `output`. Fails, naming it, for two
# sources of one file name, which the order couldn't tell apart, for a file that the order names twice or that isn't a
# source, and for a source that it doesn't name.
function(buster_compile_order output)
    set(names "")
    foreach(source IN LISTS BUSTER_SOURCES)
        get_filename_component(name "${source}" NAME)
        if(name IN_LIST names)
            buster_settings_fail("two sources are named ${name}, which the compile order names by file name")
        endif()
        list(APPEND names "${name}")
    endforeach()
    set(indices "")
    set(remaining ${names})
    foreach(name IN LISTS BUSTER_COMPILE_ORDER)
        list(FIND remaining "${name}" found)
        if(found EQUAL -1)
            buster_settings_fail("the compile order names ${name}, which isn't a source or is named twice")
        endif()
        list(FIND names "${name}" index)
        list(APPEND indices ${index})
        list(REMOVE_AT remaining ${found})
    endforeach()
    if(remaining)
        string(REPLACE ";" " " remaining "${remaining}")
        buster_settings_fail("the compile order doesn't name ${remaining}")
    endif()
    set(${output} ${indices} PARENT_SCOPE)
endfunction()

# The settings, normalized, into buster_<setting> in the caller's scope, and the name of the build tree that keeps
# them apart from the others (buster_variant): the host ISA, the baseline runtime, the sanitizer and its optimization,
# fuzzing, and debug information, each of which changes every unit's flags. Runs nothing. Fails for a value no build
# takes, with the message the settings' checks have always given.
function(buster_settings_normalize)
    set(fuzz "${BUSTER_FUZZ_AVAILABLE}")
    if(fuzz STREQUAL "")
        set(fuzz 0)
    endif()
    if(fuzz STREQUAL "0")
        if(NOT "${BUSTER_FUZZ_ENTRY}" STREQUAL "")
            buster_settings_fail("BUSTER_FUZZ_ENTRY requires BUSTER_FUZZ_AVAILABLE=1")
        endif()
    elseif(NOT fuzz STREQUAL "1")
        buster_settings_fail("BUSTER_FUZZ_AVAILABLE must be 0 or 1")
    endif()

    # Fuzzing is instrumented with AddressSanitizer unless the sanitizer is given.
    set(sanitizer "${BUSTER_SANITIZER}")
    if(sanitizer STREQUAL "")
        if(fuzz)
            set(sanitizer address)
        else()
            set(sanitizer none)
        endif()
    endif()
    if(NOT sanitizer MATCHES "^(none|address|undefined|thread|memory)$")
        buster_settings_fail("BUSTER_SANITIZER must be none, address, undefined, thread or memory")
    endif()

    set(arch "${BUSTER_HOST_ARCH}")
    if(arch STREQUAL "")
        set(arch native)
    endif()
    # The name is clang's -march value, or `vector`, and part of the build tree's directory name.
    if(NOT arch MATCHES "^[A-Za-z0-9_.][A-Za-z0-9_.-]*$")
        buster_settings_fail("BUSTER_HOST_ARCH must be a -march name, such as native, x86-64 or znver5, or vector")
    endif()
    # The host target's flags: clang's -march=ARCH, or for `vector`, the explicit target that is exactly host_isa.h's
    # predicate (#2033): x86-64-v4 and the predicate's features beyond it, the same on every runner, so that any CPU whose
    # features and OS enable them all runs the executable, whatever its model. tools/ci/changes.c's self-test keeps this
    # line, host_isa.h's predicate and the hosted vector row's probe in step; keep it on one line.
    if(arch STREQUAL "vector")
        set(host_flags -march=x86-64-v4 -maes -mgfni -mvaes -mavx512vbmi -mavx512vbmi2 -mavx512vpopcntdq -mavx512bitalg -mavx512vnni)
    else()
        set(host_flags -march=${arch})
    endif()

    set(optimization "")
    if(NOT sanitizer STREQUAL "none")
        if(NOT "${BUSTER_HOST_RUNTIME}" STREQUAL "")
            buster_settings_fail("sanitizers require the host runtime, not BUSTER_HOST_RUNTIME")
        endif()
        set(optimization "${BUSTER_SANITIZER_OPTIMIZATION}")
        if(optimization STREQUAL "")
            set(optimization 3)
        endif()
        if(NOT optimization MATCHES "^[123]$")
            buster_settings_fail("BUSTER_SANITIZER_OPTIMIZATION must be 1, 2 or 3")
        endif()
    endif()

    if(NOT DEFINED BUSTER_DEBUG_INFO)
        set(debug_info 1)
    else()
        set(debug_info "${BUSTER_DEBUG_INFO}")
    endif()
    if(NOT debug_info MATCHES "^[01]$")
        buster_settings_fail("BUSTER_DEBUG_INFO must be 0 or 1")
    endif()

    # BUSTER_PGO=1 builds the Release executable with a profile of its own compiles (#880; CMakeLists.txt's pgo
    # targets): the production executable alone, so neither a sanitizer's nor the baseline runtime's, which leaves the
    # profile's runtime out of the link.
    set(pgo "${BUSTER_PGO}")
    if(pgo STREQUAL "")
        set(pgo 0)
    endif()
    set(pgo_train "${BUSTER_PGO_TRAIN}")
    if(pgo STREQUAL "0")
        if(NOT pgo_train STREQUAL "")
            buster_settings_fail("BUSTER_PGO_TRAIN requires BUSTER_PGO=1")
        endif()
    elseif(pgo STREQUAL "1")
        if(NOT sanitizer STREQUAL "none")
            buster_settings_fail("BUSTER_PGO builds the production executable, not a sanitizer build")
        endif()
        if(NOT "${BUSTER_HOST_RUNTIME}" STREQUAL "")
            buster_settings_fail("BUSTER_PGO links the profile's runtime, which BUSTER_HOST_RUNTIME's link leaves out")
        endif()
        if(NOT pgo_train STREQUAL "")
            get_filename_component(pgo_train "${pgo_train}" REALPATH BASE_DIR "${BUSTER_SOURCE_DIR}")
            if(NOT EXISTS "${pgo_train}" OR IS_DIRECTORY "${pgo_train}")
                buster_settings_fail("BUSTER_PGO_TRAIN must name a file")
            endif()
        endif()
    else()
        buster_settings_fail("BUSTER_PGO must be 0 or 1")
    endif()

    set(entry "")
    if(fuzz)
        if(sanitizer STREQUAL "thread")
            buster_settings_fail("libFuzzer cannot be combined with ThreadSanitizer")
        endif()
        set(entry "${BUSTER_FUZZ_ENTRY}")
        if(entry STREQUAL "")
            set(entry "${BUSTER_SOURCE_DIR}/src/buster/compiler/fuzz.c")
        endif()
        get_filename_component(entry "${entry}" REALPATH BASE_DIR "${BUSTER_SOURCE_DIR}")
        # The path is a string literal in every unit, and a list element here.
        buster_settings_plain("${entry}" plain)
        if(NOT EXISTS "${entry}" OR IS_DIRECTORY "${entry}" OR NOT plain)
            buster_settings_fail("BUSTER_FUZZ_ENTRY must name a C file without quotes, backslashes, newlines or semicolons")
        endif()
    endif()

    # Split on whitespace, as the shell split it; each is one argument of every unit's compile.
    string(REGEX MATCHALL "[^ \t\r\n]+" defines "${BUSTER_BUILD_DEFINES}")
    string(FIND "${BUSTER_BUILD_DEFINES}" ";" semicolon)
    string(FIND "${BUSTER_BUILD_DEFINES}" "$<" expression)
    if(NOT semicolon EQUAL -1 OR NOT expression EQUAL -1)
        buster_settings_fail("BUSTER_BUILD_DEFINES can't hold a semicolon or \$<")
    endif()

    # Parallel native generation after the ordinary O3 IR pipeline (BusterCodegen.cmake): auto, the default, takes it
    # where the build's flags allow it and the tools are there (CMakeLists.txt checks them); 1 requires it; 0 never.
    # A sanitizer build takes it too: its module is instrumented by that pipeline, so the parts generate the single
    # compile's code (#2639). Fuzzing, PGO, an explicit host runtime and BUSTER_BUILD_DEFINES keep the single compile.
    set(parallel_codegen "${BUSTER_PARALLEL_CODEGEN}")
    if(parallel_codegen STREQUAL "")
        set(parallel_codegen auto)
    endif()
    if(NOT parallel_codegen MATCHES "^(0|1|auto)$")
        buster_settings_fail("BUSTER_PARALLEL_CODEGEN must be auto, 0 or 1")
    endif()
    set(parallel_codegen_required FALSE)
    if(parallel_codegen STREQUAL "1")
        set(parallel_codegen_required TRUE)
    endif()
    if(NOT parallel_codegen STREQUAL "0")
        if(fuzz OR pgo OR NOT "${BUSTER_HOST_RUNTIME}" STREQUAL "" OR defines)
            if(parallel_codegen_required)
                buster_settings_fail("BUSTER_PARALLEL_CODEGEN=1 requires no fuzzing, PGO, host runtime or BUSTER_BUILD_DEFINES")
            endif()
            set(parallel_codegen 0)
        else()
            set(parallel_codegen 1)
        endif()
    endif()

    set(bake 1)
    if("${BUSTER_BAKE}" STREQUAL "0")
        set(bake 0)
    endif()

    set(variant "${arch}")
    if(NOT "${BUSTER_HOST_RUNTIME}" STREQUAL "")
        string(APPEND variant "-runtime")
    endif()
    if(NOT sanitizer STREQUAL "none")
        string(APPEND variant "-${sanitizer}-O${optimization}")
    endif()
    if(fuzz)
        string(APPEND variant "-fuzz")
    endif()
    # BUSTER_DEBUG_INFO decides the Release units' debug information but in a sanitizer build, which has line tables.
    if(debug_info STREQUAL "0" AND sanitizer STREQUAL "none")
        string(APPEND variant "-g0")
    endif()
    if(pgo)
        string(APPEND variant "-pgo")
    endif()

    set(buster_fuzz ${fuzz} PARENT_SCOPE)
    set(buster_fuzz_entry "${entry}" PARENT_SCOPE)
    set(buster_sanitizer ${sanitizer} PARENT_SCOPE)
    set(buster_sanitizer_optimization "${optimization}" PARENT_SCOPE)
    set(buster_arch "${arch}" PARENT_SCOPE)
    set(buster_host_flags ${host_flags} PARENT_SCOPE)
    set(buster_debug_info ${debug_info} PARENT_SCOPE)
    set(buster_defines ${defines} PARENT_SCOPE)
    set(buster_bake ${bake} PARENT_SCOPE)
    set(buster_pgo ${pgo} PARENT_SCOPE)
    set(buster_pgo_train "${pgo_train}" PARENT_SCOPE)
    set(buster_parallel_codegen ${parallel_codegen} PARENT_SCOPE)
    set(buster_parallel_codegen_required ${parallel_codegen_required} PARENT_SCOPE)
    set(buster_variant "${variant}" PARENT_SCOPE)
endfunction()

# Runs clang (`clang`, a path) for what the flags record of it, into the caller's scope:
#   buster_flags            every unit's flags, but debug information, optimization and BUSTER_BUILD_DEFINES, with
#                           the include directory as -Isrc, relative to the source directory
#   buster_debug            the Release configuration's debug information (-g, none, or -gline-tables-only)
#   buster_optimization     the Release configuration's optimization (-O3, or the sanitizers' level)
#   buster_sanitizer_flags  what a sanitizer build adds to every unit and to the link
#   buster_link_mode        -no-pie (with -static from buster_settings_linker), or -pie for TSan and MSan
#   buster_fuzz_libraries   what a fuzzing build links after the objects
#   buster_host_vector      1 when the host has the complete vector feature set, else 0
# buster_settings_normalize must have run.
function(buster_settings_tools clang)
    # The stack-protector policy is pinned (#916): a distribution's Clang may default to -fstack-protector-strong
    # while the official release CI stages does not, so the host's configured default would otherwise decide the
    # compiler's code and its measurement. The owner chose no stack protection for now, which matches the official
    # release's default and keeps host and CI builds identical. The post-link step fails a build whose link still
    # references the canary. The compiler's predefined macros still follow the host driver's configured default
    # (c_predefines.c), which -fno-stack-protector would hide, so record that default before adding the flag.
    execute_process(COMMAND "${clang}" -dM -E -x c /dev/null
        OUTPUT_VARIABLE defaults RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        buster_settings_fail("${clang} -dM -E failed")
    endif()
    if(defaults MATCHES "#define __SSP_ALL__ ")
        set(ssp BUSTER_PREDEFINES_SSP_ALL)
    elseif(defaults MATCHES "#define __SSP_STRONG__ ")
        set(ssp BUSTER_PREDEFINES_SSP_STRONG)
    elseif(defaults MATCHES "#define __SSP__ ")
        set(ssp BUSTER_PREDEFINES_SSP_ON)
    else()
        set(ssp BUSTER_PREDEFINES_SSP_OFF)
    endif()
    set(flags ${buster_host_flags} -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror -fno-stack-protector
        -DBUSTER_HOST_SSP_DEFAULT=${ssp})

    # The target-query defaults follow the Clang driver used to build this compiler. Keep the triple as a single
    # argument and reject text that cannot be a triple.
    execute_process(COMMAND "${clang}" -dumpmachine
        OUTPUT_VARIABLE triple OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE status)
    if(NOT status EQUAL 0 OR NOT triple MATCHES "^[A-Za-z0-9_.-]+$")
        buster_settings_fail("invalid clang default target")
    endif()
    list(APPEND flags "-DBUSTER_CLANG_DEFAULT_TARGET=\"${triple}\"")

    # Every unit is linked into the executable, with main.c's units, whose homes define what a unit of its own then
    # calls rather than compiles a copy of: the lexer's Unicode names, about 300 KB (c_unicode_names.c). The same unit
    # compiled without it, to be linked alone (measure-tier1-rows' lexer), has its own copy.
    list(APPEND flags -DBUSTER_EXECUTABLE_UNIT)

    # The scalar host build retains the vector kernels in source but does not emit them. Their unused helpers are
    # expected in that build alone.
    execute_process(COMMAND "${clang}" ${flags} -dM -E -include src/buster/compiler/host_isa.h -x c /dev/null
        WORKING_DIRECTORY "${BUSTER_SOURCE_DIR}"
        OUTPUT_VARIABLE host_defines ERROR_VARIABLE error RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        buster_settings_fail("${clang} ${buster_host_flags} can't preprocess host_isa.h:\n${error}")
    endif()
    if(host_defines MATCHES "(^|\n)#define BUSTER_HOST_VECTOR 1\n")
        set(host_vector 1)
    elseif(buster_arch STREQUAL "vector")
        # The explicit vector target exists to run the vector path: a scalar build of it would pass for one.
        buster_settings_fail("BUSTER_HOST_ARCH=vector (${buster_host_flags}) doesn't select host_isa.h's vector path: its flags and the predicate differ")
    else()
        set(host_vector 0)
        list(APPEND flags -Wno-unused-function -Wno-psabi)
        message(NOTICE "buster: host execution: scalar (${buster_arch})")
    endif()

    set(optimization -O3)
    # The executable's debug information: -g, unless BUSTER_DEBUG_INFO is 0, which leaves it out of the sources'
    # objects and takes a fifth of the build's time (the 9700X job builds so, since nothing there reads it). Clang's
    # code with -g is its code without it but for a few functions' stack slots.
    if(buster_debug_info)
        set(debug -g)
    else()
        set(debug "")
    endif()

    # Sanitizer builds keep the production semantics (-fwrapv, unsigned char and aliasing) and instrument every unit
    # and the final link with the same flags. TSan and MSan require a PIE, and a fuzzing build keeps one, unbaked: its
    # initializers would write coverage counters outside the baked section. ASan and UBSan link at a fixed address, as
    # the ordinary executable does, and so are baked (BusterPostLink.cmake): as a PIE, the address row's executable took
    # 1,688,462 relative relocations at every start (ASan's descriptors of the globals and string literals among them),
    # with 13,000 page faults, and built its tables, which made a small compile's process about 30% slower (#2030).
    set(sanitizer_flags "")
    set(link_mode -no-pie)
    if(buster_sanitizer STREQUAL "address")
        set(sanitizer_flags -fsanitize=address,undefined,bounds -fsanitize-address-use-after-scope
            -fsanitize-address-use-after-return=always)
    elseif(buster_sanitizer STREQUAL "undefined")
        set(sanitizer_flags -fsanitize=undefined,bounds)
    elseif(buster_sanitizer STREQUAL "thread")
        set(sanitizer_flags -fsanitize=thread)
    elseif(buster_sanitizer STREQUAL "memory")
        set(sanitizer_flags -fsanitize=memory -fsanitize-memory-track-origins=2)
    endif()
    if(NOT buster_sanitizer STREQUAL "none")
        list(APPEND sanitizer_flags -fno-sanitize-recover=all -fno-sanitize-trap=all -fno-omit-frame-pointer
            -fno-optimize-sibling-calls)
        if(buster_sanitizer STREQUAL "thread" OR buster_sanitizer STREQUAL "memory" OR buster_fuzz)
            list(APPEND sanitizer_flags -fPIE)
            set(link_mode -pie)
        endif()
        list(APPEND flags ${sanitizer_flags})
        set(optimization -O${buster_sanitizer_optimization})
        set(debug -gline-tables-only)
    endif()

    # Fuzzing is opt-in. Its entry source defines buster_fuzz_test_input and is included where the compiler's normal
    # entry point has access to its modules.
    set(fuzz_libraries "")
    if(buster_fuzz)
        # Clang's per-target runtime directory holds it (the official releases' only layout); older layouts name the
        # architecture in the file.
        execute_process(COMMAND "${clang}" -print-runtime-dir OUTPUT_VARIABLE runtime_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(runtime "${runtime_dir}/libclang_rt.fuzzer_no_main.a")
        if(NOT EXISTS "${runtime}")
            execute_process(COMMAND "${clang}" -print-file-name=libclang_rt.fuzzer_no_main-x86_64.a
                OUTPUT_VARIABLE runtime OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        if(NOT IS_ABSOLUTE "${runtime}" OR NOT EXISTS "${runtime}")
            buster_settings_fail("Clang libFuzzer no-main runtime is missing")
        endif()
        list(APPEND flags -fsanitize=fuzzer-no-link -DBUSTER_FUZZ_AVAILABLE=1 "-DBUSTER_FUZZ_ENTRY=\"${buster_fuzz_entry}\"")
        set(fuzz_libraries "${runtime}" -lstdc++ -pthread)
    endif()

    # A static link (#652) where nothing needs the loader: the sanitizers' runtimes need it, libFuzzer's runtime
    # C++'s shared libraries, and BUSTER_HOST_RUNTIME links an old runtime's shared libraries explicitly
    # (CMakeLists.txt's buster_link). The dynamic loader took 8% to 12% of a small compile's task-clock on the 9700X
    # (an empty file's -fsyntax-only, -E -P and a one-line -c, buster-performance's small-unit-start-652-2026-11-05).
    # buster_settings_linker decides it with the linker, which must not be wild.
    set(static_candidate FALSE)
    if(buster_sanitizer STREQUAL "none" AND NOT buster_fuzz AND "${BUSTER_HOST_RUNTIME}" STREQUAL "")
        set(static_candidate TRUE)
    endif()

    set(buster_static_candidate ${static_candidate} PARENT_SCOPE)
    set(buster_flags ${flags} PARENT_SCOPE)
    set(buster_debug ${debug} PARENT_SCOPE)
    set(buster_optimization ${optimization} PARENT_SCOPE)
    set(buster_sanitizer_flags ${sanitizer_flags} PARENT_SCOPE)
    set(buster_link_mode ${link_mode} PARENT_SCOPE)
    set(buster_fuzz_libraries ${fuzz_libraries} PARENT_SCOPE)
    set(buster_host_vector ${host_vector} PARENT_SCOPE)
endfunction()

# The Debug configuration's debug information and optimization, in place of the Release configuration's. Debug keeps
# every other flag, so it has the same language and aliasing contracts (-fwrapv, -funsigned-char,
# -fno-strict-aliasing), host ISA, sanitizers and definitions; the code is clang's -O0, whose variables a debugger reads
# everywhere, with full debug information whatever BUSTER_DEBUG_INFO says.
set(BUSTER_DEBUG_CONFIGURATION_DEBUG -g)
set(BUSTER_DEBUG_CONFIGURATION_OPTIMIZATION -O0)

# The Develop configuration's debug information, in place of the Release configuration's: DWARF 4 line tables, whatever
# BUSTER_DEBUG_INFO says. Its optimization is Release's. Line tables name each function's file, by which presplit.c
# places a unit's functions in parts and finds the tests' (BusterCodegen.cmake); DWARF 5 would record every file's MD5,
# so that an edit changed every part that mentions its file (docs/stage1-build-design.md).
set(BUSTER_DEVELOP_CONFIGURATION_DEBUG -gdwarf-4 -gline-tables-only)

# The units of their own that the Develop configuration leaves out of its executable (#2559's step 2), built on demand
# by Release and Debug: the benchmark units whose only entry is a benchmark command's run_* function and that no test
# runs, which BusterDevelopAbsent.cmake's stand-ins replace in Develop. test-preprocessor's location option runs
# benchmark-preprocessor (c_preprocessor_benchmark.c), and takes Develop's stand-in as it takes a scalar build's refusal:
# the unit was Develop's longest compile (7.4 s on the 9700X, #2624). Develop builds the others: floors_benchmark.c also
# holds test-floors-input's tests, and tier1_model_measure.c and the two test units serve the correctness inventory.
set(BUSTER_DEVELOP_ON_DEMAND
    c_lexer_benchmark.c x86_64_encoder_benchmark.c lane_benchmark.c dataflow_benchmark.c dominators_benchmark.c
    tier1_allocate_benchmark.c object_writer_benchmark.c executable_benchmark.c import_benchmark.c
    branch_sizing_benchmark.c bind_benchmark.c c_refine_benchmark.c c_files_benchmark.c
    c_parser_benchmark.c c_types_benchmark.c c_builtin_benchmark.c c_checker_benchmark.c calling_convention_benchmark.c
    tier1_frame_benchmark.c snapshot_benchmark.c link_benchmark.c foreign_benchmark.c x86_64_disassembler_benchmark.c
    x86_semantics_benchmark.c tier0_benchmark.c tier1_schedule_benchmark.c tier1_select_benchmark.c
    c_preprocessor_benchmark.c)

# The units a sanitizer tree leaves out of every configuration's executable (#2639): Develop's on-demand units but
# c_lexer_benchmark.c, whose benchmark-lexer is each vector item's evidence that its CPU runs the vector kernels, in the
# sanitizer configurations too (hosted-correctness.yml, "Confirm the vector kernels run on this CPU"). The correctness
# inventory runs none of the others' commands, and on the hosted runners they were 29% of a vector or native sanitizer
# build's unit time (387.4 of 1,324.0 s with 4 jobs), which the vector rows' builders and the fuzz jobs wait for.
set(BUSTER_SANITIZER_ON_DEMAND ${BUSTER_DEVELOP_ON_DEMAND})
list(REMOVE_ITEM BUSTER_SANITIZER_ON_DEMAND c_lexer_benchmark.c)

# The linker for the executable's link alone (compilation and IR capture keep their flags), from BUSTER_LINKER, into
# buster_linker_flags (--ld-path=PATH, or none for clang's default) and buster_linker (its description).
function(buster_settings_linker)
    if(NOT DEFINED BUSTER_LINKER)
        set(linker auto)
    else()
        set(linker "${BUSTER_LINKER}")
    endif()
    set(names "")
    if(linker STREQUAL "auto" AND buster_static_candidate)
        # wild 0.10.0 links a static executable whose startup faults in glibc's __ctype_init, at its first TLS access,
        # against Ubuntu's glibc 2.39 and the hosted runners' (#652); mold, lld and GNU ld link it correctly. lld first,
        # for its -O0 (below).
        set(names ld.lld mold)
    elseif(linker STREQUAL "auto")
        set(names wild mold)
    elseif(linker STREQUAL "system")
    elseif(linker STREQUAL "lld" OR linker STREQUAL "bfd")
        set(names ld.${linker})
    elseif(linker STREQUAL "wild" OR linker STREQUAL "mold")
        set(names ${linker})
    elseif(NOT linker MATCHES "^/")
        buster_settings_fail("BUSTER_LINKER must be auto, system, wild, mold, lld, bfd or an absolute path")
    endif()
    set(path "")
    if(linker MATCHES "^/")
        set(path "${linker}")
    else()
        foreach(name IN LISTS names)
            # PATH's programs alone, as the shell finds them.
            find_program(found NAMES ${name} NO_CACHE NO_DEFAULT_PATH PATHS ENV PATH)
            if(found)
                set(path "${found}")
                break()
            endif()
        endforeach()
        if(names AND path STREQUAL "" AND NOT linker STREQUAL "auto")
            buster_settings_fail("requested linker is not an executable file on PATH or at the specified path")
        endif()
    endif()
    set(flags "")
    if(NOT path STREQUAL "")
        execute_process(COMMAND test -f "${path}" -a -x "${path}" RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            buster_settings_fail("requested linker is not an executable file on PATH or at the specified path")
        endif()
        set(flags "--ld-path=${path}")
    else()
        set(path "system default")
    endif()
    set(buster_linker "${path}" PARENT_SCOPE)
    # The static link, with any linker but wild (above): an explicit wild links dynamically, whatever its file is
    # called (ld.wild, a symlink, a path), as its --version names it ("Wild 0.10.0 ..."). BUSTER_HOST_STATIC tells the
    # code (test_libm.c): a static executable's dlsym(RTLD_DEFAULT, ...) finds nothing.
    set(wild FALSE)
    set(lld FALSE)
    if(NOT path STREQUAL "system default")
        execute_process(COMMAND "${path}" --version OUTPUT_VARIABLE version ERROR_VARIABLE version_errors
            RESULT_VARIABLE status TIMEOUT 30)
        get_filename_component(linker_name "${path}" NAME)
        if(linker_name STREQUAL "wild" OR "${version}\n${version_errors}" MATCHES "(^|\n)Wild ")
            set(wild TRUE)
        endif()
        if("${version}\n${version_errors}" MATCHES "(^|\n)LLD ")
            set(lld TRUE)
        endif()
    endif()
    # lld at -O0 keeps each object's strings where the object has them, rather than merging every object's identical
    # strings and laying the pool out in their hashes' order (#652). A compile's comparisons of its options and
    # commands against their names then read a few pages of the pool rather than pages all over it: on the 9700X, an
    # empty file's -fsyntax-only faulted 28 pages of read-only data rather than mold's 67, and took 14% less time from
    # a cold start than mold's link of the same objects (7% for a one-line -c; sqlite3.c's the same). The strings that
    # several objects repeat take 1.8 MB more of the executable. mold takes -O but ignores it.
    if(lld)
        list(APPEND flags -Wl,-O0)
    endif()
    set(buster_linker_flags ${flags} PARENT_SCOPE)
    # Whether the linker is lld, which the start's code order needs (cmake/BusterStartOrder.cmake).
    set(buster_lld ${lld} PARENT_SCOPE)
    # Whether the link merges identical constants and strings across objects (SHF_MERGE): every linker but lld at
    # -O0. The parallel parts' join keeps the flags for one that does (BusterCodegenMerge.cmake).
    if(lld)
        set(buster_linker_merges FALSE PARENT_SCOPE)
    else()
        set(buster_linker_merges TRUE PARENT_SCOPE)
    endif()
    if(buster_static_candidate AND NOT wild)
        set(buster_link_mode ${buster_link_mode} -static -Wl,--wrap=__register_frame_info,--wrap=__deregister_frame_info PARENT_SCOPE)
        set(buster_flags ${buster_flags} -DBUSTER_HOST_STATIC=1 PARENT_SCOPE)
        set(buster_static TRUE PARENT_SCOPE)
    else()
        set(buster_static FALSE PARENT_SCOPE)
    endif()
endfunction()
