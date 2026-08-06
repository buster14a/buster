#include <buster/lib/x86_64.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL CpuId cpuid(u32 leaf, u32 subleaf)
{
    CpuId result = {0};
#if BUSTER_COMPILER_MSVC
    int cpu_info[4];

    __cpuidex(cpu_info, (int)leaf, (int)subleaf);

    result.eax = (u32)cpu_info[0];
    result.ebx = (u32)cpu_info[1];
    result.ecx = (u32)cpu_info[2];
    result.edx = (u32)cpu_info[3];
#else
    __asm__ volatile("cpuid" : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx) : "a"(leaf), "c"(subleaf));
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL u64 xgetbv(u32 index)
{
#if BUSTER_COMPILER_TCC
    BUSTER_UNUSED(index);
    return 0;
#elif BUSTER_COMPILER_MSVC
    return _xgetbv(index);
#else
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(index));
    return (u64)low | ((u64)high << 32);
#endif
}

X86_64EncodedInstruction x86_64_encode_register_operation(X86_64RegisterOperation operation, u32 target_register, u32 source_register)
{
    X86_64EncodedInstruction result = {0};
    if (operation >= X86_64_REGISTER_OPERATION_COUNT || target_register >= 32 || source_register >= 32)
    {
        return result;
    }
    u8 opcodes[] = {
        0x89, 0x01, 0x29, 0x21, 0x09, 0x31,
    };
    if (target_register >= 16 || source_register >= 16)
    {
        result.bytes[result.length++] = 0xd5;
        result.bytes[result.length++] = (u8)(0x08 | (source_register >= 16 ? 0x40 : 0) | (target_register >= 16 ? 0x10 : 0) |
                                             ((source_register >> 3) & 1) << 2 | ((target_register >> 3) & 1));
    }
    else
    {
        result.bytes[result.length++] = (u8)(0x48 | ((source_register >> 3) & 1) << 2 | ((target_register >> 3) & 1));
    }
    result.bytes[result.length++] = opcodes[operation];
    result.bytes[result.length++] = (u8)(0xc0 | ((source_register & 7) << 3) | (target_register & 7));
    return result;
}

TargetCpuFeatures x86_64_cpu_features_from_cpuid(X86_64CpuFeatureInput input)
{
    TargetCpuFeatures result = TARGET_CPU_FEATURE_X86_SSE2;
    CpuId basic = input.basic;
    if (basic.ecx & (UINT32_C(0x1)))
    {
        result |= TARGET_CPU_FEATURE_X86_SSE3;
    }
    if (basic.ecx & (UINT32_C(0x2000)))
    {
        result |= TARGET_CPU_FEATURE_X86_CX16;
    }
    if (basic.ecx & (UINT32_C(0x800000)))
    {
        result |= TARGET_CPU_FEATURE_X86_POPCNT;
    }
    if (basic.ecx & (UINT32_C(0x2000000)))
    {
        result |= TARGET_CPU_FEATURE_X86_AES;
    }
    if (basic.ecx & (UINT32_C(0x2)))
    {
        result |= TARGET_CPU_FEATURE_X86_PCLMUL;
    }
    if (input.maximum_extended_leaf >= UINT32_C(0x80000001) && (input.extended_basic.ecx & (UINT32_C(0x20))))
    {
        result |= TARGET_CPU_FEATURE_X86_LZCNT;
    }

    bool has_osxsave = (basic.ecx & (UINT32_C(0x8000000))) != 0;
    bool has_avx_hardware = (basic.ecx & (UINT32_C(0x10000000))) != 0;
    bool avx_state = has_osxsave && (input.xcr0 & UINT64_C(0x6)) == UINT64_C(0x6);
    bool avx_usable = has_avx_hardware && avx_state;
    bool avx512_state = avx_usable && (input.xcr0 & UINT64_C(0xe0)) == UINT64_C(0xe0);
    bool amx_state = has_osxsave &&
                     (input.xcr0 & (UINT64_C(0x20000) | UINT64_C(0x40000))) == (UINT64_C(0x20000) | UINT64_C(0x40000));
    bool apx_state = has_osxsave && (input.xcr0 & (UINT64_C(0x80000))) != 0;
    if (input.maximum_extended_leaf >= UINT32_C(0x80000001))
    {
        if (input.extended_basic.edx & (UINT32_C(0x80000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_3DNOW;
        }
        if (input.extended_basic.edx & (UINT32_C(0x40000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_3DNOWA;
        }
        if (avx_usable && (input.extended_basic.ecx & (UINT32_C(0x800))))
        {
            result |= TARGET_CPU_FEATURE_X86_XOP;
        }
        if (avx_usable && (input.extended_basic.ecx & (UINT32_C(0x10000))))
        {
            result |= TARGET_CPU_FEATURE_X86_FMA4;
        }
        if (input.extended_basic.ecx & (UINT32_C(0x8000)))
        {
            result |= TARGET_CPU_FEATURE_X86_LWP;
        }
        if (input.extended_basic.ecx & (UINT32_C(0x200000)))
        {
            result |= TARGET_CPU_FEATURE_X86_TBM;
        }
    }
    if (avx_usable)
    {
        result |= TARGET_CPU_FEATURE_X86_AVX;
    }

    CpuId leaf_7_0 = input.leaf_7_0;
    CpuId leaf_7_1 = input.leaf_7_1;
    bool has_leaf_7 = input.maximum_basic_leaf >= 7;
    bool has_leaf_7_1 = has_leaf_7 && leaf_7_0.eax >= 1;
    // CPUID.07H:0.EDX[20] advertises CET indirect-branch tracking, which
    // supplies the ENDBR32/ENDBR64 forms exposed by the assembler metadata.
    if (has_leaf_7 && (leaf_7_0.edx & (UINT32_C(0x100000))))
    {
        result |= TARGET_CPU_FEATURE_X86_IBT;
    }
    // CPUID.07H:0.ECX[7] advertises CET shadow stacks.
    if (has_leaf_7 && (leaf_7_0.ecx & (UINT32_C(0x80))))
    {
        result |= TARGET_CPU_FEATURE_X86_SHSTK;
    }
    // CPUID.07H:0.ECX[25] advertises CLDEMOTE independently of vector state.
    if (has_leaf_7 && (leaf_7_0.ecx & (UINT32_C(0x2000000))))
    {
        result |= TARGET_CPU_FEATURE_X86_CLDEMOTE;
    }
    // CPUID.07H:1.EDX[14] advertises PREFETCHIT0/PREFETCHIT1.
    if (has_leaf_7_1 && (leaf_7_1.edx & (UINT32_C(0x4000))))
    {
        result |= TARGET_CPU_FEATURE_X86_PREFETCHI;
    }
    bool avx2_usable = false;
    if (has_leaf_7 && (result & TARGET_CPU_FEATURE_X86_AVX) && (leaf_7_0.ebx & (UINT32_C(0x20))))
    {
        result |= TARGET_CPU_FEATURE_X86_AVX2;
        avx2_usable = true;
    }
    if (has_leaf_7 && (leaf_7_0.ebx & (UINT32_C(0x8))))
    {
        result |= TARGET_CPU_FEATURE_X86_BMI1;
    }
    if (has_leaf_7 && (leaf_7_0.ecx & (UINT32_C(0x100))))
    {
        result |= TARGET_CPU_FEATURE_X86_GFNI;
    }
    if (avx2_usable && (result & TARGET_CPU_FEATURE_X86_AES) && (leaf_7_0.ecx & (UINT32_C(0x200))))
    {
        result |= TARGET_CPU_FEATURE_X86_VAES;
    }
    if (avx_usable && (result & TARGET_CPU_FEATURE_X86_PCLMUL) && (leaf_7_0.ecx & (UINT32_C(0x400))))
    {
        result |= TARGET_CPU_FEATURE_X86_VPCLMULQDQ;
    }
    bool avx10_hardware = has_leaf_7_1 && (leaf_7_1.edx & (UINT32_C(0x80000))) != 0;
    bool has_leaf_24 = input.maximum_basic_leaf >= UINT32_C(0x24);
    u32 avx10_version = has_leaf_24 ? input.leaf_24_0.ebx & UINT32_C(0xff) : 0;
    bool avx10_usable = avx2_usable && avx512_state && avx10_hardware && avx10_version >= 1;
    bool avx512f_usable = avx2_usable && avx512_state && (leaf_7_0.ebx & (UINT32_C(0x10000))) != 0;
    bool avx512_path_usable = avx512f_usable || avx10_usable;
    bool avx512bw_hardware = (leaf_7_0.ebx & (UINT32_C(0x40000000))) != 0;
    if (avx512_path_usable)
    {
        result |= TARGET_CPU_FEATURE_X86_AVX512F;
        if (leaf_7_0.ebx & (UINT32_C(0x20000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512DQ;
        }
        if (leaf_7_0.ebx & (UINT32_C(0x200000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512IFMA;
        }
        if (leaf_7_0.ebx & (UINT32_C(0x4000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512PF;
        }
        if (leaf_7_0.ebx & (UINT32_C(0x8000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512ER;
        }
        if (leaf_7_0.ebx & (UINT32_C(0x10000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512CD;
        }
        if (leaf_7_0.ebx & (UINT32_C(0x40000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512BW;
        }
        if (leaf_7_0.ebx & (UINT32_C(0x80000000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512VL;
        }
        if (avx512bw_hardware && (leaf_7_0.ecx & (UINT32_C(0x2))))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512VBMI;
        }
        if (avx512bw_hardware && (leaf_7_0.ecx & (UINT32_C(0x40))))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512VBMI2;
        }
        if (leaf_7_0.ecx & (UINT32_C(0x800)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512VNNI;
        }
        if (avx512bw_hardware && (leaf_7_0.ecx & (UINT32_C(0x1000))))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512BITALG;
        }
        if (leaf_7_0.ecx & (UINT32_C(0x4000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ;
        }
        if (leaf_7_0.edx & (UINT32_C(0x4)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX5124VNNIW;
        }
        if (leaf_7_0.edx & (UINT32_C(0x8)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX5124FMAPS;
        }
        if (leaf_7_0.edx & (UINT32_C(0x100)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT;
        }
        if (avx512bw_hardware && (leaf_7_0.edx & (UINT32_C(0x800000))))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512FP16;
        }
        if (avx512bw_hardware && has_leaf_7_1 && (leaf_7_1.eax & (UINT32_C(0x20))))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX512BF16;
        }
    }

    if (avx10_usable)
    {
        // Current AVX10.2 revision 7 reserves EBX[18:16] at one and states
        // that every usable AVX10 processor supports all three vector
        // lengths.  Keep AVX10_512 as the historical target marker, but do
        // not decode the reserved field as independent capabilities.
        result |= TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F |
                  TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512;
        if (avx10_version >= 2 && has_leaf_24 && input.leaf_24_0.eax >= 1 && (input.leaf_24_1.ecx & (UINT32_C(0x4))))
        {
            // AVX10.2 and AVX10_V1_AUX co-enumerate on conforming hardware.
            // Require both fields so malformed synthetic input under-reports
            // the pair instead of manufacturing an impossible target.
            result |= TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX;
        }
    }

    if (has_leaf_7_1 && avx2_usable)
    {
        if (leaf_7_1.eax & (UINT32_C(0x10)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX_VNNI;
        }
        if (leaf_7_1.eax & (UINT32_C(0x800000)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX_IFMA;
        }
        if (leaf_7_1.edx & (UINT32_C(0x10)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8;
        }
        if (leaf_7_1.edx & (UINT32_C(0x20)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT;
        }
        if (leaf_7_1.edx & (UINT32_C(0x400)))
        {
            result |= TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16;
        }
    }

    // MOVRS is a separate scalar/vector capability.  The EVEX vector forms
    // additionally require AVX10 at encoding time, but the CPUID feature is
    // usable and reportable independently of AVX10.
    if (has_leaf_7_1 && (leaf_7_1.eax & (UINT32_C(0x80000000))))
    {
        result |= TARGET_CPU_FEATURE_X86_MOVRS;
    }

    bool apx_hardware = has_leaf_7_1 && (leaf_7_1.edx & (UINT32_C(0x200000))) != 0;
    bool apx_nci_hardware = input.maximum_basic_leaf >= UINT32_C(0x29) && (input.leaf_29_0.ebx & (UINT32_C(0x1))) != 0;
    // APX-F and APX_NCI_NDD_NF co-enumerate on conforming hardware.  Require
    // both fields and usable APX state so malformed synthetic input under-
    // reports the pair instead of publishing an impossible target.
    if (apx_hardware && apx_nci_hardware && apx_state)
    {
        result |= TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF;
    }

    bool has_leaf_1e_1 = input.maximum_basic_leaf >= UINT32_C(0x1e) && input.leaf_1e_0.eax >= 1;
    bool amx_tile_hardware = has_leaf_7 && (leaf_7_0.edx & (UINT32_C(0x1000000))) != 0;
    bool amx_int8_hardware = has_leaf_7 && (leaf_7_0.edx & (UINT32_C(0x2000000))) != 0;
    bool amx_bf16_hardware = has_leaf_7 && (leaf_7_0.edx & (UINT32_C(0x400000))) != 0;
    bool amx_fp16_hardware = has_leaf_7_1 && (leaf_7_1.eax & (UINT32_C(0x200000))) != 0;
    bool amx_complex_hardware = has_leaf_7_1 && (leaf_7_1.edx & (UINT32_C(0x100))) != 0;
    if (has_leaf_1e_1)
    {
        // CPUID.1E.1 mirrors these four AMX fields from legacy leaves.  Once
        // the mirror exists, require both copies so malformed one-sided
        // synthetic input cannot publish a feature.
        amx_int8_hardware = amx_int8_hardware && (input.leaf_1e_1.eax & (UINT32_C(0x1))) != 0;
        amx_bf16_hardware = amx_bf16_hardware && (input.leaf_1e_1.eax & (UINT32_C(0x2))) != 0;
        amx_complex_hardware = amx_complex_hardware && (input.leaf_1e_1.eax & (UINT32_C(0x4))) != 0;
        amx_fp16_hardware = amx_fp16_hardware && (input.leaf_1e_1.eax & (UINT32_C(0x8))) != 0;
    }
    if (amx_state && amx_tile_hardware)
    {
        result |= TARGET_CPU_FEATURE_X86_AMX_TILE;
        if (amx_int8_hardware)
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_INT8;
        }
        if (amx_bf16_hardware)
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_BF16;
        }
        if (amx_fp16_hardware)
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_FP16;
        }
        if (amx_complex_hardware)
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_COMPLEX;
        }
        if (has_leaf_1e_1 && (input.leaf_1e_1.eax & (UINT32_C(0x10))))
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_FP8;
        }
        // AMX-AVX512 instructions also consume the AVX-512 architectural
        // state.  AMX tile state alone is sufficient for the other AMX
        // families, but must not make this vector-state family usable.
        if (has_leaf_1e_1 && (input.leaf_1e_1.eax & (UINT32_C(0x80))) && avx512_state &&
            (result & (TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_1)))
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_AVX512;
        }
        // AMX-MOVRS is independently enumerated.  Its tile instructions
        // still require the independent AMX tile state above, but do not
        // require APX state.
        if (has_leaf_1e_1 && (input.leaf_1e_1.eax & (UINT32_C(0x100))))
        {
            result |= TARGET_CPU_FEATURE_X86_AMX_MOVRS;
        }
    }
    return result;
}

TargetCpuFeatures cpu_detect_features_x86_64(void)
{
    X86_64CpuFeatureInput input = {0};
    CpuId maximum = cpuid(0, 0);
    input.maximum_basic_leaf = maximum.eax;
    if (input.maximum_basic_leaf >= 1)
    {
        input.basic = cpuid(1, 0);
    }
    CpuId extended_maximum = cpuid(UINT32_C(0x80000000), 0);
    input.maximum_extended_leaf = extended_maximum.eax;
    if (input.maximum_extended_leaf >= UINT32_C(0x80000001))
    {
        input.extended_basic = cpuid(UINT32_C(0x80000001), 0);
    }
    if (input.maximum_basic_leaf >= 7)
    {
        input.leaf_7_0 = cpuid(7, 0);
        if (input.leaf_7_0.eax >= 1)
        {
            input.leaf_7_1 = cpuid(7, 1);
        }
    }
    if (input.maximum_basic_leaf >= UINT32_C(0x1e))
    {
        input.leaf_1e_0 = cpuid(UINT32_C(0x1e), 0);
        if (input.leaf_1e_0.eax >= 1)
        {
            input.leaf_1e_1 = cpuid(UINT32_C(0x1e), 1);
        }
    }
    if (input.maximum_basic_leaf >= UINT32_C(0x24))
    {
        input.leaf_24_0 = cpuid(UINT32_C(0x24), 0);
        if (input.leaf_24_0.eax >= 1)
        {
            input.leaf_24_1 = cpuid(UINT32_C(0x24), 1);
        }
    }
    if (input.maximum_basic_leaf >= UINT32_C(0x29))
    {
        input.leaf_29_0 = cpuid(UINT32_C(0x29), 0);
    }
    if (input.basic.ecx & (UINT32_C(0x8000000)))
    {
        input.xcr0 = xgetbv(0);
    }
    return x86_64_cpu_features_from_cpuid(input);
}

CpuModel cpu_detect_model_x86_64(void)
{
    CpuId vendor_cpuid = cpuid(0, 0);
    char8 vendor_buffer[3 * sizeof(vendor_cpuid.eax)];
    String8 vendor_string = (String8)BUSTER_ARRAY_TO_SLICE(vendor_buffer);
    *(u32*)(vendor_buffer + 0 * sizeof(vendor_cpuid.eax)) = vendor_cpuid.ebx;
    *(u32*)(vendor_buffer + 1 * sizeof(vendor_cpuid.eax)) = vendor_cpuid.edx;
    *(u32*)(vendor_buffer + 2 * sizeof(vendor_cpuid.eax)) = vendor_cpuid.ecx;

    CpuModel result = CPU_MODEL_ERROR;

    CpuId family_model_cpuid = cpuid(1, 0);

    // let stepping = (u8)((amd_cpuid.eax >> 0) & 0xf);
    u8 model = (u8)((family_model_cpuid.eax >> 4) & 0xf);
    u8 original_family = (u8)((family_model_cpuid.eax >> 8) & 0xf);
    u8 extended_model = (u8)((family_model_cpuid.eax >> 16) & 0xf);
    u8 extended_family = (u8)((family_model_cpuid.eax >> 20) & 0xff);

    u8 family = original_family == 0xf ? original_family + extended_family : original_family;
    model = ((original_family == 0x6) | (original_family == 0xf)) ? (u8)((extended_model << 4) | model) : model;

    bool has_sse = ((family_model_cpuid.edx >> 25) & 1) != 0;
    bool has_sse3 = ((family_model_cpuid.ecx >> 0) & 1) != 0;
    bool has_avx512bf16 = false;
    bool has_avx512vnni = false;

    // vendor_cpuid.eax is the maximum supported standard leaf; leaf 7's own
    // eax is the maximum supported subleaf.
    if (vendor_cpuid.eax >= 7)
    {
        CpuId extended_features = cpuid(7, 0);
        has_avx512vnni = ((extended_features.ecx >> 11) & 1) != 0;

        if (extended_features.eax >= 1)
        {
            CpuId extended_features_1 = cpuid(7, 1);
            has_avx512bf16 = ((extended_features_1.eax >> 5) & 1) != 0;
        }
    }

    if (string_equal(vendor_string, S8("AuthenticAMD")))
    {
        switch (family)
        {
            break;
        case 4:
            result = CPU_MODEL_AMD_I486;
            break;
        case 5:
        {
            result = CPU_MODEL_AMD_PENTIUM;

            switch (model)
            {
                break;
            case 6:
            case 7:
                result = CPU_MODEL_AMD_K6;
                break;
            case 8:
                result = CPU_MODEL_AMD_K6_2;
                break;
            case 9:
            case 13:
                result = CPU_MODEL_AMD_K6_3;
                break;
            case 10:
                result = CPU_MODEL_AMD_K6_3;
            }
        }
        break;
        case 6:
            result = has_sse ? CPU_MODEL_AMD_ATHLON_XP : CPU_MODEL_AMD_ATHLON;
            break;
        case 15:
            result = has_sse3 ? CPU_MODEL_AMD_K8_SSE3 : CPU_MODEL_AMD_K8;
            break;
        case 16:
        case 18:
            result = CPU_MODEL_AMD_AMD_FAMILY_10;
            break;
        case 20:
            result = CPU_MODEL_AMD_BT_1;
            break;
        case 21:
        {
            result = CPU_MODEL_AMD_BD_1;
            if (model >= 0x60 && model <= 0x7f)
            {
                result = CPU_MODEL_AMD_BD_4;
            }
            else if (model >= 0x30 && model <= 0x3f)
            {
                result = CPU_MODEL_AMD_BD_3;
            }
            else if ((model >= 0x10 && model <= 0x1f) || model == 0x02)
            {
                result = CPU_MODEL_AMD_BD_2;
            }
        }
        break;
        case 22:
            result = CPU_MODEL_AMD_BT_2;
            break;
        case 23:
        {
            result = CPU_MODEL_AMD_ZEN_1;
            if ((model >= 0x30 && model <= 0x3f) || (model == 0x47) || (model >= 0x60 && model <= 0x67) || (model >= 0x68 && model <= 0x6f) ||
                (model >= 0x70 && model <= 0x7f) || (model >= 0x84 && model <= 0x87) || (model >= 0x90 && model <= 0x97) || (model >= 0x98 && model <= 0x9f) ||
                (model >= 0xa0 && model <= 0xaf))
            {
                // Family 17h Models 30h-3Fh (Starship) Zen 2
                // Family 17h Models 47h (Cardinal) Zen 2
                // Family 17h Models 60h-67h (Renoir) Zen 2
                // Family 17h Models 68h-6Fh (Lucienne) Zen 2
                // Family 17h Models 70h-7Fh (Matisse) Zen 2
                // Family 17h Models 84h-87h (ProjectX) Zen 2
                // Family 17h Models 90h-97h (VanGogh) Zen 2
                // Family 17h Models 98h-9Fh (Mero) Zen 2
                // Family 17h Models A0h-AFh (Mendocino) Zen 2
                result = CPU_MODEL_AMD_ZEN_2;
            }
        }
        break;
        case 25:
        {
            result = CPU_MODEL_AMD_ZEN_3;
            if ((model >= 0x10 && model <= 0x1f) || (model >= 0x60 && model <= 0x6f) || (model >= 0x70 && model <= 0x77) || (model >= 0x78 && model <= 0x7f) ||
                (model >= 0xa0 && model <= 0xaf))
            {
                // Family 19h Models 10h-1Fh (Stones; Storm Peak) Zen 4
                // Family 19h Models 60h-6Fh (Raphael) Zen 4
                // Family 19h Models 70h-77h (Phoenix, Hawkpoint1) Zen 4
                // Family 19h Models 78h-7Fh (Phoenix 2, Hawkpoint2) Zen 4
                // Family 19h Models A0h-AFh (Stones-Dense) Zen 4
                result = CPU_MODEL_AMD_ZEN_4;
            }
        }
        break;
        case 26:
            result = CPU_MODEL_AMD_ZEN_5;
            break;
        default:
        {
        };
        }
    }
    else if (string_equal(vendor_string, S8("GenuineIntel")))
    {
        switch (family)
        {
            break;
        case 6:
        {
            switch (model)
            {
                break;
            case 0x0f:
            case 0x16:
                result = CPU_MODEL_INTEL_CORE_2;
                break;
            case 0x17:
            case 0x1d:
                result = CPU_MODEL_INTEL_PENRYN;
                break;
            case 0x1a:
            case 0x1e:
            case 0x1f:
            case 0x2e:
                result = CPU_MODEL_INTEL_NEHALEM;
                break;
            case 0x25:
            case 0x2c:
            case 0x2f:
                result = CPU_MODEL_INTEL_WESTMERE;
                break;
            case 0x2a:
            case 0x2d:
                result = CPU_MODEL_INTEL_SANDY_BRIDGE;
                break;
            case 0x3a:
            case 0x3e:
                result = CPU_MODEL_INTEL_IVY_BRIDGE;
                break;
            case 0x3c:
            case 0x3f:
            case 0x45:
            case 0x46:
                result = CPU_MODEL_INTEL_HASWELL;
                break;
            case 0x3d:
            case 0x47:
            case 0x4f:
            case 0x56:
                result = CPU_MODEL_INTEL_BROADWELL;
                break;
            case 0x4e:
            case 0x5e:
            case 0x8e:
            case 0x9e:
            case 0xa5:
            case 0xa6:
                result = CPU_MODEL_INTEL_SKYLAKE;
                break;
            case 0xa7:
                result = CPU_MODEL_INTEL_ROCKETLAKE;
                break;
            case 0x55:
            {
                if (has_avx512bf16)
                {
                    result = CPU_MODEL_INTEL_COOPERLAKE;
                }
                else if (has_avx512vnni)
                {
                    result = CPU_MODEL_INTEL_CASCADELAKE;
                }
                else
                {
                    result = CPU_MODEL_INTEL_SKYLAKE_AVX512;
                }
            }
            break;
            case 0x66:
                result = CPU_MODEL_INTEL_CANNONLAKE;
                break;
            case 0x7d:
            case 0x7e:
                result = CPU_MODEL_INTEL_ICELAKE_CLIENT;
                break;
            case 0x8c:
            case 0x8d:
                result = CPU_MODEL_INTEL_TIGERLAKE;
                break;
            case 0x97:
            case 0x9a:
                result = CPU_MODEL_INTEL_ALDERLAKE;
                break;
            case 0xb7:
            case 0xba:
            case 0xbf:
                result = CPU_MODEL_INTEL_RAPTORLAKE;
                break;
            case 0xaa:
            case 0xac:
                result = CPU_MODEL_INTEL_METEORLAKE;
                break;
            case 0xbe:
                result = CPU_MODEL_INTEL_GRACEMONT;
                break;
            case 0xc5:
            case 0xb5:
                result = CPU_MODEL_INTEL_ARROWLAKE;
                break;
            case 0xc6:
                result = CPU_MODEL_INTEL_ARROWLAKE_S;
                break;
            case 0xbd:
                result = CPU_MODEL_INTEL_LUNARLAKE;
                break;
            case 0xcc:
                result = CPU_MODEL_INTEL_PANTHERLAKE;
                break;
            case 0x6a:
            case 0x6c:
                result = CPU_MODEL_INTEL_ICELAKE_SERVER;
                break;
            case 0xcf:
                result = CPU_MODEL_INTEL_EMERALD_RAPIDS;
                break;
            case 0x8f:
                result = CPU_MODEL_INTEL_SAPPHIRE_RAPIDS;
                break;
            case 0xad:
                result = CPU_MODEL_INTEL_GRANITE_RAPIDS;
                break;
            case 0xae:
                result = CPU_MODEL_INTEL_GRANITE_RAPIDS_D;
                break;
            case 0x1c:
            case 0x26:
            case 0x27:
            case 0x35:
            case 0x36:
                result = CPU_MODEL_INTEL_BONNELL;
                break;
            case 0x37:
            case 0x4a:
            case 0x4d:
            case 0x5a:
            case 0x5d:
            case 0x4c:
                result = CPU_MODEL_INTEL_SILVERMONT;
                break;
            case 0x5c:
            case 0x5f:
                result = CPU_MODEL_INTEL_GOLDMONT;
                break;
            case 0x7a:
                result = CPU_MODEL_INTEL_GOLDMONT_PLUS;
                break;
            case 0x86:
            case 0x8a:
            case 0x96:
            case 0x9c:
                result = CPU_MODEL_INTEL_TREMONT;
                break;
            case 0xaf:
                result = CPU_MODEL_INTEL_SIERRAFOREST;
                break;
            case 0xb6:
                result = CPU_MODEL_INTEL_GRANDRIDGE;
                break;
            case 0xdd:
                result = CPU_MODEL_INTEL_CLEARWATERFOREST;
                break;
            case 0x57:
                result = CPU_MODEL_INTEL_KNL;
                break;
            case 0x85:
                result = CPU_MODEL_INTEL_KNM;
                break;
            default:
            {
            }
            }
        }
        break;
        case 19:
        {
            switch (model)
            {
                break;
            case 1:
                result = CPU_MODEL_INTEL_DIAMOND_RAPIDS;
                break;
            default:
            {
            }
            }
        }
        }
    }
    else
    {
        string_print(S8("Vendor string: {S8}\n"), vendor_string);
    }

    return result;
}
