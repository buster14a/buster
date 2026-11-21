// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1844 fixture: declarators of one __auto_type declaration that deduce different types. clang names both
// deductions as each initializer spells its type, without the specifiers' qualifiers, at the __auto_type keyword, once a
// declaration, and goes on with the later declarations and statements, the mismatched declarators' uses quiet.
typedef int T;
typedef long L;
int x;
int arr[2];

int mismatched(void)
{
    __auto_type c = &x, d = 2;
    const __auto_type a = 1, b = 2.0;
    __auto_type *p = &x, q = 1.0;
    __auto_type e = 1, f = 2, g = 3.0, h = 4.0f;
    T t = 0;
    __auto_type i = t, j = 1L;
    static __auto_type volatile k = 1, l = (char)2;
    __auto_type m = arr, n = 1;
    __auto_type o = (L)1, r = (T)1;
    return d + (int)b + (int)q + (int)g + (int)h + (int)j + l + n + (int)r + undeclared;
}
