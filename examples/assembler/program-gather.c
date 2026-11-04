// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original input for #1115's whole-program -S corpus (assembler_programs_test.c): loops that read a table through an
// index of each width, which clang compiles at -O3 -march=x86-64-v4 to AVX-512's gathers (vpgatherdd, vpgatherqd,
// vpgatherdq, vpgatherqq, vgatherdps and vgatherqpd, of vector-indexed memory); the other variants are their scalar
// loops. No generated assembly is checked in.

#include <stdio.h>

#define GATHER_COUNT 1024

static int ints[GATHER_COUNT];
static long longs[GATHER_COUNT];
static float floats[GATHER_COUNT];
static double doubles[GATHER_COUNT];
static int index32[GATHER_COUNT];
static long index64[GATHER_COUNT];
static float float_out[GATHER_COUNT];
static double double_out[GATHER_COUNT];

__attribute__((noinline)) int gather_dd(const int* table, const int* index, int count)
{
    int sum = 0;
    for (int i = 0; i < count; i += 1)
    {
        sum += table[index[i]];
    }
    return sum;
}

__attribute__((noinline)) long gather_qd(const int* table, const long* index, int count)
{
    long sum = 0;
    for (int i = 0; i < count; i += 1)
    {
        sum += table[index[i]];
    }
    return sum;
}

__attribute__((noinline)) long gather_dq(const long* table, const int* index, int count)
{
    long sum = 0;
    for (int i = 0; i < count; i += 1)
    {
        sum += table[index[i]];
    }
    return sum;
}

__attribute__((noinline)) long gather_qq(const long* table, const long* index, int count)
{
    long sum = 0;
    for (int i = 0; i < count; i += 1)
    {
        sum ^= table[index[i]] * (i + 1);
    }
    return sum;
}

__attribute__((noinline)) void gather_floats(float* restrict out, const float* restrict table, const int* restrict index, int count)
{
    for (int i = 0; i < count; i += 1)
    {
        out[i] = table[index[i]];
    }
}

__attribute__((noinline)) void gather_doubles(double* restrict out, const double* restrict table, const long* restrict index, int count)
{
    for (int i = 0; i < count; i += 1)
    {
        out[i] = table[index[i]];
    }
}

int main(void)
{
    for (int i = 0; i < GATHER_COUNT; i += 1)
    {
        ints[i] = i * 7 - 300;
        longs[i] = (long)i * 1000003 - 77;
        floats[i] = (float)(i % 17) * 0.5f;
        doubles[i] = (double)(i % 29) * 0.25;
        index32[i] = (i * 389) % GATHER_COUNT;
        index64[i] = (i * 211) % GATHER_COUNT;
    }
    const int dd = gather_dd(ints, index32, GATHER_COUNT);
    const long qd = gather_qd(ints, index64, GATHER_COUNT);
    const long dq = gather_dq(longs, index32, GATHER_COUNT);
    const long qq = gather_qq(longs, index64, GATHER_COUNT);
    gather_floats(float_out, floats, index32, GATHER_COUNT);
    gather_doubles(double_out, doubles, index64, GATHER_COUNT);
    double copied = 0;
    for (int i = 0; i < GATHER_COUNT; i += 1)
    {
        copied += (double)float_out[i] * (i % 5) + double_out[i] * (i % 3);
    }
    printf("gather: %d %ld %ld %ld %.2f\n", dd, qd, dq, qq, copied);
    // Each result is exact: the program checks them, so that a wrong gather fails it, not only its output.
    return dd == 3359232 && qd == 3359232 && dq == 523777492480L && qq == -683306822656L && copied == 11690.25 ? 0 : 1;
}
