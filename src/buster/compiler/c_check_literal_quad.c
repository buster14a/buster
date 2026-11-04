// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by c_check_literals.c after the other source-format readers.

// glibc's binary128 reader, declared with the host compiler's type rather than depending on whether its system
// headers expose _Float128. This is the libc symbol, not libquadmath's strtoflt128; no extra library is needed.
extern __float128 check_literal_strtof128(const char* spelling, char** end) __asm__("strtof128");

// q/Q is rounded in binary128 first (#1250). CheckLiteral and CheckValue still hold x87 values: accept only when
// that storage preserves the rounded binary128 result exactly. Reading with strtold first would silently turn
// 0.1Q and 1 + 2^-112 into different constants. Wider values remain explicitly unsupported until their whole path,
// including folding and static storage, retains 113 significant bits. The rare format stays out of line.
static __attribute__((noinline)) bool check_literal_quad(const char* spelling, char** end, long double* value)
{
    const __float128 quad = check_literal_strtof128(spelling, end);
    const long double narrowed = (long double)quad;
    const bool exact = (__float128)narrowed == quad;
    if (exact)
    {
        *value = narrowed;
    }
    return exact;
}
