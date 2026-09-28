# Native-target compatibility exceptions for GNU-family -march=native builds.
#
# Entry point: buster_native_target_compatibility_flags(). CMakeLists.txt
# appends its result to GNU_FAMILY_NATIVE_TARGET for every configuration,
# independent of BUSTER_CHECK_OPTIONAL_WARNINGS.
#
# Legacy upstream Clang AVX10 detection (issue #1501): before LLVM 22.1.0 the
# x86 host query for -march=native read CPUID leaf 0x24 without selecting
# subleaf 0 and collapsed the AVX10 version to a Boolean. On some AVX10 hosts
# the detected set then triggers "invalid feature combination: +avx10.1-256;
# will be promoted to avx10.1-512", which -Werror makes fatal. Upstream fixed
# the query in llvm/llvm-project#172350 (commit b6f210b2); llvmorg-22.1.0 is
# the first stable release containing it. The exception applies only to
# native x86-64 upstream Clang older than 22.1.0 whose -march=native compile
# fails solely because that diagnostic group is an error on this host. It keeps
# the warning (-Wno-error=, not -Wno-) and does not repair CPU detection.
# Retire it once no supported native producer uses upstream Clang < 22.1.0.

include(CheckCSourceCompiles)

set(BUSTER_NATIVE_AVX10_FIXED_CLANG_VERSION 22.1.0)
set(BUSTER_NATIVE_AVX10_EXCEPTION_FLAG -Wno-error=invalid-feature-combination)

# Pure policy: whether a configuration may carry the legacy AVX10 exception.
function(buster_native_avx10_exception_candidate out_var compiler_id is_zig compiler_version processor cross_compile)
    set(candidate OFF)
    if (compiler_id STREQUAL "Clang" AND NOT is_zig AND NOT cross_compile AND
        processor MATCHES "^(x86_64|amd64|X86_64|X86-64|AMD64)$" AND
        compiler_version VERSION_LESS BUSTER_NATIVE_AVX10_FIXED_CLANG_VERSION)
        set(candidate ON)
    endif()
    set(${out_var} ${candidate} PARENT_SCOPE)
endfunction()

# Pure policy: whether host probe results demonstrate the AVX10 diagnostic.
# The exception requires -march=native to fail with the group as an error and
# to succeed with only that group downgraded. Any other failure stays fatal.
function(buster_native_avx10_exception_flags out_var candidate fatal_probe_compiles tolerant_probe_compiles)
    set(flags "")
    if (candidate AND NOT fatal_probe_compiles AND tolerant_probe_compiles)
        set(flags ${BUSTER_NATIVE_AVX10_EXCEPTION_FLAG})
    endif()
    set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()

function(buster_native_target_compatibility_flags out_var compiler_id is_zig compiler_version processor cross_compile)
    buster_native_avx10_exception_candidate(candidate "${compiler_id}" "${is_zig}" "${compiler_version}" "${processor}" "${cross_compile}")
    set(flags "")
    if (candidate)
        set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
        set(saved_required_quiet "${CMAKE_REQUIRED_QUIET}")
        set(CMAKE_REQUIRED_QUIET ON)
        set(probe_source "int main(void) { return 0; }\n")
        set(CMAKE_REQUIRED_FLAGS "${saved_required_flags} -march=native -Werror -Werror=unknown-warning-option -Werror=invalid-feature-combination")
        check_c_source_compiles("${probe_source}" BUSTER_NATIVE_AVX10_FATAL_PROBE)
        set(CMAKE_REQUIRED_FLAGS "${saved_required_flags} -march=native -Werror -Werror=unknown-warning-option ${BUSTER_NATIVE_AVX10_EXCEPTION_FLAG}")
        check_c_source_compiles("${probe_source}" BUSTER_NATIVE_AVX10_TOLERANT_PROBE)
        set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")
        set(CMAKE_REQUIRED_QUIET "${saved_required_quiet}")
        buster_native_avx10_exception_flags(flags ON "${BUSTER_NATIVE_AVX10_FATAL_PROBE}" "${BUSTER_NATIVE_AVX10_TOLERANT_PROBE}")
        if (flags)
            message(STATUS "Legacy Clang AVX10 -march=native exception (#1501): ${flags}")
        endif()
    endif()
    set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()
