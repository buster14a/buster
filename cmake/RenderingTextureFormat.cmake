# Headless semantic checks for retained texture formats, separate from ide.
if (BUSTER_INCLUDE_TESTS AND BUSTER_LINK_LIBC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(BUSTER_RENDERING_TEXTURE_FORMAT_MODULES os arena integer string file hash time float target)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
        list(APPEND BUSTER_RENDERING_TEXTURE_FORMAT_MODULES x86_64)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
        list(APPEND BUSTER_RENDERING_TEXTURE_FORMAT_MODULES aarch64)
    endif()
    # This standalone executable compiles the test and modules separately, so
    # its declarations must remain external in optimized Clang matrix builds.
    set(BUSTER_RENDERING_TEXTURE_FORMAT_UNITY_BUILD_DEFINE "${BUSTER_UNITY_BUILD_DEFINE}")
    set(BUSTER_RENDERING_TEXTURE_FORMAT_NON_UNITY_SOURCE_CONDITION "${BUSTER_NON_UNITY_SOURCE_CONDITION}")
    set(BUSTER_UNITY_BUILD_DEFINE 0)
    set(BUSTER_NON_UNITY_SOURCE_CONDITION 1)
    executable_add(rendering_texture_format_component_tests OFF src/buster/tests/rendering_texture_format_component_test.c
        MODULES ${BUSTER_RENDERING_TEXTURE_FORMAT_MODULES} rendering)
    set(BUSTER_UNITY_BUILD_DEFINE "${BUSTER_RENDERING_TEXTURE_FORMAT_UNITY_BUILD_DEFINE}")
    set(BUSTER_NON_UNITY_SOURCE_CONDITION "${BUSTER_RENDERING_TEXTURE_FORMAT_NON_UNITY_SOURCE_CONDITION}")
    unset(BUSTER_RENDERING_TEXTURE_FORMAT_UNITY_BUILD_DEFINE)
    unset(BUSTER_RENDERING_TEXTURE_FORMAT_NON_UNITY_SOURCE_CONDITION)
    if (UNIX)
        target_link_libraries(rendering_texture_format_component_tests PRIVATE m)
    endif()
    if (APPLE AND NOT BUSTER_USE_VULKAN)
        # rendering.c selects Metal on Apple even when BUSTER_USE_METAL is off.
        target_link_libraries(rendering_texture_format_component_tests PRIVATE
            "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    endif()
    add_custom_target(test_rendering_texture_formats
        COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:rendering_texture_format_component_tests>"
        DEPENDS rendering_texture_format_component_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Run headless rendering texture-format contract regressions"
        VERBATIM)
    if (TARGET test_all)
        add_dependencies(test_all test_rendering_texture_formats)
        add_dependencies(test_units test_rendering_texture_formats)
    endif()
endif()
