// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1844 fixture: C23's auto, whose declarators that deduce differently clang names as __auto_type's.
int x;
void f(void)
{
    const auto a = 1, b = 2.0;
    static auto c = &x, d = 1, e = 2.0;
    auto g = 1;
    b = g + undeclared;
}
