/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stdnoreturn.h> (C11 7.23), buster's: noreturn for the _Noreturn keyword. C23 makes the header obsolescent, but
   clang still defines the macro there, without a warning, and so does this. */

#ifndef __STDNORETURN_H
#define __STDNORETURN_H

#define noreturn _Noreturn
#define __noreturn_is_defined 1

#endif
