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
    TargetCpuFeatures rocketlake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_ROCKETLAKE);
    TargetCpuFeatures rocketlake_avx512 = TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX512VL |
                                          TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512CD | TARGET_CPU_FEATURE_X86_AVX512DQ |
                                          TARGET_CPU_FEATURE_X86_AVX512IFMA | TARGET_CPU_FEATURE_X86_AVX512VBMI | TARGET_CPU_FEATURE_X86_AVX512VBMI2 |
                                          TARGET_CPU_FEATURE_X86_AVX512VNNI | TARGET_CPU_FEATURE_X86_AVX512BITALG | TARGET_CPU_FEATURE_X86_AES |
                                          TARGET_CPU_FEATURE_X86_PCLMUL |
                                          TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ | TARGET_CPU_FEATURE_X86_GFNI | TARGET_CPU_FEATURE_X86_VAES |
                                          TARGET_CPU_FEATURE_X86_VPCLMULQDQ;
    BUSTER_TEST(arguments, (rocketlake_features & rocketlake_avx512) == rocketlake_avx512);
    BUSTER_TEST(arguments, !(rocketlake_features & (TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512FP16 |
                                                    TARGET_CPU_FEATURE_X86_AVX_VNNI)));
    TargetCpuFeatures zen4_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_4);
    BUSTER_TEST(arguments, (zen4_features & (rocketlake_avx512 | TARGET_CPU_FEATURE_X86_AVX512BF16)) ==
                               (rocketlake_avx512 | TARGET_CPU_FEATURE_X86_AVX512BF16));
    BUSTER_TEST(arguments, !(zen4_features & (TARGET_CPU_FEATURE_X86_AVX512FP16 | TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT |
                                              TARGET_CPU_FEATURE_X86_AVX_VNNI)));
    TargetCpuFeatures zen5_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, (zen5_features & (rocketlake_avx512 | TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX_VNNI |
                                             TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT)) ==
                               (rocketlake_avx512 | TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX_VNNI |
                                TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT));
    BUSTER_TEST(arguments, (zen5_features & ~zen4_features) ==
                               (TARGET_CPU_FEATURE_X86_AVX_VNNI | TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT));
    BUSTER_TEST(arguments, !(zen5_features & TARGET_CPU_FEATURE_X86_AVX512FP16));
    TargetCpuFeatures tigerlake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_TIGERLAKE);
    BUSTER_TEST(arguments, (tigerlake_features & TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT) != 0);
    BUSTER_TEST(arguments, !(rocketlake_features & TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT));
    BUSTER_TEST(arguments, (tigerlake_features & ~rocketlake_features) == TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT);
    TargetCpuFeatures zen1_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_1);
    TargetCpuFeatures zen2_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_2);
    TargetCpuFeatures zen3_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_3);
    TargetCpuFeatures aes_pclmul = TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_PCLMUL;
    TargetCpuFeatures haswell_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL);
    TargetCpuFeatures broadwell_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_BROADWELL);
    BUSTER_TEST(arguments, (haswell_features & TARGET_CPU_FEATURE_X86_PCLMUL) != 0);
    BUSTER_TEST(arguments, (broadwell_features & TARGET_CPU_FEATURE_X86_PCLMUL) != 0);
    BUSTER_TEST(arguments, !(haswell_features & TARGET_CPU_FEATURE_X86_AES));
    BUSTER_TEST(arguments, !(broadwell_features & TARGET_CPU_FEATURE_X86_AES));
    BUSTER_TEST(arguments, (zen1_features & aes_pclmul) == aes_pclmul);
    BUSTER_TEST(arguments, (zen2_features & aes_pclmul) == aes_pclmul);
    BUSTER_TEST(arguments, (zen3_features & aes_pclmul) == aes_pclmul);
    BUSTER_TEST(arguments, (zen3_features & (TARGET_CPU_FEATURE_X86_VAES | TARGET_CPU_FEATURE_X86_VPCLMULQDQ)) ==
                               (TARGET_CPU_FEATURE_X86_VAES | TARGET_CPU_FEATURE_X86_VPCLMULQDQ));
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
        BUSTER_TEST(arguments, (model_features & aes_pclmul) == aes_pclmul);
    }
    TargetCpuFeatures bt2_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BT_2);
    BUSTER_TEST(arguments, (bt2_features & (TARGET_CPU_FEATURE_X86_AVX | aes_pclmul)) ==
                               (TARGET_CPU_FEATURE_X86_AVX | aes_pclmul));
    CpuModel bdver1_models[] = {CPU_MODEL_AMD_BD_1, CPU_MODEL_AMD_BD_2, CPU_MODEL_AMD_BD_3};
    for (u32 model_index = 0; model_index < BUSTER_ARRAY_LENGTH(bdver1_models); model_index += 1)
    {
        TargetCpuFeatures model_features = target_cpu_features_default(CPU_ARCH_X86_64, bdver1_models[model_index]);
        BUSTER_TEST(arguments, (model_features & (TARGET_CPU_FEATURE_X86_AVX | aes_pclmul)) ==
                                   (TARGET_CPU_FEATURE_X86_AVX | aes_pclmul));
    }
    TargetCpuFeatures bdver4_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_4);
    BUSTER_TEST(arguments, (bdver4_features & (TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | aes_pclmul)) ==
                               (TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | aes_pclmul));
    TargetCpuFeatures adl_family = aes_pclmul | TARGET_CPU_FEATURE_X86_GFNI | TARGET_CPU_FEATURE_X86_VAES |
                                   TARGET_CPU_FEATURE_X86_VPCLMULQDQ | TARGET_CPU_FEATURE_X86_AVX_VNNI;
    CpuModel adl_family_models[] = {
        CPU_MODEL_INTEL_ALDERLAKE,
        CPU_MODEL_INTEL_RAPTORLAKE,
        CPU_MODEL_INTEL_METEORLAKE,
        CPU_MODEL_INTEL_GRACEMONT,
    };
    for (u32 model_index = 0; model_index < BUSTER_ARRAY_LENGTH(adl_family_models); model_index += 1)
    {
        TargetCpuFeatures model_features = target_cpu_features_default(CPU_ARCH_X86_64, adl_family_models[model_index]);
        BUSTER_TEST(arguments, (model_features & adl_family) == adl_family);
    }
    TargetCpuFeatures arrow_lake = adl_family | TARGET_CPU_FEATURE_X86_AVX_IFMA | TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT |
                                   TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8;
    TargetCpuFeatures arrow_lake_s = arrow_lake | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16;
    TargetCpuFeatures arrow_lake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_ARROWLAKE);
    TargetCpuFeatures arrow_lake_s_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_ARROWLAKE_S);
    TargetCpuFeatures lunar_lake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_LUNARLAKE);
    TargetCpuFeatures panther_lake_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_PANTHERLAKE);
    BUSTER_TEST(arguments, (arrow_lake_features & arrow_lake) == arrow_lake);
    BUSTER_TEST(arguments, !(arrow_lake_features & TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16));
    BUSTER_TEST(arguments, (arrow_lake_s_features & arrow_lake_s) == arrow_lake_s);
    BUSTER_TEST(arguments, (lunar_lake_features & arrow_lake_s) == arrow_lake_s);
    BUSTER_TEST(arguments, (panther_lake_features & arrow_lake_s) == arrow_lake_s);
    TargetCpuFeatures sierra_forest_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_SIERRAFOREST);
    TargetCpuFeatures grandridge_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GRANDRIDGE);
    TargetCpuFeatures clearwater_forest_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_CLEARWATERFOREST);
    BUSTER_TEST(arguments, (sierra_forest_features & arrow_lake) == arrow_lake);
    BUSTER_TEST(arguments, !(sierra_forest_features & TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16));
    BUSTER_TEST(arguments, (grandridge_features & arrow_lake) == arrow_lake);
    BUSTER_TEST(arguments, (clearwater_forest_features & arrow_lake_s) == arrow_lake_s);
    TargetCpuFeatures knl_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL);
    TargetCpuFeatures knl_isa = TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F |
                                TARGET_CPU_FEATURE_X86_AVX512CD | TARGET_CPU_FEATURE_X86_AVX512PF | TARGET_CPU_FEATURE_X86_AVX512ER |
                                aes_pclmul;
    BUSTER_TEST(arguments, (knl_features & knl_isa) == knl_isa);
    TargetCpuFeatures knm_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNM);
    BUSTER_TEST(arguments, (knm_features & (knl_isa | TARGET_CPU_FEATURE_X86_AVX5124VNNIW | TARGET_CPU_FEATURE_X86_AVX5124FMAPS |
                                            TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ)) ==
                               (knl_isa | TARGET_CPU_FEATURE_X86_AVX5124VNNIW | TARGET_CPU_FEATURE_X86_AVX5124FMAPS |
                                TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ));
    TargetCpuFeatures granite_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GRANITE_RAPIDS);
    TargetCpuFeatures sapphire_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_SAPPHIRE_RAPIDS);
    TargetCpuFeatures emerald_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_EMERALD_RAPIDS);
    BUSTER_TEST(arguments, emerald_rapids_features == sapphire_rapids_features);
    BUSTER_TEST(arguments, (emerald_rapids_features & (TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512FP16 |
                                                       TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_INT8 |
                                                       TARGET_CPU_FEATURE_X86_AMX_BF16 | TARGET_CPU_FEATURE_X86_AVX_VNNI)) ==
                               (TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512FP16 | TARGET_CPU_FEATURE_X86_AMX_TILE |
                                TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 | TARGET_CPU_FEATURE_X86_AVX_VNNI));
    TargetCpuFeatures granite_rapids_inherited = sapphire_rapids_features | TARGET_CPU_FEATURE_X86_AVX10_1 |
                                                  TARGET_CPU_FEATURE_X86_AVX10_512 | TARGET_CPU_FEATURE_X86_AMX_FP16;
    BUSTER_TEST(arguments, (granite_rapids_features & granite_rapids_inherited) == granite_rapids_inherited);
    BUSTER_TEST(arguments, !(granite_rapids_features & (TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AMX_COMPLEX |
                                                        TARGET_CPU_FEATURE_X86_AVX10_V1_AUX)));
    TargetCpuFeatures granite_rapids_d_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GRANITE_RAPIDS_D);
    BUSTER_TEST(arguments, (granite_rapids_d_features & (granite_rapids_inherited | TARGET_CPU_FEATURE_X86_AMX_COMPLEX)) ==
                               (granite_rapids_inherited | TARGET_CPU_FEATURE_X86_AMX_COMPLEX));
    TargetCpuFeatures diamond_rapids_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    BUSTER_TEST(arguments, (diamond_rapids_features & (granite_rapids_inherited | TARGET_CPU_FEATURE_X86_AMX_COMPLEX |
                                                       TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX |
                                                       TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF |
                                                       TARGET_CPU_FEATURE_X86_MOVRS |
                                                       TARGET_CPU_FEATURE_X86_AMX_MOVRS | TARGET_CPU_FEATURE_X86_AMX_AVX512 |
                                                       TARGET_CPU_FEATURE_X86_AMX_FP8)) ==
                               (granite_rapids_inherited | TARGET_CPU_FEATURE_X86_AMX_COMPLEX | TARGET_CPU_FEATURE_X86_AVX10_2 |
                                TARGET_CPU_FEATURE_X86_AVX10_V1_AUX | TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF |
                                TARGET_CPU_FEATURE_X86_MOVRS | TARGET_CPU_FEATURE_X86_AMX_MOVRS |
                                TARGET_CPU_FEATURE_X86_AMX_AVX512 | TARGET_CPU_FEATURE_X86_AMX_FP8));
    BUSTER_TEST(arguments, (diamond_rapids_features & TARGET_CPU_FEATURE_X86_AVX10_V1_AUX) != 0);
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
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8_SSE3) & TARGET_CPU_FEATURE_X86_SSE3) != 0);
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_K8) & TARGET_CPU_FEATURE_X86_SSE3) == 0);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL) & (TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512VL)) == 0);
    TargetCpuFeatures bit_atomic_mask = TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_BMI1 |
                                        TARGET_CPU_FEATURE_X86_CX16;
    TargetCpuFeatures amd_legacy_bit_atomic = TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_CX16;
    TargetCpuFeatures intel_atom_bit_atomic = TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_CX16;
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_AMD_FAMILY_10) & bit_atomic_mask) == amd_legacy_bit_atomic);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BT_1) & bit_atomic_mask) == amd_legacy_bit_atomic);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BT_2) & bit_atomic_mask) == bit_atomic_mask);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_1) & bit_atomic_mask) == amd_legacy_bit_atomic);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_BD_2) & bit_atomic_mask) == bit_atomic_mask);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_BONNELL) & bit_atomic_mask) == TARGET_CPU_FEATURE_X86_CX16);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_SILVERMONT) & bit_atomic_mask) == intel_atom_bit_atomic);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_GOLDMONT) & bit_atomic_mask) == intel_atom_bit_atomic);
    BUSTER_TEST(arguments,
                (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_KNL) & bit_atomic_mask) == bit_atomic_mask);
    TargetCpuFeatures zen5_bit_atomic_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, (zen5_bit_atomic_features & (TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT |
                                                        TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16)) ==
                               (TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_BMI1 |
                                TARGET_CPU_FEATURE_X86_CX16));
    TargetCpuFeatures haswell_bit_atomic_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_HASWELL);
    BUSTER_TEST(arguments, (haswell_bit_atomic_features & (TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT |
                                                           TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16)) ==
                               (TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_BMI1 |
                                TARGET_CPU_FEATURE_X86_CX16));
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_CORE_2) & TARGET_CPU_FEATURE_X86_CX16) != 0);
    BUSTER_TEST(arguments, (target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_CORE_2) & TARGET_CPU_FEATURE_X86_POPCNT) == 0);
    Target valid_avx512 = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F,
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx512));
    Target valid_bit_atomic = valid_avx512;
    valid_bit_atomic.cpu_features |= TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT |
                                     TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_bit_atomic));
    Target invalid_avx2 = valid_avx512;
    invalid_avx2.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX2;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx2));
    Target invalid_avx512f_without_avx2 = valid_avx512;
    invalid_avx512f_without_avx2.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512f_without_avx2));
    Target invalid_sse3 = valid_avx512;
    invalid_sse3.cpu_features = TARGET_CPU_FEATURE_X86_SSE3;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_sse3));
    Target invalid_avx512vl = valid_avx512;
    invalid_avx512vl.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX512VL;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512vl));
    Target invalid_avx512bw = valid_avx512;
    invalid_avx512bw.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX512BW;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512bw));
    Target valid_gfni = valid_avx512;
    valid_gfni.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_GFNI;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_gfni));
    BUSTER_TEST(arguments, target_vector_register_size(valid_gfni) == 16);
    Target invalid_gfni_without_sse2 = valid_gfni;
    invalid_gfni_without_sse2.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_SSE2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_gfni_without_sse2));
    Target valid_vaes = valid_avx512;
    valid_vaes.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 |
                              TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_VAES;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_vaes));
    BUSTER_TEST(arguments, target_vector_register_size(valid_vaes) == 32);
    Target invalid_vaes_without_aes = valid_vaes;
    invalid_vaes_without_aes.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AES);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vaes_without_aes));
    Target invalid_vaes_without_avx2 = valid_vaes;
    invalid_vaes_without_avx2.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vaes_without_avx2));
    Target valid_vpclmul = valid_avx512;
    valid_vpclmul.cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX |
                                 TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_VPCLMULQDQ;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_vpclmul));
    Target invalid_vpclmul_without_pclmul = valid_vpclmul;
    invalid_vpclmul_without_pclmul.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_PCLMUL);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vpclmul_without_pclmul));
    Target invalid_vpclmul_without_avx = valid_vpclmul;
    invalid_vpclmul_without_avx.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_vpclmul_without_avx));
    Target valid_avx10 = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 |
                         TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512,
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx10));
    BUSTER_TEST(arguments, target_vector_register_size(valid_avx10) == 64);
    Target invalid_avx10_marker = valid_avx10;
    invalid_avx10_marker.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX10_512);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx10_marker));
    Target valid_avx512_subfeatures = valid_avx512;
    valid_avx512_subfeatures.cpu_features |= TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512CD |
                                             TARGET_CPU_FEATURE_X86_AVX512DQ |
                                             TARGET_CPU_FEATURE_X86_AVX512IFMA | TARGET_CPU_FEATURE_X86_AVX512PF |
                                             TARGET_CPU_FEATURE_X86_AVX512ER | TARGET_CPU_FEATURE_X86_AVX512VBMI |
                                             TARGET_CPU_FEATURE_X86_AVX512VBMI2 | TARGET_CPU_FEATURE_X86_AVX512VNNI |
                                             TARGET_CPU_FEATURE_X86_AVX512BITALG | TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ |
                                             TARGET_CPU_FEATURE_X86_AVX5124VNNIW | TARGET_CPU_FEATURE_X86_AVX5124FMAPS |
                                             TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT | TARGET_CPU_FEATURE_X86_AVX512BF16 |
                                             TARGET_CPU_FEATURE_X86_AVX512FP16;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx512_subfeatures));
    Target invalid_avx512_subfeature = valid_avx512_subfeatures;
    invalid_avx512_subfeature.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX512F);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512_subfeature));
    Target invalid_avx512_bw_subfeature = valid_avx512_subfeatures;
    invalid_avx512_bw_subfeature.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX512BW);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx512_bw_subfeature));
    Target valid_amx = valid_avx512;
    valid_amx.cpu_features |= TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF | TARGET_CPU_FEATURE_X86_AMX_TILE |
                              TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                              TARGET_CPU_FEATURE_X86_AMX_FP16 | TARGET_CPU_FEATURE_X86_AMX_COMPLEX | TARGET_CPU_FEATURE_X86_AMX_FP8 |
                              TARGET_CPU_FEATURE_X86_AMX_AVX512 | TARGET_CPU_FEATURE_X86_AMX_MOVRS;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_amx));
    Target invalid_amx = valid_amx;
    invalid_amx.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AMX_TILE);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_amx));
    Target invalid_apx_nci = valid_avx512;
    invalid_apx_nci.cpu_features |= TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_apx_nci));
    Target invalid_apx_without_nci = valid_avx512;
    invalid_apx_without_nci.cpu_features |= TARGET_CPU_FEATURE_X86_APX;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_apx_without_nci));
    Target invalid_amx_avx512 = valid_avx512;
    invalid_amx_avx512.cpu_features |= TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_AVX512;
    invalid_amx_avx512.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX512F);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_amx_avx512));
    Target valid_amx_movrs_without_apx = valid_avx512;
    valid_amx_movrs_without_apx.cpu_features |= TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_MOVRS;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_amx_movrs_without_apx));
    Target valid_avx_independent_features = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .cpu_features_explicit = true,
        .cpu_features = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX_VNNI |
                         TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8 |
                         TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16 | TARGET_CPU_FEATURE_X86_AVX_IFMA | TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT,
    };
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx_independent_features));
    Target invalid_avx_vnni_without_avx2 = valid_avx_independent_features;
    invalid_avx_vnni_without_avx2.cpu_features &= ~((TargetCpuFeatures)TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx_vnni_without_avx2));
    Target valid_avx10_v1_aux = valid_avx10;
    valid_avx10_v1_aux.cpu_features |= TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX;
    BUSTER_TEST(arguments, target_cpu_features_are_valid(valid_avx10_v1_aux));
    Target invalid_avx10_without_v1_aux = valid_avx10;
    invalid_avx10_without_v1_aux.cpu_features |= TARGET_CPU_FEATURE_X86_AVX10_2;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_avx10_without_v1_aux));
    Target invalid_v1_aux_without_avx10 = valid_avx10;
    invalid_v1_aux_without_avx10.cpu_features |= TARGET_CPU_FEATURE_X86_AVX10_V1_AUX;
    BUSTER_TEST(arguments, !target_cpu_features_are_valid(invalid_v1_aux_without_avx10));

    struct
    {
        String8 name;
        TargetCpuFeature feature;
    } feature_names[] = {
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
        {S8("gfni"), TARGET_CPU_FEATURE_X86_GFNI},
        {S8("movrs"), TARGET_CPU_FEATURE_X86_MOVRS},
        {S8("pclmul"), TARGET_CPU_FEATURE_X86_PCLMUL},
        {S8("vaes"), TARGET_CPU_FEATURE_X86_VAES},
        {S8("vpclmulqdq"), TARGET_CPU_FEATURE_X86_VPCLMULQDQ},
    };
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(feature_names); feature_index += 1)
    {
        BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, feature_names[feature_index].name) == feature_names[feature_index].feature);
        BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(feature_names[feature_index].feature), feature_names[feature_index].name);
    }
    BUSTER_TEST(arguments, sizeof(TargetCpuFeature) == sizeof(u64));
    BUSTER_TEST(arguments, (TARGET_CPU_FEATURE_X86_MOVRS & (UINT64_C(0x10000000000000))) != 0);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("avx2")) == TARGET_CPU_FEATURE_X86_AVX2);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("avx10-vnni-int")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("sse3")) == TARGET_CPU_FEATURE_X86_SSE3);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("popcnt")) == TARGET_CPU_FEATURE_X86_POPCNT);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("lzcnt")) == TARGET_CPU_FEATURE_X86_LZCNT);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("bmi1")) == TARGET_CPU_FEATURE_X86_BMI1);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_X86_64, S8("cx16")) == TARGET_CPU_FEATURE_X86_CX16);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("neon")) == TARGET_CPU_FEATURE_AARCH64_NEON);
    BUSTER_TEST(arguments, target_cpu_feature_from_string(CPU_ARCH_AARCH64, S8("avx2")) == TARGET_CPU_FEATURE_NONE);
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_AVX512F), S8("avx512f"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_POPCNT), S8("popcnt"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_LZCNT), S8("lzcnt"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_BMI1), S8("bmi1"));
    BUSTER_STRING_TEST(arguments, target_cpu_feature_to_string(TARGET_CPU_FEATURE_X86_CX16), S8("cx16"));
#if BUSTER_CPU_ARCH_X86_64
    X86_64CpuFeatureInput full_cpuid = {
        .maximum_basic_leaf = 0x29,
        .maximum_extended_leaf = 0x80000001,
        .basic = {.ecx = (UINT32_C(0x1)) | (UINT32_C(0x2)) | (UINT32_C(0x2000)) | (UINT32_C(0x800000)) |
                           (UINT32_C(0x2000000)) | (UINT32_C(0x8000000)) | (UINT32_C(0x10000000))},
        .extended_basic = {.ecx = UINT32_C(0x20)},
        .leaf_7_0 =
            {
                .eax = 1,
                .ebx = (UINT32_C(0x8)) | (UINT32_C(0x20)) | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x200000)) |
                       (UINT32_C(0x4000000)) | (UINT32_C(0x8000000)) | (UINT32_C(0x10000000)) | (UINT32_C(0x40000000)) | (UINT32_C(0x80000000)),
                .ecx = (UINT32_C(0x2)) | (UINT32_C(0x40)) | (UINT32_C(0x100)) | (UINT32_C(0x200)) | (UINT32_C(0x400)) |
                       (UINT32_C(0x800)) | (UINT32_C(0x1000)) | (UINT32_C(0x4000)),
                .edx = (UINT32_C(0x4)) | (UINT32_C(0x8)) | (UINT32_C(0x100)) | (UINT32_C(0x400000)) | (UINT32_C(0x800000)) |
                       (UINT32_C(0x1000000)) | (UINT32_C(0x2000000)),
            },
        .leaf_7_1 =
            {
                .eax = (UINT32_C(0x10)) | (UINT32_C(0x20)) | (UINT32_C(0x200000)) | (UINT32_C(0x800000)) | (UINT32_C(0x80000000)),
                .edx = (UINT32_C(0x10)) | (UINT32_C(0x20)) | (UINT32_C(0x100)) | (UINT32_C(0x400)) | (UINT32_C(0x80000)) |
                       (UINT32_C(0x200000)),
            },
        .leaf_1e_0 = {.eax = 1},
        .leaf_1e_1 = {.eax = (UINT32_C(0x1)) | (UINT32_C(0x2)) | (UINT32_C(0x4)) | (UINT32_C(0x8)) | (UINT32_C(0x10)) |
                              (UINT32_C(0x40)) | (UINT32_C(0x80)) | (UINT32_C(0x100))},
        .leaf_24_0 = {.eax = 1, .ebx = 2 | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000))},
        .leaf_24_1 = {.ecx = UINT32_C(0x4)},
        .leaf_29_0 = {.ebx = UINT32_C(0x1)},
        .xcr0 = UINT64_C(0xe7) | (UINT64_C(0x20000)) | (UINT64_C(0x40000)) | (UINT64_C(0x80000)),
    };
    TargetCpuFeatures full_features = x86_64_cpu_features_from_cpuid(full_cpuid);
    TargetCpuFeatures full_expected = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_SSE3 | TARGET_CPU_FEATURE_X86_POPCNT |
                                      TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16 |
                                      TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_AVX |
                                      TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX512VL |
                                      TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512CD | TARGET_CPU_FEATURE_X86_AVX512DQ |
                                      TARGET_CPU_FEATURE_X86_AVX512IFMA | TARGET_CPU_FEATURE_X86_AVX512PF | TARGET_CPU_FEATURE_X86_AVX512ER |
                                      TARGET_CPU_FEATURE_X86_AVX512VBMI | TARGET_CPU_FEATURE_X86_AVX512VBMI2 | TARGET_CPU_FEATURE_X86_AVX512VNNI |
                                      TARGET_CPU_FEATURE_X86_AVX512BITALG | TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ |
                                      TARGET_CPU_FEATURE_X86_AVX5124VNNIW | TARGET_CPU_FEATURE_X86_AVX5124FMAPS |
                                      TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT | TARGET_CPU_FEATURE_X86_AVX512BF16 |
                                      TARGET_CPU_FEATURE_X86_AVX512FP16 | TARGET_CPU_FEATURE_X86_GFNI | TARGET_CPU_FEATURE_X86_VAES |
                                      TARGET_CPU_FEATURE_X86_VPCLMULQDQ | TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_2 |
                                      TARGET_CPU_FEATURE_X86_AVX10_512 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX |
                                      TARGET_CPU_FEATURE_X86_MOVRS | TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF |
                                      TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                                      TARGET_CPU_FEATURE_X86_AMX_FP16 | TARGET_CPU_FEATURE_X86_AMX_COMPLEX | TARGET_CPU_FEATURE_X86_AMX_FP8 |
                                      TARGET_CPU_FEATURE_X86_AMX_AVX512 | TARGET_CPU_FEATURE_X86_AMX_MOVRS |
                                      TARGET_CPU_FEATURE_X86_AVX_VNNI | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8 | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16 |
                                      TARGET_CPU_FEATURE_X86_AVX_IFMA | TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT;
    BUSTER_TEST(arguments, full_features == full_expected);
    BUSTER_TEST(arguments, target_cpu_features_are_valid((Target){
                                        .cpu_arch = CPU_ARCH_X86_64,
                                        .cpu_model = CPU_MODEL_BASELINE,
                                        .cpu_features_explicit = true,
                                        .cpu_features = full_features,
                                    }));
    X86_64CpuFeatureInput no_avx2_hardware = full_cpuid;
    no_avx2_hardware.leaf_7_0.ebx &= ~(UINT32_C(0x20));
    TargetCpuFeatures no_avx2_hardware_features = x86_64_cpu_features_from_cpuid(no_avx2_hardware);
    BUSTER_TEST(arguments, !(no_avx2_hardware_features & (TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F |
                                                           TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_2 |
                                                           TARGET_CPU_FEATURE_X86_AVX10_512 | TARGET_CPU_FEATURE_X86_AVX_VNNI |
                                                           TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8 | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16 |
                                                           TARGET_CPU_FEATURE_X86_AVX_IFMA | TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT |
                                                           TARGET_CPU_FEATURE_X86_VAES)));
    BUSTER_TEST(arguments, (no_avx2_hardware_features & TARGET_CPU_FEATURE_X86_GFNI) != 0);
    BUSTER_TEST(arguments, (no_avx2_hardware_features & TARGET_CPU_FEATURE_X86_VPCLMULQDQ) != 0);
    X86_64CpuFeatureInput no_aes_hardware = full_cpuid;
    no_aes_hardware.basic.ecx &= ~(UINT32_C(0x2000000));
    TargetCpuFeatures no_aes_hardware_features = x86_64_cpu_features_from_cpuid(no_aes_hardware);
    BUSTER_TEST(arguments, !(no_aes_hardware_features & (TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_VAES)));
    BUSTER_TEST(arguments, (no_aes_hardware_features & TARGET_CPU_FEATURE_X86_VPCLMULQDQ) != 0);
    X86_64CpuFeatureInput no_pclmul_hardware = full_cpuid;
    no_pclmul_hardware.basic.ecx &= ~(UINT32_C(0x2));
    TargetCpuFeatures no_pclmul_hardware_features = x86_64_cpu_features_from_cpuid(no_pclmul_hardware);
    BUSTER_TEST(arguments, !(no_pclmul_hardware_features & (TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_VPCLMULQDQ)));
    BUSTER_TEST(arguments, (no_pclmul_hardware_features & TARGET_CPU_FEATURE_X86_VAES) != 0);
    X86_64CpuFeatureInput no_avx512bw_hardware = full_cpuid;
    no_avx512bw_hardware.leaf_7_0.ebx &= ~(UINT32_C(0x40000000));
    TargetCpuFeatures no_avx512bw_hardware_features = x86_64_cpu_features_from_cpuid(no_avx512bw_hardware);
    BUSTER_TEST(arguments, (no_avx512bw_hardware_features & TARGET_CPU_FEATURE_X86_AVX512F) != 0);
    BUSTER_TEST(arguments, !(no_avx512bw_hardware_features & (TARGET_CPU_FEATURE_X86_AVX512VBMI |
                                                               TARGET_CPU_FEATURE_X86_AVX512VBMI2 |
                                                               TARGET_CPU_FEATURE_X86_AVX512BF16 |
                                                               TARGET_CPU_FEATURE_X86_AVX512BITALG |
                                                               TARGET_CPU_FEATURE_X86_AVX512FP16)));
    X86_64CpuFeatureInput reserved_avx10_vl = full_cpuid;
    reserved_avx10_vl.leaf_24_0.ebx &= ~((UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000)));
    BUSTER_TEST(arguments, x86_64_cpu_features_from_cpuid(reserved_avx10_vl) == full_features);
    X86_64CpuFeatureInput reserved_amx_bits = full_cpuid;
    reserved_amx_bits.leaf_1e_1.eax |= (UINT32_C(0x20)) | (UINT32_C(0x40));
    BUSTER_TEST(arguments, x86_64_cpu_features_from_cpuid(reserved_amx_bits) == full_features);
    X86_64CpuFeatureInput amx_int8_legacy_only = full_cpuid;
    amx_int8_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x1));
    BUSTER_TEST(arguments, !(x86_64_cpu_features_from_cpuid(amx_int8_legacy_only) & TARGET_CPU_FEATURE_X86_AMX_INT8));
    X86_64CpuFeatureInput amx_int8_mirror_only = full_cpuid;
    amx_int8_mirror_only.leaf_7_0.edx &= ~(UINT32_C(0x2000000));
    BUSTER_TEST(arguments, !(x86_64_cpu_features_from_cpuid(amx_int8_mirror_only) & TARGET_CPU_FEATURE_X86_AMX_INT8));
    X86_64CpuFeatureInput amx_bf16_legacy_only = full_cpuid;
    amx_bf16_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x2));
    TargetCpuFeatures amx_bf16_legacy_only_features = x86_64_cpu_features_from_cpuid(amx_bf16_legacy_only);
    BUSTER_TEST(arguments, !(amx_bf16_legacy_only_features & TARGET_CPU_FEATURE_X86_AMX_BF16));
    BUSTER_TEST(arguments, (amx_bf16_legacy_only_features & TARGET_CPU_FEATURE_X86_AVX512BF16) != 0);
    X86_64CpuFeatureInput amx_bf16_mirror_only = full_cpuid;
    amx_bf16_mirror_only.leaf_7_0.edx &= ~(UINT32_C(0x400000));
    TargetCpuFeatures amx_bf16_mirror_only_features = x86_64_cpu_features_from_cpuid(amx_bf16_mirror_only);
    BUSTER_TEST(arguments, !(amx_bf16_mirror_only_features & TARGET_CPU_FEATURE_X86_AMX_BF16));
    BUSTER_TEST(arguments, (amx_bf16_mirror_only_features & TARGET_CPU_FEATURE_X86_AVX512BF16) != 0);
    X86_64CpuFeatureInput amx_bf16_without_avx512_bf16 = full_cpuid;
    amx_bf16_without_avx512_bf16.leaf_7_1.eax &= ~(UINT32_C(0x20));
    TargetCpuFeatures amx_bf16_without_avx512_bf16_features = x86_64_cpu_features_from_cpuid(amx_bf16_without_avx512_bf16);
    BUSTER_TEST(arguments, (amx_bf16_without_avx512_bf16_features & TARGET_CPU_FEATURE_X86_AMX_BF16) != 0);
    BUSTER_TEST(arguments, !(amx_bf16_without_avx512_bf16_features & TARGET_CPU_FEATURE_X86_AVX512BF16));
    X86_64CpuFeatureInput avx512_bf16_without_amx_bf16 = full_cpuid;
    avx512_bf16_without_amx_bf16.leaf_7_0.edx &= ~(UINT32_C(0x400000));
    avx512_bf16_without_amx_bf16.leaf_1e_1.eax &= ~(UINT32_C(0x2));
    TargetCpuFeatures avx512_bf16_without_amx_bf16_features = x86_64_cpu_features_from_cpuid(avx512_bf16_without_amx_bf16);
    BUSTER_TEST(arguments, (avx512_bf16_without_amx_bf16_features & TARGET_CPU_FEATURE_X86_AVX512BF16) != 0);
    BUSTER_TEST(arguments, !(avx512_bf16_without_amx_bf16_features & TARGET_CPU_FEATURE_X86_AMX_BF16));
    X86_64CpuFeatureInput amx_complex_legacy_only = full_cpuid;
    amx_complex_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x4));
    BUSTER_TEST(arguments, !(x86_64_cpu_features_from_cpuid(amx_complex_legacy_only) & TARGET_CPU_FEATURE_X86_AMX_COMPLEX));
    X86_64CpuFeatureInput amx_complex_mirror_only = full_cpuid;
    amx_complex_mirror_only.leaf_7_1.edx &= ~(UINT32_C(0x100));
    BUSTER_TEST(arguments, !(x86_64_cpu_features_from_cpuid(amx_complex_mirror_only) & TARGET_CPU_FEATURE_X86_AMX_COMPLEX));
    X86_64CpuFeatureInput amx_fp16_legacy_only = full_cpuid;
    amx_fp16_legacy_only.leaf_1e_1.eax &= ~(UINT32_C(0x8));
    BUSTER_TEST(arguments, !(x86_64_cpu_features_from_cpuid(amx_fp16_legacy_only) & TARGET_CPU_FEATURE_X86_AMX_FP16));
    X86_64CpuFeatureInput amx_fp16_mirror_only = full_cpuid;
    amx_fp16_mirror_only.leaf_7_1.eax &= ~(UINT32_C(0x200000));
    BUSTER_TEST(arguments, !(x86_64_cpu_features_from_cpuid(amx_fp16_mirror_only) & TARGET_CPU_FEATURE_X86_AMX_FP16));
    X86_64CpuFeatureInput legacy_amx_without_mirror = full_cpuid;
    legacy_amx_without_mirror.maximum_basic_leaf = 7;
    TargetCpuFeatures legacy_amx_features = x86_64_cpu_features_from_cpuid(legacy_amx_without_mirror);
    BUSTER_TEST(arguments, (legacy_amx_features & (TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                                                   TARGET_CPU_FEATURE_X86_AMX_FP16 | TARGET_CPU_FEATURE_X86_AMX_COMPLEX)) ==
                               (TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                                TARGET_CPU_FEATURE_X86_AMX_FP16 | TARGET_CPU_FEATURE_X86_AMX_COMPLEX));
    X86_64CpuFeatureInput avx10_version1 = full_cpuid;
    avx10_version1.leaf_24_0.ebx = 1 | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000));
    avx10_version1.leaf_24_1.ecx = 0;
    TargetCpuFeatures avx10_version1_features = x86_64_cpu_features_from_cpuid(avx10_version1);
    BUSTER_TEST(arguments, (avx10_version1_features & (TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512)) ==
                               (TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512));
    BUSTER_TEST(arguments, !(avx10_version1_features & (TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX)));
    X86_64CpuFeatureInput avx10_version2_without_v1_aux = full_cpuid;
    avx10_version2_without_v1_aux.leaf_24_1.ecx = 0;
    TargetCpuFeatures avx10_version2_features = x86_64_cpu_features_from_cpuid(avx10_version2_without_v1_aux);
    BUSTER_TEST(arguments, !(avx10_version2_features & (TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX)));
    BUSTER_TEST(arguments, (full_features & (TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX)) ==
                               (TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX));
    X86_64CpuFeatureInput avx10_v1_aux = full_cpuid;
    avx10_v1_aux.leaf_24_0.ebx = 1 | (UINT32_C(0x10000)) | (UINT32_C(0x20000)) | (UINT32_C(0x40000));
    avx10_v1_aux.leaf_24_1.ecx = UINT32_C(0x4);
    TargetCpuFeatures avx10_v1_aux_features = x86_64_cpu_features_from_cpuid(avx10_v1_aux);
    BUSTER_TEST(arguments, (avx10_v1_aux_features & (TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512)) ==
                               (TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512));
    BUSTER_TEST(arguments, !(avx10_v1_aux_features & (TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX)));
    X86_64CpuFeatureInput no_avx10 = full_cpuid;
    no_avx10.leaf_7_1.edx &= ~(UINT32_C(0x80000));
    TargetCpuFeatures no_avx10_features = x86_64_cpu_features_from_cpuid(no_avx10);
    BUSTER_TEST(arguments, !(no_avx10_features & (TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_2 |
                                                   TARGET_CPU_FEATURE_X86_AVX10_512)));
    BUSTER_TEST(arguments, (no_avx10_features & TARGET_CPU_FEATURE_X86_MOVRS) != 0);
    X86_64CpuFeatureInput movrs_without_avx10 = no_avx10;
    movrs_without_avx10.xcr0 = 0;
    TargetCpuFeatures movrs_without_avx10_features = x86_64_cpu_features_from_cpuid(movrs_without_avx10);
    BUSTER_TEST(arguments, (movrs_without_avx10_features & TARGET_CPU_FEATURE_X86_MOVRS) != 0);
    BUSTER_TEST(arguments, !(movrs_without_avx10_features & TARGET_CPU_FEATURE_X86_AVX));
    X86_64CpuFeatureInput no_vector_state = full_cpuid;
    no_vector_state.xcr0 = 0;
    TargetCpuFeatures no_vector_state_features = x86_64_cpu_features_from_cpuid(no_vector_state);
    BUSTER_TEST(arguments, (no_vector_state_features & (TARGET_CPU_FEATURE_X86_SSE3 | TARGET_CPU_FEATURE_X86_POPCNT |
                                                        TARGET_CPU_FEATURE_X86_LZCNT | TARGET_CPU_FEATURE_X86_BMI1 |
                                                        TARGET_CPU_FEATURE_X86_CX16 | TARGET_CPU_FEATURE_X86_AES |
                                                        TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_GFNI)) ==
                               (TARGET_CPU_FEATURE_X86_SSE3 | TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT |
                                TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16 | TARGET_CPU_FEATURE_X86_AES |
                                TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_GFNI));
    BUSTER_TEST(arguments, !(no_vector_state_features & (TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 |
                                                         TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_1 |
                                                         TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_AMX_TILE)));
    BUSTER_TEST(arguments, !(no_vector_state_features & (TARGET_CPU_FEATURE_X86_VAES | TARGET_CPU_FEATURE_X86_VPCLMULQDQ)));
    BUSTER_TEST(arguments, (no_vector_state_features & TARGET_CPU_FEATURE_X86_MOVRS) != 0);
    X86_64CpuFeatureInput no_amx_state = full_cpuid;
    no_amx_state.xcr0 &= ~((UINT64_C(0x20000)) | (UINT64_C(0x40000)));
    TargetCpuFeatures no_amx_state_features = x86_64_cpu_features_from_cpuid(no_amx_state);
    BUSTER_TEST(arguments, !(no_amx_state_features & (TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_INT8 |
                                                       TARGET_CPU_FEATURE_X86_AMX_BF16 | TARGET_CPU_FEATURE_X86_AMX_FP16 |
                                                       TARGET_CPU_FEATURE_X86_AMX_COMPLEX | TARGET_CPU_FEATURE_X86_AMX_FP8 |
                                                       TARGET_CPU_FEATURE_X86_AMX_AVX512 | TARGET_CPU_FEATURE_X86_AMX_MOVRS)));
    BUSTER_TEST(arguments, (no_amx_state_features & TARGET_CPU_FEATURE_X86_AVX512F) != 0);
    X86_64CpuFeatureInput amx_state_without_avx512_state = full_cpuid;
    amx_state_without_avx512_state.xcr0 = UINT64_C(0x6) | (UINT64_C(0x20000)) | (UINT64_C(0x40000));
    TargetCpuFeatures amx_state_without_avx512_state_features = x86_64_cpu_features_from_cpuid(amx_state_without_avx512_state);
    BUSTER_TEST(arguments, (amx_state_without_avx512_state_features & TARGET_CPU_FEATURE_X86_AMX_TILE) != 0);
    BUSTER_TEST(arguments, (amx_state_without_avx512_state_features & TARGET_CPU_FEATURE_X86_AMX_BF16) != 0);
    BUSTER_TEST(arguments, (amx_state_without_avx512_state_features & TARGET_CPU_FEATURE_X86_AMX_MOVRS) != 0);
    BUSTER_TEST(arguments, !(amx_state_without_avx512_state_features & TARGET_CPU_FEATURE_X86_AVX512F));
    BUSTER_TEST(arguments, !(amx_state_without_avx512_state_features & TARGET_CPU_FEATURE_X86_AMX_AVX512));
    X86_64CpuFeatureInput amx_avx512_with_vector_state = full_cpuid;
    amx_avx512_with_vector_state.leaf_7_0.ebx &= ~(UINT32_C(0x10000));
    amx_avx512_with_vector_state.leaf_7_1.edx &= ~(UINT32_C(0x80000));
    amx_avx512_with_vector_state.leaf_24_0.ebx = 0;
    TargetCpuFeatures amx_avx512_with_vector_state_features = x86_64_cpu_features_from_cpuid(amx_avx512_with_vector_state);
    BUSTER_TEST(arguments, !(amx_avx512_with_vector_state_features & (TARGET_CPU_FEATURE_X86_AVX512F |
                                                                       TARGET_CPU_FEATURE_X86_AVX10_1 |
                                                                       TARGET_CPU_FEATURE_X86_AMX_AVX512)));
    X86_64CpuFeatureInput avx512_state_without_amx_state = full_cpuid;
    avx512_state_without_amx_state.xcr0 &= ~((UINT64_C(0x20000)) | (UINT64_C(0x40000)));
    TargetCpuFeatures avx512_state_without_amx_state_features = x86_64_cpu_features_from_cpuid(avx512_state_without_amx_state);
    BUSTER_TEST(arguments, (avx512_state_without_amx_state_features & TARGET_CPU_FEATURE_X86_AVX512F) != 0);
    BUSTER_TEST(arguments, !(avx512_state_without_amx_state_features & TARGET_CPU_FEATURE_X86_AMX_TILE));
    X86_64CpuFeatureInput no_apx_state = full_cpuid;
    no_apx_state.xcr0 &= ~(UINT64_C(0x80000));
    TargetCpuFeatures no_apx_state_features = x86_64_cpu_features_from_cpuid(no_apx_state);
    BUSTER_TEST(arguments, !(no_apx_state_features & (TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF)));
    BUSTER_TEST(arguments, (no_apx_state_features & TARGET_CPU_FEATURE_X86_AMX_MOVRS) != 0);
    X86_64CpuFeatureInput no_apx_nci_hardware = full_cpuid;
    no_apx_nci_hardware.leaf_29_0.ebx &= ~(UINT32_C(0x1));
    TargetCpuFeatures no_apx_nci_hardware_features = x86_64_cpu_features_from_cpuid(no_apx_nci_hardware);
    BUSTER_TEST(arguments, !(no_apx_nci_hardware_features & (TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF)));
    X86_64CpuFeatureInput no_apx_f_hardware = full_cpuid;
    no_apx_f_hardware.leaf_7_1.edx &= ~(UINT32_C(0x200000));
    TargetCpuFeatures no_apx_f_hardware_features = x86_64_cpu_features_from_cpuid(no_apx_f_hardware);
    BUSTER_TEST(arguments, !(no_apx_f_hardware_features & (TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF)));
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
    sorted_features.cpu_features |= TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                                    TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_GFNI | TARGET_CPU_FEATURE_X86_PCLMUL |
                                    TARGET_CPU_FEATURE_X86_VAES | TARGET_CPU_FEATURE_X86_VPCLMULQDQ;
    BUSTER_STRING_TEST(arguments, target_cpu_features_to_string(arguments->arena, sorted_features),
                       S8("aes,amx-bf16,amx-tile,apx,avx,avx10-512,avx10-v1-aux,avx10.1,avx10.2,avx2,avx512f,gfni,pclmul,sse2,vaes,vpclmulqdq"));
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
