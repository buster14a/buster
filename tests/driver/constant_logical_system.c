// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#include <constant_logical_system.h>

int logical_system_user(int x)
{
    return logical_system_and(x) + logical_system_or(x) + (x && 32);
}
