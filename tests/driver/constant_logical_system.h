// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#ifndef BUSTER_TEST_CONSTANT_LOGICAL_SYSTEM_H
#define BUSTER_TEST_CONSTANT_LOGICAL_SYSTEM_H

#pragma GCC system_header

static inline int logical_system_and(int x)
{
    return x && 4;
}

static inline int logical_system_or(int x)
{
    return x || 8;
}

_Static_assert(1 && 16, "system header assertion passes");

#endif
