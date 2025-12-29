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
    .cpu = {
#if defined(__x86_64__)
        .arch = CPU_ARCH_X86_64,
#elif defined(__aarch64__)
        .arch = CPU_ARCH_AARCH64,
#else
#pragma error
#endif
        .model = CPU_MODEL_NATIVE,
    },
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
    return (model == CPU_MODEL_NATIVE) | (model == target_native.cpu.model);
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
    target_native.cpu.model = cpu_model;
    return cpu_model;
}

BUSTER_IMPL TargetStringSplit target_to_split_os_string(Target target)
{
    OsString arch_string = cpu_arch_to_os_string(target.cpu.arch);
    OsString os_string = operating_system_to_os_string(target.os);
    OsString model_string = cpu_model_to_os_string(target.cpu.model);

    return (TargetStringSplit) { arch_string, os_string, model_string };
}

BUSTER_IMPL OsString cpu_arch_to_os_string(CpuArch arch)
{
    switch (arch)
    {
        break; case CPU_ARCH_X86_64: return OsS("x86_64");
        break; case CPU_ARCH_AARCH64: return OsS("aarch64");
        break; default: return OsS("");
    }
}

BUSTER_IMPL OsString operating_system_to_os_string(OperatingSystem os)
{
    switch (os)
    {
        break; case OPERATING_SYSTEM_LINUX: return OsS("linux");
        break; case OPERATING_SYSTEM_MACOS: return OsS("macos");
        break; case OPERATING_SYSTEM_WINDOWS: return OsS("windows");
        break; case OPERATING_SYSTEM_UEFI: return OsS("uefi");
        break; case OPERATING_SYSTEM_ANDROID: return OsS("android");
        break; case OPERATING_SYSTEM_IOS: return OsS("ios");
        break; case OPERATING_SYSTEM_FREESTANDING: return OsS("freestanding");
    }
}

BUSTER_IMPL OsString cpu_model_to_os_string(CpuModel model)
{
    switch (model)
    {
        break; case CPU_MODEL_COUNT: return OsS("count"); // TODO: crash?
        break; case CPU_MODEL_ERROR: return OsS("error"); // TODO: crash?
        break; case CPU_MODEL_BASELINE: return OsS("baseline");
        break; case CPU_MODEL_NATIVE: return OsS("native");
        break; case CPU_MODEL_AMD_I486: return OsS("i486");
        break; case CPU_MODEL_AMD_PENTIUM: return OsS("pentium");
        break; case CPU_MODEL_AMD_K6: return OsS("k6");
        break; case CPU_MODEL_AMD_K6_2: return OsS("k6-2");
        break; case CPU_MODEL_AMD_K6_3: return OsS("k6-3");
        break; case CPU_MODEL_AMD_GEODE: return OsS("geode");
        break; case CPU_MODEL_AMD_ATHLON: return OsS("athlon");
        break; case CPU_MODEL_AMD_ATHLON_XP: return OsS("athlon-xp");
        break; case CPU_MODEL_AMD_K8: return OsS("k8");
        break; case CPU_MODEL_AMD_K8_SSE3: return OsS("k8-sse3");
        break; case CPU_MODEL_AMD_AMD_FAMILY_10: return OsS("amdfam10");
        break; case CPU_MODEL_AMD_BT_1: return OsS("btver1");
        break; case CPU_MODEL_AMD_BT_2: return OsS("btver2");
        break; case CPU_MODEL_AMD_BD_1: return OsS("bdver1");
        break; case CPU_MODEL_AMD_BD_2: return OsS("bdver2");
        break; case CPU_MODEL_AMD_BD_3: return OsS("bdver3");
        break; case CPU_MODEL_AMD_BD_4: return OsS("bdver4");
        break; case CPU_MODEL_AMD_ZEN_1: return OsS("znver1");
        break; case CPU_MODEL_AMD_ZEN_2: return OsS("znver2");
        break; case CPU_MODEL_AMD_ZEN_3: return OsS("znver3");
        break; case CPU_MODEL_AMD_ZEN_4: return OsS("znver4");
        break; case CPU_MODEL_AMD_ZEN_5: return OsS("znver5");
        break; case CPU_MODEL_INTEL_CORE_2: return OsS("core2");
        break; case CPU_MODEL_INTEL_PENRYN: return OsS("penryn");
        break; case CPU_MODEL_INTEL_NEHALEM: return OsS("nehalem");
        break; case CPU_MODEL_INTEL_WESTMERE: return OsS("westmere");
        break; case CPU_MODEL_INTEL_SANDY_BRIDGE: return OsS("sandybridge");
        break; case CPU_MODEL_INTEL_IVY_BRIDGE: return OsS("ivybridge");
        break; case CPU_MODEL_INTEL_HASWELL: return OsS("haswell");
        break; case CPU_MODEL_INTEL_BROADWELL: return OsS("broadwell");
        break; case CPU_MODEL_INTEL_SKYLAKE: return OsS("skylake");
        break; case CPU_MODEL_INTEL_SKYLAKE_AVX512: return OsS("skylake-avx512");
        break; case CPU_MODEL_INTEL_ROCKETLAKE: return OsS("rocketlake");
        break; case CPU_MODEL_INTEL_COOPERLAKE: return OsS("cooperlake");
        break; case CPU_MODEL_INTEL_CASCADELAKE: return OsS("cascadelake");
        break; case CPU_MODEL_INTEL_CANNONLAKE: return OsS("cannonlake");
        break; case CPU_MODEL_INTEL_ICELAKE_CLIENT: return OsS("icelake-client");
        break; case CPU_MODEL_INTEL_TIGERLAKE: return OsS("tigerlake");
        break; case CPU_MODEL_INTEL_ALDERLAKE: return OsS("alderlake");
        break; case CPU_MODEL_INTEL_RAPTORLAKE: return OsS("raptorlake");
        break; case CPU_MODEL_INTEL_METEORLAKE: return OsS("meteorlake");
        break; case CPU_MODEL_INTEL_GRACEMONT: return OsS("gracemont");
        break; case CPU_MODEL_INTEL_ARROWLAKE: return OsS("arrowlake");
        break; case CPU_MODEL_INTEL_ARROWLAKE_S: return OsS("arrowlake-s");
        break; case CPU_MODEL_INTEL_LUNARLAKE: return OsS("lunarlake");
        break; case CPU_MODEL_INTEL_PANTHERLAKE: return OsS("pantherlake");
        break; case CPU_MODEL_INTEL_ICELAKE_SERVER: return OsS("icelake-server");
        break; case CPU_MODEL_INTEL_EMERALD_RAPIDS: return OsS("emeraldrapids");
        break; case CPU_MODEL_INTEL_SAPPHIRE_RAPIDS: return OsS("sapphirerapids");
        break; case CPU_MODEL_INTEL_GRANITE_RAPIDS: return OsS("graniterapids");
        break; case CPU_MODEL_INTEL_GRANITE_RAPIDS_D: return OsS("graniterapids-d");
        break; case CPU_MODEL_INTEL_BONNELL: return OsS("bonnell");
        break; case CPU_MODEL_INTEL_SILVERMONT: return OsS("silvermont");
        break; case CPU_MODEL_INTEL_GOLDMONT: return OsS("goldmont");
        break; case CPU_MODEL_INTEL_GOLDMONT_PLUS: return OsS("goldmont-plus");
        break; case CPU_MODEL_INTEL_TREMONT: return OsS("tremont");
        break; case CPU_MODEL_INTEL_SIERRAFOREST: return OsS("sierraforest");
        break; case CPU_MODEL_INTEL_GRANDRIDGE: return OsS("grandridge");
        break; case CPU_MODEL_INTEL_CLEARWATERFOREST: return OsS("clearwaterforest");
        break; case CPU_MODEL_INTEL_KNL: return OsS("knl");
        break; case CPU_MODEL_INTEL_KNM: return OsS("knm");
        break; case CPU_MODEL_INTEL_DIAMOND_RAPIDS: return OsS("diamondrapids");
        break; case CPU_MODEL_A64_GENERIC: return OsS("generic");
        break; case CPU_MODEL_A64_ARM_ARM926EJ_S: return OsS("arm926ej-s");
        break; case CPU_MODEL_A64_ARM_MPCORE: return OsS("mpcore");
        break; case CPU_MODEL_A64_ARM_ARM1136J_S: return OsS("arm1136j-s");
        break; case CPU_MODEL_A64_ARM_ARM1156T2_S: return OsS("arm1156t2-s");
        break; case CPU_MODEL_A64_ARM_ARM1176JZ_S: return OsS("arm1176jz-s");
        break; case CPU_MODEL_A64_ARM_CORTEX_A5: return OsS("cortex-a5");
        break; case CPU_MODEL_A64_ARM_CORTEX_A7: return OsS("cortex-a7");
        break; case CPU_MODEL_A64_ARM_CORTEX_A8: return OsS("cortex-a8");
        break; case CPU_MODEL_A64_ARM_CORTEX_A9: return OsS("cortex-a9");
        break; case CPU_MODEL_A64_ARM_CORTEX_A15: return OsS("cortex-a15");
        break; case CPU_MODEL_A64_ARM_CORTEX_A17: return OsS("cortex-a17");
        break; case CPU_MODEL_A64_ARM_CORTEX_M0: return OsS("cortex-m0");
        break; case CPU_MODEL_A64_ARM_CORTEX_M3: return OsS("cortex-m3");
        break; case CPU_MODEL_A64_ARM_CORTEX_M4: return OsS("cortex-m4");
        break; case CPU_MODEL_A64_ARM_CORTEX_M7: return OsS("cortex-m7");
        break; case CPU_MODEL_A64_ARM_CORTEX_M23: return OsS("cortex-m23");
        break; case CPU_MODEL_A64_ARM_CORTEX_M33: return OsS("cortex-m33");
        break; case CPU_MODEL_A64_ARM_CORTEX_M52: return OsS("cortex-m52");
        break; case CPU_MODEL_A64_ARM_CORTEX_M55: return OsS("cortex-m55");
        break; case CPU_MODEL_A64_ARM_CORTEX_M85: return OsS("cortex-m85");
        break; case CPU_MODEL_A64_ARM_CORTEX_R8: return OsS("cortex-r8");
        break; case CPU_MODEL_A64_ARM_CORTEX_R52: return OsS("cortex-r52");
        break; case CPU_MODEL_A64_ARM_CORTEX_R52PLUS: return OsS("cortex-r52plus");
        break; case CPU_MODEL_A64_ARM_CORTEX_R82: return OsS("cortex-r82");
        break; case CPU_MODEL_A64_ARM_CORTEX_R82AE: return OsS("cortex-r82ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A34: return OsS("cortex-a34");
        break; case CPU_MODEL_A64_ARM_CORTEX_A35: return OsS("cortex-a35");
        break; case CPU_MODEL_A64_ARM_CORTEX_A320: return OsS("cortex-a320");
        break; case CPU_MODEL_A64_ARM_CORTEX_A53: return OsS("cortex-a53");
        break; case CPU_MODEL_A64_ARM_CORTEX_A55: return OsS("cortex-a55");
        break; case CPU_MODEL_A64_ARM_CORTEX_A510: return OsS("cortex-a510");
        break; case CPU_MODEL_A64_ARM_CORTEX_A520: return OsS("cortex-a520");
        break; case CPU_MODEL_A64_ARM_CORTEX_A520AE: return OsS("cortex-a520ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A57: return OsS("cortex-a57");
        break; case CPU_MODEL_A64_ARM_CORTEX_A65: return OsS("cortex-a65");
        break; case CPU_MODEL_A64_ARM_CORTEX_A65AE: return OsS("cortex-a65ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A72: return OsS("cortex-a72");
        break; case CPU_MODEL_A64_ARM_CORTEX_A73: return OsS("cortex-a73");
        break; case CPU_MODEL_A64_ARM_CORTEX_A75: return OsS("cortex-a75");
        break; case CPU_MODEL_A64_ARM_CORTEX_A76: return OsS("cortex-a76");
        break; case CPU_MODEL_A64_ARM_CORTEX_A76AE: return OsS("cortex-a76ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A77: return OsS("cortex-a77");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78: return OsS("cortex-a78");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78AE: return OsS("cortex-a78ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A78C: return OsS("cortex-a78c");
        break; case CPU_MODEL_A64_ARM_CORTEX_A710: return OsS("cortex-a710");
        break; case CPU_MODEL_A64_ARM_CORTEX_A715: return OsS("cortex-a715");
        break; case CPU_MODEL_A64_ARM_CORTEX_A720: return OsS("cortex-a720");
        break; case CPU_MODEL_A64_ARM_CORTEX_A720AE: return OsS("cortex-a720ae");
        break; case CPU_MODEL_A64_ARM_CORTEX_A725: return OsS("cortex-a725");
        break; case CPU_MODEL_A64_ARM_CORTEX_X1: return OsS("cortex-x1");
        break; case CPU_MODEL_A64_ARM_CORTEX_X1C: return OsS("cortex-x1c");
        break; case CPU_MODEL_A64_ARM_CORTEX_X2: return OsS("cortex-x2");
        break; case CPU_MODEL_A64_ARM_CORTEX_X3: return OsS("cortex-x3");
        break; case CPU_MODEL_A64_ARM_CORTEX_X4: return OsS("cortex-x4");
        break; case CPU_MODEL_A64_ARM_CORTEX_X925: return OsS("cortex-x925");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_E1: return OsS("neoverse-e1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N1: return OsS("neoverse-n1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N2: return OsS("neoverse-n2");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_N3: return OsS("neoverse-n3");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V1: return OsS("neoverse-v1");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V2: return OsS("neoverse-v2");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V3: return OsS("neoverse-v3");
        break; case CPU_MODEL_A64_ARM_NEOVERSE_V3AE: return OsS("neoverse-v3ae");
        break; case CPU_MODEL_A64_ARM_ARM920T: return OsS("arm920t");
        break; case CPU_MODEL_A64_ARM_XSCALE: return OsS("xscale");
        break; case CPU_MODEL_A64_ARM_SWIFT: return OsS("swift");
        break; case CPU_MODEL_A64_ARM920T: return OsS("arm920t");
        break; case CPU_MODEL_A64_APPLE_A7: return OsS("apple-a7");
        break; case CPU_MODEL_A64_APPLE_A8: return OsS("apple-a8");
        break; case CPU_MODEL_A64_APPLE_A9: return OsS("apple-a9");
        break; case CPU_MODEL_A64_APPLE_A10: return OsS("apple-a10");
        break; case CPU_MODEL_A64_APPLE_A11: return OsS("apple-a11");
        break; case CPU_MODEL_A64_APPLE_A12: return OsS("apple-a12");
        break; case CPU_MODEL_A64_APPLE_A13: return OsS("apple-a13");
        break; case CPU_MODEL_A64_APPLE_M1: return OsS("apple-m1");
        break; case CPU_MODEL_A64_APPLE_M2: return OsS("apple-m2");
        break; case CPU_MODEL_A64_APPLE_A17: return OsS("apple-a17");
        break; case CPU_MODEL_A64_APPLE_M3: return OsS("apple-m3");
        break; case CPU_MODEL_A64_APPLE_M4: return OsS("apple-m4");
    }

    BUSTER_UNREACHABLE();
}
