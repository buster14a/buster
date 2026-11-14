// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef _Atomic(int) Atomic;
int unary(Record s, Atomic x)
{
    return +s.value - s.value + !x + ~x;
}
int binary(Record s, int x)
{
    return (s.value < x) + (x >= s.value) + (s.value && x);
}
Record conditional(Record s, Record t, int x)
{
    return x ? s : t;
}
int pointers(Record s, Atomic x)
{
    Record *p = &s;
    Atomic *q = &x;
    return p[0].value + *q;
}
int conditions(Record s, Atomic x)
{
    if (s.value) { return 1; }
    while (x) { return 2; }
    do { s.value++; } while (s.value < 0);
    for (; x;) { return 3; }
    switch (x) { case 1: return 4; }
    return 0;
}
_Static_assert(sizeof(Atomic) == sizeof(int), "atomic representation");
_Static_assert(0.5, "GNU floating assertion");
_Static_assert(-0.5, "GNU negative floating assertion");
_Static_assert(!(-0.0), "negative zero remains false");
int object;
int *address_constant = &object;
float floating_constant = 0.5;
