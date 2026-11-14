// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

int before = undeclared_global;
_Static_assert(0 << 3, "after error");
void assertion_recovery(void)
{
    _Static_assert(0 << 2, "body");
    int value = undeclared_local;
}
_Static_assert(1 << 2, "success");
