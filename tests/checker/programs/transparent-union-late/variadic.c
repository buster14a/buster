// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long external_late_var_plain(union U, ...);
unsigned long external_late_var_own(union A, ...);
unsigned long (*saved)(union U, ...) = external_late_var_plain;
unsigned long (*saved_own)(union A, ...) = external_late_var_own;
unsigned long seed(union U u) { return external_late_var_plain(u, 7); }
unsigned long seed_own(union A u) { return external_late_var_own(u, 9); }
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_var_new(union U, ...);
unsigned long external_late_var_new_own(union A, ...);
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(saved(u.p) + saved_own(a.p) + external_late_var_plain(u.p, 11) + external_late_var_own(a.p, 13) +
                 external_late_var_new(u.p, 1.0) + external_late_var_new_own(a.p, 2.0));
}
