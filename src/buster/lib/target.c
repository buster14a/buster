#include <buster/lib/target.h>

#include <buster/lib/string.h>

#if BUSTER_UNITY_BUILD
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/x86_64.c>
#endif
#if BUSTER_CPU_ARCH_AARCH64
#include <buster/lib/aarch64.c>
#endif
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/machine.h>
#endif

BUSTER_V_IMPL Target target_native = {
#if BUSTER_CPU_ARCH_X86_64
    .cpu_arch = CPU_ARCH_X86_64,
#elif BUSTER_CPU_ARCH_AARCH64
    .cpu_arch = CPU_ARCH_AARCH64,
#else
#error unsupported CPU architecture
#endif
    .cpu_model = CPU_MODEL_NATIVE,
#if defined(__ANDROID__)
    .os = OPERATING_SYSTEM_ANDROID,
#elif defined(__linux__)
    .os = OPERATING_SYSTEM_LINUX,
#elif defined(_WIN32)
    .os = OPERATING_SYSTEM_WINDOWS,
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    .os = OPERATING_SYSTEM_IOS,
#elif TARGET_OS_OSX
    .os = OPERATING_SYSTEM_MACOS,
#else
#error unsupported Apple platform
#endif
#endif
};

TargetDataLayout target_data_layout(Target target)
{
    bool windows = target.os == OPERATING_SYSTEM_WINDOWS;
    bool apple = target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS;
    bool arm_plain_char_unsigned = target.cpu_arch == CPU_ARCH_AARCH64 && !apple && !windows;
    u32 long_size = windows ? 4 : 8;
    u32 long_double_size = windows || apple ? 8 : 16;
    u32 long_double_bits = windows || apple ? 64 : target.cpu_arch == CPU_ARCH_X86_64 ? 80 : 128;
    u32 va_list_size = windows ? 8 : target.cpu_arch == CPU_ARCH_X86_64 ? 32 : 32;

    TargetDataLayout layout = {
        .boolean = {.size = 1, .alignment = 1, .bit_width = 1},
        .plain_char = {.size = 1, .alignment = 1, .bit_width = 8},
        .signed_char = {.size = 1, .alignment = 1, .bit_width = 8},
        .unsigned_char = {.size = 1, .alignment = 1, .bit_width = 8},
        .short_integer = {.size = 2, .alignment = 2, .bit_width = 16},
        .unsigned_short_integer = {.size = 2, .alignment = 2, .bit_width = 16},
        .integer = {.size = 4, .alignment = 4, .bit_width = 32},
        .unsigned_integer = {.size = 4, .alignment = 4, .bit_width = 32},
        .long_integer = {.size = long_size, .alignment = long_size, .bit_width = long_size * 8},
        .unsigned_long_integer = {.size = long_size, .alignment = long_size, .bit_width = long_size * 8},
        .long_long_integer = {.size = 8, .alignment = 8, .bit_width = 64},
        .unsigned_long_long_integer = {.size = 8, .alignment = 8, .bit_width = 64},
        .integer128 = {.size = 16, .alignment = 16, .bit_width = 128},
        .unsigned_integer128 = {.size = 16, .alignment = 16, .bit_width = 128},
        .float_type = {.size = 4, .alignment = 4, .bit_width = 32},
        .double_type = {.size = 8, .alignment = 8, .bit_width = 64},
        .long_double_type = {.size = long_double_size, .alignment = long_double_size, .bit_width = long_double_bits},
        .pointer = {.size = 8, .alignment = 8, .bit_width = 64},
        .va_list = {.size = va_list_size, .alignment = 8, .bit_width = va_list_size * 8},
        .atomic_min_width = 8,
        .atomic_max_width = 128,
        .atomic_alignment = 16,
        .abi_stack_alignment = 16,
        .abi_max_alignment = 16,
        .endianness = TARGET_ENDIAN_LITTLE,
        .plain_char_is_signed = !arm_plain_char_unsigned,
        .has_128_bit_integer = true,
    };
    return layout;
}

BUSTER_GLOBAL_LOCAL bool target_layout_alignment_valid(u32 alignment)
{
    return alignment && !(alignment & (alignment - 1));
}

bool target_data_layout_is_valid(TargetDataLayout layout)
{
    TargetTypeLayout types[] = {
        layout.boolean,
        layout.plain_char,
        layout.signed_char,
        layout.unsigned_char,
        layout.short_integer,
        layout.unsigned_short_integer,
        layout.integer,
        layout.unsigned_integer,
        layout.long_integer,
        layout.unsigned_long_integer,
        layout.long_long_integer,
        layout.unsigned_long_long_integer,
        layout.integer128,
        layout.unsigned_integer128,
        layout.float_type,
        layout.double_type,
        layout.long_double_type,
        layout.pointer,
        layout.va_list,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(types); index += 1)
    {
        TargetTypeLayout type = types[index];
        if (!type.size || !type.bit_width || type.bit_width > UINT32_MAX - 7 || (type.bit_width + 7) / 8 > type.size ||
            !target_layout_alignment_valid(type.alignment) || type.alignment > type.size * 2)
        {
            return false;
        }
    }
    return layout.endianness < TARGET_ENDIAN_COUNT && layout.pointer.size == 8 && layout.pointer.alignment == 8 && layout.atomic_min_width &&
           layout.atomic_min_width <= layout.atomic_max_width && layout.atomic_max_width <= 128 && target_layout_alignment_valid(layout.atomic_alignment) &&
           target_layout_alignment_valid(layout.abi_stack_alignment) && target_layout_alignment_valid(layout.abi_max_alignment) &&
           layout.abi_stack_alignment <= layout.abi_max_alignment;
}

bool cpu_is_native(CpuModel model)
{
    return (model == CPU_MODEL_NATIVE) | (model == target_native.cpu_model);
}

BUSTER_GLOBAL_LOCAL bool target_component_equal(String8 component, String8 expected)
{
    return string_equal(component, expected);
}

BUSTER_GLOBAL_LOCAL bool target_component_starts_with(String8 component, String8 expected)
{
    return string_starts_with_sequence(component, expected);
}

BUSTER_GLOBAL_LOCAL bool target_parse_version_suffix(String8 component, u64 prefix_length, Target* target)
{
    if (component.length == prefix_length)
    {
        return true;
    }
    if (component.length < prefix_length || !target)
    {
        return false;
    }
    u32 components[3] = {0};
    u32 component_count = 0;
    u64 index = prefix_length;
    while (index < component.length)
    {
        if (component_count >= BUSTER_ARRAY_LENGTH(components) || component.pointer[index] < '0' || component.pointer[index] > '9')
        {
            return false;
        }
        u32 value = 0;
        while (index < component.length && component.pointer[index] >= '0' && component.pointer[index] <= '9')
        {
            u32 digit = (u32)(component.pointer[index] - '0');
            if (value > (UINT32_MAX - digit) / 10)
            {
                return false;
            }
            value = value * 10 + digit;
            index += 1;
        }
        components[component_count++] = value;
        if (index == component.length)
        {
            break;
        }
        if (component.pointer[index] != '.')
        {
            return false;
        }
        index += 1;
        if (index == component.length)
        {
            return false;
        }
    }
    if (!component_count || components[0] > UINT16_MAX || (component_count > 1 && components[1] > UINT8_MAX) ||
        (component_count > 2 && components[2] > UINT8_MAX))
    {
        return false;
    }
    target->os_version_major = (u16)components[0];
    target->os_version_minor = component_count > 1 ? (u8)components[1] : 0;
    target->os_version_patch = component_count > 2 ? (u8)components[2] : 0;
    return true;
}

TargetParseResult target_parse_triple(String8 triple)
{
    TargetParseResult result = {
        .target =
            {
                .cpu_arch = CPU_ARCH_COUNT,
                .cpu_model = CPU_MODEL_BASELINE,
                .os = OPERATING_SYSTEM_COUNT,
            },
    };
    if (!triple.length)
    {
        result.error = TARGET_PARSE_ERROR_EMPTY;
        return result;
    }
    if (string_equal(triple, S8("native")))
    {
        result.target = target_native;
        return result;
    }

    u64 component_start = 0;
    u32 component_index = 0;
    while (component_start < triple.length)
    {
        u64 component_end = component_start;
        while (component_end < triple.length && triple.pointer[component_end] != '-')
        {
            component_end += 1;
        }
        String8 component = {
            .pointer = triple.pointer + component_start,
            .length = component_end - component_start,
        };
        if (component_index == 0)
        {
            if (target_component_equal(component, S8("x86_64")) || target_component_equal(component, S8("amd64")))
            {
                result.target.cpu_arch = CPU_ARCH_X86_64;
            }
            else if (target_component_equal(component, S8("aarch64")) || target_component_equal(component, S8("arm64")))
            {
                result.target.cpu_arch = CPU_ARCH_AARCH64;
            }
            else
            {
                result.invalid_component = component;
                result.error = TARGET_PARSE_ERROR_ARCHITECTURE;
                return result;
            }
        }
        else if (target_component_equal(component, S8("android")) || target_component_starts_with(component, S8("android")))
        {
            if (!target_parse_version_suffix(component, S8("android").length, &result.target))
            {
                result.invalid_component = component;
                result.error = TARGET_PARSE_ERROR_OPERATING_SYSTEM;
                return result;
            }
            result.target.os = OPERATING_SYSTEM_ANDROID;
        }
        else if (target_component_equal(component, S8("ios")) || target_component_starts_with(component, S8("ios")))
        {
            if (!target_parse_version_suffix(component, S8("ios").length, &result.target))
            {
                result.invalid_component = component;
                result.error = TARGET_PARSE_ERROR_OPERATING_SYSTEM;
                return result;
            }
            result.target.os = OPERATING_SYSTEM_IOS;
        }
        else if (target_component_equal(component, S8("darwin")) || target_component_starts_with(component, S8("macos")))
        {
            if (target_component_starts_with(component, S8("macos")) && !target_parse_version_suffix(component, S8("macos").length, &result.target))
            {
                result.invalid_component = component;
                result.error = TARGET_PARSE_ERROR_OPERATING_SYSTEM;
                return result;
            }
            if (result.target.os != OPERATING_SYSTEM_IOS)
            {
                result.target.os = OPERATING_SYSTEM_MACOS;
            }
        }
        else if (target_component_equal(component, S8("linux")))
        {
            if (result.target.os != OPERATING_SYSTEM_ANDROID)
            {
                result.target.os = OPERATING_SYSTEM_LINUX;
            }
        }
        else if (target_component_equal(component, S8("windows")) || target_component_equal(component, S8("win32")) ||
                 target_component_equal(component, S8("mingw32")) || target_component_equal(component, S8("msvc")))
        {
            result.target.os = OPERATING_SYSTEM_WINDOWS;
        }
        else if (target_component_equal(component, S8("uefi")))
        {
            result.target.os = OPERATING_SYSTEM_UEFI;
        }
        else if (target_component_equal(component, S8("freestanding")) || target_component_equal(component, S8("elf")))
        {
            result.target.os = OPERATING_SYSTEM_FREESTANDING;
        }

        component_index += 1;
        component_start = component_end < triple.length ? component_end + 1 : triple.length;
    }
    if (result.target.os == OPERATING_SYSTEM_COUNT)
    {
        result.invalid_component = triple;
        result.error = TARGET_PARSE_ERROR_OPERATING_SYSTEM;
    }
    return result;
}

BUSTER_TEST_F_DECL CpuModel cpu_model_resolve_detected(CpuModel model)
{
    return model == CPU_MODEL_ERROR ? CPU_MODEL_NATIVE : model;
}

CpuModel cpu_detect_model(void)
{
    CpuModel cpu_model = CPU_MODEL_ERROR;
#if BUSTER_CPU_ARCH_X86_64
    cpu_model = cpu_detect_model_x86_64();
    target_native.cpu_features = cpu_detect_features_x86_64();
#elif BUSTER_CPU_ARCH_AARCH64
    cpu_model = cpu_detect_model_aarch64();
    target_native.cpu_features = TARGET_CPU_FEATURE_AARCH64_NEON;
#else
#error TODO: implement CPU detection code for this architecture
#endif
    cpu_model = cpu_model_resolve_detected(cpu_model);
    target_native.cpu_model = cpu_model;
    target_native.cpu_features_explicit = true;
    return cpu_model;
}

CpuModel cpu_model_from_string(String8 string)
{
    for (CpuModel model = CPU_MODEL_BASELINE; model < CPU_MODEL_COUNT; model += 1)
    {
        if (string_equal(string, cpu_model_to_string_os(model)))
        {
            return model;
        }
    }
    return CPU_MODEL_ERROR;
}

bool cpu_model_supports_arch(CpuModel model, CpuArch arch)
{
    if (model == CPU_MODEL_BASELINE)
    {
        return arch == CPU_ARCH_X86_64 || arch == CPU_ARCH_AARCH64;
    }
    if (model == CPU_MODEL_NATIVE)
    {
        return arch == target_native.cpu_arch;
    }
    if (arch == CPU_ARCH_X86_64)
    {
        return model >= CPU_MODEL_AMD_I486 && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    }
    if (arch == CPU_ARCH_AARCH64)
    {
        return model == CPU_MODEL_A64_GENERIC || model == CPU_MODEL_A64_ARM_CORTEX_R82 || model == CPU_MODEL_A64_ARM_CORTEX_R82AE ||
               (model >= CPU_MODEL_A64_ARM_CORTEX_A34 && model <= CPU_MODEL_A64_ARM_NEOVERSE_V3AE) ||
               (model >= CPU_MODEL_A64_APPLE_A7 && model <= CPU_MODEL_A64_APPLE_M4);
    }
    return false;
}

TargetCpuFeatures target_cpu_features_default(CpuArch arch, CpuModel model)
{
    if (arch == CPU_ARCH_AARCH64)
    {
        return TARGET_CPU_FEATURE_AARCH64_NEON;
    }
    if (arch != CPU_ARCH_X86_64)
    {
        return 0;
    }
    TargetCpuFeatures result = TARGET_CPU_FEATURE_X86_SSE2;
    if (model >= CPU_MODEL_AMD_K8_SSE3 && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS)
    {
        result |= TARGET_CPU_FEATURE_X86_SSE3;
    }
    bool amd_family_10_or_newer = model >= CPU_MODEL_AMD_AMD_FAMILY_10 && model <= CPU_MODEL_AMD_ZEN_5;
    bool amd_bmi1 = model == CPU_MODEL_AMD_BT_2 || (model >= CPU_MODEL_AMD_BD_2 && model <= CPU_MODEL_AMD_ZEN_5);
    bool intel_popcnt = (model >= CPU_MODEL_INTEL_NEHALEM && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                        (model >= CPU_MODEL_INTEL_SILVERMONT && model <= CPU_MODEL_INTEL_TREMONT) ||
                        (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    bool intel_lzcnt = (model >= CPU_MODEL_INTEL_HASWELL && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                       (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    bool intel_bmi1 = intel_lzcnt;
    bool intel_cx16 = model >= CPU_MODEL_INTEL_CORE_2 && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    if (amd_family_10_or_newer || intel_popcnt)
    {
        result |= TARGET_CPU_FEATURE_X86_POPCNT;
    }
    if (amd_family_10_or_newer || intel_lzcnt)
    {
        result |= TARGET_CPU_FEATURE_X86_LZCNT;
    }
    if (amd_bmi1 || intel_bmi1)
    {
        result |= TARGET_CPU_FEATURE_X86_BMI1;
    }
    if (amd_family_10_or_newer || intel_cx16)
    {
        result |= TARGET_CPU_FEATURE_X86_CX16;
    }
    TargetCpuFeatures avx2_haswell = TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_PCLMUL;
    TargetCpuFeatures avx2_skylake = avx2_haswell | TARGET_CPU_FEATURE_X86_AES;
    TargetCpuFeatures avx2_alderlake = avx2_skylake | TARGET_CPU_FEATURE_X86_GFNI | TARGET_CPU_FEATURE_X86_VAES |
                                       TARGET_CPU_FEATURE_X86_VPCLMULQDQ | TARGET_CPU_FEATURE_X86_AVX_VNNI;
    TargetCpuFeatures avx2_arrowlake = avx2_alderlake | TARGET_CPU_FEATURE_X86_AVX_IFMA | TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT |
                                       TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8;
    TargetCpuFeatures avx2_arrowlake_s = avx2_arrowlake | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16;
    TargetCpuFeatures amd_btver2 = TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_PCLMUL;
    TargetCpuFeatures amd_bdver1 = amd_btver2;
    TargetCpuFeatures amd_bdver4 = amd_bdver1 | TARGET_CPU_FEATURE_X86_AVX2;
    TargetCpuFeatures amd_zen1 = avx2_haswell | TARGET_CPU_FEATURE_X86_AES;
    TargetCpuFeatures amd_zen3 = amd_zen1 | TARGET_CPU_FEATURE_X86_VAES | TARGET_CPU_FEATURE_X86_VPCLMULQDQ;
    TargetCpuFeatures avx512_skylake = avx2_skylake | TARGET_CPU_FEATURE_X86_AVX512F |
                                       TARGET_CPU_FEATURE_X86_AVX512VL | TARGET_CPU_FEATURE_X86_AVX512BW | TARGET_CPU_FEATURE_X86_AVX512CD |
                                       TARGET_CPU_FEATURE_X86_AVX512DQ;
    TargetCpuFeatures avx512_cannonlake = avx512_skylake | TARGET_CPU_FEATURE_X86_AVX512IFMA | TARGET_CPU_FEATURE_X86_AVX512VBMI;
    TargetCpuFeatures avx512_ice_lake = avx512_cannonlake | TARGET_CPU_FEATURE_X86_AVX512VNNI | TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ |
                                        TARGET_CPU_FEATURE_X86_AVX512VBMI2 | TARGET_CPU_FEATURE_X86_GFNI | TARGET_CPU_FEATURE_X86_VAES |
                                        TARGET_CPU_FEATURE_X86_VPCLMULQDQ | TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_PCLMUL |
                                        TARGET_CPU_FEATURE_X86_AVX512BITALG;
    TargetCpuFeatures sapphire_rapids = avx512_ice_lake | TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512FP16 |
                                        TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                                        TARGET_CPU_FEATURE_X86_AVX_VNNI;
    TargetCpuFeatures granite_rapids = sapphire_rapids | TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_512 |
                                       TARGET_CPU_FEATURE_X86_AMX_FP16;
    TargetCpuFeatures granite_rapids_d = granite_rapids | TARGET_CPU_FEATURE_X86_AMX_COMPLEX;
    switch (model)
    {
    case CPU_MODEL_AMD_ZEN_1:
    case CPU_MODEL_AMD_ZEN_2:
        result |= amd_zen1;
        break;
    case CPU_MODEL_AMD_ZEN_3:
        result |= amd_zen3;
        break;
    case CPU_MODEL_AMD_BT_2:
        result |= amd_btver2;
        break;
    case CPU_MODEL_AMD_BD_1:
    case CPU_MODEL_AMD_BD_2:
    case CPU_MODEL_AMD_BD_3:
        result |= amd_bdver1;
        break;
    case CPU_MODEL_AMD_BD_4:
        result |= amd_bdver4;
        break;
    case CPU_MODEL_INTEL_HASWELL:
    case CPU_MODEL_INTEL_BROADWELL:
        result |= avx2_haswell;
        break;
    case CPU_MODEL_INTEL_SKYLAKE:
        result |= avx2_skylake;
        break;
    case CPU_MODEL_INTEL_ALDERLAKE:
    case CPU_MODEL_INTEL_RAPTORLAKE:
    case CPU_MODEL_INTEL_METEORLAKE:
    case CPU_MODEL_INTEL_GRACEMONT:
        result |= avx2_alderlake;
        break;
    case CPU_MODEL_INTEL_ARROWLAKE:
        result |= avx2_arrowlake;
        break;
    case CPU_MODEL_INTEL_ARROWLAKE_S:
    case CPU_MODEL_INTEL_LUNARLAKE:
    case CPU_MODEL_INTEL_PANTHERLAKE:
        result |= avx2_arrowlake_s;
        break;
    case CPU_MODEL_AMD_ZEN_4:
        result |= avx512_ice_lake | TARGET_CPU_FEATURE_X86_AVX512BF16;
        break;
    case CPU_MODEL_AMD_ZEN_5:
        result |= avx512_ice_lake | TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX_VNNI |
                  TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT;
        break;
    case CPU_MODEL_INTEL_SKYLAKE_AVX512:
        result |= avx512_skylake;
        break;
    case CPU_MODEL_INTEL_ROCKETLAKE:
        result |= avx512_ice_lake;
        break;
    case CPU_MODEL_INTEL_CANNONLAKE:
        result |= avx512_cannonlake;
        break;
    case CPU_MODEL_INTEL_CASCADELAKE:
        result |= avx512_cannonlake | TARGET_CPU_FEATURE_X86_AVX512VNNI;
        break;
    case CPU_MODEL_INTEL_COOPERLAKE:
        result |= avx512_cannonlake | TARGET_CPU_FEATURE_X86_AVX512VNNI | TARGET_CPU_FEATURE_X86_AVX512BF16;
        break;
    case CPU_MODEL_INTEL_ICELAKE_CLIENT:
    case CPU_MODEL_INTEL_ICELAKE_SERVER:
        result |= avx512_ice_lake;
        break;
    case CPU_MODEL_INTEL_TIGERLAKE:
        result |= avx512_ice_lake | TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT;
        break;
    case CPU_MODEL_INTEL_EMERALD_RAPIDS:
        result |= sapphire_rapids;
        break;
    case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS:
        result |= sapphire_rapids;
        break;
    case CPU_MODEL_INTEL_KNL:
        result |= TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX512CD |
                  TARGET_CPU_FEATURE_X86_AVX512PF | TARGET_CPU_FEATURE_X86_AVX512ER | TARGET_CPU_FEATURE_X86_AES |
                  TARGET_CPU_FEATURE_X86_PCLMUL;
        break;
    case CPU_MODEL_INTEL_KNM:
        result |= TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX512CD |
                  TARGET_CPU_FEATURE_X86_AVX512PF | TARGET_CPU_FEATURE_X86_AVX512ER | TARGET_CPU_FEATURE_X86_AVX5124VNNIW |
                  TARGET_CPU_FEATURE_X86_AVX5124FMAPS | TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ | TARGET_CPU_FEATURE_X86_AES |
                  TARGET_CPU_FEATURE_X86_PCLMUL;
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS:
        result |= granite_rapids;
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS_D:
        result |= granite_rapids_d;
        break;
    case CPU_MODEL_INTEL_DIAMOND_RAPIDS:
        result |= granite_rapids_d | TARGET_CPU_FEATURE_X86_AVX10_2 | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX |
                  TARGET_CPU_FEATURE_X86_MOVRS | TARGET_CPU_FEATURE_X86_APX |
                  TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF |
                  TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8 | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16 | TARGET_CPU_FEATURE_X86_AVX_IFMA |
                  TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT | TARGET_CPU_FEATURE_X86_AMX_FP8 |
                  TARGET_CPU_FEATURE_X86_AMX_AVX512 |
                  TARGET_CPU_FEATURE_X86_AMX_MOVRS;
        break;
    case CPU_MODEL_INTEL_WESTMERE:
    case CPU_MODEL_INTEL_SILVERMONT:
        result |= TARGET_CPU_FEATURE_X86_PCLMUL;
        break;
    case CPU_MODEL_INTEL_GOLDMONT:
    case CPU_MODEL_INTEL_GOLDMONT_PLUS:
        result |= TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_PCLMUL;
        break;
    case CPU_MODEL_INTEL_TREMONT:
        result |= TARGET_CPU_FEATURE_X86_AES | TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_GFNI;
        break;
    case CPU_MODEL_INTEL_SIERRAFOREST:
    case CPU_MODEL_INTEL_GRANDRIDGE:
        result |= avx2_arrowlake;
        break;
    case CPU_MODEL_INTEL_CLEARWATERFOREST:
        result |= avx2_arrowlake_s;
        break;
    case CPU_MODEL_INTEL_SANDY_BRIDGE:
    case CPU_MODEL_INTEL_IVY_BRIDGE:
        result |= TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_PCLMUL;
        break;
    default:
        break;
    }
    return result;
}

bool target_cpu_features_are_valid(Target target)
{
    if (!cpu_model_supports_arch(target.cpu_model, target.cpu_arch))
    {
        return false;
    }
    if (!target.cpu_features_explicit)
    {
        return true;
    }
    TargetCpuFeatures features = target.cpu_features;
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return !(features & ~((TargetCpuFeatures)TARGET_CPU_FEATURE_AARCH64_NEON));
    }
    if (target.cpu_arch != CPU_ARCH_X86_64)
    {
        return false;
    }
    TargetCpuFeatures known = TARGET_CPU_FEATURE_X86_SSE2 | TARGET_CPU_FEATURE_X86_AVX | TARGET_CPU_FEATURE_X86_AVX2 | TARGET_CPU_FEATURE_X86_AVX512F |
                              TARGET_CPU_FEATURE_X86_AVX512VL | TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_2 |
                              TARGET_CPU_FEATURE_X86_AVX10_512 | TARGET_CPU_FEATURE_X86_APX | TARGET_CPU_FEATURE_X86_AVX512BW |
                              TARGET_CPU_FEATURE_X86_SSE3 | TARGET_CPU_FEATURE_X86_POPCNT | TARGET_CPU_FEATURE_X86_LZCNT |
                              TARGET_CPU_FEATURE_X86_BMI1 | TARGET_CPU_FEATURE_X86_CX16 | TARGET_CPU_FEATURE_X86_AVX512CD |
                              TARGET_CPU_FEATURE_X86_AVX512DQ | TARGET_CPU_FEATURE_X86_AVX512IFMA | TARGET_CPU_FEATURE_X86_AVX512PF |
                              TARGET_CPU_FEATURE_X86_AVX512ER | TARGET_CPU_FEATURE_X86_AVX512VBMI | TARGET_CPU_FEATURE_X86_AVX512VBMI2 |
                              TARGET_CPU_FEATURE_X86_AVX512VNNI | TARGET_CPU_FEATURE_X86_AVX512BITALG | TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ |
                              TARGET_CPU_FEATURE_X86_AVX5124VNNIW | TARGET_CPU_FEATURE_X86_AVX5124FMAPS | TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT |
                              TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512FP16 | TARGET_CPU_FEATURE_X86_GFNI |
                              TARGET_CPU_FEATURE_X86_VAES | TARGET_CPU_FEATURE_X86_VPCLMULQDQ | TARGET_CPU_FEATURE_X86_AES |
                              TARGET_CPU_FEATURE_X86_PCLMUL | TARGET_CPU_FEATURE_X86_AVX10_V1_AUX |
                              TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF |
                              TARGET_CPU_FEATURE_X86_AMX_TILE | TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 |
                              TARGET_CPU_FEATURE_X86_AMX_FP16 | TARGET_CPU_FEATURE_X86_AMX_COMPLEX | TARGET_CPU_FEATURE_X86_AMX_FP8 |
                              TARGET_CPU_FEATURE_X86_AMX_AVX512 | TARGET_CPU_FEATURE_X86_AMX_MOVRS |
                              TARGET_CPU_FEATURE_X86_AVX_VNNI | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8 | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16 |
                              TARGET_CPU_FEATURE_X86_AVX_IFMA | TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT | TARGET_CPU_FEATURE_X86_MOVRS;
    if ((features & ~known) || !(features & TARGET_CPU_FEATURE_X86_SSE2))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX2) && !(features & TARGET_CPU_FEATURE_X86_AVX))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_SSE3) && !(features & TARGET_CPU_FEATURE_X86_SSE2))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX512F) &&
        (!(features & TARGET_CPU_FEATURE_X86_AVX) || !(features & TARGET_CPU_FEATURE_X86_AVX2)))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX512VL) && !(features & TARGET_CPU_FEATURE_X86_AVX512F))
    {
        if (!(features & TARGET_CPU_FEATURE_X86_AVX10_1))
        {
            return false;
        }
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX512BW) && !(features & (TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_512)))
    {
        if (!(features & TARGET_CPU_FEATURE_X86_AVX10_1))
        {
            return false;
        }
    }
    TargetCpuFeatures avx512_subfeatures = TARGET_CPU_FEATURE_X86_AVX512CD | TARGET_CPU_FEATURE_X86_AVX512DQ | TARGET_CPU_FEATURE_X86_AVX512IFMA |
                                           TARGET_CPU_FEATURE_X86_AVX512PF | TARGET_CPU_FEATURE_X86_AVX512ER | TARGET_CPU_FEATURE_X86_AVX512VBMI |
                                           TARGET_CPU_FEATURE_X86_AVX512VBMI2 | TARGET_CPU_FEATURE_X86_AVX512VNNI | TARGET_CPU_FEATURE_X86_AVX512BITALG |
                                           TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ | TARGET_CPU_FEATURE_X86_AVX5124VNNIW |
                                           TARGET_CPU_FEATURE_X86_AVX5124FMAPS | TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT |
                                           TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512FP16;
    if ((features & avx512_subfeatures) && !(features & (TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_1)))
    {
        return false;
    }
    TargetCpuFeatures avx512_bw_subfeatures = TARGET_CPU_FEATURE_X86_AVX512VBMI | TARGET_CPU_FEATURE_X86_AVX512VBMI2 |
                                               TARGET_CPU_FEATURE_X86_AVX512BF16 | TARGET_CPU_FEATURE_X86_AVX512BITALG |
                                               TARGET_CPU_FEATURE_X86_AVX512FP16;
    if ((features & avx512_bw_subfeatures) && !(features & TARGET_CPU_FEATURE_X86_AVX512BW))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_GFNI) && !(features & TARGET_CPU_FEATURE_X86_SSE2))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_VAES) &&
        (!(features & TARGET_CPU_FEATURE_X86_AVX2) || !(features & TARGET_CPU_FEATURE_X86_AES)))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_VPCLMULQDQ) &&
        (!(features & TARGET_CPU_FEATURE_X86_AVX) || !(features & TARGET_CPU_FEATURE_X86_PCLMUL)))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX10_1) &&
        (!(features & TARGET_CPU_FEATURE_X86_AVX2) || !(features & TARGET_CPU_FEATURE_X86_AVX512F) ||
         !(features & TARGET_CPU_FEATURE_X86_AVX10_512)))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX10_2) && !(features & TARGET_CPU_FEATURE_X86_AVX10_1))
    {
        return false;
    }
    // Native decoding conservatively under-reports malformed hardware pairs;
    // explicit synthetic targets must obey the same AVX10 co-enumeration rule.
    if (!!(features & TARGET_CPU_FEATURE_X86_AVX10_2) != !!(features & TARGET_CPU_FEATURE_X86_AVX10_V1_AUX))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AVX10_512) && !(features & TARGET_CPU_FEATURE_X86_AVX10_1))
    {
        return false;
    }
    // APX-F and APX_NCI_NDD_NF are likewise a co-enumerated hardware pair.
    if (!!(features & TARGET_CPU_FEATURE_X86_APX) != !!(features & TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF))
    {
        return false;
    }
    TargetCpuFeatures avx2_dependent_features = TARGET_CPU_FEATURE_X86_AVX_VNNI | TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8 |
                                                 TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16 | TARGET_CPU_FEATURE_X86_AVX_IFMA |
                                                 TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT;
    if ((features & avx2_dependent_features) && !(features & TARGET_CPU_FEATURE_X86_AVX2))
    {
        return false;
    }
    TargetCpuFeatures amx_subfeatures = TARGET_CPU_FEATURE_X86_AMX_INT8 | TARGET_CPU_FEATURE_X86_AMX_BF16 | TARGET_CPU_FEATURE_X86_AMX_FP16 |
                                        TARGET_CPU_FEATURE_X86_AMX_COMPLEX | TARGET_CPU_FEATURE_X86_AMX_FP8 |
                                        TARGET_CPU_FEATURE_X86_AMX_AVX512 |
                                        TARGET_CPU_FEATURE_X86_AMX_MOVRS;
    if ((features & amx_subfeatures) && !(features & TARGET_CPU_FEATURE_X86_AMX_TILE))
    {
        return false;
    }
    if ((features & TARGET_CPU_FEATURE_X86_AMX_AVX512) && !(features & (TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_1)))
    {
        return false;
    }
    return true;
}

TargetCpuFeatures target_cpu_features_effective(Target target)
{
    if (target.cpu_features_explicit)
    {
        return target.cpu_features;
    }
    if (target.cpu_model == CPU_MODEL_NATIVE && target.cpu_arch == target_native.cpu_arch && target_native.cpu_features_explicit)
    {
        return target_native.cpu_features;
    }
    return target_cpu_features_default(target.cpu_arch, target.cpu_model);
}

bool target_cpu_feature_has(Target target, TargetCpuFeature feature)
{
    return (target_cpu_features_effective(target) & (TargetCpuFeatures)feature) != 0;
}

typedef struct TargetCpuFeatureName TargetCpuFeatureName;
struct TargetCpuFeatureName
{
    String8 name;
    TargetCpuFeature feature;
    CpuArch arch;
};

// Kept in bytewise name order so verbose output is stable without sorting or
// allocating one node per feature.
BUSTER_GLOBAL_LOCAL TargetCpuFeatureName const target_cpu_feature_names[] = {
    {.name = S8_INITIALIZER("aes"), .feature = TARGET_CPU_FEATURE_X86_AES, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-avx512"), .feature = TARGET_CPU_FEATURE_X86_AMX_AVX512, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-bf16"), .feature = TARGET_CPU_FEATURE_X86_AMX_BF16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-complex"), .feature = TARGET_CPU_FEATURE_X86_AMX_COMPLEX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-fp16"), .feature = TARGET_CPU_FEATURE_X86_AMX_FP16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-fp8"), .feature = TARGET_CPU_FEATURE_X86_AMX_FP8, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-int8"), .feature = TARGET_CPU_FEATURE_X86_AMX_INT8, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-movrs"), .feature = TARGET_CPU_FEATURE_X86_AMX_MOVRS, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("amx-tile"), .feature = TARGET_CPU_FEATURE_X86_AMX_TILE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("apx"), .feature = TARGET_CPU_FEATURE_X86_APX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("apx-nci-ndd-nf"), .feature = TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx"), .feature = TARGET_CPU_FEATURE_X86_AVX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx-ifma"), .feature = TARGET_CPU_FEATURE_X86_AVX_IFMA, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx-ne-convert"), .feature = TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx-vnni"), .feature = TARGET_CPU_FEATURE_X86_AVX_VNNI, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx-vnni-int16"), .feature = TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx-vnni-int8"), .feature = TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx10-512"), .feature = TARGET_CPU_FEATURE_X86_AVX10_512, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx10-v1-aux"), .feature = TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx10.1"), .feature = TARGET_CPU_FEATURE_X86_AVX10_1, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx10.2"), .feature = TARGET_CPU_FEATURE_X86_AVX10_2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx2"), .feature = TARGET_CPU_FEATURE_X86_AVX2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx5124fmaps"), .feature = TARGET_CPU_FEATURE_X86_AVX5124FMAPS, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx5124vnniw"), .feature = TARGET_CPU_FEATURE_X86_AVX5124VNNIW, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512bf16"), .feature = TARGET_CPU_FEATURE_X86_AVX512BF16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512bitalg"), .feature = TARGET_CPU_FEATURE_X86_AVX512BITALG, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512bw"), .feature = TARGET_CPU_FEATURE_X86_AVX512BW, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512cd"), .feature = TARGET_CPU_FEATURE_X86_AVX512CD, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512dq"), .feature = TARGET_CPU_FEATURE_X86_AVX512DQ, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512er"), .feature = TARGET_CPU_FEATURE_X86_AVX512ER, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512f"), .feature = TARGET_CPU_FEATURE_X86_AVX512F, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512fp16"), .feature = TARGET_CPU_FEATURE_X86_AVX512FP16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512ifma"), .feature = TARGET_CPU_FEATURE_X86_AVX512IFMA, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512pf"), .feature = TARGET_CPU_FEATURE_X86_AVX512PF, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512vbmi"), .feature = TARGET_CPU_FEATURE_X86_AVX512VBMI, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512vbmi2"), .feature = TARGET_CPU_FEATURE_X86_AVX512VBMI2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512vl"), .feature = TARGET_CPU_FEATURE_X86_AVX512VL, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512vnni"), .feature = TARGET_CPU_FEATURE_X86_AVX512VNNI, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512vp2intersect"), .feature = TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("avx512vpopcntdq"), .feature = TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("bmi1"), .feature = TARGET_CPU_FEATURE_X86_BMI1, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("cx16"), .feature = TARGET_CPU_FEATURE_X86_CX16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("gfni"), .feature = TARGET_CPU_FEATURE_X86_GFNI, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("lzcnt"), .feature = TARGET_CPU_FEATURE_X86_LZCNT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("movrs"), .feature = TARGET_CPU_FEATURE_X86_MOVRS, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("neon"), .feature = TARGET_CPU_FEATURE_AARCH64_NEON, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("pclmul"), .feature = TARGET_CPU_FEATURE_X86_PCLMUL, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("popcnt"), .feature = TARGET_CPU_FEATURE_X86_POPCNT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sse2"), .feature = TARGET_CPU_FEATURE_X86_SSE2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sse3"), .feature = TARGET_CPU_FEATURE_X86_SSE3, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("vaes"), .feature = TARGET_CPU_FEATURE_X86_VAES, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("vpclmulqdq"), .feature = TARGET_CPU_FEATURE_X86_VPCLMULQDQ, .arch = CPU_ARCH_X86_64},
};

TargetCpuFeature target_cpu_feature_from_string(CpuArch arch, String8 name)
{
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(target_cpu_feature_names); index += 1)
    {
        TargetCpuFeatureName entry = target_cpu_feature_names[index];
        if (entry.arch == arch && string_equal(entry.name, name))
        {
            return entry.feature;
        }
    }
    return TARGET_CPU_FEATURE_NONE;
}

String8 target_cpu_feature_to_string(TargetCpuFeature feature)
{
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(target_cpu_feature_names); index += 1)
    {
        if (target_cpu_feature_names[index].feature == feature)
        {
            return target_cpu_feature_names[index].name;
        }
    }
    return S8("");
}

String8 target_cpu_features_to_string(Arena* arena, Target target)
{
    TargetCpuFeatures features = target_cpu_features_effective(target);
    String8 parts[BUSTER_ARRAY_LENGTH(target_cpu_feature_names) * 2] = {0};
    u32 part_count = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(target_cpu_feature_names); index += 1)
    {
        TargetCpuFeatureName entry = target_cpu_feature_names[index];
        if (entry.arch == target.cpu_arch && (features & (TargetCpuFeatures)entry.feature))
        {
            if (part_count)
            {
                parts[part_count++] = S8(",");
            }
            parts[part_count++] = entry.name;
        }
    }
    if (!part_count)
    {
        return S8("none");
    }
    return string_join_arena(arena, (SliceString8){.pointer = parts, .length = part_count}, false);
}

u32 target_vector_register_size(Target target)
{
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_NEON) ? 16 : 0;
    }
    if (target.cpu_arch != CPU_ARCH_X86_64)
    {
        return 0;
    }
    TargetCpuFeatures features = target_cpu_features_effective(target);
    if (features & (TARGET_CPU_FEATURE_X86_AVX512F | TARGET_CPU_FEATURE_X86_AVX10_1 | TARGET_CPU_FEATURE_X86_AVX10_2 |
                    TARGET_CPU_FEATURE_X86_AVX10_512))
    {
        return 64;
    }
    if (features & TARGET_CPU_FEATURE_X86_AVX)
    {
        return 32;
    }
    return features & TARGET_CPU_FEATURE_X86_SSE2 ? 16 : 0;
}

TargetStringSplit target_to_split_string_os(Target target)
{
    String8 arch_string = cpu_arch_to_string_os(target.cpu_arch);
    String8 string_os = operating_system_to_string_os(target.os);
    String8 model_string = cpu_model_to_string_os(target.cpu_model);
    TargetStringSplit result = {
        .s =
            {
                [TARGET_STRING_COMPONENT_CPU_ARCH] = arch_string,
                [TARGET_STRING_COMPONENT_OPERATING_SYSTEM] = string_os,
                [TARGET_STRING_COMPONENT_CPU_MODEL] = model_string,
            },
    };

    return result;
}

String8 cpu_arch_to_string_os(CpuArch arch)
{
    switch (arch)
    {
        break;
    case CPU_ARCH_X86_64:
        return S8("x86_64");
        break;
    case CPU_ARCH_AARCH64:
        return S8("aarch64");
        break;
    default:
        return S8("");
    }
}

String8 operating_system_to_string_os(OperatingSystem os)
{
    switch (os)
    {
        break;
    case OPERATING_SYSTEM_LINUX:
        return S8("linux");
        break;
    case OPERATING_SYSTEM_MACOS:
        return S8("macos");
        break;
    case OPERATING_SYSTEM_WINDOWS:
        return S8("windows");
        break;
    case OPERATING_SYSTEM_UEFI:
        return S8("uefi");
        break;
    case OPERATING_SYSTEM_ANDROID:
        return S8("android");
        break;
    case OPERATING_SYSTEM_IOS:
        return S8("ios");
        break;
    case OPERATING_SYSTEM_FREESTANDING:
        return S8("freestanding");
        break;
    case OPERATING_SYSTEM_COUNT:
        BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
    return S8("error");
}

String8 cpu_model_to_string_os(CpuModel model)
{
    switch (model)
    {
        break;
    case CPU_MODEL_COUNT:
        BUSTER_UNREACHABLE();
        break;
    case CPU_MODEL_ERROR:
        return S8("error");
        break;
    case CPU_MODEL_BASELINE:
        return S8("baseline");
        break;
    case CPU_MODEL_NATIVE:
        return S8("native");
        break;
    case CPU_MODEL_AMD_I486:
        return S8("i486");
        break;
    case CPU_MODEL_AMD_PENTIUM:
        return S8("pentium");
        break;
    case CPU_MODEL_AMD_K6:
        return S8("k6");
        break;
    case CPU_MODEL_AMD_K6_2:
        return S8("k6-2");
        break;
    case CPU_MODEL_AMD_K6_3:
        return S8("k6-3");
        break;
    case CPU_MODEL_AMD_GEODE:
        return S8("geode");
        break;
    case CPU_MODEL_AMD_ATHLON:
        return S8("athlon");
        break;
    case CPU_MODEL_AMD_ATHLON_XP:
        return S8("athlon-xp");
        break;
    case CPU_MODEL_AMD_K8:
        return S8("k8");
        break;
    case CPU_MODEL_AMD_K8_SSE3:
        return S8("k8-sse3");
        break;
    case CPU_MODEL_AMD_AMD_FAMILY_10:
        return S8("amdfam10");
        break;
    case CPU_MODEL_AMD_BT_1:
        return S8("btver1");
        break;
    case CPU_MODEL_AMD_BT_2:
        return S8("btver2");
        break;
    case CPU_MODEL_AMD_BD_1:
        return S8("bdver1");
        break;
    case CPU_MODEL_AMD_BD_2:
        return S8("bdver2");
        break;
    case CPU_MODEL_AMD_BD_3:
        return S8("bdver3");
        break;
    case CPU_MODEL_AMD_BD_4:
        return S8("bdver4");
        break;
    case CPU_MODEL_AMD_ZEN_1:
        return S8("znver1");
        break;
    case CPU_MODEL_AMD_ZEN_2:
        return S8("znver2");
        break;
    case CPU_MODEL_AMD_ZEN_3:
        return S8("znver3");
        break;
    case CPU_MODEL_AMD_ZEN_4:
        return S8("znver4");
        break;
    case CPU_MODEL_AMD_ZEN_5:
        return S8("znver5");
        break;
    case CPU_MODEL_INTEL_CORE_2:
        return S8("core2");
        break;
    case CPU_MODEL_INTEL_PENRYN:
        return S8("penryn");
        break;
    case CPU_MODEL_INTEL_NEHALEM:
        return S8("nehalem");
        break;
    case CPU_MODEL_INTEL_WESTMERE:
        return S8("westmere");
        break;
    case CPU_MODEL_INTEL_SANDY_BRIDGE:
        return S8("sandybridge");
        break;
    case CPU_MODEL_INTEL_IVY_BRIDGE:
        return S8("ivybridge");
        break;
    case CPU_MODEL_INTEL_HASWELL:
        return S8("haswell");
        break;
    case CPU_MODEL_INTEL_BROADWELL:
        return S8("broadwell");
        break;
    case CPU_MODEL_INTEL_SKYLAKE:
        return S8("skylake");
        break;
    case CPU_MODEL_INTEL_SKYLAKE_AVX512:
        return S8("skylake-avx512");
        break;
    case CPU_MODEL_INTEL_ROCKETLAKE:
        return S8("rocketlake");
        break;
    case CPU_MODEL_INTEL_COOPERLAKE:
        return S8("cooperlake");
        break;
    case CPU_MODEL_INTEL_CASCADELAKE:
        return S8("cascadelake");
        break;
    case CPU_MODEL_INTEL_CANNONLAKE:
        return S8("cannonlake");
        break;
    case CPU_MODEL_INTEL_ICELAKE_CLIENT:
        return S8("icelake-client");
        break;
    case CPU_MODEL_INTEL_TIGERLAKE:
        return S8("tigerlake");
        break;
    case CPU_MODEL_INTEL_ALDERLAKE:
        return S8("alderlake");
        break;
    case CPU_MODEL_INTEL_RAPTORLAKE:
        return S8("raptorlake");
        break;
    case CPU_MODEL_INTEL_METEORLAKE:
        return S8("meteorlake");
        break;
    case CPU_MODEL_INTEL_GRACEMONT:
        return S8("gracemont");
        break;
    case CPU_MODEL_INTEL_ARROWLAKE:
        return S8("arrowlake");
        break;
    case CPU_MODEL_INTEL_ARROWLAKE_S:
        return S8("arrowlake-s");
        break;
    case CPU_MODEL_INTEL_LUNARLAKE:
        return S8("lunarlake");
        break;
    case CPU_MODEL_INTEL_PANTHERLAKE:
        return S8("pantherlake");
        break;
    case CPU_MODEL_INTEL_ICELAKE_SERVER:
        return S8("icelake-server");
        break;
    case CPU_MODEL_INTEL_EMERALD_RAPIDS:
        return S8("emeraldrapids");
        break;
    case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS:
        return S8("sapphirerapids");
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS:
        return S8("graniterapids");
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS_D:
        return S8("graniterapids-d");
        break;
    case CPU_MODEL_INTEL_BONNELL:
        return S8("bonnell");
        break;
    case CPU_MODEL_INTEL_SILVERMONT:
        return S8("silvermont");
        break;
    case CPU_MODEL_INTEL_GOLDMONT:
        return S8("goldmont");
        break;
    case CPU_MODEL_INTEL_GOLDMONT_PLUS:
        return S8("goldmont-plus");
        break;
    case CPU_MODEL_INTEL_TREMONT:
        return S8("tremont");
        break;
    case CPU_MODEL_INTEL_SIERRAFOREST:
        return S8("sierraforest");
        break;
    case CPU_MODEL_INTEL_GRANDRIDGE:
        return S8("grandridge");
        break;
    case CPU_MODEL_INTEL_CLEARWATERFOREST:
        return S8("clearwaterforest");
        break;
    case CPU_MODEL_INTEL_KNL:
        return S8("knl");
        break;
    case CPU_MODEL_INTEL_KNM:
        return S8("knm");
        break;
    case CPU_MODEL_INTEL_DIAMOND_RAPIDS:
        return S8("diamondrapids");
        break;
    case CPU_MODEL_A64_GENERIC:
        return S8("generic");
        break;
    case CPU_MODEL_A64_ARM_ARM926EJ_S:
        return S8("arm926ej-s");
        break;
    case CPU_MODEL_A64_ARM_MPCORE:
        return S8("mpcore");
        break;
    case CPU_MODEL_A64_ARM_ARM1136J_S:
        return S8("arm1136j-s");
        break;
    case CPU_MODEL_A64_ARM_ARM1156T2_S:
        return S8("arm1156t2-s");
        break;
    case CPU_MODEL_A64_ARM_ARM1176JZ_S:
        return S8("arm1176jz-s");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A5:
        return S8("cortex-a5");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A7:
        return S8("cortex-a7");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A8:
        return S8("cortex-a8");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A9:
        return S8("cortex-a9");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A15:
        return S8("cortex-a15");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A17:
        return S8("cortex-a17");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M0:
        return S8("cortex-m0");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M3:
        return S8("cortex-m3");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M4:
        return S8("cortex-m4");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M7:
        return S8("cortex-m7");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M23:
        return S8("cortex-m23");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M33:
        return S8("cortex-m33");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M52:
        return S8("cortex-m52");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M55:
        return S8("cortex-m55");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M85:
        return S8("cortex-m85");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R8:
        return S8("cortex-r8");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R52:
        return S8("cortex-r52");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R52PLUS:
        return S8("cortex-r52plus");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R82:
        return S8("cortex-r82");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R82AE:
        return S8("cortex-r82ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A34:
        return S8("cortex-a34");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A35:
        return S8("cortex-a35");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A320:
        return S8("cortex-a320");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A53:
        return S8("cortex-a53");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A55:
        return S8("cortex-a55");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A510:
        return S8("cortex-a510");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A520:
        return S8("cortex-a520");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A520AE:
        return S8("cortex-a520ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A57:
        return S8("cortex-a57");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A65:
        return S8("cortex-a65");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A65AE:
        return S8("cortex-a65ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A72:
        return S8("cortex-a72");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A73:
        return S8("cortex-a73");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A75:
        return S8("cortex-a75");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A76:
        return S8("cortex-a76");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A76AE:
        return S8("cortex-a76ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A77:
        return S8("cortex-a77");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A78:
        return S8("cortex-a78");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A78AE:
        return S8("cortex-a78ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A78C:
        return S8("cortex-a78c");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A710:
        return S8("cortex-a710");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A715:
        return S8("cortex-a715");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A720:
        return S8("cortex-a720");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A720AE:
        return S8("cortex-a720ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A725:
        return S8("cortex-a725");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X1:
        return S8("cortex-x1");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X1C:
        return S8("cortex-x1c");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X2:
        return S8("cortex-x2");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X3:
        return S8("cortex-x3");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X4:
        return S8("cortex-x4");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X925:
        return S8("cortex-x925");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_E1:
        return S8("neoverse-e1");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_N1:
        return S8("neoverse-n1");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_N2:
        return S8("neoverse-n2");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_N3:
        return S8("neoverse-n3");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V1:
        return S8("neoverse-v1");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V2:
        return S8("neoverse-v2");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V3:
        return S8("neoverse-v3");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V3AE:
        return S8("neoverse-v3ae");
        break;
    case CPU_MODEL_A64_ARM_ARM920T:
        return S8("arm920t");
        break;
    case CPU_MODEL_A64_ARM_XSCALE:
        return S8("xscale");
        break;
    case CPU_MODEL_A64_ARM_SWIFT:
        return S8("swift");
        break;
    case CPU_MODEL_A64_ARM920T:
        return S8("arm920t");
        break;
    case CPU_MODEL_A64_APPLE_A7:
        return S8("apple-a7");
        break;
    case CPU_MODEL_A64_APPLE_A8:
        return S8("apple-a8");
        break;
    case CPU_MODEL_A64_APPLE_A9:
        return S8("apple-a9");
        break;
    case CPU_MODEL_A64_APPLE_A10:
        return S8("apple-a10");
        break;
    case CPU_MODEL_A64_APPLE_A11:
        return S8("apple-a11");
        break;
    case CPU_MODEL_A64_APPLE_A12:
        return S8("apple-a12");
        break;
    case CPU_MODEL_A64_APPLE_A13:
        return S8("apple-a13");
        break;
    case CPU_MODEL_A64_APPLE_M1:
        return S8("apple-m1");
        break;
    case CPU_MODEL_A64_APPLE_M2:
        return S8("apple-m2");
        break;
    case CPU_MODEL_A64_APPLE_A17:
        return S8("apple-a17");
        break;
    case CPU_MODEL_A64_APPLE_M3:
        return S8("apple-m3");
        break;
    case CPU_MODEL_A64_APPLE_M4:
        return S8("apple-m4");
    }

    BUSTER_UNREACHABLE();
    return S8("error");
}
