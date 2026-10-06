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
        add_custom_target(test_rendering_raster_no_display
            COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY ${BUSTER_TEST_ENV} "$<TARGET_FILE:rendering_raster_native_tests>" --no-display
            DEPENDS rendering_raster_native_tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM)
    endif()
endif()
