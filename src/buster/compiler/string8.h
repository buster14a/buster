// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The bounded byte slice the compiler's modules share for names and source text. A unit that includes a module whose
// contract takes the type supplies it before the module; c_dependency.c is the one that documents this.

#ifndef BUSTER_STRING8_INCLUDED
#define BUSTER_STRING8_INCLUDED

#include <stdint.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#endif
