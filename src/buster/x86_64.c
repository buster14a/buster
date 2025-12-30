#pragma once
#include <buster/x86_64.h>
#include <buster/string8.h>

BUSTER_GLOBAL_LOCAL CpuId cpuid(u32 leaf, u32 subleaf)
{
    CpuId result = {};
    asm volatile ("cpuid"
                        : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
                        : "a"(leaf), "c"(subleaf));
    return result;
}

BUSTER_IMPL CpuModel cpu_detect_model_x86_64()
{
    let vendor_cpuid = cpuid(0, 0);
    char8 vendor_buffer[3 * sizeof(vendor_cpuid.eax)];
    let vendor_string = BUSTER_ARRAY_TO_SLICE(String8, vendor_buffer);
    *(u32*)(vendor_buffer + 0 * sizeof(vendor_cpuid.eax)) = vendor_cpuid.ebx;
    *(u32*)(vendor_buffer + 1 * sizeof(vendor_cpuid.eax)) = vendor_cpuid.edx;
    *(u32*)(vendor_buffer + 2 * sizeof(vendor_cpuid.eax)) = vendor_cpuid.ecx;

    CpuModel result = CPU_MODEL_ERROR;

    let family_model_cpuid = cpuid(1, 0);

    // let stepping = (u8)((amd_cpuid.eax >> 0) & 0xf);
    let model = (u8)((family_model_cpuid.eax >> 4) & 0xf);
    let original_family = (u8)((family_model_cpuid.eax >> 8) & 0xf);
    let extended_model = (u8)((family_model_cpuid.eax >> 16) & 0xf);
    let extended_family = (u8)((family_model_cpuid.eax >> 20) & 0xff);

    let family = original_family == 0xf ? original_family + extended_family : original_family;
    model = ((original_family == 0x6) | (original_family == 0xf)) ? (u8)((extended_model << 4) | model) : model;

    // TODO fill these in
    bool has_sse = false;
    bool has_sse3 = false;
    bool has_avx512bf16 = false;
    bool has_avx512vnni = false;

    if (string_equal(vendor_string, S8("AuthenticAMD")))
    {
        switch (family)
        {
            break; case 4: result = CPU_MODEL_AMD_I486;
            break; case 5:
            {
                result = CPU_MODEL_AMD_PENTIUM;

                switch (model)
                {
                    break; case 6: case 7: result = CPU_MODEL_AMD_K6;
                    break; case 8: result = CPU_MODEL_AMD_K6_2;
                    break; case 9: case 13: result = CPU_MODEL_AMD_K6_3;
                    break; case 10: result = CPU_MODEL_AMD_K6_3;
                }
            }
            break; case 6: result = has_sse ? CPU_MODEL_AMD_ATHLON_XP : CPU_MODEL_AMD_ATHLON;
            break; case 15: result = has_sse3 ? CPU_MODEL_AMD_K8_SSE3 : CPU_MODEL_AMD_K8;
            break; case 16: case 18: result = CPU_MODEL_AMD_AMD_FAMILY_10;
            break; case 20: result = CPU_MODEL_AMD_BT_1;
            break; case 21:
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
            break; case 22: result = CPU_MODEL_AMD_BT_2;
            break; case 23:
            {
                result = CPU_MODEL_AMD_ZEN_1;
                if ((model >= 0x30 && model <= 0x3f) || (model == 0x47) ||
                        (model >= 0x60 && model <= 0x67) || (model >= 0x68 && model <= 0x6f) ||
                        (model >= 0x70 && model <= 0x7f) || (model >= 0x84 && model <= 0x87) ||
                        (model >= 0x90 && model <= 0x97) || (model >= 0x98 && model <= 0x9f) ||
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
            break; case 25:
            {
                result = CPU_MODEL_AMD_ZEN_3;
                if ((model >= 0x10 && model <= 0x1f) || (model >= 0x60 && model <= 0x6f) ||
                        (model >= 0x70 && model <= 0x77) || (model >= 0x78 && model <= 0x7f) ||
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
            break; case 26: result = CPU_MODEL_AMD_ZEN_5;
            break; default: {};
        }
    }
    else if (string_equal(vendor_string, S8("GenuineIntel")))
    {
        switch (family)
        {
            break; case 6:
            {
                switch (model)
                {
                    break; case 0x0f: case 0x16: result = CPU_MODEL_INTEL_CORE_2;
                    break; case 0x17: case 0x1d: result = CPU_MODEL_INTEL_PENRYN; 
                    break; case 0x1a: case 0x1e: case 0x1f: case 0x2e: result = CPU_MODEL_INTEL_NEHALEM;
                    break; case 0x25: case 0x2c: case 0x2f: result = CPU_MODEL_INTEL_WESTMERE;
                    break; case 0x2a: case 0x2d: result = CPU_MODEL_INTEL_SANDY_BRIDGE;
                    break; case 0x3a: case 0x3e: result = CPU_MODEL_INTEL_IVY_BRIDGE;
                    break; case 0x3c: case 0x3f: case 0x45: case 0x46: result = CPU_MODEL_INTEL_HASWELL;
                    break; case 0x3d: case 0x47: case 0x4f: case 0x56: result = CPU_MODEL_INTEL_BROADWELL;
                    break; case 0x4e: case 0x5e: case 0x8e: case 0x9e: case 0xa5: case 0xa6: result = CPU_MODEL_INTEL_SKYLAKE;
                    break; case 0xa7: result = CPU_MODEL_INTEL_ROCKETLAKE;
                    break; case 0x55:
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
                    break; case 0x66: result = CPU_MODEL_INTEL_CANNONLAKE;
                    break; case 0x7d: case 0x7e: result = CPU_MODEL_INTEL_ICELAKE_CLIENT;
                    break; case 0x8c: case 0x8d: result = CPU_MODEL_INTEL_TIGERLAKE;
                    break; case 0x97: case 0x9a: result = CPU_MODEL_INTEL_ALDERLAKE;
                    break; case 0xb7: case 0xba: case 0xbf: result = CPU_MODEL_INTEL_RAPTORLAKE;
                    break; case 0xaa: case 0xac: result = CPU_MODEL_INTEL_METEORLAKE;
                    break; case 0xbe: result = CPU_MODEL_INTEL_GRACEMONT;
                    break; case 0xc5: case 0xb5: result = CPU_MODEL_INTEL_ARROWLAKE;
                    break; case 0xc6: result = CPU_MODEL_INTEL_ARROWLAKE_S;
                    break; case 0xbd: result = CPU_MODEL_INTEL_LUNARLAKE;
                    break; case 0xcc: result = CPU_MODEL_INTEL_PANTHERLAKE;
                    break; case 0x6a: case 0x6c: result = CPU_MODEL_INTEL_ICELAKE_SERVER;
                    break; case 0xcf: result = CPU_MODEL_INTEL_EMERALD_RAPIDS;
                    break; case 0x8f: result = CPU_MODEL_INTEL_SAPPHIRE_RAPIDS;
                    break; case 0xad: result = CPU_MODEL_INTEL_GRANITE_RAPIDS;
                    break; case 0xae: result = CPU_MODEL_INTEL_GRANITE_RAPIDS_D;
                    break; case 0x1c: case 0x26: case 0x27: case 0x35: case 0x36: result = CPU_MODEL_INTEL_BONNELL;
                    break; case 0x37: case 0x4a: case 0x4d: case 0x5a: case 0x5d: case 0x4c: result = CPU_MODEL_INTEL_SILVERMONT;
                    break; case 0x5c: case 0x5f: result = CPU_MODEL_INTEL_GOLDMONT;
                    break; case 0x7a: result = CPU_MODEL_INTEL_GOLDMONT_PLUS;
                    break; case 0x86: case 0x8a: case 0x96: case 0x9c: result = CPU_MODEL_INTEL_TREMONT;
                    break; case 0xaf: result = CPU_MODEL_INTEL_SIERRAFOREST;
                    break; case 0xb6: result = CPU_MODEL_INTEL_GRANDRIDGE;
                    break; case 0xdd: result = CPU_MODEL_INTEL_CLEARWATERFOREST;
                    break; case 0x57: result = CPU_MODEL_INTEL_KNL;
                    break; case 0x85: result = CPU_MODEL_INTEL_KNM;
                    break; default: {}
                }
            }
            break; case 19:
            {
                switch (model)
                {
                    break; case 1: result = CPU_MODEL_INTEL_DIAMOND_RAPIDS;
                    break; default: {}
                }
            }
        }
    }
    else
    {
        string8_print(S8("Vendor string: {S8}\n"), vendor_string);
    }

    return result;
}
