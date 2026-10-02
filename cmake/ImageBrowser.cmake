# Runnable image-browser consumer, independent of ide and its module closure.
if (BUSTER_BUILD_IMAGE_BROWSER)
    if (BUSTER_SINGLE_THREADED)
        message(FATAL_ERROR "image_browser requires BUSTER_SINGLE_THREADED=OFF for thread-safe arena/OS state")
    endif()
    buster_register_module(image_browser_state "${CMAKE_SOURCE_DIR}/src/buster/apps/image_browser/image_browser_state.c")
    buster_register_module(image_browser_linux "${CMAKE_SOURCE_DIR}/src/buster/apps/image_browser/image_browser_linux.c")
    set(BUSTER_IMAGE_BROWSER_MODULES os entry_point arena integer string file hash time float target x86_64 image window rendering_raster image_browser_state image_browser_linux)
    executable_add(image_browser OFF src/buster/apps/image_browser/image_browser.c
        MODULES ${BUSTER_IMAGE_BROWSER_MODULES})
    target_link_libraries(image_browser PRIVATE m xcb)
    if (BUSTER_INCLUDE_TESTS)
        executable_add(image_browser_state_tests OFF src/buster/tests/image_browser_state_runner.c
            MODULES os entry_point arena integer string file hash time float target x86_64 image image_browser_state image_browser_linux
            SOURCES src/buster/tests/image_browser_state_test.c src/buster/tests/image_browser_linux_test.c)
        target_link_libraries(image_browser_state_tests PRIVATE m)
        add_custom_target(test_image_browser_state
            COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:image_browser_state_tests>"
            DEPENDS image_browser_state_tests
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM)
        add_custom_target(test_image_browser_native
            COMMAND ${CMAKE_COMMAND} -E env ${BUSTER_TEST_ENV} "$<TARGET_FILE:image_browser>" tests/image-browser --smoke
            DEPENDS image_browser
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            VERBATIM)
    endif()
endif()
