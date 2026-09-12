# iOS test-fixture graph. The production target and the host-only regression
# include this same function. Fixtures are runtime inputs, not compiler sources:
# their changes rebuild only the owned bundle subtree. The content-stable
# inventory catches removals and additions with old timestamps.
function(buster_add_ios_test_assets target)
    set(options)
    set(one_value_args BUNDLE_CONTENT_DIR SOURCE_DIR)
    cmake_parse_arguments(IOS_ASSETS "${options}" "${one_value_args}" "" ${ARGN})

    if (NOT TARGET ${target})
        message(FATAL_ERROR "iOS test asset target does not exist: ${target}")
    endif()
    if (NOT IOS_ASSETS_SOURCE_DIR)
        set(IOS_ASSETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests")
    endif()
    get_property(ios_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (NOT IOS_ASSETS_BUNDLE_CONTENT_DIR)
        get_target_property(ios_output_name ${target} OUTPUT_NAME)
        if (NOT ios_output_name)
            set(ios_output_name "${target}")
        endif()
        if (ios_multi_config)
            set(IOS_ASSETS_BUNDLE_CONTENT_DIR
                "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${ios_output_name}.app")
        else()
            set(IOS_ASSETS_BUNDLE_CONTENT_DIR
                "${CMAKE_CURRENT_BINARY_DIR}/${ios_output_name}.app")
        endif()
    endif()

    file(GLOB_RECURSE ios_test_files
        LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
        "${IOS_ASSETS_SOURCE_DIR}/*")
    set(ios_dormant_test_relatives)
    set(ios_test_fixtures)
    foreach(test_file IN LISTS ios_test_files)
        file(RELATIVE_PATH test_relative "${IOS_ASSETS_SOURCE_DIR}" "${test_file}")
        if (test_file MATCHES "\\.bbb$")
            list(APPEND ios_dormant_test_relatives "${test_relative}")
        else()
            list(APPEND ios_test_fixtures "${test_file}")
        endif()
    endforeach()

    set(ios_test_inventory
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}-ios-test-fixtures.txt")
    string(REPLACE ";" "\n" ios_test_inventory_content "${ios_test_fixtures}")
    file(GENERATE OUTPUT "${ios_test_inventory}"
        CONTENT "${ios_test_inventory_content}\n")

    if (ios_multi_config)
        set(ios_asset_configs ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(ios_asset_configs __single_config__)
    endif()

    set(ios_selected_stamps)
    foreach(config IN LISTS ios_asset_configs)
        if (ios_multi_config)
            string(REPLACE "$<CONFIG>" "${config}" ios_bundle_content_dir
                "${IOS_ASSETS_BUNDLE_CONTENT_DIR}")
            set(ios_test_stamp
                "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}-ios-test-assets-${config}.stamp")
            list(APPEND ios_selected_stamps "$<$<CONFIG:${config}>:${ios_test_stamp}>")
        else()
            string(REPLACE "$<CONFIG>" "${CMAKE_BUILD_TYPE}" ios_bundle_content_dir
                "${IOS_ASSETS_BUNDLE_CONTENT_DIR}")
            set(ios_test_stamp
                "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}-ios-test-assets.stamp")
            list(APPEND ios_selected_stamps "${ios_test_stamp}")
        endif()

        set(ios_dormant_test_assets)
        foreach(test_relative IN LISTS ios_dormant_test_relatives)
            list(APPEND ios_dormant_test_assets
                "${ios_bundle_content_dir}/tests/${test_relative}")
        endforeach()
        set(ios_filter_commands)
        if (ios_dormant_test_assets)
            list(APPEND ios_filter_commands
                COMMAND "${CMAKE_COMMAND}" -E rm -f ${ios_dormant_test_assets})
        endif()

        add_custom_command(
            OUTPUT "${ios_test_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "${ios_bundle_content_dir}/tests"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${IOS_ASSETS_SOURCE_DIR}"
                "${ios_bundle_content_dir}/tests"
            ${ios_filter_commands}
            COMMAND "${CMAKE_COMMAND}" -E touch "${ios_test_stamp}"
            DEPENDS "${ios_test_inventory}" ${ios_test_fixtures}
            COMMENT "Staging active test fixtures into ${target}.app (${config})"
            VERBATIM
        )
    endforeach()
    add_custom_target(${target}_ios_test_assets DEPENDS ${ios_selected_stamps})
    add_dependencies(${target} ${target}_ios_test_assets)
endfunction()
