// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Real C producer acceptance for #179: functions whose only SIMD values are AVX-512 masks, the _k* builtins' rows,
// which the SSA promoter takes and Tier 1 allocates in the opmask class. Each width, each family, a loop with a phi.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

u32 masks8(u32 a, u32 b)
{
    u8 x = __builtin_ia32_kandqi((u8)a, (u8)b);
    u8 y = __builtin_ia32_kxnorqi(x, (u8)b);
    u8 z = __builtin_ia32_kaddqi(__builtin_ia32_kshiftliqi(y, 3), __builtin_ia32_knotqi(x));
    return z + __builtin_ia32_kortestzqi(x, y) + __builtin_ia32_ktestcqi(y, z);
}

u32 masks16(u32 a, u32 b)
{
    u16 x = __builtin_ia32_korhi((u16)a, (u16)b);
    u16 y = __builtin_ia32_kandnhi(x, (u16)b);
    u16 z = __builtin_ia32_kunpckhi(x, __builtin_ia32_kshiftrihi(y, 5));
    return z + __builtin_ia32_kortestchi(z, y) + __builtin_ia32_ktestzhi(x, z);
}

u32 masks32(u32 a, u32 b, u32 n)
{
    u32 x = __builtin_ia32_kxorsi(a, b);
    for (u32 i = 0; i < n; i += 1)
    {
        x = __builtin_ia32_kaddsi(x, (i & 1) != 0 ? __builtin_ia32_kshiftlisi(b, 1) : b);
    }
    return __builtin_ia32_kunpcksi(x, __builtin_ia32_knotsi(b)) + __builtin_ia32_kortestzsi(x, b);
}

u64 masks64(u64 a, u64 b)
{
    u64 x = __builtin_ia32_kanddi(a, __builtin_ia32_kmovq(b));
    u64 y = __builtin_ia32_kunpckdi(x, __builtin_ia32_kshiftridi(a, 33));
    return __builtin_ia32_kadddi(x, y) + __builtin_ia32_ktestcdi(x, y) + __builtin_ia32_kortestcdi(a, b);
}
