// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Real C producer acceptance for #141's machine rows: the vector operators on elements other than 32-bit integers and the
// vector builtins, whose X86 rows the typed stream's lowering makes the IR's row calls (IrFunction.rows). Bytes, words and
// quadwords, a compare into a mask (the IR's own vcmp since #1623, ir_simd_typed_compare_mask), a masked operation, a
// masked store of no result and a masked load (the IR's own masked_store and masked_load since #1840), a shift by a
// count from a general register, a permute, and a loop whose row results cross its back edge as phis.
typedef char v64i8 __attribute__((vector_size(64)));
typedef short v32i16 __attribute__((vector_size(64)));
typedef long long v8i64 __attribute__((vector_size(64)));
typedef unsigned long long u64;

v64i8 bytes(v64i8 a, v64i8 b)
{
    return a + b - (a & b);
}

v32i16 words(v32i16 a, v32i16 b)
{
    return a * b + (v32i16)__builtin_ia32_psrlwi512((v32i16)a, 3);
}

v8i64 quads(v8i64 a, v8i64 b, int n)
{
    return a + __builtin_ia32_psrlqi512(b, n);
}

u64 compare(v64i8 a, v64i8 b)
{
    return __builtin_ia32_cmpb512_mask(a, b, 0, (u64)-1);
}

void store(v64i8* p, v64i8 a, v64i8 b, u64 k)
{
    __builtin_ia32_storedquqi512_mask(p, a + b, k);
}

v64i8 load(const v64i8* p, v64i8 w, u64 k)
{
    return __builtin_ia32_loaddquqi512_mask(p, w, k) + w;
}

v8i64 permute(v8i64 a, v8i64 i, v8i64 b)
{
    return __builtin_ia32_vpermi2varq512(a, i, b);
}

u64 count(const v64i8* p, int n, char c)
{
    v64i8 sum = { 0 };
    u64 matches = 0;
    for (int i = 0; i < n; i += 1)
    {
        v64i8 eq = (v64i8)(p[i] == c);
        sum = sum - eq;
        matches += __builtin_popcountll(__builtin_ia32_cmpb512_mask(p[i], sum, 4, (u64)-1));
    }
    return matches + (u64)sum[0];
}
