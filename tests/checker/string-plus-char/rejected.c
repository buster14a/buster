// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

void rejected(const char* p)
{
    (void)("text" + 'x');
    missing_value;
    (void)(p + 'y');
}
