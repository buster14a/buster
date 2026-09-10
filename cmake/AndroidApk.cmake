# Android APK command graph. Included after SDK tools, paths, shader commands,
# and the ide target are configured by CMakeLists.txt. The mobile regression
# includes this same graph with controlled packaging tools, without an NDK.
# Fixtures are runtime inputs, not compiler sources: their changes must only
# repackage. The content-stable inventory also invalidates removals and additions
# with old timestamps, which file dependencies alone cannot express.
file(GLOB_RECURSE BUSTER_ANDROID_TEST_FILES
    LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tests/*")
set(BUSTER_ANDROID_DORMANT_TEST_ASSETS)
set(BUSTER_ANDROID_TEST_FIXTURES)
foreach(test_file IN LISTS BUSTER_ANDROID_TEST_FILES)
    if (test_file MATCHES "\\.bbb$")
        file(RELATIVE_PATH test_relative "${CMAKE_SOURCE_DIR}/tests" "${test_file}")
        list(APPEND BUSTER_ANDROID_DORMANT_TEST_ASSETS
            "${CMAKE_BINARY_DIR}/assets/tests/${test_relative}")
    else()
        list(APPEND BUSTER_ANDROID_TEST_FIXTURES "${test_file}")
    endif()
endforeach()
set(BUSTER_ANDROID_TEST_INVENTORY "${CMAKE_BINARY_DIR}/android-test-fixtures.txt")
string(REPLACE ";" "\n" BUSTER_ANDROID_TEST_INVENTORY_CONTENT "${BUSTER_ANDROID_TEST_FIXTURES}")
file(GENERATE OUTPUT "${BUSTER_ANDROID_TEST_INVENTORY}"
    CONTENT "${BUSTER_ANDROID_TEST_INVENTORY_CONTENT}\n")

# Keep the bulk directory copy, but never feed the preserved custom-language
# corpus to aapt2. The destination is rebuilt, so removals leave no stale assets.
set(BUSTER_ANDROID_TEST_FILTER_COMMANDS)
if (BUSTER_ANDROID_DORMANT_TEST_ASSETS)
    list(APPEND BUSTER_ANDROID_TEST_FILTER_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E rm -f ${BUSTER_ANDROID_DORMANT_TEST_ASSETS})
endif()

add_custom_command(
    OUTPUT "${BUSTER_APK}"
    # 0. Stage test data and, only for an explicitly enabled renderer, shaders.
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_BINARY_DIR}/assets/tests"
    ${BUSTER_ANDROID_TEST_FILTER_COMMANDS}
    ${BUSTER_ANDROID_SHADER_ASSET_COMMANDS}
    # 1. Compile the manifest + assets into a base APK (binary manifest + resources.arsc).
    COMMAND "${BUSTER_AAPT2}" link
        -I "${BUSTER_ANDROID_JAR}"
        --manifest "${BUSTER_ANDROID_MANIFEST}"
        -A "${CMAKE_BINARY_DIR}/assets"
        --min-sdk-version 24 --target-sdk-version 35
        -o "${CMAKE_BINARY_DIR}/buster-base.apk"
    # 2. Stage the native library under lib/<abi>/.
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${BUSTER_APK_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BUSTER_APK_STAGE}/lib/${ANDROID_ABI}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:ide>" "${BUSTER_APK_STAGE}/lib/${ANDROID_ABI}/libide.so"
    # 3. Add the native library into the APK.
    COMMAND "${BUSTER_JAR}" uf "${CMAKE_BINARY_DIR}/buster-base.apk" -C "${BUSTER_APK_STAGE}" "lib/${ANDROID_ABI}/libide.so"
    # 4. Align before signing, then sign with APK Signature Scheme v2/v3.
    COMMAND "${BUSTER_BASH_EXECUTABLE}" "${BUSTER_ANDROID_SIGN_SCRIPT}"
        "${BUSTER_APKSIGNER}"
        "${BUSTER_ZIPALIGN}"
        "${BUSTER_KEYSTORE}"
        "${CMAKE_BINARY_DIR}/buster-base.apk"
        "${BUSTER_APK}"
    DEPENDS ide "${BUSTER_ANDROID_MANIFEST}" "${BUSTER_ANDROID_SIGN_SCRIPT}"
        "${BUSTER_ANDROID_TEST_INVENTORY}" ${BUSTER_ANDROID_TEST_FIXTURES}
        ${BUSTER_ANDROID_SHADER_ASSET_DEPENDS}
    COMMENT "Packaging buster.apk"
    VERBATIM
)
add_custom_target(apk DEPENDS "${BUSTER_APK}")
