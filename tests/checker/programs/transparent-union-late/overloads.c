// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long choose(union U) __asm__("external_late_plain") __attribute__((overloadable));
unsigned long choose(union A) __asm__("external_late_own") __attribute__((overloadable));
unsigned long choose(long) __attribute__((overloadable));
unsigned long (*saved)(union U) = choose;
struct Keeper { unsigned long (*f)(union A); } keeper = { choose };
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_plain_apply(unsigned long (*)(union U), int);
unsigned long external_late_own_apply(unsigned long (*)(union A), int);
unsigned long mine(union U u) { return saved(u); }
unsigned long mine_own(union A u) { return keeper.f(u); }
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(saved(u.p) + keeper.f(a.p) + external_late_plain_apply(mine, n) + external_late_own_apply(mine_own, n));
}
