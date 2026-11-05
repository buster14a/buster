// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Real C producer acceptance for #1280's SSE carriers: vectors of 4 and 8 bytes, an f32 or f64 in the IR, which the
// typed stream's machine rows read and make. Narrowing shuffles into them and widening conversions out of them (the
// intrinsics' cvtepu8 and cvtepu16), loads and stores of their bytes, constants, operators on them, a 4-byte vector beside
// an 8-byte one, and a loop whose carriers cross its back edge as phis.
typedef unsigned char v16u8 __attribute__((vector_size(16)));
typedef unsigned char v8u8 __attribute__((vector_size(8)));
typedef unsigned char v4u8 __attribute__((vector_size(4)));
typedef unsigned short v8u16 __attribute__((vector_size(16)));
typedef unsigned short v4u16 __attribute__((vector_size(8)));
typedef int v8i32 __attribute__((vector_size(32)));
typedef int v4i32 __attribute__((vector_size(16)));
typedef unsigned long long u64;
typedef unsigned u32;

v8i32 widen(v16u8 a)
{
    return __builtin_convertvector(__builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7), v8i32);
}

v4i32 widen4(v16u8 a, v8u16 b)
{
    v4i32 x = __builtin_convertvector(__builtin_shufflevector(a, a, 0, 1, 2, 3), v4i32);
    v4i32 y = __builtin_convertvector(__builtin_shufflevector(b, b, 0, 1, 2, 3), v4i32);
    return x + y;
}

void copy(v8u8* out, const v8u8* in, v4u8* small, const v4u8* source)
{
    *out = *in + (v8u8){ 1, 2, 3, 4, 5, 6, 7, 8 };
    *small = *source ^ (v4u8){ 0xff, 0x80, 0x01, 0x7f };
}

u64 operators(const v4u16* p, const v4u16* q)
{
    v4u16 x = (*p + *q) & (v4u16){ 0xffff, 0x00ff, 0xff00, 0x0f0f };
    return (u64)(x - *q);
}

u64 loop(const v8u8* p, int n)
{
    v8u8 sum = { 0 };
    v4u8 low = { 0 };
    for (int i = 0; i < n; i += 1)
    {
        sum = sum + p[i];
        low = low ^ __builtin_shufflevector(p[i], p[i], 0, 1, 2, 3);
    }
    u32 bits;
    __builtin_memcpy(&bits, &low, 4);
    return (u64)sum + bits;
}
