# Explicit CPU raster component and opt-in native application dependency graph.
# No WINDOW/RENDERING_RASTER dependency is added to the headless compiler.
option(BUSTER_BUILD_IMAGE_BROWSER "Build the Linux x86-64 XCB image-browser slice" OFF)
buster_register_module(rendering_raster "${BUSTER_SOURCE_DIR}/rendering_raster${COMMON_EXTENSION}")

if (BUSTER_BUILD_IMAGE_BROWSER)
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$" OR NOT BUSTER_LINK_LIBC)
        message(FATAL_ERROR "The image-browser slice requires Linux x86-64 and libc")
    endif()
endif()

if (BUSTER_INCLUDE_TESTS AND BUSTER_LINK_LIBC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    executable_add(rendering_texture_admission_component_tests OFF src/buster/tests/rendering_texture_admission_component_test.c)
    add_custom_target(test_rendering_texture_admission
        COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:rendering_texture_admission_component_tests>"
        DEPENDS rendering_texture_admission_component_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Run headless Vulkan texture admission policy regressions"
        VERBATIM)
    if (TARGET test_all)
        add_dependencies(test_all test_rendering_texture_admission)
    endif()
    if (TARGET test_units)
        add_dependencies(test_units test_rendering_texture_admission)
    endif()

    executable_add(rendering_raster_component_tests OFF src/buster/tests/rendering_raster_component_test.c
        MODULES rendering_raster)
    target_compile_definitions(rendering_raster_component_tests PRIVATE BUSTER_RASTER_CPU_ONLY=1)
    add_custom_target(test_rendering_raster
        COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:rendering_raster_component_tests>"
        DEPENDS rendering_raster_component_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM)
    if (BUSTER_BUILD_IMAGE_BROWSER)
        executable_add(rendering_raster_native_tests OFF src/buster/tests/rendering_raster_native_test.c
            MODULES os arena integer string file hash time float window rendering_raster)
        target_link_libraries(rendering_raster_native_tests PRIVATE m xcb)
        add_custom_target(test_rendering_raster_native
            COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:rendering_raster_native_tests>"
            DEPENDS rendering_raster_native_tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM)
        executable_add(window_component_tests OFF src/buster/tests/window_component_test.c
            MODULES os arena integer string file hash time float window ui_core
            SOURCES src/buster/tests/window_test.c)
        target_link_libraries(window_component_tests PRIVATE m xcb)
        add_custom_target(test_window
            COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY ${BUSTER_TEST_ENV} "$<TARGET_FILE:window_component_tests>"
            DEPENDS window_component_tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Run headless window backend seam regressions"
            VERBATIM)
        add_custom_target(test_rendering_raster_no_display
            COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY ${BUSTER_TEST_ENV} "$<TARGET_FILE:rendering_raster_native_tests>" --no-display
            DEPENDS rendering_raster_native_tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM)
    endif()
endif()
