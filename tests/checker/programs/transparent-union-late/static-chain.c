// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long early(unsigned long (*f)(union U), union U u, void *chain)
{
    return __builtin_call_with_static_chain(f(u), chain);
}
unsigned long early_own(unsigned long (*f)(union A), union A a, void *chain)
{
    return __builtin_call_with_static_chain(f(a), chain);
}
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_plain(union U);
unsigned long external_late_own(union A);
unsigned long external_late_zero(union U);
unsigned long external_late_zero_own(union A);
unsigned long later(unsigned long (*f)(union U), union U u, void *chain)
{
    return __builtin_call_with_static_chain(f(u), chain);
}
unsigned long later_own(unsigned long (*f)(union A), union A a, void *chain)
{
    return __builtin_call_with_static_chain(f(a), chain);
}
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(early(external_late_plain, u.p, 0) + early_own(external_late_own, a.p, 0) +
                 later(external_late_plain, u.p, 0) + later_own(external_late_own, a.p, 0) +
                 external_late_zero(u.p) + external_late_zero_own(a.p));
}
