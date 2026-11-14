// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
struct S { int value; };
typedef struct S Record;
typedef _Atomic(int) Atomic;
Record make(void);
void record(Record s, int x, int *p)
{
    (void)*s;
    (void)*(_Atomic int)x;
    (void)&make();
    (void)&(_Atomic int)x;
    (void)&(Atomic)x;
    (void)&(x + 1);
    (void)s[1];
    (void)1[s];
    (void)p[s];
    (void)s[p];
    (void)p[(Atomic)x];
    (x + 1) = 2;
    (_Atomic int)x = 1;
    (Atomic)x += 2;
}
void atomic_lvalue(Atomic a, _Atomic(int *) p, Record s)
{
    (void)*a;
    (void)p[s];
    (void)s[p];
}
