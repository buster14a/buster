// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #2806: compile this unit with Tier 1 and with Clang, then link the same driver to each object.
#include <stddef.h>
#include <stdint.h>

void saxpy_f32(float* y, const float* x, float k, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        y[i] += k * x[i];
    }
}

void saxpy_f64(double* y, const double* x, double k, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        y[i] += k * x[i];
    }
}

void matmul_f32(float* y, const float* a, const float* b, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        for (size_t k = 0; k < n; k += 1)
        {
            const float factor = a[i * n + k];
            for (size_t j = 0; j < n; j += 1)
            {
                y[i * n + j] += factor * b[k * n + j];
            }
        }
    }
}

void i32_to_f32(float* d, const int32_t* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (float)a[i];
    }
}

void u32_to_f32(float* d, const uint32_t* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (float)a[i];
    }
}

void i64_to_f64(double* d, const int64_t* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (double)a[i];
    }
}

void u64_to_f64(double* d, const uint64_t* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (double)a[i];
    }
}

void f32_to_i32(int32_t* d, const float* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (int32_t)a[i];
    }
}

void f32_to_u32(uint32_t* d, const float* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (uint32_t)a[i];
    }
}

void f64_to_i64(int64_t* d, const double* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (int64_t)a[i];
    }
}

void f64_to_u64(uint64_t* d, const double* a, size_t n)
{
    for (size_t i = 0; i < n; i += 1)
    {
        d[i] = (uint64_t)a[i];
    }
}
