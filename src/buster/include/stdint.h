/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stdint.h> (C23 7.22), buster's: the integer types of given widths, their limits and their constants' macros, from
   the target's predefined macros, as clang's <stdint.h> gives them. A hosted implementation's <stdint.h> is used
   instead when there is one, as clang does.

   The target (x86-64) has the exact widths 8, 16, 32 and 64, so each least and fast type is the exact type of its
   width, as clang makes them. glibc's headers declare some of these types too, and say so with the guards
   __int8_t_defined (the signed exact types), __uint32_t_defined and __intptr_t_defined, which this header keeps. */

#ifndef __BUSTER_STDINT_H
#define __BUSTER_STDINT_H

#if __STDC_HOSTED__ && __has_include_next(<stdint.h>)
#include_next <stdint.h>
#else

#ifndef __int8_t_defined
typedef __INT8_TYPE__ int8_t;
typedef __INT16_TYPE__ int16_t;
typedef __INT32_TYPE__ int32_t;
typedef __INT64_TYPE__ int64_t;
#define __int8_t_defined
#endif
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
#ifndef __uint32_t_defined
#define __uint32_t_defined
typedef __UINT32_TYPE__ uint32_t;
#endif
typedef __UINT64_TYPE__ uint64_t;

typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;
typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef int16_t int_fast16_t;
typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t;
typedef uint8_t uint_fast8_t;
typedef uint16_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

#ifndef _INTPTR_T
#ifndef __intptr_t_defined
typedef __INTPTR_TYPE__ intptr_t;
#define __intptr_t_defined
#define _INTPTR_T
#endif
#endif
#ifndef _UINTPTR_T
typedef __UINTPTR_TYPE__ uintptr_t;
#define _UINTPTR_T
#endif

typedef __INTMAX_TYPE__ intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

/* The constants' macros, which give an integer constant the type its width promotes to. */
#define INT8_C(v) __INT8_C(v)
#define INT16_C(v) __INT16_C(v)
#define INT32_C(v) __INT32_C(v)
#define INT64_C(v) __INT64_C(v)
#define UINT8_C(v) __UINT8_C(v)
#define UINT16_C(v) __UINT16_C(v)
#define UINT32_C(v) __UINT32_C(v)
#define UINT64_C(v) __UINT64_C(v)
#define INTMAX_C(v) __INTMAX_C(v)
#define UINTMAX_C(v) __UINTMAX_C(v)

/* The limits: each type's maximum, written in its own type, and the signed minimums one below the negated maximums. */
#define INT8_MAX INT8_C(127)
#define INT8_MIN (-INT8_C(127) - 1)
#define UINT8_MAX UINT8_C(255)
#define INT16_MAX INT16_C(32767)
#define INT16_MIN (-INT16_C(32767) - 1)
#define UINT16_MAX UINT16_C(65535)
#define INT32_MAX INT32_C(2147483647)
#define INT32_MIN (-INT32_C(2147483647) - 1)
#define UINT32_MAX UINT32_C(4294967295)
#define INT64_MAX INT64_C(9223372036854775807)
#define INT64_MIN (-INT64_C(9223372036854775807) - 1)
#define UINT64_MAX UINT64_C(18446744073709551615)

#define INT_LEAST8_MAX INT8_MAX
#define INT_LEAST8_MIN INT8_MIN
#define UINT_LEAST8_MAX UINT8_MAX
#define INT_LEAST16_MAX INT16_MAX
#define INT_LEAST16_MIN INT16_MIN
#define UINT_LEAST16_MAX UINT16_MAX
#define INT_LEAST32_MAX INT32_MAX
#define INT_LEAST32_MIN INT32_MIN
#define UINT_LEAST32_MAX UINT32_MAX
#define INT_LEAST64_MAX INT64_MAX
#define INT_LEAST64_MIN INT64_MIN
#define UINT_LEAST64_MAX UINT64_MAX

#define INT_FAST8_MAX INT8_MAX
#define INT_FAST8_MIN INT8_MIN
#define UINT_FAST8_MAX UINT8_MAX
#define INT_FAST16_MAX INT16_MAX
#define INT_FAST16_MIN INT16_MIN
#define UINT_FAST16_MAX UINT16_MAX
#define INT_FAST32_MAX INT32_MAX
#define INT_FAST32_MIN INT32_MIN
#define UINT_FAST32_MAX UINT32_MAX
#define INT_FAST64_MAX INT64_MAX
#define INT_FAST64_MIN INT64_MIN
#define UINT_FAST64_MAX UINT64_MAX

#define INTPTR_MAX __INTPTR_MAX__
#define INTPTR_MIN (-__INTPTR_MAX__ - 1)
#define UINTPTR_MAX __UINTPTR_MAX__
#define INTMAX_MAX __INTMAX_MAX__
#define INTMAX_MIN (-__INTMAX_MAX__ - 1)
#define UINTMAX_MAX __UINTMAX_MAX__

#define PTRDIFF_MAX __PTRDIFF_MAX__
#define PTRDIFF_MIN (-__PTRDIFF_MAX__ - 1)
#define SIG_ATOMIC_MAX __SIG_ATOMIC_MAX__
#define SIG_ATOMIC_MIN __SIG_ATOMIC_MIN__
#define SIZE_MAX __SIZE_MAX__
#define WCHAR_MAX __WCHAR_MAX__
#define WCHAR_MIN __WCHAR_MIN__
#define WINT_MAX __WINT_MAX__
#define WINT_MIN __WINT_MIN__

/* Annex K's, on request. */
#if defined(__STDC_WANT_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__ >= 1
#define RSIZE_MAX (SIZE_MAX >> 1)
#endif

/* The widths, C23's. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define INT8_WIDTH 8
#define UINT8_WIDTH 8
#define INT16_WIDTH 16
#define UINT16_WIDTH 16
#define INT32_WIDTH 32
#define UINT32_WIDTH 32
#define INT64_WIDTH 64
#define UINT64_WIDTH 64
#define INT_LEAST8_WIDTH 8
#define UINT_LEAST8_WIDTH 8
#define INT_LEAST16_WIDTH 16
#define UINT_LEAST16_WIDTH 16
#define INT_LEAST32_WIDTH 32
#define UINT_LEAST32_WIDTH 32
#define INT_LEAST64_WIDTH 64
#define UINT_LEAST64_WIDTH 64
#define INT_FAST8_WIDTH 8
#define UINT_FAST8_WIDTH 8
#define INT_FAST16_WIDTH 16
#define UINT_FAST16_WIDTH 16
#define INT_FAST32_WIDTH 32
#define UINT_FAST32_WIDTH 32
#define INT_FAST64_WIDTH 64
#define UINT_FAST64_WIDTH 64
#define INTPTR_WIDTH __INTPTR_WIDTH__
#define UINTPTR_WIDTH __UINTPTR_WIDTH__
#define INTMAX_WIDTH __INTMAX_WIDTH__
#define UINTMAX_WIDTH __UINTMAX_WIDTH__
#define PTRDIFF_WIDTH __PTRDIFF_WIDTH__
#define SIG_ATOMIC_WIDTH __SIG_ATOMIC_WIDTH__
#define SIZE_WIDTH __SIZE_WIDTH__
#define WCHAR_WIDTH __WCHAR_WIDTH__
#define WINT_WIDTH __WINT_WIDTH__
#endif

#endif

#endif
