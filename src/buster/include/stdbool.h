/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md. */

/* <stdbool.h> (C23 7.19), buster's. Before C23, bool, true and false are macros; from C23 on they are keywords, and
   the header defines only its obsolescent __bool_true_false_are_defined. */

#ifndef __STDBOOL_H
#define __STDBOOL_H

#define __bool_true_false_are_defined 1

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ <= 201710L
#define bool _Bool
#define true 1
#define false 0
#endif

#endif
