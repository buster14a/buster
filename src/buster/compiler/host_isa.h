// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Host execution only. Generated-code ISA remains controlled by X86Target.
#ifndef BUSTER_HOST_ISA_H
#define BUSTER_HOST_ISA_H

#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
    defined(__AVX512VBMI__) && defined(__AVX512VBMI2__) && defined(__AVX512VPOPCNTDQ__) && defined(__AVX512BITALG__) && \
    defined(__AVX512VNNI__) && defined(__GFNI__) && defined(__VAES__) && defined(__AES__) && defined(__BMI__) && \
    defined(__BMI2__) && defined(__LZCNT__) && defined(__POPCNT__) && defined(__AVX2__) && defined(__SSSE3__) && \
    defined(__SSE4_1__) && defined(__XSAVE__)
#define BUSTER_HOST_VECTOR 1
#define BUSTER_SCALAR_PATH(requested) (requested)
#define BUSTER_VECTOR_PATH(requested) (requested)
#else
#define BUSTER_HOST_VECTOR 0
#define BUSTER_SCALAR_PATH(requested) 1
#define BUSTER_VECTOR_PATH(requested) 0
#endif
#include <immintrin.h>
#if !BUSTER_HOST_VECTOR
#include "host_scalar.h"
#include "host_scalar_vectors.h"
#endif
#endif
