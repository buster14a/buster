/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stddef.h> (C23 7.21), buster's: the common definitions, from the target's predefined macros, as clang's <stddef.h>
   gives them in each language mode.

   A header that wants some of them alone defines __need_ptrdiff_t, __need_size_t, __need_rsize_t, __need_wchar_t,
   __need_NULL, __need_nullptr_t, __need_unreachable, __need_max_align_t, __need_offsetof or __need_wint_t before
   including this one, as glibc's headers do: then only those parts are given, and the header can be included again
   for others. Each part has a guard of its own (_SIZE_T and the like, the names clang and GCC use), so a type is
   declared once whichever way it comes. wint_t is <wchar.h>'s, so it only comes on request. */

#if !defined(__need_ptrdiff_t) && !defined(__need_size_t) && !defined(__need_rsize_t) && !defined(__need_wchar_t) &&    \
    !defined(__need_NULL) && !defined(__need_nullptr_t) && !defined(__need_unreachable) &&                             \
    !defined(__need_max_align_t) && !defined(__need_offsetof) && !defined(__need_wint_t)
/* The whole header: the parts the language mode has. rsize_t is Annex K's, there when asked for; NULL is given the
   first time only. */
#define __need_ptrdiff_t
#define __need_size_t
#if defined(__STDC_WANT_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__ >= 1
#define __need_rsize_t
#endif
#define __need_wchar_t
#ifndef __STDDEF_H
#define __need_NULL
#endif
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define __need_nullptr_t
#define __need_unreachable
#endif
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define __need_max_align_t
#endif
#define __need_offsetof
#ifndef __STDDEF_H
#define __STDDEF_H
#endif
#endif

#ifdef __need_ptrdiff_t
#ifndef _PTRDIFF_T
#define _PTRDIFF_T
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#endif
#undef __need_ptrdiff_t
#endif

#ifdef __need_size_t
#ifndef _SIZE_T
#define _SIZE_T
typedef __SIZE_TYPE__ size_t;
#endif
#undef __need_size_t
#endif

#ifdef __need_rsize_t
#ifndef _RSIZE_T
#define _RSIZE_T
typedef __SIZE_TYPE__ rsize_t;
#endif
#undef __need_rsize_t
#endif

#ifdef __need_wchar_t
#ifndef _WCHAR_T
#define _WCHAR_T
typedef __WCHAR_TYPE__ wchar_t;
#endif
#undef __need_wchar_t
#endif

#ifdef __need_NULL
#undef NULL
#define NULL ((void*)0)
#undef __need_NULL
#endif

/* nullptr_t's guard is defined whenever it is asked for; the type only exists from C23 on. */
#ifdef __need_nullptr_t
#ifndef _NULLPTR_T
#define _NULLPTR_T
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
typedef typeof(nullptr) nullptr_t;
#endif
#endif
#undef __need_nullptr_t
#endif

#ifdef __need_unreachable
#ifndef unreachable
#define unreachable() __builtin_unreachable()
#endif
#undef __need_unreachable
#endif

/* The type of the strictest fundamental alignment, laid out as GCC's is: long long's and long double's together. */
#ifdef __need_max_align_t
#ifndef __BUSTER_MAX_ALIGN_T_DEFINED
#define __BUSTER_MAX_ALIGN_T_DEFINED
typedef struct
{
    long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
    long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;
#endif
#undef __need_max_align_t
#endif

#ifdef __need_offsetof
#ifndef offsetof
#define offsetof(t, d) __builtin_offsetof(t, d)
#endif
#undef __need_offsetof
#endif

#ifdef __need_wint_t
#ifndef _WINT_T
#define _WINT_T
typedef __WINT_TYPE__ wint_t;
#endif
#undef __need_wint_t
#endif
