// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long external_late_zero(union U);
unsigned long external_late_zero_own(union A);
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    unsigned long x = external_late_zero(u) + external_late_zero_own(a);
    typedef union U T __attribute__((transparent_union));
    typedef union A AT __attribute__((transparent_union));
    return (int)(x + external_late_zero(u.p) + external_late_zero_own(a.p));
}
