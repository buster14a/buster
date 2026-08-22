// Target and CPU-model knowledge: the feature-set algebra
// (target_cpu_features_add/subset), host CPU detection dispatch (the
// per-architecture probes live in x86_64.c and aarch64.c), the canonical
// CPU model names and their feature tables that -march/-mcpu resolve
// through (cpu_model_from_string, cpu_model_to_string_os), model/arch
// compatibility checks, target triple splitting/printing, and per-target
// data layouts. New CPU models are added to the tables here, spelled the
// way cpu_model_to_string_os prints them.

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

BUSTER_GLOBAL_LOCAL bool target_cpu_feature_bit_index(TargetCpuFeature feature, u32* word_index, u32* bit_index)
{
    u32 feature_index = (u32)feature;
    bool result;
    if (!word_index || !bit_index || feature_index == (u32)TARGET_CPU_FEATURE_NONE || feature_index >= (u32)TARGET_CPU_FEATURE_COUNT)
    {
        result = false;
    }
    else
    {
        feature_index -= 1;
        *word_index = feature_index / 64;
        *bit_index = feature_index % 64;
        result = *word_index < TARGET_CPU_FEATURE_WORD_COUNT;
    }

    return result;
}

TargetCpuFeatures target_cpu_features_empty(void)
{
    return (TargetCpuFeatures){0};
}

TargetCpuFeatures target_cpu_features_singleton(TargetCpuFeature feature)
{
    TargetCpuFeatures result = target_cpu_features_empty();
    u32 word_index = 0;
    u32 bit_index = 0;
    if (target_cpu_feature_bit_index(feature, &word_index, &bit_index))
    {
        result.words[word_index] = UINT64_C(1) << bit_index;
    }
    return result;
}

TargetCpuFeatures target_cpu_features_add(TargetCpuFeatures features, TargetCpuFeature feature)
{
    u32 word_index = 0;
    u32 bit_index = 0;
    if (target_cpu_feature_bit_index(feature, &word_index, &bit_index))
    {
        features.words[word_index] |= UINT64_C(1) << bit_index;
    }
    return features;
}

TargetCpuFeatures target_cpu_features_remove(TargetCpuFeatures features, TargetCpuFeature feature)
{
    u32 word_index = 0;
    u32 bit_index = 0;
    if (target_cpu_feature_bit_index(feature, &word_index, &bit_index))
    {
        features.words[word_index] &= ~(UINT64_C(1) << bit_index);
    }
    return features;
}

bool target_cpu_features_contains(TargetCpuFeatures features, TargetCpuFeature feature)
{
    u32 word_index = 0;
    u32 bit_index = 0;
    return target_cpu_feature_bit_index(feature, &word_index, &bit_index) && (features.words[word_index] & (UINT64_C(1) << bit_index)) != 0;
}

TargetCpuFeatures target_cpu_features_union(TargetCpuFeatures left, TargetCpuFeatures right)
{
    TargetCpuFeatures result = target_cpu_features_empty();
    for (u32 word_index = 0; word_index < TARGET_CPU_FEATURE_WORD_COUNT; word_index += 1)
    {
        result.words[word_index] = left.words[word_index] | right.words[word_index];
    }
    return result;
}

TargetCpuFeatures target_cpu_features_intersection(TargetCpuFeatures left, TargetCpuFeatures right)
{
    TargetCpuFeatures result = target_cpu_features_empty();
    for (u32 word_index = 0; word_index < TARGET_CPU_FEATURE_WORD_COUNT; word_index += 1)
    {
        result.words[word_index] = left.words[word_index] & right.words[word_index];
    }
    return result;
}

TargetCpuFeatures target_cpu_features_difference(TargetCpuFeatures left, TargetCpuFeatures right)
{
    TargetCpuFeatures result = target_cpu_features_empty();
    for (u32 word_index = 0; word_index < TARGET_CPU_FEATURE_WORD_COUNT; word_index += 1)
    {
        result.words[word_index] = left.words[word_index] & ~right.words[word_index];
    }
    return result;
}

bool target_cpu_features_equal(TargetCpuFeatures left, TargetCpuFeatures right)
{
    for (u32 word_index = 0; word_index < TARGET_CPU_FEATURE_WORD_COUNT; word_index += 1)
    {
        if (left.words[word_index] != right.words[word_index])
        {
            return false;
        }
    }
    return true;
}

bool target_cpu_features_any(TargetCpuFeatures features)
{
    bool result = false;
    for (u32 word_index = 0; word_index < TARGET_CPU_FEATURE_WORD_COUNT && !result; word_index += 1)
    {
        result = features.words[word_index];
    }

    return result;
}

bool target_cpu_features_subset(TargetCpuFeatures subset, TargetCpuFeatures superset)
{
    return !target_cpu_features_any(target_cpu_features_difference(subset, superset));
}

TargetCpuFeatures target_cpu_features_from_array(TargetCpuFeature const* features, u32 count)
{
    TargetCpuFeatures result = target_cpu_features_empty();
    if (features)
    {
        for (u32 index = 0; index < count; index += 1)
        {
            result = target_cpu_features_add(result, features[index]);
        }
    }

    return result;
}

bool target_uses_llp64_data_model(Target target)
{
    return target.os == OPERATING_SYSTEM_WINDOWS ||
           (target.os == OPERATING_SYSTEM_UEFI && target.cpu_arch == CPU_ARCH_X86_64);
}

bool target_uses_16_bit_wchar(Target target)
{
    return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI;
}

bool target_uses_pe_unwind(Target target)
{
    return (target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI) &&
           (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64);
}

TargetDataLayout target_data_layout(Target target)
{
    bool windows = target.os == OPERATING_SYSTEM_WINDOWS;
    bool llp64 = target_uses_llp64_data_model(target);
    bool apple = target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS;
    bool wasm64 = target.cpu_arch == CPU_ARCH_WASM64;
    bool bpfel = target.cpu_arch == CPU_ARCH_BPFEL;
    bool arm_plain_char_unsigned = target.cpu_arch == CPU_ARCH_AARCH64 && !apple && !windows;
    u32 long_size = llp64 ? 4 : 8;
    bool double_long_double = llp64 || wasm64 || bpfel || (apple && target.cpu_arch == CPU_ARCH_AARCH64);
    u32 long_double_size = double_long_double ? 8 : 16;
    u32 long_double_bits = double_long_double ? 64 : target.cpu_arch == CPU_ARCH_X86_64 ? 80 : 128;
    u32 va_list_size = llp64 || wasm64 || bpfel ? 8 : target.cpu_arch == CPU_ARCH_X86_64 ? 32 : 32;

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
        .atomic_max_width = wasm64 || bpfel ? 64 : 128,
        .atomic_alignment = wasm64 || bpfel ? 8 : 16,
        .abi_stack_alignment = bpfel ? 8 : 16,
        .abi_max_alignment = bpfel ? 8 : 16,
        .endianness = TARGET_ENDIAN_LITTLE,
        .plain_char_is_signed = !arm_plain_char_unsigned,
        .has_128_bit_integer = !wasm64 && !bpfel,
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
    if (component.length != prefix_length)
    {
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
    }

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
            else if (target_component_equal(component, S8("wasm64")))
            {
                result.target.cpu_arch = CPU_ARCH_WASM64;
            }
            else if (target_component_equal(component, S8("bpfel")) || target_component_equal(component, S8("bpf")) ||
                     target_component_equal(component, S8("ebpf")))
            {
                result.target.cpu_arch = CPU_ARCH_BPFEL;
            }
            else
            {
                result.invalid_component = component;
                result.error = TARGET_PARSE_ERROR_ARCHITECTURE;
                return result;
            }
        }
        // No operating system spells itself like a CPU model, so testing the
        // model names first costs nothing and keeps `-march=`-shaped mistakes
        // out of the vendor and environment fields.
        else if (cpu_model_from_string(component) != CPU_MODEL_ERROR)
        {
            result.invalid_component = component;
            result.error = TARGET_PARSE_ERROR_CPU_MODEL;
            return result;
        }
        else if (component_index >= TARGET_TRIPLE_COMPONENT_LIMIT)
        {
            result.invalid_component = component;
            result.error = TARGET_PARSE_ERROR_EXCESS_COMPONENT;
            return result;
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

CpuModel cpu_model_resolve_detected(CpuModel model)
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
#else
#error TODO: implement CPU detection code for this architecture
#endif
    cpu_model = cpu_model_resolve_detected(cpu_model);
#if BUSTER_CPU_ARCH_AARCH64
    // AArch64 model detection is the feature oracle until per-feature system
    // register probing is available.  In particular, a detected Apple M1 must
    // inherit the complete pinned M1 profile rather than the generic NEON
    // fallback.
    target_native.cpu_features = target_cpu_features_default(CPU_ARCH_AARCH64, cpu_model);
#endif
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
        return arch == CPU_ARCH_X86_64 || arch == CPU_ARCH_AARCH64 || arch == CPU_ARCH_WASM64 || arch == CPU_ARCH_BPFEL;
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
        if (model == CPU_MODEL_A64_APPLE_M1)
        {
            return target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_AARCH64_V8_4A,
                TARGET_CPU_FEATURE_AARCH64_AES,
                TARGET_CPU_FEATURE_AARCH64_ALTNZCV,
                TARGET_CPU_FEATURE_AARCH64_CCDP,
                TARGET_CPU_FEATURE_AARCH64_CCPP,
                TARGET_CPU_FEATURE_AARCH64_COMPLXNUM,
                TARGET_CPU_FEATURE_AARCH64_CRC,
                TARGET_CPU_FEATURE_AARCH64_DOTPROD,
                TARGET_CPU_FEATURE_AARCH64_FLAGM,
                TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
                TARGET_CPU_FEATURE_AARCH64_FP16FML,
                TARGET_CPU_FEATURE_AARCH64_FPTOINT,
                TARGET_CPU_FEATURE_AARCH64_FULLFP16,
                TARGET_CPU_FEATURE_AARCH64_JSCONV,
                TARGET_CPU_FEATURE_AARCH64_LSE,
                TARGET_CPU_FEATURE_AARCH64_LOR,
                TARGET_CPU_FEATURE_AARCH64_NEON,
                TARGET_CPU_FEATURE_AARCH64_PAUTH,
                TARGET_CPU_FEATURE_AARCH64_PERFMON,
                TARGET_CPU_FEATURE_AARCH64_PREDRES,
                TARGET_CPU_FEATURE_AARCH64_RAS,
                TARGET_CPU_FEATURE_AARCH64_RCPC,
                TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO,
                TARGET_CPU_FEATURE_AARCH64_RDM,
                TARGET_CPU_FEATURE_AARCH64_SB,
                TARGET_CPU_FEATURE_AARCH64_SHA2,
                TARGET_CPU_FEATURE_AARCH64_SHA3,
                TARGET_CPU_FEATURE_AARCH64_SPECRESTRICT,
                TARGET_CPU_FEATURE_AARCH64_SSBS,
                TARGET_CPU_FEATURE_AARCH64_TRACEV8_4,
            }, 30);
        }
        return target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
            TARGET_CPU_FEATURE_AARCH64_NEON,
        }, 2);
    }
    if (arch != CPU_ARCH_X86_64)
    {
        return target_cpu_features_empty();
    }
    TargetCpuFeatures result = target_cpu_features_singleton(TARGET_CPU_FEATURE_X86_SSE2);
    if (model >= CPU_MODEL_AMD_K8_SSE3 && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SSE3);
    }
    // Keep the ten-feature legacy/scalar closure expressed as generation
    // ranges rather than repeating the same feature additions in every model
    // case.  The ranges mirror the architectural feature families represented
    // by the target model table; SHA is handled by the separately audited
    // predicate immediately below.
    bool amd_ssse3 = model >= CPU_MODEL_AMD_BT_1 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_ssse3 = model >= CPU_MODEL_INTEL_CORE_2 && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_sse4_1 = model >= CPU_MODEL_AMD_BT_2 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_sse4_1 = (model >= CPU_MODEL_INTEL_PENRYN && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                        (model >= CPU_MODEL_INTEL_SILVERMONT && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    bool amd_sse4_2 = amd_sse4_1;
    bool intel_sse4_2 = (model >= CPU_MODEL_INTEL_NEHALEM && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                        (model >= CPU_MODEL_INTEL_SILVERMONT && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    bool amd_f16c = model == CPU_MODEL_AMD_BT_2 || (model >= CPU_MODEL_AMD_BD_2 && model <= CPU_MODEL_AMD_ZEN_5);
    bool intel_phi = model == CPU_MODEL_INTEL_KNL || model == CPU_MODEL_INTEL_KNM;
    bool intel_future_server = (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_CLEARWATERFOREST) ||
                               model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_f16c = (model >= CPU_MODEL_INTEL_IVY_BRIDGE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) || intel_phi || intel_future_server;
    bool amd_fma = model >= CPU_MODEL_AMD_BD_2 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_fma = (model >= CPU_MODEL_INTEL_HASWELL && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) || intel_phi || intel_future_server;
    bool amd_bmi2 = model >= CPU_MODEL_AMD_BD_4 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_bmi2 = (model >= CPU_MODEL_INTEL_HASWELL && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) || intel_phi || intel_future_server;
    bool amd_adx = model >= CPU_MODEL_AMD_ZEN_1 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_adx = (model >= CPU_MODEL_INTEL_BROADWELL && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) || intel_phi || intel_future_server;
    bool amd_movbe = model == CPU_MODEL_AMD_BT_2 || (model >= CPU_MODEL_AMD_BD_4 && model <= CPU_MODEL_AMD_ZEN_5);
    bool intel_movbe = model >= CPU_MODEL_INTEL_HASWELL && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_rdrand = model >= CPU_MODEL_AMD_BD_4 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_rdrand = (model >= CPU_MODEL_INTEL_IVY_BRIDGE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                        (model >= CPU_MODEL_INTEL_SILVERMONT && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    bool amd_rdseed = model >= CPU_MODEL_AMD_ZEN_1 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_rdseed = (model >= CPU_MODEL_INTEL_BROADWELL && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                        (model >= CPU_MODEL_INTEL_GOLDMONT && model <= CPU_MODEL_INTEL_DIAMOND_RAPIDS);
    bool amd_sha = model >= CPU_MODEL_AMD_ZEN_1 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_sha = model == CPU_MODEL_INTEL_ROCKETLAKE ||
                     model == CPU_MODEL_INTEL_CANNONLAKE ||
                     (model >= CPU_MODEL_INTEL_ICELAKE_CLIENT && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                     (model >= CPU_MODEL_INTEL_GOLDMONT && model <= CPU_MODEL_INTEL_TREMONT) ||
                     (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_CLEARWATERFOREST) ||
                     model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    // LLVM's X86.td model table exposes all three vector crypto extensions on
    // this exact audited set. Keep the membership explicit: the intervening
    // Arrow Lake and Granite Rapids profiles do not advertise the same trio.
    bool intel_sha512_sm3_sm4 = model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                                model == CPU_MODEL_INTEL_LUNARLAKE ||
                                model == CPU_MODEL_INTEL_PANTHERLAKE ||
                                model == CPU_MODEL_INTEL_CLEARWATERFOREST ||
                                model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_invpcid = model >= CPU_MODEL_AMD_ZEN_3 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_invpcid = (model >= CPU_MODEL_INTEL_HASWELL && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                         intel_future_server || intel_phi;
    bool amd_pku = amd_invpcid;
    bool intel_pku = (model >= CPU_MODEL_INTEL_SKYLAKE_AVX512 && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                     intel_future_server;
    bool intel_sgx = model == CPU_MODEL_INTEL_SKYLAKE ||
                     (model >= CPU_MODEL_INTEL_CANNONLAKE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                     (model >= CPU_MODEL_INTEL_GOLDMONT_PLUS && model <= CPU_MODEL_INTEL_TREMONT) ||
                     intel_future_server;
    bool intel_hreset = model == CPU_MODEL_INTEL_ALDERLAKE ||
                        model == CPU_MODEL_INTEL_RAPTORLAKE ||
                        model == CPU_MODEL_INTEL_METEORLAKE ||
                        model == CPU_MODEL_INTEL_GRACEMONT ||
                        model == CPU_MODEL_INTEL_ARROWLAKE ||
                        model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                        model == CPU_MODEL_INTEL_LUNARLAKE ||
                        model == CPU_MODEL_INTEL_PANTHERLAKE ||
                        model == CPU_MODEL_INTEL_SIERRAFOREST ||
                        model == CPU_MODEL_INTEL_GRANDRIDGE ||
                        model == CPU_MODEL_INTEL_CLEARWATERFOREST;
    // PCONFIG follows the audited Clang 22 model ranges exactly: the full
    // Alder-to-Granite-D client/server run, the Sierra-to-Clearwater future
    // Atom run, and Diamond Rapids as a singleton.  The intervening
    // Icelake-server row is intentionally included; no broad Intel range is
    // safe because Tiger/Icelake-client and the legacy Atom rows are gaps.
    bool intel_pconfig = (model >= CPU_MODEL_INTEL_ALDERLAKE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                         (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_CLEARWATERFOREST) ||
                         model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_tsxldtrk = (model >= CPU_MODEL_INTEL_EMERALD_RAPIDS &&
                           model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                          model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_serialize_waitpkg = model == CPU_MODEL_INTEL_ALDERLAKE ||
                                   model == CPU_MODEL_INTEL_RAPTORLAKE ||
                                   model == CPU_MODEL_INTEL_METEORLAKE ||
                                   model == CPU_MODEL_INTEL_GRACEMONT ||
                                   model == CPU_MODEL_INTEL_ARROWLAKE ||
                                   model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                                   model == CPU_MODEL_INTEL_LUNARLAKE ||
                                   model == CPU_MODEL_INTEL_PANTHERLAKE ||
                                   model == CPU_MODEL_INTEL_EMERALD_RAPIDS ||
                                   model == CPU_MODEL_INTEL_SAPPHIRE_RAPIDS ||
                                   model == CPU_MODEL_INTEL_GRANITE_RAPIDS ||
                                   model == CPU_MODEL_INTEL_GRANITE_RAPIDS_D ||
                                   model == CPU_MODEL_INTEL_SIERRAFOREST ||
                                   model == CPU_MODEL_INTEL_GRANDRIDGE ||
                                   model == CPU_MODEL_INTEL_CLEARWATERFOREST ||
                                   model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_uintr = model == CPU_MODEL_INTEL_ARROWLAKE ||
                       model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                       model == CPU_MODEL_INTEL_LUNARLAKE ||
                       model == CPU_MODEL_INTEL_PANTHERLAKE ||
                       model == CPU_MODEL_INTEL_EMERALD_RAPIDS ||
                       model == CPU_MODEL_INTEL_SAPPHIRE_RAPIDS ||
                       model == CPU_MODEL_INTEL_GRANITE_RAPIDS ||
                       model == CPU_MODEL_INTEL_GRANITE_RAPIDS_D ||
                       model == CPU_MODEL_INTEL_SIERRAFOREST ||
                       model == CPU_MODEL_INTEL_GRANDRIDGE ||
                       model == CPU_MODEL_INTEL_CLEARWATERFOREST ||
                       model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_enqcmd = (model >= CPU_MODEL_INTEL_ARROWLAKE && model <= CPU_MODEL_INTEL_PANTHERLAKE) ||
                        (model >= CPU_MODEL_INTEL_EMERALD_RAPIDS && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                        (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_CLEARWATERFOREST) ||
                        model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_ptwrite = model == CPU_MODEL_INTEL_ALDERLAKE ||
                         model == CPU_MODEL_INTEL_RAPTORLAKE ||
                         model == CPU_MODEL_INTEL_METEORLAKE ||
                         model == CPU_MODEL_INTEL_GRACEMONT ||
                         model == CPU_MODEL_INTEL_ARROWLAKE ||
                         model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                         model == CPU_MODEL_INTEL_LUNARLAKE ||
                         model == CPU_MODEL_INTEL_PANTHERLAKE ||
                         model == CPU_MODEL_INTEL_EMERALD_RAPIDS ||
                         model == CPU_MODEL_INTEL_SAPPHIRE_RAPIDS ||
                         model == CPU_MODEL_INTEL_GRANITE_RAPIDS ||
                         model == CPU_MODEL_INTEL_GRANITE_RAPIDS_D ||
                         model == CPU_MODEL_INTEL_GOLDMONT_PLUS ||
                         model == CPU_MODEL_INTEL_TREMONT ||
                         model == CPU_MODEL_INTEL_SIERRAFOREST ||
                         model == CPU_MODEL_INTEL_GRANDRIDGE ||
                         model == CPU_MODEL_INTEL_CLEARWATERFOREST ||
                         model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_movdir64b = model == CPU_MODEL_AMD_ZEN_5;
    bool intel_movdir64b = model == CPU_MODEL_INTEL_TIGERLAKE ||
                           model == CPU_MODEL_INTEL_ALDERLAKE ||
                           model == CPU_MODEL_INTEL_RAPTORLAKE ||
                           model == CPU_MODEL_INTEL_METEORLAKE ||
                           model == CPU_MODEL_INTEL_GRACEMONT ||
                           model == CPU_MODEL_INTEL_ARROWLAKE ||
                           model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                           model == CPU_MODEL_INTEL_LUNARLAKE ||
                           model == CPU_MODEL_INTEL_PANTHERLAKE ||
                           model == CPU_MODEL_INTEL_EMERALD_RAPIDS ||
                           model == CPU_MODEL_INTEL_SAPPHIRE_RAPIDS ||
                           model == CPU_MODEL_INTEL_GRANITE_RAPIDS ||
                           model == CPU_MODEL_INTEL_GRANITE_RAPIDS_D ||
                           model == CPU_MODEL_INTEL_SIERRAFOREST ||
                           model == CPU_MODEL_INTEL_GRANDRIDGE ||
                           model == CPU_MODEL_INTEL_CLEARWATERFOREST ||
                           model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_shstk = model == CPU_MODEL_AMD_ZEN_4 || model == CPU_MODEL_AMD_ZEN_5;
    bool intel_shstk = model == CPU_MODEL_INTEL_TIGERLAKE ||
                       model == CPU_MODEL_INTEL_ALDERLAKE ||
                       model == CPU_MODEL_INTEL_RAPTORLAKE ||
                       model == CPU_MODEL_INTEL_METEORLAKE ||
                       model == CPU_MODEL_INTEL_GRACEMONT ||
                       model == CPU_MODEL_INTEL_ARROWLAKE ||
                       model == CPU_MODEL_INTEL_ARROWLAKE_S ||
                       model == CPU_MODEL_INTEL_LUNARLAKE ||
                       model == CPU_MODEL_INTEL_PANTHERLAKE ||
                       model == CPU_MODEL_INTEL_EMERALD_RAPIDS ||
                       model == CPU_MODEL_INTEL_SAPPHIRE_RAPIDS ||
                       model == CPU_MODEL_INTEL_GRANITE_RAPIDS ||
                       model == CPU_MODEL_INTEL_GRANITE_RAPIDS_D ||
                       model == CPU_MODEL_INTEL_SIERRAFOREST ||
                       model == CPU_MODEL_INTEL_GRANDRIDGE ||
                       model == CPU_MODEL_INTEL_CLEARWATERFOREST ||
                       model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_state_fsgsbase = model >= CPU_MODEL_AMD_BD_3 && model <= CPU_MODEL_AMD_ZEN_5;
    bool amd_state_xsave = model >= CPU_MODEL_AMD_BT_2 && model <= CPU_MODEL_AMD_ZEN_5;
    bool amd_state_xsaves = model >= CPU_MODEL_AMD_ZEN_1 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_atom_future = (model >= CPU_MODEL_INTEL_GOLDMONT && model <= CPU_MODEL_INTEL_TREMONT) ||
                             (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_CLEARWATERFOREST);
    bool intel_state_fsgsbase = (model >= CPU_MODEL_INTEL_IVY_BRIDGE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                                intel_atom_future || intel_phi || model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_state_xsave = (model >= CPU_MODEL_INTEL_SANDY_BRIDGE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                             intel_atom_future || intel_phi || model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool intel_state_xsaves = (model >= CPU_MODEL_INTEL_SKYLAKE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                              intel_atom_future || model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_clflushopt = model >= CPU_MODEL_AMD_ZEN_1 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_clflushopt = (model >= CPU_MODEL_INTEL_SKYLAKE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                            (model >= CPU_MODEL_INTEL_GOLDMONT && model <= CPU_MODEL_INTEL_TREMONT) ||
                            (model >= CPU_MODEL_INTEL_SIERRAFOREST && model <= CPU_MODEL_INTEL_CLEARWATERFOREST) ||
                            model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_clwb = model >= CPU_MODEL_AMD_ZEN_2 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_clwb = model == CPU_MODEL_INTEL_SKYLAKE_AVX512 ||
                      (model >= CPU_MODEL_INTEL_COOPERLAKE && model <= CPU_MODEL_INTEL_CASCADELAKE) ||
                      (model >= CPU_MODEL_INTEL_TIGERLAKE && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                      (model >= CPU_MODEL_INTEL_TREMONT && model <= CPU_MODEL_INTEL_CLEARWATERFOREST) ||
                      model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    bool amd_wbnoinvd = model >= CPU_MODEL_AMD_ZEN_2 && model <= CPU_MODEL_AMD_ZEN_5;
    bool intel_wbnoinvd = (model >= CPU_MODEL_INTEL_ICELAKE_SERVER && model <= CPU_MODEL_INTEL_GRANITE_RAPIDS_D) ||
                          model == CPU_MODEL_INTEL_DIAMOND_RAPIDS;
    if (amd_ssse3 || intel_ssse3)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SSSE3);
    }
    if (amd_sse4_1 || intel_sse4_1)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SSE4_1);
    }
    if (amd_sse4_2 || intel_sse4_2)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SSE4_2);
    }
    if (amd_f16c || intel_f16c)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_F16C);
    }
    if (amd_fma || intel_fma)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_FMA);
    }
    if (amd_bmi2 || intel_bmi2)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_BMI2);
    }
    if (amd_adx || intel_adx)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_ADX);
    }
    if (amd_movbe || intel_movbe)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_MOVBE);
    }
    if (amd_rdrand || intel_rdrand)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_RDRAND);
    }
    if (amd_rdseed || intel_rdseed)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_RDSEED);
    }
    if (amd_sha || intel_sha)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SHA);
    }
    if (amd_invpcid || intel_invpcid)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_INVPCID);
    }
    if (amd_pku || intel_pku)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_PKU);
    }
    if (intel_sgx)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SGX);
    }
    if (intel_hreset)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_HRESET);
    }
    if (intel_pconfig)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_PCONFIG);
    }
    if (intel_tsxldtrk)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_TSXLDTRK);
    }
    if (intel_serialize_waitpkg)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SERIALIZE);
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_WAITPKG);
    }
    if (intel_uintr)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_UINTR);
    }
    if (intel_enqcmd)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_ENQCMD);
    }
    if (intel_ptwrite)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_PTWRITE);
    }
    if (amd_movdir64b || intel_movdir64b)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_MOVDIR64B);
    }
    if (amd_shstk || intel_shstk)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SHSTK);
    }
    if (intel_sha512_sm3_sm4)
    {
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SHA512, TARGET_CPU_FEATURE_X86_SM3, TARGET_CPU_FEATURE_X86_SM4}, 3));
    }
    if (amd_state_fsgsbase || intel_state_fsgsbase)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_FSGSBASE);
    }
    if (amd_state_xsave || intel_state_xsave)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_XSAVE);
    }
    if (amd_state_xsaves || intel_state_xsaves)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_XSAVES);
    }
    if (amd_clflushopt || intel_clflushopt)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_CLFLUSHOPT);
    }
    if (amd_clwb || intel_clwb)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_CLWB);
    }
    if (amd_wbnoinvd || intel_wbnoinvd)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_WBNOINVD);
    }
    bool amd_family_10_or_newer = model >= CPU_MODEL_AMD_AMD_FAMILY_10 && model <= CPU_MODEL_AMD_ZEN_5;
    if (amd_family_10_or_newer)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_SSE4A);
    }
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
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_POPCNT);
    }
    if (amd_family_10_or_newer || intel_lzcnt)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_LZCNT);
    }
    if (amd_bmi1 || intel_bmi1)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_BMI1);
    }
    if (amd_family_10_or_newer || intel_cx16)
    {
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_CX16);
    }
    TargetCpuFeatures avx2_haswell = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL}, 3);
    TargetCpuFeatures avx2_skylake = target_cpu_features_add(avx2_haswell, TARGET_CPU_FEATURE_X86_AES);
    TargetCpuFeatures avx2_alderlake = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AVX_VNNI}, 8);
    TargetCpuFeatures avx2_arrowlake = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AVX_VNNI,
        TARGET_CPU_FEATURE_X86_AVX_IFMA, TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8}, 11);
    TargetCpuFeatures avx2_arrowlake_s = target_cpu_features_add(avx2_arrowlake, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16);
    TargetCpuFeatures amd_btver2 = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL}, 3);
    TargetCpuFeatures amd_bdver1 = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_FMA4,
        TARGET_CPU_FEATURE_X86_LWP, TARGET_CPU_FEATURE_X86_XOP}, 6);
    TargetCpuFeatures amd_bdver2 = target_cpu_features_add(amd_bdver1, TARGET_CPU_FEATURE_X86_TBM);
    TargetCpuFeatures amd_bdver4 = target_cpu_features_add(amd_bdver2, TARGET_CPU_FEATURE_X86_AVX2);
    TargetCpuFeatures amd_zen1 = target_cpu_features_add(avx2_haswell, TARGET_CPU_FEATURE_X86_AES);
    TargetCpuFeatures amd_zen3 = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ}, 6);
    TargetCpuFeatures avx512_skylake = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ}, 9);
    TargetCpuFeatures avx512_cannonlake = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA,
        TARGET_CPU_FEATURE_X86_AVX512VBMI}, 11);
    TargetCpuFeatures avx512_ice_lake = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA,
        TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ,
        TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES,
        TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AVX512BITALG}, 18);
    TargetCpuFeatures sapphire_rapids = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA,
        TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ,
        TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES,
        TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AVX512BITALG, TARGET_CPU_FEATURE_X86_AVX512BF16,
        TARGET_CPU_FEATURE_X86_AVX512FP16, TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8,
        TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_CLDEMOTE}, 25);
    TargetCpuFeatures granite_rapids = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AES,
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA,
        TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ,
        TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_GFNI, TARGET_CPU_FEATURE_X86_VAES,
        TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AVX512BITALG, TARGET_CPU_FEATURE_X86_AVX512BF16,
        TARGET_CPU_FEATURE_X86_AVX512FP16, TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8,
        TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_CLDEMOTE,
        TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_512, TARGET_CPU_FEATURE_X86_AMX_FP16,
        TARGET_CPU_FEATURE_X86_PREFETCHI}, 29);
    TargetCpuFeatures granite_rapids_d = target_cpu_features_add(granite_rapids, TARGET_CPU_FEATURE_X86_AMX_COMPLEX);
    switch (model)
    {
    case CPU_MODEL_AMD_K8:
    case CPU_MODEL_AMD_K8_SSE3:
    case CPU_MODEL_AMD_AMD_FAMILY_10:
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_3DNOW, TARGET_CPU_FEATURE_X86_3DNOWA}, 2));
        break;
    case CPU_MODEL_AMD_ZEN_1:
    case CPU_MODEL_AMD_ZEN_2:
        result = target_cpu_features_union(result, amd_zen1);
        break;
    case CPU_MODEL_AMD_ZEN_3:
        result = target_cpu_features_union(result, amd_zen3);
        break;
    case CPU_MODEL_AMD_BT_2:
        result = target_cpu_features_union(result, amd_btver2);
        break;
    case CPU_MODEL_AMD_BD_1:
        result = target_cpu_features_union(result, amd_bdver1);
        break;
    case CPU_MODEL_AMD_BD_2:
    case CPU_MODEL_AMD_BD_3:
        result = target_cpu_features_union(result, amd_bdver2);
        break;
    case CPU_MODEL_AMD_BD_4:
        result = target_cpu_features_union(result, amd_bdver4);
        break;
    case CPU_MODEL_INTEL_HASWELL:
    case CPU_MODEL_INTEL_BROADWELL:
        result = target_cpu_features_union(result, avx2_haswell);
        break;
    case CPU_MODEL_INTEL_SKYLAKE:
        result = target_cpu_features_union(result, avx2_skylake);
        break;
    case CPU_MODEL_INTEL_ALDERLAKE:
    case CPU_MODEL_INTEL_RAPTORLAKE:
    case CPU_MODEL_INTEL_METEORLAKE:
    case CPU_MODEL_INTEL_GRACEMONT:
        result = target_cpu_features_union(result, avx2_alderlake);
        break;
    case CPU_MODEL_INTEL_ARROWLAKE:
        result = target_cpu_features_union(result, avx2_arrowlake);
        break;
    case CPU_MODEL_INTEL_ARROWLAKE_S:
    case CPU_MODEL_INTEL_LUNARLAKE:
    case CPU_MODEL_INTEL_PANTHERLAKE:
        result = target_cpu_features_union(result, avx2_arrowlake_s);
        break;
    case CPU_MODEL_AMD_ZEN_4:
        result = target_cpu_features_union(result, avx512_ice_lake);
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_AVX512BF16);
        break;
    case CPU_MODEL_AMD_ZEN_5:
        result = target_cpu_features_union(result, avx512_ice_lake);
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX_VNNI,
            TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT, TARGET_CPU_FEATURE_X86_PREFETCHI}, 4));
        break;
    case CPU_MODEL_INTEL_SKYLAKE_AVX512:
        result = target_cpu_features_union(result, avx512_skylake);
        break;
    case CPU_MODEL_INTEL_ROCKETLAKE:
        result = target_cpu_features_union(result, avx512_ice_lake);
        break;
    case CPU_MODEL_INTEL_CANNONLAKE:
        result = target_cpu_features_union(result, avx512_cannonlake);
        break;
    case CPU_MODEL_INTEL_CASCADELAKE:
        result = target_cpu_features_union(result, avx512_cannonlake);
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_AVX512VNNI);
        break;
    case CPU_MODEL_INTEL_COOPERLAKE:
        result = target_cpu_features_union(result, avx512_cannonlake);
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512BF16}, 2));
        break;
    case CPU_MODEL_INTEL_ICELAKE_CLIENT:
    case CPU_MODEL_INTEL_ICELAKE_SERVER:
        result = target_cpu_features_union(result, avx512_ice_lake);
        break;
    case CPU_MODEL_INTEL_TIGERLAKE:
        result = target_cpu_features_union(result, avx512_ice_lake);
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT);
        break;
    case CPU_MODEL_INTEL_EMERALD_RAPIDS:
        result = target_cpu_features_union(result, sapphire_rapids);
        break;
    case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS:
        result = target_cpu_features_union(result, sapphire_rapids);
        break;
    case CPU_MODEL_INTEL_KNL:
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
            TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512PF, TARGET_CPU_FEATURE_X86_AVX512ER,
            TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL}, 8));
        break;
    case CPU_MODEL_INTEL_KNM:
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
            TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512PF, TARGET_CPU_FEATURE_X86_AVX512ER,
            TARGET_CPU_FEATURE_X86_AVX5124VNNIW, TARGET_CPU_FEATURE_X86_AVX5124FMAPS,
            TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ, TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL}, 11));
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS:
        result = target_cpu_features_union(result, granite_rapids);
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS_D:
        result = target_cpu_features_union(result, granite_rapids_d);
        break;
    case CPU_MODEL_INTEL_DIAMOND_RAPIDS:
        result = target_cpu_features_union(result, granite_rapids_d);
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, TARGET_CPU_FEATURE_X86_MOVRS,
            TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF,
            TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16,
            TARGET_CPU_FEATURE_X86_AVX_IFMA, TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT,
            TARGET_CPU_FEATURE_X86_AMX_FP8, TARGET_CPU_FEATURE_X86_AMX_AVX512,
            TARGET_CPU_FEATURE_X86_AMX_MOVRS}, 12));
        break;
    case CPU_MODEL_INTEL_WESTMERE:
    case CPU_MODEL_INTEL_SILVERMONT:
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_PCLMUL);
        break;
    case CPU_MODEL_INTEL_GOLDMONT:
    case CPU_MODEL_INTEL_GOLDMONT_PLUS:
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL}, 2));
        break;
    case CPU_MODEL_INTEL_TREMONT:
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AES, TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_GFNI}, 3));
        break;
    case CPU_MODEL_INTEL_SIERRAFOREST:
    case CPU_MODEL_INTEL_GRANDRIDGE:
        result = target_cpu_features_union(result, avx2_arrowlake);
        result = target_cpu_features_add(result, TARGET_CPU_FEATURE_X86_CLDEMOTE);
        break;
    case CPU_MODEL_INTEL_CLEARWATERFOREST:
        result = target_cpu_features_union(result, avx2_arrowlake_s);
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI}, 2));
        break;
    case CPU_MODEL_INTEL_SANDY_BRIDGE:
    case CPU_MODEL_INTEL_IVY_BRIDGE:
        result = target_cpu_features_union(result, target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_PCLMUL}, 2));
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
    if (target.cpu_features_explicit)
    {
        TargetCpuFeatures features = target.cpu_features;
        if (target.cpu_arch == CPU_ARCH_WASM64 || target.cpu_arch == CPU_ARCH_BPFEL)
        {
            return !target_cpu_features_any(features);
        }
        if (target.cpu_arch == CPU_ARCH_AARCH64)
        {
            TargetCpuFeature const known_feature_list[] = {
                TARGET_CPU_FEATURE_AARCH64_V8_4A,
                TARGET_CPU_FEATURE_AARCH64_AES,
                TARGET_CPU_FEATURE_AARCH64_ALTNZCV,
                TARGET_CPU_FEATURE_AARCH64_CCDP,
                TARGET_CPU_FEATURE_AARCH64_CCPP,
                TARGET_CPU_FEATURE_AARCH64_COMPLXNUM,
                TARGET_CPU_FEATURE_AARCH64_CRC,
                TARGET_CPU_FEATURE_AARCH64_DOTPROD,
                TARGET_CPU_FEATURE_AARCH64_FLAGM,
                TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
                TARGET_CPU_FEATURE_AARCH64_FP16FML,
                TARGET_CPU_FEATURE_AARCH64_FPTOINT,
                TARGET_CPU_FEATURE_AARCH64_FULLFP16,
                TARGET_CPU_FEATURE_AARCH64_JSCONV,
                TARGET_CPU_FEATURE_AARCH64_LSE,
                TARGET_CPU_FEATURE_AARCH64_LOR,
                TARGET_CPU_FEATURE_AARCH64_NEON,
                TARGET_CPU_FEATURE_AARCH64_PAUTH,
                TARGET_CPU_FEATURE_AARCH64_PERFMON,
                TARGET_CPU_FEATURE_AARCH64_PREDRES,
                TARGET_CPU_FEATURE_AARCH64_RAS,
                TARGET_CPU_FEATURE_AARCH64_RCPC,
                TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO,
                TARGET_CPU_FEATURE_AARCH64_RDM,
                TARGET_CPU_FEATURE_AARCH64_SB,
                TARGET_CPU_FEATURE_AARCH64_SHA2,
                TARGET_CPU_FEATURE_AARCH64_SHA3,
                TARGET_CPU_FEATURE_AARCH64_SPECRESTRICT,
                TARGET_CPU_FEATURE_AARCH64_SSBS,
                TARGET_CPU_FEATURE_AARCH64_TRACEV8_4,
                TARGET_CPU_FEATURE_AARCH64_SME,
            };
            TargetCpuFeatures known = target_cpu_features_from_array(known_feature_list, (u32)BUSTER_ARRAY_LENGTH(known_feature_list));
            if (!target_cpu_features_subset(features, known))
            {
                return false;
            }
            TargetCpuFeatures neon_dependents = target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_AARCH64_AES,
                TARGET_CPU_FEATURE_AARCH64_COMPLXNUM,
                TARGET_CPU_FEATURE_AARCH64_DOTPROD,
                TARGET_CPU_FEATURE_AARCH64_FP16FML,
                TARGET_CPU_FEATURE_AARCH64_RDM,
                TARGET_CPU_FEATURE_AARCH64_SHA2,
                TARGET_CPU_FEATURE_AARCH64_SHA3,
            }, 7);
            TargetCpuFeatures fp_dependents = target_cpu_features_from_array((TargetCpuFeature const[]){
                TARGET_CPU_FEATURE_AARCH64_FPTOINT,
                TARGET_CPU_FEATURE_AARCH64_FULLFP16,
                TARGET_CPU_FEATURE_AARCH64_JSCONV,
                TARGET_CPU_FEATURE_AARCH64_NEON,
            }, 4);
            if ((target_cpu_features_any(target_cpu_features_intersection(features, neon_dependents)) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_NEON)) ||
                (target_cpu_features_any(target_cpu_features_intersection(features, fp_dependents)) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_FP_ARMV8)) ||
                (target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_ALTNZCV) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_FLAGM)) ||
                (target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_CCDP) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_CCPP)) ||
                (target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_FP16FML) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_FULLFP16)) ||
                (target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_RCPC)) ||
                (target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_SHA3) &&
                 !target_cpu_features_contains(features, TARGET_CPU_FEATURE_AARCH64_SHA2)))
            {
                return false;
            }
            return true;
        }
        if (target.cpu_arch != CPU_ARCH_X86_64)
        {
            return false;
        }
        TargetCpuFeature const known_feature_list[] = {
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
            TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_2,
            TARGET_CPU_FEATURE_X86_AVX10_512, TARGET_CPU_FEATURE_X86_APX, TARGET_CPU_FEATURE_X86_AVX512BW,
            TARGET_CPU_FEATURE_X86_SSE3, TARGET_CPU_FEATURE_X86_POPCNT, TARGET_CPU_FEATURE_X86_LZCNT,
            TARGET_CPU_FEATURE_X86_BMI1, TARGET_CPU_FEATURE_X86_CX16, TARGET_CPU_FEATURE_X86_AVX512CD,
            TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA, TARGET_CPU_FEATURE_X86_AVX512PF,
            TARGET_CPU_FEATURE_X86_AVX512ER, TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2,
            TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512BITALG, TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ,
            TARGET_CPU_FEATURE_X86_AVX5124VNNIW, TARGET_CPU_FEATURE_X86_AVX5124FMAPS, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT,
            TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512FP16, TARGET_CPU_FEATURE_X86_GFNI,
            TARGET_CPU_FEATURE_X86_VAES, TARGET_CPU_FEATURE_X86_VPCLMULQDQ, TARGET_CPU_FEATURE_X86_AES,
            TARGET_CPU_FEATURE_X86_PCLMUL, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF,
            TARGET_CPU_FEATURE_X86_AMX_TILE, TARGET_CPU_FEATURE_X86_AMX_INT8, TARGET_CPU_FEATURE_X86_AMX_BF16,
            TARGET_CPU_FEATURE_X86_AMX_FP16, TARGET_CPU_FEATURE_X86_AMX_COMPLEX, TARGET_CPU_FEATURE_X86_AMX_FP8,
            TARGET_CPU_FEATURE_X86_AMX_AVX512, TARGET_CPU_FEATURE_X86_AMX_MOVRS, TARGET_CPU_FEATURE_X86_AVX_VNNI,
            TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16, TARGET_CPU_FEATURE_X86_AVX_IFMA,
            TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT, TARGET_CPU_FEATURE_X86_MOVRS, TARGET_CPU_FEATURE_X86_3DNOW,
            TARGET_CPU_FEATURE_X86_3DNOWA, TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_LWP,
            TARGET_CPU_FEATURE_X86_TBM, TARGET_CPU_FEATURE_X86_XOP, TARGET_CPU_FEATURE_X86_IBT,
            TARGET_CPU_FEATURE_X86_CLDEMOTE, TARGET_CPU_FEATURE_X86_PREFETCHI, TARGET_CPU_FEATURE_X86_SHSTK,
            TARGET_CPU_FEATURE_X86_SSE4A,
            TARGET_CPU_FEATURE_X86_VMX, TARGET_CPU_FEATURE_X86_SVM, TARGET_CPU_FEATURE_X86_ENQCMD,
            TARGET_CPU_FEATURE_X86_FRED, TARGET_CPU_FEATURE_X86_HRESET, TARGET_CPU_FEATURE_X86_INVLPGB,
            TARGET_CPU_FEATURE_X86_INVPCID, TARGET_CPU_FEATURE_X86_KEYLOCKER, TARGET_CPU_FEATURE_X86_LKGS,
            TARGET_CPU_FEATURE_X86_MSR_IMM, TARGET_CPU_FEATURE_X86_MSRLIST, TARGET_CPU_FEATURE_X86_MONITOR,
            TARGET_CPU_FEATURE_X86_MOVDIR64B, TARGET_CPU_FEATURE_X86_PBNDKB, TARGET_CPU_FEATURE_X86_PCONFIG, TARGET_CPU_FEATURE_X86_SMAP,
            TARGET_CPU_FEATURE_X86_SGX, TARGET_CPU_FEATURE_X86_SNP, TARGET_CPU_FEATURE_X86_TDX,
            TARGET_CPU_FEATURE_X86_WBNOINVD, TARGET_CPU_FEATURE_X86_WRMSRNS, TARGET_CPU_FEATURE_X86_XSAVE,
            TARGET_CPU_FEATURE_X86_XSAVES, TARGET_CPU_FEATURE_X86_ACE_1,
            TARGET_CPU_FEATURE_X86_F16C, TARGET_CPU_FEATURE_X86_FMA, TARGET_CPU_FEATURE_X86_SSSE3,
            TARGET_CPU_FEATURE_X86_SSE4_1, TARGET_CPU_FEATURE_X86_SSE4_2, TARGET_CPU_FEATURE_X86_BMI2,
            TARGET_CPU_FEATURE_X86_ADX, TARGET_CPU_FEATURE_X86_MOVBE, TARGET_CPU_FEATURE_X86_RDRAND,
            TARGET_CPU_FEATURE_X86_RDSEED, TARGET_CPU_FEATURE_X86_SHA, TARGET_CPU_FEATURE_X86_SHA512,
            TARGET_CPU_FEATURE_X86_SM3, TARGET_CPU_FEATURE_X86_SM4, TARGET_CPU_FEATURE_X86_WAITPKG, TARGET_CPU_FEATURE_X86_PKU,
            TARGET_CPU_FEATURE_X86_PTWRITE, TARGET_CPU_FEATURE_X86_SERIALIZE, TARGET_CPU_FEATURE_X86_CLFLUSHOPT,
            TARGET_CPU_FEATURE_X86_CLWB, TARGET_CPU_FEATURE_X86_FSGSBASE, TARGET_CPU_FEATURE_X86_RTM,
            TARGET_CPU_FEATURE_X86_TSXLDTRK, TARGET_CPU_FEATURE_X86_UINTR, TARGET_CPU_FEATURE_X86_PREFETCHWT1,
        };
        TargetCpuFeatures known = target_cpu_features_from_array(known_feature_list, (u32)BUSTER_ARRAY_LENGTH(known_feature_list));
        if (!target_cpu_features_subset(features, known) || !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE2))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_XSAVES) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_XSAVE))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX2) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX))
        {
            return false;
        }
        TargetCpuFeatures fma4_or_xop = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_FMA4, TARGET_CPU_FEATURE_X86_XOP}, 2);
        if (target_cpu_features_any(target_cpu_features_intersection(features, fma4_or_xop)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_3DNOWA) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_3DNOW))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE3) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE2))
        {
            return false;
        }
        TargetCpuFeatures sse2_dependent_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SSSE3, TARGET_CPU_FEATURE_X86_SSE4_1, TARGET_CPU_FEATURE_X86_SSE4_2,
            TARGET_CPU_FEATURE_X86_SHA}, 4);
        if (target_cpu_features_any(target_cpu_features_intersection(features, sse2_dependent_features)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE2))
        {
            return false;
        }
        TargetCpuFeatures avx_dependent_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_F16C, TARGET_CPU_FEATURE_X86_FMA, TARGET_CPU_FEATURE_X86_SM3}, 3);
        if (target_cpu_features_any(target_cpu_features_intersection(features, avx_dependent_features)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX))
        {
            return false;
        }
        TargetCpuFeatures crypto_avx2_dependent_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_SHA512, TARGET_CPU_FEATURE_X86_SM4}, 2);
        if (target_cpu_features_any(target_cpu_features_intersection(features, crypto_avx2_dependent_features)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX2))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE4A) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE3))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX512F) &&
            (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX) ||
             !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX2)))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX512VL) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX512F))
        {
            if (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_1))
            {
                return false;
            }
        }
        TargetCpuFeatures avx512f_or_avx10_512 = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_512}, 2);
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX512BW) &&
            !target_cpu_features_any(target_cpu_features_intersection(features, avx512f_or_avx10_512)))
        {
            if (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_1))
            {
                return false;
            }
        }
        TargetCpuFeatures avx512_subfeatures = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX512CD, TARGET_CPU_FEATURE_X86_AVX512DQ, TARGET_CPU_FEATURE_X86_AVX512IFMA,
            TARGET_CPU_FEATURE_X86_AVX512PF, TARGET_CPU_FEATURE_X86_AVX512ER, TARGET_CPU_FEATURE_X86_AVX512VBMI,
            TARGET_CPU_FEATURE_X86_AVX512VBMI2, TARGET_CPU_FEATURE_X86_AVX512VNNI, TARGET_CPU_FEATURE_X86_AVX512BITALG,
            TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ, TARGET_CPU_FEATURE_X86_AVX5124VNNIW,
            TARGET_CPU_FEATURE_X86_AVX5124FMAPS, TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT,
            TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512FP16}, 15);
        TargetCpuFeatures avx512f_or_avx10_1 = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_1}, 2);
        if (target_cpu_features_any(target_cpu_features_intersection(features, avx512_subfeatures)) &&
            !target_cpu_features_any(target_cpu_features_intersection(features, avx512f_or_avx10_1)))
        {
            return false;
        }
        TargetCpuFeatures avx512_bw_subfeatures = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX512VBMI, TARGET_CPU_FEATURE_X86_AVX512VBMI2,
            TARGET_CPU_FEATURE_X86_AVX512BF16, TARGET_CPU_FEATURE_X86_AVX512BITALG,
            TARGET_CPU_FEATURE_X86_AVX512FP16}, 5);
        if (target_cpu_features_any(target_cpu_features_intersection(features, avx512_bw_subfeatures)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX512BW))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_GFNI) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE2))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_VAES) &&
            (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX2) ||
             !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AES)))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_VPCLMULQDQ) &&
            (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX) ||
             !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_PCLMUL)))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_1) &&
            (!target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX2) ||
             !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX512F) ||
             !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_512)))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_2) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_1))
        {
            return false;
        }
        // Native decoding conservatively under-reports malformed hardware pairs;
        // explicit synthetic targets must obey the same AVX10 co-enumeration rule.
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_2) !=
            target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_V1_AUX))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_512) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX10_1))
        {
            return false;
        }
        // APX-F and APX_NCI_NDD_NF are likewise a co-enumerated hardware pair.
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_APX) !=
            target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF))
        {
            return false;
        }
        TargetCpuFeatures avx2_dependent_features = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AVX_VNNI, TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8,
            TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16, TARGET_CPU_FEATURE_X86_AVX_IFMA,
            TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT}, 5);
        if (target_cpu_features_any(target_cpu_features_intersection(features, avx2_dependent_features)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX2))
        {
            return false;
        }
        TargetCpuFeatures amx_subfeatures = target_cpu_features_from_array((TargetCpuFeature const[]){
            TARGET_CPU_FEATURE_X86_AMX_INT8, TARGET_CPU_FEATURE_X86_AMX_BF16, TARGET_CPU_FEATURE_X86_AMX_FP16,
            TARGET_CPU_FEATURE_X86_AMX_COMPLEX, TARGET_CPU_FEATURE_X86_AMX_FP8,
            TARGET_CPU_FEATURE_X86_AMX_AVX512, TARGET_CPU_FEATURE_X86_AMX_MOVRS}, 7);
        if (target_cpu_features_any(target_cpu_features_intersection(features, amx_subfeatures)) &&
            !target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AMX_TILE))
        {
            return false;
        }
        if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AMX_AVX512) &&
            !target_cpu_features_any(target_cpu_features_intersection(features, avx512f_or_avx10_1)))
        {
            return false;
        }
    }

    return true;
}

TargetCpuFeatures target_cpu_features_effective(Target target)
{
    TargetCpuFeatures result;
    if (target.cpu_features_explicit)
    {
        result = target.cpu_features;
    }
    else if (target.cpu_model == CPU_MODEL_NATIVE && target.cpu_arch == target_native.cpu_arch && target_native.cpu_features_explicit)
    {
        result = target_native.cpu_features;
    }
    else
    {
        result = target_cpu_features_default(target.cpu_arch, target.cpu_model);
    }

    return result;
}

bool target_cpu_feature_has(Target target, TargetCpuFeature feature)
{
    return target_cpu_features_contains(target_cpu_features_effective(target), feature);
}

typedef struct TargetCpuFeatureName TargetCpuFeatureName;
struct TargetCpuFeatureName
{
    String8 name;
    TargetCpuFeature feature;
    CpuArch arch;
};

// Kept in nondecreasing bytewise name order so verbose output is stable
// without sorting or allocating one node per feature.  Names can repeat when
// separate architectures expose the same spelling (for example, `aes`).
BUSTER_GLOBAL_LOCAL TargetCpuFeatureName const target_cpu_feature_names[] = {
    {.name = S8_INITIALIZER("3dnow"), .feature = TARGET_CPU_FEATURE_X86_3DNOW, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("3dnowa"), .feature = TARGET_CPU_FEATURE_X86_3DNOWA, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("ace-1"), .feature = TARGET_CPU_FEATURE_X86_ACE_1, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("adx"), .feature = TARGET_CPU_FEATURE_X86_ADX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("aes"), .feature = TARGET_CPU_FEATURE_AARCH64_AES, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("aes"), .feature = TARGET_CPU_FEATURE_X86_AES, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("altnzcv"), .feature = TARGET_CPU_FEATURE_AARCH64_ALTNZCV, .arch = CPU_ARCH_AARCH64},
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
    {.name = S8_INITIALIZER("bmi2"), .feature = TARGET_CPU_FEATURE_X86_BMI2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("ccdp"), .feature = TARGET_CPU_FEATURE_AARCH64_CCDP, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("ccpp"), .feature = TARGET_CPU_FEATURE_AARCH64_CCPP, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("cldemote"), .feature = TARGET_CPU_FEATURE_X86_CLDEMOTE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("clflushopt"), .feature = TARGET_CPU_FEATURE_X86_CLFLUSHOPT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("clwb"), .feature = TARGET_CPU_FEATURE_X86_CLWB, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("complxnum"), .feature = TARGET_CPU_FEATURE_AARCH64_COMPLXNUM, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("crc"), .feature = TARGET_CPU_FEATURE_AARCH64_CRC, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("cx16"), .feature = TARGET_CPU_FEATURE_X86_CX16, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("dotprod"), .feature = TARGET_CPU_FEATURE_AARCH64_DOTPROD, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("enqcmd"), .feature = TARGET_CPU_FEATURE_X86_ENQCMD, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("f16c"), .feature = TARGET_CPU_FEATURE_X86_F16C, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("flagm"), .feature = TARGET_CPU_FEATURE_AARCH64_FLAGM, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("fma"), .feature = TARGET_CPU_FEATURE_X86_FMA, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("fma4"), .feature = TARGET_CPU_FEATURE_X86_FMA4, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("fp-armv8"), .feature = TARGET_CPU_FEATURE_AARCH64_FP_ARMV8, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("fp16fml"), .feature = TARGET_CPU_FEATURE_AARCH64_FP16FML, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("fptoint"), .feature = TARGET_CPU_FEATURE_AARCH64_FPTOINT, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("fred"), .feature = TARGET_CPU_FEATURE_X86_FRED, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("fsgsbase"), .feature = TARGET_CPU_FEATURE_X86_FSGSBASE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("fullfp16"), .feature = TARGET_CPU_FEATURE_AARCH64_FULLFP16, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("gfni"), .feature = TARGET_CPU_FEATURE_X86_GFNI, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("hreset"), .feature = TARGET_CPU_FEATURE_X86_HRESET, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("ibt"), .feature = TARGET_CPU_FEATURE_X86_IBT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("invlpgb"), .feature = TARGET_CPU_FEATURE_X86_INVLPGB, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("invpcid"), .feature = TARGET_CPU_FEATURE_X86_INVPCID, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("jsconv"), .feature = TARGET_CPU_FEATURE_AARCH64_JSCONV, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("keylocker"), .feature = TARGET_CPU_FEATURE_X86_KEYLOCKER, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("lkgs"), .feature = TARGET_CPU_FEATURE_X86_LKGS, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("lor"), .feature = TARGET_CPU_FEATURE_AARCH64_LOR, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("lse"), .feature = TARGET_CPU_FEATURE_AARCH64_LSE, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("lwp"), .feature = TARGET_CPU_FEATURE_X86_LWP, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("lzcnt"), .feature = TARGET_CPU_FEATURE_X86_LZCNT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("monitor"), .feature = TARGET_CPU_FEATURE_X86_MONITOR, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("movbe"), .feature = TARGET_CPU_FEATURE_X86_MOVBE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("movdir64b"), .feature = TARGET_CPU_FEATURE_X86_MOVDIR64B, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("movrs"), .feature = TARGET_CPU_FEATURE_X86_MOVRS, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("msr-imm"), .feature = TARGET_CPU_FEATURE_X86_MSR_IMM, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("msrlist"), .feature = TARGET_CPU_FEATURE_X86_MSRLIST, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("neon"), .feature = TARGET_CPU_FEATURE_AARCH64_NEON, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("pauth"), .feature = TARGET_CPU_FEATURE_AARCH64_PAUTH, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("pbndkb"), .feature = TARGET_CPU_FEATURE_X86_PBNDKB, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("pclmul"), .feature = TARGET_CPU_FEATURE_X86_PCLMUL, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("pconfig"), .feature = TARGET_CPU_FEATURE_X86_PCONFIG, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("perfmon"), .feature = TARGET_CPU_FEATURE_AARCH64_PERFMON, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("pku"), .feature = TARGET_CPU_FEATURE_X86_PKU, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("popcnt"), .feature = TARGET_CPU_FEATURE_X86_POPCNT, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("predres"), .feature = TARGET_CPU_FEATURE_AARCH64_PREDRES, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("prefetchi"), .feature = TARGET_CPU_FEATURE_X86_PREFETCHI, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("prefetchwt1"), .feature = TARGET_CPU_FEATURE_X86_PREFETCHWT1, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("ptwrite"), .feature = TARGET_CPU_FEATURE_X86_PTWRITE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("ras"), .feature = TARGET_CPU_FEATURE_AARCH64_RAS, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("rcpc"), .feature = TARGET_CPU_FEATURE_AARCH64_RCPC, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("rcpc-immo"), .feature = TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("rdm"), .feature = TARGET_CPU_FEATURE_AARCH64_RDM, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("rdrand"), .feature = TARGET_CPU_FEATURE_X86_RDRAND, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("rdseed"), .feature = TARGET_CPU_FEATURE_X86_RDSEED, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("rtm"), .feature = TARGET_CPU_FEATURE_X86_RTM, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sb"), .feature = TARGET_CPU_FEATURE_AARCH64_SB, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("serialize"), .feature = TARGET_CPU_FEATURE_X86_SERIALIZE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sgx"), .feature = TARGET_CPU_FEATURE_X86_SGX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sha"), .feature = TARGET_CPU_FEATURE_X86_SHA, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sha2"), .feature = TARGET_CPU_FEATURE_AARCH64_SHA2, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("sha3"), .feature = TARGET_CPU_FEATURE_AARCH64_SHA3, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("sha512"), .feature = TARGET_CPU_FEATURE_X86_SHA512, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("shstk"), .feature = TARGET_CPU_FEATURE_X86_SHSTK, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sm3"), .feature = TARGET_CPU_FEATURE_X86_SM3, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sm4"), .feature = TARGET_CPU_FEATURE_X86_SM4, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("smap"), .feature = TARGET_CPU_FEATURE_X86_SMAP, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sme"), .feature = TARGET_CPU_FEATURE_AARCH64_SME, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("snp"), .feature = TARGET_CPU_FEATURE_X86_SNP, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("specrestrict"), .feature = TARGET_CPU_FEATURE_AARCH64_SPECRESTRICT, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("ssbs"), .feature = TARGET_CPU_FEATURE_AARCH64_SSBS, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("sse2"), .feature = TARGET_CPU_FEATURE_X86_SSE2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sse3"), .feature = TARGET_CPU_FEATURE_X86_SSE3, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sse4.1"), .feature = TARGET_CPU_FEATURE_X86_SSE4_1, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sse4.2"), .feature = TARGET_CPU_FEATURE_X86_SSE4_2, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("sse4a"), .feature = TARGET_CPU_FEATURE_X86_SSE4A, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("ssse3"), .feature = TARGET_CPU_FEATURE_X86_SSSE3, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("svm"), .feature = TARGET_CPU_FEATURE_X86_SVM, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("tbm"), .feature = TARGET_CPU_FEATURE_X86_TBM, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("tdx"), .feature = TARGET_CPU_FEATURE_X86_TDX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("tracev8.4"), .feature = TARGET_CPU_FEATURE_AARCH64_TRACEV8_4, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("tsxldtrk"), .feature = TARGET_CPU_FEATURE_X86_TSXLDTRK, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("uintr"), .feature = TARGET_CPU_FEATURE_X86_UINTR, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("v8.4a"), .feature = TARGET_CPU_FEATURE_AARCH64_V8_4A, .arch = CPU_ARCH_AARCH64},
    {.name = S8_INITIALIZER("vaes"), .feature = TARGET_CPU_FEATURE_X86_VAES, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("vmx"), .feature = TARGET_CPU_FEATURE_X86_VMX, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("vpclmulqdq"), .feature = TARGET_CPU_FEATURE_X86_VPCLMULQDQ, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("waitpkg"), .feature = TARGET_CPU_FEATURE_X86_WAITPKG, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("wbnoinvd"), .feature = TARGET_CPU_FEATURE_X86_WBNOINVD, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("wrmsrns"), .feature = TARGET_CPU_FEATURE_X86_WRMSRNS, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("xop"), .feature = TARGET_CPU_FEATURE_X86_XOP, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("xsave"), .feature = TARGET_CPU_FEATURE_X86_XSAVE, .arch = CPU_ARCH_X86_64},
    {.name = S8_INITIALIZER("xsaves"), .feature = TARGET_CPU_FEATURE_X86_XSAVES, .arch = CPU_ARCH_X86_64},
};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(target_cpu_feature_names) == (u32)TARGET_CPU_FEATURE_COUNT - 2);

BUSTER_GLOBAL_LOCAL s32 target_cpu_feature_name_compare(String8 left, String8 right)
{
    u64 length = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; index < length; index += 1)
    {
        if (left.pointer[index] < right.pointer[index]) return -1;
        if (left.pointer[index] > right.pointer[index]) return 1;
    }
    s32 result;
    if (left.length < right.length)
    {
        result = -1;
    }
    else if (left.length > right.length)
    {
        result = 1;
    }
    else
    {
        result = 0;
    }

    return result;
}

bool target_cpu_feature_names_are_sorted(void)
{
    for (u32 index = 1; index < BUSTER_ARRAY_LENGTH(target_cpu_feature_names); index += 1)
    {
        if (target_cpu_feature_name_compare(target_cpu_feature_names[index - 1].name, target_cpu_feature_names[index].name) > 0)
            return false;
    }
    return true;
}

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
        if (entry.arch == target.cpu_arch && target_cpu_features_contains(features, entry.feature))
        {
            if (part_count)
            {
                parts[part_count++] = S8(",");
            }
            parts[part_count++] = entry.name;
        }
    }
    String8 result;
    if (!part_count)
    {
        result = S8("none");
    }
    else
    {
        result = string_join_arena(arena, (SliceString8){.pointer = parts, .length = part_count}, false);
    }

    return result;
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
    TargetCpuFeatures wide_vector_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX10_1,
        TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_512}, 4);
    u32 result;
    if (target_cpu_features_any(target_cpu_features_intersection(features, wide_vector_features)))
    {
        result = 64;
    }
    else if (target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_AVX))
    {
        result = 32;
    }
    else
    {
        result = target_cpu_features_contains(features, TARGET_CPU_FEATURE_X86_SSE2) ? 16 : 0;
    }

    return result;
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
    String8 result;
    switch (arch)
    {
    case CPU_ARCH_X86_64:
        result = S8("x86_64");
        break;
    case CPU_ARCH_AARCH64:
        result = S8("aarch64");
        break;
    case CPU_ARCH_WASM64:
        result = S8("wasm64");
        break;
    case CPU_ARCH_BPFEL:
        result = S8("bpfel");
        break;
    default:
        result = S8("");
        break;
    }

    return result;
}

String8 cpu_brand_string_os(char8* buffer, u64 capacity)
{
#if BUSTER_CPU_ARCH_X86_64
    String8 result = x86_64_cpu_brand_string(buffer, capacity);
#elif BUSTER_CPU_ARCH_AARCH64
    String8 result = aarch64_cpu_brand_string(buffer, capacity);
#else
    String8 result = { .pointer = buffer };
    BUSTER_UNUSED(capacity);
#endif
    while (result.length && (result.pointer[result.length - 1] == 0 || result.pointer[result.length - 1] == ' '))
    {
        result.length -= 1;
    }
    while (result.length && result.pointer[0] == ' ')
    {
        result.pointer += 1;
        result.length -= 1;
    }
    return result;
}

String8 operating_system_to_string_os(OperatingSystem os)
{
    String8 result;
    switch (os)
    {
    case OPERATING_SYSTEM_LINUX:
        result = S8("linux");
        break;
    case OPERATING_SYSTEM_MACOS:
        result = S8("macos");
        break;
    case OPERATING_SYSTEM_WINDOWS:
        result = S8("windows");
        break;
    case OPERATING_SYSTEM_UEFI:
        result = S8("uefi");
        break;
    case OPERATING_SYSTEM_ANDROID:
        result = S8("android");
        break;
    case OPERATING_SYSTEM_IOS:
        result = S8("ios");
        break;
    case OPERATING_SYSTEM_FREESTANDING:
        result = S8("freestanding");
        break;
    case OPERATING_SYSTEM_COUNT:
        BUSTER_UNREACHABLE();
    }

    return result;
}

String8 cpu_model_to_string_os(CpuModel model)
{
    String8 result;
    switch (model)
    {
    case CPU_MODEL_COUNT:
        BUSTER_UNREACHABLE();
        break;
    case CPU_MODEL_ERROR:
        result = S8("error");
        break;
    case CPU_MODEL_BASELINE:
        result = S8("baseline");
        break;
    case CPU_MODEL_NATIVE:
        result = S8("native");
        break;
    case CPU_MODEL_AMD_I486:
        result = S8("i486");
        break;
    case CPU_MODEL_AMD_PENTIUM:
        result = S8("pentium");
        break;
    case CPU_MODEL_AMD_K6:
        result = S8("k6");
        break;
    case CPU_MODEL_AMD_K6_2:
        result = S8("k6-2");
        break;
    case CPU_MODEL_AMD_K6_3:
        result = S8("k6-3");
        break;
    case CPU_MODEL_AMD_GEODE:
        result = S8("geode");
        break;
    case CPU_MODEL_AMD_ATHLON:
        result = S8("athlon");
        break;
    case CPU_MODEL_AMD_ATHLON_XP:
        result = S8("athlon-xp");
        break;
    case CPU_MODEL_AMD_K8:
        result = S8("k8");
        break;
    case CPU_MODEL_AMD_K8_SSE3:
        result = S8("k8-sse3");
        break;
    case CPU_MODEL_AMD_AMD_FAMILY_10:
        result = S8("amdfam10");
        break;
    case CPU_MODEL_AMD_BT_1:
        result = S8("btver1");
        break;
    case CPU_MODEL_AMD_BT_2:
        result = S8("btver2");
        break;
    case CPU_MODEL_AMD_BD_1:
        result = S8("bdver1");
        break;
    case CPU_MODEL_AMD_BD_2:
        result = S8("bdver2");
        break;
    case CPU_MODEL_AMD_BD_3:
        result = S8("bdver3");
        break;
    case CPU_MODEL_AMD_BD_4:
        result = S8("bdver4");
        break;
    case CPU_MODEL_AMD_ZEN_1:
        result = S8("znver1");
        break;
    case CPU_MODEL_AMD_ZEN_2:
        result = S8("znver2");
        break;
    case CPU_MODEL_AMD_ZEN_3:
        result = S8("znver3");
        break;
    case CPU_MODEL_AMD_ZEN_4:
        result = S8("znver4");
        break;
    case CPU_MODEL_AMD_ZEN_5:
        result = S8("znver5");
        break;
    case CPU_MODEL_INTEL_CORE_2:
        result = S8("core2");
        break;
    case CPU_MODEL_INTEL_PENRYN:
        result = S8("penryn");
        break;
    case CPU_MODEL_INTEL_NEHALEM:
        result = S8("nehalem");
        break;
    case CPU_MODEL_INTEL_WESTMERE:
        result = S8("westmere");
        break;
    case CPU_MODEL_INTEL_SANDY_BRIDGE:
        result = S8("sandybridge");
        break;
    case CPU_MODEL_INTEL_IVY_BRIDGE:
        result = S8("ivybridge");
        break;
    case CPU_MODEL_INTEL_HASWELL:
        result = S8("haswell");
        break;
    case CPU_MODEL_INTEL_BROADWELL:
        result = S8("broadwell");
        break;
    case CPU_MODEL_INTEL_SKYLAKE:
        result = S8("skylake");
        break;
    case CPU_MODEL_INTEL_SKYLAKE_AVX512:
        result = S8("skylake-avx512");
        break;
    case CPU_MODEL_INTEL_ROCKETLAKE:
        result = S8("rocketlake");
        break;
    case CPU_MODEL_INTEL_COOPERLAKE:
        result = S8("cooperlake");
        break;
    case CPU_MODEL_INTEL_CASCADELAKE:
        result = S8("cascadelake");
        break;
    case CPU_MODEL_INTEL_CANNONLAKE:
        result = S8("cannonlake");
        break;
    case CPU_MODEL_INTEL_ICELAKE_CLIENT:
        result = S8("icelake-client");
        break;
    case CPU_MODEL_INTEL_TIGERLAKE:
        result = S8("tigerlake");
        break;
    case CPU_MODEL_INTEL_ALDERLAKE:
        result = S8("alderlake");
        break;
    case CPU_MODEL_INTEL_RAPTORLAKE:
        result = S8("raptorlake");
        break;
    case CPU_MODEL_INTEL_METEORLAKE:
        result = S8("meteorlake");
        break;
    case CPU_MODEL_INTEL_GRACEMONT:
        result = S8("gracemont");
        break;
    case CPU_MODEL_INTEL_ARROWLAKE:
        result = S8("arrowlake");
        break;
    case CPU_MODEL_INTEL_ARROWLAKE_S:
        result = S8("arrowlake-s");
        break;
    case CPU_MODEL_INTEL_LUNARLAKE:
        result = S8("lunarlake");
        break;
    case CPU_MODEL_INTEL_PANTHERLAKE:
        result = S8("pantherlake");
        break;
    case CPU_MODEL_INTEL_ICELAKE_SERVER:
        result = S8("icelake-server");
        break;
    case CPU_MODEL_INTEL_EMERALD_RAPIDS:
        result = S8("emeraldrapids");
        break;
    case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS:
        result = S8("sapphirerapids");
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS:
        result = S8("graniterapids");
        break;
    case CPU_MODEL_INTEL_GRANITE_RAPIDS_D:
        result = S8("graniterapids-d");
        break;
    case CPU_MODEL_INTEL_BONNELL:
        result = S8("bonnell");
        break;
    case CPU_MODEL_INTEL_SILVERMONT:
        result = S8("silvermont");
        break;
    case CPU_MODEL_INTEL_GOLDMONT:
        result = S8("goldmont");
        break;
    case CPU_MODEL_INTEL_GOLDMONT_PLUS:
        result = S8("goldmont-plus");
        break;
    case CPU_MODEL_INTEL_TREMONT:
        result = S8("tremont");
        break;
    case CPU_MODEL_INTEL_SIERRAFOREST:
        result = S8("sierraforest");
        break;
    case CPU_MODEL_INTEL_GRANDRIDGE:
        result = S8("grandridge");
        break;
    case CPU_MODEL_INTEL_CLEARWATERFOREST:
        result = S8("clearwaterforest");
        break;
    case CPU_MODEL_INTEL_KNL:
        result = S8("knl");
        break;
    case CPU_MODEL_INTEL_KNM:
        result = S8("knm");
        break;
    case CPU_MODEL_INTEL_DIAMOND_RAPIDS:
        result = S8("diamondrapids");
        break;
    case CPU_MODEL_A64_GENERIC:
        result = S8("generic");
        break;
    case CPU_MODEL_A64_ARM_ARM926EJ_S:
        result = S8("arm926ej-s");
        break;
    case CPU_MODEL_A64_ARM_MPCORE:
        result = S8("mpcore");
        break;
    case CPU_MODEL_A64_ARM_ARM1136J_S:
        result = S8("arm1136j-s");
        break;
    case CPU_MODEL_A64_ARM_ARM1156T2_S:
        result = S8("arm1156t2-s");
        break;
    case CPU_MODEL_A64_ARM_ARM1176JZ_S:
        result = S8("arm1176jz-s");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A5:
        result = S8("cortex-a5");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A7:
        result = S8("cortex-a7");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A8:
        result = S8("cortex-a8");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A9:
        result = S8("cortex-a9");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A15:
        result = S8("cortex-a15");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A17:
        result = S8("cortex-a17");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M0:
        result = S8("cortex-m0");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M3:
        result = S8("cortex-m3");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M4:
        result = S8("cortex-m4");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M7:
        result = S8("cortex-m7");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M23:
        result = S8("cortex-m23");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M33:
        result = S8("cortex-m33");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M52:
        result = S8("cortex-m52");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M55:
        result = S8("cortex-m55");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_M85:
        result = S8("cortex-m85");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R8:
        result = S8("cortex-r8");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R52:
        result = S8("cortex-r52");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R52PLUS:
        result = S8("cortex-r52plus");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R82:
        result = S8("cortex-r82");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_R82AE:
        result = S8("cortex-r82ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A34:
        result = S8("cortex-a34");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A35:
        result = S8("cortex-a35");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A320:
        result = S8("cortex-a320");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A53:
        result = S8("cortex-a53");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A55:
        result = S8("cortex-a55");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A510:
        result = S8("cortex-a510");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A520:
        result = S8("cortex-a520");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A520AE:
        result = S8("cortex-a520ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A57:
        result = S8("cortex-a57");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A65:
        result = S8("cortex-a65");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A65AE:
        result = S8("cortex-a65ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A72:
        result = S8("cortex-a72");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A73:
        result = S8("cortex-a73");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A75:
        result = S8("cortex-a75");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A76:
        result = S8("cortex-a76");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A76AE:
        result = S8("cortex-a76ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A77:
        result = S8("cortex-a77");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A78:
        result = S8("cortex-a78");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A78AE:
        result = S8("cortex-a78ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A78C:
        result = S8("cortex-a78c");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A710:
        result = S8("cortex-a710");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A715:
        result = S8("cortex-a715");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A720:
        result = S8("cortex-a720");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A720AE:
        result = S8("cortex-a720ae");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_A725:
        result = S8("cortex-a725");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X1:
        result = S8("cortex-x1");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X1C:
        result = S8("cortex-x1c");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X2:
        result = S8("cortex-x2");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X3:
        result = S8("cortex-x3");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X4:
        result = S8("cortex-x4");
        break;
    case CPU_MODEL_A64_ARM_CORTEX_X925:
        result = S8("cortex-x925");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_E1:
        result = S8("neoverse-e1");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_N1:
        result = S8("neoverse-n1");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_N2:
        result = S8("neoverse-n2");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_N3:
        result = S8("neoverse-n3");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V1:
        result = S8("neoverse-v1");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V2:
        result = S8("neoverse-v2");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V3:
        result = S8("neoverse-v3");
        break;
    case CPU_MODEL_A64_ARM_NEOVERSE_V3AE:
        result = S8("neoverse-v3ae");
        break;
    case CPU_MODEL_A64_ARM_ARM920T:
        result = S8("arm920t");
        break;
    case CPU_MODEL_A64_ARM_XSCALE:
        result = S8("xscale");
        break;
    case CPU_MODEL_A64_ARM_SWIFT:
        result = S8("swift");
        break;
    case CPU_MODEL_A64_ARM920T:
        result = S8("arm920t");
        break;
    case CPU_MODEL_A64_APPLE_A7:
        result = S8("apple-a7");
        break;
    case CPU_MODEL_A64_APPLE_A8:
        result = S8("apple-a8");
        break;
    case CPU_MODEL_A64_APPLE_A9:
        result = S8("apple-a9");
        break;
    case CPU_MODEL_A64_APPLE_A10:
        result = S8("apple-a10");
        break;
    case CPU_MODEL_A64_APPLE_A11:
        result = S8("apple-a11");
        break;
    case CPU_MODEL_A64_APPLE_A12:
        result = S8("apple-a12");
        break;
    case CPU_MODEL_A64_APPLE_A13:
        result = S8("apple-a13");
        break;
    case CPU_MODEL_A64_APPLE_M1:
        result = S8("apple-m1");
        break;
    case CPU_MODEL_A64_APPLE_M2:
        result = S8("apple-m2");
        break;
    case CPU_MODEL_A64_APPLE_A17:
        result = S8("apple-a17");
        break;
    case CPU_MODEL_A64_APPLE_M3:
        result = S8("apple-m3");
        break;
    case CPU_MODEL_A64_APPLE_M4:
        result = S8("apple-m4");
        break;
    }

    return result;
}
