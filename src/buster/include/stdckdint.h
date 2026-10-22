/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stdckdint.h> (C23 7.20), buster's: checked integer arithmetic through the compiler's overflow builtins, which take
   the operands first and the result's address last. A hosted implementation's own header is used when there is
   one, as clang does. */

#ifndef __STDCKDINT_H
#define __STDCKDINT_H

#if __STDC_HOSTED__ && __has_include_next(<stdckdint.h>)
#include_next <stdckdint.h>
#else
#define __STDC_VERSION_STDCKDINT_H__ 202311L
#define ckd_add(R, A, B) __builtin_add_overflow((A), (B), (R))
#define ckd_sub(R, A, B) __builtin_sub_overflow((A), (B), (R))
#define ckd_mul(R, A, B) __builtin_mul_overflow((A), (B), (R))
#endif

#endif
