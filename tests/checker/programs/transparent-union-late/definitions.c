// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
typedef union U Alias;
typedef union A __attribute__((aligned(64))) AlignedAlias;
unsigned long early(Alias u) { return (unsigned long)(u.q.a + u.q.b); }
unsigned long early_own(union A a) { return (unsigned long)(a.q.a + a.q.b); }
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_plain(union U);
unsigned long external_late_own(union A);
unsigned long external_late_plain_apply(unsigned long (*)(union U), int);
unsigned long external_late_own_apply(unsigned long (*)(union A), int);
long external_late_changed(int, union U);
long external_late_changed_own(int, union A);
long external_late_changed_apply(long (*)(int, union U), int);
long external_late_changed_own_apply(long (*)(int, union A), int);
unsigned long mine(Alias u) { return external_late_plain(u) + early(u); }
unsigned long mine_own(AlignedAlias a) { return external_late_own(a) + early_own(a); }
long changed(int k, union U u) { return external_late_changed(k, u); }
long changed_own(int k, union A a) { return external_late_changed_own(k, a); }
int tested(int n)
{
    union U u = { .q = { n, n + 2 } };
    union A a = { .q = { n + 3, n + 4 } };
    return (int)(mine(u.p) + mine_own(a.p) + external_late_plain_apply(mine, n) + external_late_own_apply(mine_own, n) +
                 external_late_changed_apply(changed, n) + external_late_changed_own_apply(changed_own, n));
}
