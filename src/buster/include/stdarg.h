/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stdarg.h> (C23 7.16), buster's: variable arguments through the compiler's builtins, as clang's <stdarg.h> gives
   them in each language mode.

   A header that wants some parts alone defines __need___va_list (the type as __gnuc_va_list, which glibc's headers
   use), __need_va_list, __need_va_arg (va_start, va_arg and va_end), __need___va_copy or __need_va_copy before
   including this one; each part has a guard of its own. va_copy is C99's, and GNU C's in any mode. */

#if !defined(__need___va_list) && !defined(__need_va_list) && !defined(__need_va_arg) && !defined(__need___va_copy) &&  \
    !defined(__need_va_copy)
#define __need___va_list
#define __need_va_list
#define __need_va_arg
#define __need___va_copy
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || !defined(__STRICT_ANSI__)
#define __need_va_copy
#endif
#ifndef __STDARG_H
#define __STDARG_H
#endif
#endif

#ifdef __need___va_list
#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif
#undef __need___va_list
#endif

#ifdef __need_va_list
#ifndef _VA_LIST
#define _VA_LIST
typedef __builtin_va_list va_list;
#endif
#undef __need_va_list
#endif

/* C23's va_start takes the list alone (C23 7.16.2.5): anything after it is not evaluated. */
#ifdef __need_va_arg
#ifndef va_arg
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define va_start(...) __builtin_c23_va_start(__VA_ARGS__)
#else
#define va_start(ap, param) __builtin_va_start(ap, param)
#endif
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#endif
#undef __need_va_arg
#endif

#ifdef __need___va_copy
#ifndef __va_copy
#define __va_copy(d, s) __builtin_va_copy(d, s)
#endif
#undef __need___va_copy
#endif

#ifdef __need_va_copy
#ifndef va_copy
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#endif
#undef __need_va_copy
#endif
