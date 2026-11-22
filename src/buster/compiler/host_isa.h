// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Host execution only. Generated-code ISA remains controlled by X86Target.
#ifndef BUSTER_HOST_ISA_H
#define BUSTER_HOST_ISA_H

// The host compiler's ISA: AVX-512 with these extensions, nothing narrower (the owner's decision of 2026-11-17,
// #2924). Each function has one implementation, the vector one; a build for a host without them stops here.
#if !(defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
      defined(__AVX512VBMI__) && defined(__AVX512VBMI2__) && defined(__AVX512VPOPCNTDQ__) && defined(__AVX512BITALG__) && \
      defined(__AVX512VNNI__) && defined(__GFNI__) && defined(__VAES__) && defined(__AES__) && defined(__BMI__) && \
      defined(__BMI2__) && defined(__LZCNT__) && defined(__POPCNT__) && defined(__AVX2__) && defined(__SSSE3__) && \
      defined(__SSE4_1__) && defined(__XSAVE__))
#error "buster's host needs AVX-512 F, BW, VL, DQ, CD, VBMI, VBMI2, VPOPCNTDQ, BITALG and VNNI, GFNI, VAES, AES, BMI, BMI2, LZCNT, POPCNT and XSAVE: build with BUSTER_HOST_ARCH=native on such a CPU, znver5 or vector"
#endif
// The scalar twins that remain select themselves by these until #2924 removes each.
#define BUSTER_SCALAR_PATH(requested) (requested)
#define BUSTER_VECTOR_PATH(requested) (requested)
#include <immintrin.h>
#endif
