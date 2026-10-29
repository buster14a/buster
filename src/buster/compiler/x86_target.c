// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Generated-code ISA policy (#180), independent of the host build and the encoder. Zero-initialized options mean
// x86-64 baseline. Resolve native once, before preprocessing/selection, never in an encoding or selection loop.
// Features are a bitset for form eligibility and target builtins. The preprocessor fills it from -march and the -m
// flags as clang resolves them, and writes their macros, from c_predefines.c (predefines_target); x86_target_parse's
// profiles give it what clang has no feature for. CPUID bit positions are architectural facts; macro spellings and the
// named profiles are checked against Clang 23 (docs/x86-target.md).
#ifndef BUSTER_X86_TARGET_INCLUDED
#define BUSTER_X86_TARGET_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <cpuid.h>

// name, macro, first profile (2/3/4 or 5 for znver5), CPUID group, register (eax..edx), bit.
// Groups: 0=1:0, 1=7:0, 2=7:1, 3=80000001:0, 4=80000008:0, 5=D:1, 6=80000021:0.
#define BUSTER_X86_TARGET_FEATURES(X) \
    X(SSE3, "__SSE3__", 2, 0, 2, 0) \
    X(SSSE3, "__SSSE3__", 2, 0, 2, 9) \
    X(SSE41, "__SSE4_1__", 2, 0, 2, 19) \
    X(SSE42, "__SSE4_2__", 2, 0, 2, 20) \
    X(CRC32, "__CRC32__", 2, 0, 2, 20) \
    X(POPCNT, "__POPCNT__", 2, 0, 2, 23) \
    X(CX16, "__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16", 2, 0, 2, 13) \
    X(SAHF, "__LAHF_SAHF__", 2, 3, 2, 0) \
    X(AVX, "__AVX__", 3, 0, 2, 28) \
    X(AVX2, "__AVX2__", 3, 1, 1, 5) \
    X(BMI, "__BMI__", 3, 1, 1, 3) \
    X(BMI2, "__BMI2__", 3, 1, 1, 8) \
    X(F16C, "__F16C__", 3, 0, 2, 29) \
    X(FMA, "__FMA__", 3, 0, 2, 12) \
    X(LZCNT, "__LZCNT__", 3, 3, 2, 5) \
    X(MOVBE, "__MOVBE__", 3, 0, 2, 22) \
    X(XSAVE, "__XSAVE__", 3, 0, 2, 26) \
    X(AVX512F, "__AVX512F__", 4, 1, 1, 16) \
    X(AVX512BW, "__AVX512BW__", 4, 1, 1, 30) \
    X(AVX512CD, "__AVX512CD__", 4, 1, 1, 28) \
    X(AVX512DQ, "__AVX512DQ__", 4, 1, 1, 17) \
    X(AVX512VL, "__AVX512VL__", 4, 1, 1, 31) \
    X(ADX, "__ADX__", 5, 1, 1, 19) \
    X(AES, "__AES__", 5, 0, 2, 25) \
    X(AVX512BF16, "__AVX512BF16__", 5, 2, 0, 5) \
    X(AVX512BITALG, "__AVX512BITALG__", 5, 1, 2, 12) \
    X(AVX512IFMA, "__AVX512IFMA__", 5, 1, 1, 21) \
    X(AVX512VBMI, "__AVX512VBMI__", 5, 1, 2, 1) \
    X(AVX512VBMI2, "__AVX512VBMI2__", 5, 1, 2, 6) \
    X(AVX512VNNI, "__AVX512VNNI__", 5, 1, 2, 11) \
    X(AVX512VP2INTERSECT, "__AVX512VP2INTERSECT__", 5, 1, 3, 8) \
    X(AVX512VPOPCNTDQ, "__AVX512VPOPCNTDQ__", 5, 1, 2, 14) \
    X(AVXVNNI, "__AVXVNNI__", 5, 2, 0, 4) \
    X(CLFLUSHOPT, "__CLFLUSHOPT__", 5, 1, 1, 23) \
    X(CLWB, "__CLWB__", 5, 1, 1, 24) \
    X(CLZERO, "__CLZERO__", 5, 4, 1, 0) \
    X(FSGSBASE, "__FSGSBASE__", 5, 1, 1, 0) \
    X(GFNI, "__GFNI__", 5, 1, 2, 8) \
    X(INVPCID, "__INVPCID__", 5, 1, 1, 10) \
    X(MOVDIR64B, "__MOVDIR64B__", 5, 1, 2, 28) \
    X(MOVDIRI, "__MOVDIRI__", 5, 1, 2, 27) \
    X(MWAITX, "__MWAITX__", 5, 3, 2, 29) \
    X(PCLMUL, "__PCLMUL__", 5, 0, 2, 1) \
    X(PKU, "__PKU__", 5, 1, 2, 4) \
    X(PREFETCHI, "__PREFETCHI__", 5, 2, 3, 14) \
    X(PRFCHW, "__PRFCHW__", 5, 3, 2, 8) \
    X(RDPID, "__RDPID__", 5, 1, 2, 22) \
    X(RDPRU, "__RDPRU__", 5, 4, 1, 4) \
    X(RDRND, "__RDRND__", 5, 0, 2, 30) \
    X(RDSEED, "__RDSEED__", 5, 1, 1, 18) \
    X(SHA, "__SHA__", 5, 1, 1, 29) \
    X(SHSTK, "__SHSTK__", 5, 1, 2, 7) \
    X(SSE4A, "__SSE4A__", 5, 3, 2, 6) \
    X(VAES, "__VAES__", 5, 1, 2, 9) \
    X(VPCLMULQDQ, "__VPCLMULQDQ__", 5, 1, 2, 10) \
    X(WBNOINVD, "__WBNOINVD__", 5, 4, 1, 9) \
    X(XSAVEC, "__XSAVEC__", 5, 5, 0, 1) \
    X(XSAVEOPT, "__XSAVEOPT__", 5, 5, 0, 0) \
    X(XSAVES, "__XSAVES__", 5, 5, 0, 3) \
    X(RDTSCP, 0, 5, 3, 3, 27) \
    X(CLFLUSH, 0, 5, 0, 3, 19)

#define BUSTER_X86_TARGET_ENUM(name, macro, level, group, reg, bit) X86_FEATURE_##name,
typedef enum X86Feature
{
    BUSTER_X86_TARGET_FEATURES(BUSTER_X86_TARGET_ENUM)
    X86_FEATURE_COUNT,
} X86Feature;
#undef BUSTER_X86_TARGET_ENUM
_Static_assert(X86_FEATURE_COUNT < 63, "leave a bit for unsupported forms");
#define BUSTER_X86_FEATURE(name) (UINT64_C(1) << X86_FEATURE_##name)

// The features past those that the generated code may use, which form eligibility doesn't classify and only the
// lowering reads: AVX512-FP16 for its x86 rows (c_check_program_x86.c) and its vector and scalar half precision, AVX-NE-
// CONVERT for its VCVTNEPS2BF16 and AVX10.2 for its native __bf16 arithmetic (c_check_program_half.c), and XOP for its
// builtins' rows. X86Target.extensions, from the target options as clang resolves them and from a function's target
// attribute (c_predefines.c's predefines_target and predefines_function_target), by clang's names
// (predefines_x86_extensions).
#define BUSTER_X86_TARGET_EXTENSIONS(X) \
    X(AVX512FP16) \
    X(AVXNECONVERT) \
    X(AVX10_2) \
    X(XOP)

#define BUSTER_X86_TARGET_EXTENSION_ENUM(name) X86_EXTENSION_##name,
typedef enum X86Extension
{
    BUSTER_X86_TARGET_EXTENSIONS(BUSTER_X86_TARGET_EXTENSION_ENUM)
    X86_EXTENSION_COUNT,
} X86Extension;
#undef BUSTER_X86_TARGET_EXTENSION_ENUM
_Static_assert(X86_EXTENSION_COUNT <= 64, "the extensions are a word's bits");
#define BUSTER_X86_EXTENSION(name) (UINT64_C(1) << X86_EXTENSION_##name)

typedef struct X86FeatureDescription X86FeatureDescription;
struct X86FeatureDescription
{
    const char* macro;
    uint8_t level;
    uint8_t group;
    uint8_t reg;
    uint8_t bit;
};
#define BUSTER_X86_TARGET_DESCRIPTION(name, macro, level, group, reg, bit) { macro, level, group, reg, bit },
static const X86FeatureDescription x86_features[] = {
    BUSTER_X86_TARGET_FEATURES(BUSTER_X86_TARGET_DESCRIPTION)
};
#undef BUSTER_X86_TARGET_DESCRIPTION

typedef enum X86Architecture
{
    X86_ARCH_BASELINE,
    X86_ARCH_V2,
    X86_ARCH_V3,
    X86_ARCH_V4,
    X86_ARCH_ZNVER5,
    X86_ARCH_NATIVE,
    // Another of clang's CPUs (c_predefines.c's predefines_target): its features say what it has.
    X86_ARCH_OTHER,
} X86Architecture;

typedef struct X86Target X86Target;
struct X86Target
{
    uint64_t features; // Extra features above x86-64's mandatory x87/MMX/SSE/SSE2/FXSR/CMOV/CX8.
    // X86Extension's: the lowering's alone.
    uint64_t extensions;
    X86Architecture architecture;
    // Clang's CPU identity macros describe -march, even with an explicit -mtune.
    bool native_znver5;
};

typedef struct X86CpuId X86CpuId;
struct X86CpuId
{
    uint32_t words[7][4];
    uint64_t xcr0;
    bool znver5;
};

static X86Target x86_target_from_cpuid(const X86CpuId* cpu)
{
    X86Target result = { .architecture = X86_ARCH_NATIVE, .native_znver5 = cpu->znver5 };
    for (uint32_t i = 0; i < X86_FEATURE_COUNT; i += 1)
    {
        const X86FeatureDescription feature = x86_features[i];
        const uint64_t present = (cpu->words[feature.group][feature.reg] >> feature.bit) & 1;
        result.features |= present << i;
    }
    // AMD reports PREFETCHI in an extended leaf; Intel reports it in 7:1 EDX.
    result.features |= (uint64_t)((cpu->words[6][0] >> 20) & 1) << X86_FEATURE_PREFETCHI;
    const bool xsave = (cpu->words[0][2] & (3u << 26)) == (3u << 26);
    const bool avx = xsave && (cpu->words[0][2] & (1u << 28)) != 0 && (cpu->xcr0 & 6) == 6;
    const bool avx512 = avx && (result.features & BUSTER_X86_FEATURE(AVX512F)) != 0 && (cpu->xcr0 & 0xe6) == 0xe6;
    const uint64_t avx_bits = BUSTER_X86_FEATURE(AVX) | BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(F16C) | BUSTER_X86_FEATURE(FMA) | BUSTER_X86_FEATURE(AVXVNNI) | BUSTER_X86_FEATURE(VAES) | BUSTER_X86_FEATURE(VPCLMULQDQ);
    const uint64_t avx512_bits = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512CD) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(AVX512BF16) | BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512IFMA) | BUSTER_X86_FEATURE(AVX512VBMI) | BUSTER_X86_FEATURE(AVX512VBMI2) | BUSTER_X86_FEATURE(AVX512VNNI) | BUSTER_X86_FEATURE(AVX512VP2INTERSECT) | BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
    if (!avx)
    {
        // Match Clang's native policy: it exposes the XSAVE family only with usable AVX save state too.
        result.features &= ~(avx_bits | BUSTER_X86_FEATURE(XSAVE) | BUSTER_X86_FEATURE(XSAVEC) | BUSTER_X86_FEATURE(XSAVEOPT) | BUSTER_X86_FEATURE(XSAVES));
    }
    if (!avx512)
    {
        result.features &= ~avx512_bits;
    }
    // AVX512-FP16 (7:0 EDX bit 23), an extension, with the AVX-512 state, and no other: the C driver's targets take their
    // extensions from clang's resolution of the options (predefines_target), whose -march=native reads AVX-NE-CONVERT and
    // AVX10.2 from CPUID too (predefines_host).
    result.extensions = avx512 && ((cpu->words[1][3] >> 23) & 1) != 0 ? BUSTER_X86_EXTENSION(AVX512FP16) : 0;
    return result;
}

static X86Target x86_target_native(void)
{
    X86CpuId cpu = {0};
    uint32_t vendor[4];
    __cpuid_count(0, 0, vendor[0], vendor[1], vendor[2], vendor[3]);
    const uint32_t extended = __get_cpuid_max(0x80000000, 0);
    static const uint32_t leaves[] = { 1, 7, 7, 0x80000001, 0x80000008, 0xd, 0x80000021 };
    static const uint32_t subleaves[] = { 0, 0, 1, 0, 0, 1, 0 };
    for (uint32_t i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i += 1)
    {
        const uint32_t maximum = leaves[i] >= 0x80000000 ? extended : vendor[0];
        if (leaves[i] <= maximum && (i != 2 || cpu.words[1][0] >= 1))
        {
            __cpuid_count(leaves[i], subleaves[i], cpu.words[i][0], cpu.words[i][1], cpu.words[i][2], cpu.words[i][3]);
        }
    }
    if ((cpu.words[0][2] & (3u << 26)) == (3u << 26))
    {
        uint32_t low;
        uint32_t high;
        __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        cpu.xcr0 = ((uint64_t)high << 32) | low;
    }
    const uint32_t base_family = (cpu.words[0][0] >> 8) & 15;
    const uint32_t family = base_family + (base_family == 15 ? (cpu.words[0][0] >> 20) & 255 : 0);
    const uint32_t model = ((cpu.words[0][0] >> 4) & 15) | ((cpu.words[0][0] >> 12) & 0xf0);
    const bool zen5_model = model <= 0x4f || (model >= 0x60 && model <= 0x77) || (model >= 0xd0 && model <= 0xd7);
    cpu.znver5 = vendor[1] == 0x68747541 && vendor[3] == 0x69746e65 && vendor[2] == 0x444d4163 && family == 0x1a && zen5_model;
    return x86_target_from_cpuid(&cpu);
}

static bool x86_target_parse(const char* name, size_t length, X86Target* target)
{
    static const char* const names[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    bool found = false;
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i += 1)
    {
        if (length == strlen(names[i]) && memcmp(name, names[i], length) == 0)
        {
            X86Target result = { .architecture = (X86Architecture)i };
            for (uint32_t f = 0; f < X86_FEATURE_COUNT; f += 1)
            {
                result.features |= (uint64_t)(x86_features[f].level <= i + 1) << f;
            }
            *target = i == X86_ARCH_NATIVE ? x86_target_native() : result;
            found = true;
        }
    }
    return found;
}

// One output line per feature plus the architecture's three identity lines. The caller provides this much room. The
// preprocessor's macros come from c_predefines.c; benchmark-x86-target runs this as the lighter work before its tables.
#define BUSTER_X86_TARGET_PREDEFINES_BYTES (64 * (X86_FEATURE_COUNT + 3))
static __attribute__((unused)) uint32_t x86_target_predefines(const X86Target* target, char* buffer)
{
    uint32_t length = 0;
    uint64_t remaining = target->features & ((UINT64_C(1) << X86_FEATURE_COUNT) - 1);
    while (remaining != 0)
    {
        const uint32_t i = (uint32_t)__builtin_ctzll(remaining);
        remaining &= remaining - 1;
        if (x86_features[i].macro != 0)
        {
            length += (uint32_t)sprintf(buffer + length, "#define %s 1\n", x86_features[i].macro);
        }
    }
    const char* cpu = target->architecture == X86_ARCH_BASELINE ? "k8" : target->architecture == X86_ARCH_ZNVER5 || target->native_znver5 ? "znver5" : 0;
    if (cpu != 0)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __%s 1\n#define __%s__ 1\n#define __tune_%s__ 1\n", cpu, cpu, cpu);
    }
    return length;
}

// A bounded target-builtin query surface, alongside the preprocessor's existing generic table. These are Clang
// availability answers, not declarations of implemented lowering (#148). Unknown names return false.
typedef struct X86TargetBuiltin X86TargetBuiltin;
struct X86TargetBuiltin
{
    const char* name;
    uint64_t required;
};
static const X86TargetBuiltin x86_target_builtins[] = {
    { "__builtin_ia32_bextr_u32", BUSTER_X86_FEATURE(BMI) },
    { "__builtin_ia32_bextr_u64", BUSTER_X86_FEATURE(BMI) },
    { "__builtin_ia32_pdep_di", BUSTER_X86_FEATURE(BMI2) },
    { "__builtin_ia32_pext_di", BUSTER_X86_FEATURE(BMI2) },
    { "__builtin_ia32_lzcnt_u32", 0 },
    { "__builtin_ia32_lzcnt_u64", 0 },
    { "__builtin_ia32_vzeroupper", BUSTER_X86_FEATURE(AVX) },
    { "__builtin_ia32_permvarsi256", BUSTER_X86_FEATURE(AVX2) },
    { "__builtin_ia32_addps512", BUSTER_X86_FEATURE(AVX512F) },
    { "__builtin_ia32_kanddi", BUSTER_X86_FEATURE(AVX512BW) },
    { "__builtin_ia32_vgf2p8affineqb_v64qi", BUSTER_X86_FEATURE(GFNI) | BUSTER_X86_FEATURE(AVX512F) },
};

static bool x86_target_has_builtin(const X86Target* target, const uint8_t* name, uint32_t length)
{
    bool result = false;
    for (uint32_t i = 0; i < sizeof(x86_target_builtins) / sizeof(x86_target_builtins[0]); i += 1)
    {
        const X86TargetBuiltin builtin = x86_target_builtins[i];
        if (strlen(builtin.name) == length && memcmp(builtin.name, name, length) == 0)
        {
            result = (builtin.required & ~target->features) == 0;
        }
    }
    return result;
}

#endif
