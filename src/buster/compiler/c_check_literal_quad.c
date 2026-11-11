// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by c_check_literals.c after the other source-format readers.

// On other libcs the old behavior (refusing q/Q literals) is retained. glibc
// added this symbol in 2.26; do not make every compiler build depend on it.
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 26)
#define BUSTER_CHECK_HAVE_QUAD_READER 1
#endif
#endif
#ifndef BUSTER_CHECK_HAVE_QUAD_READER
#define BUSTER_CHECK_HAVE_QUAD_READER 0
#endif

#if BUSTER_CHECK_HAVE_QUAD_READER
// glibc's binary128 reader, declared with the host compiler's type rather than depending on whether its system
// headers expose _Float128. This is the libc symbol, not libquadmath's strtoflt128; no extra library is needed.
extern __float128 check_literal_strtof128(const char* spelling, char** end) __asm__("strtof128");
#endif

// q/Q is rounded in binary128 once, by glibc's strtof128 (correctly rounded, ties to even), as clang's APFloat reads it
// (#1250): its bits, the constant's (c_check_quad.c). Reading with strtold first would round 0.1Q and 1 + 2^-112 twice.
// false where the host has no reader, which refuses the literal. The rare format stays out of line.
static __attribute__((noinline)) bool check_literal_quad(const char* spelling, char** end, unsigned __int128* bits)
{
#if BUSTER_CHECK_HAVE_QUAD_READER
    *bits = check_quad_from_host(check_literal_strtof128(spelling, end));
    return true;
#else
    (void)spelling;
    (void)end;
    *bits = 0;
    return false;
#endif
}
