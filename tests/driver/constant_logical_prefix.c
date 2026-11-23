// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int logical_valid_prefix(int x)
{
    return x && 4;
}

_Static_assert(1 || 8, "valid prefix assertion");

int logical_initialized_prefix = 1 && 16;

int logical_malformed_after_prefix = ;
