option(BUSTER_COMPILER_SDK "Build and install the static compiler embedding library" OFF)

if (BUSTER_COMPILER_SDK)
    if (CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        message(FATAL_ERROR "BUSTER_COMPILER_SDK requires a desktop platform")
    endif()
    if (BUSTER_FUZZ_AVAILABLE)
        message(FATAL_ERROR "BUSTER_COMPILER_SDK is unavailable in fuzz builds")
    endif()

    set(BUSTER_SDK_MODULES
        os
        arena
        integer
        string
        float
        compiler_c_frontend
        compiler_aarch64_encoding
        compiler_aarch64_exact_bridge
        compiler_aarch64_control_semantics
        compiler_aarch64_system_registers
        compiler_aarch64_semantics
        compiler_aarch64_system_semantics
        compiler_aarch64_semantic_vm
        compiler_aarch64_simd_semantics
        compiler_aarch64_direct_simd
        compiler_aarch64_complex_simd
        compiler_aarch64_memory_semantics
        compiler_aarch64_alias_projection
        compiler_aarch64_base_assembly
        compiler_assembly
        compiler_assembly_unit
        compiler_assembly_metadata
        compiler_assembly_completion_census
        compiler_ir
        compiler_diagnostic
        compiler_debug
        compiler_machine
        compiler_bootstrap_trace
        compiler_codegen
        compiler_investigation
        compiler_dwarf
        compiler_codeview
        compiler_pdb
        compiler_object
        compiler_jit
        compiler_link
        compiler_wasm
        compiler_gpu
        compiler_llvm_bitcode
        compiler_ebpf
        compiler_spirv
        compiler_driver
        compiler_embed
        simd
        file
        target
        time
        hash
        byte_writer
    )
    buster_resolve_modules(BUSTER_SDK_SOURCES BUSTER_SDK_MODULE_NAMES ${BUSTER_SDK_MODULES})
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$")
        list(APPEND BUSTER_SDK_SOURCES ${BUSTER_MODULE_X86_64})
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|a64|AARCH64|ARM64|A64)$")
        list(APPEND BUSTER_SDK_SOURCES ${BUSTER_MODULE_AARCH64})
    endif()
    list(REMOVE_DUPLICATES BUSTER_SDK_SOURCES)

    add_library(buster_compiler STATIC ${BUSTER_SDK_SOURCES})
    target_include_directories(buster_compiler
        PRIVATE ${BUSTER_SHADER_GENERATED_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src
        PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
               $<INSTALL_INTERFACE:include>
    )
    if (BUSTER_USE_VULKAN AND BUSTER_VULKAN_INCLUDE_DIR)
        target_include_directories(buster_compiler PRIVATE ${BUSTER_VULKAN_INCLUDE_DIR})
    endif()
    target_compile_definitions(buster_compiler PRIVATE
        BUSTER_UNITY_BUILD=0
        BUSTER_OPTIMIZE=${BUSTER_OPTIMIZE_DEFINE}
        BUSTER_INCLUDE_TESTS=0
        BUSTER_SANITIZE=$<IF:$<BOOL:${BUSTER_SANITIZE}>,1,0>
        BUSTER_LSAN_SUPPORTED=$<IF:$<BOOL:${BUSTER_LSAN_SUPPORTED}>,1,0>
        BUSTER_FUZZ_AVAILABLE=0
        BUSTER_LINK_LIBC=$<IF:$<BOOL:${BUSTER_LINK_LIBC}>,1,0>
        BUSTER_SINGLE_THREADED=$<IF:$<BOOL:${BUSTER_SINGLE_THREADED}>,1,0>
        BUSTER_USE_VULKAN=$<IF:$<BOOL:${BUSTER_USE_VULKAN}>,1,0>
        BUSTER_USE_D3D12=$<IF:$<BOOL:${BUSTER_USE_D3D12}>,1,0>
        BUSTER_USE_METAL=$<IF:$<BOOL:${BUSTER_USE_METAL}>,1,0>
        BUSTER_USE_SLANG_SHADERS=$<IF:$<BOOL:${BUSTER_USE_SLANG_SHADERS_DEFINE}>,1,0>
        BUSTER_COMPILER_ZIG=$<IF:$<BOOL:${C_COMPILER_ZIG}>,1,0>
        BUSTER_HOST_C_COMPILER="${CMAKE_C_COMPILER}"
        BUSTER_HOST_C_COMPILER_ID="${CMAKE_C_COMPILER_ID}"
        BUSTER_HOST_C_COMPILER_ARG1="${CMAKE_C_COMPILER_ARG1}"
        BUSTER_HOST_C_RESOURCE_INCLUDE="${BUSTER_HOST_C_RESOURCE_INCLUDE}"
        BUSTER_HOST_C_COMPILER_MSVC=$<IF:$<BOOL:${C_COMPILER_MSVC}>,1,0>
        BUSTER_BENCH_ALLOCATIONS=$<IF:$<BOOL:${BUSTER_BENCH_ALLOCATIONS}>,1,0>
        BUSTER_INSTRUMENT=$<IF:$<BOOL:${BUSTER_INSTRUMENT}>,1,0>
        BUSTER_EMBED_STANDALONE_RUNTIME=1
    )
    if (C_COMPILER_GNU_FAMILY)
        target_compile_options(buster_compiler PRIVATE
            ${GNU_FAMILY_OPTIONS}
            ${GNU_FAMILY_WARNINGS}
            ${GNU_FAMILY_DISABLE_WARNINGS}
        )
        if (BUSTER_CI)
            target_compile_options(buster_compiler PRIVATE "$<$<CONFIG:Release>:-O2>")
        endif()
    elseif (C_COMPILER_MSVC_FAMILIY)
        target_compile_options(buster_compiler PRIVATE ${MSVC_FAMILY_OPTIONS} ${MSVC_FAMILY_WARNINGS} ${MSVC_FAMILY_DISABLE_WARNINGS})
    endif()
    if (C_COMPILER_CLANG_FAMILY)
        target_compile_options(buster_compiler PRIVATE ${CLANG_GNU_FAMILY_WARNINGS})
    endif()
    if (NOT BUSTER_CROSS_COMPILE AND C_COMPILER_GNU_FAMILY)
        target_compile_options(buster_compiler PRIVATE ${GNU_FAMILY_NATIVE_TARGET})
    endif()
    if (BUSTER_SANITIZE)
        target_compile_options(buster_compiler PRIVATE -fsanitize=address,undefined,bounds -fno-sanitize-recover=all)
    elseif (BUSTER_THREAD_SANITIZE)
        target_compile_options(buster_compiler PRIVATE -fsanitize=thread)
    endif()
    buster_apply_module_options(buster_compiler ${BUSTER_SDK_MODULE_NAMES})

    if (UNIX)
        target_link_libraries(buster_compiler INTERFACE m)
    endif()
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(buster_compiler INTERFACE dl pthread)
    elseif (WIN32)
        target_link_libraries(buster_compiler INTERFACE ws2_32)
    endif()

    include(GNUInstallDirs)
    install(TARGETS buster_compiler EXPORT BusterCompilerTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES "${CMAKE_SOURCE_DIR}/src/buster/lib/compiler/embed/embed.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/buster/lib/compiler/embed"
    )
    install(EXPORT BusterCompilerTargets
        FILE BusterCompilerTargets.cmake
        NAMESPACE Buster::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BusterCompiler"
    )
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/BusterCompilerConfig.cmake"
        "include(\"\${CMAKE_CURRENT_LIST_DIR}/BusterCompilerTargets.cmake\")\n"
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/BusterCompilerConfig.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BusterCompiler"
    )
endif()
