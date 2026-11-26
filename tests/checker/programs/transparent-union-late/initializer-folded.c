// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
union U { struct { float x, y; } p; struct { int a, b; } q; };
union __attribute__((aligned(32))) A { struct { float x, y; } p; struct { int a, b; } q; };
unsigned long external_late_zero(union U);
unsigned long external_late_zero_own();
int yes(void);
int no(void);
// A folded initializer's condition takes no function address in generated code.
int (*chosen)(void) = external_late_zero ? yes : no;
// Neither an overwritten reference nor a cast's target function type is classified.
struct Keeper { unsigned long (*f)(union U); } discarded = { .f = external_late_zero, .f = 0 };
unsigned long (*saved_own)(union A) = (unsigned long (*)(union A))external_late_zero_own;
typedef union U T __attribute__((transparent_union));
typedef union A AT __attribute__((transparent_union));
unsigned long external_late_zero_own(union A);
int yes(void) { return 3; }
int no(void) { return 5; }
int tested(int n)
{
    union U u = { .q = { n, n + 1 } };
    union A a = { .q = { n + 2, n + 3 } };
    return (int)(external_late_zero(u.p) + external_late_zero_own(a.p) + saved_own(a.p)) + chosen() + (discarded.f != 0);
}
