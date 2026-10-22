/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stdalign.h> (C11 7.15), buster's. In C11 and C17, alignas and alignof are macros for the keywords _Alignas and
   _Alignof. C23 makes them keywords, and the header empty. As in clang, C89, which has no __STDC_VERSION__, gets
   nothing either. */

#ifndef __STDALIGN_H
#define __STDALIGN_H

#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 202311L
#define alignas _Alignas
#define alignof _Alignof
#define __alignas_is_defined 1
#define __alignof_is_defined 1
#endif

#endif
