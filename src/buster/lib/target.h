#pragma once

#include <buster/lib/base.h>

typedef enum CpuArch
{
    CPU_ARCH_X86_64,
    CPU_ARCH_AARCH64,
    CPU_ARCH_COUNT,
} CpuArch;

typedef enum OperatingSystem
{
    OPERATING_SYSTEM_LINUX,
    OPERATING_SYSTEM_MACOS,
    OPERATING_SYSTEM_WINDOWS,
    OPERATING_SYSTEM_UEFI,
    OPERATING_SYSTEM_ANDROID,
    OPERATING_SYSTEM_IOS,
    OPERATING_SYSTEM_FREESTANDING,
    OPERATING_SYSTEM_COUNT,
} OperatingSystem;

typedef enum CpuModel
{
    CPU_MODEL_ERROR,
    CPU_MODEL_BASELINE,
    CPU_MODEL_NATIVE,
    CPU_MODEL_AMD_I486,
    CPU_MODEL_AMD_PENTIUM,
    CPU_MODEL_AMD_K6,
    CPU_MODEL_AMD_K6_2,
    CPU_MODEL_AMD_K6_3,
    CPU_MODEL_AMD_GEODE,
    CPU_MODEL_AMD_ATHLON,
    CPU_MODEL_AMD_ATHLON_XP,
    CPU_MODEL_AMD_K8,
    CPU_MODEL_AMD_K8_SSE3,
    CPU_MODEL_AMD_AMD_FAMILY_10,
    CPU_MODEL_AMD_BT_1,
    CPU_MODEL_AMD_BT_2,
    CPU_MODEL_AMD_BD_1,
    CPU_MODEL_AMD_BD_2,
    CPU_MODEL_AMD_BD_3,
    CPU_MODEL_AMD_BD_4,
    CPU_MODEL_AMD_ZEN_1,
    CPU_MODEL_AMD_ZEN_2,
    CPU_MODEL_AMD_ZEN_3,
    CPU_MODEL_AMD_ZEN_4,
    CPU_MODEL_AMD_ZEN_5,

    CPU_MODEL_INTEL_CORE_2,
    CPU_MODEL_INTEL_PENRYN,
    CPU_MODEL_INTEL_NEHALEM,
    CPU_MODEL_INTEL_WESTMERE,
    CPU_MODEL_INTEL_SANDY_BRIDGE,
    CPU_MODEL_INTEL_IVY_BRIDGE,
    CPU_MODEL_INTEL_HASWELL,
    CPU_MODEL_INTEL_BROADWELL,
    CPU_MODEL_INTEL_SKYLAKE,
    CPU_MODEL_INTEL_SKYLAKE_AVX512,
    CPU_MODEL_INTEL_ROCKETLAKE,
    CPU_MODEL_INTEL_COOPERLAKE,
    CPU_MODEL_INTEL_CASCADELAKE,
    CPU_MODEL_INTEL_CANNONLAKE,
    CPU_MODEL_INTEL_ICELAKE_CLIENT,
    CPU_MODEL_INTEL_TIGERLAKE,
    CPU_MODEL_INTEL_ALDERLAKE,
    CPU_MODEL_INTEL_RAPTORLAKE,
    CPU_MODEL_INTEL_METEORLAKE,
    CPU_MODEL_INTEL_GRACEMONT,
    CPU_MODEL_INTEL_ARROWLAKE,
    CPU_MODEL_INTEL_ARROWLAKE_S,
    CPU_MODEL_INTEL_LUNARLAKE,
    CPU_MODEL_INTEL_PANTHERLAKE,
    CPU_MODEL_INTEL_ICELAKE_SERVER,
    CPU_MODEL_INTEL_EMERALD_RAPIDS,
    CPU_MODEL_INTEL_SAPPHIRE_RAPIDS,
    CPU_MODEL_INTEL_GRANITE_RAPIDS,
    CPU_MODEL_INTEL_GRANITE_RAPIDS_D,
    CPU_MODEL_INTEL_BONNELL,
    CPU_MODEL_INTEL_SILVERMONT,
    CPU_MODEL_INTEL_GOLDMONT,
    CPU_MODEL_INTEL_GOLDMONT_PLUS,
    CPU_MODEL_INTEL_TREMONT,
    CPU_MODEL_INTEL_SIERRAFOREST,
    CPU_MODEL_INTEL_GRANDRIDGE,
    CPU_MODEL_INTEL_CLEARWATERFOREST,
    CPU_MODEL_INTEL_KNL,
    CPU_MODEL_INTEL_KNM,
    CPU_MODEL_INTEL_DIAMOND_RAPIDS,

    CPU_MODEL_A64_GENERIC,

    CPU_MODEL_A64_ARM_ARM920T,
    CPU_MODEL_A64_ARM_ARM926EJ_S,
    CPU_MODEL_A64_ARM_MPCORE,
    CPU_MODEL_A64_ARM_ARM1136J_S,
    CPU_MODEL_A64_ARM_ARM1156T2_S,
    CPU_MODEL_A64_ARM_ARM1176JZ_S,
    CPU_MODEL_A64_ARM_CORTEX_A5,
    CPU_MODEL_A64_ARM_CORTEX_A7,
    CPU_MODEL_A64_ARM_CORTEX_A8,
    CPU_MODEL_A64_ARM_CORTEX_A9,
    CPU_MODEL_A64_ARM_CORTEX_A15,
    CPU_MODEL_A64_ARM_CORTEX_A17,
    CPU_MODEL_A64_ARM_CORTEX_M0,
    CPU_MODEL_A64_ARM_CORTEX_M3,
    CPU_MODEL_A64_ARM_CORTEX_M4,
    CPU_MODEL_A64_ARM_CORTEX_M7,
    CPU_MODEL_A64_ARM_CORTEX_M23,
    CPU_MODEL_A64_ARM_CORTEX_M33,
    CPU_MODEL_A64_ARM_CORTEX_M52,
    CPU_MODEL_A64_ARM_CORTEX_M55,
    CPU_MODEL_A64_ARM_CORTEX_M85,
    CPU_MODEL_A64_ARM_CORTEX_R8,
    CPU_MODEL_A64_ARM_CORTEX_R52,
    CPU_MODEL_A64_ARM_CORTEX_R52PLUS,
    CPU_MODEL_A64_ARM_CORTEX_R82,
    CPU_MODEL_A64_ARM_CORTEX_R82AE,
    CPU_MODEL_A64_ARM_CORTEX_A34,
    CPU_MODEL_A64_ARM_CORTEX_A35,
    CPU_MODEL_A64_ARM_CORTEX_A320,
    CPU_MODEL_A64_ARM_CORTEX_A53,
    CPU_MODEL_A64_ARM_CORTEX_A55,
    CPU_MODEL_A64_ARM_CORTEX_A510,
    CPU_MODEL_A64_ARM_CORTEX_A520,
    CPU_MODEL_A64_ARM_CORTEX_A520AE,
    CPU_MODEL_A64_ARM_CORTEX_A57,
    CPU_MODEL_A64_ARM_CORTEX_A65,
    CPU_MODEL_A64_ARM_CORTEX_A65AE,
    CPU_MODEL_A64_ARM_CORTEX_A72,
    CPU_MODEL_A64_ARM_CORTEX_A73,
    CPU_MODEL_A64_ARM_CORTEX_A75,
    CPU_MODEL_A64_ARM_CORTEX_A76,
    CPU_MODEL_A64_ARM_CORTEX_A76AE,
    CPU_MODEL_A64_ARM_CORTEX_A77,
    CPU_MODEL_A64_ARM_CORTEX_A78,
    CPU_MODEL_A64_ARM_CORTEX_A78AE,
    CPU_MODEL_A64_ARM_CORTEX_A78C,
    CPU_MODEL_A64_ARM_CORTEX_A710,
    CPU_MODEL_A64_ARM_CORTEX_A715,
    CPU_MODEL_A64_ARM_CORTEX_A720,
    CPU_MODEL_A64_ARM_CORTEX_A720AE,
    CPU_MODEL_A64_ARM_CORTEX_A725,
    CPU_MODEL_A64_ARM_CORTEX_X1,
    CPU_MODEL_A64_ARM_CORTEX_X1C,
    CPU_MODEL_A64_ARM_CORTEX_X2,
    CPU_MODEL_A64_ARM_CORTEX_X3,
    CPU_MODEL_A64_ARM_CORTEX_X4,
    CPU_MODEL_A64_ARM_CORTEX_X925,
    CPU_MODEL_A64_ARM_NEOVERSE_E1,
    CPU_MODEL_A64_ARM_NEOVERSE_N1,
    CPU_MODEL_A64_ARM_NEOVERSE_N2,
    CPU_MODEL_A64_ARM_NEOVERSE_N3,
    CPU_MODEL_A64_ARM_NEOVERSE_V1,
    CPU_MODEL_A64_ARM_NEOVERSE_V2,
    CPU_MODEL_A64_ARM_NEOVERSE_V3,
    CPU_MODEL_A64_ARM_NEOVERSE_V3AE,

    CPU_MODEL_A64_ARM_XSCALE,
    CPU_MODEL_A64_ARM_SWIFT,
    CPU_MODEL_A64_ARM920T,
    CPU_MODEL_A64_APPLE_A7,
    CPU_MODEL_A64_APPLE_A8,
    CPU_MODEL_A64_APPLE_A9,
    CPU_MODEL_A64_APPLE_A10,
    CPU_MODEL_A64_APPLE_A11,
    CPU_MODEL_A64_APPLE_A12,
    CPU_MODEL_A64_APPLE_A13,
    CPU_MODEL_A64_APPLE_M1,
    CPU_MODEL_A64_APPLE_M2,
    CPU_MODEL_A64_APPLE_A17,
    CPU_MODEL_A64_APPLE_M3,
    CPU_MODEL_A64_APPLE_M4,

    CPU_MODEL_COUNT,
} CpuModel;

enum
{
    TARGET_CPU_FEATURE_BIT_CAPACITY = 256,
    TARGET_CPU_FEATURE_WORD_COUNT = TARGET_CPU_FEATURE_BIT_CAPACITY / 64,
};

typedef struct TargetCpuFeatures TargetCpuFeatures;
struct TargetCpuFeatures
{
    u64 words[TARGET_CPU_FEATURE_WORD_COUNT];
};

typedef enum TargetEndianness
{
    TARGET_ENDIAN_LITTLE,
    TARGET_ENDIAN_BIG,
    TARGET_ENDIAN_COUNT,
} TargetEndianness;

typedef struct TargetTypeLayout TargetTypeLayout;
struct TargetTypeLayout
{
    u32 size;
    u32 alignment;
    u32 bit_width;
};

// The data layout is derived from the target, rather than from the host that
// happens to run the compiler.  Keep all frontend-facing scalar and ABI
// properties here so a cross compilation cannot accidentally inherit host C
// sizes or alignment rules.
typedef struct TargetDataLayout TargetDataLayout;
struct TargetDataLayout
{
    TargetTypeLayout boolean;
    TargetTypeLayout plain_char;
    TargetTypeLayout signed_char;
    TargetTypeLayout unsigned_char;
    TargetTypeLayout short_integer;
    TargetTypeLayout unsigned_short_integer;
    TargetTypeLayout integer;
    TargetTypeLayout unsigned_integer;
    TargetTypeLayout long_integer;
    TargetTypeLayout unsigned_long_integer;
    TargetTypeLayout long_long_integer;
    TargetTypeLayout unsigned_long_long_integer;
    TargetTypeLayout integer128;
    TargetTypeLayout unsigned_integer128;
    TargetTypeLayout float_type;
    TargetTypeLayout double_type;
    TargetTypeLayout long_double_type;
    TargetTypeLayout pointer;
    TargetTypeLayout va_list;
    u32 atomic_min_width;
    u32 atomic_max_width;
    u32 atomic_alignment;
    u32 abi_stack_alignment;
    u32 abi_max_alignment;
    TargetEndianness endianness;
    bool plain_char_is_signed;
    bool has_128_bit_integer;
    u8 reserved[2];
};

typedef enum TargetCpuFeature
{
    TARGET_CPU_FEATURE_NONE,
    TARGET_CPU_FEATURE_X86_SSE2,
    TARGET_CPU_FEATURE_X86_AVX,
    TARGET_CPU_FEATURE_X86_AVX2,
    TARGET_CPU_FEATURE_X86_AVX512F,
    TARGET_CPU_FEATURE_X86_AVX512VL,
    TARGET_CPU_FEATURE_X86_AVX10_1,
    TARGET_CPU_FEATURE_X86_AVX10_2,
    TARGET_CPU_FEATURE_X86_AVX10_512,
    TARGET_CPU_FEATURE_X86_APX,
    TARGET_CPU_FEATURE_X86_AVX512BW,
    TARGET_CPU_FEATURE_AARCH64_NEON,
    TARGET_CPU_FEATURE_X86_SSE3,
    TARGET_CPU_FEATURE_X86_POPCNT,
    TARGET_CPU_FEATURE_X86_LZCNT,
    TARGET_CPU_FEATURE_X86_BMI1,
    TARGET_CPU_FEATURE_X86_CX16,
    TARGET_CPU_FEATURE_X86_AVX512CD,
    TARGET_CPU_FEATURE_X86_AVX512DQ,
    TARGET_CPU_FEATURE_X86_AVX512IFMA,
    TARGET_CPU_FEATURE_X86_AVX512PF,
    TARGET_CPU_FEATURE_X86_AVX512ER,
    TARGET_CPU_FEATURE_X86_AVX512VBMI,
    TARGET_CPU_FEATURE_X86_AVX512VBMI2,
    TARGET_CPU_FEATURE_X86_AVX512VNNI,
    TARGET_CPU_FEATURE_X86_AVX512BITALG,
    TARGET_CPU_FEATURE_X86_AVX512VPOPCNTDQ,
    TARGET_CPU_FEATURE_X86_AVX5124VNNIW,
    TARGET_CPU_FEATURE_X86_AVX5124FMAPS,
    TARGET_CPU_FEATURE_X86_AVX512VP2INTERSECT,
    TARGET_CPU_FEATURE_X86_AVX512BF16,
    TARGET_CPU_FEATURE_X86_AVX512FP16,
    TARGET_CPU_FEATURE_X86_GFNI,
    TARGET_CPU_FEATURE_X86_VAES,
    TARGET_CPU_FEATURE_X86_VPCLMULQDQ,
    TARGET_CPU_FEATURE_X86_AES,
    TARGET_CPU_FEATURE_X86_PCLMUL,
    TARGET_CPU_FEATURE_X86_AVX10_V1_AUX,
    TARGET_CPU_FEATURE_X86_APX_NCI_NDD_NF,
    TARGET_CPU_FEATURE_X86_AMX_TILE,
    TARGET_CPU_FEATURE_X86_AMX_INT8,
    TARGET_CPU_FEATURE_X86_AMX_BF16,
    TARGET_CPU_FEATURE_X86_AMX_FP16,
    TARGET_CPU_FEATURE_X86_AMX_COMPLEX,
    TARGET_CPU_FEATURE_X86_AMX_FP8,
    TARGET_CPU_FEATURE_X86_AMX_AVX512,
    TARGET_CPU_FEATURE_X86_AMX_MOVRS,
    TARGET_CPU_FEATURE_X86_AVX_VNNI,
    TARGET_CPU_FEATURE_X86_AVX_VNNI_INT8,
    TARGET_CPU_FEATURE_X86_AVX_VNNI_INT16,
    TARGET_CPU_FEATURE_X86_AVX_IFMA,
    TARGET_CPU_FEATURE_X86_AVX_NE_CONVERT,
    TARGET_CPU_FEATURE_X86_MOVRS,
    TARGET_CPU_FEATURE_X86_3DNOW,
    TARGET_CPU_FEATURE_X86_3DNOWA,
    TARGET_CPU_FEATURE_X86_FMA4,
    TARGET_CPU_FEATURE_X86_LWP,
    TARGET_CPU_FEATURE_X86_TBM,
    TARGET_CPU_FEATURE_X86_XOP,
    // CPUID.07H:EDX[20] CET indirect-branch tracking; this does not model CET shadow stacks.
    TARGET_CPU_FEATURE_X86_IBT,
    TARGET_CPU_FEATURE_X86_CLDEMOTE,
    TARGET_CPU_FEATURE_X86_PREFETCHI,
    // CPUID.07H:0.ECX[7] CET shadow stack.
    TARGET_CPU_FEATURE_X86_SHSTK,
    // Keep the identities contiguous; SVM is the first feature in the second
    // storage word so the representation is exercised by native x86 features.
    TARGET_CPU_FEATURE_X86_VMX = 64,
    TARGET_CPU_FEATURE_X86_SVM,
    TARGET_CPU_FEATURE_COUNT,
} TargetCpuFeature;

BUSTER_CT_CHECK(TARGET_CPU_FEATURE_BIT_CAPACITY >= 256);
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_BIT_CAPACITY % 64 == 0);
BUSTER_CT_CHECK(sizeof(TargetCpuFeatures) == TARGET_CPU_FEATURE_WORD_COUNT * sizeof(u64));
BUSTER_CT_CHECK(TARGET_CPU_FEATURE_COUNT <= TARGET_CPU_FEATURE_BIT_CAPACITY + 1);
BUSTER_CT_CHECK((u32)TARGET_CPU_FEATURE_X86_SVM > 64);
BUSTER_CT_CHECK((u32)TARGET_CPU_FEATURE_X86_SVM <= (u32)TARGET_CPU_FEATURE_BIT_CAPACITY);

typedef enum TargetStringComponents
{
    TARGET_STRING_COMPONENT_CPU_ARCH,
    TARGET_STRING_COMPONENT_OPERATING_SYSTEM,
    TARGET_STRING_COMPONENT_CPU_MODEL,
    TARGET_STRING_COMPONENT_COUNT,
} TargetStringComponent;

typedef struct Target Target;
struct Target
{
    CpuArch cpu_arch;
    CpuModel cpu_model;
    OperatingSystem os;
    bool cpu_features_explicit;
    u8 os_version_minor;
    u8 os_version_patch;
    u16 os_version_major;
    TargetCpuFeatures cpu_features;
};

typedef struct TargetStringSplit TargetStringSplit;
struct TargetStringSplit
{
    String8 s[(u64)TARGET_STRING_COMPONENT_COUNT];
};

typedef enum TargetParseError
{
    TARGET_PARSE_ERROR_NONE,
    TARGET_PARSE_ERROR_EMPTY,
    TARGET_PARSE_ERROR_ARCHITECTURE,
    TARGET_PARSE_ERROR_OPERATING_SYSTEM,
    TARGET_PARSE_ERROR_COUNT,
} TargetParseError;

typedef struct TargetParseResult TargetParseResult;
struct TargetParseResult
{
    String8 invalid_component;
    Target target;
    TargetParseError error;
};

BUSTER_V_DECL Target target_native;

BUSTER_F_DECL bool cpu_is_native(CpuModel model);
BUSTER_F_DECL CpuModel cpu_detect_model(void);
BUSTER_F_DECL CpuModel cpu_model_from_string(String8 string);
BUSTER_F_DECL bool cpu_model_supports_arch(CpuModel model, CpuArch arch);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_empty(void);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_singleton(TargetCpuFeature feature);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_add(TargetCpuFeatures features, TargetCpuFeature feature);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_remove(TargetCpuFeatures features, TargetCpuFeature feature);
BUSTER_F_DECL bool target_cpu_features_contains(TargetCpuFeatures features, TargetCpuFeature feature);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_union(TargetCpuFeatures left, TargetCpuFeatures right);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_intersection(TargetCpuFeatures left, TargetCpuFeatures right);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_difference(TargetCpuFeatures left, TargetCpuFeatures right);
BUSTER_F_DECL bool target_cpu_features_equal(TargetCpuFeatures left, TargetCpuFeatures right);
BUSTER_F_DECL bool target_cpu_features_any(TargetCpuFeatures features);
BUSTER_F_DECL bool target_cpu_features_subset(TargetCpuFeatures subset, TargetCpuFeatures superset);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_from_array(TargetCpuFeature const* features, u32 count);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_default(CpuArch arch, CpuModel model);
BUSTER_F_DECL TargetCpuFeatures target_cpu_features_effective(Target target);
BUSTER_F_DECL bool target_cpu_features_are_valid(Target target);
BUSTER_F_DECL bool target_cpu_feature_has(Target target, TargetCpuFeature feature);
BUSTER_F_DECL TargetCpuFeature target_cpu_feature_from_string(CpuArch arch, String8 name);
BUSTER_F_DECL String8 target_cpu_feature_to_string(TargetCpuFeature feature);
BUSTER_F_DECL String8 target_cpu_features_to_string(Arena* arena, Target target);
BUSTER_F_DECL TargetDataLayout target_data_layout(Target target);
BUSTER_F_DECL bool target_data_layout_is_valid(TargetDataLayout layout);
BUSTER_F_DECL u32 target_vector_register_size(Target target);
BUSTER_F_DECL TargetStringSplit target_to_split_string_os(Target target);
BUSTER_F_DECL String8 target_to_string(Arena* arena, Target target);
BUSTER_F_DECL String8 cpu_arch_to_string_os(CpuArch arch);
BUSTER_F_DECL String8 operating_system_to_string_os(OperatingSystem os);
BUSTER_F_DECL String8 cpu_model_to_string_os(CpuModel model);
BUSTER_F_DECL TargetParseResult target_parse_triple(String8 triple);


#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/x86_64.h>
#elif BUSTER_CPU_ARCH_AARCH64
#include <buster/lib/aarch64.h>
#endif
