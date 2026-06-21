#include <buster/target.h>

#include <buster/string.h>

#if BUSTER_UNITY_BUILD
#if BUSTER_CPU_ARCH_X86_64
#include <buster/x86_64.c>
#endif
#if BUSTER_CPU_ARCH_AARCH64
#include <buster/aarch64.c>
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
#pragma error
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
#pragma error
#endif
#endif
};

bool cpu_is_native(CpuModel model)
{
    return (model == CPU_MODEL_NATIVE) | (model == target_native.cpu_model);
}

CpuModel cpu_detect_model(void)
{
    CpuModel cpu_model = CPU_MODEL_ERROR;
#if BUSTER_CPU_ARCH_X86_64
    cpu_model = cpu_detect_model_x86_64();
#elif BUSTER_CPU_ARCH_AARCH64
    cpu_model = cpu_detect_model_aarch64();
#else
#pragma error // TODO: implement CPU detection code for this architecture
#endif
    target_native.cpu_model = cpu_model;
    return cpu_model;
}

TargetStringSplit target_to_split_string_os(Target target)
{
    String8 arch_string = cpu_arch_to_string_os(target.cpu_arch);
    String8 string_os = operating_system_to_string_os(target.os);
    String8 model_string = cpu_model_to_string_os(target.cpu_model);
    TargetStringSplit result = {
        .s = {
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
        break; case CPU_ARCH_X86_64: return S8("x86_64");
        break; case CPU_ARCH_AARCH64: return S8("aarch64");
        break; default: return S8("");
    }
}

String8 operating_system_to_string_os(OperatingSystem os)
{
    switch (os)
    {
        break; case OPERATING_SYSTEM_LINUX: return S8("linux");
        break; case OPERATING_SYSTEM_MACOS: return S8("macos");
        break; case OPERATING_SYSTEM_WINDOWS: return S8("windows");
        break; case OPERATING_SYSTEM_UEFI: return S8("uefi");
        break; case OPERATING_SYSTEM_ANDROID: return S8("android");
        break; case OPERATING_SYSTEM_IOS: return S8("ios");
        break; case OPERATING_SYSTEM_FREESTANDING: return S8("freestanding");
        break; case OPERATING_SYSTEM_COUNT: BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
    return S8("error");
}

String8 cpu_model_to_string_os(CpuModel model)
{
    switch (model)
    {
        break; case CPU_MODEL_COUNT: return S8("count"); // TODO: crash?
        break; case CPU_MODEL_ERROR: return S8("error"); // TODO: crash?
        break; case CPU_MODEL_BASELINE: return S8("baseline");
        break; case CPU_MODEL_NATIVE: return S8("native");
        break; case CPU_MODEL_AMD_I486: return S8("i486");
        break; case CPU_MODEL_AMD_PENTIUM: return S8("pentium");
        break; case CPU_MODEL_AMD_K6: return S8("k6");
        break; case CPU_MODEL_AMD_K6_2: return S8("k6-2");
        break; case CPU_MODEL_AMD_K6_3: return S8("k6-3");
        break; case CPU_MODEL_AMD_GEODE: return S8("geode");
        break; case CPU_MODEL_AMD_ATHLON: return S8("athlon");
        break; case CPU_MODEL_AMD_ATHLON_XP: return S8("athlon-xp");
        break; case CPU_MODEL_AMD_K8: return S8("k8");
        break; case CPU_MODEL_AMD_K8_SSE3: return S8("k8-sse3");
        break; case CPU_MODEL_AMD_AMD_FAMILY_10: return S8("amdfam10");
        break; case CPU_MODEL_AMD_BT_1: return S8("btver1");
        break; case CPU_MODEL_AMD_BT_2: return S8("btver2");
        break; case CPU_MODEL_AMD_BD_1: return S8("bdver1");
        break; case CPU_MODEL_AMD_BD_2: return S8("bdver2");
        break; case CPU_MODEL_AMD_BD_3: return S8("bdver3");
        break; case CPU_MODEL_AMD_BD_4: return S8("bdver4");
        break; case CPU_MODEL_AMD_ZEN_1: return S8("znver1");
        break; case CPU_MODEL_AMD_ZEN_2: return S8("znver2");
        break; case CPU_MODEL_AMD_ZEN_3: return S8("znver3");
        break; case CPU_MODEL_AMD_ZEN_4: return S8("znver4");
        break; case CPU_MODEL_AMD_ZEN_5: return S8("znver5");
        break; case CPU_MODEL_INTEL_CORE_2: return S8("core2");
        break; case CPU_MODEL_INTEL_PENRYN: return S8("penryn");
        break; case CPU_MODEL_INTEL_NEHALEM: return S8("nehalem");
        break; case CPU_MODEL_INTEL_WESTMERE: return S8("westmere");
        break; case CPU_MODEL_INTEL_SANDY_BRIDGE: return S8("sandybridge");
        break; case CPU_MODEL_INTEL_IVY_BRIDGE: return S8("ivybridge");
        break; case CPU_MODEL_INTEL_HASWELL: return S8("haswell");
        break; case CPU_MODEL_INTEL_BROADWELL: return S8("broadwell");
        break; case CPU_MODEL_INTEL_SKYLAKE: return S8("skylake");
        break; case CPU_MODEL_INTEL_SKYLAKE_AVX512: return S8("skylake-avx512");
        break; case CPU_MODEL_INTEL_ROCKETLAKE: return S8("rocketlake");
        break; case CPU_MODEL_INTEL_COOPERLAKE: return S8("cooperlake");
        break; case CPU_MODEL_INTEL_CASCADELAKE: return S8("cascadelake");
        break; case CPU_MODEL_INTEL_CANNONLAKE: return S8("cannonlake");
        break; case CPU_MODEL_INTEL_ICELAKE_CLIENT: return S8("icelake-client");
        break; case CPU_MODEL_INTEL_TIGERLAKE: return S8("tigerlake");
        break; case CPU_MODEL_INTEL_ALDERLAKE: return S8("alderlake");
        break; case CPU_MODEL_INTEL_RAPTORLAKE: return S8("raptorlake");
        break; case CPU_MODEL_INTEL_METEORLAKE: return S8("meteorlake");
        break; case CPU_MODEL_INTEL_GRACEMONT: return S8("gracemont");
        break; case CPU_MODEL_INTEL_ARROWLAKE: return S8("arrowlake");
        break; case CPU_MODEL_INTEL_ARROWLAKE_S: return S8("arrowlake-s");
        break; case CPU_MODEL_INTEL_LUNARLAKE: return S8("lunarlake");
        break; case CPU_MODEL_INTEL_PANTHERLAKE: return S8("pantherlake");
        break; case CPU_MODEL_INTEL_ICELAKE_SERVER: return S8("icelake-server");
        break; case CPU_MODEL_INTEL_EMERALD_RAPIDS: return S8("emeraldrapids");
        break; case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS: return S8("sapphirerapids");
        break; case CPU_MODEL_INTEL_GRANITE_RAPIDS: return S8("graniterapids");
        break; case CPU_MODEL_INTEL_GRANITE_RAPIDS_D: return S8("graniterapids-d");
        break; case CPU_MODEL_INTEL_BONNELL: return S8("bonnell");
        break; case CPU_MODEL_INTEL_SILVERMONT: return S8("silvermont");
        break; case CPU_MODEL_INTEL_GOLDMONT: return S8("goldmont");
        break; case CPU_MODEL_INTEL_GOLDMONT_PLUS: return S8("goldmont-plus");
        break; case CPU_MODEL_INTEL_TREMONT: return S8("tremont");
        break; case CPU_MODEL_INTEL_SIERRAFOREST: return S8("sierraforest");
        break; case CPU_MODEL_INTEL_GRANDRIDGE: return S8("grandridge");
        break; case CPU_MODEL_INTEL_CLEARWATERFOREST: return S8("clearwaterforest");
        break; case CPU_MODEL_INTEL_KNL: return S8("knl");
        break; case CPU_MODEL_INTEL_KNM: return S8("knm");
        break; case CPU_MODEL_INTEL_DIAMOND_RAPIDS: return S8("diamondrapids");
        break; case CPU_MODEL_A64_GENERIC: return S8("generic");
        break; case CPU_MODEL_A64_ARM_ARM926EJ_S: return S8("arm926ej-s");
        break; case CPU_MODEL_A64_ARM_MPCORE: return S8("mpcore");
        break; case CPU_MODEL_A64_ARM_ARM1136J_S: return S8("arm1136j-s");
        break; case CPU_MODEL_A64_ARM_ARM1156T2_S: return S8("arm1156t2-s");
        break; case CPU_MODEL_A64_ARM_ARM1176JZ_S: return S8("arm1176jz-s");
        break; case CPU_MODEL_A64_ARM_CORTEX_A5: return S8("cortex-a5");
        break; case CPU_MODEL_A64_ARM_CORTEX_A7: return S8("cortex-a7");
        break; case CPU_MODEL_A64_ARM_CORTEX_A8: return S8("cortex-a8");
        break; case CPU_MODEL_A64_ARM_CORTEX_A9: return S8("cortex-a9");
        break; case CPU_MODEL_A64_ARM_CORTEX_A15: return S8("cortex-a15");
        break; case CPU_MODEL_A64_ARM_CORTEX_A17: return S8("cortex-a17");
        break; case CPU_MODEL_A64_ARM_CORTEX_M0: return S8("cortex-m0");
        break; case CPU_MODEL_A64_ARM_CORTEX_M3: return S8("cortex-m3");
        break; case CPU_MODEL_A64_ARM_CORTEX_M4: return S8("cortex-m4");
        break; case CPU_MODEL_A64_ARM_CORTEX_M7: return S8("cortex-m7");
        break; case CPU_MODEL_A64_ARM_CORTEX_M23: return S8("cortex-m23");
        break; case CPU_MODEL_A64_ARM_CORTEX_M33: return S8("cortex-m33");
        break; case CPU_MODEL_A64_ARM_CORTEX_M52: return S8("cortex-m52");
        break; case CPU_MODEL_A64_ARM_CORTEX_M55: return S8("cortex-m55");
        break; case CPU_MODEL_A64_ARM_CORTEX_M85: return S8("cortex-m85");
        break; case CPU_MODEL_A64_ARM_CORTEX_R8: return S8("cortex-r8");
        break; case CPU_MODEL_A64_ARM_CORTEX_R52: return S8("cortex-r52");
        break; case CPU_MODEL_A64_ARM_CORTEX_R52PLUS: return S8("cortex-r52plus");
        break; case CPU_MODEL_A64_ARM_CORTEX_R82: return S8("cortex-r82");
        break; case CPU_MODEL_A64_ARM_CORTEX_R82AE: return S8("cortex-r82ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A34: return S8("cortex-a34");
        break; case CPU_MODEL_A64_ARM_CORTEX_A35: return S8("cortex-a35");
        break; case CPU_MODEL_A64_ARM_CORTEX_A320: return S8("cortex-a320");
        break; case CPU_MODEL_A64_ARM_CORTEX_A53: return S8("cortex-a53");
        break; case CPU_MODEL_A64_ARM_CORTEX_A55: return S8("cortex-a55");
        break; case CPU_MODEL_A64_ARM_CORTEX_A510: return S8("cortex-a510");
        break; case CPU_MODEL_A64_ARM_CORTEX_A520: return S8("cortex-a520");
        break; case CPU_MODEL_A64_ARM_CORTEX_A520AE: return S8("cortex-a520ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A57: return S8("cortex-a57");
        break; case CPU_MODEL_A64_ARM_CORTEX_A65: return S8("cortex-a65");
        break; case CPU_MODEL_A64_ARM_CORTEX_A65AE: return S8("cortex-a65ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A72: return S8("cortex-a72");
        break; case CPU_MODEL_A64_ARM_CORTEX_A73: return S8("cortex-a73");
        break; case CPU_MODEL_A64_ARM_CORTEX_A75: return S8("cortex-a75");
        break; case CPU_MODEL_A64_ARM_CORTEX_A76: return S8("cortex-a76");
        break; case CPU_MODEL_A64_ARM_CORTEX_A76AE: return S8("cortex-a76ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A77: return S8("cortex-a77");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78: return S8("cortex-a78");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78AE: return S8("cortex-a78ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78C: return S8("cortex-a78c");
        break; case CPU_MODEL_A64_ARM_CORTEX_A710: return S8("cortex-a710");
        break; case CPU_MODEL_A64_ARM_CORTEX_A715: return S8("cortex-a715");
        break; case CPU_MODEL_A64_ARM_CORTEX_A720: return S8("cortex-a720");
        break; case CPU_MODEL_A64_ARM_CORTEX_A720AE: return S8("cortex-a720ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A725: return S8("cortex-a725");
        break; case CPU_MODEL_A64_ARM_CORTEX_X1: return S8("cortex-x1");
        break; case CPU_MODEL_A64_ARM_CORTEX_X1C: return S8("cortex-x1c");
        break; case CPU_MODEL_A64_ARM_CORTEX_X2: return S8("cortex-x2");
        break; case CPU_MODEL_A64_ARM_CORTEX_X3: return S8("cortex-x3");
        break; case CPU_MODEL_A64_ARM_CORTEX_X4: return S8("cortex-x4");
        break; case CPU_MODEL_A64_ARM_CORTEX_X925: return S8("cortex-x925");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_E1: return S8("neoverse-e1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N1: return S8("neoverse-n1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N2: return S8("neoverse-n2");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N3: return S8("neoverse-n3");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V1: return S8("neoverse-v1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V2: return S8("neoverse-v2");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V3: return S8("neoverse-v3");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V3AE: return S8("neoverse-v3ae");
        break; case CPU_MODEL_A64_ARM_ARM920T: return S8("arm920t");
        break; case CPU_MODEL_A64_ARM_XSCALE: return S8("xscale");
        break; case CPU_MODEL_A64_ARM_SWIFT: return S8("swift");
        break; case CPU_MODEL_A64_ARM920T: return S8("arm920t");
        break; case CPU_MODEL_A64_APPLE_A7: return S8("apple-a7");
        break; case CPU_MODEL_A64_APPLE_A8: return S8("apple-a8");
        break; case CPU_MODEL_A64_APPLE_A9: return S8("apple-a9");
        break; case CPU_MODEL_A64_APPLE_A10: return S8("apple-a10");
        break; case CPU_MODEL_A64_APPLE_A11: return S8("apple-a11");
        break; case CPU_MODEL_A64_APPLE_A12: return S8("apple-a12");
        break; case CPU_MODEL_A64_APPLE_A13: return S8("apple-a13");
        break; case CPU_MODEL_A64_APPLE_M1: return S8("apple-m1");
        break; case CPU_MODEL_A64_APPLE_M2: return S8("apple-m2");
        break; case CPU_MODEL_A64_APPLE_A17: return S8("apple-a17");
        break; case CPU_MODEL_A64_APPLE_M3: return S8("apple-m3");
        break; case CPU_MODEL_A64_APPLE_M4: return S8("apple-m4");
    }

    BUSTER_UNREACHABLE();
    return S8("error");
}
