#include <buster/tests/target_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL bool target_test_cpu_features_intersect(TargetCpuFeatures left, TargetCpuFeatures right)
{
    return target_cpu_features_any(target_cpu_features_intersection(left, right));
}

UnitTestResult target_tests(UnitTestArguments* arguments)
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
    // A CPU model in a target string used to be dropped, leaving baseline code
    // generation behind: reject it and keep `-march=` the one way to ask.
    struct
    {
        String8 triple;
        String8 invalid_component;
        TargetParseError error;
    } rejected_component_cases[] = {
        {S8("x86_64-unknown-linux-gnu-znver4"), S8("znver4"), TARGET_PARSE_ERROR_CPU_MODEL},
        {S8("x86_64-linux-gnu-znver4"), S8("znver4"), TARGET_PARSE_ERROR_CPU_MODEL},
        {S8("x86_64-linux-baseline"), S8("baseline"), TARGET_PARSE_ERROR_CPU_MODEL},
        {S8("arm64-apple-ios17.0-simulator-extra"), S8("extra"), TARGET_PARSE_ERROR_EXCESS_COMPONENT},
        {S8("x86_64-unknown-linux-gnu-notacpu"), S8("notacpu"), TARGET_PARSE_ERROR_EXCESS_COMPONENT},
        {S8("x86_64-unknown-linux-gnu-xxxx"), S8("xxxx"), TARGET_PARSE_ERROR_EXCESS_COMPONENT},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(rejected_component_cases); case_index += 1)
    {
        TargetParseResult rejected = target_parse_triple(rejected_component_cases[case_index].triple);
        BUSTER_TEST(arguments, rejected.error == rejected_component_cases[case_index].error);
        BUSTER_STRING_TEST(arguments, rejected.invalid_component, rejected_component_cases[case_index].invalid_component);
    }
    // The vendor and the environment stay free-form within the four components
    // a target string has, and `native` keeps bypassing component parsing.
    BUSTER_TEST(arguments, target_parse_triple(S8("x86_64-alpine-linux-musl")).error == TARGET_PARSE_ERROR_NONE);
    BUSTER_TEST(arguments, target_parse_triple(S8("x86_64-w64-windows-gnu")).error == TARGET_PARSE_ERROR_NONE);
    BUSTER_TEST(arguments, target_parse_triple(S8("native")).error == TARGET_PARSE_ERROR_NONE);
    BUSTER_TEST(arguments, cpu_model_from_string(S8("znver5")) == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, cpu_model_from_string(S8("apple-m4")) == CPU_MODEL_A64_APPLE_M4);
    BUSTER_TEST(arguments, cpu_model_from_string(S8("not-a-processor")) == CPU_MODEL_ERROR);
    BUSTER_STRING_TEST(arguments, cpu_model_to_string_os(CPU_MODEL_ERROR), S8("error"));
    BUSTER_TEST(arguments, cpu_model_resolve_detected(CPU_MODEL_ERROR) == CPU_MODEL_NATIVE);
    BUSTER_TEST(arguments, cpu_model_resolve_detected(CPU_MODEL_AMD_ZEN_5) == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, cpu_model_supports_arch(CPU_MODEL_AMD_ZEN_5, CPU_ARCH_X86_64));
    BUSTER_TEST(arguments, !cpu_model_supports_arch(CPU_MODEL_AMD_ZEN_5, CPU_ARCH_AARCH64));
    BUSTER_TEST(arguments, cpu_model_supports_arch(CPU_MODEL_A64_APPLE_M4, CPU_ARCH_AARCH64));
    TargetCpuFeatures rocketlake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_ROCKETLAKE);
    TargetCpuFeatures rocketlake_avx512 = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW, TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA, TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512BITALG, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ, TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ}, 16);
    BUSTER_TEST(arguments, target_cpu_features_subset(rocketlake_avx512, rocketlake_features));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(rocketlake_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                   TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512FP16,
                                                                   TARGET_CPU_FEATURE_X86_AVX_VNNI}, 3)));
    TargetCpuFeatures zen4_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_4);
    TargetCpuFeatures rocketlake_avx512_bf16 = target_cpu_features_add(rocketlake_avx512, TARGET_CPU_FEATURE_X86_AVX512BF16);
    BUSTER_TEST(arguments, target_cpu_features_subset(rocketlake_avx512_bf16, zen4_features));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(zen4_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                   TARGET_CPU_FEATURE_X86_AVX512FP16, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT,
                                                                   TARGET_CPU_FEATURE_X86_AVX_VNNI}, 3)));
    TargetCpuFeatures zen5_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_5);
    TargetCpuFeatures zen5_expected = target_cpu_features_add(rocketlake_avx512_bf16, TARGET_CPU_FEATURE_X86_AVX_VNNI);
    zen5_expected = target_cpu_features_add(zen5_expected, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT);
    BUSTER_TEST(arguments, target_cpu_features_subset(zen5_expected, zen5_features));
    BUSTER_TEST(arguments, target_cpu_features_equal(target_cpu_features_difference(zen5_features, zen4_features),
                                                     target_cpu_features_from_array((TargetCpuFeature const[]){
                                                         TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT}, 2)));
    BUSTER_TEST(arguments, !target_cpu_features_contains(zen5_features, TARGET_CPU_FEATURE_X86_AVX512FP16));
    TargetCpuFeatures tigerlake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_TIGERLAKE);
    BUSTER_TEST(arguments, target_cpu_features_contains(tigerlake_features, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT));
    BUSTER_TEST(arguments, !target_cpu_features_contains(tigerlake_features, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, !target_cpu_features_contains(rocketlake_features, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT));
    BUSTER_TEST(arguments, target_cpu_features_equal(target_cpu_features_difference(tigerlake_features, rocketlake_features),
                                                     target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT)));
    TargetCpuFeatures zen1_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_1);
    TargetCpuFeatures zen2_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_2);
    TargetCpuFeatures zen3_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_3);
    TargetCpuFeatures aes_pclmul = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL}, 2);
    TargetCpuFeatures haswell_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL);
    TargetCpuFeatures broadwell_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_BROADWELL);
    BUSTER_TEST(arguments, target_cpu_features_contains(haswell_features, TARGET_CPU_FEATURE_X86_PCLMUL));
    BUSTER_TEST(arguments, target_cpu_features_contains(broadwell_features, TARGET_CPU_FEATURE_X86_PCLMUL));
    BUSTER_TEST(arguments, !target_cpu_features_contains(haswell_features, TARGET_CPU_FEATURE_X86_AES));
    BUSTER_TEST(arguments, !target_cpu_features_contains(broadwell_features, TARGET_CPU_FEATURE_X86_AES));
    BUSTER_TEST(arguments, target_cpu_features_subset(aes_pclmul, zen1_features));
    BUSTER_TEST(arguments, target_cpu_features_subset(aes_pclmul, zen2_features));
    BUSTER_TEST(arguments, target_cpu_features_subset(aes_pclmul, zen3_features));
    BUSTER_TEST(arguments, target_cpu_features_subset(target_cpu_features_from_array((TargetCpuFeature const[]){
                                                               TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ}, 2),
                                                     zen3_features));
    CpuModel aes_pclmul_models[] = {
        CPU_MODEL_INTEL_SKYLAKE,
        CPU_MODEL_INTEL_SKYLAKE_AVX512,
        CPU_MODEL_INTEL_ROCKETLAKE,
        CPU_MODEL_INTEL_COOPERLAKE,
        CPU_MODEL_INTEL_CASCADELAKE,
        CPU_MODEL_INTEL_CANNONLAKE,
        CPU_MODEL_INTEL_ICELAKE_CLIENT,
        CPU_MODEL_INTEL_ICELAKE_SERVER,
        CPU_MODEL_INTEL_TIGERLAKE,
        CPU_MODEL_INTEL_EMERALD_RAPIDS,
        CPU_MODEL_INTEL_SAPPHIRE_RAPIDS,
        CPU_MODEL_INTEL_GRANITE_RAPIDS,
        CPU_MODEL_INTEL_GRANITE_RAPIDS_D,
        CPU_MODEL_INTEL_DIAMOND_RAPIDS,
    };
    for (u32 model_index = 0; model_index < BUSTER_ARRAY_LENGTH(aes_pclmul_models); model_index += 1)
    {
        TargetCpuFeatures model_features = target_cpu_features_default(CPU_ARCH_X86_64, aes_pclmul_models[model_index]);
        BUSTER_TEST(arguments, target_cpu_features_subset(aes_pclmul, model_features));
    }
    TargetCpuFeatures bt2_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BT_2);
    TargetCpuFeatures bt2_expected = target_cpu_features_add(aes_pclmul, TARGET_CPU_FEATURE_X86_AVX);
    BUSTER_TEST(arguments, target_cpu_features_subset(bt2_expected, bt2_features));
    CpuModel bdver1_models[] = {CPU_MODEL_AMD_BD_1, CPU_MODEL_AMD_BD_2, CPU_MODEL_AMD_BD_3};
    for (u32 model_index = 0; model_index < BUSTER_ARRAY_LENGTH(bdver1_models); model_index += 1)
    {
        TargetCpuFeatures model_features = target_cpu_features_default(CPU_ARCH_X86_64, bdver1_models[model_index]);
        BUSTER_TEST(arguments, target_cpu_features_subset(bt2_expected, model_features));
    }
    TargetCpuFeatures bdver4_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_4);
    TargetCpuFeatures bdver4_expected = target_cpu_features_add(bt2_expected, TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, target_cpu_features_subset(bdver4_expected, bdver4_features));
    TargetCpuFeatures tremont_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_TREMONT);
    BUSTER_TEST(arguments, !target_cpu_features_contains(tremont_features, TARGET_CPU_FEATURE_X86_CLDEMOTE));
    BUSTER_TEST(arguments, !target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GOLDMONT),
                                                        TARGET_CPU_FEATURE_X86_CLDEMOTE));
    TargetCpuFeatures amd_3dnow_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA}, 2);
    BUSTER_TEST(arguments, target_cpu_features_subset(amd_3dnow_features,
                                                      target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8)));
    BUSTER_TEST(arguments,
                target_cpu_features_subset(amd_3dnow_features,
                                           target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8_SSE3)));
    BUSTER_TEST(arguments,
                target_cpu_features_subset(amd_3dnow_features,
                                           target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_AMD_FAMILY_10)));
    BUSTER_TEST(arguments, !target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8),
                                                         TARGET_CPU_FEATURE_X86_SSE4A));
    BUSTER_TEST(arguments, !target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8_SSE3),
                                                         TARGET_CPU_FEATURE_X86_SSE4A));
    for (CpuModel model = CPU_MODEL_AMD_AMD_FAMILY_10; model <= CPU_MODEL_AMD_ZEN_5; model += 1)
    {
        BUSTER_TEST(arguments, target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, model),
                                                            TARGET_CPU_FEATURE_X86_SSE4A));
    }
    BUSTER_TEST(arguments, !target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL),
                                                         TARGET_CPU_FEATURE_X86_SSE4A));
    TargetCpuFeatures amd_bd_isa = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_LWP, TARGET_CPU_FEATURE_X86_XOP}, 3);
    TargetCpuFeatures amd_bd2_isa = target_cpu_features_add(amd_bd_isa, TARGET_CPU_FEATURE_X86_TBM);
    TargetCpuFeatures amd_bd4_isa = target_cpu_features_add(amd_bd2_isa, TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, target_cpu_features_subset(amd_bd_isa,
                                                      target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_1)));
    BUSTER_TEST(arguments, target_cpu_features_subset(amd_bd2_isa,
                                                      target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_2)));
    BUSTER_TEST(arguments, target_cpu_features_subset(amd_bd2_isa,
                                                      target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_3)));
    BUSTER_TEST(arguments, target_cpu_features_subset(amd_bd4_isa,
                                                      target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_4)));
    for (CpuModel model = CPU_MODEL_AMD_BD_1; model <= CPU_MODEL_AMD_BD_4; model += 1)
    {
        BUSTER_TEST(arguments, !target_test_cpu_features_intersect(target_cpu_features_default(CPU_ARCH_X86_64, model), amd_3dnow_features));
    }
    Target valid_amd_extended = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_XOP, TARGET_CPU_FEATURE_X86_LWP, TARGET_CPU_FEATURE_X86_TBM, TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA, TARGET_CPU_FEATURE_X86_LZCNT}, 9),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_amd_extended));
    Target invalid_amd_3dnowa_dependency = valid_amd_extended;
    invalid_amd_3dnowa_dependency.cpu_features =
        target_cpu_features_remove(invalid_amd_3dnowa_dependency.cpu_features, TARGET_CPU_FEATURE_X86_3DNOW);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_amd_3dnowa_dependency));
    Target invalid_amd_fma4_dependency = valid_amd_extended;
    invalid_amd_fma4_dependency.cpu_features =
        target_cpu_features_remove(invalid_amd_fma4_dependency.cpu_features, TARGET_CPU_FEATURE_X86_AVX);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_amd_fma4_dependency));
    TargetCpuFeatures adl_family = target_cpu_features_union(aes_pclmul, target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ,
        TARGET_CPU_FEATURE_X86_AVX_VNNI}, 4));
    CpuModel adl_family_models[] = {
        CPU_MODEL_INTEL_ALDERLAKE,
        CPU_MODEL_INTEL_RAPTORLAKE,
        CPU_MODEL_INTEL_METEORLAKE,
        CPU_MODEL_INTEL_GRACEMONT,
    };
    for (u32 model_index = 0; model_index < BUSTER_ARRAY_LENGTH(adl_family_models); model_index += 1)
    {
        TargetCpuFeatures model_features = target_cpu_features_default(CPU_ARCH_X86_64, adl_family_models[model_index]);
        BUSTER_TEST(arguments, target_cpu_features_subset(adl_family, model_features));
    }
    TargetCpuFeatures arrow_lake = target_cpu_features_union(adl_family, target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX_IFMA, TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8}, 3));
    TargetCpuFeatures arrow_lake_s = target_cpu_features_add(arrow_lake, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16);
    TargetCpuFeatures arrow_lake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_ARROWLAKE);
    TargetCpuFeatures arrow_lake_s_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_ARROWLAKE_S);
    TargetCpuFeatures lunar_lake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_LUNARLAKE);
    TargetCpuFeatures panther_lake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_PANTHERLAKE);
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake, arrow_lake_features));
    BUSTER_TEST(arguments, !target_cpu_features_contains(arrow_lake_features, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16));
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake_s, arrow_lake_s_features));
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake_s, lunar_lake_features));
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake_s, panther_lake_features));
    TargetCpuFeatures sierra_forest_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_SIERRAFOREST);
    TargetCpuFeatures grandridge_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GRANDRIDGE);
    TargetCpuFeatures clearwater_forest_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_CLEARWATERFOREST);
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake, sierra_forest_features));
    BUSTER_TEST(arguments, !target_cpu_features_contains(sierra_forest_features, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16));
    BUSTER_TEST(arguments, target_cpu_features_contains(sierra_forest_features, TARGET_CPU_FEATURE_X86_CLDEMOTE));
    BUSTER_TEST(arguments, !target_cpu_features_contains(sierra_forest_features, TARGET_CPU_FEATURE_X86_PREFETCHI));
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake, grandridge_features));
    BUSTER_TEST(arguments, target_cpu_features_contains(grandridge_features, TARGET_CPU_FEATURE_X86_CLDEMOTE));
    BUSTER_TEST(arguments, !target_cpu_features_contains(grandridge_features, TARGET_CPU_FEATURE_X86_PREFETCHI));
    BUSTER_TEST(arguments, target_cpu_features_subset(arrow_lake_s, clearwater_forest_features));
    BUSTER_TEST(arguments, target_cpu_features_subset(target_cpu_features_from_array((TargetCpuFeature const[]){
                                                               TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI}, 2),
                                                     clearwater_forest_features));
    BUSTER_TEST(arguments, !target_cpu_features_contains(clearwater_forest_features, TARGET_CPU_FEATURE_X86_IBT));
    TargetCpuFeatures knl_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL);
    TargetCpuFeatures knl_isa = target_cpu_features_union(target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
        TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512PF, TARGET_CPU_FEATURE_X86_AVX512ER}, 6), aes_pclmul);
    BUSTER_TEST(arguments, target_cpu_features_subset(knl_isa, knl_features));
    TargetCpuFeatures knm_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNM);
    TargetCpuFeatures knm_expected = target_cpu_features_union(knl_isa, target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX5124VNNIW, TARGET_CPU_FEATURE_X86_AVX5124FMAPS,
        TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ}, 3));
    BUSTER_TEST(arguments, target_cpu_features_subset(knm_expected, knm_features));
    TargetCpuFeatures granite_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GRANITE_RAPIDS);
    TargetCpuFeatures sapphire_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_SAPPHIRE_RAPIDS);
    TargetCpuFeatures emerald_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_EMERALD_RAPIDS);
    BUSTER_TEST(arguments, target_cpu_features_equal(emerald_rapids_features, sapphire_rapids_features));
    BUSTER_TEST(arguments, target_cpu_features_contains(sapphire_rapids_features, TARGET_CPU_FEATURE_X86_CLDEMOTE));
    BUSTER_TEST(arguments, !target_cpu_features_contains(sapphire_rapids_features, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, !target_cpu_features_contains(sapphire_rapids_features, TARGET_CPU_FEATURE_X86_SHSTK));
    BUSTER_TEST(arguments, !target_cpu_features_contains(sapphire_rapids_features, TARGET_CPU_FEATURE_X86_PREFETCHI));
    TargetCpuFeatures emerald_expected = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512FP16,
        TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8,
        TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AVX_VNNI}, 6);
    BUSTER_TEST(arguments, target_cpu_features_subset(emerald_expected, emerald_rapids_features));
    TargetCpuFeatures granite_rapids_inherited = target_cpu_features_union(sapphire_rapids_features,
                                                                            target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                                TARGET_CPU_FEATURE_X86_AVX10_1,
                                                                                TARGET_CPU_FEATURE_X86_AVX10_512,
                                                                                TARGET_CPU_FEATURE_X86_AMX_FP16}, 3));
    BUSTER_TEST(arguments, target_cpu_features_subset(granite_rapids_inherited, granite_rapids_features));
    TargetCpuFeatures cldemote_prefetchi = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI}, 2);
    BUSTER_TEST(arguments, target_cpu_features_subset(cldemote_prefetchi, granite_rapids_features));
    BUSTER_TEST(arguments, !target_cpu_features_contains(granite_rapids_features, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(granite_rapids_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                   TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AMX_COMPLEX,
                                                                   TARGET_CPU_FEATURE_X86_AVX10_V1_AUX}, 3)));
    TargetCpuFeatures granite_rapids_d_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GRANITE_RAPIDS_D);
    TargetCpuFeatures granite_rapids_d_expected = target_cpu_features_add(granite_rapids_inherited, TARGET_CPU_FEATURE_X86_AMX_COMPLEX);
    BUSTER_TEST(arguments, target_cpu_features_subset(granite_rapids_d_expected, granite_rapids_d_features));
    TargetCpuFeatures diamond_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    TargetCpuFeatures diamond_expected = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AMX_COMPLEX, TARGET_CPU_FEATURE_X86_AVX10_2,
        TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, TARGET_CPU_FEATURE_X86_APX,
        TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF, TARGET_CPU_FEATURE_X86_MOVRS,
        TARGET_CPU_FEATURE_X86_AMX_MOVRS, TARGET_CPU_FEATURE_X86_AMX_AVX512,
        TARGET_CPU_FEATURE_X86_AMX_FP8}, 9);
    diamond_expected = target_cpu_features_union(granite_rapids_inherited, diamond_expected);
    BUSTER_TEST(arguments, target_cpu_features_subset(diamond_expected, diamond_rapids_features));
    BUSTER_TEST(arguments, target_cpu_features_contains(diamond_rapids_features, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX));
    TargetCpuFeatures diamond_metadata = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI,
        TARGET_CPU_FEATURE_X86_MOVRS}, 3);
    BUSTER_TEST(arguments, target_cpu_features_subset(diamond_metadata, diamond_rapids_features));
    BUSTER_TEST(arguments, !target_cpu_features_contains(diamond_rapids_features, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, !target_cpu_features_contains(diamond_rapids_features, TARGET_CPU_FEATURE_X86_SHSTK));
    for (CpuModel model = CPU_MODEL_AMD_I486; model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS; model += 1)
    {
        Target default_target = {
            .cpu_arch = CPU_ARCH_X86_64,
            .cpu_model = model,
            .cpu_features_explicit = true,
            .cpu_features = target_cpu_features_default(CPU_ARCH_X86_64, model),
        };
        BUSTER_TEST(arguments, target_cpu_features_are_valid(default_target));
    }
    BUSTER_TEST(arguments, target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8_SSE3),
                                                        TARGET_CPU_FEATURE_X86_SSE3));
    BUSTER_TEST(arguments, !target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8),
                                                         TARGET_CPU_FEATURE_X86_SSE3));
    BUSTER_TEST(arguments,
                !target_test_cpu_features_intersect(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL),
                                                    target_cpu_features_from_array((TargetCpuFeature const[]){
                                                        TARGET_CPU_FEATURE_X86_AVX512BW, TARGET_CPU_FEATURE_X86_AVX512VL}, 2)));
    TargetCpuFeatures bit_atomic_mask = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT, TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_CX16}, 4);
    TargetCpuFeatures amd_legacy_bit_atomic = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT, TARGET_CPU_FEATURE_X86_CX16}, 3);
    TargetCpuFeatures intel_atom_bit_atomic = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_CX16}, 2);
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_AMD_FAMILY_10), bit_atomic_mask),
                                          amd_legacy_bit_atomic));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BT_1), bit_atomic_mask),
                                          amd_legacy_bit_atomic));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BT_2), bit_atomic_mask),
                                          bit_atomic_mask));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_1), bit_atomic_mask),
                                          amd_legacy_bit_atomic));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_2), bit_atomic_mask),
                                          bit_atomic_mask));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_BONNELL), bit_atomic_mask),
                                          target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_CX16)));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_SILVERMONT), bit_atomic_mask),
                                          intel_atom_bit_atomic));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GOLDMONT), bit_atomic_mask),
                                          intel_atom_bit_atomic));
    BUSTER_TEST(arguments,
                target_cpu_features_equal(target_cpu_features_intersection(
                                              target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL), bit_atomic_mask),
                                          bit_atomic_mask));
    TargetCpuFeatures zen5_bit_atomic_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_5);
    TargetCpuFeatures all_bit_atomic = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT,
        TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_CX16}, 4);
    BUSTER_TEST(arguments, target_cpu_features_subset(all_bit_atomic, zen5_bit_atomic_features));
    TargetCpuFeatures haswell_bit_atomic_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL);
    BUSTER_TEST(arguments, target_cpu_features_subset(all_bit_atomic, haswell_bit_atomic_features));
    BUSTER_TEST(arguments, target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_CORE_2),
                                                        TARGET_CPU_FEATURE_X86_CX16));
    BUSTER_TEST(arguments, !target_cpu_features_contains(target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_CORE_2),
                                                         TARGET_CPU_FEATURE_X86_POPCNT));
    Target valid_avx512 = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F}, 4),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx512));
    Target valid_bit_atomic = valid_avx512;
    valid_bit_atomic.cpu_features = target_cpu_features_union(valid_bit_atomic.cpu_features, all_bit_atomic);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_bit_atomic));
    Target invalid_avx2 = valid_avx512;
    invalid_avx2.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX2}, 2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx2));
    Target invalid_avx512f_without_avx2 = valid_avx512;
    invalid_avx512f_without_avx2.cpu_features =
        target_cpu_features_remove(invalid_avx512f_without_avx2.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512f_without_avx2));
    Target invalid_sse3 = valid_avx512;
    invalid_sse3.cpu_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_SSE3);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_sse3));
    Target valid_sse4a = valid_avx512;
    valid_sse4a.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                          TARGET_CPU_FEATURE_X86_SSE3,
                                                                                          TARGET_CPU_FEATURE_X86_SSE4A},
                                                               3);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_sse4a));
    Target invalid_sse4a = valid_sse4a;
    invalid_sse4a.cpu_features = target_cpu_features_remove(invalid_sse4a.cpu_features, TARGET_CPU_FEATURE_X86_SSE3);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_sse4a));
    Target invalid_avx512vl = valid_avx512;
    invalid_avx512vl.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX512VL}, 3);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512vl));
    Target invalid_avx512bw = valid_avx512;
    invalid_avx512bw.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX512BW}, 3);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512bw));
    Target valid_gfni = valid_avx512;
    valid_gfni.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_GFNI}, 2);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_gfni));
    BUSTER_TEST(arguments, target_vector_register_size(valid_gfni) == 16);
    Target invalid_gfni_without_sse2 = valid_gfni;
    invalid_gfni_without_sse2.cpu_features =
        target_cpu_features_remove(invalid_gfni_without_sse2.cpu_features, TARGET_CPU_FEATURE_X86_SSE2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_gfni_without_sse2));
    Target valid_vaes = valid_avx512;
    valid_vaes.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_VAES}, 5);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_vaes));
    BUSTER_TEST(arguments, target_vector_register_size(valid_vaes) == 32);
    Target invalid_vaes_without_aes = valid_vaes;
    invalid_vaes_without_aes.cpu_features = target_cpu_features_remove(invalid_vaes_without_aes.cpu_features, TARGET_CPU_FEATURE_X86_AES);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vaes_without_aes));
    Target invalid_vaes_without_avx2 = valid_vaes;
    invalid_vaes_without_avx2.cpu_features =
        target_cpu_features_remove(invalid_vaes_without_avx2.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vaes_without_avx2));
    Target valid_vpclmul = valid_avx512;
    valid_vpclmul.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_VPCLMULQDQ}, 4);
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_vpclmul));
    Target invalid_vpclmul_without_pclmul = valid_vpclmul;
    invalid_vpclmul_without_pclmul.cpu_features =
        target_cpu_features_remove(invalid_vpclmul_without_pclmul.cpu_features, TARGET_CPU_FEATURE_X86_PCLMUL);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vpclmul_without_pclmul));
    Target invalid_vpclmul_without_avx = valid_vpclmul;
    invalid_vpclmul_without_avx.cpu_features =
        target_cpu_features_remove(invalid_vpclmul_without_avx.cpu_features, TARGET_CPU_FEATURE_X86_AVX);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vpclmul_without_avx));
    Target valid_avx10 = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_512}, 6),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx10));
    BUSTER_TEST(arguments, target_vector_register_size(valid_avx10) == 64);
    Target invalid_avx10_marker = valid_avx10;
    invalid_avx10_marker.cpu_features =
        target_cpu_features_remove(invalid_avx10_marker.cpu_features, TARGET_CPU_FEATURE_X86_AVX10_512);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx10_marker));
    Target valid_avx512_subfeatures = valid_avx512;
    valid_avx512_subfeatures.cpu_features = target_cpu_features_union(valid_avx512_subfeatures.cpu_features,
                                                                       target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                           TARGET_CPU_FEATURE_X86_AVX512BW, TARGET_CPU_FEATURE_X86_AVX512CD,
                                                                           TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA,
                                                                           TARGET_CPU_FEATURE_X86_AVX512PF, TARGET_CPU_FEATURE_X86_AVX512ER,
                                                                           TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2,
                                                                           TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512BITALG,
                                                                           TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ, TARGET_CPU_FEATURE_X86_AVX5124VNNIW,
                                                                           TARGET_CPU_FEATURE_X86_AVX5124FMAPS, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT,
                                                                           TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512FP16}, 16));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx512_subfeatures));
    Target invalid_avx512_subfeature = valid_avx512_subfeatures;
    invalid_avx512_subfeature.cpu_features =
        target_cpu_features_remove(invalid_avx512_subfeature.cpu_features, TARGET_CPU_FEATURE_X86_AVX512F);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512_subfeature));
    Target invalid_avx512_bw_subfeature = valid_avx512_subfeatures;
    invalid_avx512_bw_subfeature.cpu_features =
        target_cpu_features_remove(invalid_avx512_bw_subfeature.cpu_features, TARGET_CPU_FEATURE_X86_AVX512BW);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512_bw_subfeature));
    Target valid_amx = valid_avx512;
    valid_amx.cpu_features = target_cpu_features_union(valid_amx.cpu_features,
                                                       target_cpu_features_from_array((TargetCpuFeature const[]){
                                                           TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF,
                                                           TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8,
                                                           TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_FP16,
                                                           TARGET_CPU_FEATURE_X86_AMX_COMPLEX, TARGET_CPU_FEATURE_X86_AMX_FP8,
                                                           TARGET_CPU_FEATURE_X86_AMX_AVX512, TARGET_CPU_FEATURE_X86_AMX_MOVRS}, 10));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_amx));
    Target invalid_amx = valid_amx;
    invalid_amx.cpu_features = target_cpu_features_remove(invalid_amx.cpu_features, TARGET_CPU_FEATURE_X86_AMX_TILE);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_amx));
    Target invalid_apx_nci = valid_avx512;
    invalid_apx_nci.cpu_features = target_cpu_features_add(invalid_apx_nci.cpu_features, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_apx_nci));
    Target invalid_apx_without_nci = valid_avx512;
    invalid_apx_without_nci.cpu_features = target_cpu_features_add(invalid_apx_without_nci.cpu_features, TARGET_CPU_FEATURE_X86_APX);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_apx_without_nci));
    Target invalid_amx_avx512 = valid_avx512;
    invalid_amx_avx512.cpu_features = target_cpu_features_union(invalid_amx_avx512.cpu_features,
                                                                 target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                     TARGET_CPU_FEATURE_X86_AMX_TILE,
                                                                     TARGET_CPU_FEATURE_X86_AMX_AVX512}, 2));
    invalid_amx_avx512.cpu_features =
        target_cpu_features_remove(invalid_amx_avx512.cpu_features, TARGET_CPU_FEATURE_X86_AVX512F);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_amx_avx512));
    Target valid_amx_movrs_without_apx = valid_avx512;
    valid_amx_movrs_without_apx.cpu_features = target_cpu_features_union(valid_amx_movrs_without_apx.cpu_features,
                                                                         target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                             TARGET_CPU_FEATURE_X86_AMX_TILE,
                                                                             TARGET_CPU_FEATURE_X86_AMX_MOVRS}, 2));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_amx_movrs_without_apx));
    Target valid_avx_independent_features = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16, TARGET_CPU_FEATURE_X86_AVX_IFMA, TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT}, 8),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx_independent_features));
    Target invalid_avx_vnni_without_avx2 = valid_avx_independent_features;
    invalid_avx_vnni_without_avx2.cpu_features =
        target_cpu_features_remove(invalid_avx_vnni_without_avx2.cpu_features, TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx_vnni_without_avx2));
    Target valid_avx10_v1_aux = valid_avx10;
    valid_avx10_v1_aux.cpu_features = target_cpu_features_union(valid_avx10_v1_aux.cpu_features,
                                                                 target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                     TARGET_CPU_FEATURE_X86_AVX10_2,
                                                                     TARGET_CPU_FEATURE_X86_AVX10_V1_AUX}, 2));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx10_v1_aux));
    Target invalid_avx10_without_v1_aux = valid_avx10;
    invalid_avx10_without_v1_aux.cpu_features =
        target_cpu_features_add(invalid_avx10_without_v1_aux.cpu_features, TARGET_CPU_FEATURE_X86_AVX10_2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx10_without_v1_aux));
    Target invalid_v1_aux_without_avx10 = valid_avx10;
    invalid_v1_aux_without_avx10.cpu_features =
        target_cpu_features_add(invalid_v1_aux_without_avx10.cpu_features, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_v1_aux_without_avx10));
    Target valid_metadata_features = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_IBT, TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI, TARGET_CPU_FEATURE_X86_MOVRS, TARGET_CPU_FEATURE_X86_SHSTK}, 6),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_metadata_features));
    Target valid_ace_feature = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2,
                                                                                  TARGET_CPU_FEATURE_X86_ACE_1}, 2),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_ace_feature));
    Target invalid_xsaves_without_xsave = valid_metadata_features;
    invalid_xsaves_without_xsave.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_XSAVES}, 2);
    invalid_xsaves_without_xsave.cpu_features_explicit = true;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_xsaves_without_xsave));

    struct
    {
        String8 name;
        TargetCpuFeature feature;
    } feature_names[] = {
        {S8("3dnow"), TARGET_CPU_FEATURE_X86_3DNOW},
        {S8("3dnowa"), TARGET_CPU_FEATURE_X86_3DNOWA},
        {S8("ace-1"), TARGET_CPU_FEATURE_X86_ACE_1},
        {S8("aes"), TARGET_CPU_FEATURE_X86_AES},
        {S8("amx-avx512"), TARGET_CPU_FEATURE_X86_AMX_AVX512},
        {S8("amx-bf16"), TARGET_CPU_FEATURE_X86_AMX_BF16},
        {S8("amx-complex"), TARGET_CPU_FEATURE_X86_AMX_COMPLEX},
        {S8("amx-fp16"), TARGET_CPU_FEATURE_X86_AMX_FP16},
        {S8("amx-fp8"), TARGET_CPU_FEATURE_X86_AMX_FP8},
        {S8("amx-int8"), TARGET_CPU_FEATURE_X86_AMX_INT8},
        {S8("amx-movrs"), TARGET_CPU_FEATURE_X86_AMX_MOVRS},
        {S8("amx-tile"), TARGET_CPU_FEATURE_X86_AMX_TILE},
        {S8("apx"), TARGET_CPU_FEATURE_X86_APX},
        {S8("apx-nci-ndd-nf"), TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF},
        {S8("avx-ifma"), TARGET_CPU_FEATURE_X86_AVX_IFMA},
        {S8("avx-ne-convert"), TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT},
        {S8("avx-vnni"), TARGET_CPU_FEATURE_X86_AVX_VNNI},
        {S8("avx-vnni-int16"), TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16},
        {S8("avx-vnni-int8"), TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8},
        {S8("avx10-512"), TARGET_CPU_FEATURE_X86_AVX10_512},
        {S8("avx10-v1-aux"), TARGET_CPU_FEATURE_X86_AVX10_V1_AUX},
        {S8("avx10.1"), TARGET_CPU_FEATURE_X86_AVX10_1},
        {S8("avx10.2"), TARGET_CPU_FEATURE_X86_AVX10_2},
        {S8("avx5124fmaps"), TARGET_CPU_FEATURE_X86_AVX5124FMAPS},
        {S8("avx5124vnniw"), TARGET_CPU_FEATURE_X86_AVX5124VNNIW},
        {S8("avx512bf16"), TARGET_CPU_FEATURE_X86_AVX512BF16},
        {S8("avx512bitalg"), TARGET_CPU_FEATURE_X86_AVX512BITALG},
        {S8("avx512cd"), TARGET_CPU_FEATURE_X86_AVX512CD},
        {S8("avx512dq"), TARGET_CPU_FEATURE_X86_AVX512DQ},
        {S8("avx512er"), TARGET_CPU_FEATURE_X86_AVX512ER},
        {S8("avx512fp16"), TARGET_CPU_FEATURE_X86_AVX512FP16},
        {S8("avx512ifma"), TARGET_CPU_FEATURE_X86_AVX512IFMA},
        {S8("avx512pf"), TARGET_CPU_FEATURE_X86_AVX512PF},
        {S8("avx512vbmi"), TARGET_CPU_FEATURE_X86_AVX512VBMI},
        {S8("avx512vbmi2"), TARGET_CPU_FEATURE_X86_AVX512VBMI2},
        {S8("avx512vnni"), TARGET_CPU_FEATURE_X86_AVX512VNNI},
        {S8("avx512vp2intersect"), TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT},
        {S8("avx512vpopcntdq"), TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ},
        {S8("adx"), TARGET_CPU_FEATURE_X86_ADX},
        {S8("bmi2"), TARGET_CPU_FEATURE_X86_BMI2},
        {S8("clflushopt"), TARGET_CPU_FEATURE_X86_CLFLUSHOPT},
        {S8("clwb"), TARGET_CPU_FEATURE_X86_CLWB},
        {S8("ibt"), TARGET_CPU_FEATURE_X86_IBT},
        {S8("cldemote"), TARGET_CPU_FEATURE_X86_CLDEMOTE},
        {S8("f16c"), TARGET_CPU_FEATURE_X86_F16C},
        {S8("fma"), TARGET_CPU_FEATURE_X86_FMA},
        {S8("fma4"), TARGET_CPU_FEATURE_X86_FMA4},
        {S8("fsgsbase"), TARGET_CPU_FEATURE_X86_FSGSBASE},
        {S8("enqcmd"), TARGET_CPU_FEATURE_X86_ENQCMD},
        {S8("fred"), TARGET_CPU_FEATURE_X86_FRED},
        {S8("gfni"), TARGET_CPU_FEATURE_X86_GFNI},
        {S8("hreset"), TARGET_CPU_FEATURE_X86_HRESET},
        {S8("invlpgb"), TARGET_CPU_FEATURE_X86_INVLPGB},
        {S8("invpcid"), TARGET_CPU_FEATURE_X86_INVPCID},
        {S8("keylocker"), TARGET_CPU_FEATURE_X86_KEYLOCKER},
        {S8("lkgs"), TARGET_CPU_FEATURE_X86_LKGS},
        {S8("lwp"), TARGET_CPU_FEATURE_X86_LWP},
        {S8("movbe"), TARGET_CPU_FEATURE_X86_MOVBE},
        {S8("movrs"), TARGET_CPU_FEATURE_X86_MOVRS},
        {S8("msr-imm"), TARGET_CPU_FEATURE_X86_MSR_IMM},
        {S8("msrlist"), TARGET_CPU_FEATURE_X86_MSRLIST},
        {S8("monitor"), TARGET_CPU_FEATURE_X86_MONITOR},
        {S8("movdir64b"), TARGET_CPU_FEATURE_X86_MOVDIR64B},
        {S8("pbndkb"), TARGET_CPU_FEATURE_X86_PBNDKB},
        {S8("pconfig"), TARGET_CPU_FEATURE_X86_PCONFIG},
        {S8("pku"), TARGET_CPU_FEATURE_X86_PKU},
        {S8("pclmul"), TARGET_CPU_FEATURE_X86_PCLMUL},
        {S8("prefetchi"), TARGET_CPU_FEATURE_X86_PREFETCHI},
        {S8("prefetchwt1"), TARGET_CPU_FEATURE_X86_PREFETCHWT1},
        {S8("ptwrite"), TARGET_CPU_FEATURE_X86_PTWRITE},
        {S8("rdrand"), TARGET_CPU_FEATURE_X86_RDRAND},
        {S8("rdseed"), TARGET_CPU_FEATURE_X86_RDSEED},
        {S8("rtm"), TARGET_CPU_FEATURE_X86_RTM},
        {S8("serialize"), TARGET_CPU_FEATURE_X86_SERIALIZE},
        {S8("smap"), TARGET_CPU_FEATURE_X86_SMAP},
        {S8("sgx"), TARGET_CPU_FEATURE_X86_SGX},
        {S8("shstk"), TARGET_CPU_FEATURE_X86_SHSTK},
        {S8("snp"), TARGET_CPU_FEATURE_X86_SNP},
        {S8("sse4a"), TARGET_CPU_FEATURE_X86_SSE4A},
        {S8("sse4.1"), TARGET_CPU_FEATURE_X86_SSE4_1},
        {S8("sse4.2"), TARGET_CPU_FEATURE_X86_SSE4_2},
        {S8("ssse3"), TARGET_CPU_FEATURE_X86_SSSE3},
        {S8("svm"), TARGET_CPU_FEATURE_X86_SVM},
        {S8("tdx"), TARGET_CPU_FEATURE_X86_TDX},
        {S8("tsxldtrk"), TARGET_CPU_FEATURE_X86_TSXLDTRK},
        {S8("uintr"), TARGET_CPU_FEATURE_X86_UINTR},
        {S8("vaes"), TARGET_CPU_FEATURE_X86_VAES},
        {S8("vpclmulqdq"), TARGET_CPU_FEATURE_X86_VPCLMULQDQ},
        {S8("vmx"), TARGET_CPU_FEATURE_X86_VMX},
        {S8("tbm"), TARGET_CPU_FEATURE_X86_TBM},
        {S8("wbnoinvd"), TARGET_CPU_FEATURE_X86_WBNOINVD},
        {S8("waitpkg"), TARGET_CPU_FEATURE_X86_WAITPKG},
        {S8("wrmsrns"), TARGET_CPU_FEATURE_X86_WRMSRNS},
        {S8("xop"), TARGET_CPU_FEATURE_X86_XOP},
        {S8("xsave"), TARGET_CPU_FEATURE_X86_XSAVE},
        {S8("xsaves"), TARGET_CPU_FEATURE_X86_XSAVES},
    };
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(feature_names); feature_index += 1)
    {
        BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, feature_names[feature_index].name) == feature_names[feature_index].feature);
        BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(feature_names[feature_index].feature), feature_names[feature_index].name);
    }
    BUSTER_TEST(arguments, target_cpu_feature_names_are_sorted());
    BUSTER_TEST(arguments, sizeof(TargetCpuFeatures) == TARGET_CPU_FEATURE_WORD_COUNT * sizeof(u64));
    BUSTER_TEST(arguments, TARGET_CPU_FEATURE_BIT_CAPACITY >= 256);
    TargetCpuFeatures second_word_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_SVM);
    BUSTER_TEST(arguments, TARGET_CPU_FEATURE_X86_SVM > 64);
    BUSTER_TEST(arguments, second_word_features.words[0] == 0 && second_word_features.words[1] == UINT64_C(1));
    BUSTER_TEST(arguments, target_cpu_features_contains(second_word_features, TARGET_CPU_FEATURE_X86_SVM));
    BUSTER_TEST(arguments, !target_cpu_features_contains(second_word_features, TARGET_CPU_FEATURE_X86_VMX));
    BUSTER_TEST(arguments, !target_cpu_features_contains(second_word_features, TARGET_CPU_FEATURE_COUNT));
    BUSTER_TEST(arguments, !target_cpu_features_contains(second_word_features, (TargetCpuFeature)UINT32_MAX));
    Target invalid_out_of_range = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_add(second_word_features, TARGET_CPU_FEATURE_X86_SSE2),
    };
    invalid_out_of_range.cpu_features.words[TARGET_CPU_FEATURE_WORD_COUNT - 1] = UINT64_C(0x8000000000000000);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_out_of_range));
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("avx2")) == TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("ace-1")) == TARGET_CPU_FEATURE_X86_ACE_1);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("ACE_1")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("avx10-vnni-int")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("sse3")) == TARGET_CPU_FEATURE_X86_SSE3);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("sse4a")) == TARGET_CPU_FEATURE_X86_SSE4A);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("popcnt")) == TARGET_CPU_FEATURE_X86_POPCNT);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("lzcnt")) == TARGET_CPU_FEATURE_X86_LZCNT);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("bmi1")) == TARGET_CPU_FEATURE_X86_BMI1);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("cx16")) == TARGET_CPU_FEATURE_X86_CX16);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("neon")) == TARGET_CPU_FEATURE_AARCH64_NEON);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("sme")) == TARGET_CPU_FEATURE_AARCH64_SME);
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_AARCH64_SME), S8("sme"));
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("avx2")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("ibt")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("shstk")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("cet")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_AVX512F), S8("avx512f"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_POPCNT), S8("popcnt"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_LZCNT), S8("lzcnt"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_BMI1), S8("bmi1"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_CX16), S8("cx16"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_SSE4A), S8("sse4a"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_ACE_1), S8("ace-1"));

    struct
    {
        String8 name;
        TargetCpuFeature feature;
    } apple_m1_feature_names[] = {
        {S8("aes"), TARGET_CPU_FEATURE_AARCH64_AES},
        {S8("altnzcv"), TARGET_CPU_FEATURE_AARCH64_ALTNZCV},
        {S8("ccdp"), TARGET_CPU_FEATURE_AARCH64_CCDP},
        {S8("ccpp"), TARGET_CPU_FEATURE_AARCH64_CCPP},
        {S8("complxnum"), TARGET_CPU_FEATURE_AARCH64_COMPLXNUM},
        {S8("crc"), TARGET_CPU_FEATURE_AARCH64_CRC},
        {S8("dotprod"), TARGET_CPU_FEATURE_AARCH64_DOTPROD},
        {S8("flagm"), TARGET_CPU_FEATURE_AARCH64_FLAGM},
        {S8("fp-armv8"), TARGET_CPU_FEATURE_AARCH64_FP_ARMV8},
        {S8("fp16fml"), TARGET_CPU_FEATURE_AARCH64_FP16FML},
        {S8("fptoint"), TARGET_CPU_FEATURE_AARCH64_FPTOINT},
        {S8("fullfp16"), TARGET_CPU_FEATURE_AARCH64_FULLFP16},
        {S8("jsconv"), TARGET_CPU_FEATURE_AARCH64_JSCONV},
        {S8("lor"), TARGET_CPU_FEATURE_AARCH64_LOR},
        {S8("lse"), TARGET_CPU_FEATURE_AARCH64_LSE},
        {S8("neon"), TARGET_CPU_FEATURE_AARCH64_NEON},
        {S8("pauth"), TARGET_CPU_FEATURE_AARCH64_PAUTH},
        {S8("perfmon"), TARGET_CPU_FEATURE_AARCH64_PERFMON},
        {S8("predres"), TARGET_CPU_FEATURE_AARCH64_PREDRES},
        {S8("ras"), TARGET_CPU_FEATURE_AARCH64_RAS},
        {S8("rcpc"), TARGET_CPU_FEATURE_AARCH64_RCPC},
        {S8("rcpc-immo"), TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO},
        {S8("rdm"), TARGET_CPU_FEATURE_AARCH64_RDM},
        {S8("sb"), TARGET_CPU_FEATURE_AARCH64_SB},
        {S8("sha2"), TARGET_CPU_FEATURE_AARCH64_SHA2},
        {S8("sha3"), TARGET_CPU_FEATURE_AARCH64_SHA3},
        {S8("specrestrict"), TARGET_CPU_FEATURE_AARCH64_SPECRESTRICT},
        {S8("ssbs"), TARGET_CPU_FEATURE_AARCH64_SSBS},
        {S8("tracev8.4"), TARGET_CPU_FEATURE_AARCH64_TRACEV8_4},
        {S8("v8.4a"), TARGET_CPU_FEATURE_AARCH64_V8_4A},
    };
    TargetCpuFeature apple_m1_features_array[BUSTER_ARRAY_LENGTH(apple_m1_feature_names)];
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(apple_m1_feature_names); feature_index += 1)
    {
        apple_m1_features_array[feature_index] = apple_m1_feature_names[feature_index].feature;
        BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, apple_m1_feature_names[feature_index].name) == apple_m1_feature_names[feature_index].feature);
        BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(apple_m1_feature_names[feature_index].feature), apple_m1_feature_names[feature_index].name);
    }
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("aes")) == TARGET_CPU_FEATURE_X86_AES);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("avx2")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_names_are_sorted());

    TargetCpuFeatures apple_m1_expected = target_cpu_features_from_array(apple_m1_features_array, BUSTER_ARRAY_LENGTH(apple_m1_features_array));
    TargetCpuFeatures apple_m1_default = target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_APPLE_M1);
    BUSTER_TEST(arguments, target_cpu_features_equal(apple_m1_default, apple_m1_expected));
    BUSTER_TEST(arguments, !target_cpu_features_contains(apple_m1_default, TARGET_CPU_FEATURE_AARCH64_SME));
    Target apple_m1_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_APPLE_M1,
        .os = OPERATING_SYSTEM_MACOS,
    };
    BUSTER_TEST(arguments, target_cpu_features_equal(target_cpu_features_effective(apple_m1_target), apple_m1_expected));
    BUSTER_TEST(arguments, target_cpu_features_are_valid(apple_m1_target));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, apple_m1_target),
                       S8("aes,altnzcv,ccdp,ccpp,complxnum,crc,dotprod,flagm,fp-armv8,fp16fml,fptoint,fullfp16,jsconv,lor,lse,neon,pauth,perfmon,predres,ras,rcpc,rcpc-immo,rdm,sb,sha2,sha3,specrestrict,ssbs,tracev8.4,v8.4a"));
    TargetCpuFeatures generic_aarch64_expected = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
        TARGET_CPU_FEATURE_AARCH64_NEON,
    }, 2);
    BUSTER_TEST(arguments, target_cpu_features_equal(target_cpu_features_default(CPU_ARCH_AARCH64, CPU_MODEL_A64_GENERIC),
                                                     generic_aarch64_expected));
    Target explicit_sme_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_GENERIC,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_add(generic_aarch64_expected, TARGET_CPU_FEATURE_AARCH64_SME),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(explicit_sme_target));
    Target explicit_apple_m1_target = apple_m1_target;
    explicit_apple_m1_target.cpu_features_explicit = true;
    explicit_apple_m1_target.cpu_features = apple_m1_expected;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(explicit_apple_m1_target));
    struct
    {
        TargetCpuFeature dependent;
        TargetCpuFeature requirement;
    } aarch64_implications[] = {
        {TARGET_CPU_FEATURE_AARCH64_ALTNZCV, TARGET_CPU_FEATURE_AARCH64_FLAGM},
        {TARGET_CPU_FEATURE_AARCH64_CCDP, TARGET_CPU_FEATURE_AARCH64_CCPP},
        {TARGET_CPU_FEATURE_AARCH64_AES, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_COMPLXNUM, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_DOTPROD, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_FP16FML, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_FP16FML, TARGET_CPU_FEATURE_AARCH64_FULLFP16},
        {TARGET_CPU_FEATURE_AARCH64_FPTOINT, TARGET_CPU_FEATURE_AARCH64_FP_ARMV8},
        {TARGET_CPU_FEATURE_AARCH64_FULLFP16, TARGET_CPU_FEATURE_AARCH64_FP_ARMV8},
        {TARGET_CPU_FEATURE_AARCH64_JSCONV, TARGET_CPU_FEATURE_AARCH64_FP_ARMV8},
        {TARGET_CPU_FEATURE_AARCH64_NEON, TARGET_CPU_FEATURE_AARCH64_FP_ARMV8},
        {TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO, TARGET_CPU_FEATURE_AARCH64_RCPC},
        {TARGET_CPU_FEATURE_AARCH64_RDM, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_SHA2, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_SHA3, TARGET_CPU_FEATURE_AARCH64_NEON},
        {TARGET_CPU_FEATURE_AARCH64_SHA3, TARGET_CPU_FEATURE_AARCH64_SHA2},
    };
    for (u32 implication_index = 0; implication_index < BUSTER_ARRAY_LENGTH(aarch64_implications); implication_index += 1)
    {
        Target invalid_implication = explicit_apple_m1_target;
        invalid_implication.cpu_features = target_cpu_features_remove(invalid_implication.cpu_features,
                                                                       aarch64_implications[implication_index].requirement);
        BUSTER_TEST(arguments, target_cpu_features_contains(invalid_implication.cpu_features,
                                                            aarch64_implications[implication_index].dependent));
        BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_implication));
    }
    Target invalid_aarch64_x86_feature = apple_m1_target;
    invalid_aarch64_x86_feature.cpu_features_explicit = true;
    invalid_aarch64_x86_feature.cpu_features = target_cpu_features_add(apple_m1_expected, TARGET_CPU_FEATURE_X86_AES);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_aarch64_x86_feature));
    Target invalid_x86_aarch64_feature = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_AARCH64_AES}, 2),
    };
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_x86_aarch64_feature));
#if BUSTER_CPU_ARCH_X86_64
    X86_64CpuFeatureInput full_cpuid = {
        .maximum_basic_leaf = 0x29,
        .maximum_extended_leaf = 0x8000001f,
        .basic = {.ecx = (UINT32_C(0x1)) | (UINT32_C(0x2)) | (UINT32_C(0x8)) | (UINT32_C(0x20)) | (UINT32_C(0x200)) | (UINT32_C(0x1000)) |
                           (UINT32_C(0x80000)) | (UINT32_C(0x100000)) | (UINT32_C(0x2000)) | (UINT32_C(0x400000)) |
                           (UINT32_C(0x800000)) | (UINT32_C(0x2000000)) | (UINT32_C(0x20000000)) | (UINT32_C(0x40000000)) |
                           (UINT32_C(0x4000000)) | (UINT32_C(0x8000000)) | (UINT32_C(0x10000000))},
        .extended_basic = {.ecx = UINT32_C(0x4) | UINT32_C(0x20) | UINT32_C(0x40) | UINT32_C(0x800) | UINT32_C(0x8000) | UINT32_C(0x10000) | UINT32_C(0x200000),
                           .edx = UINT32_C(0x40000000) | UINT32_C(0x80000000)},
        .leaf_7_0 =
            {
                .eax = 1,
                .ebx = (UINT32_C(0x1)) | (UINT32_C(0x100)) | (UINT32_C(0x800)) | (UINT32_C(0x40000)) | (UINT32_C(0x80000)) |
                       (UINT32_C(0x800000)) | (UINT32_C(0x1000000)) | (UINT32_C(0x4)) | (UINT32_C(0x8)) | (UINT32_C(0x20)) | (UINT32_C(0x400)) | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x200000)) |
                       (UINT32_C(0x100000)) |
                       (UINT32_C(0x4000000)) | (UINT32_C(0x8000000)) | (UINT32_C(0x10000000)) | (UINT32_C(0x40000000)) | (UINT32_C(0x80000000)),
                .ecx = (UINT32_C(0x1)) | (UINT32_C(0x8)) | (UINT32_C(0x20)) | (UINT32_C(0x2)) | (UINT32_C(0x40)) | (UINT32_C(0x80)) | (UINT32_C(0x100)) | (UINT32_C(0x200)) | (UINT32_C(0x400)) |
                       (UINT32_C(0x800000)) | (UINT32_C(0x20000000)) |
                       (UINT32_C(0x800)) | (UINT32_C(0x1000)) | (UINT32_C(0x4000)) | (UINT32_C(0x2000000)) | (UINT32_C(0x10000000)),
                .edx = (UINT32_C(0x20)) | (UINT32_C(0x4000)) | (UINT32_C(0x10000)) | (UINT32_C(0x4)) | (UINT32_C(0x8)) | (UINT32_C(0x100)) | (UINT32_C(0x100000)) | (UINT32_C(0x400000)) | (UINT32_C(0x800000)) |
                       (UINT32_C(0x1000000)) | (UINT32_C(0x2000000)) | (UINT32_C(0x40000)),
            },
        .leaf_7_1 =
            {
                .eax = (UINT32_C(0x10)) | (UINT32_C(0x20)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000)) | (UINT32_C(0x80000)) |
                       (UINT32_C(0x400000)) | (UINT32_C(0x8000000)) | (UINT32_C(0x200000)) | (UINT32_C(0x800000)) | (UINT32_C(0x80000000)),
                .edx = (UINT32_C(0x10)) | (UINT32_C(0x20)) | (UINT32_C(0x100)) | (UINT32_C(0x400)) | (UINT32_C(0x4000)) | (UINT32_C(0x80000)) |
                       (UINT32_C(0x200000)),
            },
        .leaf_14_0 = {.ebx = UINT32_C(0x10)},
        .leaf_d_1 = {.eax = UINT32_C(0x8)},
        .leaf_1e_0 = {.eax = 1},
        .leaf_1e_1 = {.eax = (UINT32_C(0x1)) | (UINT32_C(0x2)) | (UINT32_C(0x4)) | (UINT32_C(0x8)) | (UINT32_C(0x10)) |
                              (UINT32_C(0x40)) | (UINT32_C(0x80)) | (UINT32_C(0x100))},
        .leaf_24_0 = {.eax = 1, .ebx = 2 | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000))},
        .leaf_24_1 = {.ecx = UINT32_C(0x4)},
        .leaf_29_0 = {.ebx = UINT32_C(0x1)},
        .extended_8 = {.ebx = UINT32_C(0x8) | UINT32_C(0x200)},
        .extended_1f = {.eax = UINT32_C(0x10)},
        .xcr0 = UINT64_C(0xe7) | (UINT64_C(0x20000)) | (UINT64_C(0x40000)) | (UINT64_C(0x80000)),
    };
    TargetCpuFeatures full_features = x86_64_cpu_features_from_cpuid(full_cpuid);
    TargetCpuFeatures full_expected = target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_SSE3, TARGET_CPU_FEATURE_X86_SSSE3, TARGET_CPU_FEATURE_X86_SSE4_1, TARGET_CPU_FEATURE_X86_SSE4_2, TARGET_CPU_FEATURE_X86_SSE4A, TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT, TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_BMI2, TARGET_CPU_FEATURE_X86_ADX, TARGET_CPU_FEATURE_X86_MOVBE, TARGET_CPU_FEATURE_X86_RDRAND, TARGET_CPU_FEATURE_X86_RDSEED, TARGET_CPU_FEATURE_X86_WAITPKG, TARGET_CPU_FEATURE_X86_PKU, TARGET_CPU_FEATURE_X86_PTWRITE, TARGET_CPU_FEATURE_X86_SERIALIZE, TARGET_CPU_FEATURE_X86_CLFLUSHOPT, TARGET_CPU_FEATURE_X86_CLWB, TARGET_CPU_FEATURE_X86_FSGSBASE, TARGET_CPU_FEATURE_X86_RTM, TARGET_CPU_FEATURE_X86_TSXLDTRK, TARGET_CPU_FEATURE_X86_UINTR, TARGET_CPU_FEATURE_X86_PREFETCHWT1, TARGET_CPU_FEATURE_X86_CX16, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_F16C, TARGET_CPU_FEATURE_X86_FMA, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW, TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA, TARGET_CPU_FEATURE_X86_AVX512PF, TARGET_CPU_FEATURE_X86_AVX512ER, TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512BITALG, TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ, TARGET_CPU_FEATURE_X86_AVX5124VNNIW, TARGET_CPU_FEATURE_X86_AVX5124FMAPS, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT, TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512FP16, TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_512, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, TARGET_CPU_FEATURE_X86_IBT, TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI, TARGET_CPU_FEATURE_X86_SHSTK, TARGET_CPU_FEATURE_X86_MOVRS, TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF, TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8, TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_FP16, TARGET_CPU_FEATURE_X86_AMX_COMPLEX, TARGET_CPU_FEATURE_X86_AMX_FP8, TARGET_CPU_FEATURE_X86_AMX_AVX512, TARGET_CPU_FEATURE_X86_AMX_MOVRS, TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16, TARGET_CPU_FEATURE_X86_AVX_IFMA, TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT, TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA, TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_LWP, TARGET_CPU_FEATURE_X86_TBM, TARGET_CPU_FEATURE_X86_XOP, TARGET_CPU_FEATURE_X86_MONITOR, TARGET_CPU_FEATURE_X86_XSAVE, TARGET_CPU_FEATURE_X86_SGX, TARGET_CPU_FEATURE_X86_INVPCID, TARGET_CPU_FEATURE_X86_SMAP, TARGET_CPU_FEATURE_X86_KEYLOCKER, TARGET_CPU_FEATURE_X86_ENQCMD, TARGET_CPU_FEATURE_X86_MOVDIR64B, TARGET_CPU_FEATURE_X86_PCONFIG, TARGET_CPU_FEATURE_X86_FRED, TARGET_CPU_FEATURE_X86_LKGS, TARGET_CPU_FEATURE_X86_WRMSRNS, TARGET_CPU_FEATURE_X86_HRESET, TARGET_CPU_FEATURE_X86_MSRLIST, TARGET_CPU_FEATURE_X86_XSAVES, TARGET_CPU_FEATURE_X86_INVLPGB, TARGET_CPU_FEATURE_X86_WBNOINVD, TARGET_CPU_FEATURE_X86_SNP}, 101);
    full_expected = target_cpu_features_add(full_expected, TARGET_CPU_FEATURE_X86_VMX);
    full_expected = target_cpu_features_add(full_expected, TARGET_CPU_FEATURE_X86_SVM);
    BUSTER_TEST(arguments, target_cpu_features_equal(full_features, full_expected));
    BUSTER_TEST(arguments, target_cpu_features_contains(full_features, TARGET_CPU_FEATURE_X86_VMX));
    BUSTER_TEST(arguments, target_cpu_features_contains(full_features, TARGET_CPU_FEATURE_X86_SVM));
    BUSTER_TEST(arguments, target_cpu_features_contains(full_features, TARGET_CPU_FEATURE_X86_MOVDIR64B));
    TargetCpuFeatures new_cpuid_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_MONITOR, TARGET_CPU_FEATURE_X86_XSAVE, TARGET_CPU_FEATURE_X86_XSAVES,
        TARGET_CPU_FEATURE_X86_INVPCID, TARGET_CPU_FEATURE_X86_SMAP, TARGET_CPU_FEATURE_X86_SGX,
        TARGET_CPU_FEATURE_X86_ENQCMD, TARGET_CPU_FEATURE_X86_MOVDIR64B, TARGET_CPU_FEATURE_X86_HRESET, TARGET_CPU_FEATURE_X86_KEYLOCKER,
        TARGET_CPU_FEATURE_X86_PCONFIG, TARGET_CPU_FEATURE_X86_FRED, TARGET_CPU_FEATURE_X86_LKGS,
        TARGET_CPU_FEATURE_X86_MSRLIST, TARGET_CPU_FEATURE_X86_WRMSRNS, TARGET_CPU_FEATURE_X86_INVLPGB,
        TARGET_CPU_FEATURE_X86_WBNOINVD, TARGET_CPU_FEATURE_X86_SNP,
        TARGET_CPU_FEATURE_X86_SSSE3, TARGET_CPU_FEATURE_X86_SSE4_1, TARGET_CPU_FEATURE_X86_SSE4_2,
        TARGET_CPU_FEATURE_X86_F16C, TARGET_CPU_FEATURE_X86_FMA, TARGET_CPU_FEATURE_X86_BMI2,
        TARGET_CPU_FEATURE_X86_ADX, TARGET_CPU_FEATURE_X86_MOVBE, TARGET_CPU_FEATURE_X86_RDRAND,
        TARGET_CPU_FEATURE_X86_RDSEED, TARGET_CPU_FEATURE_X86_WAITPKG, TARGET_CPU_FEATURE_X86_PKU,
        TARGET_CPU_FEATURE_X86_PTWRITE, TARGET_CPU_FEATURE_X86_SERIALIZE, TARGET_CPU_FEATURE_X86_CLFLUSHOPT,
        TARGET_CPU_FEATURE_X86_CLWB, TARGET_CPU_FEATURE_X86_FSGSBASE, TARGET_CPU_FEATURE_X86_RTM,
        TARGET_CPU_FEATURE_X86_TSXLDTRK, TARGET_CPU_FEATURE_X86_UINTR, TARGET_CPU_FEATURE_X86_PREFETCHWT1}, 39);
    BUSTER_TEST(arguments, target_cpu_features_subset(new_cpuid_features, full_features));
    BUSTER_TEST(arguments, target_cpu_features_contains(full_features, TARGET_CPU_FEATURE_X86_IBT));
    BUSTER_TEST(arguments, target_cpu_features_contains(full_features, TARGET_CPU_FEATURE_X86_SHSTK));
    X86_64CpuFeatureInput no_new_cpuid = full_cpuid;
    no_new_cpuid.basic.ecx &= ~(UINT32_C(0x8) | UINT32_C(0x4000000));
    no_new_cpuid.basic.ecx &= ~(UINT32_C(0x200) | UINT32_C(0x1000) | UINT32_C(0x80000) | UINT32_C(0x100000) |
                                UINT32_C(0x400000) | UINT32_C(0x20000000) | UINT32_C(0x40000000));
    no_new_cpuid.leaf_d_1.eax &= ~UINT32_C(0x8);
    no_new_cpuid.leaf_7_0.ebx &= ~(UINT32_C(0x1) | UINT32_C(0x100) | UINT32_C(0x800) | UINT32_C(0x40000) | UINT32_C(0x80000) |
                                   UINT32_C(0x800000) | UINT32_C(0x1000000) | UINT32_C(0x4) | UINT32_C(0x400) | UINT32_C(0x100000));
    no_new_cpuid.leaf_7_0.ecx &= ~(UINT32_C(0x1) | UINT32_C(0x8) | UINT32_C(0x20) | UINT32_C(0x800000) | UINT32_C(0x10000000) | UINT32_C(0x20000000));
    no_new_cpuid.leaf_7_0.edx &= ~(UINT32_C(0x20) | UINT32_C(0x4000) | UINT32_C(0x10000) | UINT32_C(0x40000));
    no_new_cpuid.leaf_14_0.ebx &= ~UINT32_C(0x10);
    no_new_cpuid.leaf_7_1.eax &= ~(UINT32_C(0x20000) | UINT32_C(0x40000) | UINT32_C(0x80000) | UINT32_C(0x400000) | UINT32_C(0x8000000));
    no_new_cpuid.extended_8.ebx &= ~(UINT32_C(0x8) | UINT32_C(0x200));
    no_new_cpuid.extended_1f.eax &= ~UINT32_C(0x10);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(x86_64_cpu_features_from_cpuid(no_new_cpuid), new_cpuid_features));
    X86_64CpuFeatureInput xsaves_without_xsave = full_cpuid;
    xsaves_without_xsave.basic.ecx &= ~UINT32_C(0x4000000);
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(xsaves_without_xsave), TARGET_CPU_FEATURE_X86_XSAVES));
    X86_64CpuFeatureInput no_basic_leaf_1 = full_cpuid;
    no_basic_leaf_1.maximum_basic_leaf = 0;
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(x86_64_cpu_features_from_cpuid(no_basic_leaf_1),
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){
                                                                   TARGET_CPU_FEATURE_X86_VMX, TARGET_CPU_FEATURE_X86_MONITOR,
                                                                   TARGET_CPU_FEATURE_X86_XSAVE, TARGET_CPU_FEATURE_X86_XSAVES}, 4)));
    X86_64CpuFeatureInput no_basic_leaf_1_vector_features = {
        .basic = {.ecx = UINT32_C(0x8000000) | UINT32_C(0x10000000) | UINT32_C(0x1000) | UINT32_C(0x20000000)},
        .xcr0 = UINT64_C(0x6),
    };
    TargetCpuFeatures no_basic_leaf_1_vector_result = x86_64_cpu_features_from_cpuid(no_basic_leaf_1_vector_features);
    BUSTER_TEST(arguments, !target_cpu_features_contains(no_basic_leaf_1_vector_result, TARGET_CPU_FEATURE_X86_AVX));
    BUSTER_TEST(arguments, !target_cpu_features_contains(no_basic_leaf_1_vector_result, TARGET_CPU_FEATURE_X86_FMA));
    BUSTER_TEST(arguments, !target_cpu_features_contains(no_basic_leaf_1_vector_result, TARGET_CPU_FEATURE_X86_F16C));
    X86_64CpuFeatureInput no_vmx_hardware = full_cpuid;
    no_vmx_hardware.basic.ecx &= ~UINT32_C(0x20);
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_vmx_hardware), TARGET_CPU_FEATURE_X86_VMX));
    X86_64CpuFeatureInput no_svm_hardware = full_cpuid;
    no_svm_hardware.extended_basic.ecx &= ~UINT32_C(0x4);
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_svm_hardware), TARGET_CPU_FEATURE_X86_SVM));
    X86_64CpuFeatureInput no_sse4a_hardware = full_cpuid;
    no_sse4a_hardware.extended_basic.ecx &= ~UINT32_C(0x40);
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_sse4a_hardware), TARGET_CPU_FEATURE_X86_SSE4A));
    X86_64CpuFeatureInput no_movdir64b_hardware = full_cpuid;
    no_movdir64b_hardware.leaf_7_0.ecx &= ~UINT32_C(0x10000000);
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_movdir64b_hardware), TARGET_CPU_FEATURE_X86_MOVDIR64B));
    X86_64CpuFeatureInput no_ibt_hardware = full_cpuid;
    no_ibt_hardware.leaf_7_0.edx &= ~(UINT32_C(0x100000));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_ibt_hardware), TARGET_CPU_FEATURE_X86_IBT));
    X86_64CpuFeatureInput no_cldemote_hardware = full_cpuid;
    no_cldemote_hardware.leaf_7_0.ecx &= ~(UINT32_C(0x2000000));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_cldemote_hardware), TARGET_CPU_FEATURE_X86_CLDEMOTE));
    X86_64CpuFeatureInput no_prefetchi_hardware = full_cpuid;
    no_prefetchi_hardware.leaf_7_1.edx &= ~(UINT32_C(0x4000));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_prefetchi_hardware), TARGET_CPU_FEATURE_X86_PREFETCHI));
    X86_64CpuFeatureInput no_shstk_hardware = full_cpuid;
    no_shstk_hardware.leaf_7_0.ecx &= ~(UINT32_C(0x80));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_shstk_hardware), TARGET_CPU_FEATURE_X86_SHSTK));
    X86_64CpuFeatureInput no_avx_xcr0 = full_cpuid;
    no_avx_xcr0.xcr0 &= ~UINT64_C(0x6);
    TargetCpuFeatures no_avx_xcr0_features = x86_64_cpu_features_from_cpuid(no_avx_xcr0);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_avx_xcr0_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_XOP}, 3)));
    BUSTER_TEST(arguments, target_cpu_features_subset(
                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA, TARGET_CPU_FEATURE_X86_LWP, TARGET_CPU_FEATURE_X86_TBM}, 4),
                               no_avx_xcr0_features));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_avx_xcr0_features, TARGET_CPU_FEATURE_X86_SSE4A));
    X86_64CpuFeatureInput no_osxsave = full_cpuid;
    no_osxsave.basic.ecx &= ~UINT32_C(0x8000000);
    TargetCpuFeatures no_osxsave_features = x86_64_cpu_features_from_cpuid(no_osxsave);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_osxsave_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_XOP}, 3)));
    X86_64CpuFeatureInput no_extended_leaf = full_cpuid;
    no_extended_leaf.maximum_extended_leaf = UINT32_C(0x80000000);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(x86_64_cpu_features_from_cpuid(no_extended_leaf),
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA, TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_LWP, TARGET_CPU_FEATURE_X86_TBM, TARGET_CPU_FEATURE_X86_XOP, TARGET_CPU_FEATURE_X86_SSE4A}, 7)));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(no_extended_leaf), TARGET_CPU_FEATURE_X86_SVM));
    BUSTER_TEST(arguments, target_cpu_features_are_valid((Target){
                                        .cpu_arch = CPU_ARCH_X86_64,
                                        .cpu_model = CPU_MODEL_BASELINE,
                                        .cpu_features_explicit = true,
                                        .cpu_features = full_features,
                                    }));
    X86_64CpuFeatureInput no_avx2_hardware = full_cpuid;
    no_avx2_hardware.leaf_7_0.ebx &= ~(UINT32_C(0x20));
    TargetCpuFeatures no_avx2_hardware_features = x86_64_cpu_features_from_cpuid(no_avx2_hardware);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_avx2_hardware_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_512, TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16, TARGET_CPU_FEATURE_X86_AVX_IFMA, TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT, TARGET_CPU_FEATURE_X86_VAES}, 11)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_avx2_hardware_features, TARGET_CPU_FEATURE_X86_GFNI));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_avx2_hardware_features, TARGET_CPU_FEATURE_X86_VPCLMULQDQ));
    X86_64CpuFeatureInput no_aes_hardware = full_cpuid;
    no_aes_hardware.basic.ecx &= ~(UINT32_C(0x2000000));
    TargetCpuFeatures no_aes_hardware_features = x86_64_cpu_features_from_cpuid(no_aes_hardware);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_aes_hardware_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_VAES}, 2)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_aes_hardware_features, TARGET_CPU_FEATURE_X86_VPCLMULQDQ));
    X86_64CpuFeatureInput no_pclmul_hardware = full_cpuid;
    no_pclmul_hardware.basic.ecx &= ~(UINT32_C(0x2));
    TargetCpuFeatures no_pclmul_hardware_features = x86_64_cpu_features_from_cpuid(no_pclmul_hardware);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_pclmul_hardware_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_VPCLMULQDQ}, 2)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_pclmul_hardware_features, TARGET_CPU_FEATURE_X86_VAES));
    X86_64CpuFeatureInput no_avx512bw_hardware = full_cpuid;
    no_avx512bw_hardware.leaf_7_0.ebx &= ~(UINT32_C(0x40000000));
    TargetCpuFeatures no_avx512bw_hardware_features = x86_64_cpu_features_from_cpuid(no_avx512bw_hardware);
    BUSTER_TEST(arguments, target_cpu_features_contains(no_avx512bw_hardware_features, TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_avx512bw_hardware_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512BITALG, TARGET_CPU_FEATURE_X86_AVX512FP16}, 5)));
    X86_64CpuFeatureInput reserved_avx10_vl = full_cpuid;
    reserved_avx10_vl.leaf_24_0.ebx &= ~((UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000)));
    BUSTER_TEST(arguments, target_cpu_features_equal(x86_64_cpu_features_from_cpuid(reserved_avx10_vl), full_features));
    X86_64CpuFeatureInput reserved_amx_bits = full_cpuid;
    reserved_amx_bits.leaf_1e_1.eax |= (UINT32_C(0x20)) | (UINT32_C(0x40));
    BUSTER_TEST(arguments, target_cpu_features_equal(x86_64_cpu_features_from_cpuid(reserved_amx_bits), full_features));
    X86_64CpuFeatureInput amx_int8_legacy_only = full_cpuid;
    amx_int8_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x1));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(amx_int8_legacy_only), TARGET_CPU_FEATURE_X86_AMX_INT8));
    X86_64CpuFeatureInput amx_int8_mirror_only = full_cpuid;
    amx_int8_mirror_only.leaf_7_0.edx &= ~(UINT32_C(0x2000000));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(amx_int8_mirror_only), TARGET_CPU_FEATURE_X86_AMX_INT8));
    X86_64CpuFeatureInput amx_bf16_legacy_only = full_cpuid;
    amx_bf16_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x2));
    TargetCpuFeatures amx_bf16_legacy_only_features = x86_64_cpu_features_from_cpuid(amx_bf16_legacy_only);
    BUSTER_TEST(arguments, !target_cpu_features_contains(amx_bf16_legacy_only_features, TARGET_CPU_FEATURE_X86_AMX_BF16));
    BUSTER_TEST(arguments, target_cpu_features_contains(amx_bf16_legacy_only_features, TARGET_CPU_FEATURE_X86_AVX512BF16));
    X86_64CpuFeatureInput amx_bf16_mirror_only = full_cpuid;
    amx_bf16_mirror_only.leaf_7_0.edx &= ~(UINT32_C(0x400000));
    TargetCpuFeatures amx_bf16_mirror_only_features = x86_64_cpu_features_from_cpuid(amx_bf16_mirror_only);
    BUSTER_TEST(arguments, !target_cpu_features_contains(amx_bf16_mirror_only_features, TARGET_CPU_FEATURE_X86_AMX_BF16));
    BUSTER_TEST(arguments, target_cpu_features_contains(amx_bf16_mirror_only_features, TARGET_CPU_FEATURE_X86_AVX512BF16));
    X86_64CpuFeatureInput amx_bf16_without_avx512_bf16 = full_cpuid;
    amx_bf16_without_avx512_bf16.leaf_7_1.eax &= ~(UINT32_C(0x20));
    TargetCpuFeatures amx_bf16_without_avx512_bf16_features = x86_64_cpu_features_from_cpuid(amx_bf16_without_avx512_bf16);
    BUSTER_TEST(arguments, target_cpu_features_contains(amx_bf16_without_avx512_bf16_features, TARGET_CPU_FEATURE_X86_AMX_BF16));
    BUSTER_TEST(arguments, !target_cpu_features_contains(amx_bf16_without_avx512_bf16_features, TARGET_CPU_FEATURE_X86_AVX512BF16));
    X86_64CpuFeatureInput avx512_bf16_without_amx_bf16 = full_cpuid;
    avx512_bf16_without_amx_bf16.leaf_7_0.edx &= ~(UINT32_C(0x400000));
    avx512_bf16_without_amx_bf16.leaf_1e_1.eax &= ~(UINT32_C(0x2));
    TargetCpuFeatures avx512_bf16_without_amx_bf16_features = x86_64_cpu_features_from_cpuid(avx512_bf16_without_amx_bf16);
    BUSTER_TEST(arguments, target_cpu_features_contains(avx512_bf16_without_amx_bf16_features, TARGET_CPU_FEATURE_X86_AVX512BF16));
    BUSTER_TEST(arguments, !target_cpu_features_contains(avx512_bf16_without_amx_bf16_features, TARGET_CPU_FEATURE_X86_AMX_BF16));
    X86_64CpuFeatureInput amx_complex_legacy_only = full_cpuid;
    amx_complex_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x4));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(amx_complex_legacy_only), TARGET_CPU_FEATURE_X86_AMX_COMPLEX));
    X86_64CpuFeatureInput amx_complex_mirror_only = full_cpuid;
    amx_complex_mirror_only.leaf_7_1.edx &= ~(UINT32_C(0x100));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(amx_complex_mirror_only), TARGET_CPU_FEATURE_X86_AMX_COMPLEX));
    X86_64CpuFeatureInput amx_fp16_legacy_only = full_cpuid;
    amx_fp16_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x8));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(amx_fp16_legacy_only), TARGET_CPU_FEATURE_X86_AMX_FP16));
    X86_64CpuFeatureInput amx_fp16_mirror_only = full_cpuid;
    amx_fp16_mirror_only.leaf_7_1.eax &= ~(UINT32_C(0x200000));
    BUSTER_TEST(arguments, !target_cpu_features_contains(x86_64_cpu_features_from_cpuid(amx_fp16_mirror_only), TARGET_CPU_FEATURE_X86_AMX_FP16));
    X86_64CpuFeatureInput legacy_amx_without_mirror = full_cpuid;
    legacy_amx_without_mirror.maximum_basic_leaf = 7;
    TargetCpuFeatures legacy_amx_features = x86_64_cpu_features_from_cpuid(legacy_amx_without_mirror);
    BUSTER_TEST(arguments, target_cpu_features_subset(
                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AMX_INT8, TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_FP16, TARGET_CPU_FEATURE_X86_AMX_COMPLEX}, 4),
                               legacy_amx_features));
    X86_64CpuFeatureInput avx10_version1 = full_cpuid;
    avx10_version1.leaf_24_0.ebx = 1 | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000));
    avx10_version1.leaf_24_1.ecx = 0;
    TargetCpuFeatures avx10_version1_features = x86_64_cpu_features_from_cpuid(avx10_version1);
    BUSTER_TEST(arguments, target_cpu_features_subset(
                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_512}, 2),
                               avx10_version1_features));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(avx10_version1_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX}, 2)));
    X86_64CpuFeatureInput avx10_version2_without_v1_aux = full_cpuid;
    avx10_version2_without_v1_aux.leaf_24_1.ecx = 0;
    TargetCpuFeatures avx10_version2_features = x86_64_cpu_features_from_cpuid(avx10_version2_without_v1_aux);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(avx10_version2_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX}, 2)));
    BUSTER_TEST(arguments, target_cpu_features_subset(
                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX}, 2),
                               full_features));
    X86_64CpuFeatureInput avx10_v1_aux = full_cpuid;
    avx10_v1_aux.leaf_24_0.ebx = 1 | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000));
    avx10_v1_aux.leaf_24_1.ecx = UINT32_C(0x4);
    TargetCpuFeatures avx10_v1_aux_features = x86_64_cpu_features_from_cpuid(avx10_v1_aux);
    BUSTER_TEST(arguments, target_cpu_features_subset(
                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_512}, 2),
                               avx10_v1_aux_features));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(avx10_v1_aux_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX}, 2)));
    X86_64CpuFeatureInput no_avx10 = full_cpuid;
    no_avx10.leaf_7_1.edx &= ~(UINT32_C(0x80000));
    TargetCpuFeatures no_avx10_features = x86_64_cpu_features_from_cpuid(no_avx10);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_avx10_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_512}, 3)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_avx10_features, TARGET_CPU_FEATURE_X86_MOVRS));
    X86_64CpuFeatureInput movrs_without_avx10 = no_avx10;
    movrs_without_avx10.xcr0 = 0;
    TargetCpuFeatures movrs_without_avx10_features = x86_64_cpu_features_from_cpuid(movrs_without_avx10);
    BUSTER_TEST(arguments, target_cpu_features_contains(movrs_without_avx10_features, TARGET_CPU_FEATURE_X86_MOVRS));
    BUSTER_TEST(arguments, !target_cpu_features_contains(movrs_without_avx10_features, TARGET_CPU_FEATURE_X86_AVX));
    X86_64CpuFeatureInput no_vector_state = full_cpuid;
    no_vector_state.xcr0 = 0;
    TargetCpuFeatures no_vector_state_features = x86_64_cpu_features_from_cpuid(no_vector_state);
    BUSTER_TEST(arguments, target_cpu_features_subset(
                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_SSE3, TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT, TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_CX16, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_GFNI}, 8),
                               no_vector_state_features));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_vector_state_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_AMX_TILE}, 6)));
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_vector_state_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ}, 2)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_vector_state_features, TARGET_CPU_FEATURE_X86_MOVRS));
    X86_64CpuFeatureInput no_amx_state = full_cpuid;
    no_amx_state.xcr0 &= ~((UINT64_C(0x20000)) | (UINT64_C(0x40000)));
    TargetCpuFeatures no_amx_state_features = x86_64_cpu_features_from_cpuid(no_amx_state);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_amx_state_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8, TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_FP16, TARGET_CPU_FEATURE_X86_AMX_COMPLEX, TARGET_CPU_FEATURE_X86_AMX_FP8, TARGET_CPU_FEATURE_X86_AMX_AVX512, TARGET_CPU_FEATURE_X86_AMX_MOVRS}, 8)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_amx_state_features, TARGET_CPU_FEATURE_X86_AVX512F));
    X86_64CpuFeatureInput amx_state_without_avx512_state = full_cpuid;
    amx_state_without_avx512_state.xcr0 = UINT64_C(0x6) | (UINT64_C(0x20000)) | (UINT64_C(0x40000));
    TargetCpuFeatures amx_state_without_avx512_state_features = x86_64_cpu_features_from_cpuid(amx_state_without_avx512_state);
    BUSTER_TEST(arguments, target_cpu_features_contains(amx_state_without_avx512_state_features, TARGET_CPU_FEATURE_X86_AMX_TILE));
    BUSTER_TEST(arguments, target_cpu_features_contains(amx_state_without_avx512_state_features, TARGET_CPU_FEATURE_X86_AMX_BF16));
    BUSTER_TEST(arguments, target_cpu_features_contains(amx_state_without_avx512_state_features, TARGET_CPU_FEATURE_X86_AMX_MOVRS));
    BUSTER_TEST(arguments, !target_cpu_features_contains(amx_state_without_avx512_state_features, TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, !target_cpu_features_contains(amx_state_without_avx512_state_features, TARGET_CPU_FEATURE_X86_AMX_AVX512));
    X86_64CpuFeatureInput amx_avx512_with_vector_state = full_cpuid;
    amx_avx512_with_vector_state.leaf_7_0.ebx &= ~(UINT32_C(0x10000));
    amx_avx512_with_vector_state.leaf_7_1.edx &= ~(UINT32_C(0x80000));
    amx_avx512_with_vector_state.leaf_24_0.ebx = 0;
    TargetCpuFeatures amx_avx512_with_vector_state_features = x86_64_cpu_features_from_cpuid(amx_avx512_with_vector_state);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(amx_avx512_with_vector_state_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AMX_AVX512}, 3)));
    X86_64CpuFeatureInput avx512_state_without_amx_state = full_cpuid;
    avx512_state_without_amx_state.xcr0 &= ~((UINT64_C(0x20000)) | (UINT64_C(0x40000)));
    TargetCpuFeatures avx512_state_without_amx_state_features = x86_64_cpu_features_from_cpuid(avx512_state_without_amx_state);
    BUSTER_TEST(arguments, target_cpu_features_contains(avx512_state_without_amx_state_features, TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, !target_cpu_features_contains(avx512_state_without_amx_state_features, TARGET_CPU_FEATURE_X86_AMX_TILE));
    X86_64CpuFeatureInput no_apx_state = full_cpuid;
    no_apx_state.xcr0 &= ~(UINT64_C(0x80000));
    TargetCpuFeatures no_apx_state_features = x86_64_cpu_features_from_cpuid(no_apx_state);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_apx_state_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF}, 2)));
    BUSTER_TEST(arguments, target_cpu_features_contains(no_apx_state_features, TARGET_CPU_FEATURE_X86_AMX_MOVRS));
    X86_64CpuFeatureInput no_apx_nci_hardware = full_cpuid;
    no_apx_nci_hardware.leaf_29_0.ebx &= ~(UINT32_C(0x1));
    TargetCpuFeatures no_apx_nci_hardware_features = x86_64_cpu_features_from_cpuid(no_apx_nci_hardware);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_apx_nci_hardware_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF}, 2)));
    X86_64CpuFeatureInput no_apx_f_hardware = full_cpuid;
    no_apx_f_hardware.leaf_7_1.edx &= ~(UINT32_C(0x200000));
    TargetCpuFeatures no_apx_f_hardware_features = x86_64_cpu_features_from_cpuid(no_apx_f_hardware);
    BUSTER_TEST(arguments, !target_test_cpu_features_intersect(no_apx_f_hardware_features,
                                                               target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF}, 2)));
    Target detected_x86 = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = cpu_detect_features_x86_64(),
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(detected_x86));
#endif
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, valid_avx512), S8("avx,avx2,avx512f,sse2"));
    Target sorted_features = valid_avx10_v1_aux;
    sorted_features.cpu_features = target_cpu_features_union(sorted_features.cpu_features,
                                                              target_cpu_features_from_array((TargetCpuFeature const[]){TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_IBT, TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI, TARGET_CPU_FEATURE_X86_SHSTK, TARGET_CPU_FEATURE_X86_SVM, TARGET_CPU_FEATURE_X86_VMX}, 14));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, sorted_features),
                       S8("aes,amx-bf16,amx-tile,apx,avx,avx10-512,avx10-v1-aux,avx10.1,avx10.2,avx2,avx512f,cldemote,gfni,ibt,pclmul,prefetchi,shstk,sse2,svm,vaes,vmx,vpclmulqdq"));
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, valid_amd_extended),
                       S8("3dnow,3dnowa,avx,fma4,lwp,lzcnt,sse2,tbm,xop"));
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
    TargetDataLayout macos_x86_layout = target_data_layout((Target){
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_MACOS,
    });
    BUSTER_TEST(arguments, macos_x86_layout.long_double_type.size == 16 && macos_x86_layout.long_double_type.bit_width == 80);
    TargetDataLayout macos_arm_layout = target_data_layout((Target){
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_MACOS,
    });
    BUSTER_TEST(arguments, macos_arm_layout.long_double_type.size == 8 && macos_arm_layout.long_double_type.bit_width == 64);
    TargetDataLayout linux_arm_layout = target_data_layout((Target){
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    });
    BUSTER_TEST(arguments, !linux_arm_layout.plain_char_is_signed);
    BUSTER_TEST(arguments, target_data_layout_is_valid(linux_arm_layout));

    // The MACHINE_INFO brand string: trimmed, buffer-bounded, and non-empty
    // on the hosts whose CI runners report one (x86-64 cpuid, Apple sysctl).
    {
        char8 cpu_name_buffer[128];
        String8 cpu_name = cpu_brand_string_os(cpu_name_buffer, sizeof(cpu_name_buffer));
        BUSTER_TEST(arguments, cpu_name.length <= sizeof(cpu_name_buffer));
        bool trimmed = !cpu_name.length || (cpu_name.pointer[0] != ' ' && cpu_name.pointer[cpu_name.length - 1] != ' ' && cpu_name.pointer[cpu_name.length - 1] != 0);
        BUSTER_TEST(arguments, trimmed);
#if BUSTER_CPU_ARCH_X86_64 || defined(__APPLE__)
        BUSTER_TEST(arguments, cpu_name.length > 0);
#endif
    }
    return result;
}
#endif
