// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long external_late_zero(union U);
unsigned long external_late_zero_own(union A);
static unsigned long (*saved)(union U) = external_late_zero;
static unsigned long (*saved_own)(union A) = external_late_zero_own;
int unrelated(int x) { return x + 1; }
int pointer_only(unsigned long (*f)(union U)) { return f != 0; }
int never(union U u, union A a)
{
    if (0) return (int)(external_late_zero(u) + external_late_zero_own(a));
    return sizeof(&external_late_zero) == sizeof(&external_late_zero_own);
}
static unsigned long deferred(union U u) { return saved(u); }
static unsigned long deferred_own(union A a) { return saved_own(a); }
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(deferred(u) + deferred_own(a) + saved(u.p) + saved_own(a.p)) + unrelated(n) + pointer_only(saved) + never(u, a);
}
