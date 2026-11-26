// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long early(unsigned long (*f)(union U), union U u) { return f(u); }
unsigned long early_own(unsigned long (*f)(union A), union A u) { return f(u); }
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_plain(union U);
unsigned long external_late_own(union A);
unsigned long mine(union U u) { return external_late_plain(u); }
unsigned long mine_own(union A u) { return external_late_own(u); }
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(early(mine, u) + early_own(mine_own, a) + mine(u.p) + mine_own(a.p));
}
