# Opt-in application; no application module is linked into the compiler.
option(BUSTER_HOT_RELOAD_DEMO "Build the controlled Linux x86-64 hot reload demo" OFF)
if (BUSTER_HOT_RELOAD_DEMO)
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
        NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$" OR
        NOT BUSTER_LINK_LIBC OR BUSTER_FUZZ_AVAILABLE)
        message(FATAL_ERROR "Hot reload demo requires Linux x86-64, libc and no fuzz instrumentation")
    endif()

    function(buster_register_hot_reload_demo)
        # This standalone main owns its lifecycle. Test seams and persistent
        # worker creation do not belong in its object/JIT runtime.
        set(BUSTER_INCLUDE_TESTS OFF)
        set(BUSTER_SINGLE_THREADED ON)
        # Loader-only sources rely on section GC for unused object converters;
        # static unity declarations require the entire compiler implementation.
        set(BUSTER_UNITY_BUILD_DEFINE 0)
        set(BUSTER_NON_UNITY_SOURCE_CONDITION 1)
        executable_add(hot_reload OFF tools/hot_replace_probe/host.c
            MODULES os arena integer string target x86_64 hash byte_writer
                    compiler_dwarf compiler_aarch64_encoding compiler_assembly_metadata
                    compiler_object compiler_jit)
        set_target_properties(hot_reload PROPERTIES EXCLUDE_FROM_ALL TRUE)
        target_compile_options(hot_reload PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(hot_reload PRIVATE "LINKER:--gc-sections")
        target_link_libraries(hot_reload PRIVATE m dl pthread)
        add_custom_target(test_hot_reload_lifecycle
            COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV}
                "$<TARGET_FILE:hot_reload>" --self-test "$<TARGET_FILE:ide>"
            DEPENDS hot_reload ide
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Exercise controlled C edit-rebuild-reload lifecycle"
            VERBATIM)
    endfunction()
    buster_register_hot_reload_demo()
endif()
