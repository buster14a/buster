// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Real C producer acceptance for #141's intrinsics: the x86 builtins' scalar bit manipulations, BMI2's pdep, pext and
// bzhi and BMI1's bextr of unsigned ints and longs, whose rows the typed stream's lowering makes the IR's intrinsics.
// Each width, results fed to each other, a loop with a phi, and the elementwise deposit of a vector.
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u32 v4u __attribute__((vector_size(16)));

u32 bits32(u32 a, u32 m, u32 n)
{
    u32 d = __builtin_ia32_pdep_si(a, m);
    u32 e = __builtin_ia32_pext_si(d ^ a, m | a);
    return __builtin_ia32_bzhi_si(e + d, n) + __builtin_ia32_bextr_u32(a, n | (n << 8));
}

u64 bits64(u64 a, u64 m, u64 n)
{
    u64 d = __builtin_ia32_pdep_di(a, m);
    u64 e = __builtin_ia32_pext_di(d ^ a, m | a);
    return __builtin_ia32_bzhi_di(e + d, n) + __builtin_ia32_bextr_u64(a, n | (n << 8));
}

u64 morton(u64 a, u32 count)
{
    u64 sum = 0;
    for (u32 i = 0; i < count; i += 1)
    {
        sum += __builtin_ia32_pext_di(a + i, 0x5555555555555555ull) ^ __builtin_ia32_pdep_di(sum, 0xaaaaaaaaaaaaaaaaull);
    }
    return sum;
}

v4u deposit(v4u a, v4u m)
{
    return __builtin_elementwise_pdep(a, m);
}
