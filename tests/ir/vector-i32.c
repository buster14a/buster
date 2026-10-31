// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Real C producer acceptance for #732. No native vector ABI: pointers carry the objects.
typedef int V4 __attribute__((vector_size(16)));
typedef unsigned int V8 __attribute__((vector_size(32)));
typedef int V16 __attribute__((vector_size(64)));

void vector4(V4* out, const V4* a, const V4* b, int choose)
{
    V4 x = *a;
    V4 y = *b;
    V4 z = choose ? x + y : x - y;
    *out = (z < y) ^ ~x;
}

void vector8(V8* out, const V8* a, const V8* b, int choose)
{
    V8 x = *a;
    V8 y = *b;
    V8 z = choose ? x * y : x | y;
    *out = (z >= y) & ~x;
}

void vector16(V16* out, const V16* a, const V16* b, int choose)
{
    V16 x = *a;
    V16 y = *b;
    V16 z = choose ? x ^ y : x & y;
    *out = (z != y) | ~x;
}
