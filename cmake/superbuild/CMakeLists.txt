cmake_minimum_required(VERSION 3.17)

# This compiler-free outer project only turns build.c's generated matrix
# manifest into one Ninja graph. Keep matrix policy and workflows in build.c.
project(buster_superbuild NONE)

if (NOT DEFINED BUSTER_SUPERBUILD_MATRIX_FILE OR BUSTER_SUPERBUILD_MATRIX_FILE STREQUAL "")
    message(FATAL_ERROR "BUSTER_SUPERBUILD_MATRIX_FILE is required")
endif()

include("${BUSTER_SUPERBUILD_MATRIX_FILE}")

if (NOT DEFINED BUSTER_SUPERBUILD_SOURCE_DIR OR NOT DEFINED BUSTER_SUPERBUILD_TREE_COUNT)
    message(FATAL_ERROR "The superbuild matrix file is incomplete")
endif()

add_custom_target(buster_compile)
add_custom_target(buster_matrix ALL)

if (BUSTER_SUPERBUILD_TREE_COUNT LESS 1)
    message(FATAL_ERROR "The superbuild matrix must contain at least one compiler tree")
endif()

math(EXPR BUSTER_SUPERBUILD_LAST_TREE "${BUSTER_SUPERBUILD_TREE_COUNT} - 1")
foreach(BUSTER_SUPERBUILD_TREE_INDEX RANGE 0 ${BUSTER_SUPERBUILD_LAST_TREE})
    set(BUSTER_TREE_PREFIX "BUSTER_SUPERBUILD_TREE_${BUSTER_SUPERBUILD_TREE_INDEX}")
    set(BUSTER_TREE_BUILD_DIRECTORY "${${BUSTER_TREE_PREFIX}_BUILD_DIRECTORY}")
    set(BUSTER_TREE_NATIVE_CONFIG "${${BUSTER_TREE_PREFIX}_NATIVE_CONFIG}")
    set(BUSTER_TREE_BUILD_TARGETS "${${BUSTER_TREE_PREFIX}_BUILD_TARGETS}")
    set(BUSTER_TREE_PARALLEL_JOBS "${${BUSTER_TREE_PREFIX}_PARALLEL_JOBS}")
    set(BUSTER_TREE_TEST_0_CONFIG "${${BUSTER_TREE_PREFIX}_TEST_0_CONFIG}")
    set(BUSTER_TREE_TEST_0_TARGET "${${BUSTER_TREE_PREFIX}_TEST_0_TARGET}")
    set(BUSTER_TREE_TEST_1_CONFIG "${${BUSTER_TREE_PREFIX}_TEST_1_CONFIG}")
    set(BUSTER_TREE_TEST_1_TARGET "${${BUSTER_TREE_PREFIX}_TEST_1_TARGET}")
    set(BUSTER_TREE_ANALYZE_CONFIG "${${BUSTER_TREE_PREFIX}_ANALYZE_CONFIG}")

    if (BUSTER_TREE_BUILD_DIRECTORY STREQUAL "" OR BUSTER_TREE_NATIVE_CONFIG STREQUAL "" OR
        BUSTER_TREE_BUILD_TARGETS STREQUAL "" OR BUSTER_TREE_PARALLEL_JOBS LESS 1 OR
        BUSTER_TREE_TEST_0_CONFIG STREQUAL "" OR BUSTER_TREE_TEST_0_TARGET STREQUAL "")
        message(FATAL_ERROR "Compiler tree ${BUSTER_SUPERBUILD_TREE_INDEX} is incomplete")
    endif()

    set(BUSTER_TREE_BUILD_OPTIONS)
    if (BUSTER_SUPERBUILD_VERBOSE)
        list(APPEND BUSTER_TREE_BUILD_OPTIONS --verbose)
    endif()
    if (BUSTER_SUPERBUILD_QUIET)
        list(APPEND BUSTER_TREE_BUILD_OPTIONS -- --quiet)
    endif()

    set(BUSTER_TREE_COMPILE_TARGET "buster_compile_${BUSTER_SUPERBUILD_TREE_INDEX}")
    add_custom_target(${BUSTER_TREE_COMPILE_TARGET}
        COMMAND "${CMAKE_COMMAND}" --build "${BUSTER_TREE_BUILD_DIRECTORY}"
            --config "${BUSTER_TREE_NATIVE_CONFIG}"
            --target ${BUSTER_TREE_BUILD_TARGETS}
            --parallel "${BUSTER_TREE_PARALLEL_JOBS}"
            ${BUSTER_TREE_BUILD_OPTIONS}
        WORKING_DIRECTORY "${BUSTER_SUPERBUILD_SOURCE_DIR}"
        VERBATIM
    )
    add_dependencies(buster_compile ${BUSTER_TREE_COMPILE_TARGET})

    set(BUSTER_TREE_TEST_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E env
            "BUSTER_TEST_JOBS=${BUSTER_TREE_PARALLEL_JOBS}"
            "${CMAKE_COMMAND}" --build "${BUSTER_TREE_BUILD_DIRECTORY}"
            --config "${BUSTER_TREE_TEST_0_CONFIG}"
            --target "${BUSTER_TREE_TEST_0_TARGET}"
            ${BUSTER_TREE_BUILD_OPTIONS}
    )
    if (NOT BUSTER_TREE_TEST_1_CONFIG STREQUAL "")
        if (BUSTER_TREE_TEST_1_TARGET STREQUAL "")
            message(FATAL_ERROR "Compiler tree ${BUSTER_SUPERBUILD_TREE_INDEX} has an incomplete second test")
        endif()
        list(APPEND BUSTER_TREE_TEST_COMMANDS
            COMMAND "${CMAKE_COMMAND}" -E env
                "BUSTER_TEST_JOBS=${BUSTER_TREE_PARALLEL_JOBS}"
                "${CMAKE_COMMAND}" --build "${BUSTER_TREE_BUILD_DIRECTORY}"
                --config "${BUSTER_TREE_TEST_1_CONFIG}"
                --target "${BUSTER_TREE_TEST_1_TARGET}"
                ${BUSTER_TREE_BUILD_OPTIONS}
        )
    endif()
    if (NOT BUSTER_TREE_ANALYZE_CONFIG STREQUAL "")
        list(APPEND BUSTER_TREE_TEST_COMMANDS
            COMMAND "${CMAKE_COMMAND}" --build "${BUSTER_TREE_BUILD_DIRECTORY}"
                --config "${BUSTER_TREE_ANALYZE_CONFIG}"
                --target clang_analyze
                ${BUSTER_TREE_BUILD_OPTIONS}
        )
    endif()

    set(BUSTER_TREE_TEST_TARGET "buster_test_${BUSTER_SUPERBUILD_TREE_INDEX}")
    add_custom_target(${BUSTER_TREE_TEST_TARGET}
        ${BUSTER_TREE_TEST_COMMANDS}
        WORKING_DIRECTORY "${BUSTER_SUPERBUILD_SOURCE_DIR}"
        VERBATIM
    )
    add_dependencies(${BUSTER_TREE_TEST_TARGET} ${BUSTER_TREE_COMPILE_TARGET})
    add_dependencies(buster_matrix ${BUSTER_TREE_TEST_TARGET})
endforeach()
