#include <buster/tests/target_test.h>

BUSTER_TEST_F_DECL UnitTestResult target_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    struct
    {
        String8 triple;
        CpuArch architecture;
        OperatingSystem operating_system;
        u16 version_major;
        u8 version_minor;
        u8 version_patch;
    } cases[] = {
        {
            S8("x86_64-unknown-linux-gnu"),
            CPU_ARCH_X86_64,
            OPERATING_SYSTEM_LINUX,
        },
        {
            S8("aarch64-linux-android35"),
            CPU_ARCH_AARCH64,
            OPERATING_SYSTEM_ANDROID,
            35,
        },
        {
            S8("x86_64-pc-windows-msvc"),
            CPU_ARCH_X86_64,
            OPERATING_SYSTEM_WINDOWS,
        },
        {
            S8("aarch64-apple-darwin"),
            CPU_ARCH_AARCH64,
            OPERATING_SYSTEM_MACOS,
        },
        {
            S8("arm64-apple-ios17.0-simulator"),
            CPU_ARCH_AARCH64,
            OPERATING_SYSTEM_IOS,
            17,
        },
        {
            S8("x86_64-unknown-uefi"),
            CPU_ARCH_X86_64,
            OPERATING_SYSTEM_UEFI,
        },
        {
            S8("aarch64-none-elf"),
            CPU_ARCH_AARCH64,
            OPERATING_SYSTEM_FREESTANDING,
        },
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
    {
        TargetParseResult parsed = target_parse_triple(cases[case_index].triple);
        BUSTER_TEST(arguments, parsed.error == TARGET_PARSE_ERROR_NONE);
        BUSTER_TEST(arguments, parsed.target.cpu_arch == cases[case_index].architecture);
        BUSTER_TEST(arguments, parsed.target.os == cases[case_index].operating_system);
        BUSTER_TEST(arguments, parsed.target.cpu_model == CPU_MODEL_BASELINE);
        BUSTER_TEST(arguments, parsed.target.os_version_major == cases[case_index].version_major);
        BUSTER_TEST(arguments, parsed.target.os_version_minor == cases[case_index].version_minor);
        BUSTER_TEST(arguments, parsed.target.os_version_patch == cases[case_index].version_patch);
    }
    TargetParseResult bad_architecture = target_parse_triple(S8("riscv64-unknown-linux-gnu"));
    BUSTER_TEST(arguments, bad_architecture.error == TARGET_PARSE_ERROR_ARCHITECTURE);
    TargetParseResult bad_operating_system = target_parse_triple(S8("x86_64-unknown-haiku"));
    BUSTER_TEST(arguments, bad_operating_system.error == TARGET_PARSE_ERROR_OPERATING_SYSTEM);
    BUSTER_TEST(arguments, cpu_model_from_string(S8("znver5")) == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, cpu_model_from_string(S8("apple-m4")) == CPU_MODEL_A64_APPLE_M4);
    BUSTER_TEST(arguments, cpu_model_from_string(S8("not-a-processor")) == CPU_MODEL_ERROR);
    BUSTER_STRING_TEST(arguments, cpu_model_to_string_os(CPU_MODEL_ERROR), S8("error"));
    BUSTER_TEST(arguments, cpu_model_resolve_detected(CPU_MODEL_ERROR) == CPU_MODEL_NATIVE);
    BUSTER_TEST(arguments, cpu_model_resolve_detected(CPU_MODEL_AMD_ZEN_5) == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, cpu_model_supports_arch(CPU_MODEL_AMD_ZEN_5, CPU_ARCH_X86_64));
    BUSTER_TEST(arguments, !cpu_model_supports_arch(CPU_MODEL_AMD_ZEN_5, CPU_ARCH_AARCH64));
    BUSTER_TEST(arguments, cpu_model_supports_arch(CPU_MODEL_A64_APPLE_M4, CPU_ARCH_AARCH64));
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_5) & TARGET_CPU_FEATURE_X86_AVX512BW) != 0);
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8_SSE3) & TARGET_CPU_FEATURE_X86_SSE3) != 0);
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8) & TARGET_CPU_FEATURE_X86_SSE3) == 0);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL) & (TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512VL)) == 0);
    Target valid_avx512 = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F,
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx512));
    Target invalid_avx2 = valid_avx512;
    invalid_avx2.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX2;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx2));
    Target invalid_sse3 = valid_avx512;
    invalid_sse3.cpu_features = TARGET_CPU_FEATURE_X86_SSE3;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_sse3));
    Target invalid_avx512vl = valid_avx512;
    invalid_avx512vl.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX512VL;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512vl));
    Target invalid_avx512bw = valid_avx512;
    invalid_avx512bw.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX512BW;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512bw));
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("avx2")) == TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("sse3")) == TARGET_CPU_FEATURE_X86_SSE3);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("neon")) == TARGET_CPU_FEATURE_AARCH64_NEON);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("avx2")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_AVX512F), S8("avx512f"));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, valid_avx512), S8("avx,avx2,avx512f,sse2"));
    TargetDataLayout linux_x86_layout = target_data_layout((Target){
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    });
    BUSTER_TEST(arguments, target_data_layout_is_valid(linux_x86_layout));
    BUSTER_TEST(arguments, linux_x86_layout.pointer.size == 8 && linux_x86_layout.pointer.alignment == 8);
    BUSTER_TEST(arguments, linux_x86_layout.long_integer.bit_width == 64);
    BUSTER_TEST(arguments, linux_x86_layout.long_double_type.bit_width == 80);
    BUSTER_TEST(arguments, linux_x86_layout.endianness == TARGET_ENDIAN_LITTLE);
    BUSTER_TEST(arguments, linux_x86_layout.atomic_min_width == 8 && linux_x86_layout.atomic_max_width == 128);
    TargetDataLayout windows_x86_layout = target_data_layout((Target){
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_WINDOWS,
    });
    BUSTER_TEST(arguments, windows_x86_layout.long_integer.bit_width == 32);
    BUSTER_TEST(arguments, windows_x86_layout.long_double_type.bit_width == 64);
    TargetDataLayout linux_arm_layout = target_data_layout((Target){
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    });
    BUSTER_TEST(arguments, !linux_arm_layout.plain_char_is_signed);
    BUSTER_TEST(arguments, target_data_layout_is_valid(linux_arm_layout));
    return result;
}
