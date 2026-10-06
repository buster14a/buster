# Standalone disk-image builder, independent of ide and its module closure.
# It is a plain default target so every configuration compiles it, and test_all
# depends on it so the normal compiler-test route cannot go green with a source
# that no longer builds. It is not run: it reads and writes build/*.img.
# Only Linux hosts validate it today; Windows and Apple builds are unverified.
if (BUSTER_LINK_LIBC AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # entry_point.c detects the CPU model through the architecture module.
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
        set(BUSTER_DISK_BUILDER_ARCHITECTURE_MODULE aarch64)
    else()
        set(BUSTER_DISK_BUILDER_ARCHITECTURE_MODULE x86_64)
    endif()
    executable_add(disk_builder OFF src/buster/apps/disk_builder.c
        MODULES os entry_point arena integer string file hash time float target ${BUSTER_DISK_BUILDER_ARCHITECTURE_MODULE})
    if (UNIX)
        target_link_libraries(disk_builder PRIVATE m)
    endif()
    if (TARGET test_all)
        add_dependencies(test_all disk_builder)
    endif()
endif()
