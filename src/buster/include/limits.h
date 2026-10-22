/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <limits.h> (C23 5.2.5.3.2 and 7.10), buster's: the sizes of the integer types, from the target's predefined macros, as
   clang's <limits.h> gives them in each language mode.

   A hosted implementation's <limits.h> comes first, for what POSIX adds (and its MB_LEN_MAX); then this header's own
   limits replace its. glibc's <limits.h> includes the compiler's again with #include_next unless _GCC_LIMITS_H_ (GCC's
   header's guard) is defined, so it is defined first. */

#ifndef __BUSTER_LIMITS_H
#define __BUSTER_LIMITS_H

#if defined(__GNUC__) && !defined(_GCC_LIMITS_H_)
#define _GCC_LIMITS_H_
#endif

#if __STDC_HOSTED__ && __has_include_next(<limits.h>)
#include_next <limits.h>
#endif

#undef CHAR_BIT
#undef SCHAR_MIN
#undef SCHAR_MAX
#undef UCHAR_MAX
#undef CHAR_MIN
#undef CHAR_MAX
#undef SHRT_MIN
#undef SHRT_MAX
#undef USHRT_MAX
#undef INT_MIN
#undef INT_MAX
#undef UINT_MAX
#undef LONG_MIN
#undef LONG_MAX
#undef ULONG_MAX

#define CHAR_BIT __CHAR_BIT__

/* The minimums are one below the negated maximums, in the types of the maximums. The unsigned maximums are twice the
   signed ones and one: in int for unsigned char, and for unsigned short where int is wider than short. */
#define SCHAR_MAX __SCHAR_MAX__
#define SCHAR_MIN (-__SCHAR_MAX__ - 1)
#define UCHAR_MAX (__SCHAR_MAX__ * 2 + 1)
#define SHRT_MAX __SHRT_MAX__
#define SHRT_MIN (-__SHRT_MAX__ - 1)
#if __SHRT_WIDTH__ < __INT_WIDTH__
#define USHRT_MAX (__SHRT_MAX__ * 2 + 1)
#else
#define USHRT_MAX (__SHRT_MAX__ * 2U + 1U)
#endif
#define INT_MAX __INT_MAX__
#define INT_MIN (-__INT_MAX__ - 1)
#define UINT_MAX (__INT_MAX__ * 2U + 1U)
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-__LONG_MAX__ - 1L)
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)

/* Plain char is signed unless -funsigned-char. */
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX __SCHAR_MAX__
#endif

/* The longest multibyte character in any locale, if the hosted implementation didn't say. */
#ifndef MB_LEN_MAX
#define MB_LEN_MAX 1
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#undef LLONG_MIN
#undef LLONG_MAX
#undef ULLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-__LONG_LONG_MAX__ - 1LL)
#define ULLONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1ULL)
#endif

/* The widths, C23's. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define BOOL_WIDTH __BOOL_WIDTH__
#define CHAR_WIDTH CHAR_BIT
#define SCHAR_WIDTH CHAR_BIT
#define UCHAR_WIDTH CHAR_BIT
#define SHRT_WIDTH __SHRT_WIDTH__
#define USHRT_WIDTH __SHRT_WIDTH__
#define INT_WIDTH __INT_WIDTH__
#define UINT_WIDTH __INT_WIDTH__
#define LONG_WIDTH __LONG_WIDTH__
#define ULONG_WIDTH __LONG_WIDTH__
#define LLONG_WIDTH __LLONG_WIDTH__
#define ULLONG_WIDTH __LLONG_WIDTH__
#define BITINT_MAXWIDTH __BITINT_MAXWIDTH__
#endif

/* GNU's older names for the long long limits: glibc's _GNU_SOURCE asks for them, and so does a GNU mode elsewhere. */
#if defined(__GNU_LIBRARY__) ? defined(__USE_GNU) : !defined(__STRICT_ANSI__)
#undef LONG_LONG_MIN
#undef LONG_LONG_MAX
#undef ULONG_LONG_MAX
#define LONG_LONG_MAX __LONG_LONG_MAX__
#define LONG_LONG_MIN (-__LONG_LONG_MAX__ - 1LL)
#define ULONG_LONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1ULL)
#endif

#endif
