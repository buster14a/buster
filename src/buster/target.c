#pragma once
#include <buster/target.h>

#if defined(__x86_64__)
#include <buster/x86_64.h>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/machine.h>
#endif

BUSTER_IMPL Target target_native = {
#if defined(__x86_64__)
    .cpu_arch = CPU_ARCH_X86_64,
#elif defined(__aarch64__)
    .cpu_arch = CPU_ARCH_AARCH64,
#else
#pragma error
#endif
    .cpu_model = CPU_MODEL_NATIVE,
#if defined(__linux__)
    .os = OPERATING_SYSTEM_LINUX,
#elif defined(_WIN32)
    .os = OPERATING_SYSTEM_WINDOWS,
#elif defined(__APPLE__)
#define BUSTER_APPLE 1
#if TARGET_OS_MAC == 1
    .os = OPERATING_SYSTEM_MACOS,
#elif (TARGET_OS_IPHONE == 1) || (TARGET_IPHONE_SIMULATOR == 1)
    .os = OPERATING_SYSTEM_IOS
#else
#pragma error
#endif
#endif
};

BUSTER_IMPL bool cpu_is_native(CpuModel model)
{
    return (model == CPU_MODEL_NATIVE) | (model == target_native.cpu_model);
}

BUSTER_IMPL CpuModel cpu_detect_model()
{
    CpuModel cpu_model = CPU_MODEL_ERROR;
#if defined(__x86_64__)
    cpu_model = cpu_detect_model_x86_64();
#elif defined(__aarch64__)
    cpu_model = cpu_detect_model_aarch64();
#else
#pragma error // TODO: implement CPU detection code for this architecture
#endif
    target_native.cpu_model = cpu_model;
    return cpu_model;
}

BUSTER_IMPL TargetStringSplit target_to_split_string_os(Target target)
{
    let arch_string = cpu_arch_to_string_os(target.cpu_arch);
    let string_os = operating_system_to_string_os(target.os);
    let model_string = cpu_model_to_string_os(target.cpu_model);

    return (TargetStringSplit) { arch_string, string_os, model_string };
}

BUSTER_IMPL StringOs cpu_arch_to_string_os(CpuArch arch)
{
    switch (arch)
    {
        break; case CPU_ARCH_X86_64: return SOs("x86_64");
        break; case CPU_ARCH_AARCH64: return SOs("aarch64");
        break; default: return SOs("");
    }
}

BUSTER_IMPL StringOs operating_system_to_string_os(OperatingSystem os)
{
    switch (os)
    {
        break; case OPERATING_SYSTEM_LINUX: return SOs("linux");
        break; case OPERATING_SYSTEM_MACOS: return SOs("macos");
        break; case OPERATING_SYSTEM_WINDOWS: return SOs("windows");
        break; case OPERATING_SYSTEM_UEFI: return SOs("uefi");
        break; case OPERATING_SYSTEM_ANDROID: return SOs("android");
        break; case OPERATING_SYSTEM_IOS: return SOs("ios");
        break; case OPERATING_SYSTEM_FREESTANDING: return SOs("freestanding");
    }

    BUSTER_UNREACHABLE();
}

BUSTER_IMPL StringOs cpu_model_to_string_os(CpuModel model)
{
    switch (model)
    {
        break; case CPU_MODEL_COUNT: return SOs("count"); // TODO: crash?
        break; case CPU_MODEL_ERROR: return SOs("error"); // TODO: crash?
        break; case CPU_MODEL_BASELINE: return SOs("baseline");
        break; case CPU_MODEL_NATIVE: return SOs("native");
        break; case CPU_MODEL_AMD_I486: return SOs("i486");
        break; case CPU_MODEL_AMD_PENTIUM: return SOs("pentium");
        break; case CPU_MODEL_AMD_K6: return SOs("k6");
        break; case CPU_MODEL_AMD_K6_2: return SOs("k6-2");
        break; case CPU_MODEL_AMD_K6_3: return SOs("k6-3");
        break; case CPU_MODEL_AMD_GEODE: return SOs("geode");
        break; case CPU_MODEL_AMD_ATHLON: return SOs("athlon");
        break; case CPU_MODEL_AMD_ATHLON_XP: return SOs("athlon-xp");
        break; case CPU_MODEL_AMD_K8: return SOs("k8");
        break; case CPU_MODEL_AMD_K8_SSE3: return SOs("k8-sse3");
        break; case CPU_MODEL_AMD_AMD_FAMILY_10: return SOs("amdfam10");
        break; case CPU_MODEL_AMD_BT_1: return SOs("btver1");
        break; case CPU_MODEL_AMD_BT_2: return SOs("btver2");
        break; case CPU_MODEL_AMD_BD_1: return SOs("bdver1");
        break; case CPU_MODEL_AMD_BD_2: return SOs("bdver2");
        break; case CPU_MODEL_AMD_BD_3: return SOs("bdver3");
        break; case CPU_MODEL_AMD_BD_4: return SOs("bdver4");
        break; case CPU_MODEL_AMD_ZEN_1: return SOs("znver1");
        break; case CPU_MODEL_AMD_ZEN_2: return SOs("znver2");
        break; case CPU_MODEL_AMD_ZEN_3: return SOs("znver3");
        break; case CPU_MODEL_AMD_ZEN_4: return SOs("znver4");
        break; case CPU_MODEL_AMD_ZEN_5: return SOs("znver5");
        break; case CPU_MODEL_INTEL_CORE_2: return SOs("core2");
        break; case CPU_MODEL_INTEL_PENRYN: return SOs("penryn");
        break; case CPU_MODEL_INTEL_NEHALEM: return SOs("nehalem");
        break; case CPU_MODEL_INTEL_WESTMERE: return SOs("westmere");
        break; case CPU_MODEL_INTEL_SANDY_BRIDGE: return SOs("sandybridge");
        break; case CPU_MODEL_INTEL_IVY_BRIDGE: return SOs("ivybridge");
        break; case CPU_MODEL_INTEL_HASWELL: return SOs("haswell");
        break; case CPU_MODEL_INTEL_BROADWELL: return SOs("broadwell");
        break; case CPU_MODEL_INTEL_SKYLAKE: return SOs("skylake");
        break; case CPU_MODEL_INTEL_SKYLAKE_AVX512: return SOs("skylake-avx512");
        break; case CPU_MODEL_INTEL_ROCKETLAKE: return SOs("rocketlake");
        break; case CPU_MODEL_INTEL_COOPERLAKE: return SOs("cooperlake");
        break; case CPU_MODEL_INTEL_CASCADELAKE: return SOs("cascadelake");
        break; case CPU_MODEL_INTEL_CANNONLAKE: return SOs("cannonlake");
        break; case CPU_MODEL_INTEL_ICELAKE_CLIENT: return SOs("icelake-client");
        break; case CPU_MODEL_INTEL_TIGERLAKE: return SOs("tigerlake");
        break; case CPU_MODEL_INTEL_ALDERLAKE: return SOs("alderlake");
        break; case CPU_MODEL_INTEL_RAPTORLAKE: return SOs("raptorlake");
        break; case CPU_MODEL_INTEL_METEORLAKE: return SOs("meteorlake");
        break; case CPU_MODEL_INTEL_GRACEMONT: return SOs("gracemont");
        break; case CPU_MODEL_INTEL_ARROWLAKE: return SOs("arrowlake");
        break; case CPU_MODEL_INTEL_ARROWLAKE_S: return SOs("arrowlake-s");
        break; case CPU_MODEL_INTEL_LUNARLAKE: return SOs("lunarlake");
        break; case CPU_MODEL_INTEL_PANTHERLAKE: return SOs("pantherlake");
        break; case CPU_MODEL_INTEL_ICELAKE_SERVER: return SOs("icelake-server");
        break; case CPU_MODEL_INTEL_EMERALD_RAPIDS: return SOs("emeraldrapids");
        break; case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS: return SOs("sapphirerapids");
        break; case CPU_MODEL_INTEL_GRANITE_RAPIDS: return SOs("graniterapids");
        break; case CPU_MODEL_INTEL_GRANITE_RAPIDS_D: return SOs("graniterapids-d");
        break; case CPU_MODEL_INTEL_BONNELL: return SOs("bonnell");
        break; case CPU_MODEL_INTEL_SILVERMONT: return SOs("silvermont");
        break; case CPU_MODEL_INTEL_GOLDMONT: return SOs("goldmont");
        break; case CPU_MODEL_INTEL_GOLDMONT_PLUS: return SOs("goldmont-plus");
        break; case CPU_MODEL_INTEL_TREMONT: return SOs("tremont");
        break; case CPU_MODEL_INTEL_SIERRAFOREST: return SOs("sierraforest");
        break; case CPU_MODEL_INTEL_GRANDRIDGE: return SOs("grandridge");
        break; case CPU_MODEL_INTEL_CLEARWATERFOREST: return SOs("clearwaterforest");
        break; case CPU_MODEL_INTEL_KNL: return SOs("knl");
        break; case CPU_MODEL_INTEL_KNM: return SOs("knm");
        break; case CPU_MODEL_INTEL_DIAMOND_RAPIDS: return SOs("diamondrapids");
        break; case CPU_MODEL_A64_GENERIC: return SOs("generic");
        break; case CPU_MODEL_A64_ARM_ARM926EJ_S: return SOs("arm926ej-s");
        break; case CPU_MODEL_A64_ARM_MPCORE: return SOs("mpcore");
        break; case CPU_MODEL_A64_ARM_ARM1136J_S: return SOs("arm1136j-s");
        break; case CPU_MODEL_A64_ARM_ARM1156T2_S: return SOs("arm1156t2-s");
        break; case CPU_MODEL_A64_ARM_ARM1176JZ_S: return SOs("arm1176jz-s");
        break; case CPU_MODEL_A64_ARM_CORTEX_A5: return SOs("cortex-a5");
        break; case CPU_MODEL_A64_ARM_CORTEX_A7: return SOs("cortex-a7");
        break; case CPU_MODEL_A64_ARM_CORTEX_A8: return SOs("cortex-a8");
        break; case CPU_MODEL_A64_ARM_CORTEX_A9: return SOs("cortex-a9");
        break; case CPU_MODEL_A64_ARM_CORTEX_A15: return SOs("cortex-a15");
        break; case CPU_MODEL_A64_ARM_CORTEX_A17: return SOs("cortex-a17");
        break; case CPU_MODEL_A64_ARM_CORTEX_M0: return SOs("cortex-m0");
        break; case CPU_MODEL_A64_ARM_CORTEX_M3: return SOs("cortex-m3");
        break; case CPU_MODEL_A64_ARM_CORTEX_M4: return SOs("cortex-m4");
        break; case CPU_MODEL_A64_ARM_CORTEX_M7: return SOs("cortex-m7");
        break; case CPU_MODEL_A64_ARM_CORTEX_M23: return SOs("cortex-m23");
        break; case CPU_MODEL_A64_ARM_CORTEX_M33: return SOs("cortex-m33");
        break; case CPU_MODEL_A64_ARM_CORTEX_M52: return SOs("cortex-m52");
        break; case CPU_MODEL_A64_ARM_CORTEX_M55: return SOs("cortex-m55");
        break; case CPU_MODEL_A64_ARM_CORTEX_M85: return SOs("cortex-m85");
        break; case CPU_MODEL_A64_ARM_CORTEX_R8: return SOs("cortex-r8");
        break; case CPU_MODEL_A64_ARM_CORTEX_R52: return SOs("cortex-r52");
        break; case CPU_MODEL_A64_ARM_CORTEX_R52PLUS: return SOs("cortex-r52plus");
        break; case CPU_MODEL_A64_ARM_CORTEX_R82: return SOs("cortex-r82");
        break; case CPU_MODEL_A64_ARM_CORTEX_R82AE: return SOs("cortex-r82ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A34: return SOs("cortex-a34");
        break; case CPU_MODEL_A64_ARM_CORTEX_A35: return SOs("cortex-a35");
        break; case CPU_MODEL_A64_ARM_CORTEX_A320: return SOs("cortex-a320");
        break; case CPU_MODEL_A64_ARM_CORTEX_A53: return SOs("cortex-a53");
        break; case CPU_MODEL_A64_ARM_CORTEX_A55: return SOs("cortex-a55");
        break; case CPU_MODEL_A64_ARM_CORTEX_A510: return SOs("cortex-a510");
        break; case CPU_MODEL_A64_ARM_CORTEX_A520: return SOs("cortex-a520");
        break; case CPU_MODEL_A64_ARM_CORTEX_A520AE: return SOs("cortex-a520ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A57: return SOs("cortex-a57");
        break; case CPU_MODEL_A64_ARM_CORTEX_A65: return SOs("cortex-a65");
        break; case CPU_MODEL_A64_ARM_CORTEX_A65AE: return SOs("cortex-a65ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A72: return SOs("cortex-a72");
        break; case CPU_MODEL_A64_ARM_CORTEX_A73: return SOs("cortex-a73");
        break; case CPU_MODEL_A64_ARM_CORTEX_A75: return SOs("cortex-a75");
        break; case CPU_MODEL_A64_ARM_CORTEX_A76: return SOs("cortex-a76");
        break; case CPU_MODEL_A64_ARM_CORTEX_A76AE: return SOs("cortex-a76ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A77: return SOs("cortex-a77");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78: return SOs("cortex-a78");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78AE: return SOs("cortex-a78ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78C: return SOs("cortex-a78c");
        break; case CPU_MODEL_A64_ARM_CORTEX_A710: return SOs("cortex-a710");
        break; case CPU_MODEL_A64_ARM_CORTEX_A715: return SOs("cortex-a715");
        break; case CPU_MODEL_A64_ARM_CORTEX_A720: return SOs("cortex-a720");
        break; case CPU_MODEL_A64_ARM_CORTEX_A720AE: return SOs("cortex-a720ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A725: return SOs("cortex-a725");
        break; case CPU_MODEL_A64_ARM_CORTEX_X1: return SOs("cortex-x1");
        break; case CPU_MODEL_A64_ARM_CORTEX_X1C: return SOs("cortex-x1c");
        break; case CPU_MODEL_A64_ARM_CORTEX_X2: return SOs("cortex-x2");
        break; case CPU_MODEL_A64_ARM_CORTEX_X3: return SOs("cortex-x3");
        break; case CPU_MODEL_A64_ARM_CORTEX_X4: return SOs("cortex-x4");
        break; case CPU_MODEL_A64_ARM_CORTEX_X925: return SOs("cortex-x925");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_E1: return SOs("neoverse-e1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N1: return SOs("neoverse-n1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N2: return SOs("neoverse-n2");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N3: return SOs("neoverse-n3");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V1: return SOs("neoverse-v1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V2: return SOs("neoverse-v2");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V3: return SOs("neoverse-v3");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V3AE: return SOs("neoverse-v3ae");
        break; case CPU_MODEL_A64_ARM_ARM920T: return SOs("arm920t");
        break; case CPU_MODEL_A64_ARM_XSCALE: return SOs("xscale");
        break; case CPU_MODEL_A64_ARM_SWIFT: return SOs("swift");
        break; case CPU_MODEL_A64_ARM920T: return SOs("arm920t");
        break; case CPU_MODEL_A64_APPLE_A7: return SOs("apple-a7");
        break; case CPU_MODEL_A64_APPLE_A8: return SOs("apple-a8");
        break; case CPU_MODEL_A64_APPLE_A9: return SOs("apple-a9");
        break; case CPU_MODEL_A64_APPLE_A10: return SOs("apple-a10");
        break; case CPU_MODEL_A64_APPLE_A11: return SOs("apple-a11");
        break; case CPU_MODEL_A64_APPLE_A12: return SOs("apple-a12");
        break; case CPU_MODEL_A64_APPLE_A13: return SOs("apple-a13");
        break; case CPU_MODEL_A64_APPLE_M1: return SOs("apple-m1");
        break; case CPU_MODEL_A64_APPLE_M2: return SOs("apple-m2");
        break; case CPU_MODEL_A64_APPLE_A17: return SOs("apple-a17");
        break; case CPU_MODEL_A64_APPLE_M3: return SOs("apple-m3");
        break; case CPU_MODEL_A64_APPLE_M4: return SOs("apple-m4");
    }

    BUSTER_UNREACHABLE();
}
