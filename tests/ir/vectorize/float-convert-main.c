// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent driver for #2806's generated loops. Clang compiles this unit in both builds. Every length through
// 129, unaligned starts, overlapping SAXPY inputs, rounding boundaries and untouched destination tails enter the hash.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void saxpy_f32(float* y, const float* x, float k, size_t n);
void saxpy_f64(double* y, const double* x, double k, size_t n);
void matmul_f32(float* y, const float* a, const float* b, size_t n);
void i32_to_f32(float* d, const int32_t* a, size_t n);
void u32_to_f32(float* d, const uint32_t* a, size_t n);
void i64_to_f64(double* d, const int64_t* a, size_t n);
void u64_to_f64(double* d, const uint64_t* a, size_t n);
void f32_to_i32(int32_t* d, const float* a, size_t n);
void f32_to_u32(uint32_t* d, const float* a, size_t n);
void f64_to_i64(int64_t* d, const double* a, size_t n);
void f64_to_u64(uint64_t* d, const double* a, size_t n);

#define BUSTER_FLOAT_COUNT 144u

static uint64_t digest = UINT64_C(1469598103934665603);

static void mix(const void* data, size_t count)
{
    const unsigned char* bytes = data;
    for (size_t i = 0; i < count; i += 1)
    {
        digest = (digest ^ bytes[i]) * UINT64_C(1099511628211);
    }
}

static void exercise(size_t n, size_t offset)
{
    static const int32_t signed32[] = {
        INT32_MIN,
        INT32_MAX,
        -16777217,
        16777217,
        -1,
        0,
        1,
        33554431,
    };
    static const uint32_t unsigned32[] = {
        0,
        1,
        16777217,
        33554431,
        UINT32_C(0x80000000),
        UINT32_MAX,
        123,
        42,
    };
    static const int64_t signed64[] = {
        INT64_MIN,
        INT64_MAX,
        -INT64_C(9007199254740993),
        INT64_C(9007199254740993),
        -1,
        0,
        1,
        INT64_C(18014398509481983),
    };
    static const uint64_t unsigned64[] = {
        0,
        1,
        UINT64_C(9007199254740993),
        UINT64_C(18014398509481983),
        UINT64_C(0x8000000000000000),
        UINT64_MAX,
        123,
        42,
    };
    static const float to_signed32[] = {
        -2147483648.0f,
        2147483520.0f,
        -1.5f,
        1.5f,
        -0.5f,
        0.5f,
        -0.0f,
        0.0f,
    };
    static const float to_unsigned32[] = {
        -0.5f,
        0.5f,
        -0.0f,
        0.0f,
        4294967040.0f,
        2147483648.0f,
        123.75f,
        1.0f,
    };
    static const double to_signed64[] = {
        -9223372036854775808.0,
        9223372036854774784.0,
        -1.5,
        1.5,
        -0.5,
        0.5,
        -0.0,
        0.0,
    };
    static const double to_unsigned64[] = {
        -0.5,
        0.5,
        -0.0,
        0.0,
        18446744073709549568.0,
        9223372036854775808.0,
        123.75,
        1.0,
    };
    int32_t s32[BUSTER_FLOAT_COUNT];
    uint32_t u32[BUSTER_FLOAT_COUNT];
    int64_t s64[BUSTER_FLOAT_COUNT];
    uint64_t u64[BUSTER_FLOAT_COUNT];
    float f[BUSTER_FLOAT_COUNT];
    float g[BUSTER_FLOAT_COUNT];
    float h[BUSTER_FLOAT_COUNT];
    double d[BUSTER_FLOAT_COUNT];
    double e[BUSTER_FLOAT_COUNT];
    for (size_t i = 0; i < BUSTER_FLOAT_COUNT; i += 1)
    {
        const size_t k = (i + n) & 7;
        s32[i] = signed32[k];
        u32[i] = unsigned32[k];
        s64[i] = signed64[k];
        u64[i] = unsigned64[k];
    }
    memset(f, 0xa5, sizeof(f));
    i32_to_f32(f + offset, s32 + offset, n);
    mix(f, sizeof(f));
    u32_to_f32(f + offset, u32 + offset, n);
    mix(f, sizeof(f));
    memset(d, 0xa5, sizeof(d));
    i64_to_f64(d + offset, s64 + offset, n);
    mix(d, sizeof(d));
    u64_to_f64(d + offset, u64 + offset, n);
    mix(d, sizeof(d));
    for (size_t i = 0; i < BUSTER_FLOAT_COUNT; i += 1)
    {
        const size_t k = (i + n) & 7;
        f[i] = to_signed32[k];
        g[i] = to_unsigned32[k];
        d[i] = to_signed64[k];
        e[i] = to_unsigned64[k];
    }
    f32_to_i32(s32 + offset, f + offset, n);
    f32_to_u32(u32 + offset, g + offset, n);
    f64_to_i64(s64 + offset, d + offset, n);
    f64_to_u64(u64 + offset, e + offset, n);
    mix(s32, sizeof(s32));
    mix(u32, sizeof(u32));
    mix(s64, sizeof(s64));
    mix(u64, sizeof(u64));
    for (size_t i = 0; i < BUSTER_FLOAT_COUNT; i += 1)
    {
        f[i] = -1.0f;
        g[i] = i & 1 ? 0x1.000002p0f : -0x1.000002p0f;
        h[i] = (float)((int)(i & 7) - 3) * 0.25f;
        d[i] = -1.0;
        e[i] = i & 1 ? 0x1.0000000000001p0 : -0x1.0000000000001p0;
    }
    saxpy_f32(f + offset, g + offset, 0x1.fffffcp-1f, n);
    saxpy_f64(d + offset, e + offset, 0x1.ffffffffffffep-1, n);
    mix(f, sizeof(f));
    mix(d, sizeof(d));
    saxpy_f32(f + 1, f, 0.5f, n);
    saxpy_f64(d + 1, d, 0.5, n);
    mix(f, sizeof(f));
    mix(d, sizeof(d));
    matmul_f32(f, g, h, n % 12);
    mix(f, sizeof(f));
}

int main(void)
{
    saxpy_f32(NULL, NULL, 1.0f, 0);
    saxpy_f64(NULL, NULL, 1.0, 0);
    matmul_f32(NULL, NULL, NULL, 0);
    i32_to_f32(NULL, NULL, 0);
    u32_to_f32(NULL, NULL, 0);
    i64_to_f64(NULL, NULL, 0);
    u64_to_f64(NULL, NULL, 0);
    f32_to_i32(NULL, NULL, 0);
    f32_to_u32(NULL, NULL, 0);
    f64_to_i64(NULL, NULL, 0);
    f64_to_u64(NULL, NULL, 0);
    for (size_t n = 0; n <= 129; n += 1)
    {
        for (size_t offset = 0; offset < 4; offset += 1)
        {
            exercise(n, offset);
        }
    }
    printf("%016llx\n", (unsigned long long)digest);
    return 0;
}
